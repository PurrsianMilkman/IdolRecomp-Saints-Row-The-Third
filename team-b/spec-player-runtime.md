# Saints Row: The Third — Player Runtime: Input Query Contract and Control-State Machine

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** player runtime, units **U0** (input query contract) and **U1** (control-state machine) of the proposed order in `review/scoping-player.md` §5. This is the first spec file for the player system; later units (U2 normal-condition movement, U3 locomotion bridge, U4 follow camera, …) are expected to be appended here as further sections. **U2 was added as §3 on 2026-10-03** (the OPEN items and clean-room check moved to §4 and §5). **U4 was added as §4 on 2026-10-03** (the OPEN items and clean-room check moved to §5 and §6). **U3 was added as §5 on 2026-10-03** (the OPEN items and clean-room check moved to §6 and §7). **U6.6 was added as §6 on 2026-10-03** (the OPEN items and clean-room check moved to §7 and §8). **U6.0 and U6.4 (the first and fifth sub-units of U6, `review/scoping-player.md` §5a.3) were added as §7 and §8 on 2026-10-03, after U6.6** (the OPEN items and clean-room check moved to §9 and §10). **U6.1 (the element-selection rule, `review/scoping-player.md` §5a.3) was added as §9 on 2026-10-04** (the OPEN items and clean-room check moved to §10 and §11). **U6.3 (the enter route: states 0–8, the routing hub and the instant route, `review/scoping-player.md` §5a.3) was added as §10 on 2026-10-07** (the OPEN items and clean-room check moved to §11 and §12). **U6.7 (the animated exit states `0x1a`–`0x21`, `review/scoping-player.md` §5a.3) was added as §11 on 2026-10-07, after U6.3** (the OPEN items and clean-room check moved to §12 and §13).
**Scope:** everything between the engine library's device block and the ~180 gameplay functions that ask "is this action pressed / held / how far is this axis deflected": the per-frame latch that copies device state into the game's control context at `0x01411b18`, the full layout of that context, the two runtime binding tables (`0x0140ff98`, 164 × `0x20`; `0x01411418`, 34 × `0x34`) and the step that fills them from `control_schemes.xtbl` / `control_binding_sets.xtbl`, the complete query API with its signatures, return values, "out" codes, dead-zone and range handling, and the input-consumption model; then the 8-row control-state machine at `0x012e6bb8` with its request stack, dispatch, push/pop/reset, and the row handlers' entry points. It does **not** specify what the per-frame player controls *do* with the input (that is U2 onward; only the dispatch skeleton of state 2's handler is recorded here, §2.7), nor the engine library's device drivers below the device block (XInput/DirectInput/raw-input code — only the *semantics* the context receives from them are pinned down, §1.3).
**Method:** Ghidra-free. Direct reads of `SaintsRowTheThird.exe`'s bytes through `tools/harnesses/exe_va.py`, a private recursive-descent capstone disassembler (jump tables followed by hand from their static targets), a raw `E8`/`E9` rel32 scan of `.text` for every call site of every function cited, a raw dword scan of `.text` for every data reference to every global cited, push-argument surveys at call sites, and static reads of the data tables (`0x012e6bb8`, `0x0111f1d0`, `0x012e6c58`, the CBA/CAA/stick/mouse vocabulary tables). Every function named in §1–§2 was read in full in its own listing unless marked otherwise. No Ghidra project was opened or copied, so none needed deleting. No `.czn_pc` data was touched; nothing in U0/U1 reached it.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear anywhere below; behaviour is described in prose and tables, and every function, global and field is cited by its plain hex address or offset. Action, axis, key, mouse-button, stick and scheme names (`CBA_OFC_SPRINT`, `CAA_CAMERA_ROTATE`, `LEFT_STICK_DIR_UP`, `MOUSE MIDDLE`, `General Controls`, …) are the engine's own strings read from its own static tables, quoted as data — the same convention every other spec in this project applies. Win32 import names (`ClipCursor`, `IsIconic`, `GetClientRect`) are read from the PE import table.
**Confidence key:** **CONFIRMED** (read directly in a listing, a static table read, or an exhaustive reference scan this pass); **HIGH CONFIDENCE** (follows from structure, call shape or a vocabulary match, but not traced end to end); **HYPOTHESIS** (plausible, unconfirmed); **OPEN** (collected in §14, with a next step where one exists).
**Relationship to other specs:** the binding *data* (`control_binding_sets.xtbl`, `control_schemes.xtbl`, the CBA/CAA/key/mouse/pad/stick vocabularies) is `spec-tables-ui-controls.md` §2–§4; this file is the runtime that evaluates it. `spec-lua-api-behaviour.md` §2.6 (`game_is_active_input_gamepad`) and §7.22 (`player_controls_disable`) both touch this layer and are cross-referenced in §1.5 and §2.9. `review/scoping-player.md` is the scoping note this file refines; every correction to it is flagged **[corrects scoping]** where it applies, and the list is collected in §1.14.

---

## 0. Results up front

1. **The seam is clean.** Every gameplay reader of input goes through one of ~45 small exported functions in the band `0x005b9000`–`0x005c3000` (§1.8–§1.12), all of which read only the control context `0x01411b18`, the two runtime binding tables, and (for three analog helpers) four engine-library accessors. A port can rebuild the context each frame from its own platform layer, keep the binding tables, and leave every consumer untouched (§1.13 lists exactly what must be reproduced).
2. **The scoping note had the two main button queries swapped.** **`0x005bae10` is "pressed this frame" and `0x005bd7b0` is "held"**, not the reverse **[corrects scoping §1.1]**. Likewise the per-key test `0x005ba4f0` (byte `+2`) is the *pressed-edge* test and `0x005bc8b0` (byte `+1`) the *held* test. Settled three ways: the latch's source for each byte (§1.3–§1.4), the pad mask each query reads (§1.8), and the consumers — `CBA_OFC_SPRINT` is read through `0x005bd7b0` by the character-motion callback **[§5.2: that is the vehicle-contact replay `0x00961d70`; the velocity composer `0x0099d1b0` reads it too]**, `CBA_OFC_SPRINT_TOGGLE` through `0x005bae10` (§1.8.4).
3. **The "out" parameter is a device-family code, but not the one hypothesised.** For the button queries it is −1 none, **0 pad button, 1 stick direction, 2 keyboard key or mouse button**; for the axis evaluators it is −1 none, **1 gamepad stick, 3 keyboard/mouse** (a code 2 is assigned and then always overwritten — dead) **[corrects scoping's −1/0/1/2/3 hypothesis]** (§1.8.2, §1.9.3).
4. **Per-key record (8 bytes at `+0xa4 + 8·scan`):** `+0` *enabled this frame* (set to 1 by every latch, cleared by "consume"/"clear" calls), `+1` held, `+2` went down this frame, `+3` went up this frame, `+4` time of the last down-edge in ms (§1.2, §1.4).
5. **Input is a one-frame consumable.** Every source carries an enable flag the latch re-arms each frame; "consume" queries clear it, after which *every* action bound to that physical input reads false for the rest of the frame. Consumer call order is therefore part of the contract (§1.11).
6. **Axes are prioritised, not summed:** a non-zero gamepad stick wins outright; keyboard/mouse-button contributions are summed only when the stick reads zero; the mouse axis is added only when the key sum is zero; the result is clamped to [−1, 1] (§1.9).
7. **Binding application** layers four gamepad schemes (General → Satellite → On_Foot → Driving, defaults `General Controls` / `Satellite Controls` / `A` / `A`) and all seven PC binding sets into the same two runtime tables; the pad half and the keyboard/mouse half of each record are written by different appliers and never interfere (§1.7). The string `"A"` at `0x01121d10` is a **control-scheme letter**, not a binding-set name **[corrects the brief's wording and scoping §1.1]**.
8. **Control-state machine** (§2): 8 rows (rows 0/1 "locked", 2 normal play, 6 "frozen" (empty row), 7 free camera); a 5-deep request stack of `{state, handle, 0}` entries; **17** push sites and **17** pop sites (not 12) **[corrects scoping §1.2]**; handles start at 1, so 0 is the universal "no handle" value.
9. **OPEN item 2 of the scoping note is resolved:** the index `0x012fd5c8` is a UI-layer screen state that is **always 0 or 1 when state 2's handler runs** — mode 5's enter requests state 0, and the UI per-frame that turns the request into the current value runs immediately before the control-state dispatcher in the same world update (§2.8). The −1 reset value is only ever visible outside mode 5.

---

## 1. U0 — The input query contract

### 1.1 Where the latch sits in the frame

**CONFIRMED** by listing of `0x005d14b0` (the per-frame function), `0x00707440`, `0x005be880` and `0x00702a50`:

| Order | Call | Role |
|---|---|---|
| 1 | `0x005d14b0` → `0x00db8d40` (only when the app-active byte `0x02a076e8` is set and `IsIconic` on the main window `0x02a076e0` is false) | engine-library pad poll: buttons, sticks, per-button edge/repeat bytes, stick direction pulses (§1.3) |
| 2 | `0x005d14b0` → `0x00707440` | the input latch |
| 2a | `0x00707440` → `0x00dd0680`, `0x00dcfc60` (same active / not-iconic gate) | engine keyboard per-frame (clears the keyboard edge bytes, then replays the queued key events) and engine mouse per-frame (clears the per-button suppress bytes, then reads the cursor) |
| 2b | `0x00707440` → `0x005be880` | game-side latch (below) |
| 2c | `0x00707440` → `0x00dcfdf0` | clears the engine mouse press/release/double-click bytes **after** the game has copied them |
| 2d | `0x00707440` → `0x005c2700` (tail call, same gate) | the stick sector / rotation tracker (§1.12.4) — a consumer of the API, not part of it |
| 3 | `0x005d14b0` → `0x00706be0` → … → world update `0x00702a50` | all gameplay readers |

`0x005be880` (gate): when the app is active and not iconic, it clears the "frozen" byte `0x01411b03`, runs the mouse-capture/cursor-clip update `0x005bac90`, and tail-calls **`0x005bcde0` with the context `0x01411b18` in `ecx`**. Otherwise, the first time only (while `0x01411b03` is 0), it calls the full clear `0x005bc830` on the context and sets `0x01411b03` to 1 — so **while the window is inactive or minimised every query reads "nothing", and the context stays frozen in that cleared state** (§1.11.3). **CONFIRMED.**

### 1.2 The control context `0x01411b18` — full layout

All offsets relative to `0x01411b18`. Writers are the latch `0x005bcde0` (L), the device-family tracker `0x005bc9d0` (D), the clear functions `0x005bbf60` / `0x005bc830` (C), and the consume helpers (K). **CONFIRMED** for every field by the listed writer and at least one reader, except where marked.

| Offset | Size | Contents | Written by |
|---|---|---|---|
| `+0x000` | 0x28 | **stick 0 (left stick) record** — see the stick sub-table below | L, C |
| `+0x028` | 0x28 | **stick 1 (right stick) record** | L, C |
| `+0x050` | 4 | pad **held** mask — bit *b* = pad button *b* is down (16 buttons) | L |
| `+0x054` | 4 | pad **repeat-pulse** mask — bit *b* = button *b* pressed this frame *or* auto-repeat fired (§1.3) | L |
| `+0x058` | 4 | pad **held-enable** mask; set to all-ones each frame when a pad is present, 0 otherwise | L, C, K |
| `+0x05c` | 4 | pad **pressed** mask — bit *b* = button *b* went down this frame | L |
| `+0x060` | 4 | pad **pressed-enable** mask; all-ones each frame when a pad is present, 0 otherwise | L, C, K |
| `+0x064` | 16 × 4 | per pad button: engine time (ms, `0x029f4db0`) of its last down-edge | L |
| `+0x0a4` | 256 × 8 | **keyboard records**, indexed by Set-1 scan code (`spec-tables-ui-controls.md` §4.3) — see below | L, C, K |
| `+0x8a4` | 256 | **"key is bound" table** — 1 for every scan code that any runtime binding uses | `0x005bab40` (§1.7.5) |
| `+0x9a4` | 7 × 8 | **mouse-button records**, indexed by the mouse-button vocabulary (`MOUSE LEFT`=0, `MOUSE MIDDLE`=1, `MOUSE RIGHT`=2, `MOUSE 4`=3, `MOUSE 5`=4, `WHEEL UP`=5, `WHEEL DOWN`=6) — same 8-byte shape as the keyboard records | L, C, K |
| `+0x9dc` | 2 × 0xc | **mouse-axis records** (`MOUSE X`=0, `MOUSE Y`=1): float value `+0`, float value copy `+4`, enable byte `+8` | L, C |
| `+0x9f4` | 1 | "keyboard or mouse button activity this frame" — set when any key or mouse button is held, pressed or released | L |
| `+0x9f5` | 1 | **gamepad is the active input** (absolute `0x0141250d`; the byte `game_is_active_input_gamepad` returns, `spec-lua-api-behaviour.md` §2.6) | D, C |
| `+0x9f6` | 1 | **camera is being driven from keyboard/mouse** (absolute `0x0141250e`) | D, C |

**Stick record** (0x28 bytes; stick 0 at `+0x00`, stick 1 at `+0x28`). That stick 0 is the left stick and stick 1 the right is CONFIRMED by vocabulary agreement: the axis code `LEFT_STICK_AXIS_X` (= 2) is evaluated from `+0x00` and `RIGHT_STICK_AXIS_X` (= 0) from `+0x28` (§1.9.1), and `LEFT_STICK_DIR_*` codes 0–3 address stick 0 (§1.10).

| Offset | Contents |
|---|---|
| `+0x00` / `+0x04` | x / y after the engine's per-axis dead zone and rescale (§1.3.3); range [−1, 1]; +y is "up" |
| `+0x08` / `+0x0c` | previous frame's `+0x00` / `+0x04` |
| `+0x10` / `+0x14` | x / y **raw** (no dead zone; invert/disable already applied) |
| `+0x18` | digital x pulse: −1 when the engine's "left" direction pulse fired this frame, +1 for "right", else 0 |
| `+0x1c` | digital y pulse: −1 for the engine's "up" pulse, +1 for "down", else 0 (note the sign: screen-style, down positive) |
| `+0x20` | block flag, x-negative ("left") direction |
| `+0x21` | block flag, x-positive ("right") |
| `+0x22` | block flag, y-positive ("up") |
| `+0x23` | block flag, y-negative ("down") |
| `+0x24` | block flag, analog x |
| `+0x25` | block flag, analog y |
| `+0x26`/`+0x27` | padding (never referenced) |

The six block flags are cleared to 0 by every latch and set to 1 by "clear latched" (§1.11.1) and by the stick-direction consume helpers (§1.10).

**Keyboard and mouse-button record** (8 bytes; keyboard at `+0xa4 + 8·scan`, mouse at `+0x9a4 + 8·button`):

| Byte | Meaning | Source (engine library, §1.3) |
|---|---|---|
| `+0` | **enabled this frame.** Set to 1 for all 256 keys, all 7 mouse buttons and both mouse axes at the start of every latch; cleared to 0 by consume helpers and by "clear latched". Every query requires it | latch |
| `+1` | **held** (currently down) | keyboard: engine key byte `+0` (down level), unless that key is suppressed; mouse: engine mouse byte `+7` |
| `+2` | **went down this frame** | keyboard: engine key byte `+1`; mouse: engine mouse byte `+4` |
| `+3` | **went up this frame** | keyboard: engine "not down and released-edge" (bytes `+0`/`+2`); mouse: engine mouse byte `+5` |
| `+4` | dword: engine time (ms) of the last down-edge (written whenever `+2` is set) | latch |

**[corrects scoping §1.1]**: the scoping note read `+2` as "held" and `+1` as "pressed". The listing of the engine accessors `0x00dd0260` / `0x00dd02a0` / `0x00dd02e0` and of the engine key-event handler `0x00dd0020` (which sets engine byte `+1` and a timestamp only on an up→down transition, and byte `+2` only on a down→up transition) settles it: `+1` is the level, `+2` the down-edge. **CONFIRMED.** No reader of the `+4` timestamps (keyboard, mouse or the pad array at `+0x064`) exists anywhere in `.text` — neither by absolute address nor by context-relative displacement inside the control band — so they are write-only in this build (HIGH CONFIDENCE).

**Adjacent globals** (just below the context; CONFIRMED by their readers/writers):

| Address | Contents |
|---|---|
| `0x01411b00` | option flag byte: bit 0 set by every binding application (§1.7); bits 1 and 2 have get/set pairs `0x005bc290`/`0x005bc2a0` and `0x005bc260`/`0x005bc270`; bit 3 is tested by the in-vehicle handler (`0x005b5412`). HYPOTHESIS: bits 1/2 are the gamepad look-invert options (see `0x005be920`/`0x005be940`, §1.12.2) |
| `0x01411b01` | "cursor captured" byte (set/cleared with `0x00dcfe70`/`0x00dcfe90`) |
| `0x01411b02` | "`ClipCursor` rectangle active" byte |
| `0x01411b03` | "context frozen while inactive" byte (§1.1) |
| `0x01411b04` / `0x01411b08` / `0x01411b0c` / `0x01411b10` | currently applied control-scheme index for the **General / On_Foot / Satellite / Driving** scheme arrays (§1.7) |
| `0x01411b14` / `0x01411b15` | option bytes with get/set pairs `0x005bc380`/`0x005bc390` and `0x005bc3a0`/`0x005bc3b0`; HYPOTHESIS: the mouse look-invert options (selected instead of `0x01411b00` bits 2/1 while `+0x9f6` is set) |
| `0x012e6f04` / `0x012e6f08` | mouse X / Y sensitivity (static default 0.24 each); getters `0x005bc3c0`/`0x005bc3d0`, clamping setters `0x005bc3e0`/`0x005bc440` |
| `0x012e6f0c` | mouse global scale (static 0.01; no writer found) |
| `0x012e6f10` | "middle button disables the wheel" byte (static 1; its only reference is the latch, §1.4 step 6) |
| `0x012e6ec8` | active pad index (−1 means pad 0); written by `0x007adeae` and `0x0086fd2c` |

### 1.3 What the engine library provides (the device-side contract)

A port does not need the engine's drivers, but it must supply the context with values that have exactly these semantics. All CONFIRMED by listing of the named accessors and of the poll `0x00db8d40`, unless marked.

#### 1.3.1 Keyboard (`0x02a38440` + 12·scan, 256 records)

- byte `+0` down level, byte `+1` down-edge this frame (with the ms time at `+4`), byte `+2` up-edge this frame, byte `+3` "suppressed" (set by `0x00dd0700(scan)`, scan < 256), `+8` time of the last key-event.
- Edge bytes and the suppress byte are cleared at the start of each keyboard per-frame (`0x00dd0680`), which then replays a 32-entry ring of queued key events (`0x02a38340`) through `0x00dd0020`. A repeated "down" for a key already down does not produce a new edge, so OS auto-repeat never reaches the context as presses.
- The three toggle-lock keys (scan `0x3a`, `0x46`, `0xc5`) invert their down level on each press instead of following the key (unless `0x02a390c9` is set).
- All three accessors return false when the global keyboard-enable bit (`0x0132abc0` bit `0x08`) is clear, when the key's suppress byte is set, or when the global suppress byte `0x02a390ca` is set.

#### 1.3.2 Mouse buttons (`0x02a381e8` + 12·button, 7 records)

`+0` time of the last press, `+4` pressed this frame, `+5` released this frame, `+6` double-click (a press within 400 ms of the previous one; not copied into the context), `+7` down level, `+8` per-button suppress (cleared every mouse per-frame). Accessors `0x00dcfea0` (held), `0x00dcfed0` (pressed), `0x00dcff00` (released) also return false when the global mouse-enable bit (`0x0132abc0` bit `0x10`) is clear or the global mouse-suppress byte `0x02a3823d` is set.

#### 1.3.3 Pads (`0x029f4440` + `0x488`·pad; pad 0 and 1)

- **Button records** (16 × `0x24` bytes from the pad base; button numbering is the gamepad vocabulary `A`=0 … `RS`=15, `spec-tables-ui-controls.md` §4.6): `+0x00` analog value, `+0x04` previous value, `+0x08` press threshold, `+0x0c` auto-repeat interval (ms), `+0x10` auto-repeat initial delay (ms), `+0x14` time of the down-edge, `+0x18` time of the last repeat, `+0x1c` down-edge this frame, `+0x1d` repeat pulse, `+0x1e` up-edge (read-and-clear accessor `0x00db8800`), `+0x1f` down level, `+0x20` suppress (cleared at the start of every poll; set by `0x00db8a10(pad, button)`).
  - "Down" means connected, value > threshold, and not suppressed (`0x00db8760`).
  - The **repeat pulse** fires on the down-edge and then whenever the button has been down longer than the initial delay *and* the last pulse is older than the interval.
- **Stick records** (2 × `0x40` bytes at pad base `+0x240`): `+0x00`/`+0x04` raw x/y, `+0x08` dead zone, `+0x0c`/`+0x0d` invert x/y, `+0x0e`/`+0x0f` one-frame disable x/y (cleared each poll; set by `0x00db8a40(pad, stick, axis)`), then four direction-pulse slots at `+0x18` + 12·*dir* (`+0` pulse byte, `−8`/`−4` first and last pulse time).
  - **Dead-zone transform** (`0x00db8af0`): each axis independently — if |v| < dead zone the result is 0, otherwise (v − sign(v)·dz) / (1 − dz). Then disable forces 0 and invert negates. The raw accessor `0x00db86a0` applies invert/disable but no dead zone.
  - **Direction pulses** (computed by the poll from the dead-zoned values): *dir* 0 = y > 0.5, 1 = −y > 0.5, 2 = −x > 0.5, 3 = x > 0.5 (threshold `0x0126d2cc`). A pulse fires on entry, then again after 500 ms, then every 150 ms while held — a menu-navigation repeat.
- A per-pad "activity" byte (pad base `+0x2c4`) is set whenever any button is down or a stick axis is non-zero.
- The dead-zone and threshold *values* are written by engine-library code outside the poll that this pass did not locate (§14, OPEN 1).

### 1.4 The latch `0x005bcde0` (582 instructions) — step by step

**CONFIRMED** by full listing. It is `__thiscall` on the context (`ecx` = `0x01411b18`). "Pad" below is `0x012e6ec8`, with −1 read as 0.

1. **No pad registered** (`0x00dd1620` false — engine pad count `0x02a390e4` is 0): zero `+0x50`, `+0x54`, `+0x58`, `+0x60`, zero the two stick records (`+0x00`…`+0x4f`), and go to step 5. (`+0x5c` is left stale; harmless, since `+0x60` is 0.)
2. **Pad buttons** (*b* = 0…15): bit *b* of `+0x50` = "button down". For *b* = 6 and 7 (`LT`, `RT`) the latch then moves the engine threshold to give the analog triggers **hysteresis**: while down, threshold = max(value − 0.1, threshold); while up, threshold = min(value + 0.1, threshold) (`0x00db88a0` value, `0x00db8970` threshold, `0x00db8940` set; 0.1 is `0x012a2d38`). Then bit *b* of `+0x54` = repeat pulse; `+0x58` = all ones; bit *b* of `+0x5c` = down-edge, storing the engine time into `+0x64 + 4b` when set; `+0x60` = all ones.
3. **Sticks**: copy `+0x00`/`+0x04` into `+0x08`/`+0x0c` and `+0x28`/`+0x2c` into `+0x30`/`+0x34`; read both dead-zoned sticks into `+0x00`/`+0x04` and `+0x28`/`+0x2c`, and both raw sticks into `+0x10`/`+0x14` and `+0x38`/`+0x3c`. **Sign-flip suppression:** for each of the four dead-zoned axes, if the previous value and the new value have strictly opposite signs, the new value is forced to 0 for this frame.
4. **Digital pulses**: stick 0 `+0x18` = −1 if direction 2 pulsed, else +1 if direction 3 pulsed, else 0; `+0x1c` = −1 if direction 0 pulsed, else +1 if direction 1, else 0; same for stick 1 at `+0x40`/`+0x44`. All twelve block bytes (`+0x20`…`+0x25`, `+0x48`…`+0x4d`) are cleared.
5. Clear `+0x9f4`. **If the current control state (`0x005aa630`) is 5 and the byte `0x0140e974` is set, stop here** — the keyboard, mouse and device-family steps are skipped, so key records keep last frame's values. Nothing in the executable pushes state 5 (§2.6), so this path is unreachable in shipped play (HIGH CONFIDENCE).
6. **Keyboard**: set `+0` of all 256 key records to 1; then for every scan code fill `+1` (held), `+2` (down-edge; also store the time at `+4`), `+3` (up-edge) from the engine, setting `+0x9f4` when any is true. **Mouse buttons**: same for the 7 mouse records; additionally, when button 1 (`MOUSE MIDDLE`) is held and `0x012e6f10` is 1 (its static value), the enable bytes of buttons 5 and 6 (`WHEEL UP`/`WHEEL DOWN`) are cleared — **the wheel is ignored while the middle button is held**.
7. **Mouse axes**: set both enable bytes; read the engine's per-frame cursor deltas (`0x00dcfcf0` — the first output is X, the second Y; a third, the wheel delta, is read but discarded); scale X by `0x012e6f04`·`0x012e6f0c` and Y by `0x012e6f08`·`0x012e6f0c` (defaults 0.24 × 0.01 = 0.0024 per count); if the squared length exceeds 1 (`0x00dae390`, an approximate ≤ with tolerance 1e‑4) divide both by the length (clamp to the unit circle); store X in `+0x9dc` and `+0x9e0`, Y in `+0x9e8` and `+0x9ec`.
8. Run the device-family tracker `0x005bc9d0` (§1.5).

### 1.5 Device-family tracking `0x005bc9d0` — `+0x9f5` and `+0x9f6`

**CONFIRMED** by full listing. This closes `spec-lua-api-behaviour.md` §2.6's "writer not located" (as the scoping note already flagged): the byte is written through the context pointer, never by absolute address.

- No pad registered: `+0x9f5` = 0, `+0x9f6` = 1, return.
- If `+0x9f5` is 0: set it to 1 when there is **pad activity** (`0x005ba600`: `+0x50` non-zero, or any of the four dead-zoned stick axes not within 1e‑4 of 0).
- If `+0x9f5` is 1: clear it when `+0x9f4` is set (any key or mouse button held/pressed/released) or there is **mouse movement** (`0x005ba5b0`: either mouse-axis value not within 1e‑4 of 0).
- Then evaluate `CAA_CAMERA_ROTATE` and `CAA_CAMERA_ELEVATE` through `0x005bb530` for their out codes only. If `+0x9f6` is set and either code is 1 (gamepad), clear `+0x9f6`; if it is clear and either code is 3 (keyboard/mouse), set it. So **`+0x9f6` tracks which device last moved the camera**, with stickiness: it changes only when the other family actually produces camera input.
- Getters: `0x005bc5d0` returns `+0x9f5` (64 call sites outside the band); `0x005bc5e0` returns `+0x9f6` (17 call sites).

### 1.6 The runtime binding tables

Both tables are flat arrays indexed directly by the action value (CBA 0…163, CAA 0…33 — `spec-tables-ui-controls.md` §4.1/§4.2). Each record has a **pad half** (written only from `control_schemes.xtbl` data) and a **keyboard/mouse half** (written only from `control_binding_sets.xtbl` data). **CONFIRMED** by the initialiser `0x005c1f70`, the appliers (§1.7) and the query readers (§1.8–§1.9).

**Button record** — 164 × `0x20` bytes at `0x0140ff98`:

| Offset | Half | Contents | Initial value | Source field |
|---|---|---|---|---|
| `+0x00` | pad | pad button A (vocabulary §4.6, 0…15) | −1 | scheme `Control` `+0x04` (`Button`) |
| `+0x04` | pad | pad button B | −1 | scheme `Control` `+0x08` (`Alternate_Button`) |
| `+0x08` | pad | stick-direction code (§1.10; `LEFT_STICK_DIR_UP`=0 … `RIGHT_STICK_DIR_LEFT`=7) | −1 | scheme `Control` `+0x0c` (`Stick_Direction`) |
| `+0x0c` | KB/M | key 1 (scan code; 0 = none) | 0 | binding-set `Control` `+0x04` (`Key`) |
| `+0x10` | KB/M | key 2 | 0 | `+0x08` (`Alt_Key`) |
| `+0x14` | KB/M | mouse button 1 (−1 = none) | −1 | `+0x0c` (`Mouse_Button`) |
| `+0x18` | KB/M | mouse button 2 | −1 | `+0x10` (`Alt_Mouse_Button`) |
| `+0x1c` | pad | display-label handle (interned `Action_Name`) | 0 | scheme `Control` `+0x10` |

**Axis record** — 34 × `0x34` bytes at `0x01411418`:

| Offset | Half | Contents | Initial value | Source field |
|---|---|---|---|---|
| `+0x00` | pad | primary stick-axis code (`RIGHT_STICK_AXIS_X`=0, `_Y`=1, `LEFT_STICK_AXIS_X`=2, `_Y`=3) | −1 | scheme `Axis` `+0x04` (`Axis`) |
| `+0x04` | pad | byte: invert primary | 0 | scheme `Axis` `+0x08` (`Inverted`) |
| `+0x08` | pad | secondary stick-axis code | −1 | scheme `Axis` `+0x0c` (`Alternate_Axis`) |
| `+0x0c` | pad | byte: invert secondary | 0 | scheme `Axis` `+0x10` — which `spec-tables-ui-controls.md` §3.2 shows is a second read of `Inverted`, so both invert bytes are always equal |
| `+0x10` / `+0x14` | KB/M | key pair 1: positive key / negative key | 0 / 0 | binding-set `Axis` `+0x04` / `+0x08` (`Key_Pos` / `Key_Neg`) |
| `+0x18` / `+0x1c` | KB/M | key pair 2: positive / negative | 0 / 0 | `+0x0c` / `+0x10` (`Alt_Key_Pos` / `Alt_Key_Neg`) |
| `+0x20` / `+0x24` | KB/M | mouse pair 1: positive / negative button | −1 / −1 | `+0x14` / `+0x18` |
| `+0x28` / `+0x2c` | KB/M | mouse pair 2: positive / negative | −1 / −1 | `+0x1c` / `+0x20` |
| `+0x30` | KB/M | mouse axis (`MOUSE X`=0, `MOUSE Y`=1, −1 none) | −1 | `+0x24` (`Mouse_Axis`) |

The record contents line up with the `Control`/`Axis` records exactly as the scoping note predicted (HIGH there, CONFIRMED here). Note that the runtime tables are **not** the 20-byte/40-byte save-format tables; `spec-tables-ui-controls.md` §2.2's narrowed OPEN (which reader builds the save table) is untouched by this unit.

### 1.7 Binding application — `0x005c1f70` and its appliers

#### 1.7.1 The full rebuild `0x005c1f70` (117 instructions; one caller, `0x005d2f74` in start-up)

**CONFIRMED** by full listing, in order:

1. Reloads both data files: `0x005c1860(0)` (`control_schemes.xtbl`) and `0x005bf2f0(0)` (`control_binding_sets.xtbl`).
2. Initialises all 164 button records and all 34 axis records to the "initial value" columns of §1.6.
3. Option byte `0x01411b00`: clears bits 1–3 and sets bit 0.
4. Applies the four gamepad scheme kinds, **in this order**, each looked up by name with −1 mapped to 0:
   - `0x005bd580("General Controls")` → **`0x005c1680`** (General array `0x01412510`, current index `0x01411b04`);
   - `0x005bd680("Satellite Controls")` → **`0x005c1340`** (Satellite array `0x0141e230`, index `0x01411b0c`);
   - `0x005bd600("A")` → **`0x005c12c0`** (On_Foot array `0x014183a0`, index `0x01411b08`);
   - `0x005bd700("A")` → **`0x005c13c0`** (Driving array `0x014240c0`, index `0x01411b10`).
   The four lookups are the same function over the four arrays of `spec-tables-ui-controls.md` §3: the first scheme whose name compares equal case-insensitively wins; failing that, the first whose name *contains* the needle (`0x00da7780`); failing that, −1. So `"A"` at `0x01121d10` is the **default lettered scheme for on-foot and driving**, not a binding-set name.
5. Applies **every** PC binding set's keyboard/mouse half: `0x005bcc80(slot)` for each button-set slot of `0x0142a010` (count `0x0142a018`, stride `0xce8`), then `0x005bcd80(slot)` for each axis-set slot of `0x01436ea0` (count `0x01436ea8`, stride `0x568`).
6. Rebuilds the bound-key table (`0x005bab40`, §1.7.5).
7. Releases the cursor: `0x00dcfe90(1)`, clears `0x01411b01`, `ClipCursor(NULL)` (clearing `0x01411b02` on success), then tail-calls the capture/clip update `0x005bac90`.

#### 1.7.2 The four scheme appliers (`0x005c1680`, `0x005c1340`, `0x005c12c0`, `0x005c13c0`; 31 instructions each, identical shape)

**CONFIRMED** by full listing of all four. Each takes a scheme index and:

1. clamps it to [0, count − 1] of its array (counts `0x01412518`, `0x0141e238`, `0x014183a8`, `0x014240c8`);
2. copies the **currently applied** scheme slot (by the current-index global) onto the stack (`0x005c1240`: both sub-arrays, counts capped at 100, plus `+0xfb8`/`+0xfbc`) and **un-applies** it (`0x005bec10`): for every `Control` record in it, the runtime button record's pad half (`+0x00`, `+0x04`, `+0x08` → −1; `+0x1c` → 0); for every `Axis` record, the axis record's pad half (`+0x00`, `+0x08` → −1; `+0x04`, `+0x0c` → 0);
3. stores the new index into the current-index global;
4. copies the **new** slot onto the stack and **applies** it (`0x005bd4e0`): for every `Control` record, button record [`Action`] ← `Button`, `Alternate_Button`, `Stick_Direction`, label handle (into `+0x00`, `+0x04`, `+0x08`, `+0x1c`); for every `Axis` record, axis record [`Action`] ← `Axis`, `Inverted`, `Alternate_Axis`, second `Inverted` (into `+0x00`, `+0x04`, `+0x08`, `+0x0c`); then a notification `0x00846c60(flag from 0x009b9160(local player), 1)` (HYPOTHESIS: refreshes on-screen control legends).

Consequences (CONFIRMED by the above): later scheme kinds override earlier ones for any action both name; the applier never touches the keyboard/mouse half; and **switching a scheme first unbinds every action the outgoing scheme named**, even if an earlier-applied scheme kind had also bound it — such an action stays pad-unbound until a scheme naming it is applied again. The On_Foot and Driving appliers are also called from the options UI (`0x007e10f7`, `0x007e1102`, `0x007e12bf`…`0x007e12eb`) and from `0x008413d8`/`0x008413e1`; the General/Satellite ones only from start-up and from `0x005c17eb`/`0x005c154b`.

#### 1.7.3 Keyboard/mouse button applier `0x005bcc80` (83 instructions)

For each 20-byte `Control` record in the set: skip it if all 20 bytes are zero; **skip the whole record if either key field is outside 0…255** (so an absent key, stored as −1 by the loader, discards the row, while an authored `UNBOUND` = 0 is accepted); skip it if either mouse field is neither −1 nor below 7; otherwise write key 1, key 2, mouse 1, mouse 2 into the runtime button record's `+0x0c`…`+0x18`. **CONFIRMED.** Sets are applied in slot (file) order, so for an action named in several sets the last one wins.

#### 1.7.4 Keyboard/mouse axis applier `0x005bcd80` → `0x005babd0` (40 + 49 instructions)

For each 40-byte `Axis` record, `0x005babd0` validates: all four keys within 0…255, all four mouse buttons −1 or below 7, mouse axis −1 or below 2 — any failure discards the record — then writes the nine fields into the runtime axis record `+0x10`…`+0x30` in file order. **CONFIRMED.** This incidentally confirms the field pairing that `spec-tables-ui-controls.md` §2.3 left as a data-check OPEN: `Key_Pos` feeds the positive half, `Key_Neg` the negative half (§1.9.2).

#### 1.7.5 Bound-key table `0x005bab40`

Clears the 256 bytes at context `+0x8a4` (`0x014123bc`) and sets byte [scan] = 1 for every non-zero key in every button record (`+0x0c`, `+0x10`) and axis record (`+0x10`…`+0x1c`). **CONFIRMED** (writer). No reader of this table was found by absolute-address scan; a context-relative reader cannot be excluded (OPEN 4).

### 1.8 Button-action queries

#### 1.8.1 Signatures

All are `cdecl` with the CBA value as the first argument. Every one of them rejects a CBA above `0xa3` by returning false/0 **without writing the out parameter**. "Sites" are call sites outside the band `0x005b9000`–`0x005c3000` (raw `E8` scan). **CONFIRMED** by full listing of each.

| Function | Signature | Returns | Sites |
|---|---|---|---|
| **`0x005bae10`** | (cba, int* out) | bool — **went down this frame** | 73 |
| **`0x005bd7b0`** | (cba, int* out) | bool — **held** | 83 |
| **`0x005bdfe0`** | (cba, int* out) | bool — went down this frame, **and consume** it | 26 |
| `0x005bdef0` | (cba, int* out) | bool — **navigation pulse, consuming**: pad repeat pulse, stick digital pulse, or key/mouse held | 2 |
| `0x005bbdd0` | (cba) | void — **consume** every bound pad button, key and mouse button (not the stick direction) | 46 |
| `0x005bbff0` | (cba) | bool — "every bound source is already disabled/consumed" | 4 |
| `0x005bd8b0` | (cba, int* out) | float in [0, 1] — **analog value** of the action | 14 |
| `0x005bdb60` | (cba, int* out) | float in [0, 1] — the **previous frame's** analog value | (internal) |
| `0x005bde80` | (cba, float threshold, int* out) | bool — analog value **fell through** `threshold` this frame | 7 |
| `0x005bc1a0` | (cba, bool alternate) | int — pad button A or B (−1 when out of range) | 8 |
| `0x005bc4d0` / `0x005bc4e0` / `0x005bc4f0` / `0x005bc500` | (cba) | int — key 1 / key 2 / mouse 1 / mouse 2 (no range check) | 4 / 3 / 5 / 3 |
| `0x005bc200` / `0x005bc230` | (cba) | byte `+5` / `+0xa` of pad button A's 12-byte descriptor at `0x01122008` (0 when unbound) | 2 / 1 |
| `0x005baf30` / `0x005baef0` | (cba, bool alternate) | display string for the pad button via `0x00843250` / `0x00843290` (fallback strings `0x01122824` / `0x01122810`) | 13 / 2 |

#### 1.8.2 The out code

When the pointer is non-null it receives, for the *first* source that satisfied the query: **−1** none, **0** pad button, **1** stick direction, **2** keyboard key or mouse button. For `0x005bd8b0` and `0x005bdb60` the same codes mean "which family contributed the (last non-zero) value", with the precedence described in §1.8.3. **CONFIRMED** in all six functions. **[corrects scoping §1.1's −1/0–3 hypothesis: there is no separate mouse code for buttons, and 1 is the stick, not a key.]**

#### 1.8.3 Evaluation order (CONFIRMED)

For the three digital queries the sources are tried in this order, stopping at the first that is satisfied:

1. **Pad.** Build a mask from button A and B (bit per bound button). Pressed: mask & `+0x5c` & `+0x60`; held: mask & `+0x50` & `+0x58`; pressed-and-consume: as pressed, then clear the mask bits from **both** `+0x58` and `+0x60`; navigation pulse: mask & `+0x54` & `+0x58`, consuming like the previous one.
2. **Stick direction** (button record `+0x08`, §1.10): crossing / held / crossing-and-block / digital-pulse-and-block respectively.
3. **Keys** 1 then 2: record byte `+2` (pressed) or `+1` (held), each also requiring `+0`; the consuming variants clear `+0`. Scan code 0 never matches.
4. **Mouse buttons** 1 then 2: same bytes; −1 never matches.

`0x005bd8b0` (analog): returns 0 with out = −1 if `0x005bbff0` says every source is disabled; otherwise sums the **engine analog values** of pad buttons A and B (`0x00db88a0` — raw value, not thresholded, and not gated by the enable masks; out = 0 if non-zero), adds the positive component of the bound stick direction (`0x005ba6b0`, honouring the analog block bytes; out = 1 if that is non-zero), and only if the total is still within 1e‑4 of 0 takes 1.0 from the first held key or mouse button (out = 2); the result is clamped to ≤ 1. `0x005bdb60` is the same over the previous frame: engine previous value `+0x04` (`0x00db8870`), previous-frame stick components (`0x005ba810`), and for keys/mouse "was down last frame" = (held and not pressed-this-frame) or released-this-frame. `0x005bde80(cba, t, out)` returns true when the previous value is ≥ t and the current value is < t (and the action is not fully disabled) — a release through a threshold; its out code comes from the previous-frame evaluation.

`0x005bbff0` (all disabled): true when every bound pad bit is clear in `+0x58` and every bound key/mouse button has `+0` clear; an action with no bindings counts as disabled. The stick direction is not considered. One special case: an action with no pad buttons, no key 1, a key 2, no mouse 1 and a mouse 2 always reports false (CONFIRMED, purpose unknown).

#### 1.8.4 Validation of the pressed/held reading

Push survey of all 73 + 83 call sites (CONFIRMED): `CBA_OFC_SPRINT` (`0x12`) is read through `0x005bd7b0` at seven sites, including the per-human motion callback (`0x00961f3f`) and `0x0099e1af` **[§5: the vehicle-contact replay `0x00961d70` and the velocity composer `0x0099d1b0`]**, where only a level makes sense; `CBA_OFC_SPRINT_TOGGLE` (`0x13`) is read through `0x005bae10` (`0x005ab9ae`); `CBA_OFC_WALK` (`0x14`) through `0x005bd7b0`; `CBA_OFC_CROUCH` (`0x19`) through both (`0x005b2e14` held with an out pointer, then `0x005b2e1f` pressed). That is the pattern expected only if `0x005bae10` is the edge and `0x005bd7b0` the level.

### 1.9 Axis-action queries

#### 1.9.1 Signatures (CONFIRMED by full listing)

| Function | Signature | Returns | Sites |
|---|---|---|---|
| **`0x005bb530`** | (caa, int* out) | float in [−1, 1] — **the axis value** | 74 |
| `0x005bb980` | (caa, int* out) | float in [−1, 1] — same, but from the **raw** (non-dead-zoned) sticks and the mouse record's second float | 4 |
| `0x005bb140` | (caa, int* out) | float, **unclamped** — same, but each stick axis passed through the response curve 0.9·v³ ± 0.1 (`0x005b9f60`/`0x005b9fe0`: plus 0.1 when positive, minus 0.1 when negative, 0 stays 0) | 8 |
| `0x005bc5f0` / `0x005bc680` | (caa, int* out) | bool — **positive / negative half went down** this frame (keys and mouse buttons only) | 3 / 3 |
| `0x005bea10` / `0x005beab0` | (caa, int* out) | bool — positive / negative half **held** (keys and mouse buttons only) | 4 / 4 |
| `0x005bc1d0` | (caa, bool secondary) | int — primary / secondary stick-axis code | 4 |
| `0x005bc510` / `0x005bc540` / `0x005bc570` / `0x005bc5a0` | (caa, int* a, int* b) | bool "both bound" — key pair 1, key pair 2, mouse pair 1, mouse pair 2 | 2 each |

**The three evaluators do not range-check the CAA value** (only the half queries reject values above `0x21`); every call site passes a constant (HIGH CONFIDENCE from the push survey of `0x005bb530`'s callers in the scoping note).

#### 1.9.2 Evaluation of `0x005bb530` (CONFIRMED)

1. **Primary stick axis** (`+0x00`): code 0/1 → stick 1 x/y, 2/3 → stick 0 x/y, read from the dead-zoned context values unless that stick's analog block byte (`+0x24` x, `+0x25` y) is set, in which case 0. If the value is exactly non-zero, apply the primary invert byte and go to step 3.
2. Otherwise the **secondary stick axis** (`+0x08`) the same way, then apply the secondary invert byte.
3. If the pad value is **not** within 1e‑4 of 0, set out = 1 and skip to step 6 — **the gamepad has priority**.
4. **Keys and mouse buttons**: start from the pad value (≈ 0) and add +1 for each *held* positive input and −1 for each held negative input, in the order key pair 1 (`+0x10` +, `+0x14` −), key pair 2 (`+0x18`, `+0x1c`), mouse pair 1 (`+0x20`, `+0x24`), mouse pair 2 (`+0x28`, `+0x2c`). Keys use record bytes `+1` and `+0`; scan code 0 and mouse −1 contribute nothing.
5. **Mouse axis**: only if that sum is within 1e‑4 of 0, add the context mouse value for `+0x30` (`+0x9dc` for X, `+0x9e8` for Y) when its enable byte is set. If the final value is not within 1e‑4 of 0, out = 3.
6. Clamp to [−1, 1] and return.

There is **no game-side dead zone**: the only dead zone is the engine's per-axis one already folded into the context (§1.3.3), plus the 1e‑4 "is zero" tolerance. The value is not scaled by time; mouse values are per-frame counts × sensitivity.

#### 1.9.3 The axis out code

−1 none, **1** gamepad stick, **3** keyboard/mouse (keys, mouse buttons or mouse axis). The code 2 is assigned when the key sum is non-zero and then unconditionally overwritten by 3 in the same pass, so it is never observable (CONFIRMED in all three evaluators). The camera-provenance logic of §1.5 depends on exactly these two values.

#### 1.9.4 Half queries

`0x005bc5f0` tests key `+0x10` and `+0x18` and mouse `+0x20` and `+0x28` for the down-edge (`+2` with `+0`); `0x005bc680` the negative fields `+0x14`, `+0x1c`, `+0x24`, `+0x2c`. `0x005bea10`/`0x005beab0` are the same with the held byte. Out is 2 when satisfied, else −1. No pad source is consulted.

### 1.10 Stick-direction primitives

A stick-direction code *d* (−1 none, 0…7) selects stick ⌊*d*/4⌋ and direction *d* mod 4: **0 up (+y), 1 down (−y), 2 right (+x), 3 left (−x)** — matching the vocabulary `LEFT_STICK_DIR_UP`=0, `_DOWN`=1, `_RIGHT`=2, `_LEFT`=3, `RIGHT_STICK_DIR_*`=4…7 (CONFIRMED: the jump tables `0x005ba938`, `0x005ba9d8`, `0x005baa78`, `0x005bab18`, `0x005ba7e8`, `0x005ba898` map each code to these primitives). Thresholds are **±0.4** (`0x01117b70`, `0x01118140`), on the dead-zoned values:

| Helper (per code) | Test | Uses block byte |
|---|---|---|
| held — `0x005ba8c0` → `0x005ba110` / `0x005ba0d0` / `0x005ba0a0` / `0x005ba060` | current component beyond ±0.4 | must be clear |
| pressed — `0x005ba960` → `0x005ba1e0` / `0x005ba230` / `0x005ba190` / `0x005ba140` | current beyond ±0.4 **and** previous frame's not beyond | must be clear |
| pressed-and-consume — `0x005baa00` → `0x005ba320` / `0x005ba370` / `0x005ba2d0` / `0x005ba280` | as pressed, then sets the block byte | must be clear |
| pulse-and-consume — `0x005baaa0` → `0x005ba420` / `0x005ba450` / `0x005ba3f0` / `0x005ba3c0` | digital pulse `+0x1c` < 0 (up), > 0 (down), `+0x18` > 0 (right), < 0 (left); zeroes the pulse and sets the block byte | must be clear |
| analog — `0x005ba6b0` (returns in `xmm0`) | positive part of the signed component (via `0x005b9e60`/`0x005b9ea0`, which honour the analog block bytes) | `+0x24`/`+0x25` |
| previous analog — `0x005ba810` (returns in `xmm0`) | positive part of the previous-frame component | none |

### 1.11 The consumption model

#### 1.11.1 "Clear latched" — `0x005bbf60` (33 instructions; 14 call sites)

Sets all twelve stick block bytes to 1, zeroes both pad enable masks (`+0x58`, `+0x60`), and clears the enable byte of all 256 key records, all 7 mouse records and both mouse-axis records. **Every query then returns false/0 for the rest of the frame**; the next latch re-arms everything. It does not clear the underlying held/pressed data. **CONFIRMED.** (Callers include `player_controls_disable`'s apply `0x009e2dd0`, the locked-state enter `0x005aa5f0` and the menu branch of state 2's handler, §2.7.)

#### 1.11.2 Per-input consumption

- `0x005bdfe0` / `0x005bdef0` consume the source that satisfied them; `0x005bbdd0` consumes every pad button, key and mouse button bound to the action (stick directions are not consumed by it).
- Consumption is **per physical input, not per action**: clearing pad bit *b* from `+0x58`/`+0x60`, or a key's `+0` byte, makes every *other* action bound to the same input read false (pressed *and* held) for the rest of the frame. Axis evaluation of a consumed key also stops counting it.
- Hence **the order in which consumers run within a frame is part of the contract** (fixed by the world update, `review/scoping-player.md` §1.0): e.g. a cutscene skip that consumes `CBA_SKIP_MODAL_SEQUENCE` hides that press from everything evaluated later in the frame.

#### 1.11.3 Full clear — `0x005bc830`

Zeroes the stick records, all five pad masks, all key records (including enable bytes), `+0x9f4`, the mouse-button records and the mouse-axis records, resets the engine keyboard (`0x00dd0190`) and mouse (`0x00dcfe20`) state, and clears `+0x9f5`/`+0x9f6`. Used only by the inactive-window path of §1.1. **CONFIRMED.**

### 1.12 Remaining API surface

#### 1.12.1 Raw access

- `0x005badb0(stick, float* x, float* y)` — raw (non-dead-zoned) stick values, 0 for an axis whose analog block byte is set (6 sites).
- `0x005bc2c0(x, y, float* ox, float* oy)` — scales (x, y) down to unit length when longer than 1 (with the same tolerance as §1.4 step 7).

#### 1.12.2 Options

- Mouse sensitivity: `0x005bc3c0`/`0x005bc3d0` (get X/Y), `0x005bc3e0`/`0x005bc440` (set, clamped between the constants at `0x012a2ee0`/`0x01115d6c` and `0x012a2e80`).
- `0x005be920` returns `0x01411b14` while `+0x9f6` is set, else bit 2 of `0x01411b00`; `0x005be940` likewise `0x01411b15` / bit 1 (7 / 2 sites). HYPOTHESIS: look-invert, chosen per device family. `0x005be800` maintains bit 2 from a profile flag (`0x00705a80()` `+0x300c` record byte `+8`).
- `0x005bc180` / `0x005bc190`: current On_Foot / Driving scheme index; `0x005bd780(bool driving)`: pointer to that scheme's slot.

#### 1.12.3 Cursor capture

`0x005bac90` (called by the latch gate and at the end of a rebuild) has two branches. Branch A is taken when (byte `0x02317b05` is clear, `0x007205d0()` is false and the current game mode `0x00706ab0()` is 5) or when `0x00bc70b0()` is true: it calls `0x00dcfc50(1)` and captures the mouse (`0x00dcfe70`, recorded in `0x01411b01`). Branch B otherwise: `0x00dcfc50(0)`; then, when `0x005df0c0()` is 1, capture and clip the cursor to the window's client rectangle (`GetClientRect` + `ClipCursor`, recorded in `0x01411b02`), else release capture and clip. `0x005bc4a0` releases both unconditionally (8 sites). **CONFIRMED** (listing); what `0x00dcfc50`'s argument selects (relative-mouse mode vs. visible cursor) and the meaning of the predicates are HYPOTHESIS-level and not needed by the query contract.

#### 1.12.4 The stick sector / rotation tracker `0x005c2700` (not part of the contract)

Runs after the latch (§1.1 step 2d). For each stick it computes an 8-way sector (0–7, or −1 below the magnitude threshold `0x0143c5f0`) through `0x005c2190` — stick 0 from the raw left stick when the gamepad is active, otherwise from `CAA_WALK_TURN_LEFT_RIGHT`/`CAA_WALK_FORWARD_BACKWARD`; stick 1 from the raw right stick, or from the camera axes scaled by `0x012e8b08` while `+0x9f6` is set — and feeds a 20-entry history ring (`0x0143c630` + `0x58`·stick) and a rotation detector (`0x005c2540`, state at `0x0143c610` + 12·stick, results at `0x0143c5f8`+). It reads input **only through the query API**, so a port keeps it unchanged. **CONFIRMED** for the call structure and sources; the rotation rule itself is HIGH CONFIDENCE (read, not exercised).

### 1.13 What a replacement backend must reproduce

To keep all consumers unchanged, a port must, once per frame **before** the world update and after polling its own devices:

1. Fill the context of §1.2 with the semantics of §1.3–§1.4: dead-zoned and raw stick values (per-axis rescaled dead zone, +y up), previous-frame values, the sign-flip-to-zero rule, ±0.5 direction pulses with 500/150 ms repeat, pad down/edge/repeat masks with trigger hysteresis, per-key and per-mouse-button `{enabled, held, down-edge, up-edge}`, mouse deltas scaled and clamped to the unit circle, and the wheel-while-middle-held rule.
2. Re-arm every enable byte/mask each frame; implement consumption exactly as §1.11.
3. Keep `+0x9f5`/`+0x9f6` per §1.5 (several systems, including `game_is_active_input_gamepad`, read them).
4. Build the two binding tables as §1.6–§1.7, including the scheme layering order and the un-apply-on-switch behaviour.
5. Freeze to the cleared state while the window is inactive or minimised.

The query functions themselves are pure functions of that state and can be ported as specified in §1.8–§1.10.

### 1.14 Corrections and cross-spec flags raised by U0 (no other spec edited)

- **`review/scoping-player.md` §1.1:** `0x005bae10` = pressed-edge and `0x005bd7b0` = held (swapped there); `0x005ba4f0` reads the down-edge byte, `0x005bc8b0` the held byte; the out code is −1/0 pad/1 stick/2 key-or-mouse for buttons and −1/1 pad/3 KB-M for axes; only the half queries range-check the axis index (the evaluators do not); the "binding-set name `A`" is the default On_Foot/Driving **control-scheme** letter.
- **`spec-tables-ui-controls.md` §2.2:** the four appliers named there as "unread" are now specified (§1.7.2); they and `0x005c1f70` touch only the runtime tables, consistent with that section's narrowed OPEN (the save-table builder is still among the other readers). **§2.3**: the `Key_Pos`/`Key_Neg` pairing is confirmed by the runtime consumer, not only the loader's store order (§1.7.4). **§2.2/§2.3**: rows whose `Key`/`Alt_Key` (or any of the four axis keys) are absent (−1) are **dropped at application** — `UNBOUND` (0) and absent (−1) are not equivalent at runtime either.
- **`spec-lua-api-behaviour.md` §2.6:** writer of `0x0141250d` is `0x005bc9d0`, rule in §1.5 (same finding as the scoping note, now with the full rule).

---

## 2. U1 — The control-state machine

### 2.1 The state table `0x012e6bb8`

8 rows of `0x14` bytes. Slot roles are fixed by the dispatcher (§2.3): `+0x00` **enter**, `+0x04` **exit**, `+0x08` **per-frame**, `+0x0c` **secondary hook**, `+0x10` unused (no reader anywhere: an exhaustive data-reference scan finds no use of `0x012e6bc8`). Static contents, **CONFIRMED** by direct read:

| Row | Enter | Exit | Per-frame | Secondary | `+0x10` | Meaning |
|---|---|---|---|---|---|---|
| 0 | `0x005aa5f0` | — | `0x005b2170` | — | — | locked (pushed by mission/cutscene code) |
| 1 | `0x005aa5f0` | — | `0x005b2170` | — | — | locked (pushed by UI, menus, `player_controls_disable`) |
| 2 | `0x00754410` | `0x005ac4f0` | `0x005b7430` | `0x005b69c0` | `0x00754410` | **normal play** (the default) |
| 3 | — | — | — | — | — | empty |
| 4 | — | — | — | — | — | empty |
| 5 | — | — | — | — | — | empty (the latch's state-5 test, §1.4 step 5, is unreachable) |
| 6 | — | — | — | — | — | empty — pushed once by UI code (`0x007e900c`): **no player-control code runs at all** |
| 7 | `0x00754410` | `0x00754410` | `0x005c2800` | — | — | free camera (pushed by the camera band) |

`0x00754410` is the shared single-`RET` stub (`review/scoping-player.md` §6). "—" is a null slot, which the dispatcher skips.

### 2.2 Globals

| Address | Contents | Static value |
|---|---|---|
| `0x0140e62c` | current state | 0 (`.bss`) |
| `0x0140e628` | previous state | 0 |
| `0x012e6c58` | request stack header: data pointer | `0x012e6c64` (storage inline, 5 × 12 bytes to `0x012e6ca0`) |
| `0x012e6c5c` | capacity | **5** |
| `0x012e6c60` | count | 0 |
| `0x0140e634` | handle counter | 0 |

Each stack entry is 12 bytes: `+0` requested state, `+4` handle, `+8` always 0. **CONFIRMED.**

### 2.3 Dispatch `0x005aa6c0` (34 instructions)

Called once per world update at `0x00702ad4`, and from the cutscene/cinema per-frame at `0x00bdbc59`. **CONFIRMED** by full listing:

1. **Desired state** = the `+0` field of the top (last) stack entry, or **2** when the stack is empty.
2. If it differs from the current state: call the current row's **exit** (if non-null); previous ← current; current ← desired; if the new row's **enter** is non-null, call `0x00d48e50` (a two-instruction "return 1" stub — an inert hook) and then the enter.
3. Tail-jump to the current row's **per-frame** (if non-null).

So a transition and the new state's first per-frame happen in the **same** world update. Because the enter of rows 0/1, `0x005aa5f0`, is "clear latched, then jump into `0x005b7430`" (state 2's per-frame), **the frame that enters a locked state runs the normal-play handler once on cleared input, then the locked per-frame** (§2.7).

**Ordering:** the UI-layer per-frame `0x007c7270` is called immediately before, at `0x00702acf` (and at `0x00bdbc1f` before `0x00bdbc59`). It matters for §2.8.

### 2.4 Secondary dispatch `0x005aa610` (7 instructions)

Called once per world update at `0x00702e9b`, after physics, character movement and the streaming focus. Tail-jumps to the current row's `+0x0c` slot if non-null; only row 2 has one (`0x005b69c0`, §2.7). **CONFIRMED.**

### 2.5 Push, pop, remove, reset, current-state getter

- **Push `0x005aa780(state)`** (23 instructions; `cdecl`, **one** argument — the scoping note's second "extra" argument does not exist **[corrects scoping §1.2]**): increments the handle counter; if count + 1 ≤ capacity, appends `{state, handle, 0}` and increments count. **Returns the new handle whether or not it was stored** — on a full stack the push is silently dropped and the returned handle matches nothing. Handles are pre-incremented from 0, so **the first handle is 1 and 0 never names an entry**; every caller uses 0 as "nothing pushed". **CONFIRMED.**
- **Pop `0x005aa740(handle)`** (21 instructions): searches the stack from the top down for the entry whose handle matches; if found, removes it with `0x005aa640(index)` (`__thiscall` on `0x012e6c58`: decrements the count and shifts every later entry down by one, preserving order); if not found, does nothing. So **a lock can be released out of order**, and the desired state then becomes whatever entry is now on top. **CONFIRMED.**
- **Reset `0x005aa690`** (5 instructions; one caller, the start-up sequence at `0x00708459`): calls `0x005aed70` (zeroes four control floats `0x0140e698`/`0x0140e69c`/`0x0140e6a4`/`0x0140e6a8` and the bytes `0x0140e6e7`/`0x0140e6f4`, passes those two bytes to `0x0086d770` together with the names `grenade_disable` / `grenade_disable_veh` (HIGH CONFIDENCE: a named-variable registration, possibly network-replicated), and sets `0x012e6de8`/`0x012e6df0` to −1), then sets current = **1**, previous = −1, count = 0. The next dispatch therefore performs a 1 → 2 transition (calling row 2's no-op enter). **CONFIRMED.**
- **Getter `0x005aa630`**: returns the current state (6 callers: `0x005b0407`, `0x005b3fa9`, the latch `0x005bd23e`, `0x00846c61`, `0x009aebbb`, `0x009bba85`).

### 2.6 Every push and pop site

Exhaustive raw rel32 scan: **17 calls to push and 17 to pop** (the scoping note counted 12 push sites). Pushed values from the immediately preceding `push`; handle storage from the following store. **CONFIRMED.**

| Push site | State | Handle kept in | Matching pop site(s) |
|---|---|---|---|
| `0x00564ce2` (camera band, `0x00564cd0`; also pushes camera mode 3) | 7 | `0x013c873c` | `0x00564d46` (also pops camera mode 3) |
| `0x006155e6`, `0x00617d2d` | 0 | `0x014b2dd8` | `0x0061508a`, `0x006151aa` |
| `0x0061dabb` | 1 | `0x014b4424` | `0x0062280d`, `0x00624ceb`, `0x006252fc` |
| `0x00689c1f` | 1 | object field `+0x18` | `0x0068007d` |
| `0x006d890c`, `0x006d8d2c`, `0x006d8e69` (mission/cutscene code) | 0 | `0x014c7a10` | `0x006d4384` |
| `0x007af9a9` | 1 | `0x012fcae0` | `0x007af590` |
| `0x007df5d1` | 1 | `0x02299ea8` | `0x007d973f` |
| `0x007e1d76` | 1 | `0x0229cd90` | `0x007e1b1d` |
| `0x007e900c` | **6** | `0x022b220c` | `0x007e906d` |
| `0x0080de66` | 1 | `0x022cce60` | `0x0080df40` |
| `0x0083c23b` | 1 | `0x023151a0` | `0x0083be9e` |
| `0x009e2ef2` (`player_controls_disable` apply `0x009e2dd0`) | 1 | `0x0263ae3c` | `0x009e2f4e` |
| `0x00b94c19` | **2** | **discarded** | none |
| `0x00bcd0e1` | 1 | `0x02946b78` | `0x00bcd0cd`, `0x00bcbe7d` |

Two observations: the push of **2** at `0x00b94c19` discards its handle, so that entry can only be removed by a reset — it forces normal play on top of whatever is beneath it; and every keeper tests its stored handle for 0 before popping and zeroes it afterwards, which relies on the "first handle is 1" property.

### 2.7 Row handlers (entry points; their bodies belong to later units)

- **`0x005aa5f0`** (rows 0/1 enter): `0x005bbf60` (clear latched), then tail-jump to `0x005b7430`. **CONFIRMED.**
- **`0x005b2170`** (rows 0/1 per-frame; not a defined function in the shared project): if a cutscene is in progress (`0x00720640`), consume-pressed `CBA_SKIP_MODAL_SEQUENCE` (`0x8e`) and, if it fired, tail-call the skip request `0x00720830`; done. Otherwise: camera pre-update `0x00565c60`, `0x0056b220(−1.0)`; when a local player exists and the cinema editor (`0x00bc5610`) is not active **(CORRECTED 2026-10-10: `0x00bc5610` is not a general "editor active" test — it is `0x029443bc==2||3`, true only while a machinima recording is stopping/finalizing, see `spec-lua-api-behaviour.md` §3.7's correction. It is false both when idle AND throughout an entire in-progress recording, and true only in the brief stop/finalize window. This is the overwhelmingly common (false) case: the handler takes the ordinary no-op-movement/stance-reset path below in both idle play and during active recording — it only swaps to the camera-update tail-call during that brief finalize window.)**, call the movement routine `0x005aee30` with the player's own position (`+0x40`/`+0x44`/`+0x48`) and 0 — i.e. no new intent — then the stance resets `0x00985df0(player, 0, 1, 1)` and `0x009fef60`, and if `0x00611650(player)` is true copy "consume-pressed `CBA_MENU_DP_DOWN` (`0x71`)" into bit `0x08` of `0x0140e684`. Finally, if the cinema editor is active **(same predicate — i.e. the recorder is stopping/finalizing)** and `0x00707490()` is true, tail-call the camera update `0x00566a40`. **CONFIRMED.**
- **`0x005ac4f0`** (row 2 exit): if `0x007f0130()` then `0x007f0140(1)`. HYPOTHESIS: closes an on-screen prompt layer.
- **`0x005b7430`** (row 2 per-frame, 248 instructions) — the skeleton, **CONFIRMED** by full listing, consistent with `review/scoping-player.md` §1.3 with these additions:
  - it clears byte `0x0140e6e5` first, and maintains two flags: `0x0140e6f8` "**controls ran this frame**" (set after the paused check, cleared by every early-out branch; read by the secondary hook) and a local "**run the camera hook**" flag;
  - paused path (`0x00707490` or `0x00831f70`): cutscene-skip test as above, camera defaults `0x0056b220(−1.0)`/`0x0056b3b0(1.0)` when `0x00831f70` is true, clear `0x0140e6f8`, return;
  - pre-hook `[0x0111f1d0 + 12·[0x012fd5c8]]` with the local player, unguarded (§2.8);
  - gates in order: menu `0x007c1ec0` (zero the six context words, clear latched, zero the four control floats, movement routine with own position) → dead `0x0096f4f0` (non-terminal) → camera mode 3 → cinema editor → cutscene (skip via `0x8e`) → script-controlled (`+0x28ac` bit `0x02`: when context bit 1 is set, copy "consume-pressed `CBA_MENU_DP_DOWN`" into `0x0140e684` bit `0x08`; when rig flag `0x10` is set, zero `+0xa50`…`+0xa5c`, `+0xa60`, `+0xa68`) → controls disabled (`+0x1d99` bit `0x08`: movement routine with own position) → otherwise, unless rig flag `0x1000000`, the action dispatch `0x005b73c0`;
  - tail: return if `0x012fd5c8` is −1; camera hook `[… + 8]` when the local flag is set and the slot is non-null, else `0x0056b220(−1.0)`/`0x0056b3b0(1.0)`; camera pre-update `0x00565c60` unless paused; post-hook `[… + 4]` only if `0x0140e6f8` is still set.
- **`0x005b69c0`** (row 2 secondary): if `0x0140e6f8` is set, run the late context dispatch `0x005b6420` — `0x005ae280` when `0x009b91e0(player)`; else context bit 2 on foot with no vehicle and no rig flag → `0x005ae3f0` (reads `CBA_OFC_ATTACK_PRIMARY`/`SECONDARY` analog values against 0.4); bit 4 in vehicle → `0x005b4e60`; **bit 8 → `0x005ac250`, which reads `CBA_SWC_EXIT_SATELLITE_MODE`, `CBA_MENU_ABORT`, `CBA_SWC_FIRE_GUIDED` and `CBA_SWC_FIRE_FREE`** — so the scoping note's unnamed "third context" (context bit 8) is the **satellite-weapon control context** (HIGH CONFIDENCE from the action names); otherwise `0x009fef60`. If the flag is clear: `0x007f0130`/`0x007f0140(1)` as in the exit, then `0x009da750`. **CONFIRMED** for the dispatch.
- **`0x005c2800`** (row 7 per-frame, 412 instructions; listed, not read line by line): builds the camera orientation (`0x013c8790`…`0x013c87b0`) and position (`0x013c8750`) itself and, under a mode-3 / pause test, calls the camera update `0x00566a40`. HYPOTHESIS (unchanged from scoping): a free/debug camera.

**Structural note, 2026-10-08 (closes a gap between this section and `spec-weapons-runtime.md`'s own fire-path hunt):** on-foot per-frame controls run **two independently-dispatched action trees, not one**. The "early" tree is `0x005b73c0` (§3.3 below: melee/jump/crouch/sprint/pratfall/skydive/avatar, off this section's own row-2 per-frame `0x005b7430`). The "late"/secondary tree is `0x005b69c0`→`0x005b6420`→`0x005ae3f0` (documented in the bullet above only as a row-handler entry point, never previously cross-referenced against weapon code) — attack-primary/secondary and the grenade slot live here, and `0x005ae3f0` is CONFIRMED (`spec-weapons-runtime.md` §2.2b) to be the player's actual fire-input dispatch, resolving the equipped item's own vtable `+0x8c` slot. The two trees were investigated independently by different units; this note is the first place they are tied together.

### 2.8 The index `0x012fd5c8` — scoping OPEN item 2 resolved

**What it is.** `0x012fd5c8` is the **current UI screen state** of a small UI state machine (CONFIRMED by listing of `0x007c6610`…`0x007c69b0`): a stack of state ids at `0x02289ab0` with depth `0x02289b14`, a per-state 24-byte callback table at `0x02289ae4` (`+0` enter, `+4` exit, `+0x0c` per-frame), a pending request `0x012fd5c4`, a push-mode byte `0x02289b18` and a pending-pop count `0x02289b1c`. Request functions: `0x007c68f0(id)` (request with push) and `0x007c6850(id)` (request replacing the stack). The processor `0x007c6610` performs pending pops (after each pop the current state becomes the entry below, or **0** when the stack empties), then, if a request differs from the current state and is not −1, calls its enter, pushes or replaces, and **stores it into `0x012fd5c8`**. Getter `0x007c68e0`; `0x007c6830` tests "not paused and state 0".

**Values.** Push survey of all request sites: `0x007c68f0(1)` ×4 (`0x0065c4dc`, `0x00686dec`, `0x0068a351`, `0x007b80f7`), `0x007c6850(0)` ×4 (`0x00702fc2`, `0x007e3a79`, `0x007e5780`, `0x007e57eb`), `0x007c6850(1)` ×1 (`0x007e534e`). **No other value is ever requested**, so the only values the index can hold are −1, 0 and 1 — matching the 2-row profile table `0x0111f1d0` (row 0 populated, row 1 empty). The bytes before the table (`0x0111f1c4`, what index −1 would read) are float constants, so a pre-hook call at −1 would jump to garbage.

**Where −1 is written.** Only by `0x007c6870` (which immediately re-processes a pending 0 in the same call, so −1 is never visible) and by `0x007c6990` (stack emptied, index left at −1), whose two callers are **mode 5's exit `0x00703f50`** and the teardown sequence at `0x0070734d`.

**Why state 2's handler never sees −1.** Mode 5's enter is `0x00702f90` (CONFIRMED: it is the `+0x00` slot of the descriptor registered for mode 5 at `0x007041c4`, alongside per-frame `0x00703c00` and exit `0x00703f50`), and it calls `0x007c6850(0)`. Every path into `0x005b7430` — the dispatcher at `0x00702ad4` and at `0x00bdbc59`, directly or through the rows-0/1 enter `0x005aa5f0` — is preceded in the same update by `0x007c7270` → `0x007c6610` (`0x00702acf`, `0x00bdbc1f`), which turns that pending 0 into the current value. Between mode 5's exit and its next enter, mode 5's per-frame (and with it the world update — whose single caller is `0x00703f04` inside `0x00703c00`) does not run. The second dispatcher site belongs to **game mode 7's** per-frame `0x00bdbbb0` (registered at `0x00bdc132` with enter `0x00bdbb20`, exit `0x00bdbed0`); it calls the dispatcher only while a cutscene is in progress (`0x00720640`). Game modes form a **stack** (`0x01503b50`, top index `0x012f4a80`, at most 15; read in `0x00706be0` and the per-frame walker `0x007068d0`): a mode's exit (`+0x04`) runs only when *it* is popped, a push runs only the new mode's enter (`+0x00`), slot `+0x0c` is the per-frame for uncovered modes and `+0x14` a per-frame that also runs when covered. Mode 7 places its per-frame in `+0x14` and does not request a UI state in its enter, i.e. it runs **on top of** mode 5, whose UI state (0 or 1) is therefore still current. **Resolution: `0x012fd5c8` is 0 or 1 whenever state 2's per-frame runs; the missing −1 guard on the pre-hook is safe in shipped flow** (HIGH CONFIDENCE — every writer of the index and both paths to the reader are CONFIRMED; that mode 7 is only ever pushed above mode 5, never entered with mode 5 absent, was not traced through every mode-push site — OPEN 9). The tail hooks' −1 tests are defensive. When the value is 1 (a full-screen UI), row 1 of the profile table is empty, so no pre-hook, camera hook or movement hook runs.

### 2.9 Agreement with `spec-lua-api-behaviour.md` §7.22

§7.22 (updated 2026-10-03) states that `player_controls_disable` pushes state 1 with `0x005aa780(1)`, stores the handle at `0x0263ae3c`, and pops that exact handle with `0x005aa740` on re-enable, on the 8-row machine with current/previous at `0x0140e62c`/`0x0140e628` and a 5-entry stack at `0x012e6c58`. **All of that agrees with §2.2–§2.6.** Two details that section can now cite (not edited here): push takes a single argument and returns a handle that is never 0; and a push onto a full stack (5 entries) is silently dropped while still returning a non-zero handle, so the later pop is a no-op. The other calls in that body: `0x005bbf60` is "clear latched" (§1.11.1); `0x00db8680` was not read in this unit.

### 2.10 The handle `0x0263ae3c`'s value at mission start, and the player's `+0x1d99` flags at spawn — Team B's two blockers, answered

**Question A: is `0x0263ae3c` valid (non-null, stale) at mission start, or genuinely null, lazily created?**

**CONFIRMED null at mission start — this is not a HIGH, it is a direct empirical result.** Three independent facts, found this pass by a whole-`.text` reference scan and a PE section read, each confirming the others:
1. **Image-load value.** `0x0263ae3c` sits inside `.data`'s zero-filled extension (its file offset within the section is past `SizeOfRawData`, inside the zero-filled remainder up to `Misc_VirtualSize`) — the loader zero-fills it at process start, the same way `.bss` works. So it is 0 before any code runs at all.
2. **An explicit, unconditional reset exists independent of the Lua natives.** `0x009e1fc0` (one caller, see below) unconditionally stores `0` into `0x0263ae3c`, with no null-check or read first — it does not rely on the handle already being 0, it forces it. Its one caller is `0x00941c90`, a short sequence of eight no-argument subsystem resets (`0x009b70e0`, `0x009e1fc0`, `0x009d3f90`, `0x004dd260`, `0x00985b40`, `0x0097e250`, `0x009adaf0`) that tail-jumps into the local-player getter `0x009da4e0` (the same singleton getter `review/scoping-player.md` §1.5 and §4.1 above cite) — i.e. this is a "reset session state, then hand back the local player" routine, the shape of a new-game/session start-up call, not a per-frame or per-object one.
3. **The only create/destroy pair is `player_controls_disable`/`_enable`'s own apply `0x009e2dd0`** (§2.6, `spec-lua-api-behaviour.md` §7.22): disable creates the handle only when it reads 0; enable releases and re-zeroes it only when it reads non-zero. A whole-`.text` reference scan of `0x0263ae3c` (6 total references) found no other writer and no other reader — the mechanism is fully self-contained and self-balancing: every disable that finds it null creates exactly one entry, and every matching enable removes exactly that entry and re-nulls it.

**So:** at the start-up sequence reached through `0x00941c90` (new game / session start), the handle is forced to 0 regardless of anything a previous session left behind. Within a session, it stays null between missions as long as each mission's own `player_controls_disable` is matched by a `player_controls_enable` (which is what the two natives are *for*) — the mechanism cannot leak a stale non-zero handle across a clean disable/enable pair. **This was not traced through every individual mission's own start/end script** to confirm each one truly pairs the calls (that is a mission-table audit, out of scope here), but the code-level guarantee — null at session start, self-balancing thereafter — is CONFIRMED by disassembly, not inferred. A port treating the handle as "0/null until the first disable call of the session or mission" will match the original.

**Question B: what does spawn leave the player's `+0x1d99` bits (`0x08`/`0x10`/`0x20` — controls/movement/actions disabled) as?**

**HIGH CONFIDENCE, not CONFIRMED: all clear (0) — enabled.** This pass read both the player constructor `0x009de970` and the human constructor `0x00954340` in full (a disassembly covering well past each one's own length): **neither writes `+0x1d99` at all.** A further whole-`.text` scan found every writer of this byte in the entire binary — 25 instructions across roughly a dozen functions — and all of them are narrow, semantic bit operations (the three Lua-native setters already specified, plus two further bits this pass found and does not further identify: `0x40` and `0x80`, the latter contradicting `spec-lua-api-behaviour.md`'s note at the `0x80`-setting native that the bit is "never cleared" — a clearer at `0x00e0e919` exists; flagged for that file's owner, not resolved here). **No dedicated reset/initialiser for this byte exists anywhere in the binary.** Its value at construction is therefore whatever the player object pool's backing allocation already held — this project's established reading of an unconfirmed pool allocation is "zero-filled unless shown otherwise" (`spec-lua-api-behaviour.md` §34.5's `+0xe3` finding uses the identical reasoning and the identical caveat); the pool's own raw backing allocator was not traced to CONFIRM the zero-fill directly, which is why this stays HIGH rather than CONFIRMED. **Practical answer for Team B: treat all three bits (and `0x40`/`0x80`) as clear at spawn** — controls, movement and actions all enabled, nothing latched — consistent with a freshly spawned player being controllable by default.

---

## 3. U2 — Normal-condition movement and the intent fields

**Scope.** Condition-table row 0 (`0x0111f1e8`, the human's condition field `+0xcc8` = 0) as the player's per-frame controls see it: the classify slot `0x005ac620` and the six context words it writes; the action dispatch `0x005b73c0` that §2.7 stopped at, with its targets; the look slot `0x005ab6a0`; the movement slot `0x005b3e80` and everything it calls down to the intent writer `0x005aee30`; the on-foot action handler `0x005b2ca0` and its children; and the stance bytes `+0x1d94`…`+0x1d97`. What the animation network does with the intent is U3 (`spec-anim-runtime.md` §4.2 already records the reader side); vehicle controls are U7; other condition rows are U8.
**Method.** As U0/U1: Ghidra-free. A private recursive-descent capstone walker over `exe_va.py` (its instruction counts match the scoping note's census exactly for all 15 functions of that census this unit read: 452, 31, 420, 33, 186, 603, 325, 64, 207, 114, 268, 1307, 317, 376, 210; the in-vehicle handler `0x005b5c50` was not walked), a full linear sweep of `.text` for displacement and absolute-address scans (every "writers of" and "readers of" statement below is from that sweep), a raw `E8`/`E9` + dword scan of the whole image for the reference checks marked "raw scan", push surveys at query call sites, and static reads of `.rdata` tables and constants. Every function in §3.2–§3.6 was read in full unless marked. No Ghidra project was opened. No `.czn_pc` data was touched.
**Names.** Tweak-variable names (`Player_max_run_speed`, `Player_acceleration`, `Crouch_click_sensitivity`, …), table field names (`PantPercentage`, `Control_delay_on_foot`, …), HUD string ids and the AI sub-state names are engine strings read from the executable, quoted as data; each is tied to its global by the registration call that pushes both (cited where used). **Data source (answers a Team B question, 2026-10-03): these are not read from `control_parameters.xtbl` (`spec-anim-runtime.md` §1) — they are the in-code **tweak registry** documented in full at `spec-tables-progression.md` §10.10/§14.15, overridable by the shipped `tweak_table.xtbl` (`<Tweak_Table_Entry><Name/><Value/><Description/><Framework/></Tweak_Table_Entry>`, 601 real base rows). Code never "keys into" the table at runtime: gameplay reads the plain global at each name's fixed destination address (cited inline throughout this section, e.g. `Player_acceleration` → `0x0140e64c`), and at load time the loader (`0x0071B420`/`0x0071B450` → `0x0071B070`) matches each row's `<Name>` case-insensitively against the 586-entry static registry (one static-constructor call site per tunable — `tools/ai_tweak_registry.tsv` lists every name, type, destination and call-site address; `Player_acceleration`'s call site `0x00fefebf` and `Player_max_run_speed`'s `0x0100769f` both fall where this section already cites registration, confirming the two are the same mechanism) and overwrites the destination in place (`0x0071AE40`); an unmatched row is ignored and an unmatched registry entry keeps its compiled-in default.

### 3.0 Results up front (U2)

1. **The move-vector math, closed form** (§3.5.5). With A = `CAA_WALK_TURN_LEFT_RIGHT` and B = `CAA_WALK_FORWARD_BACKWARD` from `0x005bb530` (U0 §1.9.2), and the camera orientation rows R₀ = `0x013c8790`, R₂ = `0x013c87b0`:
   **desired-move `+0x2048` = M · min(A² + B², 1) · normalise( B · flat(R₂) + A · flat(R₀) )**, where flat() zeroes the vertical (y) component and normalises, and M is the stance's maximum speed (`Player_max_run_speed`, `Player_max_sprint_speed`, … §3.5.4 step 5); A² + B² is replaced by 1 while sprinting or diving and by 0.3 for keyboard walk. The magnitude is **quadratic in stick deflection** and the direction is **camera-relative in the horizontal plane**; the camera-rotate axis is **not** part of it. The animation-facing velocity `+0x203c` then chases that vector at `Player_acceleration` · (1 + 0.5·distance) per second and is clamped to length M; `+0xa60` is its length.
2. **The intent is expressed as a target point, not a vector.** `0x005b3e80` builds a world point 5.0 units along the unit move direction (`0x005b31f0`) and passes it with a separate speed scalar s to the writer `0x005aee30(player, point, s)`, which subtracts the position again. The locked states pass the player's own position and s = 0, which is how they write "no intent" (§2.7).
3. **The scoping note's movement chain is not a chain [corrects scoping §1.3/§2].** `0x005ad510` is an alternative branch of `0x005b3e80`, taken only when human byte `+0xa20` is set; its only setter `0x0098d6d0` has **no reference anywhere in the image** (raw scan), so the branch is dormant. The live path is `0x005b3e80` → `0x005ad130` (axes) → `0x005b31f0` (target point) → `0x005aee30` (writer). **`CAA_CAMERA_ROTATE` is read by `0x005ad130` only when its sixth argument is set, which only the dormant branch does** — normal movement never reads it.
4. **Row 0's classify writes `+0x2070` = 3 (bits 1 and 2) on every path [corrects scoping §1.3 and §1.7].** Bit 4 ("driving") is written only by `0x005b3750`, the classify of condition rows 8, 9 and 17; bit 8 only by `0x005aaf00`, the classify of row 13, which also writes `+0x2080` (7 or `0x38`) — confirming §2.7's satellite reading. So the hand-off to the in-vehicle handler is a **condition-row change**, not a bit `0x005ac620` sets; under row 0 a non-zero vehicle handle routes to `0x009fef60` instead.
5. **Stance bytes decoded** (§3.7): `+0x1d94` bit `0x10` = sprint requested and granted by the stamina system (re-derived every frame), `0x20` = walk, `0x40` = sprint-from-standstill burst pending, `0x80` = move attempted while the animation locks movement; `+0x1d95` bit `0x02` = **panting** (stamina at or below `PantPercentage`) **[corrects scoping §1.5's "sprint allowed"]**, `0x10` = weapon scope view active, `0x40` = shoulder-camera toggle latch; `+0x1d96` bits `0x04`/`0x08`/`0x10` = sprint-toggle, walk-toggle and crouch-toggle latches; `+0x1d97` bit `0x08` = stopping, `0x10`/`0x20` = the two sprint styles (`0x20` is the animation's `Weapon Sprint Bool`).
6. **The "dead-zone test" is the crouch-toggle's stick test and it is switched off.** `0x005b2ca0` refuses a crouch toggle while A² + B² ≥ `Crouch_click_sensitivity`, unless byte `0x012e6ced` is non-zero — and that byte is 1 in the image with no writer in `.text`. The other stick test in this area, in `0x005ab930`, cancels keyboard sprint when both walk axes are idle (|value| < 1e‑4) and a 300 ms grace timer has run out.
7. **U0 OPEN 2 partly resolved** (§3.4): the camera input setters apply the per-device option bytes as **look inversion** — horizontal through `0x005be940`, vertical through `0x005be920`.

### 3.1 Where U2 runs (call graph, CONFIRMED by listing)

```
0x005b7430 (row-2 per-frame, §2.7)
 ├─ pre-hook 0x005b3d30 ──► condition row(+0xcc8).+0x00     row 0: classify 0x005ac620   (§3.2)
 │                     └─► 0x005b3c50, 0x005ab330, 0x0060b530 (mask edits, §3.2.4)
 ├─ gates (§2.7) … then 0x005b73c0 action dispatch            (§3.3)
 │     0x005ad830 (D-pad / radio latch) ; on foot 0x005b2ca0 │ 0x005b5c50 │ 0x005ac1f0 │ 0x009fef60
 │     + 0x005b6af0 if +0x2070 bit 1 ; + 0x0060d960 always   (both take &0x0140e6f8)
 ├─ camera hook 0x005ab5b0 ──► row.+0x08                     row 0: look 0x005ab6a0      (§3.4)
 └─ post-hook  0x005ab390 ──► row.+0x04 (only if 0x0140e6f8 still set)
                                                              row 0: move 0x005b3e80      (§3.5)
        0x005b3e80 ─┬─ (+0xa20 set: 0x005ad510 — dormant) 
                    ├─ 0x005ad130   axes A, B (+ modifiers)
                    ├─ camera rows 0x013c8790 / 0x013c87b0, flattened
                    ├─ 0x005b31f0   target point = position + 5·unit(B·F + A·R)
                    ├─ 0x005aee30   intent writer  → +0x2048, +0x203c, +0xa50, +0xa60, +0xa68, +0x1d94/95/97
                    └─ 0x00985df0   locomotion-state update (+0xccc), U3
```

The move slot `0x005b3e80` is shared by condition rows 0, 1, 3, 7 and 12 (`.rdata` dword scan: `0x0111f1ec`, `0x0111f1f8`, `0x0111f210`, `0x0111f240`, `0x0111f27c`); the look slot `0x005ab6a0` by every row except 4, 5, 8, 13, 14 and 17 (static read of the table). Row 13's look slot is `0x005b2540`, row 14's `0x005ad6b0`. **CONFIRMED.**

The writer `0x005aee30` has exactly seven call sites (raw scan): `0x005b412a` (this slot), `0x005b43f3` (row 16's move slot `0x005b4190`), `0x005b4574` (rows 8/17's `0x005b4440`), `0x005b0482` (row 2's `0x005b0350`), `0x005b21df` (rows 0/1 locked per-frame, own position, s = 0), and `0x005b752a`/`0x005b7693` (the menu and controls-disabled gates of `0x005b7430`, own position, s = 0).

### 3.2 The classify slot `0x005ac620` (452 instructions) and the six context words

#### 3.2.1 The words

| Word | Role (consumer evidence) |
|---|---|
| `+0x2070` | **dispatch word** for `0x005b73c0`: bit 1 interaction prompts, 2 on-foot actions, 4 in-vehicle actions, 8 satellite actions (§3.3). Also read by `0x005ad830` (bits 1, 2, 4, 8) |
| `+0x2074` | small flag word. Bit 1 gates the `CBA_GAC_ACTION` stage of the prompt routine (`0x005b70c6`); bit 4 gates `CBA_GAC_RECRUIT_DISMISS` in `0x005ad830` and is also read at `0x0083d8aa`; bit 2 has no reader found by displacement scan (§14, OPEN 14) |
| `+0x2078` | **on-foot action permission mask** — one bit per action family, tested by the on-foot handlers (§3.2.3) |
| `+0x207c` | in-vehicle permission mask (written by `0x005b3750`; read at `0x005ad903` bit `0x100` radio, `0x005ad9e5` bit `0x10000` aim). Row 0 leaves it 0 |
| `+0x2080` | satellite word (row 13 writes 7 or `0x38`; read by `0x005ac2c5`/`0x005ac306`). Row 0 leaves it 0 |
| `+0x2084` | **interaction-prompt enable mask**: bit *n* enables entry *n* of the 22-entry function arrays at `0x0111f2d0`, `0x0111f3d8` and `0x0111f430`, walked by `0x005ac340` and `0x005ac390` (CONFIRMED by listing; the arrays' roles are U6/U8). Entry 15 of `0x0111f2d0` is the vehicle enter/exit handler `0x009e5e80` (scoping §1.7), so **bit `0x8000` = vehicle entry/exit prompt** |

#### 3.2.2 Procedure (CONFIRMED by full listing)

The argument is the player. "Sprinting" below is human byte `+0xccc` = 3 — the predicate `0x00941e60` (`+0xccc` = 3 and not seated in a vehicle) is what `spec-anim-runtime.md` §4.2 row 16 writes as `sprint bool`.

1. **Sub-state shortcuts on byte `+0xccd`** (which `spec-anim-runtime.md` §4.2 rows 1–2 and the climb writer key on): 4 → `0x005aad40`; 5 → `0x005aadd0`; 6 or 8 → `0x005aae40`; each writes all six words and returns:

   | `+0xccd` | `+0x2070` | `+0x2074` | `+0x2078` | `+0x2084` |
   |---|---|---|---|---|
   | 4 | 3 | 1 | `0x4000` \| `0x200000`, plus `0x400` when the activity test holds | `0x10`, or `0x1000` when `+0x1d9c` bit `0x01` |
   | 5 | 3 | 1 | 0, or `0x400` when the activity test holds | 0 |
   | 6, 8 | 3 | 1 | 1, replaced by `0x400` when the activity test holds | `0x1010`, plus `0x400` when `0x00695640()` |

   "Activity test" = `0x00614d00()` (current activity type) is 2 or `0x014f71e8` bit 0 (`0x005aacc0`/`0x005aace0`). `+0x207c`/`+0x2080` are 0.
2. Otherwise **start from {3, 0, 0, 0, 0, `0x800`}** and return with that if `0x00943900`, `0x00942040` or `0x00942220` holds for the player. If `0x00973810` holds: `+0x2074` = 1, `+0x2084` = 1, return.
3. Local predicates: X = `0x0095d720(player)` (return now if X and `+0x13c4` bit `0x04`); if `0x0097cdd0(player)` and not `+0x1c98` bit `0x2000`: `+0x2078` = `0x4000`, return. Z = `0x009634f0(player)`. W = the equipped weapon's info record (`0x00941f40` → `+0x19c`), or 0. **R = `0x009ba920(player)` or `+0x1d99` bit `0x10`** (the movement-disabled bit of `player_movement_disable`, scoping §1.5).
4. **If sprinting** (`+0xccc` = 3): `+0x2074` = 1, then `0x005aad10` ORs 2 if byte `0x0130af61` and 4 if byte `0x0130af60` (both 1 in the image); `+0x2078` |= `0x4000`, plus `0x200` unless R, plus `0x40` unless X; `+0x2084` |= `0x8024`, plus `0x140` unless X; if W, not `0x00944de0`, and the mission type `0x006a3910()` is `0xa`, call `0x006a3990()`; then `+0x2084` |= `0x200000` if `0x009ba300` holds, else `+0x2078` |= `0x400000` and `+0x2084` |= `0x80`. Go to step 6.
5. **Not sprinting:**
   - if `0x009a1dc0(player)`: `+0x2078` |= 1 and go to step 6;
   - `+0x2074` = 1 when `+0x1d95` bit `0x10` (scope view) is clear, else 0; then `0x005aad10` as above;
   - `+0x2084` |= `0x200000` if `0x009ba300`, else `0x80`;
   - unless Z or fine aim (`0x009da830`, `+0x1d98` bit `0x80`): `+0x2078` |= `0x200` unless R, and `0x4000` unless scoped;
   - `+0x2078` |= `0x10000` if any entry of the list at `0x014a2ce8` (count `0x014a2cd8`) has bit 0 of `+0x6c` set (through `0x006039f0`);
   - `+0x2078` |= 2 unless seated (`0x009b9160`) or Z;
   - the weapon used below is W, or — when `0x007e2b10` holds and `0x007e2b50` names a slot other than 0 and `0xff` that the inventory at `+0x1b78` resolves — that slot's info record;
   - `+0x2078` |= `0x8000` when `0x00956390` is false, `+0xccc` = 0, not X, current AI mode `+0x501` ≠ 24 (`Panic`, `spec-ai-runtime.md`) and animation states `0x1d95`/`0x1d50` are both inactive (`0x004b4ce0` on the controller `[+0xd24]+0x10`);
   - **with a weapon** (and not `0x00944de0`): `+0x2078` |= `0x10`; then by weapon class — `0x00b81440`: |= `0xc`, plus `0x100000` when not scoped and `grenade_disable` (`0x0140e6e7`, §2.5) is 0; `0x00b88610`: |= `0x140` unless Z or `0x009bbcb0`, plus `0x100000` when `grenade_disable` is 0 (no scope test on this arm); `0x00b83a00`: |= `0x40`, plus the grenade bit on the same unscoped test; `0x00b81600`: |= `0x100` unless `0x009bbcb0`; `0x00b817b0`: |= `0x80`. (Some arms also call `0x006a3990` under mission type `0xa`, as in step 4.)
   - **without a weapon:** |= `0x40` unless X, and the grenade bit when `grenade_disable` is 0;
   - then `+0x2078` |= 1; `+0x2084` |= `0xce23d`, plus `0x140` unless X, plus `0x10000` when `+0x1e18` > 0 and not scoped.
6. **Common tail:** `+0x2078` |= `0x200000`. If the activity test holds: clear `+0x2078` bit `0x400`, then set it again unless the timer `+0x2110` is running or `0x00985d80(player, 7, 9)` holds. `+0x2084` |= 2 unless `0x00942220`. **`+0x2084` bit `0x8000` is cleared when `+0x28ac` bit `0x10` (`cant_enter_exit_vehicles`, scoping §1.5) is set.** Finally `+0x2078` |= `0x2000` unless `0x009ba970`, R, X or Z.

#### 3.2.3 `+0x2078` bits and their readers

Bit meanings are CONFIRMED where the reader gates a named action query; otherwise "reader" gives the location only.

| Bit | Gates | Reader |
|---|---|---|
| `0x1` | attacks (late on-foot dispatch `0x005ae3f0`, §2.7) | `0x005ae441` |
| `0x4` | aim / shoulder camera / scope (`0x005ad9a0`, §3.6.5) | `0x005ada3c` |
| `0x10`, `0x40` | melee (`CBA_OFC_MELEE_NORMAL_ATTACK`/`_POWER_ATTACK` need `0x40`; the block runs if either is set) | `0x005abd70` |
| `0x80`, `0x100`, `0x100000` | weapon-class and grenade arms of the late attack handler | `0x005ae75b`, `0x005aeb1a`, `0x005aea03` |
| `0x200` | jump/climb (`CBA_OFC_JUMP_CLIMB`, §3.6.4) | `0x005b2b4b` |
| `0x400` | `CBA_OFC_PRATFALL` | `0x005b2cff` |
| `0x1000` | `CBA_OFC_MULTI_PRATFALL` (not written by row 0) | `0x005b2d45` |
| `0x2000` | crouch (§3.6.3) | `0x005b2db7` |
| `0x4000` | sprint and the walk toggle (§3.6.2) | `0x005ab88d`, `0x005abb38` |
| `0x40000` | `CBA_OFC_HS_EXECUTE` (human shield) | `0x005b2cb5` |
| `0x800000`, `0x1000000` | `CBA_SKYDIVE_DIVE`, `CBA_SKYDIVE_FLIP` | `0x005b3055`, `0x005b301f` |
| `0x2000000` | `CBA_AVATAR_*` (four actions) | `0x005b313c` |
| `0x10000` | a nearby-object interaction (`0x006040a0`) inside the prompt routine | `0x005b6e84` |
| `0x2`, `0x8`, `0x8000`, `0x200000`, `0x400000` | — | not attributed: whole-word readers at `0x005ae777`, `0x005aead5`, `0x005b6eac`, `0x005b7018` were not resolved bit by bit (§14, OPEN 14) |

#### 3.2.4 What the pre-hook does around the classify (CONFIRMED by listing of `0x005b3d30`)

- **Actions disabled** (`+0x1d99` bit `0x20`): the classify is skipped and the words become {1, 1, 0, 0, 0, `0x2000`} — prompts only, with only prompt entry 13.
- After the classify: `0x005b3c50` clears bits by activity type (`0x00614d00()` through a 7-way jump table at `0x005b3d0c`: e.g. `+0x2078` &= `0xffdffe83` with the grenade bit restored, or `+0x2084` &= `0xffff7fff` when `0x00ab5070(vehicle, 2)` on the player's vehicle `+0x16c0` returns the player); `0x005ab330` clears `+0x2078` bit `0x200` when `0x006c49e0` holds, and on mission type `0xa` clears `+0x2078` bits `0x200179` (grenade bit restored as above) and `+0x2084` bits `0x8080`, on mission type `0xe` only `+0x2084` bit `0x8000`; and when `0x0060b530(player)` holds the words are forced to {1, 0, 0, 0, 0, 0}.
- Then, when `+0x2084` has any of bits `0x0f` and `0x007162d0(0x7a)` is false, HUD hint `0x7a` is raised (`0x007169e0`) if a vehicle is found within 4.0 of the player (`0x00ac4990`, `0x004dcf00`) that passes three class/state tests (HIGH for "hint").

### 3.3 The action dispatch `0x005b73c0` (31 instructions) and its targets

**CONFIRMED** by full listing. `edi` on entry is `0x0140e6f8`, the "controls ran this frame" flag (§2.7; loaded into `edi` by `0x005b7430` at `0x005b76b8`).

1. `0x005ad830` — the **D-pad latch** `0x0140e684`, cleared and rebuilt each frame: bit `0x01` = consume-pressed `CBA_GAC_RECRUIT_DISMISS` (when `+0x2070` bit 1 and `+0x2074` bit 4); bit `0x08` = `CBA_MENU_DP_DOWN` — consume-pressed when no activity (`0x00614d00()` = −1), `0x006cf0a0()` = −1, `0x007044f0()` ≠ 1 and not `0x006c4420()`, held otherwise — when `+0x2070` has bit 1 or 8 — unless `0x007afba0()` and `CBA_GAC_TAKE_SCREENSHOT` is held, which calls `0x007b04f0()` instead; bits `0x02`/`0x04` = consume-pressed `CBA_OFC_AUDIO_PLAYER_PREV_TRACK`/`_NEXT_TRACK` on foot (`+0x2070` bit 2), or `CBA_VDC_PREV_RADIO`/`_NEXT_RADIO` in a vehicle (bit 4, `+0x207c` bit `0x100`, a vehicle). **If one of `0x02`/`0x04` and one of `0x01`/`0x08` are both set, all four are cleared** (chord rejection).
2. Route on `+0x2070`: bit 2 **and no vehicle handle** (`+0x16d8`/`+0x16dc` both 0) → **`0x005b2ca0`** (§3.6); else bit 4 → `0x005b5c50` (in-vehicle actions, U7 — the hand-off exists and is unconditional on the bit); else bit 8 → `0x005ac1f0`; else → **`0x009fef60`**.
3. If `+0x2070` bit 1: `0x005b6af0(&0x0140e6f8)`. Always: `0x0060d960(&0x0140e6f8)`.

Under row 0 (`+0x2070` = 3) step 2 therefore reaches **only `0x005b2ca0` or `0x009fef60`**.

- **`0x005ac1f0`** (33 instructions, satellite): when `CBA_SWC_EXIT_SATELLITE_MODE` is pressed from keyboard/mouse (out code 2), or `CBA_MENU_ABORT` is pressed from any device, and `0x00b6fc30` on the satellite object `0x0130eee8` holds, `0x00b709d0(object, player)` exits satellite mode. The pad path for the same action is in the secondary hook (§2.7). Row 13's classify `0x005aaf00` reads the same object (`0x00b714c0`) to choose `+0x2080`. Together with §2.7's action names this makes bit 8 the satellite context: **CONFIRMED** for the routing; the name remains HIGH.
- **`0x009fef60`** (186 instructions) is **the sprint system's "not sprinting this frame" step**, not a generic reset: it always clears `+0x1d94` bit `0x10`, advances the stamina state machine `0x0268d2ac` (states 0–4; timers `0x0130b894`/`0x0130b898`/`0x0130b89c`; amount `0x0268d27c`), sets `+0x1d95` bit `0x02` while the amount is at or below the threshold `0x0268d288`, clears it when recharge completes (state 0), clears the sprint-toggle latch `+0x1d96` bit `0x04` when recharge starts, and updates the HUD meter (`0x007f5f40`, `0x007f6040`, `0x007f6090`). The threshold is stored at `0x00a000d4` by the `SprintEntry` loader next to `PantPercentage` (strings pushed at `0x00a00049`…`0x00a000df`): HIGH that it is the pant level. **[corrects scoping §1.3's "shared reset of sprint and stance state".]**
- **`0x005b6af0`** (603, read for its input surface): the party-dismiss hold/press prompt (`CBA_GAC_RECRUIT_DISMISS` held, HUD ids `HUD_HOLD_DPADUP_PARTY_DISMISS`, `HUD_PRESS_DPADUP_PARTY_DISMISS`, gated by byte `0x012e6cef`), then the generic action prompt: `CBA_GAC_ACTION` pressed/held and `CBA_OFC_PICKUP_RELOAD`, with the release-through-threshold query `0x005bde80`, the prompt-array walkers `0x005b6870`/`0x005ac340`/`0x005ac390`/`0x005b68e0`/`0x005b6950`; after a prompt fires it consumes `CBA_GAC_ACTION`, `CBA_OFC_PICKUP_RELOAD` and `CBA_OFC_DROP_RIOT_SHIELD` and clears the flag through its argument (HIGH: the pointer's stack slot was tracked through one reuse).
- **`0x0060d960`** (325, read for structure): when the local player's QTE record (`0x014b01c0` + `0x70`·index) is active it **clears `0x0140e6f8`** (first store, `0x0060d997`) and tests the record's own CBA through `0x005bae10`. Consequence (CONFIRMED with §2.7's tail): **while a QTE is active the movement post-hook and the secondary hook do not run.**

### 3.4 The look slot `0x005ab6a0` (64 instructions) — and U0 OPEN 2

**CONFIRMED** by full listing:

1. Return at once if `+0xa20` is set (dormant pivot mode, §3.5.6), controls are disabled (`+0x1d99` bit `0x08`), or `0x013c88a0` bit `0x02` is set.
2. rot = `0x005bb530(CAA_CAMERA_ROTATE)`, elev = `0x005bb530(CAA_CAMERA_ELEVATE)`.
3. Only when the camera mode `0x013c87c8` is 0 (follow camera), or 2 with `0x005780d0()` = 3: apply the drunk-camera modifier `0x00975ce0(&rot, &elev)` (it calls `0x009786c0`, then while byte `0x02625871` is set adds a ramped drift term clamped to [−1, 1]; read, role HIGH), then `0x0056b100(−rot)` and `0x0056b120(elev)`.
4. If either value is non-zero: `0x00950b70(player, 0.3)` (below).

The two setters: **`0x0056b100(x)` stores x into `0x013c8990` if `0x005be940()` returns 1, else −x; `0x0056b120(y)` stores −y into `0x013c8994` if `0x005be920()` returns 1, else y.** So the yaw input is +rot normally and −rot with the first option; the pitch input is +elev normally and −elev with the second. `0x005be940`/`0x005be920` choose bytes `0x01411b15`/`0x01411b14` while the camera is mouse-driven (`+0x9f6`) and bits 1/2 of `0x01411b00` otherwise (§1.12.2). **This confirms U0's HYPOTHESIS as behaviour: those options are horizontal and vertical look inversion, one pair per device family** (the options-menu labels are still unread — §12, OPEN 2).

`0x00950b70(player, blend)` — **"player gave input"** (CONFIRMED by listing; role HIGH): when `0x008addb0(player, 0)` holds it restarts the idle timer `+0x19b4` to a random 20–60 s (`0x00d9e180(20000, 60000)`) and, when `0x009561f0` holds, ends controller state `0x1d4d` with the given blend (`0x004b41b0(controller, state, blend, flag)`, which also recurses into child controllers — "ends" is HYPOTHESIS for that helper throughout this section); then, when the human passes `0x00853b30(…, 0x21)` ("is a player", `spec-anim-runtime.md` §4.2) and its AI mode `+0x501` is 0 (`Idle`), it begins AI mode 43 `Player` (`0x004f13a0`) — the input-driven exit from `Idle` that scoping §1.3 hypothesised for the `Idle` tick.

### 3.5 The movement chain

#### 3.5.1 The move slot `0x005b3e80` (207 instructions) — CONFIRMED by full listing

1. If `+0xa20` (`0x0098d6c0`) is set: run `0x005ad510` and return (§3.5.6).
2. `0x005ad130(&A, &B, &centred, &stopped, &moving, 0)` with A = B = 0 initially (§3.5.2).
3. **Camera basis.** F = the 16-byte row at `0x013c87b0` with lane 1 (y) zeroed, normalised by `0x004d6210`; Rt = the row at `0x013c8790`, treated the same way. `0x004d6210` normalises x, y, z with w forced to 0, and returns (1, 0, 0, 0) when every component is within 1e‑5 of 0. (State-5 override: when the control state is 5 and `0x00578100()` holds, F and Rt come from two other basis vectors via `0x00d9ff50` on `0x013213f0`/`0x013213d8`; unreachable in shipped play, §1.4 step 5.)
4. **Gate:** nothing below runs — no target, no writer call, so `+0x2048`… keep last frame's values — when the player is entering a vehicle (`0x009b91e0`), its AI mode `+0x501` is 0 (`Idle`), or its AI sub-state `+0x500` is `0x8b` (`exiting node`). Names from `spec-ai-runtime.md` §2.5 and the sub-state table `0x012e1168`.
5. `0x005b31f0(player, B, A, &F, &Rt, 1, &0x02289b24)` writes the **target point** to `0x02289b24`/`0x02289b28`/`0x02289b2c` (§3.5.3).
6. If B or A is non-zero: `0x00950b70(player, 0.3)` (§3.4).
7. **Speed scalar s:**
   - 1.0 when `+0x1d94` bit `0x10` (sprint) is set or the player is diving (`0x009db960`: `+0xcce` = `0x26`, the `Jump Dive Bool` value of `spec-anim-runtime.md` §4.2 row 1);
   - else **0.3** (`0x012e6d08`) when the gamepad is **not** the active input (`0x005bc5d0`, §1.5) and `+0x1d94` bit `0x20` (walk) is set;
   - else **s = A² + B²**, computed in double precision from the published copies `0x0140e6c0` (A) and `0x0140e6bc` (B).
8. `0x005aee30(player, target.x, target.y, target.z, s)`.
9. `0x00985df0(player, +0x1d94 bit 0x10, f, 1)`, where f = `stopped` and not sprint-requested and `+0xa60` = 0 exactly (just written by the writer) — the locomotion-state update that maintains `+0xccc` (3 = sprinting) and the sprint ramp timer `+0x2058`; read at its head only (U3).

#### 3.5.2 Axis read and conditioning `0x005ad130` (268 instructions) — CONFIRMED by full listing

Signature (cdecl): `(float* A, float* B, bool* centred, bool* stopped, bool* moving, bool add_camera_rotate)`. On entry `stopped` = `centred` = 0 and `moving` = 1.

1. **Hard block** `0x005ad050` — true for no local player, `+0x33` bit `0x08` clear, `0x009570b0` = `0x346`, controls or movement disabled (`+0x1d99` bits `0x08`/`0x10`), entering a vehicle, dead (`0x0096f4f0`), script-controlled (`+0x28ac` bit `0x02`), `+0xe8` bit `0x100000`, mission type `0x1a`, in cover (`0x009aa260(player, 0)`) unless `+0x18d4` = 1 and `0x009a9550` is false, controller state `0x285`, `0x009a1dc0`, or AI sub-state `0x8c` (`turnaround`). When true: A = B = 0, `stopped` = `centred` = 1, latch `0x0140e6dd` = 1, and go to step 8.
2. **B = `0x005bb530(CAA_WALK_FORWARD_BACKWARD)`, A = `0x005bb530(CAA_WALK_TURN_LEFT_RIGHT)`** (push survey; U0 §1.9: pad stick wins, keys ±1, clamped to [−1, 1], no game-side dead zone).
3. Only if `add_camera_rotate`: A ← clamp(A + `0x005bb530(CAA_CAMERA_ROTATE)`, −1, 1). **Only the dormant `0x005ad510` passes 1** (§3.5.6).
4. **Modifiers, in order** (each takes &A, &B):
   - `0x009758b0` — the **drunk/weed effect** (its band loads `drunk_levels.xtbl` with fields `Control_delay_on_foot`, `Random_input_amount_foot`, `Random_input_switch_time`, …; HIGH for the field-to-global mapping, OPEN 17). It (a) refreshes a random sign at `0x0262585c` on timer `0x0130a0f8` (`0x009750e0`); (b) passes (A, B) through a **90-entry time-stamped delay line** (`0x005aa900` on `0x02625c68`, mode 0: returns the sample from the configured delay ago, linearly interpolated; a no-op while the delay is 0); (c) when |A| < 0.25 and |B| > 0.25 and the amount `0x02625bd0` > 0, sets A = sign · amount (a sideways stagger); (d) on keyboard/mouse scales A by 0.7 and B by 0.8 when the delay line changed them; (e) multiplies both by min(`0x009785e0()`, 1), which is 1.0 normally, `[0x0130a12c]` (1.0 in the image) in effect state 2 and **0 in effect state 3** (`0x02626520`).
   - `0x005e5a90` — **negates both axes when `0x012ec730` = 0**. That global is the current mission-16 effect index (−1 none; reset by `0x005e5e40`; chosen at `0x005e6623`/`0x005e662b`); effect 0 is the one stopped by the events `m16_invert_stop`/`m16_boss_invert_stop`. Its image value is 0, so correctness relies on the reset running before play (HIGH).
   - `0x006e62e0` — negates both axes when byte `0x014f3d34` is set, the activity type is 8 and the activity object answers `0x006e5430(7)`.
5. **Cover: dominant axis only.** When `0x009aa260(player, 0)` holds, `+0x1d94` bit `0x10` is clear and `+0x18d4` = 1: if |B| > |A| then A = 0, else B = 0 (a tie keeps A). Reading `0x009aa260` as "in cover" is HYPOTHESIS throughout this section, from its use in §3.6.4 (jump leaves it through a vault) and §3.5.4 step 12.
6. **Release detector.** With dt = `0x0132a0b4` (a frame-time global, 1/30 in the image) and the previous axes `0x0140e6a8` (A) / `0x0140e6a4` (B): rates a′ = (A − A_prev)/dt, b′ = (B − B_prev)/dt; n = (A, B) and m = (a′, b′) each normalised by `0x00db92d0` (a zero vector becomes (1, 0)); k = n·m; `0x0140e68c` = k·a′, `0x0140e688` = k·b′, **`0x0140e694` = (k·a′)² + (k·b′)²**.
   - `centred` = |A| < 1e‑4 **and** |B| < 1e‑4 (`0x00dad830(v, 0, 1e‑4)` is "|v| < 1e‑4");
   - `stopped` = `centred` if the latch `0x0140e6dd` was already set, else `centred` **and** `0x0140e694` < 1.0; the latch ← `stopped`.
   So a stick that **snaps** back to centre counts as stopped one frame later than one eased back.
7. Publish: `0x0140e6c0` = A, `0x0140e6bc` = B (read by `0x005b3e80`, row 16's `0x005b4190`, `0x0098bb76`, `0x0099b244` and the driving reader `0x00a91800`), and the previous-value pair `0x0140e6a8`/`0x0140e6a4`.
8. If neither `centred` nor `stopped`: when `+0xe8` bit `0x80000` is set, clear it and call `0x0094b070`, `0x0095acb0(player, 1, 0.2, 0, 0)` and `0x004dd900` (HYPOTHESIS: interrupt an idle action). Otherwise `moving` = 0. If `centred`: A = B = 0 exactly. Finally `0x0140e69c` = A, `0x0140e698` = B (no reader by absolute address — the "four control floats" of §2.5 are this pair plus the previous-value pair) and byte `0x0140e6e2` = 0.

`moving` and `centred` are not read by `0x005b3e80`.

#### 3.5.3 Target point `0x005b31f0` (384 instructions) — CONFIRMED by full listing

Arguments: (player, B, A, &F, &Rt, flag, &out). Overrides first:

- **Move-to-object:** when the handle `+0x1a28`/`+0x1a2c` is non-zero and resolves (`0x00458230` on `0x024433a8`) to a live object whose class flags pass (`+0x33` bit `0x10` clear, class-table byte `+6` bit `0x08`, not `0x00853b10`), out = that object's position `+0x40` (or, for a class with byte `+0xa` bit `0x01` and `+0xe8` & `0x1200`, a transformed point from its `+0xcc0` attachment). A handle that fails to resolve is released (`0x00943d00(player, null handle, 0)`) and the normal path runs.
- **Forced point:** when that handle is 0 and the vector `+0x1a30` is non-zero (by the 1e‑7 zero test `0x00d9f9d0`), out = `+0x1a30`.

**Normal path:**
1. **v = B·F + A·Rt** (component-wise, double precision per product); v = 0 when A = B = 0.
2. u = v normalised (`0x00da03c0`), or the zero vector `0x029cdb98` when v is within 1e‑7 of 0.
3. Only when `flag` is 0 — never from the move slot, which passes 1; the other caller `0x008a3ca0` (§3.5.7) passes a computed value — an auto-steer may replace u with the normalised vector from the position to `+0xcb0`: it needs one of five predicates (`0x00987ae0`, fine aim `0x009da830`, `0x009ba7e0`, `0x009baa60`, `0x009baaa0`), none of three vehicle predicates, no camera flag (`0x013c88a0` bit `0x02` with `0x0091faf0`), and `0x009bbcb0`.
4. If u is zero and (sprint bit `+0x1d94` bit `0x10`, or diving): u = the facing axis `+0x64`/`+0x68`/`+0x6c`.
5. **out = position + 5.0 · u** (the 5.0 is the double at `0x012a2f20`).

#### 3.5.4 The intent writer `0x005aee30` (1307 instructions) — CONFIRMED by full listing

Signature (cdecl): `(player, x, y, z, s)`. Constants are read from `.rdata` (double unless marked). Steps in order:

1. **d = (x, y, z) − position `+0x40`.**
2. **Burst:** if `+0x1d94` bit `0x40` is set and d is zero (1e‑7 test): d = facing `+0x64`, clear the bit, s = 1.0.
3. **Animation lock:** if the controller flag `0x10` is set (`0x004b4fc0([+0xd24]+0x10, 0x10)`): when s > 0.01 (float), set `+0x1d94` bit `0x80`; then d = 0, s = 0, `+0xa68` = 0.
4. Timers: controller flag 4 → `+0x2068` set already expired. **While s > 0.8, timer `+0x206c` alternates each frame between "started now" and "unset"** (`0x005aef9f`…`0x005aefb3`; §14, OPEN 12). `+0xd1c` (current speed, `spec-anim-runtime.md` §4.2 row 20) > 0.7 (float) → `+0x2068` = now + 500 ms.
5. **Stance maximum speed** `0x009dca30(player, &M, &M₀)` (CONFIRMED by listing). Let *sp* = `+0x1d94` bit `0x10`, forced false while animation state `0x305` or `0x1d8f` is active. First match wins:

   | Condition | M (tweak variable, registered with its name at `0x01007650`…`0x01007790`) |
   |---|---|
   | heavy weapon (`0x009ba860`) | *sp* ? `Player_max_heavy_weapon_sprint_speed` (`0x0262e9c0`) : `Player_max_heavy_weapon_speed` (`0x0262e9c4`) |
   | avatar (`0x005e6ce0`) | `Player_max_avatar_move_speed` (`0x0262e9c8`) |
   | swimming (`0x009b5690`) | `Player_max_swimming_speed` (`0x0262e9cc`) |
   | *sp* | `+0x1c9c` bit `0x80000` ? boosted sprint `0x0263ae34` : `Player_max_sprint_speed` (`0x0262e9e8`) |
   | fine aim (`+0x1d98` bit `0x80`) | `Player_max_fine_aim_speed` (`0x0262e9e4`) |
   | controller mode `+0x44` = `0xb` | `Player_max_riot_move_speed` (`0x0262e9dc`) |
   | skydiving (`0x009adb00`) | `Player_skydive_move_speed` (`0x0262e9d4`) |
   | rappelling (`0x009a5d60`) | `Player_rappel_move_speed` (`0x0262e9d8`) |
   | crouched (`0x00985cd0`) | `Player_crouch_move_speed` (`0x0262e9d0`) |
   | otherwise | **`Player_max_run_speed`** (`0x0262e9e0`) |

   Then ×0.8 (`0x0130b0b0`) when `+0xd40` has bit `0x2000` (`backpedal bool`, `spec-anim-runtime.md` §4.2 row 12), or bits `0x20` and `0x1000` together. M₀ = that value. **Sprint ramp:** while the timer `+0x2058` is set with r > 0 ms remaining (`0x00d9e210`), M = (1 − r/300)·M₀ + (r/300)·S (300.0 is the double at `0x01129698`), with S = `Player_max_sprint_speed` (30.0 when `+0x1c9c` bit `0x80000`). The boosted sprint `0x0263ae34` is set to 30.0 and decays by the frame time each frame until it reaches `Player_max_sprint_speed` (per-player update, `0x009ea514`…`0x009ea58c`; HIGH).
6. **Momentum states:** when controller state `0x1d80` or `0x1d8f` is active, a local "momentum" flag is set; then `+0x1d95` bit `0x01` is set if s > 0.2, d = velocity `+0xd04` if s ≤ 1e‑4 or d is zero, s = max(s, `+0xd1c`/M₀), and s = 1.0 when `0x1d80` is active.
7. **Scale:** if s > 1e‑4 and d is non-zero: u = normalise(d); **d = u · clamp(s, 0, 1) · M**. Otherwise u = 0 and **d is left as it is**.
8. **Store `+0x2048`/`+0x204c`/`+0x2050` = d.**
9. Reversal check: p = the previous `+0x203c` vector. If not sprinting (or controller flag `0x800`), and normalise(`+0x1cec`) · normalise(d) < 0 while `+0x1cec` is non-zero: p = 0 (no smoothing through a reversal).
10. **`+0x1d97` bit `0x08` ("stopping")** is cleared, then set again when d is zero and (p is non-zero, or the bit was set last frame and `+0xe0` bit `0x1000` is set).
11. Distances: δ = |d − p|; e = |`+0xd04` − `+0x1a68`| (velocity relative to the support's velocity, HIGH).
12. **Locomotion mode** `0x005ab400`: −1 when any of about twenty blocking states holds (controller mode `0x14` or 5, `+0xe8` bit `0x20`, `0x00942220`, `0x0097cdd0`, ladders/vaults/skydive predicates, states `0x1d50`/`0x1d53`/`0x1da1`/`0x1d54`, in a vehicle, swimming, `0x009b9180`, controller mode `0x14` by `0x004b2190`); 1 in cover; else 0 (or −1 when combat-ready and `0x009ba7e0`).
13. **Animation requests** (ids are animation actions/states, names OPEN 11; "end" is `0x004b41b0`, §3.4):
    - mode 0, state `0x1d8d` inactive, e > 3.5 and stopping: end `0x305`; if e > 5.1 play `0x326` (weapon-sprint style, `+0x1d97` bit `0x20`) or `0x325`, else `0x31a` (when `+0xab1` = 1) or `0x31b` — the **stop** animations (`0x00959fe0`);
    - not momentum, e > 2.0, u · normalise(`+0x1cec`) < −0.2 (a reversal), mode 0: end `0x1d8d` (blend 0.2) and play a **turn-around** (`0x2ff`/`0x300` normally; `0x301`…`0x304` when `+0xccc` = 3, chosen by `+0x1d97` bit `0x20` and `+0xab1`), aligning it with `0x004b7850`; when `+0xccc` = 3 with controller flag `0x800` only the alignment is applied;
    - otherwise, when `+0x2068` has expired, mode 0, e < 1.7, s > 0.6, no controller flag `0x800`, `+0x206c` elapsed < 1000 ms, and (`+0xec` bit `0x800` clear or the heading test `0x00434f10` on `+0x1a78` > −0.6): end `0x1d8d` (blend 0.3), **start-to-move** `0x009ddf60(player, &d)`, clear `+0x1d95` bit `0x01`;
    - otherwise mode 1 while sprinting-state (`+0xccc` = 3) and not sprint-requested: `0x009ddb40(player, &d)`.
14. **Acceleration rate** a = `Player_acceleration` (`0x0140e64c`, registered at `0x00fefeb0`):
    - when `0x009573d0` holds, a ← a · (0.25 + 0.75·c), where c = max(0, normalise(d) · v) and v is `+0xa70` rotated about the vertical axis by `+0xa80` (`0x00da09e0`);
    - a ← 0.8·a when `+0xd40` bit `0x2000` (backpedal);
    - a ← 0.5·a when not supported by ground (`0x0096e670`), else 2·a when `+0xe8` bit `0x20`;
    - **diving replaces a by `Player_dive_roll_acceleration` (`0x0140e648`).**
15. **Approach:** step = a · (1 + 0.5·δ) · dt, dt = `0x0132a0ac` (1/30 in the image). In x and z separately, r moves from p toward d by step · |dᵢ − pᵢ| / δ without overshooting (no movement when δ = 0); **r.y = d.y directly.**
16. **Sprint style** on `+0x1d97` with the 5 s timer `+0x205c`:
    - while sprint-requested: if `+0x205c` has expired, bit `0x10` (with the sprint-start animation `0x306` via `0x0095be60` in mode 0, unless already set or state `0x305` is active) and bit `0x20` cleared — except that a weapon whose id matches the controller's current weapon id flips to bit `0x20` and restarts the timer; if it has not expired, bit `0x20` and the timer restarts (5000 ms);
    - while not sprint-requested: bit `0x10` is cleared (playing `0x305` in mode 0 unless `+0xccd` = 5 or state `0x306`/`0x325` is active), and the timer restarts while `+0xccc` = 3.
17. If momentum: r = d (no smoothing).
18. **Store `+0x203c`/`+0x2040`/`+0x2044` = r**; `0x00987360(player, &(position + r))`; **`+0xa60` = min(|r|, M)**; if r is non-zero, rescale `+0x203c` to length `+0xa60`.
19. **Facing `+0xa50`/`+0xa54`/`+0xa58`:** keep q = old `+0xa50` rotated by the old `+0xa68`; set `+0xa50` = `+0xcb0` − position (`+0xcb0`: the aim point, HIGH **[corrected by §5.4.4: `+0xcb0` is the move target that step 18's `0x00987360` has just stored, so `+0xa50` = unit(r) whenever r ≠ 0]**); if non-zero, normalise it (`0x00446f60`); else when in fine aim it becomes the facing axis (local player and camera mode 1), the camera forward row `0x013c87b0` (local player otherwise) or `+0x1f98` (others); when stopping, zero and combat-ready (`0x00943a20`) it becomes q; when `+0xa6c` ≠ −1 and `+0xa7c` = 1 it becomes `+0xa70` scaled by `+0xa80` (`0x004d62c0`, HIGH).
20. **Turn angle `+0xa68`:** when `0x009574d0` holds, take the orientation from `0x009df8c0(player, 0, &m)`; if `+0xa50` is zero, set it to m's third row flattened and normalised and stop; otherwise **`+0xa68` = heading(m) − heading(`+0xa50`)** (`0x004bc550`, `0x004cd810`; HIGH for "heading").

#### 3.5.5 The move-vector math in closed form

For row 0, a player who passes the gate of §3.5.1 step 4, not in the momentum states of §3.5.4 step 6 and not locked by the animation:

- **Axes:** A = `CAA_WALK_TURN_LEFT_RIGHT`, B = `CAA_WALK_FORWARD_BACKWARD`, each in [−1, 1] after the engine's per-axis dead zone and rescale (§1.3.3); keys give exactly ±1 (two opposing keys cancel). The modifiers of §3.5.2 step 4 and the cover rule of step 5 apply first.
- **Basis:** F = normalise(R₂.x, 0, R₂.z), Rt = normalise(R₀.x, 0, R₀.z), with R₀ = `0x013c8790` and R₂ = `0x013c87b0`, the camera orientation written by the camera update that ran earlier in the same world update (§1.0 of the scoping note). So +B moves along the camera's flattened row 2 and +A along its flattened row 0 (that these are "forward" and "right" is the conventional reading, HIGH; the two are orthogonal when the camera has no roll). The vertical component of the intent is always 0.
- **Direction:** u = normalise(B·F + A·Rt).
- **Magnitude:** s = A² + B² (gamepad, or keyboard without walk); s = 0.3 with keyboard walk; s = 1 when sprint-requested or diving.
- **Desired move (`+0x2048`):** **D = u · min(s, 1) · M.** On a pad the speed is therefore **quadratic in deflection** (half-tilt gives a quarter of M), and a full diagonal (s = 2 with the engine's square, per-axis gate) clamps to M. Keyboard gives M for any direction, 0.3·M walking.
- **Animation-facing velocity (`+0x203c`):** r ← approach(r, D, a·(1 + 0.5·|D − r|)·dt) in x/z, zeroed first on a reversal against `+0x1cec` when not sprinting; then |r| ≤ M, and **`+0xa60` = |r|** (the "desired speed" the AI-facing human fields carry) **[§5.5: then multiplied by `+0xa64` in `0x00985df0`; §5.4.4: the bridge drives the character controller from exactly this vector]**.

Edge case (CONFIRMED, port decision): when 1e‑4 < |A|, |B| but s = A² + B² ≤ 1e‑4 — a stick barely past the engine dead zone — step 7 of §3.5.4 does not rescale, so `+0x2048` is the raw 5.0-unit offset to the target point; `+0x203c`/`+0xa60` are still bounded by M through steps 15 and 18.

#### 3.5.6 The dormant pivot branch `0x005ad510` (114 instructions)

Read in full; recorded at structure level because it cannot run. When `+0xa20` is set: `0x005ad130(…, add_camera_rotate = 1)`; target = position + normalise(position − pivot `+0xa24`) (`0x0098d7c0`), stored to `0x02289b24`; `0x00985df0(player, sprint bit, stopped, 1)`; `0x005735b0` on the same unit vector; `0x00987410(player, &target, 0, 0, 0)`; and the camera elevation input is forwarded here (`0x0056b120(CAA_CAMERA_ELEVATE)`) because the look slot returns early. **It never calls `0x005aee30`.** `+0xa20` is cleared by `0x0095442a` and `0x0096edab` and set only by `0x0098d6d0`, which has no call, jump or data reference in the image (raw scan): **HIGH CONFIDENCE the branch is dead in this build.**

#### 3.5.7 Other writers of the intent fields (linear-sweep displacement scan)

- `+0x2048`…`+0x2050`: `0x005aee30`; the player constructor (`0x009deb69`, zeroes with `+0x203c`…); and **`0x008a3ca0`** (`0x008a3fce`), which calls `0x005b31f0` with zero axes and flag 0 and then writes `+0x2048` = `+0xa50` · `+0xa60` (or zero under controller flag `0x10`) — HYPOTHESIS: the network-driven remote-player update (OPEN 18).
- `+0x203c`…`+0x2044`: also zeroed or reset by `0x005e6c0e`, `0x009846c5`, `0x009899c3`, `0x009955b2`, `0x009a0991`.
- `+0xa50`, `+0xa60`, `+0xa68` are **human** fields (offset < `+0x1d90`, scoping §1.5): dozens of writers across AI modes (the mode-43 tick at `0x004f1ba8`), vehicle and animation code.
- **[corrects scoping §1.3:]** "the only player-controls routine that writes `+0x2048` (and `+0x203c`, `+0xa60`/`+0xa68`)" holds for the controls band only.

### 3.6 The on-foot action handler `0x005b2ca0` (420 instructions) and its children

#### 3.6.1 Order (CONFIRMED by full listing)

1. `0x005ad9a0` — aim, shoulder camera and scope (§3.6.5).
2. Human-shield execute: `+0x2078` bit `0x40000` and consume-pressed `CBA_OFC_HS_EXECUTE`, not `+0x28ac` bit `0x800` → `0x009ad200(player, 0)`.
3. `0x005abd70` — melee (§3.6.6); its "attack started" result is kept.
4. Pratfalls: bit `0x400` + consume-pressed `CBA_OFC_PRATFALL`, or bit `0x1000` + not scoped + consume-pressed `CBA_OFC_MULTI_PRATFALL` → `0x00620470(player)`, then `0x005ab780` unless the timer `+0x2110` is running.
5. `0x005b29d0` — jump/climb (§3.6.4), clearing `0x0140e6f5` first.
6. Crouch (§3.6.3).
7. `0x005ab930(jumped)` — sprint/walk (§3.6.2).
8. Skydiving (`0x009adb00`): `CBA_SKYDIVE_FLIP` pressed under bit `0x1000000` → `0x009aee20(player, 2 or 0, …)` by `+0xcce` = `0x17`/`0x1b`; `CBA_SKYDIVE_DIVE` held under bit `0x800000` → `0x009aee20(player, 1, …)` unless `+0xcce` = `0x18`, released → `…(0, …)` when it is.
9. When `0x009a8450` holds: `CBA_OFC_ATTACK_PRIMARY` pressed → `0x009a8b20(player)`, provided controller states `0x4b4`/`0x4b5` are inactive and `0x0095d720` is false, or `0x004b4320(controller, [0x0153be14], 0x1d54, −1)` holds.
10. Avatar (bit `0x2000000`): `CBA_AVATAR_SWORD_SWEEP` → `0x00950c30`; then, unless `0x009634f0`/`0x00963510`, `CBA_AVATAR_SHOCKWAVE` → `0x005e7470`, `_FLAME_BREATH` → `0x005e6e80`, `_DASH` → `0x005e71b0` (first match).

#### 3.6.2 Sprint and walk: `0x005ab860` (61) and `0x005ab930` (317) — CONFIRMED by full listing

**Walk `0x005ab860`** clears `+0x1d94` bit `0x20` every frame, then — only with `+0x2078` bit `0x4000`, sprint not held, movement not disabled (`+0x1d99` bit `0x10`) and controller flag `0x10` clear (`0x004b4fe0`) — sets it when `CBA_OFC_WALK` is held (also clearing the walk latch `+0x1d96` bit `0x08` and the sprint latch `0x04`), or toggles the walk latch on `CBA_OFC_WALK_TOGGLE` pressed and sets bit `0x20` while the latch is set (clearing the sprint latch). When the gate fails the walk latch is cleared. **Walk only changes s on keyboard/mouse** (§3.5.1 step 7); on a pad the bit has no effect on the intent in the routines read here (a pad walks by small deflection, §3.5.5).

**Sprint `0x005ab930`:**
1. Walk first; clear `+0x1d96` bit `0x02`; remember and **clear `+0x1d94` bit `0x10`** (re-derived each frame). If the sprint system is switched off (byte `0x0268d2a0` read by `0x009fee70` is 0 — the global that the Lua function of `spec-lua-api-behaviour.md` §18.26 writes) → `0x009fef60` and return.
2. Read `CBA_OFC_SPRINT_TOGGLE` pressed, `CBA_OFC_SPRINT` held and pressed, `CBA_OFC_WALK` held.
3. **Keyboard idle test:** when the gamepad is not active and both walk axes are within 1e‑4 of 0, and the timer `0x012e6df4` is unset or expired: clear the sprint latch and forget all three sprint inputs. At the end of every keyboard frame in which the player is sprinting and the keys are not idle, that timer is set to 300 ms — so **keyboard sprint survives a release of the movement keys shorter than 0.3 s**.
4. Walk held cancels the latch; sprint held → want = true (and clears the latch); otherwise `CBA_OFC_SPRINT_TOGGLE` pressed under bit `0x4000` flips the latch, want = latch or diving. Movement disabled or controller flag `0x10` force want = false.
5. **When want and `+0x2078` bit `0x4000`:** clear walk; `+0x1d96`: clear `0x08`, set `0x02`; when `0x009562b0` holds call `0x0095acb0(player, 1, 0.2, 0, 0)`; clear `+0xe0` bit `0x10000000`; leave scope view, or stand up when crouched (`0x00988610`); leave fine aim (`0x009dcf10`). Then (when `0x009a1dc0` holds, step 6 applies instead): if `0x00985d80(player, 5 or 6, 0)` was true, `0x009ff270()` and restore the previous sprint bit. Otherwise the player must be able to move on the ground (`0x00941da0`: not swimming, climbing, falling, skydiving, rappelling, in or entering a vehicle, in cover, `+0xe8` & `0x81200` clear, AI mode ≠ 32) or be in cover, with `+0xe8` bit `0x100` clear (`0x0097cdd0`) — or be diving; failing that, `0x009fef60`. Then, unless already sprinting, it also needs `+0xcce` ≠ 3, stamina available (`0x009ff400()` ≥ 0), the cool-down timer `+0x2060` expired, and state `0x1d50` and controller flag `0x10` inactive — failing that, nothing more happens this frame (the sprint bit stays clear). If those hold: if current speed `+0xd1c` > 4.0, already sprinting, in cover, carrying a heavy weapon or `0x009ba300` → **`0x009ffd00()`** (the stamina "sprinting" step, which sets `+0x1d94` bit `0x10` while stamina remains and clears `+0x1d95` bit `0x02`), and on a fresh `CBA_OFC_SPRINT` press `0x009fffc0()` (applies the start penalty `0x0268d280`); **otherwise set `+0x1d94` bit `0x40`** — a burst from (near) standstill that the writer turns into one full-speed step along the facing (§3.5.4 step 2).
6. **Otherwise** `0x009fef60` (recharge), or `0x009ff270()` with the previous sprint bit restored under the same `0x00985d80` test.
7. When the jump handler fired and `0x0098b970`/`0x00989670` hold: `0x009fffa0()` (jump penalty `0x0268d284`).

#### 3.6.3 Crouch, and the dead-zone test

Inside `0x005b2ca0` (CONFIRMED by listing). Runs only with `+0x2078` bit `0x2000`, `CBA_OFC_SPRINT` not held and the sprint latch clear; otherwise a crouched player stands up (`0x00988610`).

1. Queries: pressed `CBA_OFC_CROUCH_TOGGLE` (out o₁), held `CBA_OFC_CROUCH` (out o₂), pressed `CBA_OFC_CROUCH`. *kb* = o₁ or o₂ is 2 (keyboard/mouse, §1.8.2); option bit 0 of `0x01411b00` (set by every binding application, §1.7.1).
2. **Hold mode** — crouch held and (*kb*, or option bit 0 clear): crouch now if possible (`0x00986810`, `0x00987730(player, 0)`; first-use hint `0x007169e0(0x52)`), clear the crouch latch `+0x1d96` bit `0x10`, consume `CBA_OFC_CROUCH`.
3. **Toggle mode** — `CBA_OFC_CROUCH` pressed on a pad (option bit set, not *kb*), or `CBA_OFC_CROUCH_TOGGLE` pressed on keyboard/mouse: flip the latch. Turning it on while standing crouches **only if byte `0x012e6ced` ≠ 0 or A² + B² < `Crouch_click_sensitivity`** (`0x0140e690`, registered at `0x00fefc00`); turning it off while crouched stands up. The latch is then re-synchronised to the actual crouched state (`0x00985cd0`) and both crouch actions are consumed.
4. Neither: a crouched player whose latch is clear stands up (hold released).

**`0x012e6ced` is 1 in the image and has no writer in `.text`** (the only instruction naming it is the read at `0x005b2f25`), so the stick test is skipped and a crouch toggle always applies (HIGH CONFIDENCE — an indirect writer cannot be excluded). This is the scoping note's "dead-zone test" **[corrects scoping §1.3: present but bypassed]**; `0x005ab930`'s keyboard idle test (§3.6.2 step 3) is the other stick test in this area.

#### 3.6.4 Jump and climb `0x005b29d0` (210 instructions) — CONFIRMED by full listing

Only on `CBA_OFC_JUMP_CLIMB` pressed:
1. When `0x0140e6d8` = 2, `+0x2084` bit `0x80` is set and `+0x28ad` bit `0x08` is clear: in cover, leave it with a vault (`0x009aaf80`, after realigning `+0x18c0` from `0x0071d5c0` or `0x014f7204`); otherwise, unless `+0x28ac` bit `0x80`, find a vault/climb target (`0x009660e0`) and start it (`0x009acb20`, hint `0xaf`), clearing `+0x2078` bit `0x200`.
2. With `+0x2078` bit `0x200` and no melee started this frame: report "jumped" (the argument byte), hint `0x51`; unless `0x0098b970`, probe for a ledge (`0x0058ae80`) and climb it (`0x00989ed0`, leaving scope view if active); then, unless `0x00989670`, clear `+0xec` bit `0x1000` and **jump** with `0x0098bea0(player, CBA_OFC_SPRINT held)`, call `0x007e2b30()` and set `0x0140e6f5`.

#### 3.6.5 Aim, shoulder camera and scope `0x005ad9a0` (376 instructions)

Read in full; the per-branch truth table is HIGH (traced once, not tabulated exhaustively — OPEN 20). Runs only with `+0x2078` bit `0x4` on foot, or in a vehicle with `+0x207c` bit `0x10000` and a vehicle; otherwise it leaves scope view and fine aim and clears the latch `+0x1d95` bit `0x40`. Inputs: consume-pressed `CBA_OFC_SHOULDER_CAMERA` and `_TOGGLE`, held `CBA_OFC_SHOULDER_CAMERA`, `CBA_OFC_ZOOM_IN`/`_ZOOM_OUT`; a weapon of type 7 (`+0x19c` record `+0x1c`) ignores the shoulder-camera inputs. Holding the button (keyboard/mouse, or a pad button whose descriptor byte `+0xa` is 0) requests fine aim directly; a press (pad hold-type button) or `_TOGGLE` (keyboard/mouse) flips the latch `+0x1d95` bit `0x40`, and fine aim follows the latch (`0x009dd010` toggles it, `+0x1d98` bit `0x80`). With a scope-capable weapon (record `+0x10` bit `0x80000`; `+0x14` bit `0x4000000`), `CBA_OFC_ZOOM_IN` while aiming enters **scope view** (`0x009e6510(player, 1)`: sets `+0x1d95` bit `0x10`, clears fine aim, switches the camera submode through `0x005753e0`); while scoped, zoom-type-1 weapons (`+0x760` = 1) zoom with `_ZOOM_IN`/`_ZOOM_OUT` by the frame time (`0x009da880`/`0x009e6820`), and releasing aim leaves it (`0x009e6510(player, 0)`).

#### 3.6.6 Melee `0x005abd70` (68 instructions) — CONFIRMED by full listing

With `+0x2078` bit `0x10` or `0x40`: `0x00984670(player)`; under bit `0x40`, `CBA_OFC_MELEE_NORMAL_ATTACK` pressed (clears `+0x15c4` bits `0x03`) or `CBA_OFC_MELEE_POWER_ATTACK` pressed (sets `0x01`, clears `0x02`) → `0x00950c30(player)`, whose result is "attack started"; if none started, `CBA_OFC_MELEE_NUT_SHOT` pressed (sets `0x02`, clears `0x01`) → `0x00950c30`. Then, when `0x009821b0(+0x1358)` holds and neither melee action is held, `0x009682d0(player, 1)`.

### 3.7 Stance bytes and intent fields — semantics

Writers and readers from the linear-sweep scan of `+0x1d94`…`+0x1d97` (some 300 instructions). Bits marked "outside U2" have writers outside the routines above; they are listed for completeness, not decoded.

| Field | Bit | Meaning | Evidence | Confidence |
|---|---|---|---|---|
| `+0x1d94` | `0x10` | **sprint requested and granted** — cleared at the top of `0x005ab930` and by `0x009fef60`, set by `0x009ffd00` while stamina remains; read as *sp* (max speed), s = 1, the writer's sprint branches, and passed to `0x00985df0` which drives `+0xccc` = 3 | §3.6.2, §3.5 | CONFIRMED (behaviour) |
| | `0x20` | **walk** (KB/M: s = 0.3) | `0x005ab860` | CONFIRMED |
| | `0x40` | sprint burst pending (one full-speed step along the facing) | set `0x005abcdb`, consumed `0x005aeeef` | CONFIRMED |
| | `0x80` | movement attempted while the animation locked movement | set `0x005aef43` only; no reader found | CONFIRMED (writer) |
| | `0x01`, `0x02`, `0x04`, `0x08` | outside U2 (writers `0x009e45e7`/`0x009e626f`; `0x006da520`/`0x009e71f0`; `0x009e2919`/`0x009e9351`; `0x009da1b2`) | — | OPEN 15 |
| `+0x1d95` | `0x01` | start/turn animation owed (set in the momentum states when s > 0.2, cleared when start-to-move fires) | §3.5.4 steps 6, 13 | CONFIRMED (behaviour) |
| | `0x02` | **panting** — stamina at or below the `PantPercentage` threshold | `0x009fef60`, `0x009ffd00`; Lua disable clears it (`spec-lua-api-behaviour.md` §18.26) | HIGH **[corrects scoping §1.5's "sprint allowed"; the switch is `0x0268d2a0`]** |
| | `0x10` | **weapon scope view active** | set/cleared by `0x009e6510`; read by ~40 camera/HUD/controls sites | CONFIRMED (writer); name HIGH |
| | `0x40` | shoulder-camera toggle latch | `0x005ad9a0` | CONFIRMED |
| | `0x80` | cleared on aim entry/exit | `0x005ad9a0`, `0x009e65fe` | OPEN 15 |
| | `0x04`, `0x08`, `0x20` | outside U2 (`0x00afba3f`; `0x009e892f`; `0x009dce73`/`0x009dce9a`/`0x005adc6b`) | — | OPEN 15 |
| `+0x1d96` | `0x02` | sprint was wanted this frame | `0x005ab930` | CONFIRMED |
| | `0x04` | sprint-toggle latch | `0x005ab930`, `0x009fef60`, `0x009ffd00` | CONFIRMED |
| | `0x08` | walk-toggle latch | `0x005ab860` | CONFIRMED |
| | `0x10` | crouch-toggle latch (re-synced to the crouched state) | `0x005b2ca0` | CONFIRMED |
| `+0x1d97` | `0x08` | **stopping** (intent just went to zero with residual velocity) | §3.5.4 step 10 | CONFIRMED |
| | `0x10` | sprint style without weapon (start animation `0x306`) | §3.5.4 step 16 | CONFIRMED (behaviour) |
| | `0x20` | sprint style with weapon — `Weapon Sprint Bool` (`spec-anim-runtime.md` §4.2 row 29) | §3.5.4 step 16 | CONFIRMED |
| | `0x01`, `0x02`, `0x04`, `0x40`, `0x80` | outside U2 (`0x01` is set by about 20 sites in `0x00824000`…`0x00893400`) | — | OPEN 15 |

| Field | Contents | Confidence |
|---|---|---|
| `+0x2048`/`+0x204c`/`+0x2050` | **desired move vector D** (units/s; y = 0) — `spec-anim-runtime.md` §4.2 row 6 reads its length as `desired speed` for the local player | CONFIRMED |
| `+0x203c`/`+0x2040`/`+0x2044` | **smoothed move vector r**, length ≤ M | CONFIRMED |
| `+0xa60` | \|r\| (desired speed carried in the human block) | CONFIRMED |
| `+0xa50`/`+0xa54`/`+0xa58` | **desired facing / aim direction** — unit vector toward `+0xcb0` for the player **[§5.4.4: `+0xcb0` is the move target, so this is unit(r), the desired move direction]** | CONFIRMED (writer) |
| `+0xa68` | turn angle: heading(orientation) − heading(`+0xa50`) | HIGH |
| `+0x2058`, `+0x205c`, `+0x2060`, `+0x2068`, `+0x206c` | timers: sprint ramp; sprint style (5 s); sprint cool-down; start-move hold-off (500 ms); stick-above-0.8 (§3.5.4 step 4) | CONFIRMED (use) |
| `+0xa20`, `+0xa24`…`+0xa38` | dormant pivot mode and its pivot | CONFIRMED (structure) |
| `+0x1a28`/`+0x1a2c`, `+0x1a30` | move-to-object handle; forced target point | CONFIRMED (use) |
| `0x02289b24`…`0x02289b2c` | the move slot's target point | CONFIRMED |
| `0x0140e6c0`/`0x0140e6bc` | published A / B | CONFIRMED |

### 3.8 What a port must reproduce (on top of §1.13)

1. Per frame, in `0x005b7430`'s order: the classify (§3.2) → the action dispatch (§3.3) → the look (§3.4) → the movement slot (§3.5), honouring `0x0140e6f8` (a QTE or fired prompt suppresses movement).
2. The move intent of §3.5.5 exactly — quadratic magnitude, camera-flattened basis, M from the stance table, 0.3 keyboard walk, the approach rule and the M clamp — plus the gate of §3.5.1 step 4 (stale intent while entering a vehicle, in `Idle` or in `exiting node`).
3. The sprint/walk/crouch latches and the stamina coupling (§3.6.2–§3.6.3), including the 300 ms keyboard grace and the standstill burst.
4. A port without the original animation network may drive a character controller directly from `+0x203c` (velocity) and `+0xa50` (facing), and treat the animation requests of §3.5.4 step 13 as no-ops.

### 3.9 Corrections and cross-spec flags raised by U2 (no other spec edited)

- **`review/scoping-player.md` §1.3/§2:** the chain `0x005b3e80` → `0x005ad510` → `0x005ad130` → `0x005aee30` is wrong — `0x005ad510` is a dormant side branch that never reaches the writer; `0x005ad130` reads `CAA_CAMERA_ROTATE` only on that branch; `0x005ac620` never sets context bits 4 or 8 (rows 8/9/17 and 13 do); "the only routine that writes `+0x2048`…" holds only within the controls band; `0x009fef60` is the sprint recharge step; the crouch "dead-zone test" is bypassed by `0x012e6ced`. **§1.7**'s "`0x005ac620` sets context bit 4" — the in-vehicle context comes with a condition-row change. **§1.5**: `+0x1d95` bit 2 is "panting", not "sprint allowed".
- **This file, §2.7:** the satellite reading of bit 8 is confirmed by the classify side (row 13's `0x005aaf00`). §2.5's "four control floats" `0x0140e698`/`0x0140e69c`/`0x0140e6a4`/`0x0140e6a8` are the published and previous walk axes of §3.5.2 (the first pair has no reader).
- **`spec-anim-runtime.md` §4.2 row 21a** calls `+0xa50`…`+0xa58` "the velocity"; for the player the writer stores a **unit facing/aim direction** there (§3.5.4 step 19), and the velocity-like vector is `+0x203c`. Flagged for that file's owner; it may hold for NPCs.
- **`spec-lua-api-behaviour.md` §18.26:** the bit it clears (`+0x1d95` bit 2) is the panting flag; the sprint switch it writes (`0x0268d2a0`) is what `0x009fee70` reads to bypass sprint entirely (§3.6.2 step 1).

---

## 4. U4 — The follow camera and the camera mode machine

**Scope.** The camera-mode table and its selection stack; the generic pre-update `0x00565c60` and update `0x00566a40` that every mode runs through; the frame cadence of those calls in the world update; the gameplay follow camera (mode 0: enter `0x005732b0`, pre-update `0x005779b0`, update `0x00577cb0` → core `0x00575e30`) with the look-input path, the submode choice and blend, collision and the outputs; and where vehicle data enters it. Each other mode is identified by its selectors and entry points only; their bodies (look-through, death, satellite, store, cinema editor, …) are U8 territory. The cutscene camera (mode 7) is `spec-cutscene-camera-format.md` §10 and is cited, not re-derived. **Method:** Ghidra-free, as for U0–U2: capstone recursive-descent listings over `tools/harnesses/exe_va.py`, a linear sweep of `.text` for every instruction naming a camera global (absolute displacement) plus a displacement scan of the camera band for vehicle-record offsets, raw `E8`/`E9` rel32 scans and whole-image dword scans for every function cited as a callee or table entry, push surveys at every call site of the mode-stack functions, and static reads of the tables and constants. `0x00575e30` (1558 instructions), `0x005779b0`, `0x00577cb0`, `0x005732b0`, `0x00566a40`, `0x00565c60`, `0x00564880`, `0x00564910`, `0x005649d0`, `0x00566830`, `0x00565a40`, `0x0056f8b0`, `0x00570560`, `0x00570650`, `0x005750f0`, `0x00574350`, `0x0056c5b0`, `0x00574fe0`, `0x00573110`, `0x00573c10`, `0x0056ec70`, `0x0056ad50`, `0x00573630` and `0x00565d70` were read in full; others are marked where only their head or call shape was read. No Ghidra project was opened. No `.czn_pc` data was touched.

### 4.0 Results up front (U4)

1. **The mode table starts at `0x0111a1e8`, not `0x0111a1f0`, and has five slots per row: `+0x00` init, `+0x04` reset, `+0x08` enter, `+0x0c` pre-update, `+0x10` update** **[corrects scoping §1.8]**. The scoping layout read each row from its third slot, so its "`+0x0c` read by `0x00565a40`" and "`+0x10` read by `0x00566830`" are the *next* row's init and reset, and its "row 17's last two dwords are floats" is the start of the following constant block. Row numbers are unchanged. Row 0's init is the `camera_free.xtbl` loader `0x0056e4c0` (`spec-tables-environment.md` §12.2). CONFIRMED (§4.1).
2. **Mode identities** (§4.1): 0 follow camera; 3 debug free camera (with control state 7); 4 satellite-weapon view (empty row — the satellite controller writes the camera itself); 6 scripted look-through; 7 cutscene; 9 death camera; 11 object-relative hold camera (turnaround, cutscene manager); 12 a UI-band fixed camera; 13 gang customization; 15 showcase (stronghold unlock); 16 store/customization; 17 cinema editor. **Rows 1, 2, 10 and 14 are populated but unreachable** (no selector ever names them), and rows 5 and 8 are empty and never selected.
3. **Stack semantics** (§4.2): push **moves an already-stacked mode to the top** instead of duplicating it and has **no capacity check** (safe only because just six distinct values are ever pushed); pop removes **by value**; set-base only records the base while the stack is non-empty; a mode change calls the new row's enter and **never an exit**. Exhaustive survey: 8 push, 7 pop, 17 set-base sites; base values 0, `0xa`–`0x10` (scoping listed 0, `0xc`, `0xd`, `0xe`, `0x10`).
4. **The three update calls are not three phases** **[corrects scoping §1.8's HYPOTHESIS]**: they are mutually exclusive branches of the world update (§4.3). In ordinary play the camera updates **once per frame, after physics and character movement** (`0x00702d18`); while a cutscene is playing it updates once **before** the controls instead (`0x00702a98`); while paused it updates (`0x00702eee`) only when certain HUD screens are open. The *pre-update*, which integrates look input into the camera angles, normally runs **inside the controls** (state 2's tail), so a frame is: look input → angles (controls) → physics → position, collision, blend, commit (camera update).
5. **Look input → rotation** (§4.6): the condition table's look slot stores yaw/pitch input in `0x013c8990`/`0x013c8994` (inversion applied, §3.4); the follow camera's pre-update turns them into angular **rates** through the `panning_group` multipliers and situation dampening (linear below `fast_pan_input_threshold`, time-accelerated above it for the pad; direct per-frame for the mouse), integrates heading `0x013c8944` and the user pitch term `0x013c8964`, clamps pitch to the active submode's elevation range, and writes the orientation rows; the update consumes the inputs (zeroes them).
6. **Outputs come in three sets** (§4.10): the active mode writes the **working** set (position `0x013c8750`, rows `0x013c8790`/`0x013c87a0`/`0x013c87b0`, FOV `0x013c87c4`); the update then blends and **commits** it to `0x013c8740`, `0x013c8760`/`0x013c8770`/`0x013c8780`, `0x013c87c0` (FOV optionally smoothed), after saving the previous commit to `0x013c8800`, `0x013c8810`…, `0x013c8840`. The "two adjacent position vectors" are working and committed; they are equal everywhere outside the camera update. Row 1 is right, row 2 up, row 3 forward.
7. **Submodes** (§4.8): the 61 `camera_free.xtbl` submode records are chosen per frame from the target's state (human or vehicle chooser) and blended by a 4-kind blend (snap, hold-then-cut, linear, symmetric ease) over the authored `blend_time`, with special durations for interior↔exterior and ragdoll, and an instant snap when the two look-at offsets are more than 15 units apart. Record fields `+0x24`/`+0x28` (left open by `spec-tables-environment.md` §12.2) are the **base elevation** atan(`y_dist`/`z_dist`) and the **follow distance** √(`z_dist`² + `y_dist`²).
8. **Collision** (§4.9) is a sphere cast of radius 0.12 from the look-at pivot to the desired camera position (filter callback `0x00566070`); the camera stops at the first blocking contact. A per-object flag (`+0xd24` → `+0x10` bit `0x1000`) disables it.
9. **Vehicle cameras** (§4.12): driver views are submodes of mode 0 — CONFIRMED by the vehicle chooser `0x00573110`. Eight `vehicle_cameras.xtbl` fields are read by the follow camera (swing, sticky, heading retention, slerp/min-angle, roll, proximity lock, FOV scale, skip-transition), plus three per-vehicle manual-look hold values at `+0x6dc`…`+0x6e4`; the three camera-angle sets at `+0x628`…`+0x687` are **not** referenced by the camera band (OPEN 28).

### 4.1 The mode table `0x0111a1e8`

18 rows × `0x14` bytes. Slot readers, exhaustive by whole-image dword scan of each slot base (CONFIRMED): `+0x00` init ← `0x00565a40` (walks all 18 rows once at start-up; one caller, `0x005d304b`); `+0x04` reset ← `0x00566830` (the camera reset, §4.2); `+0x08` enter ← push, pop and set-base only (`0x005648f6`, `0x005649ae`, `0x00564a1a`); `+0x0c` pre-update ← `0x00565c60` only (`0x00565d3a`); `+0x10` update ← `0x00566a40` only (`0x00566bed`). Null slots are skipped; `0x00754410` is the shared single-`RET` stub. Static contents (CONFIRMED by direct read):

| Row | init | reset | enter | pre | update | Selected by (§4.2) | Identity | Confidence |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0056e4c0` | `0x005749e0` | `0x005732b0` | `0x005779b0` | `0x00577cb0` | base 0; forced by the reset | **gameplay follow camera** | CONFIRMED |
| 1 | — | — | `0x0057ea50` (FOV ← `0x012e59d0` = 60) | — | stub | nothing | unreachable | HIGH |
| 2 | — | — | `0x00578110` (sub-state `0x013cb7b0` ← 0, FOV ← 60) | `0x00578170` | `0x00578220` (279) | nothing | unreachable (§3.4's "mode 2 with `0x005780d0()` = 3" test is vestigial) | HIGH |
| 3 | — | — | `0x0057b6a0` (FOV ← `0x012e430c` = 60) | — | stub | push/pop by `0x00564cd0`/`0x00564d40` | **debug free camera**: pushed together with control state 7, whose per-frame `0x005c2800` writes the working set and calls the update (§2.7) | HIGH (identity); the gate is HYPOTHESIS |
| 4 | — | — | — | — | — | push `0x00564d60` / pop `0x00564d70` | **satellite-weapon view** (empty row) | HIGH |
| 5 | — | — | — | — | — | nothing | empty | CONFIRMED |
| 6 | — | — | — | — | `0x0057a490` | push by the look-through starters | **scripted look-through** | HIGH |
| 7 | — | — | — | — | `0x00569640` | push `0x005695e0`, pop `0x00569500` and the update itself | **cutscene camera** — `spec-cutscene-camera-format.md` §10.2 | CONFIRMED (cited) |
| 8 | — | — | — | — | — | nothing | empty | CONFIRMED |
| 9 | — | — | `0x00569a60` | — | `0x00569c60` (149) | push only | **death camera** | HIGH |
| 10 | — | — | — | — | `0x00569ed0` | set-base by `0x00569f20` (no caller) | fixed stored transform; unreachable | HIGH |
| 11 | — | — | `0x0057f7b0` | — | `0x0057ff10` (200) | set-base by `0x00580260` | **object-relative hold camera** (turnaround, cutscene manager) | callers CONFIRMED; behaviour HYPOTHESIS |
| 12 | `0x0057e740` | `0x0057e740` | — | — | stub | set-base by `0x0057e7d0` | UI-band fixed camera | OPEN 25 |
| 13 | stub | stub | — | — | `0x005676e0` (292) | set-base by `0x005674d0(true)` | **gang customization** | HIGH |
| 14 | `0x00577e70` | stub | — | — | `0x00577e80` (154) | `0x00577e20` — only ever called with *false* | unreachable | HIGH |
| 15 | stub | stub | — | — | `0x0057ee90` | set-base by `0x0057eb10` | **showcase camera** (stronghold unlock and two others) | callers CONFIRMED; name HYPOTHESIS |
| 16 | `0x00567bf0` | — | — | `0x005681f0` | `0x005683c0` (563) | set-base by `0x00567f80` | **store / customization camera** | HIGH (band); name HYPOTHESIS |
| 17 | — | — | — | — | `0x00bca9b0` (127) | push `0x00bc6a70`, pop `0x00bc6160` | **cinema-editor camera** — scoping's HYPOTHESIS confirmed as to selection | HIGH |

Evidence for the identities (CONFIRMED as call edges):
- **3:** `0x00564cd0` pushes camera mode 3 and control state 7 (§2.6) and `0x00564d40` pops both; their only entries are tail jumps from `0x0057b660` (taken when the global `0x029cdb88` is non-zero and the mode is not already 3) and `0x0057b680` (when it is 3). Neither of those two has a direct caller or a dword reference (whole-image scans), so they are reached through a table this pass did not find — HYPOTHESIS: a debug command, `0x029cdb88` being a development switch (233 readers; written only by `0x00d9f250`). This advances §12 OPEN 6.
- **4:** pushed at `0x005b25d7` inside condition row 13's look slot `0x005b2540` (row 13 is the satellite condition, §3.9) right after `0x00b6fb90` initialises the satellite controller `0x0130eee8`; popped from the controller's teardowns `0x00b700d0` (`0x00b70160`) and `0x00b709d0` (`0x00b70b5b`; `satellite_weapon_mode_exit`, `spec-lua-api-behaviour.md` §12). `0x005b2540`, `0x00b700d0`, `0x00b709d0` and `0x00b70d10` write the working outputs themselves, which is why the row is empty; the streaming focus uses the player's own position in this mode (scoping §1.6).
- **6:** pushed by `0x00579ef0` (at `0x0057a2fc`; callers `0x00a27dfc`, `0x00a469ac` in the Lua band) and by the small wrapper `0x00579ec0` (at `0x00579edb`, right after calling the path builder `0x00578d40`, which sums an array of segment durations into a timed path; the wrapper has no direct caller or dword reference); popped by `camera_end_look_through` `0x00a44140` (`0x00a44280`, `spec-lua-api-behaviour.md` §13.16, whose "`0x00564910(6)`" this resolves) and `0x00a27c90` (`0x00a27cd5`), each right after `0x0056cc30`.
- **9:** pushed by `0x00702810` (uses `MISSION_FAILURE_DIED_TEXT`; caller `0x009504bf`) and by the local-player branch of `0x0098ce40` (`0x0098d592`); never popped — the camera reset clears it at respawn. The enter stores the normalised horizontal offset from the local player to the previous camera position (`0x013c8800`) in `0x013c9240`.
- **11:** `0x00580260(object, …, position, rows, …, flag)` saves the current base in `0x013cba50` and sets base 11 (with a line-of-sight query `0x0075d7e0` first when its last argument is 1). Callers: AI mode 43's tick `0x004f1440` (`0x004f21fa`, passing the current working position `0x013c8750` and rows `0x013c8790`), the cutscene manager (`0x007291fa`, `0x0072b6df`) and `0x006bbbe0`. The same AI tick restores the saved base with `0x0057ef00` (`0x004f1526`) next to releasing the HUD element `0x012e1b88`. The update can return to the follow camera by itself (`0x0056b370` at `0x0058021b`).
- **13:** `0x005674d0(1)` from `0x0083ac4b` and `0x005674d0(0)` from `0x0083a89f`, both in the gang-customization UI code (`spec-lua-api-behaviour.md` §53.1's registrar `0x0083a900`).
- **15:** `0x0057eb10(position, rows, …)` from the stronghold-unlock helper `0x005f1cc0` (`0x005f1fa0`, `spec-lua-api-behaviour.md` crib unlock), `0x008123de` and `0x00a035cc`; restored by `0x0057ea90` (`0x005f0b0a`, `0x008127d0`, `0x00a00653`) and `0x0057eae0` (`0x00811f26`), each gated by `0x0057ead0`.
- **16:** `0x00567f80` has 16 callers (`0x005fa9af`…`0x008206a3`), all but two in the store/customization UI band `0x0080c000`–`0x00821000`; its init calls `0x0080c470` and `0x0080b920` in the same band.
- **17:** `0x00bc6a70`/`0x00bc6160` are in the cinema-editor band (`0x00bc5610` ~~is the editor-active test of §2.7~~ **is the machinima-recorder stopping/finalizing predicate referenced in §2.7's row handler — not a general "editor active" test; see §2.7's correction, 2026-10-10**).

### 4.2 Mode selection

| Address | Contents | Static |
|---|---|---|
| `0x013c87c8` | current mode | 0 |
| `0x013c87cc`…`0x013c87e8` | stack, 8 dwords (bottom first) | 0 |
| `0x013c87ec` | stack count | 0 |
| `0x013c87f0` | base mode (used when the stack is empty) | 0 |
| `0x013c88a0` | camera flag word: bit `0x02` **transition blend active**, bit `0x04` **snap requested**, bit `0x08` **smooth the FOV** | 0 |

**Push `0x00564880(mode)`** (37 instructions; CONFIRMED): search the stack from the top down for an entry equal to *mode*; if found, remove it (later entries shift down, count − 1). Then store *mode* at index *count* and increment the count — **there is no capacity check**; the dword after the eighth slot is the count itself. Because a re-push only moves the mode to the top, the depth can never exceed the number of distinct values pushed, which is six (3, 4, 6, 7, 9, `0x11`), so the overflow is unreachable in this build (HIGH). Finally, if the current mode differs from *mode*: clear flag bit `0x02`, store *mode* as current, and tail-call the row's enter. This differs from the control-state push (§2.5), which drops on overflow and keys by handle.

**Pop `0x00564910(mode)`** (46 instructions; CONFIRMED): nothing if the count is 0. If the top entry is *mode*, drop it; otherwise search downward for it and remove it if present (no effect if absent). Then — even when nothing was removed — the current mode becomes the top entry, or the base when the stack is empty; flag bit `0x02` is cleared; and if the current mode changed, the new row's enter is tail-called. **Removal is by value**, so out-of-order pops work; at most one entry can match.

**Set-base `0x005649d0(mode, bool blend, float time)`** (23 instructions; CONFIRMED): always stores the base. Only if the stack is empty and the current mode differs: clear bit `0x02`, set the current mode, and when *blend* is set start a **transition blend** `0x00580700(time, 7)`; then tail-call the enter. With a non-empty stack the new base takes effect at the next pop that empties it — **without** a blend.

**No mode has an exit callback**: every change runs only the incoming row's enter (`+0x08`).

**Getter `0x005649c0`** returns the current mode (32 callers). **Reset `0x00566830`** (123 instructions; callers `0x006d4375`, `0x00708490` (start-up, next to the control-state reset of §2.5), `0x009a7b6a`, `0x009e73bb`): sets the camera target handle `0x013c8848`/`0x013c884c` to the local player, **current ← 0 and count ← 0 without calling an enter and without touching the base**, clears `0x013c88a0` and the snap/fade timer `0x012e3cf4`, snapshots the target's position and orientation into `0x013c8850`/`0x013c8860`…, calls every row's reset slot, resets the blend (`0x00580410`), snaps the follow camera (`0x00566620`) and restores the DOF defaults. **Init `0x00565a40`** (start-up) calls every row's init slot, zeroes the count, sets all three FOV fields to 60 (`0x01173de4`) and the committed rows to the identity.

**Transition blend** (`0x00580700(time, parts)`, 6 callers): unless `0x013cbb34` is 1, snapshot the committed camera — rotation as a quaternion (`0x00533d90` on `0x013c8760`) at `0x013cbb40`, position `0x013cbb50`, FOV `0x013cbb60` — zero the elapsed time `0x013cbb64`, store the duration `0x013cbb68` and the part mask `0x013cbb6c` (7 from set-base), select blend type 0 (`0x013cbb30`) and set flag bit `0x02`. The update applies it (§4.11 step 3) through `0x00580b20` → type 0 `0x00580820` (eased parameter elapsed/duration via `0x00dadb00`; position lerp, quaternion slerp `0x004c0d10` and FOV lerp, each enabled by a bit of the part mask; at the end the type becomes −1 and bit `0x02` is cleared) or type 1 `0x00580430` (not read; started from a second entry inside the `0x00580700` routine). `0x00580420` cancels (18 callers). **While bit `0x02` is set the look slot ignores look input** (§3.4 step 1).

**Every call site** — exhaustive raw rel32 scan; there is no dword reference to push, pop, set-base, the getter or the reset anywhere in the image, so there is no indirect caller. Pushed values from the preceding `push` (CONFIRMED):

| Function | Site | Mode | Containing routine / caller |
|---|---|---|---|
| push | `0x00564cdb` | 3 | `0x00564cd0` (debug camera on) |
| push | `0x00564d62` | 4 | `0x00564d60` ← `0x005b25d7` (satellite on) |
| push | `0x00569625` | 7 | `0x005695e0` (cutscene camera enter) |
| push | `0x00579edb` | 6 | `0x00579ec0` (look-through path, after `0x00578d40`) |
| push | `0x0057a2fc` | 6 | `0x00579ef0` (look-through start) |
| push | `0x007028b7` | 9 | `0x00702810` (death) |
| push | `0x0098d592` | 9 | `0x0098ce40` (local player death) |
| push | `0x00bc6bf3` | `0x11` | `0x00bc6a70` (cinema editor) |
| pop | `0x00564d4d` | 3 | `0x00564d40` (debug camera off) |
| pop | `0x00564d72` | 4 | `0x00564d70` ← `0x00b70160`, `0x00b70b5b` |
| pop | `0x0056950c` | 7 | `0x00569500` (cutscene script cleared) |
| pop | `0x00569667` | 7 | `0x00569640` (cutscene update, no script) |
| pop | `0x00a27cd5` | 6 | `0x00a27c90` |
| pop | `0x00a44280` | 6 | `camera_end_look_through` `0x00a44140` |
| pop | `0x00bc617f` | `0x11` | `0x00bc6160` (cinema editor) |
| set-base | `0x005674f9` / `0x00567509` | 13 / saved `0x013c9020` | `0x005674d0(bool)` |
| set-base | `0x0056809a` / `0x00568105` | 16 / saved `0x013c9120` | `0x00567f80` |
| set-base | `0x00569f74` | 10 | `0x00569f20` (no caller) |
| set-base | `0x0056b37f` | 0 (blend and time from the caller) | `0x0056b370` ← `0x0058021b`, `0x006a4264`, `0x009a951c` |
| set-base | `0x00577e3f` / `0x00577e4f` | 14 / saved `0x013cb790` | `0x00577e20(bool)` ← `0x009a952f` (false only) |
| set-base | `0x0057ea18` / `0x0057ea38` | 12 / saved `0x013cb8b4` | `0x0057e7d0` |
| set-base | `0x0057eb9a` | 15 (saves `0x013cb9e0`) | `0x0057eb10` |
| set-base | `0x0057eaa2`, `0x0057eafb` | saved `0x013cb9e0` | `0x0057ea90`, `0x0057eae0` |
| set-base | `0x00580394` | 11 (saves `0x013cba50`) | `0x00580260` |
| set-base | `0x0057ef12` | saved `0x013cba50` | `0x0057ef00` |
| set-base | `0x006baabc` | 0 | `0x006baa30` (also sets the target handle) |
| set-base | `0x0072a6a4` | 0 | cutscene manager `0x0072a160` (also sets the target handle) |

Unless stated, set-base sites pass blend = 1 and time 0.4 s (`0x011192f8`). Every "saved" restore re-sets a value that was the base before, so no restore can introduce a new mode. **Hence modes 1, 2, 5, 8, 10 and 14 can never become current in this build** (HIGH — no other writer of `0x013c87c8`/`0x013c87f0` exists; the only other writers of the current mode and count are the reset and init above).

**Camera target.** `0x013c8848`/`0x013c884c` is a handle pair (resolved through `0x00458230` on `0x024433a8`, as everywhere in the game). Writers: the reset (local player), `0x005728a0` (the submode blend, §4.8), `0x00573500` (7 callers, vehicle interaction and `0x0060c89c`/`0x0060e396`), `0x0057fc10` (cutscene manager, 9 callers), `0x0061bff0`, `0x006baa30`, `0x0072a160`, `0x009e3b90`. **Without a valid target the camera update does nothing at all** (§4.11).

### 4.3 Frame cadence — the three update sites

CONFIRMED by listing of the world update `0x00702a50` and the predicates:
- `0x00707490` = "paused" (`0x01503eb0` > 0).
- `0x007205d0` = "a cutscene is playing" (cutscene manager state `0x0153b520` in 10…13 — the states `spec-cutscene-camera-format.md` §10.6 item 2 names).
- `0x007bfa70` = any of HUD screens `0x35`…`0x39` present in the HUD-screen registry `0x012fced8` (`0x007b3ba0`/`0x007b3c50` tests; `0x007bd710` covers `0x35`–`0x37`). Which screens these are was not resolved (OPEN 26).

| Site | Branch | Runs when | Preceded by |
|---|---|---|---|
| `0x00702a98` | early | not paused **and** a cutscene is playing; sets a local "early camera ran" flag | the world update's only pre-update call, `0x00702a93` |
| `0x00702d18` | late — after physics (`0x00702c46`), character movement (`0x00702c9b`) and the systems after them | not paused, and either (the early pass did not run **and** no cutscene is playing now) **or** `0x007bfa70()` | — |
| `0x00702eee` | paused path | paused **and** `0x007bfa70()` **and** screen `0x39` is not present | — |

The first two are each followed by `0x0058b470` (a jump to `0x00a1f880`, not read). So **one update per world update in ordinary play (late)**, one early instead during cutscene playback, a second (late) pass only while one of those HUD screens is up, and none while paused unless such a screen is up. All three use the same mode and the same time step: the update's step is `0x013c88b4`, stored by the most recent pre-update. Other callers of the update: control state 7's per-frame (`0x005c2f0f`, under a mode-3 test), the rows-0/1 per-frame in the cinema editor (`0x005b2239`, §2.7), the cutscene manager `0x0072ba85`, and `0x005b9e3e`.

**Where the pre-update runs** (`0x00565c60`, 3 callers): state 2's tail `0x005b770d` (after the profile's camera hook, unless paused — §2.7), the locked-state per-frame `0x005b2170` (`0x005b219e`), and the cutscene branch above. Its step is the frame time `0x0132a0ac`, replaced by `0x00756790()` — the physics step's elapsed time quantised to whole fixed steps (`0x012f8c14`) — when the target passes a class test (`0x00853b30(target, 7)`, or a human for which `0x009a1dc0` holds with `+0xcd2` ≠ 3) and no transition blend is active (HIGH: keeps a physics-driven target and the camera in step). It calls the current row's pre-update with that step and stores it in `0x013c88b4`.

**Consequence:** in ordinary play the camera's angles are updated during the controls (before physics), and its position after character movement, so camera-relative movement in U2 (§3.5.5) reads the committed rows of the **previous** frame's update (§4.10), while the camera follows the **current** frame's player position.

### 4.4 Follow-camera state

**Angle block `0x013c8940`** (offsets relative to it; writers named are the main ones):

| Offset | Address | Contents |
|---|---|---|
| `+0x00` | `0x013c8940` | follow sub-state 0…5 (§4.7 step 6) |
| `+0x04` | `0x013c8944` | **heading** (wrapped by `0x00dadeb0`) |
| `+0x08` | `0x013c8948` | **pitch** = `+0x20` + `+0x24` + `+0x28` + `+0x2c` + base elevation `0x012e4124` |
| `+0x0c`, `+0x10` | `0x013c894c`, `0x013c8950` | roll base and roll (roll = base + sway, §4.7 step 9) |
| `+0x14`, `+0x18` | `0x013c8954`, `0x013c8958` | sway terms (phase at `+0x18`) |
| `+0x20`…`+0x2c` | `0x013c8960`…`0x013c896c` | pitch terms: `+0x24` **user pitch** (look input), `+0x28` vehicle/helicopter pitch, `+0x2c` zeroed on a submode change |
| `+0x30`, `+0x34` | `0x013c8970`, `0x013c8974` | per-frame scripted pitch/yaw rates (`0x0056b190`), zeroed by every pre-update |
| `+0x40`, `+0x44` | `0x013c8980`, `0x013c8984` | yaw/pitch gains (1.0, set by the loader) |
| `+0x48`, `+0x4c` | `0x013c8988`, `0x013c898c` | pad pan-acceleration parameters (`+0x48` is passed to `0x00dade20`; 1.0, set by the loader) |
| `+0x50`, `+0x54` | `0x013c8990`, `0x013c8994` | **look input** yaw / pitch (§3.4) |
| `+0x58`, `+0x5c` | `0x013c8998`, `0x013c899c` | yaw / pitch pan rate (§4.6) |
| `+0x60`, `+0x64` | `0x013c89a0`, `0x013c89a4` | follow-swing request this frame / last frame (`0x0056b220`) |
| `+0x68` | `0x013c89a8` | swing rate (`0x0056b3b0`; reset each update to `follow_aggression_group` `default_swing_rate` `0x012e3f50`) |
| `+0x104` | `0x013c8a44` | base FOV (reset each update to the blended submode `base_fov`, §4.7) |

The enter `0x005732b0` sets the heading to the target's own heading (0 without a target) and then `0x0056bc60` zeroes every term above, sets the swing request to 1.0, the swing rate to `default_swing_rate`, the base FOV to the blended `base_fov`, and stops the follow timers (`0x012e41a4`, `0x013c8a20`). CONFIRMED.

**Submode blend object `0x012e4030`** (call it B): `+0x00` per-submode flag bits (set by `0x0056e370`'s per-submode switch), `+0x10` **current submode index `0x012e4040`**, `+0x14` blend kind, `+0x18` duration, `+0x1c` **progress `0x012e404c`** (1 = done), `+0x20` x-shift sign byte, `+0x50` the "from" record, `+0x90` the previous step's blended record, **`+0xd0` the blended record** (`0x012e4100`: look-at offset; `0x012e4110` min elevation; `0x012e4114` max; `0x012e4118` has-default; `0x012e411c` default elevation; `0x012e4120` base FOV; `0x012e4124` base elevation; `0x012e4128` follow distance; `0x012e412c` blend time; `0x012e4134` x-shift), **`+0x110` the look-at pivot** (`0x012e4140`), `+0x130` the "from" submode, `+0x140` a hold timer, `+0x158`/`+0x15c` the target seen last, `+0x160`/`+0x164`/`+0x168` changed / desired submode / its flag. CONFIRMED for every field named by a reader in §4.7–§4.8.

**Submode record `+0x24`/`+0x28`** (`0x0056ad50(z_dist, y_dist)`, called by the `camera_free.xtbl` submode reader; CONFIRMED): if `z_dist` is within 0.001 of 0 both are 0; otherwise `+0x24` = atan(`y_dist` / `z_dist`) and `+0x28` = √(`z_dist`² + `y_dist`²). So `z_dist`/`y_dist` author the camera's **base elevation and distance behind the pivot** in polar form. This closes the `0x0056AD50` question of `spec-tables-environment.md` §12.2 (flag only; that file not edited).

### 4.5 Pre-update `0x005779b0` (192 instructions; CONFIRMED)

1. When the sub-state is 0 or 5: `fine = 0x0056a690(current submode)` (true for the zoom submodes 9, 12, 13, 55), then the yaw step `0x00570560(step)` and the pitch step `0x00570650(step, fine)` (§4.6); `0x0057ad60` on `0x012e42a8` (with the angle block, the committed position and the base elevation — hill tracking, HYPOTHESIS from the `hill_tracking_group` values it reads); recompute the pitch sum `+0x08`; build the rotation from (pitch, roll, heading) with `0x004cd740`; in sub-state 5 adjust it with `0x00571f90`; **write the working rows `0x013c8790`/`0x013c87a0`/`0x013c87b0`**.
2. Fetch the target set `0x00572140` and test `0x0056c510` (the human target is in one of two animation states named by `0x0153bd54`/`0x0153bd58`, or has rig flag `0x4000`). When it holds, the pre-update runs the **animation-driven camera** `0x00575790` (it writes the working position, rows and FOV) and sets `0x013c9388`, so the next update skips the core once (§4.7). A latch `0x013c9383` suppresses `0x00575790` for the rest of the episode; it is set when `0x0056ab40` rejects the episode at its start, or when the probe `0x005812d0` succeeds for a point above the target or for `0x009e2d00`'s point while the target human is in locomotion state 3 and `0x00956380` holds. When the test fails, the latch is cleared and an active episode is ended (`0x0056ac60`). Structure CONFIRMED; what the episodes are (takedowns or similar) is HYPOTHESIS.
3. Zero the per-frame rates `+0x30`/`+0x34`.

### 4.6 Look input → rotation

**Producer** (§3.4, `0x005ab6a0`): only in mode 0, only when controls are enabled and no transition blend is active: `0x013c8990` = yaw input, `0x013c8994` = pitch input, after the drunk modifier and the per-device inversion options. The profile camera hook `0x005ab5b0` (§2.7) runs first and sets the swing request `0x013c89a0` (1.0 when the camera is pad-driven and the descriptor byte `+5` of `CBA_OFC_SPRINT`'s pad button is set, 0.3 under `0x006c4990(player)`, otherwise −1.0) and the swing rate (1.0). The two setters `0x0056b100`/`0x0056b120` have exactly three callers (raw scan): the row-0 look slot (shared by 14 condition rows, §3), row 14's look slot `0x005ad6b0` (with `0x005ad690` just before it), and `0x005b69f0` (`0x005b6ac9`/`0x005b6ad5`, beside the secondary hook of §2.7). Apart from them only the enter reset `0x0056bc60` and the follow update write the pair.

**Yaw step `0x00570560(step)`** (CONFIRMED): rate = `0x0056f8b0(block, step, fine)`; with the pad, heading += `+0x40` · rate · `0x00975770()` · step; with the mouse (`+0x9f6`, §1.5), heading += rate · `0x00975770()` — **no time step for the mouse**, whose input is already a per-frame delta (§1.9.2). Then heading += `+0x34` · step; both sums are wrapped. `0x00975770` is the player-side yaw factor: the smallest of three globals (`0x02625bc4`, `0x02625bfc`, `0x02625c34`), scaled by `0x0130a0b4` for the mouse when it is not 1, times `0x009785a0()` (HYPOTHESIS: the look-sensitivity option; OPEN 27).

**Pitch step `0x00570650(step, fine)`** (CONFIRMED): the same with `0x0056fdb0`, `+0x44`, `0x00975810` (`0x02625bc8`/`0x02625c00`/`0x02625c38`, `0x0130a0b8`, `0x009785c0`), added to the user pitch `+0x24`; then `0x0056bfe0` (pitch return, uses `pitch_reset_time`, not read in full), then `+0x24` += `+0x30` · step. **Reset to the default elevation:** when the submode has a default elevation (`0x012e4118`), the submode blend is incomplete and the fine-aim class did not change (`0x012e4169`), or once after a request (`0x012e4168`), `+0x24` = default − base. **Clamp:** `+0x24` ∈ [min elevation − base, max elevation − base] (`0x012e4110`, `0x012e4114`, base `0x012e4124`).

**Pan rate `0x0056f8b0`** (yaw, 323 instructions; CONFIRMED) — the `panning_group` block (`spec-tables-environment.md` §12.2):
1. Scale *s* = 1, or with *fine* the **zoom scale**: lerp(`zoom_scale_min` `0x012e3e78`, `zoom_scale_max` `0x012e3e7c`, clamp₀¹((player `+0x1e54` − weapon `+0x764`) / (weapon `+0x768` − weapon `+0x764`))) — the weapon's zoom level (`0x0056a880`).
2. *s* ×= one **dampening** factor chosen by the current submode, in this order: tank driver / rc tank → `tank` (or `tank_…_genki` when the vehicle's name hash is that of `sp_mdsphere_genki`, literal `0x0111bde8`); human shield / human shield fine aim → `human_shield`; `0x009ba860(player)` → `heavy_weapon` (HYPOTHESIS: carrying a heavy weapon); a fine-aim submode (`0x0056a630`: 4, 8, 20, 21, 22, 24, 28, 30, 45, 47, 49, 57, 60) → `fine_aim`; interior close / interior sprint → `interior_h`; skydiving 44–49 → `skydive_h`; freefall → `freefall_h`; parachute → `parachute_h`; helicopter driver → `helicopter_h`. The mouse uses the `_mouse` copies (`+0x44` further on).
3. **Mouse:** rate = yaw input · *s*. **Pad, inside `fast_pan_input_threshold`** (|(yaw, pitch)| ≤ `0x012e3e80`): rate = yaw · *s* · `slow_pan_horizontal_multiplier`. **Pad, beyond it:** if |yaw| ≤ `hpan_threshold` the rate is 0; otherwise an acceleration factor from `0x00dade20(+0x48, 0.1, 10, peg_accel_pan_horizontal_min, _max)` is applied to yaw · *s* and added to the previous rate, the result is kept between yaw · *s* · slow and yaw · *s* · `fast_pan_horizontal_multiplier`, and `0x0056bde0` eases the stored rate toward it. Stored at `+0x58`.

`0x0056fdb0` (pitch, 289) is the same with the vertical members (`fast/slow_pan_vertical_multiplier`, `peg_accel_pan_vertical_min/max`, `vpan_threshold`, the `_v` dampening values; freefall, parachute and helicopter have no vertical value and are not dampened), stored at `+0x5c`.

These reads fix the horizontal/vertical order of the dampening pairs that `spec-tables-environment.md` §12.2 left as a pattern: within `0x012e3e9c`…`0x012e3ec0` the horizontal member is first (`fine_aim` `0x012e3e9c`/`0x012e3ea0`, `heavy_weapon` `…ea4`/`…ea8`, `human_shield` `…eac`/`…eb0`, `tank` `…eb4`/`…eb8`, `tank_genki` `…ebc`/`…ec0`), then `interior_v` `0x012e3ec4` before `interior_h` `0x012e3ec8` as that section says (HIGH: inferred from which axis reads which slot; flag only).

**Consumption:** the core reads the pair again (any non-zero value cancels the look-at assist, §4.7 step 12, and restarts the manual-look hold timer, step 4), the sub-state handlers 1 and 4 read it (`0x0056c450`, `0x0056ef80`), the per-player update's `0x009ebde0` reads it (scoping §1.3's camera-assist HYPOTHESIS), and the follow update `0x00577cb0` **zeroes it** after the core. So one frame's look input is consumed by exactly one camera update.

### 4.7 Update `0x00577cb0` and the core `0x00575e30`

**`0x00577cb0(step)`** (82 instructions; CONFIRMED): unless the local player has `+0x1d95` bit `0x10` (scope view), `0x00994f60(player)` holds, `0x00614d00()` is 5 or `0x013c9363` is set, set the base FOV `0x013c8a44` to the blended `base_fov`. Run the core — or, when the pre-update set `0x013c9388`, skip it once and clear the flag. Then a surface clamp: `0x0079a420` on the working position returns two heights; if the camera is below their sum + 0.75 and less than 25 below it, it is raised to that level (**RESOLVED 2026-10-11, orchestrator-routed follow-up on the parked ground-missing bug** — see `spec-zone-data-format.md` §13.3's water-hypothesis note for full detail: `0x0079a420` first classifies the X/Y position against a single GLOBAL rectangular bounding box, `0x0223a7cc`-`0x0223a7d8` — not any per-tile `.czn_pc` record — then, only when near/inside that box, probes a downward ray from the position to 100 units below it via `0x0075dfc0` and returns the hit surface's position/height; outside the box it returns a fixed default. This is a generic anti-clip surface-raycast, not confirmed to be water-specific by this pass — the box is consistent with being the map's single ocean/water-plane boundary (one global region, not a per-tile water-volume lookup), but that identification remains HIGH CONFIDENCE, not proven; `0x0223a7cc`-`d8`'s own values and the box's real-world meaning were not read this pass). Then `0x0056ef10`, zero the look input, move the swing request to "last frame" and reset it to 1.0, and reset the swing rate to `default_swing_rate`.

**The core `0x00575e30(step)`** (1558 instructions; CONFIRMED by full listing). T = the camera target; V = T when T is of vehicle class (descriptor `+6` bit `0x80`), else none; P = the local player.
1. Without a valid T: return. Re-add last frame's lateral offset `0x013c8a60` to the working position and clear it (only intermediate reads see this; purpose OPEN 29). `0x005723e0`, `0x00920680(T)`.
2. **Submode choice and blend:** `0x005750f0(step)` on B (§4.8).
3. In submode 20, 8, 21, 22, 54, 57, 59 or 60 (the fine-aim family) with the blend complete, when P's weapon record has flag `0x80000` (scope-capable, §3.6.5), P's `+0x1d95` bits `0x10`/`0x20` are clear and `0x00956270(P)` is false: enter scope view (`0x009e6510(P, 1)`).
4. **Distance ramp** (vehicle fallback cams): when `0x012e3d31` is set, `0x013c8a48` ≠ 2, the timer `0x013c8a20` runs and the submode is not 7/8: ramp the follow distance between `0x013c8a14` and `0x013c8a0c` over the timers `0x013c8a1c`/`0x013c8a18` (totals `0x013c8a24`/`0x013c8a28`), never pulling the camera in faster than it is; store into `0x013c8a10` and over the blended follow distance `0x012e4128`; at expiry `0x0056da40` ends it. The ramp is started by `0x0056d980` from the `vehicle_fallbacks` records (`0x0056da10` reads `0x013c92f0` + `0x10`·n: distance, ramp-in, duration, return) — HIGH. **Manual-look hold:** when `0x0056f370()` holds (HIGH: the target is a vehicle), restart the timer `0x012e41a4` — keeping the longer of the remaining and the new time — whenever the look input is non-zero, or `0x00ad3700(V)`, `0x00b71ff0()` or `0x00b71790()` holds, or both `0x0056a850()` and `0x0056ef80(V, …)` hold; the new time is `0x00573630(V)` ms: `stationary_time` (`0x012e3f48`, or the vehicle's `+0x6dc`) when the vehicle's speed² is below `stationary_threshold` (`0x012e3f4c`, or `+0x6e4`), else `default_time` (`0x012e3f44`, or the vehicle's `+0x6e0`) — the `asvct_group` values (image defaults 3 s, 0.5 s, 1.0), all × 1000. With the mouse camera the moving case is 250 ms when `0x0056a850()` holds, else `+0x6e0` truncated **without** the × 1000 (CONFIRMED; HYPOTHESIS that it is a defect, or an intended near-zero hold for the mouse); `0x013c92e4` replaces `+0x6e0` when `0x012e3d44` = 1 (mouse) or `0x013c9360` is set (pad). Without a vehicle the timer is stopped.
5. **Sub-state selection:** with V and P: sub-state 5 when V passes `0x00ad3140` and `0x00ac04a0`; otherwise, if P drives V (`0x00ab5100`) or `0x009b91a0(P)` holds: sub-state 4 while the hold timer runs (unless the sub-state is 1 or 2), and, unless `0x009ec9e0()`, a vehicle-camera preset `0x0056d390(V, block)` with block = V's record `+0x688` when `0x00ad3ae0(V)` (skipped when `0x00ad3270` and `0x00ab4790`/`0x00ab47e0` hold) or `+0x6a4` when `0x00ad3b10(V)`; `0x0056d390` can set sub-state 2 (OPEN 28). Without V: a pending request `0x013c8a48` = 2 re-targets (`0x005735b0`, zeroing `+0x24`), = 1 sets `0x013c8a40`, and either returns to sub-state 0; with no request, sub-state 5 when `0x00ac13d0` holds for P's vehicle handle (`0x009b9130`).
6. **Sub-state handler** (jump table `0x00577994`, CONFIRMED targets): 0 `0x005710b0` (follow swing — reads the vehicle's `Sphere_Based`/`Position_Based` fields, `Camera_Roll`, and `0x0056f410`/`0x00570d70`, §4.12), 1 `0x0056c450`, 2 `0x00571680`, 3 `0x00573b10`, 4 `0x00571b10` (manual-look hold), 5 `0x00571e60`. Setters: 0 by `0x0056b1e0` (3 callers incl. the snap) and `0x00574b30`; 1 by `0x00574c50` (15 callers: cutscene manager, UI, player module, vehicle interaction); 2 by `0x0056d310` (via `0x0056d390`); 3 and 4 by `0x00574b30` (vehicle interaction); any by `0x005753e0`. Their bodies were listed for structure only (OPEN 30).
7. **Vehicle-aim heading limits:** in submodes 7–9 (`vehicle aim`, `vehicle fine aim`, `vehicle zoom`) with the target unchanged, the `Vehicle_Aim` record `0x013ca2f0` + `0x50`·k (k from `0x012e3d38`/`0x012e3d3c`/`0x012e3d40` per submode; `spec-tables-environment.md` §17.5) with flag bit 1 clamps the heading to `Heading_Center` ± `Heading_Range`/2 around V's heading (`0x0056b240`), unless controls are disabled.
8. **Helicopter pitch:** if P drives V, V's record `+0x2c` = 3, and the sub-state is 0 or 1: when V is slower than `Helicopter_Landing_Max_Speed` and a downward probe (`0x0075d7e0`) within `Helicopter_Landing_Max_Altitude` hits, the target is `Helicopter_Landing_Pitch`; otherwise V's pitch × `+0x71c`, zeroed inside the dead band `+0x724` and clamped to ±`+0x720`. `+0x28` moves 4 % of the way to the target per update (decays to 0 without one) — **per update, not per second**.
9. Recompute the pitch sum. **Sway:** effective heading = heading + sin(phase) · `0x013c936c`; roll = roll base + sin(0.9·phase) · `0x013c9370` + `+0x14` (smoothed with a per-step limit `0x00dadf70` unless V passes `0x00ac04a0`); both amplitudes decay by 10 % per update; phase += real frame time (`0x0132a0b0`) · 1.0. Large amplitudes cancel the look-at assist. The amplitude writers were not traced (OPEN 31).
10. **Orientation** M = `0x004cd740`(pitch, roll, effective heading); sub-state 5 adjusts it (`0x00571f90`).
11. **Distance** d = the blended follow distance (as of the previous blend step, `0x012e40e8`); for a locally driven, non-RC V outside submodes 7–9, scaled by V's speed against its record `+0x878` and `0x00a77170(V)` (HIGH).
12. **Look-at assist:** while the timer `0x012e41a0` runs and P exists, an assist point `0x013cb6f0` moves toward the object `0x013c92e8`/`0x013c92ec` (or follows the stick when there is none) and M is re-aimed at it over 20 refinement passes (`0x0056e170`), after which heading, roll base and user pitch are re-derived from M. Any look input, a strong sway or the submode blend stops it (handle reset to the "none" value `0x0111ab70`). Starters and the meaning of the object were not traced (OPEN 32).
13. **Placement.** Pivot G = B `+0x110` (§4.8). With a zoom submode (step 1's *fine*) and no V: **the camera sits at G** (scope view) with M rotated by `0x009ba2e0()`, and the angles are re-derived from it. Otherwise: d is capped by `0x00624070()` when `0x00614d00()` = 2, and in `shooting qte` d is the current distance from G to the previous camera position; back = −row 3 of M; for a non-`0x00ac04a0` V, back.y is raised to the vehicle's `Camera_Proximity_Lock_Y` (`+0x624`) and re-normalised; skydiving submodes 44–49 rotate it (`0x004d6160`); **desired = G + d·back**. If desired is within 1e‑5 of G on every axis the camera sits at G. Otherwise, unless T has the no-collision flag, desired is **collision-resolved** (§4.9). Store the working position `0x013c8750`; store M as the working rows except in submodes 44–49.
14. **Shoulder offset:** if the blended `x_shift` (`0x012e4134`) is non-zero, offset = `x_shift` · row 1 (right), kept in `0x013c8a60`, and the position moves by it — collision-resolved again unless the flag is set.

The FOV is written in step 2's helper path: `0x0056ec70(angle block)` (called at `0x00576204` with the output base `0x013c8740` in a register, so it does not appear in an absolute-address scan) sets **FOV = base FOV `0x013c8a44` · `0x00975bd0()`**, or for a locally driven, non-RC V outside submodes 7–9: base + (`0x00a77120(V)` − base) · speed / `+0x878`, times `camera_fov_scale` (`+0x860`), capped at `0x00a77120(V)` (0.9 of it when `0x00ad3290(V)`), and ±30° when `0x006ba890()` and `0x006ba8a0()` is 3 / 2. CONFIRMED (that `0x00a77120` returns `Max_FOV` `+0x864` is HYPOTHESIS).

### 4.8 Submodes: choice and blend

**Choice** (`0x00574fe0(target)`, CONFIRMED): a human target (descriptor `+0xa` bit 1, `+0x33` bit 8) → the human chooser `0x005745c0` (370 instructions; read for structure, not tabulated — OPEN 30); a vehicle target → `0x00573110(P, V)`; any other existing object → `spectator` (15); none → 62 (out of range, "no submode").

**Vehicle chooser `0x00573110`** (128 instructions; CONFIRMED) — this is where the driver views come from:
- P in a gunner seat (seat record `+0x9b8` not "none") and not driving (`0x009b91e0`): `vehicle zoom` (9) when P is a human with descriptor bit 2, `0x0056db80` holds and P is in scope view; else `vehicle fine aim` (8) when `0x009da830(P)`; else `vehicle aim` (7).
- `0x00ad3140(V)` (HYPOTHESIS: the skydiving tank): with `0x00ac04a0(V)`, `skydiving tank` (50) for the driver, else `skydiving tank gunner` (51); without it `tank driver` (11), or `rc tank` (42) when remote-controlled (`0x00a77a10`).
- by vehicle class `0x00ad2f80` (2…5): `watercraft driver` (5) / `rc watercraft` (39); `helicopter driver` (6) / `rc helicopter` (40); `airplane driver` (10) / `rc airplane` (41); otherwise `rc vehicle` (38) when remote-controlled, else **`0x012e3d34`** — the selectable driver view (static 3 = `vehicle driver`; set to 4 = `vehicle driver alt` by `0x0056b390`/`0x00575757`).

**Blend update `0x005750f0(step)`** on B (199 instructions; CONFIRMED): while B's hold timer `+0x140` runs, only step the blend. Otherwise choose the desired submode; if it or the target changed: restart a blend from the current to the desired submode (`0x0056e370`, which also sets B `+0x00`'s per-submode flag bits). **Duration:** the "from" record's `blend_time` if its `override_exit_blend_time` is set, else the "to" record's; `Exterior_Interior_Blend_Time` between `exterior close` and `interior close`; `Ragdoll_Blend_Time` (`0x012e3f3c`) while P is ragdolling (`0x009a1dc0`), or the value at `0x012e3f40` when the new submode is `ragdoll` and `0x009a1de0(P)`; **0 (instant)** when the target is unchanged and `0x013c9362` is set. `0x005728a0` installs the new target and submode; for a positive duration with zoom/zoom-crouch or a first blend, `0x00573890(duration in ms, fine-aim-mismatch)` starts B's hold timer. A change also zeroes the pitch term `+0x2c`, resets `0x013c8978`/`0x013c897c`, and records whether the fine-aim class changed (`0x012e4169`).

**Blend step `0x00574350(step)`** (144 instructions; CONFIRMED): save the blended record to `+0x90`; if `0x0056c780` reports the request unchanged, `0x00574070`; else, while progress < 1: **if the "from" look-at offset and the target record's are more than 15 units apart (squared distance > 225), snap** (`0x0056c860`: copy the record, progress = 1, cancel any transition blend, flag a camera cut `0x013c872c`). Otherwise progress += step / duration (clamped to 1) and, by blend kind `+0x14` (table `0x00574568`):

| Kind | Rule |
|---|---|
| 0 | snap (`0x0056c860`) |
| 1 | hold the "from" record until progress reaches 1, then switch |
| 2 | linear: `0x0056c5b0(from, to, t = progress)` |
| 3 | eased: t = `0x00dad960(progress, 1, 0.5)` — a symmetric ease-in/ease-out (constant acceleration to the midpoint, then constant deceleration; HIGH from the formula's shape), then `0x0056c5b0` |

`0x0056c5b0` interpolates the look-at offset, `x_shift` (the target's sign-flipped when B `+0x20` is set), min/max elevation, base FOV, base elevation and follow distance linearly; the default elevation and its flag are taken from the target outright. Then the pivot is recomputed (`0x00573c10`).

**Pivot `0x00573c10`** (297 instructions; CONFIRMED): G = the blended look-at offset, its height scaled by the human's size factor (`+0xd24` → `+0x18`), rotated by the target's heading (or full orientation when B flag `0x10`; not at all with flag `0x08`), plus the target's reference point (`0x0056e530`); with flag `0x02` and one of four human states (`0x00985cd0`…`0x00985d40`) a shape query (`0x0075df80`) may replace it; with flag `0x04` a vertical probe (`0x0075d7e0`) moves it to a hit of kind `0x14`, 0.5 higher (structure CONFIRMED; the purpose of both adjustments HYPOTHESIS).

### 4.9 Collision `0x00565d70`

`0x00565d70(out, from, to, filter, out-hit-flag, out-normal)` (205 instructions; CONFIRMED): if *to* is within 1e‑5 of *from* on every axis, out = *to*. Otherwise a swept-sphere world query (`0x0075c880`) from *from* to *to* with radius **0.12** (`0x012e3cec`) and the per-object filter **`0x00566070`** (407 instructions, also used by the snap and the core; read for structure only: it resolves the camera target and the local player's vehicle (`0x009445b0`, `0x009b9210`, `0x009b9230`) — HYPOTHESIS that it excludes them from blocking; OPEN 33). Hits on the local player's own vehicle of kinds 1, 4 and 8 are skipped. With no blocking hit, out = *to*; at the first blocking hit, out = **the hit's sweep position** (`+0x10`; normal `+0x20` to the optional output) and the camera-cut flag `0x013c872c` is set. There is no smoothing of the pull-in: the camera jumps to the contact each update and returns as soon as the path is clear (CONFIRMED for this function; any easing would have to come from the submode blend or the transition blend).

### 4.10 Outputs and the two position vectors

| Set | Position | Rows (right / up / forward) | FOV (degrees) | Written by |
|---|---|---|---|---|
| **working** | `0x013c8750` | `0x013c8790` / `0x013c87a0` / `0x013c87b0` | `0x013c87c4` | the active mode (pre-update and update), the setters below |
| **committed** | `0x013c8740` | `0x013c8760` / `0x013c8770` / `0x013c8780` | `0x013c87c0` | the update's commit (§4.11 step 6), the snap, init, `0x00564c20` |
| **previous** | `0x013c8800` | `0x013c8810` / `0x013c8820` / `0x013c8830` | `0x013c8840` | the update, before the mode runs; the snap |

Row identities: the follow camera places itself at pivot − d · row 3, so **row 3 is forward**; the shoulder offset is along row 1, so **row 1 is right**; row 2 is up (CONFIRMED by use). Readers outside the camera band: the committed set is read far more (position 176 sites, forward row 64, FOV 9 — including the frame setup `0x005e14a0`) than the working set (24 / 11 / 5), so **the renderer and gameplay read the committed set**. The two position vectors are equal at every point outside the update, except that `0x00564c20(position)` writes both directly (and records the jump in `0x013c8738`). That resolves `spec-tables-environment.md` §7's "whether another writer ever makes them differ" (they differ only transiently, inside the update) and refines `spec-cutscene-camera-format.md` §10.2/§10.5's "camera output globals", which are the working set (flags only).

Other per-update outputs: the target's position `0x013c8850` and rotation rows `0x013c8860`…`0x013c888c`, the step splatted in `0x013c8890`; the **camera-cut byte `0x012e59f8`** — 1 when the committed position moved by 75 units or more in this update, counting the largest direct jump recorded in `0x013c8738` (which is then cleared); the camera's zone word `0x013c88a4` from `0x00861aa0(position)` (previous in `0x013c88a6`); and the camera-cut flag `0x013c872c` (consumed by `0x009ed510`).

### 4.11 The generic update `0x00566a40` (464 instructions; CONFIRMED)

1. When the timer `0x012e3cf4` has expired, reset it and call `0x00a93320` (HYPOTHESIS: leaves the remote-control gun mode that `0x00a93070` enters, `spec-tables-environment.md` §7).
2. Resolve the target; **without one, skip everything** (step 3–7). DOF defaults (`0x013c8fd0`…`0x013c8fe4`) are reloaded every update unless the mode is 7, and cleared when `0x006948c0()` is false.
3. Save the committed set as previous. If the step `0x013c88b4` > 0, call the mode's update. If a transition blend is active, blend the working set from the snapshot (§4.2) and write it back.
4. `0x0057c930` (camera shake; 18 output writes, not read here).
5. **Snap:** if flag bit `0x04` (set by `0x00567280`, 12 callers including `camera_end_look_through`'s optional fourth call), run `0x00566620` — mode 0 only: reset the sub-state, snap the submode blend (`0x00574b10`), place the camera behind the target (`0x005733b0`, collision-resolved) and write the **same** transform into the working, committed and previous sets — and if that moved the camera by more than the threshold 225 (`0x01115320`, against `0x00da1330` of the old and new positions) and `0x024d4461` is clear, notify `0x0090fa60(0, 0)`.
6. **Commit:** working → committed (position and rows). FOV: with flag bit `0x08` (set by `0x006bbff0`, cleared by `0x00569640`, `0x006bc8e0`, `0x00722bd0`, `0x00bc9410`), committed += (working − committed) · clamp₀¹(5 · real frame time `0x0132a0b4`); otherwise committed = working.
7. When `0x0130a7b8` is set, sweep a sphere of radius 3 × 0.12 around the committed position and call `0x00564bc0` for each qualifying human found (HYPOTHESIS: hides characters the camera is inside); then the per-update outputs of §4.10.

### 4.12 Vehicle cameras

**Driver views are submodes of mode 0** — CONFIRMED (§4.8: the vehicle chooser maps class, seat, RC and the selectable driver view to submodes 3–11, 38–42, 50–51; the vehicle never pushes a camera mode). Where `vehicle_cameras.xtbl` (`spec-tables-ui-controls.md` §8.1) enters, by displacement scan of the camera band for the record offsets (every read is through V's record pointer `+0xbf4`; CONFIRMED as reads, use as described):

| Record field (§8.1 name) | Read by | Use |
|---|---|---|
| `+0x624` `Camera_Proximity_Lock_Y` | core `0x00577597` | minimum y of the camera's back direction (§4.7 step 13) |
| `+0x6ec`…`+0x6fc` `Position_Based` swing rates, `+0x6f0`/`+0x6f4` heading retention | `0x00570d70` (sub-state 0) | follow swing |
| `+0x70c`/`+0x710` `From_Sticky_Fwd`/`_Rev` | `0x0056f410` | a timed ramp (`0x013c89ac`) of the swing rate from the sticky value to the current one |
| `+0x700`/`+0x704` `Slerp_Value`/`From_Sticky_Slerp_Value`, `+0x708` `Min_Angle`, `+0x870` bit `0x80000000` | `0x005710b0` (sub-state 0) | sphere-based follow |
| `+0xaf0`/`+0xaf4` `Camera_Roll` | `0x005710b0` | roll |
| `+0x860` `camera_fov_scale` | `0x0056ec70` | FOV (§4.7) |
| `+0x870` bit `0x8000` `skip_camera_transition` | `0x00572090`, `0x005753e0` | submode change without blend (HIGH) |
| `+0x878` (not in §8.1) | `0x0056ec70`, core | speed normaliser for FOV and distance |
| `+0x6dc`/`+0x6e0`/`+0x6e4` (not in §8.1) | `0x00573630` | per-vehicle `stationary_time` / `default_time` / `stationary_threshold` (§4.7 step 4) |
| `+0x71c`/`+0x720`/`+0x724` (not in §8.1) | core | helicopter pitch gain / clamp / dead band (§4.7 step 8) |

The **camera-angle sets** (`Primary`/`Secondary`/`RC_Camera_Angle` at `+0x628`/`+0x648`/`+0x668`: look-at offset, follow distance and height, pitch limits) are **not referenced anywhere in the camera band**, and the core's two preset blocks `+0x688`/`+0x6a4` (passed to `0x0056d390`) lie just past them (OPEN 28). So scoping OPEN 8 is only partly answered.

### 4.13 What a port must reproduce

1. The 18-row table with the five slot roles, the stack semantics of §4.2 (move-to-top push, by-value pop, deferred base, enter-only, transition blend) and the reset at respawn/start-up.
2. The cadence of §4.3: angles in the controls, position after character movement; early update during cutscene playback; nothing while paused unless those HUD screens are up; the physics-quantised step for physics-driven targets.
3. For mode 0: the look-input pipeline of §4.6 with the `panning_group` constants (and the mouse's lack of a time step), the pitch clamp to the blended elevation range, the submode choice and 4-kind blend of §4.8 including the 15-unit snap, the pivot, distance, proximity lock and 0.12-radius sphere cast, the shoulder offset, the FOV rule, and the working → committed commit with optional FOV smoothing.
4. Consumers read the **committed** set; the working set is private to the camera update.

### 4.14 Corrections and cross-spec flags raised by U4 (no other spec edited)

- **`review/scoping-player.md` §1.8:** the table base is `0x0111a1e8` with slots init/reset/enter/pre/update (the "`+0x0c`/`+0x10`" roles and the "row 17 float block" are artifacts of the shifted base); base modes also include `0xa`, `0xb` and `0xf`; the three update sites are alternative branches, not phases; rows 1, 2, 10, 14 are unreachable. **§1.3:** the camera-assist `0x009ebde0` reads the look-input pair (`0x013c8990`/`0x013c8994`), not only the walk axes.
- **`spec-cutscene-camera-format.md` §10.2/§10.5:** the "camera output globals" `0x013C8750`/`0x013C8790`…/`0x013C87C4` are the *working* set; the generic update commits them to `0x013C8740`/`0x013C8760`…/`0x013C87C0`, and the DOF frame setup reads the committed FOV `0x013C87C0`. "Row 7 … third dword" is the fifth slot of a row based at `0x0111A1E8` (same row index). The mode-7 enter `0x005695E0` is called directly, not through the table (the row's enter slot is null). §10.6 item 4's "downstream cut smoothing" is the transition blend of §4.2 (only when the mode change goes through set-base with blend).
- **`spec-tables-environment.md` §7:** `0x013C8740` and `0x013C8750` are the committed and working camera positions; they differ only inside the camera update (§4.10). **§12.2:** `0x0056AD50` sets the submode record's base elevation `+0x24` and follow distance `+0x28` from `z_dist`/`y_dist`; the dampening pairs are horizontal-then-vertical.
- **`spec-tables-ui-controls.md` §8.1:** the follow camera also reads vehicle-record `+0x6dc`/`+0x6e0`/`+0x6e4`, `+0x71c`/`+0x720`/`+0x724` and `+0x878` (presumably from the four unread sub-readers there) and does not read the three camera-angle sets.
- **`spec-render-pipeline.md` §11** (view-matrix OPEN): its inputs are the committed set (`0x013c8740`, `0x013c8760`/`0x013c8770`/`0x013c8780`, FOV `0x013c87c0` in degrees), right/up/forward rows as above.
- **`spec-lua-api-behaviour.md` §13.16:** `0x00564910(6)` is "pop camera mode 6 (look-through)"; `0x00567280(0)` requests a follow-camera snap.
- **This file, §2.7 / §14 OPEN 6:** control state 7's camera is debug mode 3, entered only through `0x0057b660`, gated by `0x029cdb88` (§4.1).

---

## 5. U3 — The locomotion bridge

**Scope.** Everything between the intent fields U2 writes (`+0x2048`, `+0x203c`, `+0xa50`, `+0xa60`, `+0xa68`) and a moved capsule: which human field the character controller is actually driven by, who composes it, how the controller hands it to the Havok-side character proxy, what comes back (velocity, position, support, contact events), and when each step runs in the frame. The traditional U3 scope of `review/scoping-player.md` §5 — `0x00958690`'s reading of the intent fields into the named control parameters — is `spec-anim-runtime.md` §4.2 and is cited, not re-derived (§5.3). Havok's own step, solve and integrate code is out of scope; the Havok side of the boundary is `spec-physics-runtime.md` §2 and §4, cited by row. The game's custom proxy passes (`0x0079b000`–`0x0079e000`) and the controller's motion routine `0x007a5b10` are recorded at the contract level only; their internals belong to the physics character-controller unit (P5, `spec-physics-runtime.md` §2 preamble).
**Method.** As U0–U4: Ghidra-free. Capstone recursive-descent listings over `exe_va.py` — `0x0099ebe0` (1482 with the one jump-table case at `0x0099efbf` added by hand), `0x00961d70` (980) and `0x007a5760` (239) match the scoping census; `0x0099d1b0` (1022), `0x009a02a0` (593), `0x009989b0` (430), `0x0099ae30` (423), `0x00985df0` (409), `0x0099e2a0` (336), `0x009918b0` (330), `0x007a52c0` (319), `0x0099b430` (285), `0x0095f5b0` (194), `0x00997b50` (172), `0x0079e900` (39), `0x0099e7e0` (25) and the small accessors were read in full; `0x007a5b10` (1467), `0x007a2960` (578), `0x0079de40` (536), `0x004c58c0` (324), `0x007a45f0` (288), `0x007a4bc0` (226), `0x007a2170` (201), `0x0079f970` (156) and the replay handlers `0x007a37f0`/`0x00962c20`/`0x007a3fa0`/`0x007a2470`/`0x007a3640` were read for structure, call shape and their boundary fields only. A full linear sweep of `.text` for displacement scans of every human field named below, raw `E8`/`E9` rel32 and whole-image dword scans for every function and global cited, an RTTI walk of the four character classes' vtables, and `.rdata` constant reads. No Ghidra project was opened. No `.czn_pc` data was touched.

### 5.0 Results up front (U3)

1. **The scoping note's chain is not the locomotion bridge [corrects scoping §1.0, §1.4, §2 and this unit's brief].** `0x0099ebe0` is the **post-move per-human pass** (position sync from the proxy, water, death, attachments) and reads none of the intent fields; `0x007a5760` is **not the character controller** but a parameterless **flush of two deferred contact-event queues**; and `0x0079e900` → `0x00961d70` is **the replay of one queued "character touched a vehicle" event** — vehicle-hits-pedestrian damage, knock-down and AI events, plus the upgraded-sprint ram that pushes the car — run once per frame after all movement, never per sub-step, and writing **no velocity or displacement** to the proxy (§5.2, §5.8).
2. **The real bridge** (CONFIRMED by listing of the world update, §5.1): controls write intent (U2, `0x00702ad4`) → AI and per-human updates → animation jobs, including `0x00958690` and the controller update (`0x00702b9f`) → **velocity composer** `0x009989b0` (`0x00702bb7`) → `0x009a02a0` → **`0x0099d1b0`**, which writes the controller's desired velocity `+0x1cec` → Havok world step `0x0075ac20` (`0x00702c46`), which does **not** move characters → **character-controller step `0x0099e7e0`** (`0x00702c55`): gather `0x0099ae30`, step `0x0099b430` (the controller's motion routine `0x007a5b10` sets the proxy velocity, then at most ten lock-step integrate passes), write-back `0x0099e2a0` (`+0xd04` velocity, `+0xd1c` speed, landing and falling) → `0x00998850` → … → `0x0099ebe0` (`0x00702c9b`): position `+0x40` ← proxy, then the contact flush `0x007a5760`.
3. **The contract is one vector.** The character controller is driven only by human `+0x1cec`/`+0x1cf0`/`+0x1cf4`, the **controller desired velocity** (world space, units/s), recomposed every frame by `0x0099d1b0`. For the player on foot it is **`+0xa50` · `+0xa60` + R·E/Δt**, where `+0xa50` = unit(r) and `+0xa60` = k·\|r\| (k = `+0xa64`, 1.0 unless scripted) — i.e. **k · `+0x203c`**, U2's smoothed vector — plus the rotated per-frame translation E of the non-locomotion animation layers; **sprinting replaces the direction by the facing axis `+0x64`**. **The desired-move block `+0x2048` (D) is not read anywhere in the bridge**; it only feeds the animation parameters `desired speed` and `desired direction` (`spec-anim-runtime.md` §4.2 rows 6, 21b, 21c). (§5.4.)
4. **Root motion versus velocity — answered from the movement side** (the open question of `review/scoping-animation.md` §1.7). Horizontal motion is **velocity-driven**: the speed is `+0xa60`, and the root translation T of the phase-synchronised (cyclic) animation layers — the locomotion cycles — is used only to adapt the animation playback rate `+0x1a90` (clamped to [0.1, 1.5]) so the stride matches the speed. It is **root-motion-driven** only while the animation controller's flag `0x100` is set — i.e. while a playing animation record carries `Anim_Movement` (then the velocity is R·T/Δt). The translation E of the non-synchronised layers (non-looping graph clips) is **always** added — that is how climbs and vaults move the body vertically. Orientation is animation-driven and applied before physics; the capsule never rotates (§5.4.3, §5.4.5). **[Corrected by `spec-anim-runtime.md` §9.7 (U5), 2026-10-07: "animation-driven" holds only inside an authored correction window (`start correction`/`dest correction` triggers). Outside one, `0x004b6530` returns the orientation unchanged, and what turns a human in ordinary locomotion is not established there either — `spec-anim-runtime.md` §5.1 item 35.]**
5. **No sub-stepping reaches the intent.** Physics takes one world step per frame (`spec-physics-runtime.md` §4.3) and characters are not integrated inside it at all; game code integrates them afterwards with the full frame Δt, in up to ten lock-step cast passes over the remaining time. The desired velocity is composed **once per frame, before physics**, copied into a per-human record and handed to the controller **once**; the passes and the Havok listener callbacks never re-read intent, and the callbacks only queue events, which are replayed once per frame after movement (§5.9). Everything the animation side and U2 read back (`+0xd04`, `+0xd08`, `+0xd1c`, support, `+0x1cec`) is therefore **one frame old** when they run.
6. **NPCs share every function of the bridge** but diverge inside it (§5.10): the composer's NPC branch shapes direction and speed (acceleration-limited speed, turn-rate-limited heading, `+0xa60` = −1 meaning "the animation's own speed", a procedural lean); the gather **skips the controller step for far, unseen NPCs**; and only the player gets **stick-scaled air control**. The AI throttle of `spec-ai-runtime.md` §1.4 does not throttle locomotion.
7. **[corrects §3.5.4 step 19 and §3.7 of this file]** `+0xcb0` is the **move target point** (written by `0x00987360`, which the intent writer calls in step 18 with position + r), not an aim point; so for the player `+0xa50` = unit(r), the direction of the smoothed move — which is also what `spec-anim-runtime.md` §4.2 row 21a's "velocity" heading reads.
8. **`0x00985df0`, the locomotion-state update (OPEN 19), is resolved** (§5.5): it derives `+0xccc` (0 stopped or riding, 1 walk or in cover, 2 run above 2.5 units/s, 3 sprint), runs the sprint ramp timer `+0x2058`, and rewrites `+0xa60` ×= `+0xa64` through the shared locomotion setter `0x00942110`.

### 5.1 Frame order (CONFIRMED by listing of `0x00702a50` and of each callee's head)

| Site | Call | Role in the bridge |
|---|---|---|
| `0x00702ad4` | `0x005aa6c0` → state 2 `0x005b7430` | controls: intent `+0x2048`, `+0x203c`, `+0xa50`, `+0xa60`, `+0xa68`, `+0xcb0`; `+0xccc` (§3, §5.5) |
| `0x00702b84` | `0x009ea630` | player list: per-human update `0x0099bcb0` incl. AI mode 43 (`spec-ai-runtime.md` §1.1) |
| `0x00702b90` | `0x0099bba0` | NPC list: the AI writes the NPCs' `+0xa50`/`+0xa60`/`+0xcb0` (§5.10) |
| `0x00702b9a` | `0x00ac1950` | vehicle per-frame pass; ends with a contact-queue flush `0x007a5760` (`0x00ac19ea`, §5.8) |
| `0x00702b9f` | `0x00994e80` | animation jobs: `0x00958690` (`spec-anim-runtime.md` §4.2), the controller update, and `0x009a15f0`, which clears `+0xe4` bit `0x100` |
| `0x00702bb7` | `0x009989b0(Δt)` | **velocity composer** (§5.4) |
| `0x00702c46` | `0x0075ac20` | Havok world step (`spec-physics-runtime.md` §4.3); the character phantoms ride in it but are not integrated |
| `0x00702c55` | `0x0099e7e0(Δt)` | **character-controller step** (§5.6) |
| `0x00702c7a` | `0x00998850(Δt)` | post-step per-human pass `0x00997b50` (§5.7) |
| `0x00702c9b` | `0x0099ebe0` | **post-move per-human pass**: position ← proxy, then the contact flush `0x007a5760` (§5.7, §5.8) |
| `0x00702d18` | `0x00566a40` | camera late update on the moved player (§4.3) |

Both Δt arguments are the frame time `0x0132a0ac` read before the physics call (`0x00702b57`, `0x00702c3c`) — the same clamped variable step physics uses (`spec-physics-runtime.md` §4.3). `0x0099e7e0` does nothing when Δt is exactly 0.

### 5.2 The scoping chain, re-identified

| Address | Scoping note | What it is | Confidence |
|---|---|---|---|
| `0x0099ebe0` (1482) | character movement | post-move per-human pass over the object manager's `+0x1e4` list (§5.7); a displacement sweep of the full listing finds none of `+0x2048`…`+0x2050`, `+0x203c`…`+0x2044`, `+0xa50`…`+0xa58`, `+0xa60`, `+0xa68` | CONFIRMED |
| `0x007a5760` (239) | character controller | flush of the two contact queues `0x0223d3b0` (≤ 100 × `0x70`) and `0x0223ff70` (≤ 100 × `0x50`); no arguments; two callers, `0x009a025b` (end of `0x0099ebe0`) and `0x00ac19ea` (raw scan) | CONFIRMED |
| `0x0079e900` (39) | proxy callback | adaptor: unpacks one `0x70` record into `0x00961d70`'s seven arguments; one caller (`0x007a58bc`) plus one dword hit at `0x00b7c047` inside unrelated code | CONFIRMED |
| `0x00961d70` (980) | per-human motion callback | human-touched-vehicle response (§5.8) | CONFIRMED (role HIGH) |

The character controller proper is the RTTI class `character_controller` (`spec-physics-runtime.md` §2.1 row 25), one per human at `+0xcc0` (§5.6.1).

### 5.3 U3's traditional scope: `spec-anim-runtime.md` §4.2 — cited, offsets agree

`0x00958690`'s field → parameter table is `spec-anim-runtime.md` §4.2 and is not re-derived here. Agreement check of the fields it reads against U2 and this unit (offsets CONFIRMED; the source column is this file's):

| §4.2 row | Field read | Written by | Agreement |
|---|---|---|---|
| 6 `desired speed` | \|`+0x2048`\| for the local player (`0x0091f5e0` is "is the local player", CONFIRMED by listing), else `+0xa60` | `0x005aee30` step 8 (D); `+0xa60` by step 18 and `0x00985df0` (§5.5) | agrees; the player's parameter is the **unsmoothed** D while its controller speed is the **smoothed** k·\|r\| |
| 21b / 21c `desired direction`, `direction` | heading of `+0x2048` | §3.5.4 step 8 | agrees |
| 21a `direction` (combat ready) | heading of `+0xa50` | §3.5.4 step 19 = unit(r) (§5.4.4) | agrees; the "velocity" heading is the direction of the smoothed move |
| 20 `current speed` | `+0xd1c` | `0x0099e2a0`: \|proxy velocity\| (§5.6.6) | source identified; one frame old |
| 7 `Vertical Speed` | `+0xd08` | `0x0099e2a0`: proxy velocity y | source identified; one frame old |
| 5 `Supported By Ground` | `0x0096e670` | controller state byte `+0x08` bit `0x04` through `0x0079e670`, set by `0x007a5b10` from its support query | source identified |
| 24 `Speed Along rvec` | right axis · `+0x1cec` | **`+0x1cec` = the controller desired velocity** (§5.4) | resolves §4.2's HYPOTHESIS ("a velocity"); last frame's value, since `0x0099d1b0` runs after `0x00958690` |
| 25 `turn angle` | `+0xaac` | a rate-limited follower updated by `0x00956970`, called from the animation job (`0x00994a3d`); its link to U2's `+0xa68` was not traced | OPEN 37 |

§4.2's note that "the sources of those fields are U3's subject": position `+0x40` comes from the proxy (`0x0096e710`, §5.7), the orientation axes `+0x4c`/`+0x58`/`+0x64` from ~~the animation's root rotation~~ `0x004b6530` (§5.4.1 step 3), `+0xd04`…`+0xd1c` from the proxy (§5.6.6); the movement flags `+0xd40` were not traced (OPEN 38). **[Corrected by `spec-anim-runtime.md` §9.7 (U5), 2026-10-07: `0x004b6530` writes the orientation axes only inside an authored correction window; see §5.4.5's note below.]**

### 5.4 The velocity composer

#### 5.4.1 `0x009989b0(Δt)` (430 instructions) — CONFIRMED by full listing

1. **Player list** (`+0x1f0`): for each human with `+0x33` bit `0x08`, `+0xe4` bit `0x100` clear and the rig test `0x004bd660([+0xd24]+0x14)`: `0x009da350(player, Δt)` (130 instructions: `0x009a02a0`, then, for the local player, seven animation-state → camera-cue checks through `0x0057b6c0`), then `+0xe4` |= `0x100`.
2. **NPC list** (`+0x1fc`): the same gate, except that `+0xcd2` ∈ {1, 4} also passes the rig test: `0x009d3340(npc)` (81 instructions: `0x009a02a0`, then `+0x1da4` bit `0x40` upkeep and network replication `0x008ccb90`), `+0xe4` |= `0x100`; then a nearest-NPC bookkeeping on `+0xac4` and a per-NPC tint upload (`Diffuse_Color_a`…`_c`, `Hair`, `Sphere_Map`; not movement).
3. **All humans** (`+0x1e4`) with an animation controller: the **orientation step** `0x004b6530(out, controller, Δt, 1)` (596 instructions, animation band, read only as far as its use of controller `+0x70` and `+0x1a4`) and its matrix applied through the human's virtual slot `+0x48` (set orientation; HIGH from its other uses at `0x0099f154` and `0x0095f8c2`); then the synced-pair step `0x0095f5b0` (§5.4.6).

`+0xe4` bit `0x100` ("post-animation update done") is cleared by the animation job's `0x009a15f0` (`0x00994a74`) and by the cutscene manager, so each human gets one composer pass per animation step. The cutscene manager also calls `0x009a02a0` directly (`0x007268bf`, `0x00728119`, `0x007281ae`).

#### 5.4.2 `0x009a02a0(human)` (593 instructions) — CONFIRMED by full listing (helper roles HIGH)

The per-human post-animation update. In order: a pending `+0xe0` bit `0x100` request (`0x00943cc0`); for a player in fine aim (`0x009da830`), the timer `+0x1394` is kept at ≥ 1 s; return for an unusable human, or after `0x004b4030` for a network proxy (`0x008cc4d0`); bounds `+0x100`/`+0x110` from the animation (`0x004bf210`, padded) or, for `+0xcd2` 3 and 6, from the ragdoll (`0x0073fa00`); `0x009b3a20`; release of the facing override (`+0xa6c` → −1, `+0xa7c` = 2, `+0xa80` = 0) when its animation ended; **`0x0099d1b0(human, 0)` when the human is alive (or `+0xe8` bit `0x200000` / `+0xe0` bit `0x400000`) and `+0xcd2` is 0 or 6**; for one character type (`[+0xf0]+8` = `0x0262cf70`), every 3 s an 8 % chance of a knock-down when moving faster than 25 units/s (`0x0079efb0`, `0x009a48e0`); synced-partner animation (`0x0095f250`); removal (`0x00853ea0`) of a qualifying human more than 250 units (62500 squared) from both players; `0x0073eee0`; the weapon bool `+0x1bd0` (on a change, end `0x1d8f` and copy `+0xd04` into `+0x203c`); held-weapon animation hooks; and finally **`+0xdc` |= `0x100000`**, the bit `0x0099ebe0` requires. That bit is cleared each frame only for NPCs (`0x009d91a4` in `0x009d9190`, `spec-ai-runtime.md` §1.1).

`+0xcd2` is the human's physics-representation byte: 0 = character proxy; 3 and 6 take their bounds from the ragdoll (HYPOTHESIS: ragdoll and recovery); 1 and 4 let an NPC through the rig test.

#### 5.4.3 `0x0099d1b0(human, 0)` (1022 instructions) — CONFIRMED by full listing

Notation: s a speed, u a direction, v the composed velocity, all world space; R the human's orientation rows (`+0x4c` right, `+0x58` up, `+0x64` forward); Δt = `0x0132a0ac`; `0x029f4dd0` is the same Δt splatted into a vector by the frame timer (`0x00db9d6a`).

1. Unless `+0xe8` bit `0x80000000` (velocity pre-set by the synced-pair step, §5.4.6) or `+0xec` bit `0x01`: `+0x1cec` = 0.
2. A player-class human (`0x00853b30(·, 0x21)`) outside a cutscene whose `+0xcd2` is neither 0 nor 6 leaves now.
3. **Root-motion read.** `0x004be240(&E, &T, &X, h)`, h = the controller's animation-instance handle (controller `+0x20`, read by `0x00487e80`), → `0x004c58c0`, which sums the root-translation deltas of this step over the **layers of the animation instance** — not over the controller's slots: **T** = the blend-weighted (not normalised) translation of the layers whose layer flag bit `0x01` is set — the instance's **phase-synchronised group**: plain clips, blend-tree `States` and looping graph clips, i.e. the locomotion cycles; **E** = the weight-normalised translation of the other layers — non-looping graph clips (starts, stops, turns, vaults, jumps); **X** = the unweighted sum of the same non-synchronised deltas re-expressed in each clip's start-of-clip root frame — a translation, not a rotation — stored to controller `+0x70` for the orientation step. The bit is set by the engine's layer-post API, **not** copied from `Anim_Movement` (CONFIRMED by listing; full account in "Step 3 in detail" below, which resolves OPEN 39 in part). If `0x009987d0(&E)` holds — a state test, or (not free-falling, not jumping) \|E.y\| > 1e‑4 — controller `+0x1a4` |= `0x80` ("the animation drives the vertical", read by the gather and cleared by the write-back). **Pony cart:** a rider (`0x009b91a0`) of the vehicle whose `+0xbf4` record names `sp_pony_cart01` instead gets E = X = 0, a constant T, `+0xa60` = the cart's speed (`0x00abaf10`) and `+0xa50` = its own facing.
4. **Desired speed and direction.** s = `+0xa60`, u = `+0xa50`, v = u·s; "speed-matched" = (s ≥ 0). **If s is exactly −1** (the "use the animation's speed" sentinel, §5.5): s = (\|T\|/Δt) / `+0x1a90` × `+0xa64`.
5. **Choice of velocity** (first match):
   - animation-controller flag `0x100` (`0x004b4fc0` at `0x0099d544`; that "flag" is controller `+0x198` bit 8, i.e. **a playing animation record carries `Anim_Movement`**, see "Step 3 in detail"): **v = R·T/Δt** — the **root-motion-driven** case; not speed-matched;
   - `0x009573d0` (the facing override `+0xa6c`/`+0xa7c`, controller flag 4, or two states — U2 §3.5.4 step 14's predicate): v = 0;
   - **player class:** E = 0 under controller flag 4; then u = the facing axis `+0x64` when sprinting (`0x00941e60`: `+0xccc` = 3, not seated), or — when `+0xccd` is 2 or 3 (`0x0098da50`) — s = the stance maximum M (`0x009dca30`, §3.5.4 step 5) and u, s shaped by `0x009918b0` (§5.10) and speed-matched; **v = u·s**;
   - **not player class:** the local player, skydivers (`0x009adb00`) and riders (`0x009b9160`) keep v = `+0xa50`·`+0xa60`; otherwise E = 0 under controller flag 4; when s = −1 and controller `+0x1a4` bit `0x40` with an active rig, E = 0 and s = `0x004b49e0(controller, …)` (an animation-side speed, not read); then **`0x009918b0(&u, &s, npc)`** shapes both (§5.10), **v = u·s, also stored to `+0x1a94`**, speed-matched; and when u is non-zero, `+0xccc` is 2 or 3 and `0x00530340` is false, a **procedural lean** is updated (HIGH for "lean"): `+0x1aa0` chases a target in [0, 0.5] derived from the change of speed (0 when it rises faster than 5 units/s², 0.5 when it falls faster than 5; otherwise a `0x00dade20` remap capped at 0.5) with factor 0.7, and `+0x1abc` tilts away from up (0, 1, 0) toward a turn term (`0x00434f10` against `+0xd04`, remapped to ±0.2) at Δt/0.4 per frame.
6. Unless the lean was just updated, `+0x1aa0` decays by 15·Δt (to 0) and `+0x1abc` halves toward up; `+0x1acc` = `+0xa50`.
7. **Speed match (stride matching).** Not speed-matched when `+0xccd` ∈ {4, 5, 6}, when swimming (`0x009b5690`), under `0x00530340`, or when the rig (controller `+0x50`, flag `+0x80` bit `0x08`) has a weighted blend entry whose byte `+0x40` is clear. Otherwise, with s > 1e‑4 and ratio q = s / (\|T\|/Δt + 1e‑4): the rate `+0x1a90` rises toward q by Δt per frame (2·Δt below 1.0) or falls by 1.4·Δt (2.8·Δt below 1.0, and Δt more while it exceeds 2q), then is clamped to **[0.1, 1.5]**; not speed-matched → 1.0. `0x004be580(rig, +0x1a90)` makes it the rig's playback rate (rig record `+0x68`). `+0x1a90` has no other writer than this routine and the human constructor (displacement sweep).
8. `+0xe8` bit `0x80000000`: v += the old `+0x1cec`. `+0x1c9c` bit `0x2000` (HIGH: standing on a moving support): v += `+0x1c60`, and v.y += the proxy's current vertical velocity (`0x0079efb0`).
9. E ← R·E. **Upgraded jump:** with `+0x1c9c` bits `0x80000` (U2's boosted-sprint bit, §3.5.4 step 5) and `0x100000`, not supported, jumping (`0x0098b970`), `+0xec` bit `0x800` clear, `+0xcce` ≠ `0x27` and **`CBA_OFC_SPRINT` held** (`0x005bd7b0` at `0x0099e1af`, one of the sprint readers of §1.8.4): E ×= 1.5 (`0x0130a7b4`). Otherwise, for the local player, `0x0094a3b0(player, 0)` (not read).
10. Clear `+0xe8` bit `0x80000000`; **`+0x1cec` = v + E/Δt**.

**Step 3 in detail: the layer walk `0x004c58c0` (CONFIRMED by full listing, 2026-10-03) [corrects this step's earlier "slot flag `+0x80` bit 0" and refutes its `Anim_Movement` hypothesis].** `0x004be240` (19 instructions) and `0x004c58c0` (324) were read in full, with the layer-post, layer-advance and delta routines named below; call sites from a raw rel32 scan.

- **Which structure.** `0x004be240` validates h against the `0x3a0`-byte animation-instance pool (`0x0344c924`, 16-bit count `0x0344c91a`, self-index at `+0x50`; `review/scoping-animation.md` §1.6). `0x004c58c0` walks that instance's **inline layer array**: count instance `+0x58`, `0x50`-byte records from instance `+0x70`, kept sorted by group (insertion in `0x004c60e0`). Layer fields used: `+0x04` clip index (registry `0x030f0c68`, stride `0x24`), `+0x08`/`+0x0c` previous/current time (4,800 Hz ticks), **`+0x10` flags**, `+0x14` group (0–4, from post flags `0x10`/`0x20`/`0x40`/`0x80` → 1/2/3/4), `+0x24` effective weight. The "`+0x80`" a listing shows is instance `+0x80` = layer 0's `+0x10`, stepped by `0x50`.
  - It is **not** the controller's `0x84`-byte slot array (`spec-anim-runtime.md` §6.2, controller `+0x4c`), whose own `+0x80` flag word has `0x01` = "in use" — a field this walk never reads. Step 7's "flag `+0x80` bit `0x08`" **is** that slot word (the current slot, controller `+0x50`; `0x08` = blend-tree slot), so the two `+0x80`s of §5.4.3 belong to different structures.
- **Per-layer weight.** f = `0x004ca620`: 0 without clip data; 1.0 unless clip-header byte `+0x05` bit `0x40` (the per-track override table of `spec-anim-format.md` §10), else the override value (`0x004cad00`, not read). w = f · (layer `+0x24`) · (1 − min(1, Σ f′)), Σ over the later layers of a strictly higher group that are not additive (layer flag `0x2000`): a higher group with full root weight masks the lower ones. Layers with w ≤ 0 are skipped.
- **Per-layer delta.** `0x004ca850(clip, out, t₀, t₁)` clamps both times to [0, duration] (duration = clip-header word `+0x06` × 4,800 / 30 ticks) and takes Δ = P(t₁) − P(t₀) (`0x004ca260`, two samples by `0x004c9b90`). When the clip binding supplies it, `out` = Δ multiplied by the conjugate of a quaternion sampled at t₁ (`0x004c8a80`, identity (0, 0, 0, 1) when absent; conjugate by the sign mask `0x011942a0`; to a matrix by `0x004bc470`; product `0x004998e0`), and a second copy Δ′ = Δ by the conjugate of the same quaternion at t = 0. That this quaternion is the root rotation is HIGH (`0x004c8a80`, 1,236 instructions, read at its entry only); the conjugate-and-rotate chain is CONFIRMED.
- **Routing on layer flag bit `0x01`** (`0x004c5b30`):
  - **set → T += w · out**, wrap-aware: without layer flag `0x04`, t₀ ≤ t₁ gives Δ(t₀, t₁) and otherwise Δ(t₀, end) + Δ(0, t₁); with `0x04` (reverse play), t₀ < t₁ gives Δ(t₀, 0) + Δ(end, t₁) and otherwise Δ(t₀, t₁). T is not normalised.
  - **clear → E += w · out** (Δ(t₀, t₁) only, no wrap), **X += Δ′** (unweighted), Σw += w; at the end E ×= 1/Σw when Σw > 0.
- **What bit `0x01` is: the phase-synchronised group mark** (behaviour CONFIRMED, name HIGH).
  - **Set** by `0x004c6590` (find-or-re-weight a layer), which stores flags | 1 as an assignment (`0x004c66b3`/`0x004c66c5`). It is reached from the controller update's per-frame plain-clip post (`0x004b3cd6`), the blend-tree `States` (`0x004b0619` with flags `0x11`, `0x004b0a18`), and through `0x004bce90` (which ORs 1 itself at `0x004bce93`) from the graph post step for **looping** clips (`0x004b0fc7`, gated by output-entry `+0x20` bit 0, the clip block's `loop_flag`, at `0x004b0f91`) and from the re-trigger step (`0x004b2fae`).
  - **Not set** by `0x004c6980` (layer create, through `0x004bce30`), which zeroes the flags and ORs in the caller's without adding 1 (`0x004c6a6f`); its reuse search (`0x004c6a10`) only matches layers with the bit clear. Callers: the graph post step for **non-looping** clips (`0x004b107e`, `0x004b10e1`, flags | `0x100400`, then explicit times through `0x004bd9b0`/`0x004bda10`); the play entries' first creation (`0x004b1eae`, `0x004b1fdd`, `0x004b20fc`) and the re-trigger child (`0x004b31b5`), whose layers the next controller update re-posts through `0x004c6590` — that update runs before this composer (§5.1); and `0x004c6ebb` (not read).
  - **The layer advance splits on the same bit.** `0x004c4810` (`0x004c48f7`) sends set layers to `0x004c3d20`, which advances **one shared normalised phase** (instance `+0x60`) at the instance rate `+0x68` over the weight-blended duration of the highest group present, wraps it modulo 1, and sets every member's time to phase × its own duration (layer flag `0x20000`: own clock instead). Clear layers go to `0x004c45a0`: independent clocks at layer rate `+0x18`, no advance under `0x100000` (externally timed — the non-looping graph clips), wrap under `0x02`, otherwise clamp at the end.
  - **Consequence for step 7.** `0x004be580` writes the stride-matching rate `+0x1a90` to instance `+0x68` — the rate of the synchronised phase only. Stride matching therefore rescales exactly the layers whose translation is T, which is what makes q = s / (\|T\|/Δt) a consistent feedback.
- **`Anim_Movement` — refuted as the T/E discriminator; its real role.** No post path copies animation-record `+0x2c` bit 8 into the layer flags: the graph post step reads record `+0x2c` only for `Additive` (→ `0x2000`), `No_Interpolation` (→ `0x10000`) and the override masks of bits 1, 2 and 7 (`0x004b0ef9`…`0x004b0f8b`). The record flags act one level up instead: `0x004b4fc0(controller, mask)` (5 instructions) tests controller `+0x198`, the OR of the record flag words of every playing animation (`spec-anim-runtime.md` §6.1). So step 5's "animation-controller flag `0x100`" (`0x0099d544`) is **`Anim_Movement`** (bit 8), "controller flag 4" (`0x0099d7d4`, `0x0099d928`) is **`Block_Translation`** (bit 2) and §3's "controller flag `0x10`" is **`Zero_Input`** (bit 4), with bits 1–2 masked out under an override (§6.1 there).
- **Shipped data (CONFIRMED by static read).**
  - `anim_files.xtbl` (5,968 rows): `Anim_Movement` on **220** rows. Of the 790 rows whose filename names a locomotion cycle (`walk`, `run`, `jog`, `sprint`, `strafe`), **4** carry it: the three `cop_ps_wlk*_rdy_to_run` transitions and `dist_ped_walk`. The 220 are dominated by taunts (89), reactions (17), stumbles (11), dances (6) and bumps (6): whole-body actions whose root motion replaces the intent velocity through v = R·T/Δt. They are not the locomotion set, so the old hypothesis would have put almost every cycle in E.
  - Node graphs: `locomotion_bt`'s `walk`, `run`, `jog` and `stand` are all `loop_flag` true (synchronised → T, stride-matched); `walk_run_sm`'s two turn-180s, `sprint_sm`'s four turn-180 nodes and every clip of `sprint_to_stand_sm` and `jump_sm` are false (→ E); `sprint_sm`'s `Sprint` and `weapon sprint` are true. This is the split the composer needs: cycles feed stride matching, one-shots move the body.

#### 5.4.4 Closed form for the player

For the player on foot (condition row 0, `+0xcd2` = 0, animation-controller flags `0x100` and 4 clear, `0x009573d0` false, `+0xccd` ∉ {2, 3}):
- U2 step 18 calls `0x00987360(player, position + r)`, which stores that point in **`+0xcb0`** (the move target; from `+0xccd` = 0 it also sets `+0xccd` = 1). Step 19 then sets `+0xa50` = normalise(`+0xcb0` − position) = **unit(r)**, and `+0xa60` = min(\|r\|, M) = \|r\|, which `0x00985df0` rescales to **k·\|r\|** (§5.5; k = `+0xa64`, 1.0 at construction).
- **`+0x1cec` = k·r + R·E/Δt** (not sprinting), or **k·\|r\|·(`+0x64`) + R·E/Δt** (sprinting), with r = `+0x203c` of §3.5.5. When r = 0, `+0xa60` = 0 and only R·E/Δt remains.
- So the bridge consumes U2's **smoothed** vector r, not D: the `Player_acceleration` approach of §3.5.4 step 15 is the only acceleration the player's horizontal intent receives before the controller.
- `+0xccd` = 2 or 3 (meaning not resolved, OPEN 40) switches the player to NPC-style shaping at stance speed M.

#### 5.4.5 What moves the body

| Component | Source | Applied by |
|---|---|---|
| horizontal velocity | `+0xa60` along `+0xa50` (the facing while sprinting); R·T/Δt under animation-controller flag `0x100` | `+0x1cec` → controller (§5.6) |
| stride | T against the speed, through the playback rate `+0x1a90` ∈ [0.1, 1.5] | animation only |
| vertical / extra translation | R·E/Δt of the non-locomotion layers (×1.5 in an upgraded jump) | `+0x1cec` |
| orientation | X → controller `+0x70` → `0x004b6530` → virtual slot `+0x48` | directly on the human, before physics |
| position | the proxy's position after the integrate passes | `0x0096e710` in `0x0099ebe0` (§5.7) |

**[Corrected by `spec-anim-runtime.md` §9.7 (U5), 2026-10-07.]** The "orientation" row holds only while an authored correction (`start correction`/`dest correction` trigger) window is active: `0x004b6530` then steers the orientation — and, during a start correction, the velocity — by a share of the remaining error each frame, and X feeds only that window's motion warp. **Outside every such window `0x004b6530` returns the human's orientation unchanged.** What turns a human in ordinary locomotion is accordingly still open — `spec-anim-runtime.md` §5.1 item 35.

#### 5.4.6 Synced pairs `0x0095f5b0` (194 instructions) — CONFIRMED by full listing

For a human with a partner handle `+0xdb0`/`+0xdb4` that resolves to a live human (class bit `0x20`) whose animation-controller `+0x1a4` bit `0x80` is clear: find the pair entry for this human's rig id in the table `0x02624b50` (count `0x02624b54`, `0x30`-byte entries keyed at `+0x14`); compute the partner's target = this human's position + this human's `+0x1cec`·Δt + the entry's offset rotated by this human's orientation (with both humans' `+0x1b50` height terms); then set the **partner's** `+0x1cec` = (target − partner position)/Δt, its controller `+0x1a4` |= `0x80`, its `+0xe8` |= `0x80000000`, and its orientation to this one's turned by the entry's angle (`+0x1c`, degrees). It runs after the composers, so the partner's next composer pass adds the pre-set velocity (§5.4.3 step 8). HIGH: grapples and takedowns.

### 5.5 The locomotion-state update `0x00985df0(human, sprint, stopped, flag)` (409 instructions) — CONFIRMED by full listing; resolves OPEN 19

Callers (raw scan): the move slots `0x005b4171` (§3.5.1 step 9), `0x005b4424`, `0x005b0b9c`, `0x005ad5f2`, the locked per-frame `0x005b21eb`, and the remote-player routine of OPEN 18 (`0x008a408a`).

1. Return at once while climbing or vaulting (`0x00989670`) or diving (`0x009db960`).
2. **Sprint ramp `+0x2058`** (the timer §3.5.4 step 5 blends M over): while sprinting with the request held it is re-armed every frame to 300·min(1, (`+0xd1c` − 5)/3) ms (a non-positive value is already expired), and it is armed the same way when the request ends; while it runs it keeps the request **true** for a sprinting human (a tail of at most 300 ms after release) and **false** for a non-sprinting one; it is unset when it expires.
3. **New `+0xccc`:** riding → 0; swimming → 2, or 0 when `0x009b5830`/`0x009b5670` say so; `0x009b5600` → unchanged; otherwise **3** when sprint is requested, else **2** when `+0xd1c` > 2.5, not in fine aim and not in cover (`0x009aa260`), else **1**; a run in fine aim becomes 1. While the start-to-move state `0x2f9` is active, a run start with `+0x1d96` bit `0x02` (sprint wanted, §3.7) ends it (blend 0.3); otherwise the new state is forced to 0 until that state's progress (`0x004b4600`) exceeds 0.55. When `stopped` and the human can move on the ground (`0x00941da0`), the state becomes **0**, playing the sprint stop `0x336`/`0x1d4c` (`0x00959b50`) when leaving a sprint. Leaving state 0 starts the 500 ms timer `+0xaa0`; starting a sprint sets `+0x2054` = 750 ms and, for the local player, `0x007f0140(1)` and `0x007059f0(5)`.
4. Calls `0x00950b70(human, 0.3)` (§3.4), ends state `0x1d66` (unless `0x009715d0`), calls `0x0095acb0(human, 1, 0.2, 0, 0)` while state `0x309` is active (and under `0x00956230` with a rig test), then **`0x00942110(human, −1, state, +0xa60)`**: `+0xccc` = state and **`+0xa60` = `+0xa60` × `+0xa64`** when positive.
5. Local player: stat ticks `0x00705350(5, Δt)` while running and `0x00705350(0x11, Δt)` while running or sprinting with no stamina (`0x009ff400`).

**`0x00942110(human, condition, state, speed)`** (22 instructions, **121 callers**, CONFIRMED) is the shared locomotion setter: condition −1 keeps `+0xcc8`, state `0xff` keeps `+0xccc`, and a speed > 0 is multiplied by `+0xa64`; **−1 is stored unscaled — the "animation speed" sentinel of §5.4.3 step 4**.

### 5.6 The character controller and the controller step

#### 5.6.1 Objects (CONFIRMED by RTTI walk and constructor listing; Havok side cited)

- Human `+0xcc0` = the **`character_controller`** (`spec-physics-runtime.md` §2.1 row 25, vtable `0x01154ac4`), handed out from `0x0096f1ad` by the pool `0x007a52c0` (two free rings `0x0223d370`/`0x0223d378` chosen by the kind byte `+0x16d`; constructor `0x007a4bc0` for a new one).
- Controller `+0x0c` = the **proxy**, `hk_custom_character_proxy` (row 22; built by `0x0079c400`); `+0x14` = its **shape phantom**, `hk_character_shape_phantom` (row 21; `0x0079a890`), added to the world by `0x00c18930`; `+0xa8` = the **`MyCharacterListener`** (row 23, vtable `0x01154a98`), registered on the proxy by `0x00c84680`, with the owner's handle at `+0x20`/`+0x24`; `+0x70` = X (§5.4.3 step 3); `+0x08` = state byte (bit `0x04` supported; `0x80` too steep; `0x02`, `0x08`, `0x10`, `0x20`, `0x40` maintained by `0x007a5b10`); `+0x150` bit `0x02`; timers `+0x154`, `+0x158`, `+0x15c`; `+0x160`; `+0x170` = 44.704 (100 mph; HIGH: a speed cap).
- Thin accessors (CONFIRMED by listing): `0x0079efb0` velocity; `0x0079efe0` set velocity; `0x0079ef60` position (the proxy position mapped through the phantom's offset, `0x00758670`); `0x0079e670` supported (state bit `0x04`); `0x007a4a40` warp.

#### 5.6.2 `0x0099e7e0(Δt)` (25 instructions) — CONFIRMED

If Δt ≠ 0: n = `0x0099ae30(records)` into a `0x1800`-byte stack buffer (256 records of `0x18`; no bound check — HIGH that the human pool keeps n below it); `0x0099b430(records, n, Δt)`; `0x0099e2a0(records, n)`.

#### 5.6.3 Gather `0x0099ae30` (423 instructions) — CONFIRMED by full listing

Walks `+0x1e4`, the list of all humans. A human gets a record when `0x00999770` holds — it has a controller, is usable, is not dead (unless `+0xe8` bit `0x200000` or `+0xe0` bit `0x400000`), is not ragdolled (`0x009a1dc0`), has `+0x33` bit `0x08`, fails `0x00ad3820`, and passes several cutscene, UI and network checks — and the global `0x00722000` test does not exclude it. Record (`0x18` bytes): `+0x00` human; `+0x04` **velocity** (3 floats); `+0x10` a float, always 0; `+0x14` flags:

| Flag | Meaning |
|---|---|
| `0x01` | step this human (set on every path that reaches the controller; a skipped record keeps 0) |
| `0x02` | controller `+0x150` bit `0x02` clear (`0x00942260`), or `0x009b9210` with `+0xccd` = 2 |
| `0x04` | the animation drives the vertical (animation-controller `+0x1a4` bit `0x80`, §5.4.3 step 3) |
| `0x08` | animation-controller `+0x1a4` bits `0x800` and `0x100` both set |
| `0x10` | LOD keep (below) |
| `0x20` | ground-probe eligible (`0x0099ada0`) |
| `0x40` | `+0xec` bit `0x100`, consumed: reset the proxy's two contact caches (`0x0079b9a0`) |

In order: (a) an NPC with \|`+0x1cec`\|² < 1e‑5 has it zeroed. (b) **NPC LOD:** flag `0x10` is cleared — the NPC is **not stepped this frame** — when its `+0x1da5` bit `0x80` is set, it fails the visibility test `0x0091f310(·, 2)` (HYPOTHESIS: on screen), it is more than 50 units from every player (`0x008902a0`: `+0xac4`, or the distance to the second player), it has no partner and `+0x1c9c` bit `0x2000` is clear. A second branch for slow NPCs (\|`+0x1cec`\|² < 0.01, `+0xccd` ≠ 6) re-arms `+0xaa8` to a random 1–2 s and applies the same skip at 100 units — but only while `+0xaa8` has **not** expired, and the constructor arms it at 0 ms with no other writer (displacement sweep), so as read **this second branch never engages** (HIGH; OPEN 41). (c) `+0xec` bit `0x08` (a warp request, consumed): `0x007a4a40` moves the proxy to `+0x40` (or the attachment `+0xb30`'s point). (d) **Swimming:** `0x009b5c50(human, &+0x1cec)` adjusts it; velocity = `+0x1cec`, flag `0x01` only. (e) **Airborne** (animation-controller `+0x1a4` bit `0x80` clear, `+0xec` bit `0x01` clear, no cutscene, not supported): velocity = `+0x1cec` — except for a **player** not in free fall (`0x00994f60`) and with `+0x1c9c` bit `0x2000` clear: **velocity = `+0x1cec` × clamp(\|A\| + \|B\|, 0, 1)**, A and B being the published walk axes `0x0140e6c0`/`0x0140e6bc` (§3.5.2 step 7) — stick-scaled air control, applied to all three components. (f) Otherwise (grounded or animation-driven): velocity = `+0x1cec`, flags per the table, and `+0xec` bit `0x01` is cleared.

#### 5.6.4 Step `0x0099b430(records, n, Δt)` (285 instructions) — CONFIRMED by full listing

1. For each record with flag `0x01`: (flag `0x40`: reset the contact caches); **`0x007a5b10(human, &velocity, 0, Δt, flag 0x02, flag 0x04, flag 0x08)`** on the controller (§5.6.5); controller `+0x09` |= 2; **`0x0079f5f0(Δt)`**, which grows the phantom's AABB by \|proxy velocity\|·Δt plus two proxy margins (`+0x88`, `+0x8c`) and sets it (`0x00c30150`).
2. The stepped humans are collected into a list; for each, `0x0079e6c0(Δt)` → `0x0079bb00`: the proxy snapshots the phantom transform, sets its remaining time `+0xc0` = Δt and starts a cast.
3. **Lock-step passes** over two ping-pong lists, pass p = 0, 1, …: join the previous casts (`0x0079af00` waits on the scheduler group `0x0223ac40`, which `0x0079b470` creates when it submits them); every listed proxy advances one cast-and-solve over its remaining time (`0x007a1c40` → `0x0079dd50(&{Δt, 1/Δt}, world + 0x10)`); join again; every proxy asks `0x007a2420` → `0x0079ddc0(last = (p = 9))` whether time remains (> 1.19e‑7) and if so joins the next list. **At most 10 passes per frame**; no pass reads the human.
4. Finish: `0x0079e6e0` → `0x0079b560` for each, then `0x0079e6f0` → `0x0079bbb0` (commit the proxy's new position into the phantom; broadphase update `0x00c838d0`) and `0x007a1c90(human)` (support data → human `+0x1a74`, `+0x1a84`, `+0x1a88`/`+0x1a8c`).

**The pass functions, read in full (closes OPEN 36, 2026-10-07, unit W-U2).** The custom proxy's own cast-and-solve machinery composes into one lock-step cycle, each stage a genuinely separate function: **seed** (`0x0079bb00`: snapshot phantom transform → proxy `+0xd0..+0x100`; set remaining time `+0xc0`=Δt; clear iteration count `+0x1a4`; call snapshot+submit) → **snapshot+submit** (`0x0079b5b0`: writes the snapshot into the phantom's live transform, resets both cast sub-objects, submits a 9-field job via the job submitter if the phantom's counter `+0x164` is nonzero) → **submit** (`0x0079b470`: lazily creates the shared job-group object, allocates the next 0x40-byte ring-buffer slot (mod 150), submits as a **CLASS-3 job** — the identical job class and submit/wait API `spec-physics-runtime.md` §4.4 already documents for the main per-frame step's own 2 worker jobs) → **advance** (`0x0079dd50`: calls the real cast-and-solve `0x0079c5f0`; if converged, done; else predicted next position, resubmit) → **cast-and-solve** (`0x0079c5f0`: Havok narrowphase on both cast sub-objects; the integrate/solve call through the proxy's own primary-vtable slot `+0x14` — this IS `0x0079de40` itself, §5.6.4a below, not a separate Havok method; the phantom-interface listener fan-out called directly here; caches the world's gravity vector `world+0x10` → proxy `+0x180` and a velocity snapshot → `+0x190` for the finish step; returns "converged within epsilon" as a boolean) → **ask: time remains?** (`0x0079ddc0`: calls `0x0079d7c0` to resolve one contact + time bookkeeping; if remaining time ≤1.19e-7 or this was pass 10, finish both cast sub-objects and stop; else resubmit) → loop while time remains (≤10 passes) → **finish step 1** (`0x0079b560`: walks a separate provisional touch-list, applies a physical impulse + wakes approaching dynamic entities via `0x0079afe0`) → **finish step 2** (`0x0079bbb0`: refreshes the phantom's AABB, tears down and re-populates the two "currently touching" lists, temporarily swaps in the cached velocity snapshot for the manifold commit `0xc838d0` — Havok-facing half already specified by W-U1 §7.6 — then restores the real velocity). `0x00702760` (→ `0x005aa170`, a control-band timer tick) also runs inside this routine and is unrelated.

**5.6.4a — `0x0079de40`, the manifold update (reached as a game-introduced virtual, closing `spec-physics-runtime.md` §2.1 row 22's own open question of which vtable/slot and who calls it).** The proxy constructor `0x0079c400` installs three vtables on the proxy: `+0x0`=`0x01154a18` (primary), `+0x8`=`0x011549fc`, `+0xc`=`0x011549e4`. The primary vtable has exactly 6 slots — a destructor plus 4 Havok-interface methods, then, at `+0x14` (past the Havok interface's own 5-slot length), `0x0079de40`. `0x0079c5f0` reads this slot and calls through it with 6 arguments matching `0x0079de40`'s own parameters exactly. **Role:** a contact-identity reconciliation pass — matches each cast sub-step's fresh hit records against a reference/previous set (via a "find closest match" helper, `0x0079b170`) so per-contact tracking state stays keyed to the same physical contact across sub-steps, firing a "collidable changed" notification (`0xc83820`) plus a register/re-attach call (`0xc837e0`, the fourth member of the `0xc83720`/`0xc83760`/`0xc837a0` listener cluster) only when the matched contact's touched object actually changed.

**Thread safety (resolves `spec-physics-runtime.md` §11 OPEN item 22 beyond its own "likely but unverified" hedge, and the thread-safety half of OPEN 36).** `MyCharacterListener`'s three callbacks (§5.8) and the phantom-listener fan-out both fire only from inside `0x0079c5f0`/`0x0079d7c0`/`0x0079de40` — all three reached only as the worker body of the CLASS-3 job `0x0079b470` submits. **Class-3 is the game's persistent, multi-worker thread pool** (bring-up submits one class-3 job per active scheduler worker purely to install per-thread Havok memory, per §4.4) — **so both listener mechanisms run on whichever worker thread the pool dispatches that job to, not guaranteed the main thread, and not serialized against other proxies' jobs stepped in the same pass.** This is exactly why §5.8's own "the queue adders take no lock (plain increments)" is a real, load-bearing design choice in the original engine — it works only because each queue slot is ever appended by just the one human/proxy pair that owns it; **a straight reimplementation fanning these jobs across real OS threads must provide its own synchronization** if two humans' proxies can touch the same object in the same pass.

#### 5.6.5 Controller motion `0x007a5b10` (1467 instructions) — CONFIRMED by full listing (closes OPEN 35, 2026-10-07, unit W-U2)

`thiscall` on the controller, `ret 0x1c`: `(this=controller, human, &velocity, heightArg, Δt, b1, b2, b3)`. `heightArg` (the function's own 3rd argument) is a **jump-height parameter**, read only by the jump-launch formula below — zero on the normal per-frame call (§5.6.4 step 1), non-zero at the function's two other callers (end of this subsection). It clears state bit `0x80` and float field `+0x10` (two distinct resets — `+0x10` is a 4-byte float, not the state byte's own bit `0x10`), and reads the proxy velocity and position (`0x00c835b0`, `0x00c83570`).

**Support resolution.** Unless `b1`, or both `b2` and `b3`, are set, it skips the support query and treats the human as supported on an up normal `(0,1,0)` with support-state value `2` ("normally supported"); otherwise `0x0079d0b0` (the proxy's support check) returns the support state, surface normal and surface velocity. **A new per-human-kind capability gate** (byte `human+0x34` indexes a pointer table at `0x02cc9900`; bit `0x02` of the pointed-to record's own `+0xa` byte) gates BOTH of the next two behaviors — not every human kind gets slope removal or the steep-slope-timer override below. Gated on that: a surface whose normal · up is below 0.7 (`0x012a2ff0` = 0.699999988) and state bits `0x10`/`0x40` are both clear has the into-slope part of the desired velocity removed by proper vector projection via the cosine-between helper `0x00434f10` (written back into the record's copy, only when moving INTO the slope); independently, below 0.55 (`0x01154ab0`) sets state bit `0x80` ("too steep") **and forces the support-state variable itself to 0** (airborne for the rest of this call, not just a flag).

**Two independent ways to become "unsupported" that the raw query did not report**, both gated on a "coyote" local (built every call, unconditionally copied into `controller+0x160`) exceeding `1.5` and only while still reporting `state != 0`: (1) a **departure-speed override** — a combined scalar from the cached proxy velocity and the surface-derived vectors, compared against `0.05` (as a double); exceeding it forces support to 0 (moving away from the surface fast enough overrides a "supported" result); (2) a **steep-slope-timer override** — if `normalY < 0.7` and `controller+0x160` (the same coyote-local-derived field) is any positive value, support is also forced to 0. **`controller+0x160` is therefore a monotonically-available "how long has the surface been steep" counter**, extended toward its own previous value plus a fixed margin (`5.0`, units unresolved — plausibly a frame count) whenever the same per-kind gate and state conditions hold, including a check against `heightArg` itself.

**State byte `controller+0x08`, full catalog:** `0x01` gravity-integration suppressed this call (set upstream, not in this function); `0x02` gates the slope-removal re-check/gravity gate, cleared twice in the step-assist tail below; `0x04` **supported** (cleared unconditionally by the jump-launch block); `0x08` **"fall danger"** (consumed by §5.6.6's knock-down check), set under the step-assist ground probe; `0x10` set when the step-down raycast fails; `0x20` set when the step-up raycast fails outright or the global toggle `0x012fc74f` is false; `0x40` set when the step-up raycast succeeds and horizontal speed exceeds ≈1.1 units/s; `0x80` too steep (cleared unconditionally at entry).

**Step-up/step-down/ledge-assist** (independent of the above): a first raycast (`0x0075d7e0`, length 90) that, on failure or if the global toggle `0x012fc74f` is false, clears bit `0x02` and sets bit `0x20`, skipping the rest; on success, tests the hit normal against `0.72`/`0.6` and sets bit `0x40` when horizontal speed² exceeds `1.2`; a second raycast (`0x0075c880`, new address, margin `0.35`) tests a step-up height limit of ≈0.52 units against a squared-speed gate of ≈5.1 units/s; a third raycast (`0x0075d7e0` again, the step-down probe) sets bit `0x10` on failure (accumulating a small vertical correction using the GLOBAL frame Δt `0x0132a0ac`, not this call's own Δt) or clears bit `0x02` on success. A follow-up gate (run when the block didn't just set bit `0x20`, and support confidence is below `0.8`) calls `0x00d9f9d0(human)` ("is climbing/vaulting/otherwise exempt") and, if false, runs the fall-danger ground probe: `0xc8aa60` (a Havok range-A probe, two output buffers) with bit `0x08` cleared first and potentially re-set by a follow-up vector computation when support confidence is below `0.7` or a cosine-between test fails. A three-timer chain (`+0x154` 500ms, `+0x15c` a per-human dynamic duration via `0x00d9e210`, `+0x158` 200ms once `+0x15c` expires) gates the fall-danger re-arming and a Y-velocity soft-damping step (subtracting a runtime-tunable rate, `0x0223d1d0`/`0x0223d1d4`, from vertical velocity while falling gently between 0 and 9.0 units/s).

**Unreconciled discrepancy flagged 2026-10-08, not silently resolved either way:** an independent fresh disassembly of this exact first-raycast call site (`0x007a6475`) reports the hit-normal test as comparing against `0x012a2ff0` (0.7) — **the same shared constant** §2 above uses for slope removal, not two separate `0.72`/`0.6` values as this paragraph's own prior language states. Whoever next touches this section should re-disassemble `0x007a6475` directly to settle which framing is correct (a single shared 0.7 constant vs. two distinct 0.72/0.6 constants) before relying on either number.

**The support query `0x0079d0b0`, opened in full 2026-10-08 (previously cited by name only).** `(this=proxy, out_struct*)`: output struct `+0x00` = support state (`2` if ≥1 qualifying manifold point, else `0`), `+0x10` = averaged surface normal, `+0x20` = averaged surface "velocity," `+0x30` zeroed/unused. Source data: the proxy's own contact manifold, a `hkArray`-shaped buffer at proxy `+0x10`(data)/`+0x14`(count), stride `0x60`, capped at 32 entries considered — this is the manifold the §5.6.4 continuous shape-cast loop fills every frame, directly tying that mechanism to this read. Per manifold entry (before averaging): a virtual call through proxy vtable slot `+0x10` gathers a per-contact velocity sample into a parallel `0x40`-stride buffer (HIGH CONFIDENCE: sampling a touched body's own local velocity at the contact point — this is what lets a moving platform's motion show up in the averaged output). **Qualifying-point filter: a manifold point counts toward the support average only if `dot(up, contactPosition) < −0.08`** (`0x01154a30`) — keeps side/ceiling/wall contacts out of the ground-support average. Normal-averaging has a zero-length guard (`0x0125e270` = 0.0) before a Newton-Raphson `rsqrt`-refined normalize. **Net: this is a textbook multi-contact support algorithm already generic over arbitrary contact geometry — slopes/steps/stairs all "just work" once real collidable shapes are registered; nothing about it is terrain- or plane-specific.** `0x0079d7c0` sits immediately after `0x0079d0b0` in the image (no padding beyond 4 `int3` bytes) and matches the already-documented "ask: time remains?" resolve-one-contact description, §5.6.4 — confirms the two sit back-to-back, not independently opened further.

**Correction flag, not resolved:** the forward-probe's "length 90" field (`0x007a6475`) is written with a plain integer `mov`, not an SSE float store like every other position/direction field in the same struct — mild evidence against "length" being a literal world-space ray length in the same units as the rest of the struct; may be a flags/mode/iteration-count integer instead. Flagged OPEN, not silently carried forward as settled.

Full detail, including the forward-probe's `−2.2` offset constant (axis/meaning unresolved) and what the open "which file supplies the ground collision shape" question bounds (separate from and not blocking any of the above): `scratchpad/havok_character_proxy_ground_following_2026-10-08.md`.

**Follow-up pass, 2026-10-10: the open-world ground-collision-source question is narrowed, not closed.**

**CONFIRMED, cited from `spec-physics-format.md` §4.4.6(g) (closed 2026-10-08, re-read not re-derived this pass):** `.clmesh_pc`'s parsed collision-hull data does reach a real Havok shape — RTTI-resolved `hkpExtendedMeshShape` (vtables `0x01246834`/`0x01246814`) wrapped via `hkpSingleShapeContainer` (`0x01154174`) into a real `hkpMoppBvTreeShape` (`0x0124484c`), built from the file's own vertex/`u16`-index arrays plus a pre-baked, on-disk-validated ("MOPP" magic) Havok MOPP tree — a direct triangle-mesh-to-MOPP wrap, no primitive decomposition.

**This path IS wired into ordinary open-world zone streaming, not just bundled interiors/missions** — cited from `spec-world-streaming.md` §10.5/§10.6: every ordinary city tile's sub-box registration pass registers each overlapping placed object's own `.clmesh_pc` into a `"%sh%d"` container; population-wide, 41,689 `Level Mesh` entries sit in ordinary-tile containers alone. So `.clmesh_pc`/Level-Mesh collision is a routine part of ordinary street-tile streaming, not an interior-only mechanism.

**However — checked directly against real open-world street tiles, already documented `spec-zone-data-format.md` §13.2 (2026-10-08): `.clmesh_pc`'s real content on ordinary tiles is street furniture (curbs, barriers, posts), not the paved street surface itself** (142 distinct props across 7 tiles' fine cells, zero road-named, none tile-scale). **The visible street pavement is a different, already-identified source: `.czn_pc`'s own `0x2237` ("WE geom") section** — the same spec section (2026-10-08) decoded real vertex positions from 374 real road/sidewalk-textured tiles and found 359/374 (96%) genuinely flat, tile-scale slabs (e.g. a real 31m×140m×0.55m slab). This supersedes the older "decal/path-deform overlay, not base ground" framing of `0x2237` in `spec-terrain-format.md` (dated 2026-10-07, now stale — flagged there).

**So the real open item is narrower than before: what Havok shape, if any, backs `0x2237`/`.gzn_pc`'s own pavement geometry for collision — not traced this pass.** Two bounds on the search space, both new this pass: (1) precedent exists in the same file format for a `.czn_pc` content kind embedding a raw Havok shape directly with no `.clmesh_pc` involved at all — `0x2250` ("Parking") is a thin wrapper around a real embedded Havok packfile (`hkpListShape` of `hkpBoxShape`s), `spec-zone-data-format.md` §13.4, CONFIRMED by RTTI — so the pattern is established, just not yet shown for `0x2237`. (2) The other live candidate, `0x2244` (Heightmap)'s already-documented runtime query chain, was traced this pass by direct xref/decompile to its real callers: the public "get height at world position" wrapper is `0x00b41670` (`if !0x00b412c0(&cell, pos) return -5.0; else return 0x00b41000(&cell)` — the `-5.0` failure sentinel exactly matches the already-documented population floor of real decoded height values, strong confirmation this is the live public entry point). **Independently re-verified this pass via the Ghidra query server: `0x00b41670` has 18 callers across 17 functions, and `0x00b412c0` has 9 callers across 6 functions — none fall inside the Havok character-proxy/controller address range (`0x0079xxxx`-`0x007axxxx`: the support query `0x0079d0b0`, controller motion `0x007a5b10`, the shape-cast wrappers).** Two opened by decompile this pass look like gameplay/effect consumers, not Havok narrowphase code: `0x00669bb0` (a nearby-offset height sampler keyed off the current player's own position, footstep/decal/effect-placement-shaped) and `0x005ebac0` (gates on the same per-character-kind capability table/bit `spec-player-runtime.md` §5.6.5 already ties to the controller's own slope-removal gate — a real link to the character-kind system, but not inside Havok cast code). **Net: no vertex/index-buffer construction and no Havok shape registration was found downstream of `0x2244`'s height data in this pass's budget — the remaining ~15 of 17 `0x00b41670` callers were not individually opened, so this is not an exhaustive negative, but it leans toward "`0x2244` is a scalar query API for gameplay/camera/effects," not a Havok collision feed.**

**Bottom line:** the character proxy's support query itself needs no further work — it genuinely contacts whatever real Havok shape is registered in the broadphase. Which file registers that shape for bare open-world street pavement specifically remains OPEN, now narrower: `.clmesh_pc`/`.glmesh_pc` is ruled out (confirmed furniture/props only), `0x2244`'s own confirmed callers are ruled out (gameplay consumers, not Havok code, on the two opened), and the one remaining structurally-plausible path — `0x2237`'s own pavement content being wrapped into a Havok shape the same way `0x2250`/Parking's boxes are — was not traced this pass (its documented handler `0x00864c60` shows only material/texture-pool resolution in its own body, a clean negative for Havok content *inside that specific handler*, but its callers/siblings in the zone-dispatch chain were not individually checked for a separate collision-registration step). **Recommended next step: an xref census on the Havok broadphase-add call implicit in `0x00d54300`'s MOPP composite construction, checked against whether the zone-instantiate dispatcher (`0x00866580`/`0x008652d0`, `spec-zone-data-format.md` §11.1) or `0x2237`'s own handler chain ever reaches it directly, independent of the `.clmesh_pc`/Level-Mesh path.** Full detail: this pass's own hand-off, folded in here; no separate scratch file.

**RESOLVED, 2026-10-11, following exactly the recommended next step above (SPEC TEAM, agent
`a73e5e44581178516`, independently re-verified by direct Ghidra query before splicing): `0x2237`'s
own handler (`0x00864c60`) directly builds its own Havok collision shape from the SAME record's
data, with no dependency on `.clmesh_pc` at all.** Two call sites inside `0x00864c60` (one
outside a mesh-reference loop, one inside a second per-sub-item loop — a given `0x2237` record can
register more than one shape) run the identical 3-step chain:

1. **`0x007a79f0`** parses a raw blob embedded in the `0x2237` record (align16, `u32 blobLen`,
   align16, `blobLen` opaque bytes, align4, two 12-byte vec3s — an AABB min/max pair). **Confirmed
   by direct xref query, independently of the agent's own report: `0x007a79f0` has exactly 2
   callers in the entire binary — `0x00865219` inside `0x00864c60` (this chain) and `0x00749c14`
   inside `0x00749ba0`, the already-documented `.clmesh_pc` collision-hull parser (§4.4.6) — the
   identical blob-parse front end feeds both paths.**
2. **`0x007a7a70`** builds the actual shape: calls `0x00d56010`, which constructs an object with
   vtables `0x01246834`/`0x01246814` — **re-verified by direct decompile: the constructor writes
   the address of the data at `0x01246834` into the new object's own vtable slot and the address
   of the data at `0x01246814` into its offset-`0x10` slot — the EXACT same `hkpExtendedMeshShape`
   vtables already RTTI-confirmed for `.clmesh_pc`'s own collision shape** (cited above in this
   section). Then validates the record's own embedded MOPP blob (`0x0077a5b0`, checks the literal
   `"MOPP"` magic) and, if valid, calls `0x00d54300` — the MOPP-wrapper constructor (vtables
   `0x0124484c`/`hkpMoppBvTreeShape`, `0x01154174`/`hkpSingleShapeContainer`, storing the mesh
   shape at `+0x34`). **Re-verified by direct xref query: `0x00d54300` has exactly 4 callers in
   the whole binary — `0x00761ed0`, `0x00792580`, `0x007a7a70` (this chain), `0x008658c0` — a
   small, fully-enumerated set, confirming this is a narrow, identifiable MOPP-wrap mechanism, not
   a generic ubiquitous helper.**
3. **`0x007a78a0`** wraps the resulting shape into a generic property-bearing object via the
   shared base constructor `0x00757c00` with classId `0xc006c`, then sets property id `7` via
   `0x00c21b20` — **re-verified by direct decompile: a local is set to the literal classId
   `0xc006c` and passed to the base constructor, then the property setter is called with literal
   property id `7` — exact match.** This is the IDENTICAL generic
   physics-shape-attached-object wrapper (classId, base ctor, property-id-7 mechanism)
   `spec-zone-data-format.md` §13.4 already documents for `0x2250`/Parking's own `hkpBoxShape` —
   not merely analogous, the same shared code (`0x007a78a0`'s own callee list includes
   `0x00c16840`, a world/registration-shaped call whose caller list also includes `0x00862bcb`
   inside `0x00862ae0`, §13.4's own cited Parking-shape-construction delegate — the same
   registration call point is shared between `0x2237`'s new shape and Parking's already-confirmed
   one).

**Net: street pavement's Havok collision shape is the SAME `.czn_pc` record (`0x2237`) that
already supplies its visible geometry, independently self-contained — no `.clmesh_pc` dependency.
This closes the open item.** One gap, honestly flagged, not closed this pass: this confirms the
CODE PATH exists and runs whenever the record's MOPP-blob/mesh-blob fields are populated; it does
NOT verify, against real shipped file bytes, that ordinary flat-slab pavement records (as opposed
to some other `0x2237` sub-case, e.g. curved road meshes) actually populate those fields
non-trivially. **Closed cheaply, same day, no agent, against real shipped bytes:** reused the
already-landed extraction pipeline (`czn_u2_replay.collect_pairs`/`walk_top_level`,
unmodified) to locate the literal `"MOPP"` magic inside the real `0x2237` record span of two
known flat-pavement tiles — `sr3_city~s0916` (484,684-byte record; MOPP magic found at
record-relative offset 247,156, with a substantial 237,503-byte preceding length field,
consistent with a real populated mesh/MOPP-adjacent blob, not a near-empty stub) and
`sr3_city~s1312` (the already-published `roads_crosswalk_01_d_co.tga` clean example;
216,540-byte record; MOPP magic at offset 133,540, preceding length field 82,974 bytes — and
identically reproduced across all 4 of its `^burn`/`^daedelus`/`^lockdown` mission-variant
siblings). By contrast, the much smaller `~al` ("awld_compact"/Level-Always-Loaded) variants of
the SAME tiles (92-10,812 bytes) carry NO `"MOPP"` magic at all — consistent with that sparse
tier not carrying collision-bearing geometry, a sensible negative control. **This confirms the
MOPP/mesh-blob fields are genuinely populated, at real tens-to-hundreds-of-KB scale, for real
ordinary flat-pavement records — not merely a theoretically-reachable code path.** Not
independently re-derived from the exact `0x0077a3e0` byte layout (align16/u32-len/align16/data)
field-by-field — the preceding-length-field reading is HIGH CONFIDENCE from proximity and
plausible magnitude, not a byte-level walk of every intervening field. Script:
`scratchpad/havok_collision_pavement/dump_mopp_fields_2026-10-11.py` (sizes/offsets only, no
bulk file content printed, per this project's own convention). Also not pursued this
pass: the heightmap (`0x2244`) exhaustive-negative caller sweep above remains at its prior
coverage (2 of 18 / partial of 9 opened) — unchanged, still technically not fully exhaustive, but
now moot for THIS open item since a positive mechanism has been found elsewhere. **[CONFIRMED —
RTTI-level vtable match for the shape classes (`0x01246834`/`0x01246814`,
`0x0124484c`/`0x01154174`), re-verified by independent direct query (not taken on the agent's own
word) for the two load-bearing uniqueness claims (`0x007a79f0`'s 2-caller xref,
`0x00d54300`'s 4-caller xref) and the classId/property-id decompile match. HIGH CONFIDENCE not
fully CONFIRMED: `0x00c16840` as genuine broadphase/world registration (its body was only
partially read, not fully decompiled, by the dispatched pass). CONFIRMED, same day: real-file-
bytes occupancy for ordinary flat pavement records (`sr3_city~s0916`/`sr3_city~s1312`, see
above) — no longer OPEN.]** Full detail:
`scratchpad/havok_collision_pavement/` (`func_0x00864c60.txt`, `func_0x007a7a70.txt`,
`func_0x00c16840.txt`, `func_0x008658c0.txt`, `func_0x00865a20.txt`), independently re-verified in
`scratchpad/verify_havok_pavement/` (`xref_0x007a79f0.txt`, `xref_0x00d54300.txt`,
`func_0x00d56010.txt`, `func_0x007a78a0.txt`).

**Gravity integration (the actual formula):**
```
if bit 0x01 clear AND state==0 (unsupported) AND (b2==0 OR b3!=0) AND bit 0x02 clear:
    gravity = world+0x10                        ; the world's gravity vector (−9.8 at construction,
                                                  ; spec-physics-runtime.md §4.1 step 5)
    sinkSpeed = −dot(velocity, up)               ; how fast already falling
    if sinkSpeed > 0:
        ratio = sinkSpeed / controller+0x170      ; the speed cap (44.704, 100mph, §5.6.1)
        gravity *= (1.0 − ratio·ratio)            ; quadratic terminal-velocity falloff
    velocity += gravity * Δt
```
`controller+0x170` is therefore the terminal-fall-speed reference gravity scales itself against, not a hard post-hoc clamp — gravity's contribution asymptotically weakens as fall speed nears the cap, never clipped afterward.

**Jump launch:** gated on `heightArg > 0.0`. Formula: `gravityDir = normalize(world+0x10)` (Newton-Raphson rsqrt-refined); `speed = sqrt(2.0 · |gravity| · |heightArg|)` (the classic v=√(2gh) formula); `velocity = −gravityDir · speed` (launched opposite gravity). A small correction softens the launch by up to 2.0 units when already falling faster than −3.0 units/s. **Bit `0x04` (supported) is unconditionally cleared the instant a jump launch is applied.** The function **ends by setting the proxy's linear velocity** (`0x00c835c0`, called at `0x007a73a5`), after a pause/scripted clamp (`0x00530340`: any computed upward Y-velocity is zeroed while paused/scripted) and a surface-material-change notification (gated on the separate byte `controller+0x09` bit `0x02`). It reads no intent field of the human.

**Its two other callers, `0x007a7413` and `0x007a750e`, are internal continuation labels inside a larger shared caller (no prologue of their own), not independent functions** — both are jump triggers, both passing a non-zero `heightArg` (one computed as `1.0/x` off the human's own cast sub-object; the other a fixed `≈0.016667`, suspiciously one 60Hz frame), distinct from the per-frame call's `heightArg = 0`. Both also call `0x007a1c90` (the support-data write-back, §5.6.4 step 4) afterward.

#### 5.6.6 Write-back `0x0099e2a0(records, n)` (336 instructions) — CONFIRMED by full listing

For **every** record (flag `0x01` is not tested): clear animation-controller `+0x1a4` bit `0x80`. **Landing:** while airborne (`+0xe8` bit `0x80`) — either supported, or airborne for more than 1 s (timer `+0x1aac`, restarted on every grounded frame) with fall speed `+0x1aa4` ≤ 0.01, `0x00942260` true, `+0x1c9c` bit `0x2000` clear and `0x0063c830` false — and state `0x1d50` inactive: clear `+0xe8` bit `0x80` and land (`0x00986b80`); then `+0x1aa4` = −`+0xd08`. **`+0xd04`/`+0xd08`/`+0xd0c` = the proxy velocity; `+0xd1c` = its length.** The 300 ms timer `+0x1ab0` is re-armed on walkable ground (normal y `+0x1a64` ≥ 0.715, or ≥ 0.7 when `+0xa60` = 0) and while the controller flags the surface too steep. Airborne, too steep and \|vy\| < 0.1 for 400 ms (`+0x1ab4`) → knock-down (`0x009a48e0`). **State bit `0x08` with a vertical speed below −12.5 → knock-down.** For ground-probe-eligible, LOD-kept, unsupported humans, after 200 ms (`+0x1aa8`) or with state bit `0x20`: a ray from 0.5 above to 0.6 below the position (`0x0075d7e0`) that hits nothing, with no vehicle handle, starts a fall (`+0xe8` |= `0x80`, `+0x1aa4` = 0, `0x0098aa10(human, 1, 1, 1)`).

### 5.7 After the step: `0x00998850` and `0x0099ebe0`

- **`0x00998850(Δt)`** (34 instructions) runs `0x00997b50` (172, CONFIRMED by full listing) on every `+0x1e4` human: `+0xdc` bit `0x4000` sets the proxy velocity to 5 units/s along its current direction and knocks the human down (local authority); for the local player not riding, `0x0098a3f0` and the `0x00710cb0` checks (not read).
- **`0x0099ebe0`** (1482, CONFIRMED by full listing). First loop over the `+0x1e4` humans with `+0xdc` bit `0x100000` and `+0x33` bit `0x08`: ragdoll pose sync `0x009984a0`; **`0x0096e710`: when the proxy position differs from `+0x40` by more than 0.0008 on any axis, set the position through the virtual slot `+0x44` and refresh the world transform (`0x0094bc80`, `0x0094c9e0` → `0x0045e370`, `0x00997180`)**; `0x00999020`; a transform refresh. Second loop: drowning and the scripted shark attack (`Shark`, `0x00594460`), cover exits (`0x009aa360`, `0x009aaa20`), death bookkeeping (`0x0099a630`, `0x0099a740`), the dive end `0x0099cda0`, bounds to the controller (`0x0079f080` from `+0x100`/`+0x110`), held-item placement (`+0x1b78`), out-of-world kills, the cutscene object `+0xd34`, and a moving-support check (`+0x1c9c` bit `0x2000`: a ray through `0x0075d7e0` and `+0x1c60`); then **the contact flush `0x007a5760`**. No intent field is read.

### 5.8 Havok → game: the listener and the contact replay

- **The Havok-callable side** is `MyCharacterListener`'s three overrides (`spec-physics-runtime.md` §2.1 row 23; the slot names are Havok 2010.2's public `hkpCharacterProxyListener` API, HIGH from slot order): `0x007a2960` (`processConstraintsCallback`, 578: per-contact class tests of the touched object and a swimming test; not read further), `0x0079fb80` (`characterInteractionCallback`, 24: queues into `0x0223ff70` through `0x0079f970`) and `0x007a45f0` (`objectInteractionCallback`, 288: queues into `0x0223d3b0` through `0x007a2170`; for the local player's own proxy it also pushes the touched object, through Havok calls and `0x00a89860` in the vehicle band, not read). Slot 0, `0x0079f8d0`, is the scalar deleting destructor. They run inside the passes of §5.6.4 and **only queue**; the queue adders take no lock (plain increments). Duplicate (human, object) pairs are merged by running average, so a flush sees at most one event per pair per frame; each queue holds at most 100.
- **Queue records.** `0x0223d3b0` (`0x70`): `+0x00` the listener owner's handle (the human); `+0x08` the proxy; `+0x10` the touched body's object handle (`0x00758780`); `+0x18` the body (`0x00757e30`); `+0x20`…`+0x53` the interaction event (position, normal, four scalars, body); `+0x60` merge count. `0x0223ff70` (`0x50`): two humans' handles, two vectors, count `+0x40`.
- **Flush `0x007a5760`.** Queue 1 entries whose two handles resolve to live objects with class bit 3 (`0x00853b30`, i.e. kind-row `+6` bit `0x08`) and whose body, if any, passes `0x00757cc0`, dispatched on the touched object's class: bit `0x0a` or 5 → `0x007a37f0` (483); bit `0x20` (human) → `0x007a2470` (345); **bit 7 (vehicle; kind-row `+6` bit `0x80`, as `spec-lua-api-behaviour.md` names it) → `0x0079e900` → `0x00961d70`**; bit `0x1e` → `0x007a3fa0` (392); bit 8 → nothing; bit 6 → `0x007a3640` (134). Queue 2 (both humans, class bit `0x20`) → `0x00962c20(A, B)` (474). These handlers apply damage (`0x0096fa20`), knock-downs (`0x009a48e0`, `0x00966d20`) and ragdoll impulses (`0x00780040`, `0x00781b80`); their individual roles beyond `0x00961d70` are OPEN 42. Both counts are then zeroed.
- **`0x00961d70(human lo, hi, vehicle lo, hi, &position, &normal, scalar)`** (980, CONFIRMED by full listing; the seventh argument, record `+0x48`, is never read). It returns early for a dead human, for the human's own vehicle (unless `0x00af1860`), for a surfing human on that vehicle (`0x006c4990`/`0x006c48f0`), or under `0x0052a230`/`0x0052a5c0`. Direction = normalise(human − vehicle position). **Upgraded-sprint ram:** with `+0x1c9c` bit `0x80000` and **`CBA_OFC_SPRINT` held** (`0x00961f3f`), an impulse built from 1.25·`+0x1cec` and the contact normal is applied to the vehicle (`0x00ab91b0`, `0x00ab96b0`), with driver bookkeeping (`0x0096c290`, `0x009e26b0`) and an effect (`0x00558c60`). Then the **impact**: relative speed along the normal in mph (×2.23694) from the vehicle's point velocity (`0x005247f0`) and the human's `+0xd04`; attribution to the driver; damage scaled by `0x009613d0` and per-class factors (`0x02624b98`…`0x02624ba8`, zero in the image — runtime tweaks); **knock-down or ragdoll with an impulse** (`0x009a48e0`, `0x00966d20`, `0x00780040`, `0x00781b80`); AI event `0x3e` (`0x004f6420`, `0x004f5dd0`). It reads `+0x1cec` (this frame's composed value) and `+0xd04` (this frame's proxy velocity) and **writes nothing to the proxy or the controller**; it returns nothing.
- The sprint query is **not gated on "is the local player"**: any human with `+0x1c9c` bit `0x80000` would read the local pad (OPEN 43).

### 5.9 Ordering, timing and sub-steps

1. **No physics sub-step touches a character.** The world step runs once per frame (`spec-physics-runtime.md` §4.3); the shape phantoms ride in it, but the proxies are integrated only by `0x0099b430`, after the physics window has closed (`0x0161d394` is decremented inside `0x0075ac20`), over the full frame Δt.
2. **The intent is read exactly once per frame**, by `0x0099d1b0`, before physics. The gather copies `+0x1cec` into the record; `0x007a5b10` turns the record into the proxy velocity **once**; the ≤ 10 passes advance the proxy over the remaining time without reading the human; the listener callbacks run inside those passes and only queue.
3. The **replay** runs once per frame after all passes (end of `0x0099ebe0`), so `0x00961d70` sees the `+0x1cec` the controller was given and the proxy velocity just written back. The second flush at the end of the vehicle pass (`0x00ac19ea`) runs before the next composer and only finds what was queued in between (HIGH: nothing from the character step, which the post-move flush has already drained).
4. **One-frame lag** (CONFIRMED by order): `0x00958690` (`current speed` `+0xd1c`, `Vertical Speed` `+0xd08`, `Supported By Ground`, `Speed Along rvec` `+0x1cec`), U2's writer (`+0xd04`, `+0x1cec`, `+0xd1c`, `+0x1a68`) and the camera pre-update all run before this frame's controller step and read the previous frame's results.
5. **Port consequence.** A port with a different integrator (sub-stepped or not) reproduces the original's input side exactly if it (a) composes `+0x1cec` once per frame as in §5.4, (b) sets the capsule velocity once per frame from it under §5.6.5's rules, (c) integrates over the frame Δt, (d) publishes velocity, speed and support back as in §5.6.6 and the position as in §5.7, and (e) buffers contacts and runs the §5.8 responses once, after movement.

### 5.10 Player and NPC: one bridge, four divergences

Every function of the bridge — `0x009a02a0`, `0x0099d1b0`, `0x0099ae30`, `0x0099b430`, `0x007a5b10`, the passes, `0x0099e2a0`, `0x0099ebe0`, `0x007a5760` — is called identically for players and NPCs. The bridge walks the object manager's lists itself (`+0x1f0`, `+0x1fc`, `+0x1e4`) and is **not** part of the per-human update `0x0099bcb0` of `spec-ai-runtime.md` §1. The divergences:

1. **Composer branch** (§5.4.3 step 5). Player: u = `+0xa50` (= unit(r)), or the facing while sprinting, and s = `+0xa60`, with no further shaping because U2's writer has already smoothed r. NPC: the AI's `+0xa50`/`+0xa60` (set through `0x00942110`, and `+0xcb0` through `0x00987360`) pass through **`0x009918b0(&u, &s, npc)`** (330 instructions, CONFIRMED by full listing): nothing when u or `+0xa50` is zero; the target speed is `0x0098e270(+0xccc)` (a per-locomotion-state speed) unless that returns −1 (then the incoming s); `0x00530340`, or a failing `0x0091f310(·, 2)`, take it unshaped. Otherwise the heading starts from the current horizontal velocity's direction when \|v_xz\|² > 0.25, else from the facing turned by `+0xa68`; while the pivot timer `+0x1ab8` runs, the target is scaled by a factor in [0.3, 0.8] taken from the cosine between `+0xa50` and that heading (`0x00434f10`; HIGH); when moving fast on fairly flat ground (`+0x1a64`) and not rising (`+0xd08` < 0.5), **the heading turns toward `+0xa50` at a limited rate** (Δt × a remap of the speed through `0x00dade20`, kept within [1.3, 5]; HIGH that the unit is rad/s), else u = `+0xa50`. **Speed: s = target + 0.4·(`+0xd1c` − target), kept within `+0xd1c` ± 10·Δt** (10 units/s²), then clamped to [0.1 when `+0xccc` ≠ 0, else 0; 11]. NPCs also get the procedural lean (`+0x1aa0`, `+0x1abc`), and **`+0xa60` = −1 makes the speed the animation's own** (\|T\|/Δt / rate × `+0xa64`).
2. **Gather** (§5.6.3): the NPC LOD skips the controller step for far, unseen NPCs flagged `+0x1da5` bit `0x80`; NPC velocities below 1e‑5 (squared) are zeroed; **only the player gets stick-scaled air control** (\|A\| + \|B\|).
3. **Per-list wrappers** (§5.4.1): players `0x009da350` (with Δt; camera cues); NPCs `0x009d3340` (network replication, `+0x1da4` upkeep) and an alternative entry gate on `+0xcd2`.
4. **Upgraded sprint and jump:** the ram (§5.8) and the jump boost (§5.4.3 step 9) depend on `+0x1c9c` bits that only the player's upgrades set (HIGH), but neither is gated on the player class.

**The AI throttle does not throttle locomotion.** A human skipped by the AI budget (`spec-ai-runtime.md` §1.4) keeps its last `+0xa50`/`+0xa60`, and the bridge still composes, steps and writes it back every frame; the only locomotion LOD is the gather's (item 2).

### 5.11 What a port must reproduce

1. The frame order of §5.1: compose after animation and before physics; step characters after physics with the frame Δt; sync the position afterwards; replay contacts last.
2. The composer of §5.4.3: `+0x1cec` = u·s + R·E/Δt (plus support velocity and a pre-set partner velocity), the −1 speed sentinel, stride matching with the [0.1, 1.5] rate clamp and its asymmetric rates, root motion under animation-controller flag `0x100`, the player's sprint along the facing, and the NPC shaping of §5.10.
3. `0x00985df0`'s state rules and the `+0xa64` rescale (§5.5).
4. The gather's NPC LOD and the player's air control (§5.6.3); the write-back's velocity and speed publication, landing, ground probe and the fall knock-down below −12.5 (§5.6.6).
5. A port that moves its own capsule may replace §5.6.4–§5.6.5 wholesale, provided it consumes the same per-frame velocity and publishes the same fields; the cast-and-solve internals are not part of the contract.
6. Deferred contact responses with per-pair merging and the 100-entry caps (§5.8).

### 5.12 Corrections and cross-spec flags raised by U3 (no other spec edited)

- **`review/scoping-player.md` §1.0 (diagram), §1.4, §2 and §5 (U3):** `0x0099ebe0` is the post-move pass, `0x007a5760` the contact flush (not the character controller), and `0x0079e900` → `0x00961d70` the vehicle-contact replay (not a per-human motion callback, not per sub-step); the bridge is `0x009989b0` → `0x009a02a0` → `0x0099d1b0` and `0x0099e7e0` (§5.1–§5.2). The single button query of `0x00961d70` is right, but `0x0099d1b0` reads `CBA_OFC_SPRINT` too (`0x0099e1af`).
- **This file:** §0 item 2 and §1.8.4 call `0x00961d70` the character-motion callback — it is the vehicle-contact replay (the level reading of `CBA_OFC_SPRINT` there stands). §3.5.4 step 19 and §3.7: `+0xcb0` is the move target point (§5.4.4), so `+0xa50` = unit(r). §3.5.5: `+0xa60` = k·\|r\| after `0x00985df0`, k = `+0xa64`. §3.8 item 4's suggestion to drive a controller from `+0x203c` is what the game itself does (times k). OPEN 19 resolved; OPEN 18 advanced.
- **`spec-anim-runtime.md` §4.2:** row 24's `+0x1cec` is the controller desired velocity (one frame old when read); the sources of rows 5, 7 and 20 are §5.6.1/§5.6.6; row 21a's `+0xa50` is unit(r) for the player; the note on field sources is answered in §5.3. **Root motion** (`review/scoping-animation.md` §1.7): §5.4.5 answers it from the movement side; the slot flag `+0x80` bit 0 that separates T from E, the orientation step `0x004b6530`, and the meaning of animation-controller flags `0x100`, `0x4` and `+0x1a4` bits `0x40`/`0x80`/`0x100`/`0x800` are animation-side (U5).
- **`spec-physics-runtime.md` §2.1 rows 21–25 and §4.6:** the character proxies are integrated by game code after the world step and outside the physics window, in at most ten lock-step passes whose casts run as game scheduler jobs (group `0x0223ac40`); `MyCharacterListener`'s overrides only queue; row 23's slot 0 `0x0079f8d0` is the scalar deleting destructor; `0x007a5b10` (controller motion) and `0x0079de40` (the proxy's game-introduced virtual: HIGH, a contact-manifold update with 0.1/1.1 distance thresholds) are P5 entry points.
- **`spec-ai-runtime.md` §1.1 / §1.4:** the `+0x1e4` list is the all-humans list the locomotion bridge walks; the AI throttle does not throttle locomotion; `+0xdc` bit `0x100000` (cleared per NPC at `0x009d91a4`) is set by the post-animation update `0x009a02a0`; NPC movement intent is `+0xa50`/`+0xa60` through `0x00942110` (−1 = animation speed) and `+0xcb0` through `0x00987360`, shaped by `0x009918b0`; the NPC LOD skip of §5.6.3 keys on the NPC-tail bit `+0x1da5` bit `0x80`.

---

## 6. U6.6 — The exit request and the non-animated exit states (`0x14`–`0x19`)

Unit U6.6 of `review/scoping-player.md` §5a.3. It covers how anything outside the vehicle-interaction band asks an occupant to get out (the exit request `0x00af3cd0` and the cancel family around it), and the six exit states that choose no animation `Element`: `0x14`–`0x19`. The animated exit states `0x1a`–`0x21` are U6.7's; the slot registry, the three setters and the full state numbering are U6.0's. **U6.0 had not landed when this section was written**, so the few slot fields and helpers this unit needs are cited below by raw address (§6.1); the same fields are also described in U6.0 once it lands, and the two should be reconciled then.

### 6.0 Results up front (U6.6)

1. **The exit request `0x00af3cd0(human, request)` is a cdecl call with two stack arguments and a `bool` result** (CONFIRMED by listing). `request` points to a 0x33-byte **exit request record** that the caller builds on its own stack with the constructor `0x004dccd0` and then edits (§6.2). The request finds the human's registry record from the human's vehicle handle `+0x16c0`/`+0x16c4`, runs a permission check (`0x00b0b170`, §6.3.2), refuses a slot that is already entering (phase 1) or exiting (phase 2), copies the record into the slot at `+0xa0`, and calls set-exit with **state `0x14`, send = 1, from-network = 0**. It never requests any other state. **[confirms scoping §5a.1 item 2]**
2. **It has 79 call sites in 70 functions** (raw `E8` scan; the census caller list agrees), in nine groups (§6.3.3): the Lua natives, activities and diversions, cutscene script, the store/purchase UI, a debug command, the human runtime (including the player's own exit prompt `0x009e5e80`, which sets request `+0x30` bit 7), the AI, vehicle code, and U6's own enter states (which ask the *current* occupant of a seat to leave, for example on a hijack). **[refines scoping §5a.3/§5a.6's caller categories]**
3. **`0x00af3e10` is not an enter-only cancel. It dispatches on the slot's phase** (CONFIRMED): phase 1 → set-enter state 8 (the stick-abort cancel); phase 2 → `0x00af24d0`; phase 3 → a fresh exit request with the "instant" bit set. The enter-only cancel is its sibling **`0x00af2460`** (16 call sites), which also *defers* the cancel during enter state `0x0d` (`extract`). **[corrects scoping §5a.1 item 8, §5a.3 and §5a.4 point 5]** (§6.4)
4. **`0x00af24d0` does not route "mid-entry" into exit. It acts only on slots that are already exiting (phase 2)**: from state `0x14` it sets the instant bit and goes to `0x15`; from any other exit state it goes straight to the finaliser `0x19`. It is an *exit fast-forward*. **[corrects scoping §5a.3, §5a.4 point 5]** (§6.4.3)
5. **The six states** (§6.5): `0x14` *requested* (wait for the request's start time and network authority), `0x15` *leave the seat* (vehicle- and human-side hand-off, then route), `0x16` *wait for the exit seat* (reached from `0x1f`'s on-entry), `0x17` *instant placement*, `0x18` *parked* (no local setter; reachable only through the network applier), `0x19` *finalise*.
6. **`0x00b03d80` is confirmed as the exit finaliser** (state `0x19`'s on-entry handler). Its last act releases the slot through `0x00b086d0`, which writes `+0x16d4` = 0 (scoping §5a.1 item 6). It is also entered from inlined copies of set-exit in the per-frame handlers of `0x1d` and `0x21`. **[resolves scoping §5a.3's HYPOTHESIS]** (§6.11)
7. **Equip-on-exit — mechanism pinned down** (§6.7.3). `0x00b036c0` is **state `0x15`'s on-entry handler**, not a free helper. On the animated path it reads the seat's `Equip Rifle on Exit` flag: the vehicle's `Vehicle_Interaction_Info` record at vehicle info `+0xa7c`, byte `+0x4f + 0x24·seat`, bit 0, which is `spec-tables-vehicle-world.md` §3.2's seat byte `+0x0B` bit 0.
   - If inventory slot 5 or slot 3 is occupied, the handler switches to slot 5.
   - Otherwise, unless class bit `0x21` is set, it gives the human a rifle by team: team byte `+0x1ca8` 5 → `Rifle-NG`, 6 → `Rifle-STAG`, anything else → `Rifle-Gang` (`0x00601890`). It then switches to slot 5 (`0x00600720`).
   - The finaliser reads the same flag again and calls `0x00943960(human, 1, 0)`, with the human's `+0x1394` timer back-dated by 2000 ms.

   **[confirms and details scoping §5a.2's HIGH]**
8. **The exit permission rule `0x00b0b170` reads `+0x1c9c` bits 8 and 9** (`cannot_exit_vehicles`, `cannot_exit_rc_vehicle`) against the human's active-vehicle pair `+0x16d8`/`+0x16dc`. This answers the OPEN item "who reads `+0x1c9c` bit 9" in `spec-lua-api-behaviour.md` §45.1 and its §45 review status (§6.3.2).
9. **Data check** (§6.12): the element key builder sends states `0x14`–`0x19` to its default case (first token `none`). No handler of this unit reaches the element rule, even through the full callee closure (5,104 functions). All 813 shipped `vi_exit.xtbl` `Element`s begin with a token of an animated exit state: `exit` 573, `extract` 191, `change seat` 33, `traverse` 16, and none with `none`. **[closes scoping §5a.1 item 7 from the exit side]**
10. **Size:** the U6.6 roots own ~3.0k instructions; their in-band closure (stopping at the setters and at other units' handlers) is 93 functions / ~9.1k. That is in line with scoping §5a.3's ~4.3k exclusive estimate, because this closure counts shared helpers.

### 6.1 Shared infrastructure used here (cited by raw address; to be reconciled with U6.0)

Read directly for this unit (CONFIRMED by listing unless marked):

- **Registry** `0x02845cb0`: 48 records, `0x1640` apart. **Slot** *i* is at record `+0x6a0 + 0x100·i`.
  - `0x00b08400(registry; vehicle handle)` finds a record by the vehicle's handle (record `+0x00`).
  - `0x00b08380(human)` returns the human's cached record `+0x1c48`.
  - `0x00b08390(record; occupant handle)` returns the slot index, or −1.
- **Resolver** `0x00af4500(record, slot, &record, &slot pointer, &enter request, &exit request, &vehicle, &human)`. Every handler in this unit starts with it. It returns:
  - the slot pointer;
  - slot `+0x5c` (the 0x44-byte enter request of `spec-lua-api-behaviour.md` §38.1);
  - slot `+0xa0` (**the exit request, §6.2**);
  - the vehicle, resolved from record `+0x00` through `0x004dcf00`;
  - the occupant human, resolved from slot `+0x00` through `0x004d7f30`.
- **Slot fields this unit reads or writes:**

  | Offset | Use here |
  |---|---|
  | `+0x00` | occupant handle |
  | `+0x08` | phase: 1 enter, 2 exit, 3 ride |
  | `+0x0c` | state |
  | `+0x10`, `+0x14`, `+0x18` | per-state fields; every on-entry handler of this unit sets them to −1. The finaliser first saves the old `+0x10` (§6.11). |
  | `+0x1c` | landing kind: set to 1 by `0x15`; set from `0x00b0f060`'s result (or 1 with no vehicle) by `0x17`; read by `0x19` |
  | `+0x20`, `+0x1c` | written by `0x00b034c0` |
  | `+0x2c` | seat index (0 = driver, as `spec-tables-vehicle-world.md` §3.1's eight-seat convention); set to −1 by the finaliser |
  | `+0x30` | set to −1 by `0x18` and `0x19` |
  | `+0x38` | timer (`0x16`'s exit-seat wait) |
  | `+0x3c` | set to −1 by `0x15` |
  | `+0x40` | an inventory item handle restored by the finaliser (HIGH: the weapon held before entry) |
  | `+0x58` bit 3 | cleared by `0x15` after calling `0x00920170(human, 1)` |
  | `+0x5c` | the enter request |
  | `+0xa0`–`+0xd2` | the exit request |
- **Set-exit `0x00b058f0(record, slot, state, send, from_network)`** (cdecl, five arguments):
  1. It returns at once unless the occupant resolves and passes three checks: object flag `+0x33` bit `0x10` clear, class row (`0x02cc9900`) byte `+0xa` bit 0, and `0x00853b10`.
  2. It logs `Set Exit` with both network ids through `0x00b0c280`.
  3. It jumps through the 14-entry table `0x00b05ac4` on state − `0x14`.
  4. **For `0x14` and `0x15` only**, `send` together with exit-request `+0x32` bit 1 calls the replication send `0x00b03010(record, slot, state)` (§6.6.3).
  5. For `0x15` only, `from_network` = 0 also runs the tip-over test `0x00b02540` (§6.7.1).
- **Inlined set-exit.** Six further places push the `Set Exit` string themselves and call an on-entry handler directly, without going through the table:
  - `0x00b05b90` (`0x1d` per-frame) → `0x19`;
  - `0x00b06020` (`0x16` per-frame) → `0x1f` and `0x1b`;
  - `0x00b063c0` (`0x1f` per-frame) → `0x21`;
  - `0x00b06d30` (`0x21` per-frame) → `0x19`.

  **A push survey of set-exit's 22 call sites therefore misses these transitions.** Find them by the string push `0x01185b38`.
- **Exit per-frame dispatcher** `0x00b06ef0` (table `0x00b06fdc`). In this unit's range only **`0x14` → `0x00b05b00`** and **`0x16` → `0x00b06020`** have per-frame handlers; `0x15`, `0x17`, `0x18` and `0x19` have none. The per-slot pose driver `0x00af6310` handles exit states `0x1b`–`0x21` only (scoping §5a.1 item 5), so **none of `0x14`–`0x19` is pose-driven**.

### 6.2 The exit request record (0x33 bytes)

**Constructor `0x004dccd0`** (thiscall on the record, 23 instructions). It sets:
- `+0x00` = −1;
- `+0x08`/`+0x0c` = the null handle (`0x011153a0`, 8 zero bytes);
- `+0x10`–`+0x1f` = 0;
- `+0x20` = `+0x24` = −1;
- `+0x28` = a timer set to "now" (`0x00d9e140(0)`);
- `+0x2c` = −1;
- the word `+0x30` = 0;
- byte `+0x32` bits 0 and 2 clear, **bit 1 set**.

**Copy `0x00af2250`** (thiscall, 28 instructions) copies exactly those fields into the slot at `+0xa0`, so the record is `+0x00`…`+0x32`.

**Timers.** `0x00d9e140(ms)` stores "now + ms" in milliseconds from `0x01320d94`, wrapping at `0x6b49d200`. `0x00d9e1e0` reports "expired" (now ≥ t) and returns false for a negative (unset) timer. Both CONFIRMED.

| Offset | Field (CONFIRMED by listing, roles HIGH unless marked) |
|---|---|
| `+0x00` | **exit seat**: the seat the occupant leaves through. −1 until `0x15`'s instant path or the exit-seat router `0x00b034c0` (§6.8) writes it. |
| `+0x08`/`+0x0c` | an object handle, null by default; written by some callers (`0x0051ec70`, `0x00b24cc0`, `0x00b3d7e0`, `0x00b62120`). Its reader is not in this unit (OPEN). |
| `+0x10`–`+0x1f` | a vector, zero by default; written by the vehicle-band callers (`0x00a9d6c0`, `0x00ab2480`, `0x00ab5720`, `0x00ab5e60`, `0x00ab6540`). At `0x00ab5720` it is the vehicle's `+0xc24`–`+0xc2c` scaled and transformed (`0x00da38e0`). HYPOTHESIS: the eject/exit-at-speed launch vector; it is read by U6.7. |
| `+0x20`, `+0x24` | −1 by default. `0x00afd290` writes `+0x20` = `0x13`; `+0x24` is passed to `0x00b0dec0` by the exit-seat router. |
| `+0x28` | **start time**. State `0x14` waits until it has passed. "Now" by default, so there is no wait. |
| `+0x2c` | **animation override**: an animation-state id (`vehicle_exit_do` arg 5 through `0x004bf810`); −1 = none. Read by U6.7. |
| `+0x30` bit 0 | **instant**: no exit animation. Set by many callers (§6.3.3), and forced by the request itself (§6.3.1 step 5), by `0x00af3e10`/`0x00af3f20`, by `0x00af24d0`, by `0x15`'s tip-over test, by `0x16`'s timeout and by `0x14`'s "skip wait" path. |
| `+0x30` bit 1 | **keep in place**: `0x17` does not reposition the human and the finaliser does not re-orient it. It is `0x00af3e10`'s third argument. |
| `+0x30` bit 2 | read by the finaliser: when clear, `0x009d3480(human, 1)` runs for humans whose `+0x1de0` is 1 or 3 |
| `+0x30` bit 3 | **being extracted**: the animated route picks `0x1d` (`extract`) instead of `0x1f` (`exit`) (`0x00b02150`). Set by `0x00afd290` (an enter-band caller). |
| `+0x30` bit 4 | `vehicle_exit_do` arg 4; also set by `0x0063e610` and `0x006b7780`. Read by U6.7 (OPEN). |
| `+0x30` bits 5, 6 | set by the vehicle-band callers (`0x20`, or `0x60`). Bit 5 also stops `0x15`'s despawn-the-dead branch. Read by U6.7 (OPEN). |
| `+0x30` bit 7 | set by the player's exit prompt `0x009e5e80`. Read by U6.7 (OPEN). |
| `+0x30` mask `0xa8` | if any of bits 3, 5, 7 is set, `0x15` skips the driver's vehicle hand-off calls (§6.7.2) |
| `+0x31` bit 2 | set by `0x15` from the point-set query `0x00b0b9a0(vehicle, seat, 7, 0x10, 0)` |
| `+0x31` bit 3 | on `0x15`'s instant path, go to `0x19` instead of `0x17` |
| `+0x31` bit 4 | `0x15` leaves `+0xec` bit `0x100000` clear, and the finaliser does not restore the saved weapon; set by `0x00a66c70` (a Lua native) |
| `+0x31` bit 1 | the finaliser's landing branch passes it as the new locomotion state (0 or 2 = run, §6.11 step 11) |
| `+0x31` bit 6 | finaliser: skip `0x00995410(human, 1, 1)` |
| `+0x31` bit 7 | `0x16`'s "waiting for the door" flag (§6.8) |
| `+0x32` bit 1 | **replicated**: default 1. With set-exit's `send`, it sends `0x14`/`0x15` over the network, and it makes `0x14` wait for network authority. It is `0x00af3e10`'s second argument. |
| `+0x32` bit 2 | **vehicle may be gone**: the request proceeds without a resolvable vehicle, and `0x14` skips its wait. Set by `0x00af3f20`'s fourth argument. |

### 6.3 The exit request `0x00af3cd0` (111 instructions)

#### 6.3.1 Procedure (CONFIRMED by full listing)

Arguments: `human` (`[ebp+8]`) and `request` (`[ebp+0xc]`). The result is `true` only when the transition to `0x14` was issued.

1. **No human, or no vehicle handle** (`+0x16c0`/`+0x16c4` both 0) → `false`.
2. **Resolve the vehicle** with `0x004dcf00(handle, 1)`. If it does not resolve and request `+0x32` bit 2 is clear → `false`.
3. **Clear the human's `being_vacuumed` force flag** with `0x009496c0(human, 0)`. This writes `+0x1c9c` bit `0x800` directly when either network gate holds (`0x008ae480`, `0x008837a0`); otherwise it sends an opcode-`0x46` record tagged `human` / `human_force_flagsbeing_vacuumed`. It is the same record shape as the `human_force_flags…` setters of `spec-lua-api-behaviour.md` (for example §15.13).
4. **Force "instant"** (request `+0x30` |= 1) when:
   - there is no vehicle;
   - the vehicle's info (`+0xbf4`) has **no `Vehicle_Interaction_Info` record** at `+0xa7c`;
   - or the human's `+0x1c9c` bit 3 is set.

   A vehicle with no interaction info therefore always exits without animation.
5. **Permission.** With a vehicle, `0x00b0b170(human, vehicle, instant)` must return true (§6.3.2); otherwise → `false`.
6. **Find the slot**: the record by vehicle handle (`0x00b08400`), then the slot by the human's own handle `+0x08`/`+0x0c` (`0x00b08390`). Either missing → `false`.
7. **Refuse phases 1 and 2.** A slot that is entering or already exiting is left alone → `false`.
   - In practice this means **only riding (phase 3) slots are accepted**.
   - By the literal test a phase-0 slot is accepted too. Phase 0 is seen only transiently, inside the network applier's enter case.
   - To redirect an exit already running, use `0x00af24d0` (§6.4.3).
8. **Copy** the request into slot `+0xa0` (`0x00af2250`).
9. **Set-exit**(record, slot, **`0x14`**, send = 1, from_network = 0) → `true`.

**Note.** The occupant's network-id check, the trace and the replication all happen inside set-exit (§6.1). The request itself never touches the slot's phase; `0x14`'s on-entry handler writes it (§6.6).

#### 6.3.2 The permission rule `0x00b0b170(human, vehicle, instant)` (77 instructions, CONFIRMED)

1. **Local player** (`0x0091f5e0`: human = `0x009da4e0()`): when `0x00818570()` is false and the request is not instant → **refuse**.
   - `0x00818570` is true when two conditions hold: the byte `0x022cde2e` is not (bit 0 set, bit 1 clear, bit 2 set), and the dword `0x022cf8d0` = 0.
   - HYPOTHESIS: a UI/cutscene "input may leave the vehicle" test.
2. When the vehicle is not instantiated (`+0x33` bit `0x08` clear) and the vehicle's `+0x1ac8` is non-zero (`0x00ad33a0`), a non-instant request → **refuse**.
3. **Exit locks.** Let *same* = the vehicle's handle equals the human's active-vehicle pair `+0x16d8`/`+0x16dc`. The lock applies when either holds:
   - `+0x1c9c` bit 8 (`0x100`, **`cannot_exit_vehicles`**, written by `follower_remain_in_car`, `spec-lua-api-behaviour.md` §15.13) is set and the vehicle is **not** *same*;
   - `+0x1c9c` bit 9 (`0x200`, **`cannot_exit_rc_vehicle`**, `character_set_cannot_exit_rc_vehicle`, §45.1) is set and the vehicle **is** *same*.

   A locked exit is still allowed when the request is instant, the human is dead (`0x0096f4f0`) or the human is ragdolled (`0x009a1dc0`); otherwise → **refuse**.
4. Otherwise → allow.

#### 6.3.3 Callers: 79 call sites in 70 functions

The request record is built with `0x004dccd0` at every site except four: `0x006d2fc0`, `0x00a67a00`, `0x00b3d3e0` and `0x00afa540` pass a record they did not build in the same function, and the unreferenced `0x00af0dd0` builds it inline. The field writes below are a push-corrected survey of the instructions between the constructor and the call.

| Group | Functions / sites | Members | Fields set |
|---|---|---|---|
| **Lua natives** (registered from the `.text` table at `0x00a256xx`) | 4 / 4 | `vehicle_exit_do` `0x00a67800`; `vehicle_exit_group_do` `0x00a67a00`; `0x00a66c70`; `0x00a348f0` | `+0x30` bit 0 from arg 2, bit 4 from arg 4, `+0x2c` from arg 5 (`vehicle_exit_do`); `+0x31` bit 4 (`0x00a66c70`); `+0x30` bit 0 (`0x00a348f0`) |
| **Activities and diversions** (under strings such as `ACT_TORCH_FAIL_WATER`, `ACT_TRAFFIC_*`, `ACT_ESCORT_*`, `DIVERSION_CHOP_SHOP_*`, `fail_hos_all_died`) | 14 / 18 | `0x0061b070`…`0x006d2fc0` | mostly defaults; `+0x30` bit 0 at `0x0061b070` and `0x0069f8e0`, bit 4 at `0x0063e610` and `0x006b7780` |
| **Cutscene script** | 2 / 2 | `0x00726570` (under `-- Cutscene Script Group --`), `0x0072fab0` | `+0x30` bit 0 set / cleared |
| **Store / purchase UI** | 8 / 9 | `0x007e4180`…`0x007e5210`, all under `0x007e58a0` (`STORE_AMBULANCE_MSG`, `mission_purchase`) | defaults |
| **Debug** | 2 / 2 | `0x007e7630` (`Homie force to exit vehicle!`); `0x00af0dd0` (no references; under the debug-camera global `0x029cdb88`, it walks every vehicle in the list `0x027c2eb8`/`+0xbec` and asks every occupant to leave) | `+0x30` bit 0 / defaults |
| **Human runtime** | 9 / 10 | `0x00953450`, `0x0099ebe0` (the post-move pass, §5.7: `Shark`), `0x009a48e0` (`Brute Fuel Tank`), `0x009c1ff0`, `0x009d9190`, `0x009e5b30`, **`0x009e5e80` (prompt entry 15, the player's enter/exit handler, §3.2.1)**, `0x009e7b70`, `0x009e8020` (two) | **`+0x30` bit 7 at `0x009e5e80`**; bit 0 at `0x009a48e0` (plus `+0x32` bit 1 cleared: not replicated) and `0x009e8020` |
| **AI** | 4 / 4 | `0x004dd050`, `0x0051ec70`, `0x0051f5c0`, `0x0052adc0` | `+0x08` handle at `0x0051ec70`; vector at `0x0052adc0` |
| **Vehicle code** | 8 / 10 | `0x00a757f0`, `0x00a9d6c0` (two), `0x00ab2480`, `0x00ab4060`, `0x00ab5720` (two), `0x00ab5e60`, `0x00ab6540`, `0x00ad1d90` | `+0x30` bits 5/6 plus the vector `+0x10`; bit 0 at `0x00a9d6c0` |
| **U6's own band** | 12 / 12 | `0x00af3e10`, `0x00af3f20` (§6.4); enter-state code `0x00afa540`, `0x00afabd0`, the commit `0x00afbd90`, `0x00afd290`, the routing hub `0x00afd4c0`, `0x00afed10`, `0x00afeeb0`, `0x00b000f0`, `0x00b01080`; `0x00b0d100` | `0x00afd290`: `+0x20` = `0x13`, `+0x30` bit 3 (being extracted), `+0x2c`; hub and `0x00afeeb0`: bit 0 |
| **Other** | 7 / 8 | `0x0088d260`, `0x00b24cc0` (called from the commit), `0x00b34040`, `0x00b3d3e0`, `0x00b3d7e0` (two), `0x00b46920`, `0x00b62120` | `+0x30` bit 0 at `0x0088d260`; `+0x08` handle at `0x00b24cc0`, `0x00b3d7e0`, `0x00b62120` |
| **Total** | **70 / 79** | | |

**How the in-band enter callers use it (HIGH).** The request is addressed to a *human*, and it refuses that human's own entering slot, so these calls are always about **another occupant**: the person in the seat being taken, for example on `0x00afd290`'s extraction. That is how a hijack makes the victim leave.

### 6.4 The cancel family

#### 6.4.1 `0x00af3e10(human, replicate, keep_in_place)` (94 instructions, CONFIRMED)

It finds the human's record through `0x00b08380` and the slot through `0x00b08390`, returning if either is missing, then switches on the **slot phase**:

| Phase | Action |
|---|---|
| 1 (entering) | set-enter(record, slot, **8**, `replicate`): the enter-side cancel state, U6.3's |
| 2 (exiting) | `0x00af24d0(human, replicate)` (§6.4.3) |
| 3 (riding) | builds a request (`0x004dccd0`), sets `+0x30` bit 0 (instant), bit 1 = `keep_in_place` and `+0x32` bit 1 = `replicate`, then calls `0x00af3cd0` |

It has **16 call sites plus one tail jump**:
- the wrapper `0x009b9250(human, flag)` (8 call sites) jumps to it with `replicate` = NOT `flag`, but only when `+0x16d4` is 3 or 1;
- the wrapper **dereferences a null human**: its "null" branch falls through to the `+0x16d4` read;
- the callers are activities and co-op diversions, `vehicle_exit_do` (cancel path: `(human, 1, 0)`), `character_evacuate_from_all_vehicles` `0x00a413d0`, `0x009720f0` and the network receiver `0x00af8810`.

**The answer to the OPEN item in `spec-lua-api-behaviour.md` §28.22 (`character_evacuate_from_all_vehicles` calls it with a negated boolean and 0):** the negated argument is the replicate flag (set-enter's flag / request `+0x32` bit 1); the 0 is *keep in place* (request `+0x30` bit 1) = off.

#### 6.4.2 `0x00af2460(human, replicate)`, the enter-only cancel (42 instructions, CONFIRMED)

1. Clears the `being_vacuumed` flag (`0x009496c0(human, 0)`).
2. Finds the slot as above. **There is no −1 guard on the slot index:** a missing slot reads record `+0x5a8`.
3. Acts only on **phase 1**:
   - in state `0x0d` (`extract`), it **defers** the cancel by setting enter-request field slot `+0x64` = 1;
   - otherwise it calls set-enter(…, **8**, `replicate`).

It has 16 call sites. Most are guarded by `0x009b9210` (`+0x16d4` = 1, entering): AI (`0x00512f90`, `0x00523aa0`, `0x00523c50`, `0x00524460`, `0x00545370`), `0x006ae5e0`, `0x00727190`, the human (`0x00987130`, `0x009a48e0`, `0x009e6f20`, `0x009e91c0`), vehicle code `0x00a9d6c0`, the network receiver `0x00af8810` and the routing hub `0x00afd4c0`. **This is the function scoping §5a.4 point 5 meant by "send it to enter state 8".**

#### 6.4.3 `0x00af24d0(human, replicate)`, the exit fast-forward (46 instructions, CONFIRMED)

1. Finds the slot through `0x00b08380`/`0x00b08390`. **There is no −1 guard here either.**
2. Requires **phase 2**:
   - **state `0x14`** → request `+0x30` |= 1, then set-exit(…, **`0x15`**, `replicate`, 0);
   - **any other exit state** → set-exit(…, **`0x19`**, `replicate`, 0). This jumps from the middle of an animated exit straight to the finaliser.

Its nine call sites:
- `0x00727190`, `0x009a48e0`, `0x009e6f20`: each first tests `0x009b9230`, i.e. `+0x16d4` = 2 (exiting);
- `0x00a9d6c0`, behind `0x009b9160` (`+0x16d4` = 3, so it does nothing unless the phase is 2);
- `0x00af3e10`, `0x00af3f20`;
- `0x00b02cd0`;
- `0x00b063c0` (exit `0x1f`'s per-frame handler, two sites).

Nothing here starts from an *entering* slot.

#### 6.4.4 `0x00af3f20(vehicle, despawn, no_replicate, vehicle_may_vanish)`: empty a vehicle (137 instructions, CONFIRMED)

It walks all eight slots of the vehicle's record. For each slot with a valid occupant:
- If `despawn` is set, the occupant is **despawned** (`0x00853ea0(human, 0, 0)`, the object-destroy primitive of `spec-lua-api-behaviour.md` §27.9) when all of the following hold:
  - the occupant is not class bit `0x21` (`0x00853b30`);
  - `0x0094f690` is false;
  - the network gate `0x008addb0` holds;
  - `0x0097e280` returns 0.
- Otherwise, by phase:
  - 1 → set-enter 8;
  - 2 → `0x00af24d0`;
  - 3 → a request with `+0x30` bit 0, `+0x32` bit 1 = NOT `no_replicate` and bit 2 = `vehicle_may_vanish`, then `0x00af3cd0`.

It has eight call sites (activities `0x0069e6f0`, `0x0069f8e0`, `0x006af4a0` ×2, the human `0x00938040`, Lua-band `0x00a396e0`, `0x00a62420`, vehicle `0x00a83cb0`).

### 6.5 The six states at a glance

| State | On-entry (via `0x00b05ac4`) | Per-frame (via `0x00b06fdc`) | Entered from | Leaves to |
|---|---|---|---|---|
| `0x14` requested | [`0x00b03010` send] + `0x00b021c0` | `0x00b05b00` | the exit request; the network applier | `0x15` |
| `0x15` leave the seat | [`0x00b02540` tip-over test] + [`0x00b03010`] + `0x00b036c0` | — | `0x14`; `0x00af24d0`; the applier | `0x1d`/`0x1f` (animated); `0x17`/`0x19` (instant); none (despawned dead occupant) |
| `0x16` wait for the exit seat | `0x00b022c0` | `0x00b06020` | `0x00b034c0` (from `0x1f`'s on-entry `0x00b05560`); the applier | `0x1b`, `0x1f` (inlined); `0x17` (timeout) |
| `0x17` instant placement | `0x00b02640` | — | `0x15`, `0x16`, `0x00b034c0`; `0x1d`'s, `0x1f`'s, `0x21`'s handlers | `0x19` (immediately) |
| `0x18` parked | `0x00b02860` | — | **the network applier only** (no immediate `0x18` and no local register-passed source) | — |
| `0x19` finalise | `0x00b03d80` | — | `0x15`, `0x17`, `0x00af24d0`; `0x1f`'s per-frame handler; inlined from `0x1d` and `0x21` | slot released |

**Network applier `0x00af2950`** (CONFIRMED by listing; the network unit is parked). Its steps:
1. It sets `0x02845b1c` = 1 (read back by `0x00af1eb0`).
2. It finds or claims the slot.
3. It copies the received snapshot into the slot, **including the whole exit request** (`+0xa0`–`+0xd2`).
4. It publishes the carried state at `0x0130e5a4` (read back by `0x00af1ec0`).
5. It applies the state:
   - `0x14` → set-exit(…, `0x14`, send = 0, from_network = 1);
   - any other state with slot phase 2 → set-exit(…, carried state, 0, 1).

**Correction.** Exit is entered from the exit request, from `0x00af24d0` on an already-exiting slot, and from this applier. **There is no mid-entry → exit route; a cancelled entry goes to enter state 8 (§6.4.1, §6.4.2) [corrects scoping §5a.4 point 5].**

### 6.6 State `0x14`: requested

#### 6.6.1 On-entry `0x00b021c0` (81 instructions, CONFIRMED by full listing)

It is called with the record on the stack and the slot in `edi`.

1. Human `+0x16d4` = **2**. This is scoping §5a.1 item 6's "exiting" write (`0x00b0220c`), already folded into `spec-lua-api-behaviour.md` §21.31's corrected `+0x16d4` note (0 none, 1 entering, 2 exiting, 3 seated).
2. Slot phase `+0x08` = **2**, state `+0x0c` = `0x14`, `+0x10`/`+0x14`/`+0x18` = −1.
3. Unless the human is down or dead (`0x0096f540`: `+0xcc8` is 5 or 6, or `+0xe3` bit 0): `0x00942110(human, 0xb, 0, −1)`. In §5.5's terms that is condition `+0xcc8` = `0xb`, locomotion state `+0xccc` = 0 and speed `+0xa60` = −1 (the animation-speed sentinel).
4. Human `+0xec` bit `0x100000` cleared.
5. **Skip the wait** when request `+0x32` bit 2 is set and either the network gate `0x008addb0(human, 0)` holds or `+0x32` bit 1 is clear. In that case: `+0x30` |= 1 (instant), then set-exit(…, `0x15`, 1, 0).

#### 6.6.2 Per-frame `0x00b05b00` (53 instructions, CONFIRMED)

When the request's start time `+0x28` has passed (`0x00d9e1e0`), and either the network gate holds or request `+0x32` bit 1 is clear, it calls set-exit(…, `0x15`, 1, 0).

**So a replicated request on a machine without authority stays in `0x14` until the owner's `0x15` arrives through the applier.** The start time defaults to "now", so a local request normally moves to `0x15` on the next tick.

#### 6.6.3 The send `0x00b03010(record, slot, state)` (84 instructions, CONFIRMED; network unit)

It is run from set-exit only for `0x14` and `0x15`, and only with `send` and request `+0x32` bit 1. It sends when:
- the object-resolving gate `0x008ae480(occupant handle, 0)` holds; or
- the state is `0x14` and the call is not an echo (an echo is `0x00af1eb0()` true with `0x00af1ec0()` = state).

To send, it snapshots the whole `0x690`-byte record, builds a header (`0x00af2b80`) and calls `Send Packet` `0x00af2eb0`.

### 6.7 State `0x15`: leave the seat

#### 6.7.1 Tip-over test `0x00b02540` (71 instructions; run first, only when not from the network)

The test applies only when the request is not already instant and the vehicle passes `0x00ad3b40` or `0x00ad5240`. Both are HYPOTHESIS-level vehicle predicates: the first walks a list on the vehicle's handle; the second resolves the handle and requires class byte `+6` bit `0x80`, then calls `0x00ad3c50`.

When it applies, it takes the second of the three angles that `0x00da48a0` derives from the vehicle orientation `+0x4c` (HIGH: a matrix-to-Euler conversion). If |angle| × 57.2958 is in [75°, 105°), the request becomes **instant**. **A vehicle lying on its side is exited without animation** (HIGH for the reading; the 75/105 bounds and the angle index are CONFIRMED).

#### 6.7.2 On-entry `0x00b036c0` (570 instructions, CONFIRMED by full listing)

**Common part**
1. `0x00bdb6d0(human, vehicle)`.
2. Slot state = `0x15`; `+0x10`/`+0x14`/`+0x18` = −1; **`+0x1c` = 1**.
3. Human `+0xe4` bit 2 cleared.
4. For an instantiated human (`+0x33` bit `0x08`, the bit §5.6.3 requires for a controller record): `0x0095acb0(human, 1, 0.2, 0, 0)`.

**Despawn a dead occupant (early return).** When all of the following hold, the handler calls `0x00853ea0(human, 0, 0)` (destroy) and **returns without leaving state `0x15`**:
- the network gate `0x008addb0(human, 0)` holds;
- the human is dead (`0x0096f4f0`);
- the human is not ragdolled;
- request `+0x30` bit 5 is clear;
- class bit `0x22` is set (`0x00853b30`).

`0x15` has no per-frame handler, so the slot stays there until the tick releases slots whose occupant is gone (scoping §5a.1 item 3).

**Human-side hand-off**
1. If slot `+0x58` bit 3: `0x00920170(human, 1)`, then clear the bit.
2. Let *mount* = vehicle `+0x688` when vehicle `+0xbd0` = 1, else none. HIGH: a mounted-weapon sub-object; it is reused in §6.11.
3. Slot `+0x3c` = −1.
4. Unless `0x00755df0(human)` = `0x80007` or `+0xcc8` = `0x12`:
   - class byte `+0xa` bit `0x02` → `0x009dd890(human, 0)`;
   - otherwise `0x00759d80(human, 0x13, 0)`.
5. If the human is ragdolled and *mount* exists: `0x0077ce40(human, mount+0x194)`.
6. `+0xdc` bit `0x100` cleared, then `0x00942110(human, −1, 0, −1)`: locomotion state 0, speed −1.
7. When `0x00985d80(human, 2, 0)` or `(human, 3, 0)` holds: `0x00986920(human, 0, 0)`.

**Driver's vehicle hand-off** (vehicle present and **slot `+0x2c` = 0**, the driver seat). HIGH as a group; the individual callees are vehicle-band contracts, not traced.
1. If *mount* `+0x180` is set: `0x00ae92b0`.
2. Release the handle at vehicle `+0x1624` (`0x0045f1a0`), clear it, and clear `+0x163c` bit 2.
3. If `0x0045ed50([vehicle+0x1638])`: `0x0045ea70([vehicle+0x159c], 0x286e13b2, 0, 0, 0)`.
4. If `0x0045ed50([vehicle+0x15bc])`: release that handle (`0x0045f1a0`).
5. Unless request `+0x30` & `0xa8`, or `0x009ec9e0()`: `0x00aa92c0(vehicle, human)`, `0x00ad57a0(vehicle handle, 0)`, `0x00ad5460(handle, 1, 0)` and `0x00ad5460(handle, 0, 0)`.
6. If the vehicle is usable: `0x00a929e0(handle, 0.0)`.

**Then, for every seat**
1. Human `+0xdc` |= 8; if `+0xcc0` is set: `0x0094cca0(human, 0, 0)`.
2. **Local player:** `0x00574e30(0.0, 0)` (camera band). Also `0x007f0140(1)` (§14, OPEN 5), and `0x009dcf10` when in fine aim (`0x009da830`).
3. If the held weapon (`0x00941f40`) passes `0x00b81440`: `0x009c7cc0(human)` and `0x009d1aa0(vehicle)`.
4. For class bit `0x02` humans: the `+0x2088` sub-object query `0x009f0860(…, 5, …, 1)` → `0x00821740` → `0x009f3530`. HYPOTHESIS: an AI/follower hand-off.
5. `0x0055f770(human)`, then `0x00943960(human, 0, 0)`.

**Route.** Request `+0x30` bit 0 (instant) set, or the human dead, missing or not instantiated → **instant path**. Otherwise → **animated path**.

- **Instant path.** Request `+0x00` (exit seat) = slot `+0x2c`, and `0x00b0b540(vehicle, human, seat)` (HIGH: seat reservation). Then:
  - the human usable and request `+0x31` bit 3 clear → set-exit(…, **`0x17`**, 1, 0);
  - otherwise → set-exit(…, **`0x19`**, 1, 0).
- **Animated path:**
  1. The equip-on-exit block (§6.7.3).
  2. If the vehicle exists and the seat's `Equip Rifle on Exit` flag is clear: human `+0xec` bit `0x100000` = (NOT `0x0052a230(human)` AND NOT request `+0x31` bit 4). With the flag set, the bit is cleared instead.
  3. `0x00b0b9a0(vehicle, seat, 7, 0x10, 0)` non-zero → request `+0x31` |= 4.
  4. The next state is `0x00b02150(record, slot)`. It is a pure function of request `+0x30` bit 3: **`0x1d`** (`extract`) when set, **`0x1f`** (`exit`) otherwise (CONFIRMED: ((NOT `+0x30` & 8) | `0x74`) >> 2).
  5. Set-exit(…, that state, 1, 0). **The animated exit therefore always starts in `0x1d` or `0x1f`; U6.7 takes it from there.**

#### 6.7.3 Equip-on-exit, exactly (CONFIRMED by listing; slot meanings HIGH)

On the animated path only, for a living, instantiated human, and only when the vehicle exists:
1. Read seat flag byte `[vehicle+0xbf4]+0xa7c` → record `+0x4f + 0x24·(slot +0x2c)`, bit 0. That is `Vehicle_Interaction_Info` `+0x44 + 0x24·seat + 0x0B`, i.e. `Equip Rifle on Exit` (written by the loader `0x00b07330` at `0x00b07716`–`0x00b07722`; `spec-tables-vehicle-world.md` §3.2).
2. If set, use the inventory at human `+0x1b78`:
   - **inventory slot 5 occupied** (`0x005fec20(5)`, an item lookup by slot through `0x004f0400`) → switch to slot 5;
   - else **slot 3 occupied** → switch to slot 5 as well (CONFIRMED as written; whether this is intended is OPEN);
   - else, **unless class bit `0x21` is set** (`0x00853b30(human, 0x21)`), give a weapon by name. `0x00601890` resolves the name through `0x008dcb10`, then adds it with `0x00601380` and the flag byte `0x10`. The name comes from the team byte `+0x1ca8`: **5 → `Rifle-NG`, 6 → `Rifle-STAG`, otherwise `Rifle-Gang`**. That is the same mapping `0x008d9e20` uses for its own rifle choice (there, other teams get `SMG-Gang` by chance). Then switch to slot 5.
   - The switch is `0x00600720(inventory; 5, 1, 1, 1)`: the shared 556-instruction slot-switch helper of scoping §5a.2, network-gated, with opcode `0x41` when not authoritative.
3. Then human `+0xec` bit `0x100000` is cleared.

**A second consumer of the same flag is the finaliser** (§6.11 step 12). It calls `0x00943960(human, 1, 0)` (`0x15` calls it with 0), and sets the human's `+0x1394` timer to now − 2000 ms. HYPOTHESIS: arrive combat-ready, with a "recently fired" timer already expired.

### 6.8 State `0x16`: wait for the exit seat

**Entry router `0x00b034c0`** (187 instructions; not an on-entry handler). It is called from `0x00b04990` and `0x00b04ae0`, both reached from `0x1f`'s on-entry handler `0x00b05560`; the router's own register argument is the slot.
1. It chooses an exit seat. If `0x00b0dec0(vehicle, slot +0x2c, request +0x24)` ≥ 1 it uses `0x00b02b50` (seeded with the current seat); otherwise `0x00b03110`.
2. **None found:**
   - with its stack flag, it releases any reservation (`0x00b0b770`), makes the request instant, sets the exit seat to the current seat, reserves it (`0x00b0b540`) and goes to **`0x17`**;
   - without the flag, it returns false and leaves the state unchanged.
3. **Found:** slot `+0x20`/`+0x1c` are set from the choice. Then:
   - if the seat already recorded in request `+0x00` is the same one → return true, no change;
   - if request `+0x00` holds a different seat → release it (`0x00b0b770`), reserve the new one (`0x00b0b540`), store it in `+0x00`, and go to **`0x16`**;
   - if none was recorded → reserve and store it. When it equals the current seat, request `+0x31` bit 7 = NOT `0x00b0a3f0(vehicle, seat)` (HYPOTHESIS: the exit point is clear), and if that bit is set the slot timer `+0x38` = 2000 ms. Then go to **`0x16`**.

**On-entry `0x00b022c0`** (36 instructions): state = `0x16`, `+0x10`/`+0x14`/`+0x18` = −1. Nothing else.

**Per-frame `0x00b06020`** (216 instructions, CONFIRMED):
- **`+0x31` bit 7 set (wait at the own seat):**
  - when `0x00b0a3f0(vehicle, exit seat)` holds: clear bit 7 and go to **`0x1f`** through an inlined set-exit (trace, then `0x00b05560` called directly);
  - otherwise, when `+0x38` has expired: instant bit, then set-exit(…, **`0x17`**, 1, 0).
- **Bit 7 clear (move to another seat):** let *taken* = the occupant of the exit seat (`0x00ab5070`, else `0x00ab51a0`) exists and is not this human. When `0x00b0b8d0(vehicle, exit seat)` returns this human's handle and the seat is not *taken*, go to **`0x1b`** (`change seat`) through an inlined set-exit (`0x00b02330` called directly). Otherwise wait, with no timeout.

### 6.9 State `0x17`: instant placement (`0x00b02640`, 172 instructions, CONFIRMED)

1. State = `0x17`, `+0x10`/`+0x14`/`+0x18` = −1.
2. **With a vehicle:**
   - unless request `+0x30` bit 1 (keep in place): `0x00b0f060` computes the exit position for seat `+0x2c` (HYPOTHESIS: from the seat's interaction point set) and its result goes to slot `+0x1c`. The human is then **teleported** there with its own transform setter (virtual `+0x40`, position plus an orientation from `0x00da5aa0`, facing along the horizontal vector from the vehicle's position `+0x40`/`+0x48` to the exit point, up `0x013213e4`), and `0x00941cd0(human, 0x029cdb98)` is called;
   - in both cases `0x00aa1e70(vehicle, exit seat, 1.0)` (HIGH: open that door fully).
3. **Without a vehicle:** unless keep in place, re-apply the current position with the orientation constant `0x01321400`; slot `+0x1c` = 1.
4. **Play animation state `0x1d95`** directly: `0x00957ee0(human, 0x1d95, 0.2, 0, −1, 0)` is the animation API's play call (`spec-anim-runtime.md` §6). This is not an `Element` lookup; state `0x1d95`'s name is OPEN.
5. `0x0096e6c0(human, 0, 1)`; human `+0xe4` |= `0x8000`; `0x004b21a0` on the rig (`[+0xd24]+0x10`) with `+0x1cc8`.
6. Set-exit(…, **`0x19`**, 1, 0) at once.

**`0x17` is a pass-through: one call takes the slot through `0x17` to `0x19`.**

### 6.10 State `0x18`: parked (`0x00b02860`, 53 instructions, CONFIRMED)

1. State = `0x18`, `+0x10`/`+0x14`/`+0x18` = −1.
2. `0x00af3630(vehicle handle, human handle, 2)`. This is a network record builder (the `0x028888e8` queue); it belongs to the network unit.
3. Slot `+0x30` = −1.

It has **no transition and no per-frame handler**. No immediate `0x18` appears at any set-exit call site or inlined copy, and the only register-passed local source (`0x00b02150`) yields `0x1d`/`0x1f`. **So locally `0x18` is reachable only through the network applier, and the snapshot it would carry must itself come from a local `0x18`.** HIGH: dead in the shipped game, like enter states `0x10`/`0x12` (scoping §5a.6).

### 6.11 State `0x19`: the finaliser `0x00b03d80` (928 instructions, CONFIRMED by full listing)

1. Save the old slot `+0x10`. State = `0x19`; `+0x10`/`+0x14`/`+0x18` = −1.
2. `0x00af3630(human +0x16c0/+0x16c4, human handle, 2)` (network). Slot `+0x30` = −1.
3. *mount* as in §6.7.2.
4. For an instantiated human: if animation state `0x1d95` is active (`0x004b46c0`, `0x004bf5e0`, `0x004beff0`), end it (`0x004b41b0(rig, 0x1d95, 0.0, 0)`). This is the hold `0x17` started.
5. `0x00b0baf0(human)`; *driver* = (slot `+0x2c` = 0); human `+0xdc` bit `0x8000` cleared; `0x0094cca0(human, 1, 0)`.
6. **Weapon restore** (human usable, class byte `+0xa` bit `0x02`):
   - unless `0x00755df0` = `0x80007`: `0x009dd870(human, 0)`;
   - with network authority, a non-zero slot `+0x40` and request `+0x31` bit 4 clear: look `+0x40` up in the inventory (`0x005feaa0`). When `0x008dc6f0(current item, saved item)` returns 1, switch back to the saved item's slot (`0x00600720` with `0x008dc690`'s result);
   - refresh the held weapon's attachment (`0x00b85fa0`, `0x004b2140` on the human's and the weapon's rigs);
   - local player: `0x005ffaf0(inventory, 1)`;
   - with no `+0x1c40` pair and `0x009bac20`: `0x009bdc40(human, weapon)`;
   - when the vehicle's info `+0x874` bit `0x20` is set, `0x00ad33a0` is false, and vehicle `+0x16c4` bit `0x80` and `+0x1704` bit 2 are clear: `0x00607ea0(3, −1, 2)`.

   For other humans: unless `0x80007`, `0x00759d80(human, 0xd, 0)`.
7. Weapon hold animation `0x009215b0(weapon, …)`, from `[+0xf4]` `+0xb4` or `+0xb6` by weapon flag `0x800`.
8. With *mount* and *driver*: when `0x00ab5100(vehicle)` = 0, call `0x00ab9ea0(vehicle, 1)`.
9. Unless the human is down or dead (`0x0096f540`): `0x00942110(human, 0, 0xff, +0xa60)`. That sets condition `+0xcc8` = 0 and keeps the locomotion state. **`+0xa60` is re-multiplied by `+0xa64` when positive**: a double scaling if `+0xa60` already carried the factor; behaviour CONFIRMED, intent OPEN.
10. **For every occupant:**
    - only class byte `+0xa` bit `0x04` humans (call this *full*): `+0x1dcc` = 0;
    - local player: the sprint-start timer `+0x2054` (§5.5) = 750 ms;
    - with a vehicle: release the seat reservation (`0x00b0b770(vehicle, human, 1)`), then the door sequence `0x00aa2080(vehicle, +0x16d0)` → `0x00a88e70`, `0x00a88f00`, `0x00aa1f60`, `0x00a88eb0` (HIGH: close or release the door used);
    - if vehicle `+0x16c8` bit 3 and `0x00ab4e60` = 0: `0x00ac2280(vehicle)`;
    - `0x00b0a170(human)`; local player: `0x00bbb5d0(+0x1db4)`.
11. **Placement on the ground.** There are two variants.
    - **(a) A *full*, living, instantiated human:**
      - if *driver*: `+0x978`/`+0x97c` = the vehicle's handle (HIGH: the last vehicle driven);
      - if `+0x1de0` is 1 or 3 and request `+0x30` bit 2 is clear: `0x009d3480(human, 1)`;
      - `0x00b0ce40(human, vehicle, seat)`, then `0x00b0d2f0(human, vehicle)`;
      - then, if not ragdolled, branch on the **landing kind** slot `+0x1c` (HYPOTHESIS for the names):
        - **0 (airborne)**: only when the saved `+0x10` is `0x17` or `0x18`. The human's `+0x1a00`/`+0x1a04` = the vehicle's handle (or null), the timer `+0x1a08` = 1000 ms, `0x00b02f00`, `0x0098aa10(human, 1, 1, 0)`, and unless request `+0x31` bit 6, `0x00995410(human, 1, 1)`;
        - **2 (water)**: let b = `0x009b6f40(human)`. If the request is instant, then only when b: `0x009b61d0(human)` and `0x00942110(human, −1, 0, −1)`. Otherwise: `0x0098aa10(human, 1, 0, 0)`, `+0xdc` |= `0x2000` when b, then `0x0096e6a0(human, v)`, where v is `+0xd10` (when valid) taken through the orientation `+0x4c`, else zero;
        - **any other value (ground)**: `0x00942110(human, −1, 0, −1)` (locomotion state 0).
    - **(b) Every other human** (not *full*, dead, or not instantiated):
      - `0x00b0d2f0(human, vehicle)` only;
      - then, if not ragdolled and not down or dead (`0x0096f540`): **landing kind 0** → the same airborne sequence as (a), under the same saved-`+0x10` condition; **any non-zero kind** → `0x00942110(human, −1, request +0x31 bit 1 (0 or 2), −1)`, so the human leaves walking or running.

    A port must keep the saved-`+0x10` condition (OPEN: that field's meaning is U6.0's/U6.1's).
12. **With a vehicle:**
    - script and HUD events carrying (human, vehicle, seat), posted only for the local player, or for a *full* human that passes `0x00921060`. They go through `0x0069ccf0`/`0x004d4b90` → `0x00e0cfb0`/`0x00e0ce20`:
      - on the vehicle's `+0xbf8` list (event 6);
      - and either on the local player's `+0x1f00` (event `0x17`) or on the human's `+0x1da8` → `+0xf8` (event `0x18`);
    - local player, under the global `0x014f71e8` bits 2, 3 and 4: `0x00a7e040`, `0x00a7b690` and `0x00a7b520` (vehicle, 0);
    - class bit `0x21`: `0x00abcd50(vehicle, 0)`, plus `0x00ad35b0(vehicle, 0)` for the local player;
    - `0x00ad3590(vehicle)` = 1 and not the local player: `0x0094f3e0(human, 0)`;
    - `0x00ab9c20(vehicle, seat, 0)`; when the *vehicle's* network gate `0x008addb0(vehicle, 0)` holds, `0x00aaa3d0(vehicle, 0)`;
    - **`Equip Rifle on Exit` set**: `0x00943960(human, 1, 0)` and `+0x1394` = now − 2000 ms.
13. Slot `+0x2c` = −1.
14. **Upright the human** unless ragdolled or request `+0x30` & 3 (instant, or keep in place): take the yaw of `+0x4c` (`0x00da4130`, `0x00da3910`), build a yaw-only orientation (`0x00da6de0(0, 0, yaw)`) and set it through the human's virtual `+0x48`.
15. **Release the slot: `0x00b086d0(record; slot)`.** This writes `+0x16d4` = 0 at `0x00b0880b` (scoping §5a.1 item 6).
16. Local player: `0x007f1930(0)`. Instantiated human: `+0xec` |= `0x100`. With a held weapon and no `+0x1c40` pair: `0x009bd610(human, weapon)`.
17. `0x0051fe60(human)`. HIGH: the AI's "left the vehicle" notification.

### 6.12 Validation against the data (closes scoping §5a.1 item 7 from the exit side)

1. **Code side (CONFIRMED by table read).** The key builder `0x00b0a6f0` jumps through `0x00b0abe4` on state − `0x0a` (24 entries). Entries `0x0a`–`0x0f` (states `0x14`–`0x19`) are all **`0x00b0ab73`**, the same target as its bounds check. That target writes first token `edi` = 0 (`none`) and zeros the next two. The non-zero first tokens of the exit side belong to `0x1a`–`0x21` only.
2. **Call-graph side (CONFIRMED by census closure).** From the 18 U6.6 roots, the full callee closure was taken, stopping only at the three setters, at `0x00b05560`/`0x00b02330` (U6.7's on-entry handlers that `0x00b06020` calls directly), at the network send `0x00af2eb0` and at slot release; callees over 1,500 instructions were not expanded. It covers 5,104 functions / ~358k instructions, and it **never reaches** `0x00b0ad10`, `0x00b0ac60`, `0x00b0a6f0`, `0x00b0a510` or `0x00b0a650`. Indirect calls are outside a census closure; none of this unit's handlers makes a virtual call that could reach the band.
3. **Data side (CONFIRMED, empirical; `scratchpad/u66/viexit.py` over `tools/as_base_xtbl/vi_exit.xtbl`).** 99 sets / 813 `Element`s, matching `spec-tables-vehicle-world.md` §2.2. `Parameter1`:

   | First token | Elements | State that produces it |
   |---|---|---|
   | `exit` | 573 | `0x1f` |
   | `extract` | 191 | `0x1d`/`0x1e` |
   | `change seat` | 33 | `0x1b` |
   | `traverse` | 16 | `0x21` |
   | `none`, or absent | 0 | (`0x14`–`0x19`) |

   **No shipped exit `Element` is addressable by a non-animated exit state.**
4. **Two nuances.**
   - "Non-animated" means "chooses no `Element`". **`0x17` still plays animation state `0x1d95`** directly, and the finaliser ends it.
   - `vi_exit.xtbl` has no `Element` starting with `deactivate`, `open` or `close`, the first tokens of exit states `0x1a`, `0x1c` and `0x20`. Flagged for U6.7: either those states are not set, or their keys resolve elsewhere.

### 6.13 What a port must reproduce

1. The **exit request contract** (§6.3.1): the 0x33-byte record with the constructor defaults (§6.2), the permission rule including the `+0x1c9c` bit 8/9 locks against `+0x16d8` (§6.3.2), the phase 1/2 refusal, and "always `0x14`". Callers may set only the fields in §6.2.
2. **Cancel semantics by phase** (§6.4): enter → state 8, with the `extract` deferral; exit → fast-forward to `0x15` or `0x19`; ride → an instant exit request.
3. **`0x14`'s wait** on the start time and network authority. A single-player port can treat the gate as always true, so `0x14` lasts one tick.
4. **`0x15`'s routing** (§6.7.2): instant to `0x17`/`0x19`, animated to `0x1d`/`0x1f`; the tip-over rule (75°–105°); the despawn-dead rule; equip-on-exit with the team-to-rifle mapping (§6.7.3).
5. **`0x17`'s placement** (teleport to the exit point facing away, play `0x1d95`) and **the finaliser's ordering** (§6.11). Above all: the `+0x16d4` write 2 → 0 on release, the weapon restore, the landing-kind branch, the upright, and the release last. A port that stubs U6.7 can send every exit through the instant path (request `+0x30` bit 0) and still reproduce the observable end state.

### 6.14 Corrections and cross-spec flags raised by U6.6 (no other spec edited)

- **`review/scoping-player.md`:**
  - §5a.1 item 8, §5a.3 (U6.6 row) and §5a.4 point 5: `0x00af3e10` is phase-generic, not "sets *enter* state 8". The enter-only cancel is `0x00af2460`, with 16 sites; `0x00af3e10` has 16 sites plus a tail jump (§6.4).
  - §5a.3/§5a.4 point 5: `0x00af24d0` is the exit fast-forward on already-exiting slots, not "from mid-entry". There is no mid-entry → exit route (§6.4.3, §6.5).
  - §5a.3: `0x00b036c0` is `0x15`'s on-entry handler, and equip-on-exit is confirmed with its mechanism; `0x00b03d80` is confirmed as the finaliser.
  - §5a.1 item 2: confirmed; the exit request requests `0x14` only.
  - §5a.6: the "70 callers" are 70 functions / 79 sites; the categories are refined in §6.3.3.
  - Set-exit has six inlined copies (§6.1), so the push survey of its 22 sites is not the full transition list. The same applies to U6.0's validation rule "account for every immediate value in the push survey".
- **`spec-lua-api-behaviour.md`:**
  - §21.11 `vehicle_exit_do`: **arg 3 is parsed but never used**. Arg 2 doubles as request `+0x30` bit 0 (instant) on the seated path; arg 4 → `+0x30` bit 4; arg 5 → `+0x2c`. The "49-byte buffer, 40 + 9" reading should become the 0x33-byte request built by `0x004dccd0` (§6.2).
  - The OPEN item on `0x00af3e10`'s negated argument and trailing 0 (the evacuate native, §21) is answered in §6.4.1.
  - The §45 OPEN "who reads `+0x1c9c` bit 9" is answered by `0x00b0b170` (§6.3.2), which also reads bit 8 (`cannot_exit_vehicles`).
  - The "`+0x1c9c` bit `0x8` consulted inside the struct-fill delegate" note (after §21.11) means: bit 3 forces an instant exit.
  - The `+0x16d4` = 2 write cited is already folded in; no change.
- **`spec-tables-vehicle-world.md` §3.2:**
  - `Equip Rifle on Exit` (seat byte `+0x0B` bit 0) is consumed by `0x00b036c0` and `0x00b03d80` (§6.7.3).
  - Vehicle info `+0xa7c` points to the vehicle's `Vehicle_Interaction_Info` record. A vehicle without one always exits instantly (§6.3.1 step 4).
- **`spec-ai-runtime.md`:** the AI's exit callers (`0x004dd050`, `0x0051ec70`, `0x0051f5c0`, `0x0052adc0`) and enter-cancel callers (`0x00512f90`, `0x00523aa0`, `0x00523c50`, `0x00524460`, `0x00545370`) go through the contracts of §6.3 and §6.4.2.

---

## 7. U6.0 — The vehicle-interaction slot record, registry and state skeleton

**Scope.** The first sub-unit of U6 as `review/scoping-player.md` §5a.3 defines it: the 48-record registry at `0x02845cb0`, the record and slot layouts, find / allocate / claim / release, the submitter `0x00af22a0` (completing `spec-lua-api-behaviour.md` §38.1), the three state setters and the numbering of all 35 states, the phase tick `0x00af9260` with its per-record upkeep and its three phase dispatchers, the routine scoping called the "on-entry prelude" `0x00afa400`, the four `+0x16d4` writes, and the public query surface every other unit cites. The behaviour *inside* each state belongs to U6.2–U6.8 and is only routed here. **U6.6 (§6) landed first** and cited the shared infrastructure by raw address in its §6.1. This section is the reconciliation, and every point checked agrees with §6.1/§6.2. Where §6 already specifies something (the exit-request record, the exit states, the six inlined set-exit copies), it is cited, not repeated. Network replication is parked (§5a.4 item 8); where it hooks in is recorded. **Method:** Ghidra-free, as for U0–U4: capstone listings over `tools/harnesses/exe_va.py` (with an esp-depth tracker to resolve every stack operand to its frame slot), the shared census for call edges, raw `E8`/`E9` rel32 and whole-image dword scans for every setter and on-entry handler, a push survey of all 51 setter call sites that also follows jumps into shared call tails, a displacement sweep of every census function in the band `0x00af0000`–`0x00b12000` for stores to the state, phase and `+0x16d4` fields, and static reads of the jump tables. `0x00b08400`, `0x00b083d0`, `0x00b08440`, `0x00b08460`, `0x00b08380`, `0x00b08390`, `0x00b084f0`, `0x00b08560`, `0x00b085d0`, `0x00b086d0`, `0x00b08860`, `0x00af20d0`, `0x00af26b0`, `0x00af4500`, `0x00af22a0`, `0x00afe570`, `0x00b058f0`, `0x00b09ff0`, `0x00af9260`, `0x00af5cb0`, `0x00af5bd0`, `0x00af9210`, `0x00b01f90`, `0x00b06ef0`, `0x00b0a0c0`, `0x00afa400`, `0x00af9a90`, `0x00b02150`, `0x00af1690`, `0x00af16d0`, `0x00af1770`, `0x00af17e0`, `0x00af1860` and `0x00af1cf0` were read in full. No Ghidra project was opened. No `.czn_pc` data was touched.

### 7.0 Results up front (U6.0)

1. **The registry is keyed by vehicle.** Each of the 48 records (`0x1640` bytes, `0x02845cb0`…`0x028888b0`) belongs to one vehicle handle (record `+0x00`); a human finds its record through the back-pointer **human `+0x1c48`**, which slot allocation sets and release clears (§7.1, §7.3). CONFIRMED.
2. **The setters never write the state.** Set-enter/exit/ride only log and jump to the new state's on-entry handler; **each on-entry handler stores its own state number** (one constant store per handler, all found by the band sweep, §7.5). Consequence: **states `0x10`, `0x12`, `0x1a`, `0x1e` and `0x20` have numbers, table slots and key-builder cases but can never be held** — setting one only logs, and the slot keeps its old state. Only **30 of the 35 numbers are live**. CONFIRMED.
3. **The 35-state numbering is validated** against every one of the 27 + 22 + 2 setter call sites (raw rel32 scan, no dword references), including register-passed values and shared call tails, and against the two on-entry jump tables (§7.5). Three numbered states with handlers — **enter 7, exit `0x18` and exit `0x1c`** — have **no static producer** anywhere except the network applier (OPEN 60). This agrees with §6.0 item 5 and OPEN 52 for `0x18`, and answers OPEN 58: `0x1a` and `0x20` cannot be held, and `0x1c` has no producer.
4. **`0x00afa400` is not an on-entry prelude: it is the network send of a transition** — it snapshots the record (`0x690` bytes) and posts it with the new state through `0x00af2eb0` when this machine owns the occupant (or the vehicle, for the first two enter transitions), unless that very transition is being replayed from the network **[corrects scoping §5a.1 item 8, §5a.3]** (§7.7).
5. **Set-exit is a no-op unless the occupant resolves to a human** (flag `+0x33` bit `0x10` clear, kind-row `+0xa` bit `0x01`); set-enter and set-ride log and dispatch regardless (§7.5). CONFIRMED.
6. **The per-record upkeep `0x00af5cb0` is the seat-resolution step**: every 250 ms per record it takes each slot still in phase 1 with an unresolved seat, chooses a seat and an entry point, and moves the slot to state 1 (or to the cancel state 8 when no seat can be found). It is also a **fourth place that sets the "instant" flag** (`0x00af5e03`) **[refines scoping §5a.5's list of three readers]** (§7.6).
7. **The state-1 hub `0x00afd4c0` also routes to state 5** (a shared call tail at `0x00afe05e` pushes 5 into the `0x0f` call) **[corrects scoping §5a.1 item 4's 2/4/8/`0x0f`/`0x13`]**.
8. **`+0x16d4` is written at exactly the four sites scoping listed** (1 at `0x00af9ae6`, 2 at `0x00b0220c`, 3 at `0x00b09a20`, 0 at `0x00b0880b`) — CONFIRMED by the band sweep, no fifth store (§7.8).
9. **`0x00b108c0` and `0x00b10900` are not "in/entering vehicle" queries**: the first initialises a `0x1c`-byte options block, the second is a caller of the submitter. The real query surface is the 15-function family `0x00af1770`…`0x00af1e50` (13 character-side predicates built on `0x00b084f0` + `0x00b08390`, and 2 vehicle-side ones built on `0x00b08440`) **[corrects scoping §5a.6's HYPOTHESIS]** (§7.9).

### 7.1 The registry `0x02845cb0` and the record layout

48 records × `0x1640` bytes, `0x02845cb0`…`0x028888b0` (bss). Record layout, CONFIRMED from the reset `0x00b08860`, the constructor `0x00b08460` and the readers named:

| Offset | Size | Contents | Written by / read by |
|---|---|---|---|
| `+0x00` | 8 | **vehicle handle** (0 = free record) | claim `0x00b08560`; reset → null |
| `+0x08` | 4 | record-level mode, 0/1/2 | state-1 hub `0x00afd4c0` (`0x00afde39`, `0x00afdeb2`, `0x00afdec9`, `0x00afdf7d`); reset → 0. Meaning: U6.3 |
| `+0x10` + `0xd0`·k, k < 8 | `0xd0` | **per-entry-point waiting queues**: `+0x00` count, `+0x08` + 8·j the queued humans' handles. Indexed by the seat's **`Queue`**: seat sub-record `+0x0C` of the vehicle's `Vehicle_Interaction_Info` record (`spec-tables-vehicle-world.md` §3.2), read as descriptor `+0xa7c` → `+0x50` + `0x24`·seat. The descriptor is vehicle `+0xbf4`; `+0xa7c` is the pointer to that record, §6.0 item 7 | `0x00b0b270` removes a human (below); initialised by `0x00af1f50` |
| `+0x690` | 4 | **number of occupied slots** | `++` allocate, `--` release |
| `+0x6a0` + `0x100`·i, i < 8 | `0x100` | **the eight occupancy slots** (§7.2) | |
| `+0xea0` | 4 | **upkeep timer** (−1 = unarmed; a `0x00d9e140` deadline) | `0x00af5cb0` re-arms 250 ms; state 0's on-entry clears it (`0x00af2660`) |
| `+0xea4` | 4 | pointer to the vehicle descriptor's block `+0xa80` (descriptor = vehicle `+0xbf4`) | submitter `0x00af23cd`; network receiver |
| `+0xea8`…`+0x153f` | `0x698` | **network shadow** of `+0x00`…`+0x690` (handle `+0xea8`, word `+0xeb0`, queues `+0xeb8`) | only the network applier `0x00af2950` and receiver `0x00af8810` (displacement sweep) |
| `+0x1540` | `0x100` | network shadow slot | same two only |

**Lifecycle.** The constructor `0x00b08460` has no direct caller (HIGH: run by a static array initialiser). Every record is reset (`0x00b08860`) at start-up by `0x00af1690` (← `0x005d25f0`) and at world reset by `0x00af16d0` (← `0x00707c80`), which also clears the receive queue and sets byte `0x02845bcc` = 1 through `0x00af9a80` (the only writer; the commit reads it, §8.2 step 12). A record whose vehicle no longer resolves as a vehicle is reset by the tick itself (§7.6).

**Receive queue** (network, parked): 300 entries × `0x38` at `0x028888e8`…`0x0288ca88`; `0x00b089a0` finds a free entry, `0x00b08980` clears one, `0x00af9210` drains it (§7.6).

### 7.2 The slot layout

`0x100` bytes; reset by `0x00af26b0` (CONFIRMED values in the "reset" column):

| Offset | Reset | Contents |
|---|---|---|
| `+0x00` | 0 (8 bytes) | **occupant handle** (0 = free slot) |
| `+0x08` | 0 | **phase**: 0 none, 1 enter, 2 exit, 3 ride. Stores: `0x00af9af0` (1), `0x00b0221a` (2), `0x00b09a26` (3), reset (0), network applier |
| `+0x0c` | −1 | **state** (§7.5); stored only by the on-entry handlers and the network applier |
| `+0x10`, `+0x14`, `+0x18` | −1 | per-state variant indices; reset to −1 again on entry to states 0 and 9. Read by the element key builder (U6.1, scoping §5a.1 item 7) |
| `+0x1c` | −1 | exit landing kind (§6.1) |
| `+0x20` | −1 | chosen entry point (written by the upkeep, §7.6) |
| `+0x24` | −1 | variant (upkeep writes 0; state `0x0a`'s per-frame writes it at `0x00affefb`); key builder |
| `+0x28` | −1 | |
| `+0x2c` | −1 | **seat index** — set by the commit from request `+0x10` (§8.2 step 3); read by release |
| `+0x30`…`+0x3c` | −1 | `+0x30` cleared again by the commit; `+0x38` the exit-seat wait timer, `+0x3c` cleared by `0x15` (§6.1) |
| `+0x40` | 0 | **the item held when the occupant sat down**. The commit stores the in-hand item's info `+0x5c` (§8.4). The finaliser looks it up with `0x005feaa0` and switches back to it (§6.11). §6.1 calls it a handle; it is the info record's `+0x5c` value (HIGH: an item id) |
| `+0x48`, `+0x50` | null handles | second and third handles (`+0x50` is resolved as a human by the commit, `0x00afc81d`); owners U6.3/U6.5 |
| `+0x58` | low nibble 0 | flag byte: bit `0x02` tested by the commit (§8.2 step 17); bit `0x08` = `Hide Occupant` applied (set by the commit, cleared by `0x15`, §6.1) |
| `+0x5c`…`+0x9f` | see below | **copy of the `0x44`-byte request record** |
| `+0xa0`…`+0xd2` | `0x004dccd0` | **the `0x33`-byte exit request** of §6.2. Slot `+0xd0` = its `+0x30`: bit 3 makes the exit chooser `0x00b02150` pick `0x1d` over `0x1f`. Slot `+0xd2` = its `+0x32`: bit 1 gates set-exit's network send |
| `+0xe0`, `+0xe4`, `+0xe8`, `+0xec` | 0.0 | **ride body-lag model**: vertical lag, vertical error, forward lag, forward error (§8.5) |
| `+0xf0` | −1 | ride reaction-delay timer (§8.5) |
| `+0xf4` | −1 | timer started (deadline = now) by the commit |

**Request record** (slot `+0x5c` = request `+0x00`). Field meanings from their uses in the submitter, the upkeep and the commit (offsets are request-relative; slot offset = +`0x5c`):

| Request | Slot | Meaning | Confidence |
|---|---|---|---|
| `+0x00` | `+0x5c` | requested seat (−1 none) | HIGH (upkeep mode 0) |
| `+0x04` | `+0x60` | first seat for a passenger search (−1 → 1) | HIGH |
| `+0x08` | `+0x64` | set to 1 by the submitter when `0x00ab5070(vehicle, +0x18)` holds and flag `+0x22` bit 3 is set; 1 suppresses the forced-instant fallback | HIGH |
| `+0x0c` | `+0x68` | **seat-search mode**: 0 the requested seat, 1 any seat from 0 (driver first), 2 any seat from `+0x04`. The submitter turns 1 into 2 for a remote-controlled vehicle (`0x00a77a10`) and a player-class character | HIGH |
| `+0x10` | `+0x6c` | candidate seat; becomes slot `+0x2c` at the commit | CONFIRMED (flow) |
| `+0x14`, `+0x1c` | `+0x70`, `+0x78` | previous `+0x10` / `+0x18` (copied each upkeep pass) | CONFIRMED |
| `+0x18` | `+0x74` | resolved seat / door index (−1 unresolved); the commit maps it to a door part (§8.3) | HIGH |
| `+0x20` bit 0 | `+0x7c` bit 0 | **instant** | CONFIRMED |
| `+0x20` bit 2 | `+0x7c` bit 2 | allow a vehicle with descriptor flag `+0x1d7b` bit `0x40` | CONFIRMED |
| `+0x21` bit 0 | `+0x7d` bit 0 | skip seat resolution: the upkeep goes straight to state 1 | CONFIRMED; also a key-builder field |
| `+0x22` bit 3, bit 6 | `+0x7e` | see `+0x08`; bit 6 skips the commit's team check (§8.2 step 33). Bits 1/2 of `+0x7e` are written by the upkeep (entry-angle class) | CONFIRMED |
| `+0x23` bit 3 | `+0x7f` bit 3 | **forced**: eligibility relaxed in the submitter; camera snap at the commit | CONFIRMED |
| `+0x23` bit 0, bit 2, bit 4 | `+0x7f` | bit 0 retry/"change seat" marker in the upkeep; bit 2 written by the upkeep; bit 4 passed to seat scoring | CONFIRMED (uses) |
| `+0x24` bit 0 | `+0x80` | passed to the eligibility predicate | CONFIRMED |
| `+0x28`…`+0x30` | `+0x84`…`+0x8c` | −1 on reset | |
| `+0x34` | `+0x90` | 0 on reset | |
| `+0x3c` | `+0x98` | **completion callback**, called once by the commit and cleared (§8.2 step 32) | CONFIRMED |

### 7.3 Find, allocate, claim, release (all CONFIRMED by full listing)

- **`0x00b08400(handle)`** (`this` = registry): the record whose `+0x00` equals the vehicle handle, or 0. `0x00b08440(object)` is the same for an object's own handle at `+0x08`.
- **`0x00b083d0()`**: the first record whose handle is 0, or 0 (full registry).
- **`0x00b08560(handle)`** (`this` = record, the **claim**): reset the head (`0x00af20d0`: handle null, `+0x08` 0, the eight queues), store the vehicle handle, return 1.
- **`0x00b085d0(handle)`** (`this` = record, **allocate a slot**): the first slot whose handle is 0, else −1. Reset it (`0x00af26b0`), store the occupant handle, `+0x690` += 1, and — when the occupant resolves to a live human — **human `+0x1c48` = the record**. Returns the slot index.
- **`0x00b086d0(i)`** (`this` = record, **release**):
  1. If the vehicle resolves and slot `+0x2c` (seat) ≠ −1: in the vehicle's seat record **`+0x1340` + `0x30`·seat**, clear `+0x18`/`+0x1c` (the seated occupant) if it is this occupant.
  2. If the occupant resolves to a human: if the vehicle exists and human `+0x16d4` = 1, `0x00b0b270(vehicle, human, 1)` (below); then **human `+0x16d4` = 0, `+0x1c48` = 0, `+0x16c0`/`+0x16c4` = null**.
  3. `+0x690` −= 1; reset the slot.
- **`0x00b08860()`** (`this` = record, **full reset**): release every slot whose occupant resolves to a human, reset all eight slots, then the head (handle, `+0x08`, queues, `+0x690`, `+0xea0` = −1, `+0xea4` = 0) and the network shadow.
- **`0x00b0b270(vehicle, human, flag)`** (121): if human `+0x16cc` ≠ −1, remove the human from the record's entry-point queue `+0x10` + `0xd0`·(seat descriptor `+0x50` of that entry point), compacting the queue, and set `+0x16cc` = −1. With *flag*: if the human's seat record (`+0x16c8`) has the human as its claim (`+0x00`), promote the next claim (`+0x10` → `+0x00`, `+0x10` = null) or clear it; then `+0x16c8` = `+0x16cc` = −1.
- **Lookups:** `0x00b08380(human)` = human `+0x1c48`; `0x00b084f0(handle)` = the same through a handle (0 unless it resolves to a human); `0x00b08390(handle)` (`this` = record) = the slot index of an occupant, or −1. **`0x00af4500(record, i, &record, &slot, &request, &exit-request, &vehicle, &human)`** (66 callers) is the band's standard resolver: slot = record `+0x6a0` + `0x100`·i, request = slot `+0x5c`, exit request = slot `+0xa0`, vehicle = `0x004dcf00(record handle)`, human = `0x004d7f30(slot handle)`; each output optional. `0x00b098e0` is the ride states' equivalent.

**Vehicle seat record** (vehicle `+0x1340` + `0x30`·seat), as U6.0's functions use it: `+0x00` the current **claim** (who is entering or seated), `+0x08` a pending handle (`0x00b0b540`), `+0x10` the **next claim**, `+0x18` the **seated occupant**, `+0x24` a timer. Human `+0x16c8` = seat, `+0x16cc` = queued entry point (both −1 when none). Field roles HIGH (from the claim/promote/seat pattern); offsets CONFIRMED.

### 7.4 The submitter `0x00af22a0(character, vehicle-or-0, request)` — complete

`spec-lua-api-behaviour.md` §38.1 summarised it; the full order (CONFIRMED by listing; 25 callers):

1. `0x0091faf0(character handle)` — the "is the local player" test; **its result is discarded**.
2. If character `+0x1c9c` bit `0x08` is set, set the request's instant bit. If instant and the character is ragdolling (`0x009a1dc0`), stop it (`0x009a1e10(character, 0)`).
3. **Eligibility** `0x00b0ca60(character, forced, request +0x24 bit 0)`, forced = instant or request `+0x23` bit 3. Refused → return false. **Full trace: §7.4a.**
4. **Vehicle and seat:** with a vehicle, `0x00b0fdf0(character, vehicle, request)` must succeed; without one, `0x00b105c0(character, request)` picks it. Both are U6.2. Failure → false.
5. A vehicle with descriptor flag `+0x1d7b` bit `0x40` is refused unless request `+0x20` bit 2 is set.
6. Remote-controlled vehicle (`0x00a77a10`) and player-class character (`0x00853b30(character, 0x21)`, kind-row `+0xa` bit `0x02`) with search mode 1 → mode 2.
7. **Record:** find by the vehicle's handle; else take a free record, reset it, claim it, and set `+0xea4` = descriptor `+0xa80`. No free record → false.
8. **Slot:** allocate for the character's handle; −1 → false. **No de-duplication** — a second request for the same character takes a second slot (§38.1's finding stands).
9. If descriptor `+0xa7c` (the seat/entry table) is 0 or record `+0xea4` is 0, **force instant**.
10. If request `+0x22` bit 3 and `0x00ab5070(vehicle, request +0x18)` is non-zero, request `+0x08` = 1.
11. Copy the request (`0x11` dwords) into slot `+0x5c` and call **set-enter(record, slot, 0, 1)**. Return true.

Steps 2, 6, 9 and 10 **modify the caller's request record in place** before the copy. Every submission starts at **state 0** (CONFIRMED: the only submitter call of a setter is `0x00af2443`).

Callers outside U6 also include **`0x00b10900`** (115; 21 callers in missions, activities and the UI band), which the `0x1c`-byte options block initialised by `0x00b108c0` (23 callers) accompanies (HIGH).

### 7.4a `0x00b0ca60(character, forced, eligibility_bit)` — the eligibility gate, traced in full: a pure character-state check, no vehicle/physics involved

**Scope.** Direct follow-up requested via a routed mission-scripting question, framed as "does this read vehicle position/distance, seat occupancy, or physics-body state, and what are its result codes." **Both premises are wrong, confirmed by full disassembly: it has no vehicle argument at all, reads only character-local state, and returns a plain boolean with no secondary code** — the vehicle/physics-side checks the question anticipated all belong to the sibling functions §7.4 step 4 and §13.4-§13.6 already cover, called only after this gate passes.

**Signature, confirmed from the body's own stack layout and both real call sites:** `(character, forced: bool, eligibility_bit: bool)`.

**The gate chain, in order (all 9 refusal paths, 1 success path):**
1. Refuse if already seated (`0x009b9160`, `+0x16d4 == 3`).
2. Refuse if already mid-entry or mid-exit of some vehicle (`0x009b91e0`, `+0x16d4 ∈ {1,2}`) — a second, previously unnamed predicate on the same phase field.
3. Refuse if `+0x16d8` or `+0x16dc` is non-zero — the same unnamed "busy/pending" pair already flagged OPEN (§7.3/§13.0 item 2); this is a second confirmed reader with identical polarity, strengthening (not resolving) that open item.
4. Refuse if ragdolling (`0x009a1dc0`, `+0xcd2 ∉ {0,6}`).
5. Refuse if down or dead (`0x0096f540`, `+0xcc8 ∈ {5,6}` or `+0xe3` bit 0).
6. **Only when NOT forced**, three more checks run (forced skips straight past all three to step 7): refuse via a generic, non-vehicle-specific interaction-lock helper (`0x009562d0`, widely reused elsewhere — shape confirmed, exact semantics OPEN, hinges on a `+0x1b78`-indirected sub-object and two unidentified hash constants `0x1d56`/`0x1d7e`); refuse if mid jump/dive pose (`+0xccd == 4`); refuse if mid climb pose (`+0xccd == 6`).
7. A class-capability-row filter (table `0x02cc9900`, row `+0xa` bit `0x04`, HIGH CONFIDENCE "has the standard AI-state block at all" by analogy to the same row's already-named `+0xa` bit `0x02` "is-player" test) gates the final tail.
8. **Tail:** forced → success unconditionally. Otherwise, if step 7's bit was clear → success anyway (the final refusal below applies only to kinds that have the bit set). Otherwise: refuse if `+0x2ba` bit `0x10` is set (the exact, already-named `cant_enter_vehicle` flag); else allow if current AI mode (`+0x501`) isn't `Idle`; else allow unless the AI sub-state (`+0x500`) is `0x10`/`0x11` — resolved directly against the already-documented 177-entry sub-state table (`0x012e1168`) to mean, literally, **"observe vehicle world accident"**/**"observe vehicular accident"**: a character currently playing an idle vehicle-accident reaction cannot start entering a vehicle unless forced.

**Return value is a plain boolean, nothing richer** — every refusal path sets the same `0`, the one success path sets `1`; there is no secondary reason code, only which branch fired.

**What it reads — exhaustively, exclusively on the character object:** `+0x16d4`, `+0x16d8`/`+0x16dc`, `+0xcd2`, `+0xcc8`/`+0xe3`, `+0xccd`, the `0x02cc9900` kind-row bit, `+0x2ba` bit `0x10`, `+0x501`/`+0x500`, plus the generic interaction-lock helper's own reads. **It touches no vehicle position/distance, no seat occupancy, and no physics/collision state anywhere, at any depth** — confirmed exhaustively (every instruction in its own body and all 8 direct callees, cross-checked against a raw whole-image byte scan so no indirect caller/callee was missed).

**The "2 physics obstruction queries" question, answered directly rather than assumed:** `0x00b0ca60` calls neither of the two obstruction-gate functions (§13.6's `0x00b0da10`/`0x00b0dc20`) nor their own queries (`0x0075d7e0`/`0x0075c880`, OPEN item #104) at any depth — there is no second, distinct pair of obstruction queries hiding in this function or its call tree. Any "2 obstruction queries still untraced" reference is a restatement of OPEN item #104 against the already-documented `0x00b0da10`/`0x00b0dc20`, not a new finding about this function.

**A second, previously undocumented caller: `0x005b7cc0`, a per-frame "Press [ACTION] to enter watercraft" UI-prompt helper.** It resolves the local-player singleton, runs its own busy/seated/dead pre-checks, then reuses the exact same `0x00b0ca60`→`0x00b105c0` (auto-pick, §13.3)→`0x00ad2f80` (vehicle-category test `== 5`, §13.4 step 4) chain the real submission path uses — not to submit an entry, only to decide whether to render the shared interact-button prompt (`"Press %s to enter watercraft."`, the same `CBA_GAC_ACTION` button §13.1 already names for all 22 vehicle-enter-style prompts). `0x005b7cc0` sits at local slot 15 of a previously-unidentified **second, contiguous 22-slot function-pointer table at `0x0111f488`** (immediately following the two tables §13.1 already names at `0x0111f2d0`/`0x0111f430` — the existing "sibling table" at `0x0111f430` was independently re-confirmed exactly, only slot 7 populated) — the same slot number as the main table's own vehicle-enter/exit handler (`0x009e5e80`). **Checked directly, 2026-10-09 — the table is real, but the lead does NOT close OPEN item #97.** All 22 entries read directly from `.rdata`: 18 populated, 4 empty (local indices 3, 4, 14, 18, 19). Decompiling a 7-slot sample (indices 0, 1, 2, 9, 10, 15, 20) confirms every populated, non-degenerate slot is a single-purpose "build and emit one specific on-screen interact prompt" function sharing the identical tail (a string-table lookup followed by a common "emit this prompt" sink) — the same shape already established for slot 15 alone, now shown to be the table's general rule: index 0 = `HUD_DOWNED_REVIVE`, index 1 = a forced-vehicle-entry prompt keyed on a previously unnamed player handle pair (`+0x28c0`/`+0x28c4`), index 2 = `HUD_ANSWER_CELL`, index 9 = the vehicle-surfing diversion start prompt, index 10 = the base-jumping diversion start prompt, index 15 = the watercraft prompt (already named above), index 20 is a trivial linker-folded stub (coincidentally resolving to a Wwise SDK symbol name, not a real audio-engine connection) acting as a "no prompt" filler. This table's sole walker (`0x005b6540`, a per-frame HUD-update routine, a confirmed 4th member of the same "trigger button" function family as the two already-named sibling tables' own walkers) loops all 22 bits of `+0x2084` and calls whichever populated slot's bit is set, first match wins — **but `+0x2084` is only ever READ in this walker, never written, verified across its entire disassembly.** Slot 15's own function was independently re-decompiled and confirmed to never touch `+0x2084` either — it runs only after the bit is already known set. **So this second table is a real, confirmed sibling consumer of the same bits the main table also only reads — not a producer, and not the setter OPEN item #97 is looking for.** That item remains open exactly as before, unredirected. Bonus, flagged as a new unexplored lead: the same walker also drives a further, previously-unknown **4th contiguous 22-slot table at `0x0111f4e0`** (immediately following this one), gated on a different button, only when this table's own scan finds nothing — not enumerated this pass. **This 4th table was enumerated directly, 2026-10-09, with the same negative result.** It is almost entirely empty — only local indices 16 and 18 are populated (20 of 22 slots null, including index 15, this table's own analog of the watercraft slot, which simply has no function at all). Index 16 (`0x005b8770`) is, byte-for-byte, the *same* function pointer as the first table's own slot 16 — a weapon pickup/replace prompt builder, gated on unrelated fields (`+0x1e18`, `+0x1dc0`/`+0x1dc4`, `+0x1a0`/`+0x1a1`); index 18 (`0x005b7940`) is a weapon-overheat ("cool the weapon") prompt builder, gated on a heat float at `+0x7a8`. **Neither slot references `+0x2084` at all, in either direction — not even as a read — and the walker's own two scan loops (one per table) each `TEST` the bit only, confirmed across the function's full 329-instruction body.** With both tables now fully checked, every function anywhere in the "trigger button" family's reachable code that touches `+0x2084` has been found to be a reader, none a writer — this negative result should be treated as solid, not merely "not yet found." A bounded-effort look at `0x005b6af0` (603 instructions, previously presumed by this item's own framing to hold the setter) found **zero references to `+0x2084` anywhere in its own body**, contradicting that presumption directly; it calls the other 3 confirmed trigger-button family members plus roughly 20 other, not-yet-characterized functions (listed in the scratch report below) that remain the most promising unexamined lead for a future pass. Full detail: `scratchpad/watercraft_prompt_table_open97_2026-10-09.md`, `scratchpad/fourth_prompt_table_0x0111f4e0_2026-10-09.md`.

Full detail, every address: `scratchpad/vehicle_enter_eligibility_0x00b0ca60_2026-10-09.md`.

### 7.4b Driving animation state: the seated/driving pose and enter/exit sequence, forward-traced from §7.4a (2026-10-09, mostly synthesis + new real-data confirmation)

**There is no separate "animation selection" subsystem.** Once `0x00b0ca60` and the sibling
vehicle/seat validators (§13.4-§13.6) pass, entry proceeds through the same 35-state
vehicle-interaction state machine (§7.5) that already owns slot/seat allocation; animation
selection is one more thing that machine's on-entry handlers do, via a shared **element-selection
rule** (§9, three small functions) that turns a slot's state/variant fields into a **named clip
lookup** against three shipped tables, `vi_enter.xtbl`/`vi_exit.xtbl`/`vi_ride.xtbl`.

**Forward trace, confirmed by listing:** submitter `0x00af22a0` starts every entry at state 0;
approach states 0-8 (§10) are pure routing, no clip lookup. The 250ms seat-resolution upkeep
`0x00af5cb0`/seat-scoring helper `0x00b0eb90` commits the seat. **Seating commit `0x00afbd90`**
(state 9, §8) is where the character is actually marked seated: `human+0x16d4 = 3`, and —
critically for the animation question — **`human+0xcc8` = 8 (driver) or 9 (every other seat), the
"condition row," written via the shared locomotion setter `0x00942110(human, 8/9, 0, -1.0)`** —
the same mechanism every other named locomotion state (sprinting, climbing, ragdoll) uses, not a
one-off override. The animated enter states (`0xa`-`0x13`) each call the element-selection rule
(`0x00b0ad10`) for a matching `Enter_Animations` clip and play it via **`0x00af45a0(human, anim)`**
on a hit; the per-slot pose driver `0x00af6310` (§12.2-§12.3) runs every frame to pin the character
to the seat's attach point while the clip plays. Once seated (phase `0x22`), the ride-key builder
`0x00af4760`/matcher `0x00b08aa0`/`0x00b08b80` picks a `vi_ride.xtbl` clip every relevant frame, and
the **rider-parameter writer `0x00af4b80`** feeds 11 named controller parameters (`Vehicle
Steering`, `Vehicle MPH`, lean/tilt, `Wheelie Bool`, `Endo Bool`) every frame from live vehicle
state — the continuous, non-discrete half of the seated pose. Exit mirrors entry: animated exit
states `0x1a`-`0x21` (§11) call the same rule against `Exit_Animations` and play via `0x00af45a0`.

**Clip names are real, named, data-driven entries, not hardcoded ids** — confirmed by direct read
of the real shipped `.xtbl` text (`tools/as_base_xtbl/vi_enter.xtbl`/`vi_ride.xtbl`), not just the
schema. The `Animation` field of a matched `Element` is a text name (e.g. `"Enter Left Driver"`,
`"Drive Ready SM"`, `"drive network"`), resolved once at load through `0x004bf810` against the
merged State/Action registry `0x03171c10` (`spec-tables-animation.md` §1.3/§5). An ordinary car's
ride set (`"standard driver"`) is a single `Element` (`"drive network"`) covering the entire seated
loop; a motorcycle's ride set (`"motorcycle driver"`) is a rich combat-ready/aim-direction set
(`"Drive Ready SM"` + 5 `Alt` variants); a watercraft-flavoured set (`"Wavecraft driver"`) adds
turn/reverse/fast variants a car/motorcycle set never uses.

**Tie-in to the blend/state-machine system:** the resolved registry index feeds the engine's one
generic animation-controller API (`0x00af45a0`, the `0x004b44a0`/`0x004b4320`/`0x004b46c0`/
`0x004b2710` query family, typed parameter setters `0x004b1410`/`0x004b1490`/`0x004b1510`) — not a
vehicle-specific interface. A real shipped node graph named **`driving_sm`** exists and is the
largest alias table in the whole 249-graph corpus (10 parameters, nested graphs, confirmed by
`spec-anim-runtime.md`'s own full-population validation pass, which also found real `ride_fire`/
`drive_fire` clip-match data consistent with a state machine selecting fire-while-riding vs.
fire-while-driving variants). **The literal instruction-level link from a specific `Animation`
registry string (e.g. `"drive network"`) to `driving_sm` itself was not traced by this or any prior
pass — HIGH CONFIDENCE by naming/structural correlation only, not CONFIRMED by disassembly.** This
is the single biggest open item from this investigation.

**Vehicle-class dependence is CONFIRMED pure data, zero vtable/code-level class switch.** The
vehicle descriptor carries a named `Vehicle_Interaction_Info` string reference
(`spec-vehicle-data.md` line 203/216); that row's per-seat `Enter_Animations`/`Exit_Animations`/
`Ride_Animations` fields are each an independent named reference into the three tables above. Real
shipped data proves there is no engine-side "vehicle class" concept at this layer: the shipped
`Wavecraft` jet-ski's `Ride_Animations` is literally `"motorcycle driver"`, while the shipped `Bike
Moped`'s `Ride_Animations` is literally `"Wavecraft Driver"` — cross-reuse in both directions. A
clean-room port only needs to reproduce the two-level string-indirection lookup (vehicle → info row
→ per-seat animation-set name → token-key match), never a hardcoded car/motorcycle/watercraft
branch — none exists in the original.

**Open items this pass flagged, not closed:** the `Animation`-string → `driving_sm` link (above);
`driving_sm`'s own internal node-graph structure (blend trees, parameters, layer masks — never
opened by any pass); `0x00b08ea0`'s half of the existing OPEN item 71 (the other ride-key-builder
caller); the SSE position/orientation blending inside `0x00af6310`'s shared tail (confirmed only at
call-target level). Full detail: `scratchpad/driving_animation_state_2026-10-09.md`.

### 7.5 The three setters and the numbering of all 35 states

**Mechanics (CONFIRMED by listing).**
- **Set-enter `0x00afe570(record, i, state, flag)`**: resolve the occupant (result unused), log `"Set Enter"` with both network ids through `0x00b0c280`, then — for 0 ≤ state ≤ `0x13` — jump through **`0x00afe924`** (20 entries). Cases 0, 1, 6 and 8 first call the network send `0x00afa400(record, i, state)` when *flag* is set; case 9 sends the record snapshot itself after the commit (§8.2). **No store to the slot.**
- **Set-exit `0x00b058f0(record, i, state, flagA, flagB)`**: **returns at once unless the occupant resolves to a human** (`+0x33` bit `0x10` clear, kind-row `+0xa` bit `0x01`; the `0x00853b10` result is discarded); otherwise log `"Set Exit"` and jump through **`0x00b05ac4`** (14 entries, index state − `0x14`). Cases `0x14` and `0x15` call the exit network send `0x00b03010` when *flagA* is set and exit-request `+0x32` bit 1 (slot `+0xd2`) is set; case `0x15` first calls `0x00b02540` unless *flagB*.
- **Set-ride `0x00b09ff0(record, i, state, flag)`**: log `"Set Ride"`; only state `0x22` reaches its on-entry `0x00b099d0`. *flag* is unused.

**Who writes the state.** The band sweep for stores to `+0x0c` finds exactly one constant store per on-entry handler (column "store"), the network applier (`0x00af29bc`, `0x00af2aec`), and the slot reset. So a state number is *held* only if its on-entry handler exists.

**Numbering** (CONFIRMED: jump tables read statically; per-frame tables `0x00b02100`, `0x00b06fdc` and the ride step `0x00b0a0c0`):

| State | Phase | On-entry | Store | Per-frame | Produced by (setter immediates, choosers, direct handler calls) | U6 unit |
|---|---|---|---|---|---|---|
| `0x00` | enter | `0x00af9a90` | `0x00af9af7` | `0x00afeb30` | submitter (`0x00af2443`) | 6.3 |
| `0x01` | enter | `0x00afd4c0` (hub) | `0x00afd529` | — | upkeep `0x00af6185`, `0x00af5e23` | 6.3 |
| `0x02` | enter | `0x00af95d0` | `0x00af961d` | `0x00afeca0` | hub `0x00afdbf9` | 6.3 |
| `0x03` | enter | `0x00af9640` | `0x00af968d` | `0x00afed10` | state `0x0f` per-frame `0x00b0131d` | 6.3 |
| `0x04` | enter | `0x00afabd0` | `0x00afac2f` | `0x00afeeb0` | hub `0x00afdc21`; state 2 per-frame `0x00afed00` | 6.3 |
| `0x05` | enter | `0x00af9dd0` | `0x00af9e1c` | `0x00aff1a0` | hub, shared tail `0x00afe05e` → `0x00afe06c` | 6.3 |
| `0x06` | enter | `0x00afb630` | `0x00afb692` | — | state 5 per-frame `0x00affbee` | 6.3 |
| `0x07` | enter | `0x00af96b0` | `0x00af96fd` | `0x00affc10` | **none found** (OPEN 60) | 6.3 |
| `0x08` | enter | `0x00afb930` (cancel) | `0x00afb98c` | — | 8 setter sites (incl. the cancel requests `0x00af2460`, `0x00af3e10`, `0x00af3f20`, the upkeep and the hub) and 9 direct calls (`0x00afe980` and eight per-frame handlers; the chooser's 8 is routed to these) | 6.3 |
| `0x09` | enter | `0x00afbd90` (**commit**) | `0x00afbdfa` | — | 5 setter sites (`0x00af995b`, `0x00afad79`, `0x00afd243`, `0x00aff181`, `0x00b01452`); direct calls from states `0x07`/`0x13` per-frame (`0x00affd47`, `0x00b01aa6`, each with an inlined copy of the setter's case 9) | **6.4** |
| `0x0a`–`0x0e` | enter | `0x00af9720`, `0x00af97e0`, `0x00afa0d0`, `0x00afae20`, `0x00afaf90` | `0x00af976f`, `0x00af9832`, `0x00afa121`, `0x00afae70`, `0x00afaff6` | `0x00affdb0`, `0x00b000f0`, `0x00b00c60`, `0x00b00310`, `0x00b00950` | the chooser `0x00afb220` | 6.5 |
| `0x0f` | enter | `0x00afd0a0` | `0x00afd0f2` | `0x00b01080` | hub `0x00afe06c`; the chooser | 6.5 |
| `0x10` | enter | **none** | — | — | nothing — **never held** | — |
| `0x11` | enter | `0x00af99b0` | `0x00af9a00` | `0x00b013c0` | state `0x0f` per-frame; direct call from state 3 per-frame | 6.5 |
| `0x12` | enter | **none** | — | — | nothing — **never held** | — |
| `0x13` | enter | `0x00afd290` | `0x00afd2ef` | `0x00b01630` | hub `0x00afdd3f` | 6.5 |
| `0x14` | exit | `0x00b021c0` | `0x00b02225` | `0x00b05b00` | exit request `0x00af3ddb`; network | 6.6 |
| `0x15` | exit | `0x00b036c0` | `0x00b0372a` | — | `0x00af2527` (mid-entry), `0x00b022ae`, `0x00b05b7e` | 6.6 |
| `0x16` | exit | `0x00b022c0` | `0x00b0230d` | `0x00b06020` | `0x00b035b5`, `0x00b03635` | 6.6 |
| `0x17` | exit | `0x00b02640` | `0x00b02692` | — | 6 sites | 6.6 |
| `0x18` | exit | `0x00b02860` | `0x00b028af` | — | **none found** (OPEN 60) | 6.6 |
| `0x19` | exit | `0x00b03d80` | `0x00b03ddb` | — | 6 sites; direct calls from `0x1d`/`0x21` per-frame | 6.6 |
| `0x1a` | exit | **none** | — | — | nothing — **never held** | — |
| `0x1b` | exit | `0x00b02330` | `0x00b0237f` | `0x00b062b0` | direct call from `0x16` per-frame | 6.7 |
| `0x1c` | exit | `0x00b023f0` | `0x00b0243d` | `0x00b024f0` | **none found** (OPEN 60) | 6.7 |
| `0x1d` | exit | `0x00b02900` | `0x00b02950` | `0x00b05b90` | the exit chooser `0x00b02150` | 6.7 |
| `0x1e` | exit | **none** | — | — | nothing — **never held** | — |
| `0x1f` | exit | `0x00b05560` | `0x00b055ab` | `0x00b063c0` | the exit chooser; direct calls from `0x21` on-entry and `0x16` per-frame | 6.7 |
| `0x20` | exit | **none** | — | — | nothing — **never held** | — |
| `0x21` | exit | `0x00b057b0` | `0x00b05822` | `0x00b06d30` | direct call from `0x1f` per-frame | 6.7 |
| `0x22` | ride | `0x00b099d0` | `0x00b09a2e` | `0x00b09ab0` | the commit `0x00afcdec`; network | **6.4** |

**The two choosers.** `0x00afb220` (333; called by state 6's on-entry and the per-frame handlers of `0x0a`, `0x0b`, `0x0c`, `0x0d`) returns 8, `0x0a`, `0x0b`, `0x0c`, `0x0d`, `0x0e` or `0x0f` — its seven returns, CONFIRMED. `0x00b02150` (34) returns `0x1d` when exit-request `+0x30` bit 3 is set, else `0x1f`.

**Validation against the push survey.** All 27 set-enter sites, 22 set-exit sites and 2 set-ride sites (raw rel32 scan = census; no dword reference to any setter or on-entry handler, so no indirect entry) resolve to: enter immediates {0, 1, 2, 4, 5, 6, 8, 9, `0x0f`, `0x13`}, register values from the chooser {8, `0x0a`…`0x0f`} and from `0x00b01080` {3, `0x11`}; exit immediates {`0x14`, `0x15`, `0x16`, `0x17`, `0x19`} and the chooser {`0x1d`, `0x1f`}; ride `0x22`; plus the network applier `0x00af2950`, which passes whatever the remote machine sent. Every value names a live state. **Inlined setter copies** reach the remaining live states. A whole-image scan for the trace string `0x01185a38` (`Set Enter`) finds 13 pushes: the setter's own, plus 12 places that log the transition themselves and call an on-entry handler directly — nine for state 8, two for 9 (`0x00affc10`, `0x00b01630`) and one for `0x11` (`0x00afed10`). The inlined set-exit copies found by §6.1 through `0x01185b38` give `0x19`, `0x1b`, `0x1f` and `0x21`. §6.1 counts six copies; the same whole-image scan finds five pushes besides the setter's own (`0x00b05d1e`, `0x00b0614e`, `0x00b0628f`, `0x00b06865`, `0x00b06ec8`), two of them in `0x00b06020`. `Set Ride` (`0x01186748`) is pushed only by its setter. That accounts for every live state except **7, `0x18` and `0x1c`**. Scoping §5a.6's "whether `0x10` and `0x12` are ever set" is answered: they cannot be held at all.

**Element-key cases for unheld states.** The key builder `0x00b0a6f0` (U6.1) has cases for `0x10` (`close`), `0x12` (`activate`), `0x1a` (`deactivate`), `0x1e` (`extract`) and `0x20` (`close`); with the states unheld, those cases are dead in this build (HIGH). **[Corrected by §9.5: the `0x1e` case is live — state `0x0e`'s on-entry `0x00afaf90` looks up the victim's `extract fail` exit animation through a temporary slot in phase 2, state `0x1e`; the 15 `extract fail` `Element`s of `vi_exit.xtbl` are reachable only this way. The other four cases are dead as stated.]**

### 7.6 The phase tick `0x00af9260` and the dispatchers

Called once per world update from `0x00702a50` (§5.1 frame order). For each of the 48 records whose handle is non-zero (CONFIRMED):

1. If the vehicle no longer resolves as a vehicle (kind-row `+6` bit `0x80`), **reset the whole record** and go to the next.
2. **Release** every slot whose occupant does not resolve to a live human, or whose human has a null vehicle handle `+0x16c0`/`+0x16c4`.
3. **Seat-resolution upkeep `0x00af5cb0(record)`** (459):
   - Runs only when the record timer `+0xea0` is unarmed or expired; re-arms it to **250 ms** at the end.
   - For each slot in phase 1 that is in state 0, or whose request `+0x10` or `+0x18` is still −1:
     - **force instant** (`0x00af5e03`) when the vehicle object fails the `+0x33` bit `0x08` test (`0x004d47e0`), or the occupant does not resolve, or fails the same test;
     - copy `+0x10`/`+0x18` to `+0x14`/`+0x1c`;
     - request `+0x21` bit 0 set → **set-enter(1)** directly;
     - otherwise, only where this machine owns the vehicle (`0x008ae480`): search a seat by the request's mode with the seat-exists test `0x00b0b230` and the seat scorer `0x00b0eb90` (U6.2), choose the entry point (`0x00b0c5e0`, `0x00af5840`, `0x00af5440`, door tests `0x00ab5070`/`0x00ab5150`, `0x00b0b9a0`, `0x00b0dec0`) from the occupant's bearing to the vehicle (degrees; thresholds 45 and 105), and write slot `+0x20`, `+0x24` and request bits `+0x7e` 1/2 and `+0x7f` 2;
     - a resolved seat → **set-enter(1)**; none, with retry bit `+0x7f` 0 set → clear it and retry the same slot; none otherwise → **set-enter(8)** (cancel).
4. **Ownership upkeep `0x00af5bd0(record)`** (81): when this machine does **not** own the vehicle (`0x008addb0`) but the vehicle body is active (`+0xbd0` = 1), any locally owned occupant in phase 1 or 3 sitting in seat 0 (`+0x16c8` = 0) requests ownership of the vehicle (`0x008addd0(vehicle, 0, 0)`). HIGH (network).
5. **Phase dispatch** per occupied slot on `+0x08`: 1 → `0x00b01f90` (per-frame table `0x00b02100`, 20 entries); 2 → `0x00b06ef0` (`0x00b06fdc`, 14 entries, index state − `0x14`); 3 → `0x00b0a0c0`, which calls `0x00b09ab0` for state `0x22` only. Each dispatcher resolves the occupant (result unused) and then calls the handler; the handlers take the record and slot in mixed stack/register conventions (per call site, CONFIRMED).

After all records, `0x00af9210` drains the receive queue: each used entry is applied through `0x00af8810` and cleared on success or when it is older than its own timeout (`+0x2c` start, `+0x30` ms).

### 7.7 The network send `0x00afa400(record, i, new state)` — scoping's "on-entry prelude"

CONFIRMED by listing (network replication itself stays parked):
- Ownership: o_h = `0x008ae480(occupant handle, 0)`, o_v = `0x008ae480(vehicle handle, 0)` (true when this machine owns the object, or for an unresolved one).
- Replay: r = (byte `0x02845b1c` is set **and** `0x0130e5a4` = new state) — the applier sets both while replaying a received transition.
- Send when o_h; or when o_v and the slot is in phase 1 / state 0 going to 1, and not r; or when the slot is in phase 0 / state −1 going to 0, and not r.
- Sending copies the record's first `0x690` bytes (`0x1a4` dwords), packs the slot (`0x00af2b80`) with the new state and posts both through `0x00af2eb0`.

It is called from set-enter's cases 0, 1, 6 and 8 and from the nine functions that call state 8's on-entry handler directly (the same nine — `0x00afe980` and eight per-frame handlers; scoping §5a.1 item 8's "inlined" cancel). The commit's own send (state 9) is inlined in set-enter and in the two direct callers. Exit transitions use `0x00b03010`, the same shape.

### 7.8 Phase-boundary writes

| Field | Value | Site | Confidence |
|---|---|---|---|
| human `+0x16d4` | 1 | state 0 on-entry `0x00af9ae6` | CONFIRMED |
| | 2 | state `0x14` on-entry `0x00b0220c` | CONFIRMED |
| | 3 | ride on-entry `0x00b09a20` | CONFIRMED |
| | 0 | release `0x00b0880b` | CONFIRMED |
| slot `+0x08` (phase) | 1 / 2 / 3 / 0 | `0x00af9af0` / `0x00b0221a` / `0x00b09a26` / reset | CONFIRMED |
| human `+0x16c0`/`+0x16c4` | the vehicle handle | state 0 on-entry (`0x00af9b1c`, `0x00af9b29`) | CONFIRMED |
| | null | release `0x00b0881c` | CONFIRMED |
| human `+0x1c48` | the record | slot allocation `0x00b086b6` | CONFIRMED |
| | 0 | release `0x00b08811` | CONFIRMED |

The band sweep (scoped to this vehicle-occupancy code band only) found no other store to `+0x16d4`
within it — the four sites above remain the only writers *inside the occupancy subsystem*. A fifth,
earlier write exists far outside this band, in the human constructor itself: **`0x00954be8`
(`mov [esi+0x16d4],edi`, inside constructor `0x00954340`) zeroes `+0x16d4` unconditionally for every
newly-constructed character**, confirmed 2026-10-08 by tracing `edi`'s definition back to a single
`xor edi,edi` at `0x00954388` with no redefinition before the write — this is the object's
default/initial value, not a fifth occupancy-transition state, and it is consistent with (not a
correction of) the four-site table above: it simply explains what `+0x16d4` reads as *before* any of
those four writers has ever touched a given character. This also closes the open question in
`scratchpad/char_state_enum_0x16d4_full_value_table_20261008.md` (line 174) of whether the constructor's
own contribution was independently pinned down — it now is. Full citation:
`scratchpad/named_objects_sentinels_0x954be8_m05_2026-10-08.md` §3.

Readers outside the band treat `+0x16d4` = 3 as seated (`0x009b9160`) and 1/2 as "entering or exiting"
(`0x009b91e0`). **Value 2 means exiting, not mid-entry**, as scoping §5a.1 item 6 said; this refines
`spec-lua-api-behaviour.md` §21.11 and §22.3.

State 0's on-entry also sets slot `+0x10`/`+0x14`/`+0x18` = −1, clears request `+0x10`…`+0x1c` unless request `+0x21` bit 0 is set, and disarms the record timer, so the upkeep runs on the next tick. Its other effects are U6.3's.

### 7.9 The public query surface

These functions all take a character handle and resolve it the same way: record = `0x00b084f0(handle)`, slot = `0x00b08390(handle)`. Each then tests the phase and the state (CONFIRMED for `0x00af1860` and `0x00af1cf0`; structure CONFIRMED for the rest).

- **`0x00af1cf0(handle)`** (28; 12 callers): the slot's state, or −1. **This is the generic "where is this character in vehicle interaction" query.**
- **`0x00af1860(handle, flag)`** (40; 12 callers): true when phase 1 and the state is in a fixed set (jump table `0x00af18d0`/`0x00af18dc`).
- `0x00af18f0`, `0x00af1960`, `0x00af1a00`, `0x00af1a60`, `0x00af1ac0`, `0x00af1b20`, `0x00af1b80`, `0x00af1be0`, `0x00af1c40`, `0x00af1ca0`, `0x00af1e50` are further predicates of the same shape. Their state sets belong to the units that own those states.
- **Vehicle-side** queries `0x00af1770` and `0x00af17e0` (both ← `0x00a924d0`) take a vehicle object and ask whether any slot in phase 1 is in a given state set (`0x00af1770`: `0x03`, `0x0c`, `0x0f`, `0x10`, `0x11`, `0x12`).

**Defect (CONFIRMED):** `0x00af1770` and `0x00af17e0` scan only the first `+0x690` slots, not all eight. Slots are not compacted on release (§7.3), so once a lower slot has been released, an occupant in a higher slot is missed.

### 7.10 What a port must reproduce (U6.0)

1. The registry keyed by vehicle handle, the human back-pointer `+0x1c48`, the slot and request layouts of §7.2, allocation without de-duplication, and release semantics, including the seat-record and `+0x16d4` clean-up of §7.3.
2. The submitter of §7.4, including its in-place edits of the request.
3. **Setters that dispatch without storing, and on-entry handlers that store their own state** (§7.5). A port that stores in the setter would make `0x10`/`0x12`/`0x1a`/`0x1e`/`0x20` holdable and change behaviour.
4. Set-exit's live-human gate (§7.5).
5. The tick order of §7.6: record reset, dead-occupant release, the 250 ms seat-resolution upkeep, the ownership upkeep, then the phase dispatch, then the queue drain.
6. In a single-player port, the network send (§7.7), the ownership upkeep and the queue can be stubbed. Ownership tests then read "owned".

### 7.11 Corrections and cross-spec flags raised by U6.0 (no other spec edited)

- **`review/scoping-player.md` §5a.1 item 4:** the hub's targets are 2, 4, **5**, 8, `0x0f`, `0x13`. **Item 8 and §5a.3:** `0x00afa400` is the transition's network send (§7.7), not a prelude. **§5a.5:** the instant flag is also *set* by the upkeep at `0x00af5e03` (§7.6), and the submitter forces it at `0x00af2408` and from `+0x1c9c` bit 3. **§5a.6:** `0x00b108c0`/`0x00b10900` are an options-block initialiser and a submitter wrapper, not queries (§7.9); enter `0x10`/`0x12` (and exit `0x1a`/`0x1e`/`0x20`) can never be held (§7.5). **§5a.2's "(b)" row** should add the 15 query functions of §7.9 to U6.0's public surface.
- **`spec-lua-api-behaviour.md` §38.1:** the submitter's full order is §7.4. The "8 fixed occupancy slots per vehicle" are per *registry record*, keyed by the vehicle handle, and the slot-clearing code is `0x00b086d0`. **§21.11:** the "per-seat-transition-kind" values 1/2/3 at slot `+0x08` are the **phase** (enter/exit/ride). The base `+0x6a8` is record `+0x6a0` + slot `+0x08`. `0x00b08380` returns the human's record back-pointer `+0x1c48`. **§21.11/§22.3:** `+0x16d4` = 2 means exiting (§7.8).
- **This file, §6 (U6.6), reconciled:**
  - §6.1's registry, resolver and slot fields agree with §7.1–§7.3.
  - The set-exit gate: the occupant resolves, flag `+0x33` bit `0x10` is clear and kind-row `+0xa` bit `0x01` is set. **The `0x00853b10` call that follows has its result discarded** (`0x00b05960`; no test follows), so it is not a third check as §6.1 lists it.
  - §6.1 counts six inlined set-exit copies; the string scan finds five besides the setter (§7.5).
  - §6.1's slot `+0x40` "item handle" is the in-hand item's info `+0x5c` value (§7.2).
  - **OPEN 48:** slot `+0x10`/`+0x14`/`+0x18` are the per-state variant fields that U6.1's key builder reads. They are reset at states 0, 9 and every U6.6 on-entry, and their writers are U6.3/U6.5/U6.7.
  - **OPEN 52 and 58** are answered by §7.5.
  - **OPEN 55:** `0x00853b30(object, n)` tests bit n mod 8 of kind-row byte `+6` + n/8. So class bits `0x21`/`0x22` *are* row byte `+0xa` bits `0x02`/`0x04`: one set of bits, not two.
  - **OPEN 55, extended 2026-10-09 — the class-registration mechanism that builds the kind row, confirmed for 43 real classes; the writer of a non-zero kind-row bit itself NOT found, an honest negative after an exhaustive search (dispatched LOW PRIORITY by `purrsian-04`, Team B's "is this a vehicle" classifier currently uses an object-name-string heuristic, labelled CHOSEN, and wanted the real mechanism if findable):**
    - A whole-`.text` byte-shape scan (not name-guessing) found **43 registered classes total**, including — for the first time — the real runtime `vehicle` (class index `7`), `player` (`0x21`), and `npc` (`0x22`) classes, alongside `general_mover`(5)/`weapon`(0x1f)/`projectile`(6)/`item`(0x1e)/`door`(0x1b)/`script_npc`(0x2a)/`script_vehicle`(0x2b) and 33 more (full table in the scratch report). Every one of the 43 shares one byte-identical registration-stub template: writes a transient shared vtable, the literal class index (`+4`), a second per-class literal byte (`+5`, value differs per class — `player`/`npc`=`0x20`, `vehicle`/`general_mover`=`3`, `weapon`=`0x1e`, `door`=`1`, `script_npc`/`script_vehicle`=`0x24`, meaning unresolved), the name-string pointer (`+0x28`), then writes the descriptor pointer into `0x02cc9900[classIndex]` — directly confirming `spec-zone-data-format.md` §11.5's inferred table mechanism for 9 concrete classes, not just the 2 it originally found. **Each class's `Create()` vtable slot (index 0) is what stamps a NEW OBJECT's own `+0x34` class-index byte** (`player`→`0x009e8660`: `mov [eax+0x34],0x21`; `npc`→`0x009d9ad0`: `0x22`; `vehicle`→`0x00a88640`: `7`) — this is the real, concrete writer of an object's class identity, with zero direct call sites anywhere in `.text` (reached only via the descriptor's own vtable, matching §11.5's "no central create-object-of-class-X dispatcher" note).
    - **The kind row itself (byte `+6` through `+0x25`, exactly what `0x00853b30` reads) is unconditionally ZEROED by this same registration-stub template, for every one of 9 classes fully disassembled** (`vehicle`, `player`, `npc`, `general_mover`, `weapon`, `door`, `item`, `script_vehicle`, `script_npc`) — no exception. **A later writer that ever sets a kind-row bit to 1 was searched for four independent ways across the full `.text` section (4.14M decoded instructions) and NOT found**: absolute-address writes to the 3 fixed descriptor globals (`player`=`0x0263aef8`, `npc`=`0x0262e940`, `vehicle`=`0x0271a8e0` — only 2 references each exist anywhere, both already-explained construct/destructor calls); register-indirect writes reached via a runtime class-index lookup (2 raw matches, both confirmed false positives on manual inspection); writes via the only 2 hardcoded literal table-slot pointers in the whole binary (`npc`'s and `vehicle`'s own slots, 22 call sites, all a generic pool-query idiom unrelated to the kind row); the x86 `bts`/`bt`/`btr`/`btc` bit instructions (4 occurrences total, all unrelated CRT bitset code); and a broadened "`or` into the `+6..+0x25` range near the reader's own shift-mask idiom" sweep (26 candidates, 3 superficially promising — same displacement, same shift idiom, even a companion bit-clear on byte `+5` — but all 3 traced to an unrelated linked-list subsystem via a different global, `0x024d8590`, that doesn't match the descriptor's own struct layout). **This is a solid, multi-strategy negative, not an incomplete search — but it means no concrete writer address can be cited for either the vehicle bit or the human/player bit as currently understood.**
    - **A real open question this pass surfaced, not previously flagged: `player`'s classIndex (`0x21`) and `npc`'s (`0x22`) numerically match the bit numbers `0x21`/`0x22` this section already confirmed — this COULD be the actual mechanism (bit = own class index, for these two specific bits) or could be registration-order coincidence (`vehicle`'s own class index is `7`, nowhere near `0x22`, which argues against a simple "kind bit = my class index" reading in general).** Not settled either way this pass — would need a live capture (reading each descriptor's own `+0xa` byte at runtime, correlated with `0x00853b30`'s live return value on a known player/pedestrian/vehicle) or re-deriving this section's own original 4 bit-confirmations' exact call-site arguments from scratch. Flagging, not dispatching — a live-capture decision belongs to `purrsian-04`/the user, consistent with this project's standing convention. Full 43-class table, every registration-stub instruction, and the complete negative-search writeup: `scratchpad/player_vehicle_class_bit_writer_2026-10-08.md`.
- **`spec-ai-runtime.md` §8 item 4** already belongs to U6.3 (scoping); no change from U6.0.

---

## 8. U6.4 — The seating commit, the ride state and the camera hand-off

**Scope.** State 9's on-entry handler `0x00afbd90`, the seating commit and the only local caller of set-ride. The ride state `0x22`: on-entry `0x00b099d0`, per-frame `0x00b09ab0`. The camera blend `0x00b0bc10`, which is the commit's camera hand-off. Owned helpers (scoping §5a.3): `0x00b0b930`, `0x00b0b270` (shared with release, §7.3), `0x00b098e0`. The pose snap `0x00b09400` is shared with U6.8 and is cited by role here.

**Method.** As §7, Ghidra-free. The commit (1551 instructions) was read in full through an esp-depth-tracked listing, which resolves every stack operand to one of its seven frame slots: vehicle, human, slot, request, record, player-or-0, record argument. The tracker reported no depth mismatch at any join. Read in full besides: `0x00b099d0`, `0x00b09ab0`, `0x00b098e0`, `0x00b0bc10`, `0x00b0b930`, `0x00b0b270`, and the vehicle, item and human callees named in §8.1–§8.4. Others are marked "role only". The vehicle-interaction tweak names were read from their registration stubs (`0x0100a840`…`0x0100ab00`); they are quoted as data. No Ghidra project was opened. No `.czn_pc` data was touched.

### 8.0 Results up front (U6.4)

1. **The commit's complete observable end state is §8.1.** This is the list a port-side stub must reproduce if it skips the commit's internals. Its core is small:
   - slot: phase 3, state `0x22`, seat `+0x2c`;
   - human: `+0x16d4` = 3, **condition `+0xcc8` = 8 (driver) or 9 (passenger)**, `+0xccc` = `+0xccd` = 0, seat `+0x16c8`;
   - vehicle: the seat record's occupant `+0x18`; for the driver, vehicle `+0x16d8`/`+0x16dc` (driver handle) and `+0x16e0` (team);
   - camera: target and submode.
   About thirty further flags and calls, mostly vehicle-side and player-only, complete it.
2. **The hand-off to vehicle controls is the condition row, not `+0x16d4`.** The commit sets human `+0xcc8` through the shared locomotion setter `0x00942110` (§5.5): 8 in seat 0, 9 elsewhere. U2 §3.0 item 4 already showed that rows 8, 9 and 17 run the classify `0x005b3750`, which sets context bit 4, and that bit 4 routes the action dispatch to the in-vehicle handler `0x005b5c50`. **So scoping §5a.5's stub list ("`+0x16d4` = 3 plus the vehicle handle; the context classifier sets driving from these") is incomplete [corrects scoping §5a.5; U2 already corrected the same attribution in §1.7]**: without `+0xcc8` = 8/9 the player keeps the on-foot controls (HIGH for the consequence: the row-8/9 per-frame bodies are U7).
3. **The holstering HYPOTHESIS is confirmed in shape (§8.4).**
   - The commit records the item in hand (its id goes to slot `+0x40`).
   - When the item may not be used in that seat, it selects inventory slot 0 through the inventory select `0x00600720`. The seat test is the descriptor bitmask `+0x8b4` and flags `+0x870` bits `0x100000`/`0x200000` (`0x00ac1a80`); the item test is `0x00b869b0`.
   - The ride on-entry then re-attaches whatever is still in hand to a rig attach point.
   - **The weapon/item calls are `0x005fe3e0`, `0x00600720`, `0x005ffaf0`, `0x00b85fa0` and `0x00b869b0`. `0x00b4d260` is not one of them**: it operates on the vehicle's AI object `+0xc68` **[corrects scoping §5a.2's list]**.
4. **The 39 vehicle-band callees are confirmed (exact count, census)**, and so is the claim that the commit changes the vehicle's own state. About half are predicates. The rest:
   - set the seat record;
   - move the vehicle onto the "occupied" list;
   - clear driver flag bits;
   - latch the entry door;
   - set the local-player-aboard bit `+0x16c4` `0x20`;
   - apply active cheats as vehicle force flags (`infinite_vehicle_collision_mass`, `cheat_no_damage`, `cheat_smash`);
   - seven calls into the vehicle-AI object (`drunk_driver` handling multipliers from the driver's drunkenness, goal reset, `override_speed` off, `never_go_ambient`).
   See §8.3.
5. **`Collision Bool`/`Collision Dir`** (§8.5) are set when the vehicle's horizontal velocity changes by more than `VI_collision_min_vel_diff` in a frame. The direction is the change's bearing relative to the occupant's heading. The same routine runs a **body-lag model**: two lag values chase the vehicle's forward and vertical velocity at `VI_forward_vel_accel`/`VI_vert_vel_accel` (± `VI_random_accel_factor`), but only after a random `VI_reaction_delay_min_ms`…`_max_ms` delay. These six tweaks are bss with no initialiser in the image (OPEN 63).
6. **The camera hand-off reads the vehicle's camera-angle sets.** `0x00b0bc10` passes `Primary_Camera_Angle` (or `RC_Camera_Angle` for a remote-controlled vehicle) and `Secondary_Camera_Angle` (descriptor `+0x628`/`+0x668`, `+0x648`) to the camera band's `0x0056cf60`/`0x0056cd10`. **This answers the core of §14 OPEN 28 and scoping OPEN 8** (§8.6).
7. **The commit can end in an immediate exit.** If another occupant belongs to a different team (`+0x1ca8`), the human is sent straight to the exit request after being seated (§8.2 step 33). Its *completion callback* (request `+0x3c`) has already fired by then.

### 8.1 The observable end state of the commit (what a stub must reproduce)

"After" means after `0x00afbd90` returns on its normal path, including the set-ride it calls (§8.5). Values are CONFIRMED by listing unless marked. **P** = only when the human is player-class (kind-row `+0xa` bit `0x02`); **L** = only for the local player (`0x0091f5e0`); **D** = only in seat 0; **A** = only when this machine owns the object (`0x008addb0`).

**Slot record** (§7.2):

| Field | Value | Site |
|---|---|---|
| `+0x08` phase | 3 | ride on-entry `0x00b09a26` |
| `+0x0c` state | 9, then `0x22` | `0x00afbdfa`, `0x00b09a2e` |
| `+0x10`/`+0x14`/`+0x18` | −1 | `0x00afbe08`… |
| `+0x2c` seat | request `+0x10` | `0x00afbe3f` |
| `+0x30` | −1 | `0x00afbe46` |
| `+0x40` | id (`+0x19c` → `+0x5c`) of the item in hand; 0 with none (P) | `0x00afc495` / `0x00afc544` |
| `+0x58` | bit `0x08` set when the seat's `Hide Occupant` flag is set (`Vehicle_Interaction_Info` seat byte `+0x09` bit 6, read at descriptor `+0xa7c` → `+0x4d` + `0x24`·seat); state `0x15` clears it (§6.1) | `0x00afcf98` |
| `+0xf4` | timer started now | `0x00afcfa8` |
| request `+0x3c` | 0, after the callback has been called | `0x00afcea4`, `0x00afceaa` |
| `+0xe0`…`+0xf0` | unchanged (0.0 / −1 from the reset) until the ride per-frame runs | — |

**Human:**

| Field | Value | Site / via | Confidence |
|---|---|---|---|
| `+0x16d4` | **3** | `0x00b09a20` | CONFIRMED |
| `+0x16c0`/`+0x16c4` | vehicle handle (set at state 0; unchanged) | `0x00af9b1c` | CONFIRMED |
| `+0x1c48` | the record (set at allocation) | `0x00b086b6` | CONFIRMED |
| `+0x16c8` | **seat** | `0x00b0b930` | CONFIRMED |
| `+0x16cc` | −1 (queue entry removed) | `0x00b0b270` | CONFIRMED |
| `+0xcc8` | **8 in seat 0, else 9** (condition row) | `0x00942110(human, 8/9, 0, −1.0)` at `0x00afcbde` | CONFIRMED |
| `+0xccc` | 0 | the same call, and `0x00afcbf8` | CONFIRMED |
| `+0xa60` | −1.0 (the "animation speed" sentinel, §5.4.3) | the same call | CONFIRMED |
| `+0xccd`/`+0xcce` | 0/0 (previous values to `+0xcd0`/`+0xcd1`) | `0x00986920(human, 0, 0)` at `0x00afcbef`; the player passes (1, 0) first at `0x00afc1af` | CONFIRMED |
| collision filter | `0x30010` (the seated layer) | `0x0094f450(human, 1, 0)` → `0x00759d80`; chosen because `+0x16d4` = 3 is already set | CONFIRMED (selection) |
| character controller `+0xcc0` | `0x0094cca0(human, 0, 0)`, then `controller → 0x007a5a90(0x00b0e9f0(vehicle), 1)` | `0x00afc02e`, `0x00afc050` | role only. HYPOTHESIS: the controller is parked on the vehicle's body |
| `+0xdc` | bit `0x08` cleared unless vehicle class 1 or descriptor `+0x874` bit `0x10` | `0x00afc0d1` | CONFIRMED |
| `+0xe8` | bit `0x20` cleared | `0x00afc0dc` | CONFIRMED |
| `+0xe0` | bit `0x100` set, and `+0x3a` bit `0x80` set (`0x00943cc0(human, 0)`), when `+0x1700` ≠ 0, `+0x3a` bit `0x80` and `+0x3b` bit 0 clear and `0x00ad3820` false | `0x00afcd89` | CONFIRMED |
| `+0x1b10` | 0 | `0x00afcda6` | CONFIRMED |
| `+0x1b08`/`+0x1b0c` | null handle | `0x00afcdb7` | CONFIRMED |
| `+0xe4` | bit `0x1000` set; bit `0x04` cleared when not owned here; bit `0x800` = vehicle `+0x16c4` bit `0x20` (`0x0094f3e0`) | `0x00afcff7`, `0x00afcfc4`, `0x00afce53` | CONFIRMED |
| `+0xec` | bit `0x100000` cleared | `0x00afd005` | CONFIRMED |
| `+0x19bc` | −1 | `0x00afd022` | CONFIRMED |
| animation controller `[+0xd24]+0x10` | `+0x1a4` bits `0x0800`–`0x4000` cleared (`0x004b7f70`); instant entries snap the seat pose (`0x00b09400` → `0x004b21a0`) | `0x00afcdda`, `0x00afcb9a` | CONFIRMED (bits) / role only (pose) |
| ragdoll | stopped (`0x009a1e10(human, 1)`) when active — non-player path | `0x00afc679` | CONFIRMED |
| inventory `+0x1b78` | §8.4 | | CONFIRMED (shape) |
| item in hand | attached to a rig point (ride on-entry, §8.5) | `0x009215b0` | CONFIRMED (call) |

**Vehicle:**

| Field | Value | Site / via | Confidence |
|---|---|---|---|
| seat record (`+0x1340` + `0x30`·seat) | `+0x18`/`+0x1c` = **human handle** (seated); the claim `+0x00` ← the next claim `+0x10` (or null), and `+0x10` = null | `0x00b0b930` | CONFIRMED |
| access seats' records | for the seat's `Primary_Access_Seat` and `Secondary_Access_Seat` (seat sub-record `+0x10`/`+0x14`, `spec-tables-vehicle-world.md` §3.2): if that seat's record has no claim, arm its `+0x24` timer to a random 250–750 ms (`0x00dab660(250, 750)`) | `0x00afc6fc`…`0x00afc779` | CONFIRMED |
| `+0x16c8` | bit `0x08` set (once; skipped if `+0x16c8` & 9) | `0x00ac2180` | CONFIRMED |
| `+0x16c0` | bit `0x40` set (same condition), and the vehicle is moved from the list `0x027c2eac` to the list headed at `0x027c2eb8` (links `+0xbec`/`+0xbf0`) | `0x00ac2180` | CONFIRMED (list). HYPOTHESIS: "has been occupied"; the list keeps it from ambient clean-up |
| `+0x16c0` | **D**: bits `0x800` and `0x1000` cleared | `0x00ad5460(handle, 1, 0)`, `0x00ad5460(handle, 0, 0)` | CONFIRMED |
| `+0x16c0` | **P**+**D**: bit `0x100000` set | `0x00afccb1` | CONFIRMED |
| `+0x16c0` | **L**, when `0x0071b7e0()` and not `0x00680db0()`, `0x00ad3290`, `0x00ad31d0`: bit `0x4000` set | `0x00ab6cf0(vehicle, 1)` | CONFIRMED |
| `+0x16c4` | bit `0x20` set (the local player is aboard), when the human is player-class and is the local player; each occupant's `+0xe4` bit `0x800` follows | `0x00ad35b0(vehicle, 1)` | CONFIRMED |
| `+0x16d8`/`+0x16dc` | **D**: the human's handle (the driver) | `0x00afcc89`, `0x00afcc96` | CONFIRMED |
| `+0x16e0` | **D**: the human's team `+0x1ca8` (`vehicle_set_team`'s field, `spec-lua-api-behaviour.md` §18.20) | `0x00afcc78` | CONFIRMED |
| `+0x16e1` | **D**, player-class, team changed: the previous `+0x16e0` | `0x00afcc64` | CONFIRMED |
| class 1 vehicles | `[+0x808]+0x5f4` = 0 (`0x00ab48f0`); **D**: every kind-7 part's `+0x48` = 0 (`0x00ab4680(vehicle, 0)`) | | CONFIRMED |
| parts `+0x68c`[k] | **D**: kind-8 parts with `+0x4c` = −1 → `+0x30` = 0 (`0x00aa2310`) | | CONFIRMED |
| entry door | when request `+0x18` ≠ −1: part d = `0x00aa2080(vehicle, it)` (−1 for a part flagged `+0xf0` bit `0x400000`); `+0xf0` bit `0x4000000` cleared; physics flag `+0x24` bit `0x10000` cleared; bit `0x20000` set unless `0x00aa1f60` holds | `0x00a88e70`, `0x00a88f00`, `0x00a88eb0` | CONFIRMED (bits). HYPOTHESIS: the door is released from "held open" |
| seat `+0x1e00` effect | `0x00ab9c20(vehicle, seat, 1)`: the seat's entry in the `+0x1e00` table is applied through `0x00755420`/`0x00755e10` with flags `0x80002` | | role only |
| body | `0x00a90c70(vehicle, 0)` (classes 0–2, 4 → `[+0x808]` `0x00a90630(0)`); `0x00aa9350(vehicle, human)` → `0x00aa91c0(vehicle, 1, 0)` unless vehicle `+0x1d7c` bit `0x20` is set, when the human is the driver or the local player and `0x00ad5180` is false. `0x00a90c70`'s class 4 case also requires `0x00ad3700` false | | role only |
| `vehicle_ai` `+0xc68` | **D**+**A**: `0x00b2baf0(vehicle, drunk)`, drunk = human `+0x2bb` bit `0x80` or `0x009422d0(human)`. Nothing happens when `+0xc6e` bit 1 already equals drunk; otherwise the `drunk_driver` flag is updated through `0x00b240d0(ai, drunk)` and, when owned here, `+0x1190`/`+0x1194`/`+0x1198` = 1.2/1.0/2.0 (drunk) or 1/1/1, the `degrade_info` update `0x00b26ef0`, `+0x1248`/`+0x1254` = 0 and the timers `+0x124c`/`+0x1258` restarted | `0x00afc017` | CONFIRMED (multipliers, gate); the flag bit HIGH |
| `vehicle_ai` | **A**, request `+0x10` = 0 and `+0xc72` bit `0x10`: `override_speed` off — `+0xc72` bit `0x10` cleared, `+0xd04` = 0.0 (`0x00b29cf0`) | `0x00afc111` | CONFIRMED |
| `vehicle_ai` | **P**+**A**: goal cleared (`0x00b2e510(ai, 0, 0, 0)`: goal word `+0x50` released and zeroed, `+0x18` = 0, `goal_stop_override` update); unless descriptor `+0x874` bit `0x2000`, `0x00b24cc0(vehicle, player)`, which **asks every other live occupant for which `0x00950550(occupant, player)` is false (HYPOTHESIS: not the player's ally) and whose seat passes `0x00b72c20` to get out**: exit request with `+0x08` = the player (§6.3.3); **L**, not scope view (`+0x1d95` bit 4), request `+0x10` = 0: `0x00b25c40(ai)` | `0x00afc145`… | CONFIRMED (calls) / role only |
| `vehicle_ai` | **P**: `0x0092cd00(vehicle)` → `0x0092cdb0` (`never_go_ambient`); `0x00932b70(vehicle)` → `0x00935130` | `0x00afccc0`… | role only |
| `vehicle_ai` | **A**, class bit `0x22`, slot `+0x58` bit `0x02`: `0x00b47110`/`0x00b4d260` (or `0x00b464c0`/`0x00b462a0`) on `+0xc68`, after a scan of the human's followers that are not in AI mode 25 (`Passenger`) through `0x00520950` | `0x00afc801`… | role only |
| force flags | **L**: if global `0x014f71e8` bit `0x04`/`0x08`/`0x10` is set → `infinite_vehicle_collision_mass` / `cheat_no_damage` / `cheat_smash` on the vehicle (`0x00a7e040`/`0x00a7b690`/`0x00a7b520`, 1) | `0x00afcd19`… | CONFIRMED (strings). HIGH: active cheats |
| hijack bookkeeping | when `+0x16c0` bit `0x10` is set and `+0x16e8`/`+0x16ec` is this human: `0x00b0d280(vehicle, human, 1)` over the other seats | `0x00afc6bf` | role only (U6.5) |
| engine/feature | class 1, **D**: `0x00ab4680(vehicle, 0)` (above); if vehicle `+0x33` bit `0x08`: `0x00ab9ea0(vehicle, b)`, b = not (seat-0 occupant player-class) and not remote-controlled | `0x00afc1fb` | role only |

**Camera** (**L** only; §8.6): the camera target becomes the vehicle (`0x00573500`), or the human for a gunner seat whose descriptor `+0x9c4`[seat] bit 0 is set. A vehicle submode is requested through `0x005753e0`. The follow camera is placed behind the vehicle, or snapped. The manual-look hold timer `0x012e41a4` is set to 500 ms. Submode-blend flag `0x012e4030` bit 0 is set (`0x0056b0e0(1)`).

**Player-only side effects:**
- the HUD inventory is hidden (`0x007f0140(1)`, `hud_inventory_hide`);
- `player_last_vehicle_handle` is updated (`0x009e1c40(player, vehicle, 0)`);
- `0x009dd110(player, vehicle)` runs when the vehicle's team byte is non-zero;
- `0x009e7b70(player)` and `0x009bc270(human)` run;
- when the vehicle descriptor's `+0x874` bit `0x20` is set, `0x00ad33a0(vehicle)` is false, vehicle `+0x16c4` bit `0x80` and `+0x1704` bit 4 are clear, and mission type `0x006a3910()` ≠ `0xe`: `0x00607ea0(3, 5, 2)`;
- each follower is notified (`0x004f5dd0(0x2e, player, follower, 0, 0x2d, 0xb2, 0)`);
- **L**, seat 0, byte `0x02845bcc` set: one **vehicle-type tutorial** through `0x007169e0`.
  - The ids are `0x58`/`0x59`, `0x5a`…`0x5d`, `0x6e`…`0x74`, `0xb1`, `0xb7`, `0xc3`, picked by the type predicates `0x00ad3140`…`0x00ad32d0`, by the variant test `0x005bc5d0`, and by the model names `sp_mdsphere_genki` and `truck_2dr_classic01`.
  - `0xb7` is shown only when `0x007162d0(0xb7)` is false.
  - Which predicate means which vehicle type, and the meaning of the `0x005bc5d0` variant, were not read (OPEN 62);
- `0x00bd8240(human, vehicle)`: for the local player in condition 8, when three bytes (`0x013123d1`, `0x029897a5`, `0x029897a6`) are set, it restarts a timer `0x0131244c` and zeroes `0x029897b0`. HYPOTHESIS: a driving statistic.

**AI and script notifications:**
- `0x004e4130(human, 0x19)` (`human_ai_data`);
- the AI action is completed (`0x004f4f50`) when the human's action `+0x4a8` is `0x25`…`0x27` or `0x31` — `vehicle_enter_do`'s order is action `0x31` (`spec-lua-api-behaviour.md` §38.1);
- the request's completion callback `+0x3c` is called with (vehicle handle, occupant handle, request, 1) and then cleared;
- the forced flag (request `+0x23` bit 3) runs `0x00853ea0(0x00afb170(human), 0, 0)`.

**Network:** when set-enter's flag is set and the slot is in phase 3 after the commit, the `0x690`-byte record snapshot and the slot are posted (`0x00af2b80` → `0x00af2eb0`, §7.7). A first slice can stub this.

**Minimal stub.** A port that skips the commit but wants the original seated behaviour must at least do the following. Everything else in the tables above is a vehicle-, AI- or player-only refinement that the stub can add incrementally.
- Write the slot rows above, and `+0x16d4`, `+0x16c8`, `+0x16cc`, `+0xcc8`, `+0xccc`, `+0xccd`.
- Apply the seated collision filter and park the character controller.
- Write the seat record's `+0x18`, and the driver handle and team.
- Hand the camera over (§8.6).
- Holster as §8.4.
- Run set-ride.

### 8.2 The commit `0x00afbd90(record, i)` in order

All sites are in `0x00afbd90` unless named. The order matters for the port: set-ride runs late (step 29), after most human and vehicle changes, and the team check runs last (step 33).

1. Resolve (`0x00af4500`). Slot state = 9; `+0x10`/`+0x14`/`+0x18` = −1.
2. `0x00af3630(vehicle handle, human handle, 1)`: drop any queued network packet for this pair from the receive queue. When it finds one, it raises human `+0x16b4` and zeroes `+0x16b8`. Network.
3. Slot `+0x2c` = request `+0x10` (the seat); `+0x30` = −1.
4. If human `+0x16cc` ≠ −1: `0x00b0b270(vehicle, human, 0)` — leave the entry-point queue (§7.3).
5. `0x00b0b930(human, vehicle, seat)` (seat record and `+0x16c8`), `0x00ac2180(vehicle)`, `0x00ab48f0(vehicle, 0)`, `0x00aa9350(vehicle, human)` (§8.1).
6. **P**: the player block of §8.1 (`0x009dd110`, `0x009e7b70`, `0x00607ea0`, followers).
7. **D**: `0x00ad5460` ×2; **A**: `0x00b2baf0` (drunk driver).
8. Character controller (§8.1).
9. Entry door (§8.1).
10. Human `+0xdc`/`+0xe8` bits.
11. **A**, request `+0x10` = 0: `override_speed` off.
12. **P** → steps 12a–12f; otherwise (NPC) → 12g.
    - (a) Vehicle-AI goal; `0x00986920(human, 1, 0)`; `0x00ab9ea0`.
    - (b) **L**: tutorial (§8.1).
    - (c) `0x007f0140(1)`.
    - (d) **Camera `0x00b0bc10(human, vehicle, f, 0, f, g)`**, f = request `+0x23` bit 3 (forced), g = request `+0x23` bit 0 (§8.6).
    - (e) **Holster** (§8.4); `0x005ffaf0(inventory, 0)`; `0x009bc270`; `0x009e1c40`.
    - (f) Vehicle `+0x16c0` bit `0x10` set: `0x0069b260(1, &vehicle handle, 1.0)` and `0x00710e40(0x6d)`.
    - (g) NPC: holster (§8.4), stop the ragdoll, `0x00986920(human, 0, 0)`.
13. Hijack bookkeeping (§8.1).
14. **D**: `0x00aa2310(vehicle)`.
15. Linked seats' timers.
16. Class 1, **D**: `0x00ab4680(vehicle, 0)`.
17. **A** (human): the vehicle-AI passenger block (§8.1); `0x004e4130(human, 0x19)`; the AI-action completion.
18. **L**: `0x00ab6cf0(vehicle, 1)` under its conditions.
19. **A** (human), class bit `0x21`, or `0x00921060` with class bit `0x22`: a three-value report.
    - The report object is looked up in a container: under vehicle `+0xbf8` → `+0xb0` with key 7; under player `+0x1f00` with key `0x16`; or under a non-player's `+0x1da8` → `+0xf8` with key `0x17`.
    - It is filled with `0x00a353d0(human)`, `0x00a3a140(vehicle)` and the seat as a float (`0x00e0cfb0`, `0x00e0ce20`).
    - HYPOTHESIS: a statistics or dialogue event.
20. Instant entry and human `+0x33` bit `0x08`: snap the seat pose.
21. **`0x00942110(human, seat = 0 ? 8 : 9, 0, −1.0)`** — the condition row.
22. `0x00986920(human, 0, 0)`; human `+0xccc` = 0.
23. **L** and vehicle `+0x33` bit `0x08`: `0x007f1930(0x00a794a0(vehicle))`. Role only (HUD band).
24. **D**: team and driver handle (§8.1).
25. **P**: `+0x16c0` bit `0x100000` (**D**), `never_go_ambient`, `0x00935130`.
26. **L**: cheat force flags; the manual-look hold timer `0x012e41a4` = 500 ms; `0x0056b0e0(1)`.
27. Human `+0xe0`/`+0x3a` (§8.1).
28. Human `+0x1b10`, `+0x1b08`/`+0x1b0c`; `0x004b7f70`.
29. **Set-ride(record, i, `0x22`, 1)** at `0x00afcdec` (§8.5).
30. `0x0094f450(human, 1, 0)` (collision filter). Class bit `0x21`: `0x00abcd50(vehicle, 1)`, and for the local player `0x00ad35b0(vehicle, 1)`. If vehicle `+0x16c4` bit `0x20`: `0x0094f3e0(human, 1)`.
31. `0x00ab9c20(vehicle, seat, 1)`; `0x00a90c70(vehicle, 0)`.
32. **Completion callback** request `+0x3c` (cdecl, six arguments), then cleared.
33. **Team check**, skipped when any of these holds: request `+0x22` bit 6, the seat's `Ignore Team Disposition` flag (seat byte `+0x09` bit 4), or the human's team is 0, 7 or 9.
    - Otherwise the commit looks for any other occupant (`0x00ab5070`) whose team `+0x1ca8` differs.
    - If it finds one, it builds an exit request (`0x004dccd0`; exit-request `+0x30` bit 0, instant, copied from request `+0x20` bit 0; §6.2), calls **`0x00af3cd0(human, exit request)`** and **returns**, skipping steps 34–36.
34. Human `+0xe4` bit 4 and `0x00ad3820`: `0x008ccb90(human, 0, 1, 1)`. The seat's `Hide Occupant` flag (seat byte `+0x09` bit 6): `0x00920170(human, 0)` and slot `+0x58` |= 8. State `0x15` undoes it with `0x00920170(human, 1)` (§6.1). **What these two calls actually do at the render level, and how rarely shipped data sets `Hide Occupant` (2 of 131 vehicles' driver seats — tanks only), is now traced in full: `spec-render-pipeline.md` §23.14.**
35. Slot `+0xf4` timer; human `+0xe4` bit 4 (not owned); forced flag call.
36. Human `+0xe4` |= `0x1000`, `+0xec` bit `0x100000` cleared, `0x00bd8240(human, vehicle)`, human `+0x19bc` = −1.

**Callers.**
- The setter's case 9 (`0x00afe7b2`) follows the commit with the network send, gated on *flag* and phase 3. After step 33 the phase is 2, so no seat snapshot is sent then.
- The per-frame handlers of states 7 and `0x13` call the commit directly (`0x00affd47`, `0x00b01aa6`). Each first logs `"Set Enter"` with state 9 and then repeats the send itself — an inlined copy of the setter's case 9.

### 8.3 The vehicle-band callees

The census lists **39** distinct callees in `0x00a70000`–`0x00af0000`, which confirms scoping §5a.2's count.
- **Predicates, 18:** class `0x00ad2f80`; seat count `0x00ab4af0`; seat occupants `0x00ab5070`/`0x00ab5150`; remote control `0x00a77a10`; type tests `0x00ad3140`, `0x00ad3160`, `0x00ad31d0`, `0x00ad3200`, `0x00ad3220`, `0x00ad3270`, `0x00ad3290`, `0x00ad32d0`, `0x00ad33a0`, `0x00ad3590`, `0x00ad3820`, `0x00ab6cd0`, `0x00aef8b0`. Four of these are now individually resolved (2026-10-07, disassembly): `0x00ad3140` = vehicle
  special-type sub-field (`+0xbf4`→`+0x4c4`) `== 14` (Tank); `0x00ad3270` = `Vehicle_Type`
  (`+0xbf4`→`+0x2c`) `== 1` (Motorcycle); `0x00ad3290` = same field `== 5` (Watercraft); `0x00ad31a0`
  (cited elsewhere in this table, not in this specific list) = same field `== 3 or 4` (Helicopter or
  VTOL) — enum order `Automobile`=0, `Motorcycle`=1, `Airplane`=2, `Helicopter`=3, `Vtol`=4,
  `Watercraft`=5, pinned from two already-landed cross-references in `spec-lua-api-behaviour.md` and
  `spec-vehicle-data.md` §5.
- **The other 21 write the vehicle state** listed in §8.1:
  - `0x00ac2180`, `0x00ad5460`, `0x00ab6cf0`, `0x00ad35b0`, `0x00ab48f0`, `0x00ab4680`, `0x00aa2310`;
  - the door group `0x00aa2080`, `0x00a88e70`, `0x00a88f00`, `0x00a88eb0`, `0x00aa1f60`;
  - `0x00ab9c20`, `0x00ab9ea0`, `0x00abcd50`, `0x00a90c70`, `0x00aa9350`;
  - the force flags `0x00a7e040`, `0x00a7b690`, `0x00a7b520`;
  - and `0x00a794a0`, the HUD lookup.
- **Outside that band,** the vehicle-AI object takes seven more: `0x00b2baf0`, `0x00b2e510`, `0x00b24cc0`, `0x00b25c40`, `0x00b29cf0`, `0x00b4d260` and its siblings, and `0x0092cdb0` (all identified by their `vehicle_ai` force-flag strings).

**The exit finaliser `0x00b03d80` (state `0x19`) calls the same routines** (§14, OPEN 53): `0x00ab9c20`, `0x00abcd50`, `0x00ad35b0`, `0x00ad3590`/`0x0094f3e0`, the door group, the force flags, `0x00607ea0` (with (3, −1, 2) against the commit's (3, 5, 2)), `0x00b85fa0`, and `0x00ac2280` beside the commit's `0x00ac2180`. HIGH that these are the commit's undo pairs. U6.6 should check them against this list.

### 8.4 The weapon/item calls — holstering

**Player path** (`0x00afc431`…`0x00afc554`; CONFIRMED by listing):

1. w = the item in hand: `0x00941f40(player)` = `0x005fe3e0(inventory +0x1b78, 0)`, which resolves inventory `+0x90`.
2. If there is no item, slot `+0x40` = 0 and the steps below are skipped.
3. Usable u = (item definition `+0x19c` → `+0x4c` = `0x16`) or `0x00ac1a80(vehicle, seat)`. The seat test fails when:
   - the seat's bit is set in descriptor `+0x8b4`; or
   - descriptor `+0x870` bit `0x200000` is set and the seat is not 0; or
   - bit `0x100000` is set and the seat is above 1.
4. Slot `+0x40` = the id of the item in hand (definition `+0x5c`).
5. Not usable: **`0x00600720(inventory, 0, 1, 0, 0)` — select inventory slot 0** (HIGH: empty-handed; `spec-ai-runtime.md` reads the same routine's (13, 1, 0, 0) as "draw a weapon").
6. Usable, but `0x00b869b0(item definition, player, seat = 0, vehicle handle)` refuses the item in a vehicle: select slot `0xe`, or slot 0 when player `+0x28ac` bit 4 is set.
   - `0x00b869b0` accepts type `0x16` unconditionally.
   - It refuses an item whose definition `+0x14` bit 4 is set.
   - Otherwise it consults the vehicle descriptor `+0x874` bit 4 and the seat.
7. Then `0x00b85fa0(player, w)` gives an animation-set id. That id is applied to the animation controller (`0x004b2140`), a second time when w `+0xe8` is non-zero.
8. Finally `0x005ffaf0(inventory, 0)` runs.

**Non-player path:** when the human is owned here, holds an item, and is not excused by `0x0052a230`, it selects slot 0 unless both the seat test and `0x00b869b0` pass.

**Ride on-entry** (§8.5): whatever is still in hand is attached (`0x009215b0`) to one of four rig attach points.
- The points are words `+0xb4`/`+0xb6`/`+0xb8`/`+0xba` of the human's rig record `+0xf4`.
- `0x00942180` picks `+0xba`/`+0xb6`; `0x009421d0` picks `+0xb8`/`+0xb4`.
- Within each pair, the item definition's `+0x14` bit `0x8000000` selects the first word.
- `0x009421d0` is used when the definition's `+0x14` bit `0x800` is set, or when the seat's `Hold Weapon in Left Hand` flag is set (seat byte `+0x0A` bit 3, tested by `0x00af5390`). So `+0xb4`/`+0xb8` are left-hand points and `+0xb6`/`+0xba` right-hand ones (HIGH).

**So the scoping HYPOTHESIS "holstering" holds:**
- an unusable item is put away (slot 0);
- the held item's id is remembered in slot `+0x40`. The finaliser reads it back, looks it up (`0x005feaa0`) and re-selects it through the same `0x00600720` (§6.11). Holster and restore are therefore a pair;
- an item kept in hand is re-attached for in-vehicle use.

What inventory slots 0 and `0xe` are, and what `0x00600720`'s other arguments mean, are open (OPEN 64).

### 8.5 The ride state

**Set-ride** only dispatches (§7.5).

**On-entry `0x00b099d0`** (75; CONFIRMED):
1. Resolve through `0x00b098e0`.
2. **Human `+0x16d4` = 3, slot phase = 3, state = `0x22`.**
3. If an item is in hand, attach it (§8.4).

That is all. Everything else seated-related was done by the commit.

**Per-frame `0x00b09ab0`** (320; CONFIRMED), every frame in state `0x22`, through `0x00b0a0c0`:

1. **Resolve** human, vehicle and slot. Without a human, it stops here.
2. **Velocity components.** With a vehicle: v = `0x00abae00(vehicle)` (HIGH: its linear velocity). f = v · forward row (vehicle `+0x64`…`+0x6c`); u = v · up row (`+0x58`…`+0x60`). Without a vehicle, f = u = 0.
3. **Step and noise.** Δt = `0x00756790()`, the physics-quantised step (§4.3). n = `0x00dab6a0(−r, r)` with r = `VI_random_accel_factor` (`0x0288ca94`).
4. **Forward lag.** Error `+0xec` = f − `+0xe8`. If the timer `+0xf0` has expired, `+0xe8` moves toward f by min(\|error\|, (`VI_forward_vel_accel` `0x0288ca9c` + n) · Δt).
5. **Vertical lag.** Error `+0xe4` = u − `+0xe0`. With the same timer test, `+0xe0` moves toward u by min(\|error\|, (`VI_vert_vel_accel` `0x0288ca98` + n) · Δt).
6. **Reaction delay.** If the timer is unarmed, or both \|errors\| are below 0.05 (`0x012a2ee0`), re-arm it to a random delay `0x00dab660(VI_reaction_delay_min_ms 0x0288ca90, VI_reaction_delay_max_ms 0x0288ca8c)`.
   - Effect: the lag values hold still for a random reaction time after settling, then chase the vehicle's motion.
   - HIGH that they drive the occupant's body sway in the pose driver (U6.8; the reader was not traced).
7. **Parameter ids.** Look up once (guard bits in `0x0288caf4`) the animation-parameter ids `"Collision Bool"` → `0x0288caf0` and `"Collision Dir"` → `0x0288caec` (`0x004cbaf0(name, 1)`).
8. **Collision parameters.** Only with a vehicle and an animation controller whose `+0x24` byte is set:
   - (a) `Collision Bool` = false (`0x004b1410`).
   - (b) Then, only if vehicle `+0x1640` & 7 (HIGH: a contact this frame): d = (vehicle `+0xc24` − v.x, vehicle `+0xc2c` − v.z), the horizontal velocity change against the stored value.
   - (c) If \|d\|² > `VI_collision_min_vel_diff`² (`0x0288ca88`): `Collision Bool` = true, and `Collision Dir` = the wrapped difference (`0x00dae190`) between d's bearing (`0x00dae5b0`, an atan2) and the human's heading (`0x00da3910` on `+0x4c`).

The six tweaks are registered by name at `0x0100aa50`…`0x0100aaff` (registrars `0x0071b580` for floats, `0x0071b530` for the two millisecond values). They are bss: no initialiser was found in the image (OPEN 63).

`review/scoping-player.md` §1.7 named this routine the `Collision Bool`/`Collision Dir` writer; that is CONFIRMED, and those two names belong to `spec-anim-runtime.md`'s parameter census.

### 8.6 The camera hand-off `0x00b0bc10(human, vehicle, cancel-blend, no-gunner-test, snap, place-mode)`

381 instructions; 10 callers (raw scan): AI mode 43's tick, `0x0060c3f0`, vehicle code `0x00a93070`/`0x00a93320`, enter states `0x0b`, `0x0c` and `0x0f`, `0x00b01460`, exit `0x1d`, and the commit. It acts only when the human is the local player.

It drives U4's machinery and does not duplicate it; the camera-side behaviour of every callee is in §4. The routine runs (CONFIRMED):

1. If `0x009feeb0()`: `0x009fef60()` (the sprint step, §3.6.2).
2. **Camera target = the vehicle** (`0x00573500`, §4.2).
3. **Submode choice.** This mirrors the vehicle chooser `0x00573110` (§4.8), but runs at hand-off time.
   - (a) Unless the fourth argument is set: if `0x0056da70(human, vehicle, …)` reports a gunner view, request submode 7 `vehicle aim`.
   - (b) Else a tank (`0x00ad3140`): 11 `tank driver`, or 42 `rc tank`.
   - (c) Else by class through the jump table `0x00b0c104`:
     - class 2: 10 `airplane driver` / 41 `rc airplane`;
     - classes 3 and 4: 6 `helicopter driver` / 40 `rc helicopter`;
     - class 5: 5 `watercraft driver` / 39 `rc watercraft`;
     - any other class: reset the selectable driver view to 3 (`0x00575750(3)`, §4.8), then 3 `vehicle driver` / 38 `rc vehicle`.
   - (d) The choice is requested through `0x005753e0` (§4.7 step 6, §4.12; it honours `skip_camera_transition`).
4. Third argument set: cancel the transition blend (`0x00580420`, §4.2).
5. **Gunner seat** (human `+0x16c8` ≠ −1 and the gunner view chosen): `0x0056cd80` with the angles from `0x0056da70`. If the seat's descriptor entry `+0x9c4` + `0x10`·seat has bit 0 set, the camera target becomes the human instead.
6. **Otherwise** (steps 6a–6c):
   - (a) Take the vehicle's camera angles (`0x00a77500`). When vehicle `+0x1a3c` is non-null, add half the vehicle's bounding-box height and depth (its own vtable `+0x60`) to the two values.
   - (b) Pass the descriptor's **`Primary_Camera_Angle`** set — or **`RC_Camera_Angle`** for a remote-controlled vehicle — to `0x0056cf60`: the look-at vector at `+0x628`/`+0x668` and the floats at `+0x63c`/`+0x640` resp. `+0x67c`/`+0x680`.
   - (c) Pass the **`Secondary_Camera_Angle`** set (`+0x648`, floats `+0x65c`/`+0x660`) to `0x0056cd10`.
   - These are the three sets §4.12 found unreferenced by the camera band itself (§14 OPEN 28).
7. `0x0056b230()`.
8. **Placement**, unless `0x0056b200()` holds or a gunner view was chosen:
   - activity state `0x00614d00()` = `0xf` → placement through `0x0063e030`, else as below;
   - the sixth argument set → `0x0056d310` (sub-state 2's preset placer, §4.7 step 6) with the vehicle's handle and the globals `0x0288cb04`/`0x0288cb08`;
   - otherwise → `0x00574c50` (sub-state 1) along the vehicle's forward row with the distance `0x0288cb00`.
9. **Snap** when the fifth argument is set, the descriptor has `skip_camera_transition` (`+0x870` bit `0x8000`), or the vehicle is remote-controlled: sub-state 3 (`0x00574b30(3)`), one follow update with a zero step (`0x00577cb0(0.0)`), then sub-state 0.

The commit passes (human, vehicle, forced, 0, forced, request `+0x23` bit 0). So a forced entry cancels any transition blend and snaps; an ordinary entry blends. The commit's other camera writes are the manual-look hold timer and `0x012e4030` bit 0 (§8.1).

### 8.7 What a port must reproduce (U6.4)

1. The end state of §8.1 — at minimum the stub list there — in the order of §8.2. Two orderings matter: set-ride comes after the condition-row change, and the team check comes after the callback.
2. The ride per-frame of §8.5: the two parameters and the body-lag model with its reaction delay. Its tweak values must come from the original tweak data, which was not located (OPEN 63).
3. The camera hand-off of §8.6, implemented on top of §4's submode and sub-state machinery.

### 8.8 Corrections and cross-spec flags raised by U6.4 (no other spec edited)

- **`review/scoping-player.md` §5a.5:** the cheap stub's end state is §8.1. It must include the condition row `+0xcc8` = 8/9, `+0x16c8`, the seat record and the camera, not only `+0x16d4` and the vehicle handle. **§1.7 bullet 1:** the hand-off is the condition-row change (U2 §3.0 item 4), not a bit `0x005ac620` sets from `+0x16d4`. **§5a.2:** `0x00b4d260` belongs to the vehicle-AI object, not the weapon band (§8.4). The commit's 39 vehicle callees are confirmed (§8.3).
- **This file, §4.12 and §12 OPEN 28:** the camera-angle sets are read by the vehicle-interaction hand-off `0x00b0bc10` (§8.6). §4.12's "not referenced by the camera band" stays true of the camera band itself. **§4.8:** the hand-off's class table maps class 2 to the airplane submodes, 3/4 to the helicopter ones and 5 to the watercraft ones (§8.6 step 3). **§12 OPEN 39:** the commit clears animation-controller `+0x1a4` bits `0x0800`–`0x4000` through `0x004b7f70`. **§12 OPEN 17 (drunk effect):** the driver's drunkenness is applied to the vehicle as the `drunk_driver` multipliers (§8.1).
- **`spec-lua-api-behaviour.md` §38.1 (`vehicle_engine_start`):** that section's HYPOTHESIS "classes 2/3/4 = car/helicopter/VTOL" conflicts with the camera class table — class 2 gets the airplane submodes, 3 and 4 the helicopter ones, and ordinary cars fall into the default branch. **§18.20/item 23:** a driver entering overwrites the vehicle's team `+0x16e0` with its own and keeps the old value in `+0x16e1` (§8.1).
- **This file, §6 (U6.6):**
  - the commit's two exit-request sites — step 33's team check, and `0x00b24cc0`'s eviction of non-allied occupants for a player — are §6.3.3's "U6's own band" and "Other" rows;
  - the vehicle-side callees of OPEN 53 pair with the commit's (§8.3);
  - the weapon restore pairs with §8.4's holster.
- **`spec-anim-runtime.md` (parameter census):** `Collision Bool` and `Collision Dir` are written only in the ride state (§8.5).
- **`spec-ai-runtime.md`:** `0x00600720` with first argument 0 is the vehicle holster (§8.4), which complements its (13, 1, 0, 0) reading. A seated human's AI action `0x25`…`0x27`/`0x31` is completed by the commit (§8.1).

---

## 9. U6.1 — The element-selection rule

**Scope.** Unit U6.1 of `review/scoping-player.md` §5a.3: the rule that turns a slot's state and variant fields into a token key and finds the matching `Element` of the seat's animation set. Roots: the key builder `0x00b0a6f0`, the matcher `0x00b0a650`, the lookup `0x00b0ad10`, the counting sibling `0x00b0ac60`, and `0x00b0a510`. The side map `0x00b0a5f0` and the random pick `0x00dab660` are covered where the rule uses them. The 83-token vocabulary at `0x0130e638` is read in full. The rule is validated against every `Element` of the shipped `vi_enter.xtbl`, `vi_exit.xtbl` and `vi_ride.xtbl`. What the callers do with the animation they get back belongs to U6.2/U6.5/U6.7. The slot fields are cited by offset with the meanings §7.2 gives them.

**Method.** Ghidra-free. Capstone listings over `tools/harnesses/exe_va.py`: the five roots, `0x00b0a580`, `0x00b0a5f0`, `0x00dab660`, the table loader `0x00b07920`, the ride matcher pair `0x00b08aa0`/`0x00b08b80` and the callers `0x00af9580`, `0x00afa0d0`, `0x00afa540`, `0x00afaf90`, `0x00afb220`, `0x00b02900` and `0x00b0eb90` were read in full; the other callers were read at their call sites. Also used: a raw `E8`/`E9` rel32 scan of `.text` for every call site of the seven rule functions (it agrees with the census caller lists); a whole-image dword scan for absolute references (none); the census closure of every state handler; and static reads of the jump tables `0x00b0abe4`, `0x00b0ac44`, `0x00b0a554` and `0x00b0a628` and of the token table. The validation re-implements the key builder from its listing and runs it over the three tables (`scratchpad/u61/keyval.py`, `keyval2.py`, over `tools/as_base_xtbl/`). No Ghidra project was opened or copied. No `.czn_pc` data was touched.

### 9.0 Results up front (U6.1)

1. **The rule is three functions:** the **key builder `0x00b0a6f0`** (slot → five tokens), the **wildcard matcher `0x00b0a650`** (first `Element` whose first five tokens equal the key, where a key token `any` matches anything), and the **lookup `0x00b0ad10`**, which picks the seat's enter set (phase 1) or exit set (any other phase) and returns the `Element`'s animation or −1. `0x00b0ac60` is the same lookup in counting mode. It has 23 call sites in 16 functions (CONFIRMED; raw rel32 scan = census). The builder is called only by `0x00b0ad10` and `0x00b0ac60`. **[confirms scoping §5a.3's counts]**
2. **`0x00b0a510` is not a matcher.** It is a 23-instruction **seat-index validator** (0…7 unchanged, anything else −1). The second matcher is **`0x00b0a580`**, an **exact six-token** compare used only by the ride side (`0x00b08aa0` ← `0x00b08b80`), not by this rule. **[corrects scoping §5a.1 item 7 and §5a.3]** (§9.1)
3. **The key reads more fields than scoping listed:**
   - `+0x0c` (state) selects the case;
   - `+0x10` and `+0x14` hold the variants;
   - **`+0x1c`** gives the **surface** token: −1 `none`, 0 `air`, 1 `ground`, **2 `vehicle`**, 3 `water`;
   - **`+0x20`** gives the **side** token through `0x00b0a5f0`: 0…5 = `left`, `right`, `front`, `front left`, `front right`, `back`;
   - `+0x24` gives `prepare lift`/`prepare pull`;
   - **`+0x7d`** (request `+0x21`) bits 2, 3 and 6 give `traverse no door` and `enter convertible`;
   - **`+0xd1`** (exit request `+0x31`) bit 0 gives the `convertible` exit variants.

   **[corrects scoping §5a.4 point 2's "`+0x0c`, `+0x10`, `+0x14`, `+0x24`, `+0x7d`"]** (§9.3)
4. **`Ride_Animations` is never read by this rule.** So **`vi_ride.xtbl` is outside it by construction**: 0 of 454 ride `Element`s can be produced, and none needs to be. Ride `Element`s are six-token keys matched exactly by `0x00b08aa0`. All 454 fit that layout (§9.6.4).
5. **Validation (CONFIRMED, empirical):**
   - **`vi_enter.xtbl`: 657 / 657 `Element`s** can be produced by the key of some enter state.
   - **`vi_exit.xtbl`: 808 / 813** can be produced by an exit state, so this is a full-key check, beyond §6.12's first-token check. **The 5 that cannot** (`Bike Rocket Rider` ×1, `Bulldozer Right` ×1, `BRUTE` ×3) have a third token that the builder never pairs with their second token. They can never be chosen (§9.6.2).
   - Every key that matches nothing returns −1. The `Couldn't find animation …` text is formatted into a stack buffer and **thrown away**; nothing is printed or logged (§9.4.2).
6. **First match wins.** Within one set, 5 `vi_enter` and 6 `vi_exit` `Element`s repeat an earlier `Element`'s five-token key. **4 of the `vi_exit` ones carry a different animation that this rule can never return**: `Heli Fighter Rider`'s `Exit Left Fast`, and `RiotCop_Right`'s three alternatives to `Exit Right Extracted Pull` (§9.6.3).
7. **The rule is also run on temporary slots.** Four callers build a slot record on their own stack, with a state the occupant is not in, to ask whether an animation exists:
   - seat scoring (`fast`, state `0x13`);
   - the hijack chooser probing the victim (state `0x1d`);
   - the failed-hijack pick (state `0x1e`, exit set);
   - the next-state chooser (state `0x0e`).

   The chooser also hand-builds one key itself. **So the builder's `0x1e` case is live, not dead**: the 15 `extract fail` `Element`s of `vi_exit.xtbl` are reachable only through it. **[corrects §7.5 "Element-key cases for unheld states"]** (§9.5)
8. **Short keys.** For some field values the builder writes only 3 or 4 tokens, and the matcher still compares 5. It then reads **uninitialised stack words** from the caller's key buffer. One such case is reachable in the shipped game:
   - a state-`0x1d` exit whose exit request `+0x20` is still −1;
   - with the shipped data it always misses, and the caller then falls back to the instant exit `0x17` (§9.4.3).
9. **The vocabulary is exactly 83 entries.** All are read from the pointers at `0x0130e638`. The 84th dword is `0x0000ffff`, not a string, and a different string list follows it (§9.2). This settles the OPEN in `spec-tables-vehicle-world.md` §2.1.
10. **Size:** the five roots own 559 instructions (`0x00b0a6f0` 326, `0x00b0ad10` 90, `0x00b0a650` 65, `0x00b0ac60` 55, `0x00b0a510` 23), plus the side map `0x00b0a5f0` (19). That is scoping's ~0.6k.

### 9.1 The functions (CONFIRMED by full listing)

| Address | Size | Role | Convention and behaviour |
|---|---|---|---|
| `0x00b0a6f0` | 326 | **key builder** | Register call: `esi` = slot, `ecx` = key buffer (dwords). Writes 3–5 tokens and returns their count in `eax`. Both callers ignore the count. §9.3. |
| `0x00b0a650` | 65 | **wildcard matcher** | cdecl `(set, key, &counter, &out)`. A null set gives *out* = −1 and leaves *counter* alone. Otherwise, for each `Element` of the set in order, compare positions 0–4: a key token of 1 (`any`) skips the position; anything else must equal the `Element`'s token. On a match: if *counter* = −1, *out* = the `Element`'s `Animation` (`+0x18`) and return; otherwise *counter* += 1 and continue. At the end, *out* = −1. **Position 5 is never compared.** |
| `0x00b0a580` | 47 | exact matcher (**ride only**) | cdecl `(set, key, &out)`. The first `Element` whose six tokens all equal the key gives *out* = its `Animation`, otherwise −1. No wildcard. Its only caller is `0x00b08aa0` (3 sites). |
| `0x00b0ad10` | 90 | **lookup** | cdecl `(info record, slot, request, &exit seat, quiet)`. Build the key, use counter = −1 (first match), choose the set (§9.4.1), match. On a miss with *quiet* = 0, format the diagnostic (§9.4.2). Returns the animation or −1. |
| `0x00b0ac60` | 55 | **count** | cdecl `(info record, slot, request, &exit seat)`. Same set choice; counter = 0, so it returns the **number of matching `Element`s** and no animation. One caller: `0x00afaf90`. |
| `0x00b0a510` | 23 | **seat validator** | `(seat)`: 0…7 → unchanged; anything else (including −1) → −1 (table `0x00b0a554`, 9 entries). It is the lookup's enter-seat path; also used by `0x00afe0d0`. |
| `0x00b0a5f0` | 19 | **side map** | `(slot +0x20)`: −1 → `none`; 0 → `left` (`0x3a`); 1 → `right`; 2 → `front`; 3 → `front left`; 4 → `front right`; 5 → `back` (`0x3f`); any other value → `none` (table `0x00b0a628`). Called once, from the builder. |

The counter protocol of `0x00b0a650` would allow choosing the n-th match (counter = −(n+1)). **No caller uses it**: the direct calls pass −1 (`0x00b0ad10`, `0x00afb220`) or 0 (`0x00b0ac60`). Among `Element`s with the same key, the first one in the file always wins.

### 9.2 The token vocabulary `0x0130e638`

83 pointers, read one by one (CONFIRMED). The loader `0x00b07920` compares every `ParameterN` against entries 0…`0x52` with `_stricmp`; a string that matches nothing is stored as 0 (`none`), as `spec-tables-vehicle-world.md` §2.1 says. The 84th dword (`0x0130e784`) is `0x0000ffff`. A separate list (`request pending`, `start`, `wait for exit exterior`, `wait for exit interior`, `teleport`, `pathfind`) begins at `0x0130e788`; it is not part of the vocabulary.

| Group | Index, token (index order) |
|---|---|
| wildcard | `0x00` `none`, `0x01` `any` |
| action (first token) | `0x02` `activate`, `0x03` `deactivate`, `0x04` `change seat`, `0x05` `close`, `0x06` `enter`, `0x07` `exit`, `0x08` `extract`, `0x09` `open`, `0x0a` `prepare`, `0x0b` `traverse`, `0x0c` `fast` |
| variant (second/third token) | `0x0d` `prepare lift`, `0x0e` `prepare pull`, `0x0f` `open standard`, `0x10` `open shady`, `0x11` `open shopper`, `0x12` `traverse standard`, `0x13` `traverse no door`, `0x14` `enter standard`, `0x15` `enter convertible`, `0x16` `enter shopper`, `0x17` `enter guardian angel`, `0x18` `extract standard`, `0x19` `extract punch`, `0x1a` `extract bash`, `0x1b` `extract kill`, `0x1c` `extract no door`, `0x1d` `extract convertible`, `0x1e` `extract kick`, `0x1f` `extract door slam`, `0x20` `extract fast`, `0x21` `extract fail`, `0x22` `exit standard`, `0x23` `exit convertible`, `0x24` `exit fast`, `0x25` `exit at speed`, `0x26` `exit dive`, `0x27` `exit at altitude`, `0x28` `exit eject`, `0x29` `exit climb`, `0x2a` `exit crawl`, `0x2b` `exit guardian angel`, `0x2c` `extract fail 1`, `0x2d` `extract fail 2`, `0x2e` `extract fail 3`, `0x2f` `exit fast outward`, `0x30` `exit fast forward`, `0x31` `exit fast backward`, `0x32` `exit at speed cruise`, `0x33` `exit at speed fast`, `0x34` `exit dive convertible`, `0x35` `exit fast convertible` |
| surface | `0x36` `water`, `0x37` `ground`, `0x38` `air`, `0x39` `vehicle` |
| side and seat | `0x3a` `left`, `0x3b` `right`, `0x3c` `front`, `0x3d` `front left`, `0x3e` `front right`, `0x3f` `back`, `0x40` `passenger`, `0x41` `hostage a`, `0x42` `hostage b`, `0x43` `hostage c` |
| motion (ride) | `0x44` `rocker`, `0x45` `reverse`, `0x46` `stopped`, `0x47` `forward`, `0x48` `forward fast`, `0x49` `wheelie`, `0x4a` `stoppie`, `0x4b` `turn left`, `0x4c` `turn right` |
| combat (ride) | `0x4d` `combat ready`, `0x4e` `aim forward`, `0x4f` `aim back`, `0x50` `aim left`, `0x51` `aim right`, `0x52` `alternate` |

This agrees with scoping §5a.6's grouping, which split entries 0–53 differently. The key builder emits only entries `0x00`–`0x3f`. Entries `0x40`, `0x47`, `0x4b` and `0x4c` are written by the ride pair (§9.6.4). `0x41`–`0x43` (`hostage a`/`b`/`c`), `0x44`, `0x49` and `0x4a` are written by neither and occur in none of the three shipped tables.

### 9.3 The key builder `0x00b0a6f0` (326 instructions, CONFIRMED)

**Step 1 — action and variants (tokens 0–2).** Switch on state − `0x0a` through `0x00b0abe4` (24 entries). Any state outside `0x0a`…`0x21` goes to the default case `0x00b0ab73`, which writes `none`, `none`, `none`.

| State | Token 0 | Token 1 | Token 2 |
|---|---|---|---|
| `0x0a` | `prepare` | `prepare lift` if `+0x24` = 2, else `prepare pull` | `none` |
| `0x0b` | `traverse` | `traverse no door` if `+0x7d` bit 2 is set and bit 3 is clear, else `traverse standard` | `none` |
| `0x0c` | `open` | `+0x14`: −1 → `open standard`, 2 → `open shady`, 3 → `open shopper`; **other → short key** | `none` |
| `0x0d` | `extract` | `+0x10` 1…8 → `extract standard`, `punch`, `bash`, `kill`, `no door`, `convertible`, `kick`, `door slam`; **other → `none`, short key** | `none` |
| `0x0e`, `0x1e` | `extract` | `extract fail` | `+0x14`: 0 → **`any`**, 5/6/7 → `extract fail 1`/`2`/`3`; **other → short key** |
| `0x0f` | `enter` | `+0x10` = 0 → `enter guardian angel`; else `+0x14` = 4 → `enter shopper`; else `+0x7d` bit 6 → `enter convertible`, clear → `enter standard` | `none` |
| `0x10`, `0x20` | `close` | `none` | `none` |
| `0x11`, `0x1b` | `change seat` | `none` | `none` |
| `0x12` | `activate` | `none` | `none` |
| `0x13` | `fast` | `none` | `none` |
| `0x14`–`0x19` | `none` | `none` | `none` (the default target; §6.12) |
| `0x1a` | `deactivate` | `none` | `none` |
| `0x1c` | `open` | `open standard` | `none` |
| `0x1d` | `extract` | `+0x10` `0x0a`…`0x11` → `extract standard`…`extract door slam` (the `0x0d` list, offset by 9); `0x13` → `extract fast`; **other (including `0x12` and −1) → `none`, short key** | `none` |
| `0x1f` | `exit` | `+0x10`: `0x14` → `exit standard` (`+0xd1` bit 0 → `exit convertible`); `0x15` → `exit fast`; `0x16` → `exit at speed`; `0x17` → `exit dive`; `0x18` → `exit at altitude`; `0x19` → `exit crawl`; `0x1a` → `exit climb`; `0x1b` → `exit eject`; `0x1c` → `exit guardian angel`; **other → short key** | see the next table |
| `0x21` | `traverse` | `none` | `none` |

Token 2 for `exit` (state `0x1f`):
- **`exit fast`:** `+0xd1` bit 0 → `exit fast convertible`. Otherwise `+0x14` 9/`0x0a`/`0x0b` → `exit fast outward`/`forward`/`backward`. **Any other `+0x14` → short key.**
- **`exit at speed`:** `+0x14` `0x0c`/`0x0d` → `exit at speed cruise`/`fast`. **Otherwise short key.**
- **`exit dive`:** `+0xd1` bit 0 → `exit dive convertible`, otherwise `none`.
- **every other kind:** `none`.

**Step 2 — surface and side (the last two tokens).**
- The surface token comes right after the last variant token:
  - **`any` for states `0x11` and `0x1b`** (change seat matches every surface);
  - otherwise slot `+0x1c` through `0x00b0ac44`: −1 → `none`, 0 → `air`, 1 → `ground`, 2 → `vehicle`, 3 → `water`, any other value → `none`.
- The side token follows: `0x00b0a5f0(+0x20)` (§9.1).

**Short keys.** "Short key" above means the variant step stopped after token 0 or 1. The surface and side then land at positions 1–2 or 2–3, and the builder returns 3 or 4. **The remaining positions are not written.** Both callers pass an uninitialised stack buffer, and the matcher always compares five positions (§9.4.3).

**Fields read** (offsets are slot-relative; meanings as in §7.2):

| Field | Used for | Written by (from the listings read here) |
|---|---|---|
| `+0x0c` | the case | each on-entry handler (§7.5); the temporary slots of §9.5 |
| `+0x10` | extract kind (`0x0d`: 1…8; `0x1d`: `0x0a`…`0x11`, `0x13`); guardian angel (`0x0f`: 0); exit kind (`0x1f`: `0x14`…`0x1c`) | `0x00afa540` (the enter-side extract kind); `0x00b02900` (the victim's, from exit request `+0x20`); `0x00b04ae0` (exit kinds `0x15`…`0x18`, `0x1b`, `0x1c` as immediates) |
| `+0x14` | open variant (`0x0c`); extract-fail pick (`0x0e`/`0x1e`); `enter shopper` (`0x0f`: 4); exit-fast and at-speed sub-variants (`0x1f`) | `0x00afa0d0` (request `+0x21` bit 4 → 2, bit 5 → 3); `0x00afaf90`; `0x00b04ae0` (`0x0c`, `0x0d`) |
| `+0x1c` | surface | the state-1 hub `0x00afd4c0` (`0x00afd591` = 3, `0x00afd5a8`); `0x00b02900` (from the hijacker's slot); `0x00b057b0`; exit `0x15`/`0x17` (§6.1) |
| `+0x20` | side | the upkeep `0x00af5cb0` (§7.6); `0x00b02900` (from exit request `+0x24`); `0x00b04ae0` |
| `+0x24` | `prepare lift`/`pull` (`0x0a` only) | the upkeep (0); state `0x0a`'s per-frame (§7.2) |
| `+0x7d` | request `+0x21`: bit 2 + bit 3 → `traverse no door`; bit 6 → `enter convertible` | the request's submitter (§7.4) |
| `+0xd1` | exit request `+0x31` bit 0 → `exit convertible`, `exit fast convertible`, `exit dive convertible` | not found here (OPEN 69) |

`+0x18` is **not** read.

### 9.4 Matching and lookup

#### 9.4.1 Which set, which seat (`0x00b0ad10`, `0x00b0ac60`)

- **Slot phase 1 (enter):** the set is info record `+0x5c` + `0x24`·seat, i.e. `Seat_Info` `+0x18` **`Enter_Animations`** (`spec-tables-vehicle-world.md` §3.2). Seat = `0x00b0a510(request +0x18)`.
- **Any other phase:** `+0x60` + `0x24`·seat, i.e. `Seat_Info` `+0x1c` **`Exit_Animations`**. Seat = slot `+0x2c` in state `0x1b` (change seat), otherwise `*`(the caller's exit-seat pointer). That value is **not** validated.
- **`Ride_Animations`** (`+0x20` → record `+0x64`) is read only by `0x00b08aa0`.

The info record is vehicle descriptor `+0xa7c`. Callers pass it as `vehicle +0xbf4 → +0xa7c`.

**Guard quirk (CONFIRMED):** an enter seat of −1 indexes `Seat_Info` −1, i.e. record `+0x38`, which lies inside the 64-byte `Name`. The exit path has the same quirk with any out-of-range seat. Only a non-null pointer there would be dereferenced as a set (OPEN 72).

#### 9.4.2 The miss path

With *quiet* = 0 and no match, `0x00b0ad10`:
- looks up all five key tokens in `0x0130e638`;
- formats `"\nCouldn't find animation for %s -> %s -> %s -> %s -> %s\n\n"` (`0x01186848`) through `0x00ea454a` into a 160-byte stack buffer.

**Nothing reads the buffer afterwards.** The function returns −1 either way. Every caller treats −1 as "no animation"; what that leads to (skip, another state, the instant fallback) is the caller's business (§9.5).

#### 9.4.3 Short keys in practice

| Case | Reachable in the shipped game? |
|---|---|
| `0x0c`, `+0x14` ∉ {−1, 2, 3} | No. The on-entry handler `0x00afa0d0` sets −1, 2 or 3 only. |
| `0x0d`, `+0x10` ∉ 1…8 | Only through the override global `0x0130e5f4` (−1 in the image; 13 references, writers `0x00af9419`…`0x00af954f`, HIGH: debug commands; OPEN 68). |
| `0x0e`/`0x1e`, `+0x14` ∉ {0, 5, 6, 7} | Only if a set had more than three `extract fail` `Element`s for one surface and side, because `0x00afaf90` picks `+0x14` from 5…N+4 (§9.5). All 12 shipped groups have exactly three (§9.6.3). |
| `0x1d`, `+0x10` = −1 | **Yes.** A `0x1d` exit is chosen by exit-request `+0x30` bit 3 (§7.5), and the request constructor leaves `+0x20` = −1 (§6.2). Only the hijack chooser fills it in (§9.5). The key is then `extract`, `none`, surface, side, ⟨stack word⟩. No shipped `extract` `Element` has `none` as token 1, so it always misses. `0x00b02900` then sets exit-request `+0x30` bit 0 and goes to the instant exit `0x17`. |
| `0x1f` with an unlisted kind or an unset sub-variant | Depends on U6.7's writer `0x00b04ae0`. It writes kinds `0x15`–`0x18`, `0x1b`, `0x1c` as immediates and others from a register. |

On a short-key miss with *quiet* = 0, the diagnostic also indexes `0x0130e638` with the uninitialised words (§9.4.2). That is an out-of-bounds pointer read in the original. **A port must zero the key buffer**, and that matches the shipped behaviour whenever the original misses.

### 9.5 Call sites — what each passes

23 call sites of `0x00b0ad10` in 16 functions, 1 of `0x00b0ac60`, and 1 direct call of `0x00b0a650` (CONFIRMED). "Real slot" means the occupant's own slot in its current state; "temporary" means a slot record built on the caller's stack (constructor `0x00af2e40`, or the copy `0x00af2b80`).

| Caller | Sites | Reached from | Slot passed and what it varies |
|---|---|---|---|
| `0x00af9720` | 1 | enter `0x0a` on-entry | real |
| `0x00af97e0` | 1 | enter `0x0b` on-entry | real |
| `0x00afa0d0` | 1 | enter `0x0c` on-entry | real; `+0x14` from request `+0x21` bits 4/5 |
| `0x00afa540` | 5 | enter `0x0d` on-entry `0x00afae20`, enter `0x13` on-entry `0x00afd290` | **hijack chooser.** On the real slot, `+0x10` = each candidate extract kind in turn: the override, request `+0x21` bit 1, and kinds 5, 1, 8, 7, 2, 3, 4 through `0x00af9580`. Request `+0x21` bit 6 picks kind 6 without a lookup. On a **temporary copy of the victim's slot** (state `0x1d`, `+0x10` = `0x0b`/`0x0c`/`0x0d`), it drops candidates 2/3/4 (punch, bash, kill) when the victim's exit set has no `extract` `Element` for kind + 9. It picks among the rest with `0x00dab660(0, n−1)` and sends the victim an exit request with `+0x20` = kind + 9 (or `0x13` = `extract fast` when request `+0x23` bit 0 is set) and `+0x24` = the side |
| `0x00af9580` | 1 | `0x00afa540` | real; sets `+0x10` = candidate, looks up, restores `+0x10` = 1 |
| `0x00afaf90` | 2 + count | enter `0x0e` on-entry | real slot with `+0x14` = 0 (`any`): **count** N, then `+0x14` = `0x00dab660(5, N+4)`, then look up the hijacker's animation. Then a **temporary phase-2, state-`0x1e` slot** with the same `+0x14`, `+0x1c` = 1 (ground) and the hijacker's `+0x20`, which looks up the **victim's** `extract fail` animation in the exit set. N = 0 would make `0x00dab660` divide by zero (it computes r mod (hi − lo + 1)); the next-state chooser normally rules it out (next row) |
| `0x00afb220` | 1 + 1 direct | enter `0x06` on-entry; per-frame of `0x0a`–`0x0d` | **next-state chooser** (§7.5). (a) A **hand-built key** `open` / `open standard` / `none` / `ground` / `none`, matched directly on the enter set with counter −1. It decides between `0x0c` (open, only on a match) and `0x0b` (traverse). (b) A **temporary phase-1, state-`0x0e` slot** with `+0x14` = 5 and the real `+0x1c`/`+0x20`: found → `0x0e`, otherwise `0x0d` |
| `0x00afd0a0` | 1 | enter `0x0f` on-entry | real |
| `0x00af99b0` | 1 | enter `0x11` on-entry (also called by `0x03`'s per-frame) | real; key token 3 is `any` |
| `0x00b01460` | 1 | enter `0x13` on-entry and per-frame, exit `0x1d` on-entry | real |
| `0x00b02330` | 1 | exit `0x1b` on-entry (also called by `0x16`'s per-frame) | real; exit seat = slot `+0x2c` |
| `0x00b02900` | 2 | exit `0x1d` on-entry | real; `+0x10` = exit request `+0x20`, `+0x20` = exit request `+0x24`, `+0x1c` from the hijacker's slot; on a miss → instant `0x17` (§9.4.3) |
| `0x00b04ae0` | 2 | exit `0x1f` on-entry `0x00b05560` | real; `+0x10`/`+0x14` = the exit kind |
| `0x00b057b0` | 1 | exit `0x21` on-entry (also called by `0x1f`'s per-frame) | real |
| `0x00b063c0` | 1 | exit `0x1f` per-frame | real |
| `0x00b0eb90` | 1 | seat scoring (U6.2; ← `0x00b0fdf0`, the upkeep `0x00af5cb0`, `0x005457f0`) | **temporary phase-1, state-`0x13` slot** (`fast`), `+0x1c` = 1 (ground), `+0x20` = `0x00b0c5e0`'s side, *quiet* = 1, exit-seat pointer null. Asked only when its eighth argument is non-zero. A seat with no `fast` animation from that side is skipped |

**Which states consult the rule** (census closure of every handler, stopping at the setters and other states' handlers; CONFIRMED):
- **Enter:** `0x0a`, `0x0b`, `0x0c`, `0x0d`, `0x0e`, `0x0f`, `0x11` and `0x13` on entry, and `0x06` (through the chooser).
- **Exit:** `0x1b`, `0x1d`, `0x1f` and `0x21`.
- **Through the submitter only:** states 1, 5 and 6 and several per-frame handlers reach it through `0x00afe980` → the submitter → seat choice → `0x00b0eb90`. That is seat scoring, not their own element choice.
- **Never:** `0x00`–`0x04`, `0x07`–`0x09`, `0x14`–`0x19` (§6.12), `0x1c` (`0x00b023f0`, `0x00b024f0`), and the unheld `0x10`, `0x12`, `0x1a`, `0x1e`, `0x20`.

**`0x1e` is used only as a key**, through `0x00afaf90`'s temporary slot.

### 9.6 Validation against the shipped tables

`scratchpad/u61/keyval.py` re-implements §9.3 and enumerates the builder's whole output. It covers every state `0x0a`…`0x21`; `+0x10` and `+0x14` over −1…`0x20`; `+0x24` ∈ {2, other}; all eight combinations of `+0x7d` bits 2/3/6; `+0xd1` bit 0; every surface and side value. It then matches every shipped `Element` with `0x00b0a650`'s rule: a key token `any` matches anything, and an uninitialised key position matches nothing. Enter states are paired with `vi_enter.xtbl` and exit states with `vi_exit.xtbl`, as §9.4.1 requires. **Loader semantics are reproduced exactly:** `Parameter1`, `Parameter2`, … are read by name until the first absent one, and unknown strings become 0. No shipped `Element` has a gap or an unknown string. **Every `vi_enter`/`vi_exit` `Element` has exactly 5 tokens and every `vi_ride` `Element` exactly 6.** So no position is left to the allocator, and none reaches the loader's unbounded seventh write.

#### 9.6.1 Coverage

| Table | Sets | `Element`s | Producible by the builder | Not producible | Contain `any` |
|---|---|---|---|---|---|
| `vi_enter.xtbl` | 106 | 657 | **657 (100 %)** | 0 | 0 |
| `vi_exit.xtbl` | 99 | 813 | **808 (99.4 %)** | 5 | 0 |
| `vi_ride.xtbl` | 69 | 454 | **0** (by construction, §9.4.1) | 454 | 0 |

Matches per state (an `Element` can match keys of more than one state):
- **enter:** `0x0a` 12, `0x0b` 30, `0x0c` 64, `0x0d` 197, `0x0e` 36, `0x0f` 164, `0x11` 54, `0x13` 100;
- **exit:** `0x1b` 33, `0x1d` 176, `0x1e` 15, `0x1f` 568, `0x21` 16.

The exit first-token tally (`exit` 573, `extract` 191 = 176 + 15, `change seat` 33, `traverse` 16) agrees with §6.12 item 3. Token positions in shipped data:
- `vi_enter`: token 3 is `ground` 610, `vehicle` 35, `water` 11, `none` 1; token 4 is a side or `none`.
- `vi_exit`: token 3 is `ground` 614, `air` 129, `water` 38, `vehicle` 32.

#### 9.6.2 The five `vi_exit` `Element`s no key can reach

| Set | Tokens | `Animation` | Why |
|---|---|---|---|
| `Bike Rocket Rider` | `exit` / `exit standard` / `exit fast outward` / `ground` / `right` | `Exit Right Rider` | `exit standard` always takes token 2 = `none` |
| `Bulldozer Right` | `exit` / `exit fast` / `none` / `ground` / `none` | `Exit Right Fast` | `exit fast` takes token 2 from {`exit fast outward`/`forward`/`backward`/`convertible`}. Otherwise the key is short: the surface lands in position 2 and the side in position 3, so position 3 can never be `ground` |
| `BRUTE` (×3) | `exit` / `exit fast` / `none` / `ground` / `back`, `left`, `right` | `exit back rider`, `Exit Left Rider`, `Exit Right Rider` | same |

These are dead data in the shipped build (OPEN 70).

#### 9.6.3 First-match shadowing, the extract-fail groups and the hand-built key

- **Shadowed `Element`s:** an `Element` whose five-token key repeats an earlier `Element` of the same set.
  - `vi_enter`: **5** shadowed, all with the same `Animation` as the first, so nothing is lost: `medium passenger right`, and the four `VTOL2` seat sets.
  - `vi_exit`: **6** shadowed, 2 identical. **4 carry a different animation that the rule can never return:**
    - `Heli Fighter Rider`: `Exit Left Fast`, shadowed by `Exit Right Fast` on `exit` / `exit fast` / `exit fast outward` / `ground` / `none`;
    - `RiotCop_Right`: `Exit Right Extracted Bash`, `… Shoot` and `… Punch`, all shadowed by `Exit Right Extracted Pull` on `extract` / `extract standard` / `none` / `ground` / `none`.
  - So 652 of 657 enter `Element`s and 802 of 813 exit `Element`s can actually be returned.
- **`extract fail` groups** (`vi_enter.xtbl`, grouped by set, surface and side): **12 groups, each exactly `extract fail 1`/`2`/`3`**. So N = 3 and `0x00afaf90` picks `+0x14` ∈ {5, 6, 7}: never a short key. `vi_exit.xtbl` has the same 15 (5 each).
- **The chooser's hand-built key** `open` / `open standard` / `none` / `ground` / `none` matches **48 `Element`s in 48 sets**, the whole `open standard` population. The 8 `open shady` and 8 `open shopper` `Element`s are reached only by `0x0c`'s own lookup.

#### 9.6.4 `vi_ride.xtbl` against the ride matcher (structure only)

`0x00b08aa0(seat in eax, vehicle, key, &out1, &out2, &out3)` matches a **six-token key exactly** (`0x00b0a580`) against `Ride_Animations` three times: as given, then with token 2 = `turn left`, then `turn right`; it restores token 2 = `none` at the end. `0x00b08b80` (← `0x00b08ea0`, `0x00af4760`) retries while any result is missing, each step on top of the last:
1. token 5 cleared;
2. tokens 3 and 4 cleared;
3. token 1 = `forward`;
4. token 0 = `passenger`.

After that it falls back to built-in animation ids `0x10a`…`0x111`.

The shipped table fits this layout exactly:

| Position | Tokens |
|---|---|
| 0 | `passenger` 452, `none` 2 |
| 1 | `stopped`, `forward`, `reverse`, `forward fast` |
| 2 | `none` 444, `turn left` 5, `turn right` 5 |
| 3 | `combat ready` 306, `none` 148 |
| 4 | an `aim …` token or `none` |
| 5 | `alternate` 38, `none` 416 |

2 `Element`s are shadowed (identical `Drive Ready SM`). **Who builds the ride key, and so whether every ride `Element` is reachable, is not this unit's** (OPEN 71).

#### 9.6.5 Keys that match nothing

For every state, the fully determined keys that match no `Element` of the table that state searches:
- **heads that never match:**
  - enter `close` (`0x10`) and `activate` (`0x12`);
  - exit `none` (`0x14`–`0x19`), `deactivate` (`0x1a`), `open standard` (`0x1c`) and `close` (`0x20`);
  - `exit climb` (`0x1f` with `+0x10` = `0x1a`).
- **every other head** matches at least one `Element` for some surface and side.

Each of these keys falls through to −1 and the discarded diagnostic (§9.4.2). Of them, only the `exit climb` key can ever be looked up, because the other states never consult the rule (§9.5).

### 9.7 What a port must reproduce

1. **The key builder of §9.3, literally.** That includes the variant precedence in `0x0f` (guardian angel, then shopper, then convertible) and `0x0b`'s two-bit rule, the `any` surface for `0x11`/`0x1b`, the `any` third token for `0x0e`/`0x1e` with `+0x14` = 0, and the surface and side maps. **Zero-fill the unwritten positions of a short key.**
2. **First-match, five-position matching with the wildcard on the key side only** (§9.1). An `Element` that itself says `any` is not a wildcard. Position 5 is ignored by this rule; it matters only for ride.
3. **Set choice by phase** (§9.4.1): enter set in phase 1, exit set otherwise, with the change-seat exception for the seat.
4. **The temporary-slot lookups of §9.5** (seat scoring, hijack probing, failed-hijack pick, next-state chooser). They decide which states and seats are entered, not only which animation plays.
5. **The miss is silent.** Do not reproduce the diagnostic's out-of-bounds reads (§9.4.3).

### 9.8 Corrections and cross-spec flags raised by U6.1 (no other spec edited)

- **`review/scoping-player.md`:**
  - §5a.1 item 7, §5a.3: `0x00b0a510` is the seat validator, not a matcher. The rule's only matcher is `0x00b0a650`; `0x00b0a580` is the ride side's exact matcher (§9.1).
  - §5a.4 point 2: the key also reads `+0x1c`, `+0x20` and `+0xd1`, and the later tokens are not all "from slot fields" — tokens 1–2 come from `+0x10`/`+0x14`/`+0x24`/`+0x7d`/`+0xd1`, and tokens 3–4 from `+0x1c`/`+0x20` (§9.3).
  - §5a.4 point 2's "every shipped `Element` tuple should be producible": true for `vi_enter` (657/657); false for 5 of 813 in `vi_exit`; not applicable to `vi_ride`, which this rule never reads (§9.6).
  - §5a.1 item 7's "the animated states are exactly enter `0x0a`–`0x13` and exit `0x1a`–`0x21`": the states that consult the rule are enter `0x0a`–`0x0f`, `0x11` and `0x13` and exit `0x1b`, `0x1d`, `0x1f` and `0x21`. Of the rest, `0x10`, `0x12`, `0x1a`, `0x1e` and `0x20` are never held (§7.5), and `0x1c`'s handlers never reach the rule. Enter state 6 queries the data through the next-state chooser (§9.5).
  - §5a.6: the vocabulary groups are wildcard 0–1, action 2–12, variant 13–53, surface 54–57, side/seat 58–67, motion 68–76, combat 77–82 (§9.2).
- **This file, §6 (U6.6):**
  - §6.12 items 1 and 3 are confirmed; the full-key check is §9.6.
  - **OPEN 48** — the key builder maps `+0x1c` = 2 to `vehicle` and 3 to `water`, against the hypothesis "0/1/2 = airborne/ground/water". Exit `+0x10` is the exit kind, and `0x17`/`0x18` are `exit dive`/`exit at altitude`. That is why the finaliser's airborne branch (§6.11 step 11, and the "OPEN" in that step) keys on them.
  - **OPEN 47** — exit-request `+0x20`/`+0x24` are the victim's extract kind and side, written by the hijack chooser `0x00afa540` and copied to slot `+0x10`/`+0x20` by `0x00b02900`.
  - **OPEN 58** — the handlers of `0x1a`, `0x1c` and `0x20` never reach the rule.
- **This file, §7 (U6.0):**
  - §7.5 "Element-key cases for unheld states": **the `0x1e` case is live** through `0x00afaf90`'s temporary slot (§9.5). The other four are dead as stated.
  - §7.2: request `+0x21` (slot `+0x7d`) bits read here are 2, 3 and 6 (and bits 1, 4, 5 by the on-entry handlers of §9.5), not bit 0. Slot `+0x1c` is the surface for **enter** keys too, written by the hub `0x00afd4c0`, not only an exit landing kind. Slot `+0x20` is the side index 0…5 of §9.1. Request `+0x23` bit 0 marks a `fast` entry (HIGH; `0x00afa540` maps it to `extract fast`, and seat scoring sets it on its temporary request).
- **This file, §8 (U6.4):** ride `Element`s are chosen by `0x00b08b80`/`0x00b08aa0` (§9.6.4), which §8 does not cover (OPEN 71).
- **`spec-tables-vehicle-world.md`:**
  - **§2.1:** the "entries 2–82 OPEN until the data range is dumped" is answered by §9.2. The "no-match stores 0" rule and the 83 bound are confirmed.
  - **§2.2:** the counts 106/657, 99/813 and 69/454 are reproduced independently by this pass. Every shipped `Element` has exactly 5 (`vi_enter`, `vi_exit`) or 6 (`vi_ride`) `ParameterN`, with no gaps.
  - **§3.2:** `Enter_Animations`/`Exit_Animations` are read by `0x00b0ad10`/`0x00b0ac60`/`0x00afb220`; `Ride_Animations` only by `0x00b08aa0`.
  - §2's prose uses auto-generated names for `0x00b07c40`, `0x00b07920` and the array globals; this file cites them as plain hex.

---

## 10. U6.3 — The enter route: states 0–8, the routing hub and the instant route

**Scope.** Unit U6.3 of `review/scoping-player.md` §5a.3: the enter states 0–8 (approach, the routing hub, the waits, the arrival router, the cancel) and how an "instant" request reaches the seating commit. The 15 roots are the on-entry handlers of states 0–8 and the per-frame handlers of states 0, 2, 3, 4, 5 and 7 (census own size 3,812 instructions). The helpers owned here are the entry-point queue and seat-claim functions (§10.1) and the cancel-and-resubmit routine `0x00afe980` (§10.4.3). State 9 is U6.4's (§8) and is only reached here; the animated states `0x0a`–`0x13` are U6.5's and are characterised only as far as the routing question needs (§10.4.4). Slot, request and record offsets are §7.2's; states and setters are §7.5's. Network replication stays parked (§7.7); where it touches the routing it is recorded.

**Method.** As §7–§9, Ghidra-free: capstone listings over `tools/harnesses/exe_va.py` with an esp-depth tracker (every stack operand resolved to the resolver's output slots), census call edges, a band sweep of `0x00af0000`–`0x00b12000` for every read and write of the instant bit and of request `+0x08`/`+0x30`/`+0x34`, raw image scans for the tweak globals, static reads of the condition table `0x0111f1e8` and of the submitter wrapper's jump table `0x00b10a68`, and the shipped `tweak_table.xtbl` (`tools/ai_base_xtbl/`). **Read in full:** the 15 roots (`0x00af9a90`, `0x00afd4c0`, `0x00af95d0`, `0x00af9640`, `0x00afabd0`, `0x00af9dd0`, `0x00afb630`, `0x00af96b0`, `0x00afb930`, `0x00afeb30`, `0x00afeca0`, `0x00afed10`, `0x00afeeb0`, `0x00aff1a0`, `0x00affc10`); the helpers `0x00afe980`, `0x00b0cb20`, `0x00b0b460`, `0x00b0b3f0`, `0x00b0b4c0`, `0x00b0c120`, `0x00af1a00`, `0x00b0efe0`, `0x00b0a330`, `0x00b0baf0`, `0x00b0b9a0`, `0x00afb170`, `0x00b0a170`, `0x00b11520`, `0x00ab5070`, `0x00ab5150`, `0x00ab51a0`, `0x00aa2370`, `0x00aa1ff0`, `0x00aa1fc0`, `0x00985d80`, `0x00993d30`, `0x00993d50`, `0x009b5690`, `0x009b9230`, `0x009b92e0`, `0x0097ea20`, `0x00ad2fa0`, `0x00b500f0`, `0x00d9fc60`, `0x0056dc20`, `0x00da1330`; for the routing question also `0x00afd0a0` (state `0x0f` on-entry), the exits of `0x00b01080`, the network applier `0x00af2950`, the resync sender `0x00af3550` and the submitter wrapper `0x00b10900`. `0x00993e90` (435) was read for its interface and exits only. Other callees are cited by role. No Ghidra project was opened or copied. No `.czn_pc` data was touched.

### 10.0 Results up front (U6.3)

1. **The hub `0x00afd4c0` routes on request flags, not on geometry** (§10.3). In order:
   - **instant** (request `+0x20` bit 0) → **2** when the seat has an occupant, **4** when it is free, **8** when request `+0x08` = 1 or a tank's seat holds a player;
   - else request `+0x23` bit 0 (the fast-entry request) → **`0x13`**;
   - else **forced** (`+0x23` bit 3) or an **animation override** (request `+0x30` ≠ −1) → **`0x0f`**;
   - else → **5**, the walk to the vehicle.

   The target set {2, 4, 5, 8, `0x0f`, `0x13`} of §7.0 item 7 is CONFIRMED, with nothing missing. Every path leaves state 1 inside the same call, so **state 1 is never held across a frame**.
2. **The instant-route HYPOTHESIS is refuted** (§10.4). `0x0f` is not the instant route. An instant request goes **0 → (upkeep) 1 → 4 → 9**, or 1 → 2 → 4 → 9 when the seat is occupied:
   - the upkeep skips entry-point choice for it (entry = seat);
   - the hub evicts the occupant with an instant exit request;
   - state 4 commits as soon as the human is at the head of its entry-point queue.

   With a free seat and an empty queue the commit runs **inside the first phase tick after the submission** (§7.6). `0x0f` is the "play the enter animation in place" route for forced and override entries. Its on-entry goes straight to 9 only when no `Enter` animation can be found (§10.4.4).
3. **A second snap route exists: `0x00afe980`, cancel-and-resubmit-as-instant** (§10.4.3). It cancels the slot (state 8) and submits a fresh request with the instant bit set. The approach states fall back to it when they cannot proceed: navigation failure, the vehicle not instantiated, and the state-6 chooser's answer 8.
4. **State 8 is a one-shot cancel that releases the slot inside its own on-entry handler** (§10.9). That is why it has no per-frame handler. It undoes the hub's claims, queue entry, door, hat and condition row, and returns the camera to the human. **It never calls the completion callback `+0x3c`**: a cancelled entry is reported only through the `+0x34` callback, and only if the hub had not yet run.
5. **The states:**

   | State | Role |
   |---|---|
   | 0 | requested; locks movement (condition row `0xa`); stick abort |
   | 1 | the hub |
   | 2 | instant: wait for the seat to empty |
   | 3 | change seat: wait for the target seat |
   | 4 | instant: take the seat |
   | 5 | walk to the entry point |
   | 6 | arrival router into the animated states |
   | 7 | **dead** |
   | 8 | cancel |

   States 1, 6 and 8 are transient.
6. **OPEN 60 is resolved for state 7, and the same evidence closes it for exit `0x18`/`0x1c`** (§10.8). No local code produces 7, and no packet can carry it. The senders transmit only enter states 0, 1, 6, 8 (`0x00afa400`'s 13 call sites), `0x22` seat snapshots and exit states `0x14`/`0x15`. The applier turns a received `0x22` into a fresh instant entry at state 0, never into 7.
7. **Request `+0x08` is a request kind**, not a flag (§10.10):
   - −1 is the default;
   - **0 is approach only**: walk to the door, report through `+0x34`, cancel;
   - **1 is "remove the occupant, then stop"** (also the deferred cancel of §6.4.2);
   - **2 is a normal entry** from the wrapper `0x00b10900`.
8. **The stick abort exists twice**, and only for the local player:
   - in state 0, after an 800 ms grace and never for instant requests;
   - in state 5, after an 800 ms grace from the start of navigation, unless player `+0x28ac` bit `0x02` is set.

   Any non-zero walk-axis value cancels (§10.2, §10.6).
9. **The instant bit is set at 7 sites and tested at 12** (§10.4.1). This corrects scoping §5a.5's "three places". Two sites in the hub force it: an entry seat with no `Enter_Animations`, and a missing or uninstantiated vehicle.
10. **Defect (CONFIRMED in code): the entry-point queue is ordered by squared distance from the world origin.** The reference vector is the never-written bss vector `0x029cdb98`, not the entry point (§10.1). Separately, `0x00afe980`'s resubmission keeps none of the original request's callbacks (§10.4.3).
11. **The approach tweaks were located with their shipped values:**
    - `VI_pathfind_cancel_dist_multiplier` 1.5;
    - `VI_pathfind_check_pause_distance` 2.5;
    - `VI_pathfind_play_stop_dist` 1;
    - `VI_on_vehicle_entry_max_y_diff` 0.3.

    The same shipped `tweak_table.xtbl` holds **the six ride tweaks of OPEN 63**, which are resolved (§10.6, §10.13).

### 10.1 Seat claims and the entry-point queue (helpers owned here)

**Seat record readers** (vehicle seat record `+0x1340` + `0x30`·seat, §7.3). Each returns the handle resolved as a human through `0x004d7f30(…, 1)`, or 0 when the seat index is out of range (vehicle `+0x824` seats when the body is active, else 8). All CONFIRMED:
- `0x00ab5070(vehicle, seat)` reads `+0x18`, **the seated occupant**;
- `0x00ab5150` reads `+0x00`, the **claim**;
- `0x00ab51a0` reads `+0x08`, the pending handle.

**Claim and enqueue `0x00b0cb20(vehicle, human, seat, entry)`** (220; only caller the hub). Steps (CONFIRMED):
1. The human becomes the seat's claim if the claim is empty; otherwise it becomes the **next claim** (`+0x10`).
2. It is inserted into the record's queue for the entry seat's `Queue` (record `+0x10` + `0xd0`·`Queue`; `Queue` is `Vehicle_Interaction_Info` seat sub-record `+0x0C`, §7.1).
3. Human `+0x16c8` = seat and `+0x16cc` = entry.

The insertion position:
- **appended** when seat = entry, or when the queue is empty;
- otherwise **inserted before** the first live queued human that either is farther away by the measure below or is heading for the entry seat itself (its `+0x16c8` = entry). So humans passing through a seat go ahead of the seat's own claimants.

**The distance is `0x00da1330` (squared distance) from the human's controller position to the bss vector `0x029cdb98`, not to the entry point.** `0x029cdb98` is zero in the image, has 555 readers and no store in `.text` (raw store-encoding scan), so it is the shared zero vector (HIGH). **The queue is therefore ordered by distance from the world origin.** CONFIRMED as code; the runtime value is HIGH; **defect HYPOTHESIS** (OPEN 74).

**Queue queries** (CONFIRMED):
- `0x00b0b460(vehicle, entry)` returns the **queue head**'s handle, or the null handle;
- `0x00b0b3f0(human, vehicle, entry)` asks "is the human the queue head";
- `0x00b0b4c0(vehicle, human, entry)` returns the human's 1-based position, or −1.

**Other helpers** (CONFIRMED):
- `0x00af1a00(handle)`: the character is in phase 1, state 5.
- `0x00b0c120(vehicle, human, occupant, seat)`: true when the occupant is not dead (`0x0096f4f0`), the seat is 0, and vehicle `+0x16e8` is not already this human. Each caller then **records the hijack**: vehicle `+0x16e8`/`+0x16ec` = the human's handle and `+0x16c0` |= `0x10`. That is the bookkeeping §8.1 reads back.
- `0x00b0efe0(human, &position, &direction)`: the target of the human's chosen entry point (slot `+0x20`, `+0x24`, through `0x00b0eac0`); −1 without a slot.
- `0x00b0a330(human, vehicle, flag)`: the **approach radius** — 15.0 for a tank (`0x00ad3140`) or when *flag* is set, else 12.0. It is 12.0 regardless when the human passes `0x0094f690` and `0x00ad39d0(vehicle, 0, 1)` holds.

### 10.2 State 0 — requested

**On-entry `0x00af9a90`** (259; CONFIRMED by full listing; the store and the phase-boundary writes are §7.8):
1. Human `+0x16d4` = 1; slot phase 1, state 0, `+0x10`/`+0x14`/`+0x18` = −1; human `+0x16c0`/`+0x16c4` = the vehicle handle.
2. Unless request `+0x21` bit 0 (skip seat resolution): request `+0x10`…`+0x1c` = −1.
3. **Local player only** (`0x0091f5e0`), with p = `0x009da4e0()`:
   - call `0x007e2b30()`;
   - leave fine aim (`0x009da830` → `0x009dcf10`) and scope view (`+0x1d95` bit `0x10` → `0x009e6510(p, 0)`);
   - p `+0x2054` = −1;
   - `0x0056dc20(0.0)`, which clears the camera's look-at object when it is the camera target (camera band, role HIGH);
   - p `+0x1d97` bit `0x10` cleared (a sprint style, §3.7);
   - the animation-facing velocity `+0x203c` zeroed (`0x00d9fc60`).
4. Cancel what the human is doing:
   - reloading (`0x009ba8b0`) → `0x009c07a0`;
   - skydiving (`0x009adb00`) → `0x009ae150(human, 0)`;
   - a held weapon that passes `0x00b81440` → `0x009c7cc0` (the driving-request helper of `spec-ai-runtime.md`).
5. Instantiated human (`+0x33` bit `0x08`):
   - instant → `0x00957c80(human, 0.2, 0, 0, 0)`;
   - otherwise, for each of the animation states `0x2fb`, `0x2fc`, `0x2fd` and `0x1d8f` that is active (`0x004b4ce0`): `0x00957ee0(human, id, 0.2, 0, −1, 0)`, then `0x004b7f70` on the controller. HYPOTHESIS: these end those states with a 0.2 blend; §6.9 uses the same call to play `0x1d95`.
6. **`0x00942110(human, 0xa, 0xff, −1.0)`: condition row `0xa`.** Its slots (static read of `0x0111f260`) are:
   - classify `0x005ab240`;
   - **no move slot**;
   - the standard look slot `0x005ab6a0`.

   So **the human cannot walk but can look** while the entry is pending. Row `0xb`, which exit state `0x15` sets (§6.7), has the same three slots.
7. Disarm the record timer (`0x00af2660`), so the 250 ms upkeep runs on the next tick.
8. **Arm slot `+0x84` (request `+0x28`) to 800 ms** — the stick-abort grace.
9. `0x0094fa00(human)`; human `+0xec` bit `0x100000` cleared.

**Per-frame `0x00afeb30`** (120; CONFIRMED). Only when the occupant is the local player (`0x0091faf0`), the 800 ms timer has expired, and the request is **not** instant:
- A = |`0x005bb530(2, 0)`| and B = |`0x005bb530(3, 0)`| (`CAA_WALK_FORWARD_BACKWARD` and `CAA_WALK_TURN_LEFT_RIGHT`, §1.9);
- **any non-zero value** (compared with 0.0, no dead zone of its own) runs an inlined set-enter 8: the `Set Enter` log, the network send `0x00afa400(record, i, 8)`, then the cancel `0x00afb930`.

Nothing else happens in state 0. The way out is the upkeep (§7.6): → 1, or → 8 when no seat can be found.

### 10.3 State 1 — the routing hub `0x00afd4c0` (976; CONFIRMED by full listing)

In order:
1. Resolve. State 1; `+0x10`/`+0x14`/`+0x18` = −1.
2. **The "begun" callback.** If request `+0x34` is set, call it once (cdecl) with (vehicle handle, occupant handle, request, **1**), then clear it.
3. **Surface** slot `+0x1c` (U6.1's surface token):
   - 3 (`water`) when `0x009b5690` holds (human `+0x1858` ≠ 0; HIGH: swimming);
   - else 2 (`vehicle`) when request `+0x23` bit 2 is set;
   - else 1 (`ground`).

   Request `+0x23` bit 2 is written by the upkeep's entry-point chooser `0x00af5840`. HIGH: an entry from on top of a vehicle (§10.6).
4. `0x00b11520(vehicle)`: when the vehicle is owned here, release the object it holds at `+0xc40` (role only; HYPOTHESIS: a towed or carried object). Then, when the vehicle is owned here, `+0xc72` bit `0x10` is set and `0x00ab6210(vehicle handle)` is 0: `override_speed` off (`0x00b29cf0`, as §8.1).
5. Animation controller `+0x34` = `0x004b16c0(record +0xea4)` (role only). Human `+0xa50` (16 bytes) and `+0xa68` = 0.
6. **Local player: clear the way.** While the queue head of the seat's queue (`0x00b0b460(vehicle, request +0x10)`) exists, is **not** a live player-class object, and is in state 5 (`0x00af1a00`): cancel its entry (`0x00af2460(head, 1)`) and look again. The player thus pushes walking NPCs out of its queue.
7. **Claim and enqueue**: `0x00b0cb20(vehicle, human, request +0x10, request +0x18)` (§10.1). Then `0x009b92e0(human)` (clears `+0x1790`…`+0x17cf`, `+0x17d0` = 0.0, `+0x17d4` = −1; role only). The entry door: `0x00a88eb0(vehicle, 0x00aa2080(vehicle, request +0x18), 0)`, the counterpart of the commit's `(…, 1)` (§8.1).
8. Combat-ready (`0x00943a20`) → `0x00943960(human, 0, 0)` (clear it). Then `0x00950b70(human, 0.3)` — "player gave input" (§3.4) — and a scripted combat move (`0x009634f0`) → `0x009682d0(human, 0)`.
9. Player-class (`0x00853b30(human, 0x21)`):
   - if inventory `0x005fe3e0(…, 1)` returns an item → `0x009bfbc0(human, 1, 0)`;
   - if `0x005fe460(…, 0)` = `0xa` → `0x009bfbc0(human, 0, 0)` (weapon band, role only).
10. Human `+0xec` bit `0x100000` = NOT `0x0052a230(human)` (the brute test) AND NOT forced. The commit and the cancel clear it again.
11. **Force instant** (CONFIRMED):
    - (a) not instant, and the entry seat's sub-record `+0x18` (`Enter_Animations`, read at descriptor `+0xa7c` → `+0x5c` + `0x24`·request `+0x18`) is NULL → set instant (`0x00afd881`);
    - (b) no vehicle, or vehicle `+0x33` bit `0x08` clear → set instant (`0x00afd897`).
12. Local player with request `+0x22` bit `0x20` → `0x00545010(human)`. Bit `0x20` is cleared in every case.
13. **`Should Hide Big Hats`**: for a player-class human whose seat (`+0x16c8`) has seat byte `+0x0A` bit 7, the human's customization `+0x2088` hides item 5 through `0x009f0860`/`0x00821740`/`0x009f34d0`. The cancel restores it (§10.9).
14. Instantiated human: end animation states `0x305` and `0x325` (`0x004b41b0(controller, id, 0.0, 0)`; role as §3.4).
15. **Routing.** Decision table (CONFIRMED; sites in parentheses):

    | Test, in order | Then | → |
    |---|---|---|
    | **instant** | §10.4.2 steps H1–H4 | **8** (`0x00afda8c`) when request `+0x08` = 1, or for a tank whose entry seat holds a player; else **2** (`0x00afdbf9`) when the seat has an occupant; else **4** (`0x00afdc21`) |
    | request `+0x23` bit 0 | record the occupant as victim (slot `+0x48`, `+0x58` bit 0, hijack bookkeeping) — no eviction | **`0x13`** (`0x00afdd3f`) |
    | otherwise | steps 16–18 | **5** if not forced and request `+0x30` = −1, else **`0x0f`** (shared tail `0x00afe05e` → `0x00afe06c`) |

16. **Vehicle-AI reaction** (vehicle owned here).
    - The record word `+0x08` is used as a **local variable**: it is set to 1 or 2 and **reset to 0 before the hub returns**, so it is 0 whenever any other code can see it (this answers §7.1's "Meaning: U6.3").
    - When the seat is 0, or the seat is 1 and the player is the recorded hijacker in activity state 4 (`0x00614d00()`), the driver d = `0x004d7e00(0x00ab5070(vehicle, 0))` is examined. The vehicle-AI type byte `+0xc68` must not be 3, and `0x00b1f6c0` of it must be false.
    - Then: `0x004e6600(d, human)` → kind 2. Otherwise every occupant on team 7 in AI mode `0x19` (`Passenger`) gets `+0x8d0` |= 2 and condition row 9 (`0x00942110(occupant, 9, 0, −1.0)`), and the kind is 1.
    - Request `+0x20` bit `0x20` also gives kind 1.
    - Kind 1 → `0x00b50180(vehicle +0xc68, human handle)` unless `0x00b1f6c0`; kind 2 → `0x00b464c0(…)` and vehicle `+0x1ac4` armed to 5000 ms.
    - These are role only (vehicle AI). HYPOTHESIS: the driver flees (kind 2) or the vehicle waits for its passengers (kind 1).
17. **Door unlatch** (descriptor `+0x874` bit 0):
    - when the entry has a working door (`0x00aa2370`, both outputs) whose open fraction (`0x00aa1ff0`) is at most 0.1, call `0x00aa1eb0(vehicle, part, 0.1, 0, 1, 0.0, 0.0, 1)`;
    - and **slot `+0x30` = the door part**, which the cancel and the commit reset to −1.
18. Animation state `0x94` active → request `+0x21` |= `0x20`.
19. After set-enter 5/`0x0f` returns: a human owned here with request `+0x08` = 2 and not (AI mode `0xe` with action `0x25`) → `0x004e4130(human, 0x26)` (`human_ai_data`).

### 10.4 The instant route

#### 10.4.1 The instant bit (request `+0x20` bit 0 = slot `+0x7c` bit 0)

**Set at** (band sweep, CONFIRMED):
- the submitter: `0x00af22c8` from character `+0x1c9c` bit 3, and `0x00af2408` when the vehicle has no seat table, §7.4;
- the network applier `0x00af2add` (§10.8);
- the upkeep: `0x00af5e03` (objects not instantiated, §7.6) and **`0x00af615f`, when no entry point can be resolved and request `+0x08` ≠ 1**;
- the hub: `0x00afd881` and `0x00afd897` (§10.3 step 11);
- the resubmission request of `0x00afe980` (on the stack, §10.4.3).

**Read at:**
- the submitter (`0x00af22cc`, `0x00af22ea`);
- the upkeep `0x00af5fa8`;
- state 0's on-entry `0x00af9c45` and per-frame `0x00afeb64`;
- the hub (`0x00afd85f`, `0x00afd9b1`);
- state `0x0a`'s per-frame `0x00afff52`;
- the commit `0x00afcb66` (§8.2 step 20);
- seat choice `0x00b0fdf0` (three sites, U6.2).

That is 12 test sites. The commit also copies the bit into its exit request (`0x00afd060`, §8.2 step 33).

#### 10.4.2 The route, branch by branch (CONFIRMED)

1. **Submit** (§7.4) → set-enter 0, which plays `0x00957c80` instead of ending states (§10.2 step 5). **The stick abort is disabled.**
2. **Upkeep** (next tick, §7.6): an instant slot skips entry-point choice — **request `+0x18` = request `+0x10`** (enter through the seat's own sub-record), slot `+0x20` = −1 — and is sent to set-enter 1. **No seat found → 8.**
3. **Hub** (§10.3 steps 1–14 run first; queueing in step 7 uses entry = seat), then the instant branch:
   - **H1.** o = the seat's occupant. With o and request `+0x08` ≠ 0:
     - slot `+0x48`/`+0x4c` = o's handle and `+0x58` |= 1;
     - hijack bookkeeping (`0x00b0c120`).
     - For a **tank** (`0x00ad3140`): v = the occupant of entry seat request `+0x18`. If v is player-class → **set-enter 8 and return**. Otherwise v is killed (`0x0096fa20(v, human, −1.0, null handle, 0, 0)`, the damage routine of `spec-lua-api-behaviour.md`; HIGH: lethal) and, when owned here, destroyed (`0x00853ea0(v, 0, 0)`).
     - **Any other vehicle**: when this machine owns the entering human (`0x008ae480`), o gets an **instant exit request** (`0x004dccd0`, exit-request `+0x30` bit 0, `+0x28` = −1; `0x00af3cd0`, §6.3).
   - **H2.** Without o (or with request `+0x08` = 0): slot `+0x48` = null and `+0x58` bit 0 cleared. When the seat is 0 and vehicle `+0x16e8` is not this human, clear it and vehicle `+0x16c0` bit `0x10` (forget an old hijack).
   - **H3.** Stop a ragdoll (`0x009a1e10(human, 0)`). Instantiated human → `0x00957c80(human, 0.0, 0, 0, 0)`.
   - **H4.** Request `+0x08` = 1 → **8**; o present → **2**; else → **4**.
4. **State 2** (§10.5) waits, without a time limit, until the seat has no occupant, then → 4. **With request `+0x08` = 0 nobody evicts the occupant** (the hub skipped it), so the wait lasts until the occupant leaves of its own accord.
5. **State 4 on-entry** (§10.5): stop a ragdoll. If the human is the **head of its entry-point queue** (`0x00b0b3f0`): tilt fix, then **set-enter 9 — the commit (§8)**. Otherwise it stays in 4, and **the per-frame handler retries every frame**:
   - it cancels a queue head that is still walking (state 5);
   - once at the head, it evicts any occupant instantly and commits.

**So a free seat with an empty queue commits inside the first upkeep pass after the submission.** Every step from the upkeep's set-enter 1 to the commit is a synchronous call chain.

#### 10.4.3 Cancel and resubmit: `0x00afe980(record, i)` (136; CONFIRMED)

Builds a fresh `0x44`-byte request on the stack (all −1, `+0x20`…`+0x24` and `+0x34`/`+0x3c` 0), then copies from the old one:
- `+0x00` (requested seat), `+0x08` (kind) and `+0x0c` (search mode);
- the dword `+0x20`, **with bit 0 (instant) set and `+0x23` bit 0 (fast) cleared**.

It then runs an inlined set-enter 8 (log, `0x00afa400(record, i, 8)`, the cancel `0x00afb930` — which releases the slot) and **submits the new request** with `0x00af22a0(human, vehicle, &new)`. The human therefore restarts at state 0 as an instant entry and follows §10.4.2.

**Lost on the way:**
- the callbacks `+0x34` and `+0x3c`;
- the forced bit is kept (it is in the `+0x20` dword), but `+0x22`/`+0x24`/`+0x28`…`+0x30` are not.

A script waiting on `+0x3c` is never told that its entry completed through a resubmission (OPEN 73).

**Callers** (11): states 5 and 6 (on-entry), the per-frame handlers of 5, `0x0a`, `0x0b`, `0x0c`, `0x0d`, `0x0f`, `0x11` and `0x13`, and `0x00b01460`.

#### 10.4.4 What `0x0f` is (refutes scoping §5a.5's HYPOTHESIS)

**On-entry `0x00afd0a0`** (167; CONFIRMED):
1. State `0x0f`; `+0x10`/`+0x14` = −1 and `+0x18` = 4.
2. `+0x14` = 4 when request `+0x21` bit `0x20` is set (the `enter shopper` variant, §9.3).
3. **Forced**: `+0x10` = 0 (`enter guardian angel`), and the human's attached `ga_cable` object (`0x00afb170`; the cancel and the commit destroy it) plays animation state `0x1d4` (`0x004b1b90`).
4. The entry door is driven open (`0x00aa1eb0(vehicle, part, 1.0, 0, 1, 0.0, 0.0, 1)`) and the entry seat record's `+0x20` timer is disarmed.
5. **e = request `+0x30`; if −1, e = the `Element` lookup `0x00b0ad10`** (§9.4).
6. With e and an animation controller (`0x00487e80`), the element is played (`0x00af45a0(human, e)`). **Otherwise → set-enter 9 directly** (`0x00afd243`).
7. Unless `0x00b0ae40(human, e)`: the camera hand-off `0x00b0bc10(human, vehicle, 0, slot +0x1c ≠ 2, 0, 0)` (§8.6).

**Per-frame `0x00b01080`** (exits only, U6.5's):
- when the animation ends: → **9** when the entry seat is the destination (request `+0x18` = `+0x10`, `0x00b012d6`); → `0x11` when the destination is free; → 3 otherwise (all through the set-enter at `0x00b0131d`);
- `0x00afe980` when the vehicle is gone.

**So:**
- `0x0f` is reached only from the hub, for forced or override requests, and from the state-6 chooser;
- it plays the `enter` animation **at the human's current position**, with no walk;
- the direct jump to 9 is a **fallback for a missing animation**, not an instant path;
- **instant requests never reach `0x0f`**: the hub tests the instant bit first, and a forced entry resubmitted by `0x00afe980` is instant.

### 10.5 States 2, 3 and 4

**On-entry of 2, 3 and 7** (`0x00af95d0`, `0x00af9640`, `0x00af96b0`; 36 each; CONFIRMED). They are byte-identical except for the stored state: resolve, store the state, `+0x10`/`+0x14`/`+0x18` = −1. Nothing else.

**State 2 per-frame `0x00afeca0`** (40): seat (request `+0x10`) without an occupant → set-enter 4. Reached only from the instant hub.

**State 3 per-frame `0x00afed10`** (135). Entered from `0x0f`'s per-frame when the destination seat is taken (a change of seat after entering through another seat):
- o = the destination's occupant, p = its claim (0 when the claim is this human);
- o not already exiting (`0x009b9230`: `+0x16d4` = 2) → **o gets an ordinary exit request** (`0x00af3cd0`, exit request `+0x28` = −1);
- no o and no foreign claim → inlined set-enter `0x11` (log, then `0x00af99b0`), the change-seat animation (U6.5).

**State 4 on-entry `0x00afabd0`** (193):
1. State 4.
2. Human owned here:
   - stop a ragdoll (`0x009a5520(human, 0, 0)`, `0x009a1e10(human, 0)`);
   - `0x0097cdd0` → `0x0097cf40(handle, 1)`;
   - in water (`0x009b5690`) → `0x009b63f0(human, +0xccc)`.
3. Instantiated human at the queue head → **tilt fix**: a vehicle passing `0x00ad3270` whose orientation angle from `0x00da48a0` (on `+0x4c`), doubled, exceeds 0.5 in magnitude is righted through `0x00a87880` (role only). Then **set-enter 9**.
4. Hijack bookkeeping for the recorded hijacker (`0x00b0d280(vehicle, human, 1)`).
5. A seat occupant other than the human that is not already exiting gets an ordinary exit request. After a successful commit the occupant is the human, so this only fires when the commit did not run.

**State 4 per-frame `0x00afeeb0`** (235):
1. c = the queue head of the seat's queue, j = c's slot (`0x00b08390`). **If slot j is in state 5, j's entry is cancelled** (inlined set-enter 8). With an empty queue, j = −1 and slot −1's state (record `+0x5ac`) is read (OPEN 56).
2. The local player additionally needs an instantiated vehicle.
3. Not queue head (`0x00b0b3f0`) → wait.
4. Occupant o → victim fields and hijack bookkeeping; **o gets an instant exit request** when this machine owns the entering human.
5. Tilt fix; **set-enter 9** with the network flag = `0x008addb0(human, 0)`.

### 10.6 State 5 — walking to the entry point

**On-entry `0x00af9dd0`** (264; CONFIRMED):
1. State 5.
2. **Human not owned here** → `0x00986920(human, 3, 0)` (**`+0xccd` = 3**, §10.13) and return; the owner drives the walk.
3. **Convertible-style entry.** On a vehicle with `+0x16c8` bit `0x4000` (`0x00ad2fa0`; HYPOTHESIS: open-topped), request `+0x21` bit `0x40` (the `enter convertible` variant, §9.3) is set when either:
   - the entry has no usable door (`0x00aa2370`); or
   - all of the following hold: descriptor `+0x870` bit `0x40000` is clear, the human is the queue head, the door works and is closed (`0x00aa1fc0`), and a coin flip (`0x00ea4290` odd) comes up — or the byte `0x02845b1d` is set, which has no writer in `.text`.
4. **Slot `+0x24` = the `Interaction_Point_Type` the walk targets** (`spec-tables-vehicle-world.md` §4.1), with c = request `+0x21` bit 6:
   - entry seat occupied or claimed by someone else: 4 + c (`Enter Start Extract`, `… Convertible`) when the seat's point set has a type-4 point for the chosen side (`0x00b0b9a0(vehicle, entry, slot +0x20, 4, 0)`); otherwise c;
   - free: 2 (`Enter Start Lift`) for request `+0x22` bit 1, 3 (`Enter Start Pull`) for bit 2 (both set by the upkeep from the entry angle), otherwise c (`Enter Start` / `… Convertible`).

   This refines §9.3's "the upkeep (0)".
5. **Start navigating**: `0x00993e90(human, +0x1714 bit 4, 1, 0)` — the human-side **move-to-entry-point navigator**. While active it sets `+0xccd` = 3 with `+0xcce` = `0xd`/`0xe`, and it returns false when it cannot route (it also sets `+0x1854` bit 0). Role HIGH; internals OPEN 76.
   - **Success** → arm slot `+0x84` to 800 ms (the abort grace).
   - **Failure** → set-enter 8 when the seat (request `+0x10`) is occupied or claimed by someone else and `0x0097ea20(human)` is false; **otherwise `0x00afe980`**. `0x0097ea20` is never false for a player-class human; for others it is `0x005088c0`, not read. So the player, and any human facing a free seat, always snaps in.

**Per-frame `0x00aff1a0`** (758; CONFIRMED by full listing). Nothing happens unless the human is owned here. Then, first match wins:
1. **Stick abort** — local player, player `+0x28ac` bit `0x02` clear, the 800 ms timer expired, any non-zero walk axis → inlined set-enter 8.
2. No vehicle, or the vehicle not instantiated → **`0x00afe980`**.
3. **Too far** — unless the entry seat has `Ignore Distance Checks` (seat byte `+0x09` bit 3): when the 3-D distance from the human's controller position to the vehicle position exceeds `0x00b0a330`'s radius (12 or 15) × **`VI_pathfind_cancel_dist_multiplier`** (`0x02845c10`, shipped 1.5) → set-enter 8.
4. **While navigating** (`+0xccd` = 3). E = the entry point (`0x00b0efe0`).
   - (a) Request `+0x23` bit 2: when |(E − position) · vehicle up row| exceeds **`VI_on_vehicle_entry_max_y_diff`** (`0x02845b20`, 0.3) → set-enter 8. HIGH: the entry is from on top of a vehicle and the human has fallen off it.
   - (b) Moving (`+0xccc` ≠ 0) and within **`VI_pathfind_play_stop_dist`** (`0x02845bf0`, 1.0; horizontal distance):
     - `0x00993d50(human, 1)` ends the walk;
     - when `0x00af45a0(human, 0x327)` plays the stop, state `0x1d95` is placed by `0x004b7d50` at the computed transform with blend 0.3, and controller `+0x12c` = 0.0;
     - slot `+0x58` |= 4.
   - (c) Within **`VI_pathfind_check_pause_distance`** (`0x02845bd0`, 2.5):
     - wait (`0x00993d30`) while the entry seat has a pending handle (`0x00ab51a0`), or while the human is not the queue head;
     - at the head: with human `+0xc36` bit 2, `0x009947b0` and collision filter `0x1c` (`0x00759d80`);
     - otherwise, unless `+0xccd`/`+0xcce` = 3/`0xe` already, re-navigate in final-approach mode (`0x00993e90(…, 1, 1)`): success → filter `0x1c`; failure → `0x00993d50(human, 0)`, which marks the walk as failed.
   - (d) Otherwise keep navigating (`0x00993e90(…, 1, 0)`). Failure → `0x00993d50(human, 0)`.
5. **Arrived** (`+0xccd` ≠ 3):
   - wait for state `0x1d95` to finish (`0x004b4320`);
   - **walk failed** (`+0x1854` bit 0) → set-enter 8;
   - with human `+0x2bb` bit `0x10`, re-navigate when farther than 0.36 from E;
   - wait until the human is **first in the queue** (`0x00b0b4c0` = 1);
   - when the destination is the entry seat itself, also wait until the human holds its claim;
   - unless slot `+0x58` bit 2 is set: when the human's position and its `+0xc40` point (role not read) are more than 0.36 apart → re-navigate, or set-enter 8 when that fails.
6. **At the door:**
   - **request `+0x08` = 0 (approach only)** → call the `+0x34` callback with 1 (if still set; the hub normally consumed it) and **set-enter 8**;
   - otherwise → collision filter `0x15`, `0x0094cca0(human, 0, 0)` (the controller parked), for the local player `0x007f0140(1)` (`hud_inventory_hide`), then **set-enter 6**.

### 10.7 State 6 — the arrival router `0x00afb630` (241; CONFIRMED)

1. State 6.
2. **Snap to the entry point**: when `0x00b0efe0` succeeds, the human's position and then its orientation are set through its own vtable `+0x44`/`+0x48`. The heading is built from the point's direction about (0, 1, 0) (`0x00da05a0`, `0x00da5aa0`).
3. Controller present: `0x0094cca0(human, 0, 0)` and collision filter `0x15`. Human `+0xa50`/`+0xa68` = 0.
4. Request `+0x08` = 2 with an occupant → victim fields and hijack bookkeeping.
5. Vehicle-AI type `0xb` and the local player: for a victim (slot `+0x48`) with a non-zero team, `0x009dd390(player, victim, vehicle)` and `0x00b33dc0(vehicle, player handle, vehicle +0x40, 7, −1)`. Role only; HYPOTHESIS: a carjacking report.
6. **Request `+0x08` = 1 and an empty entry seat → set-enter 8** (the occupant is already out, §10.10).
7. Otherwise s = the chooser `0x00afb220(record, i, 0)` (§7.5, §9.5):
   - **s = 8 → `0x00afe980`** (snap in instead of cancelling);
   - otherwise → set-enter s (`0x0a`…`0x0f`).

### 10.8 State 7 — dead (resolves OPEN 60 for state 7)

**Per-frame `0x00affc10`** (128; CONFIRMED). When both the human and the vehicle are instantiated (`+0x33` bit `0x08`), it runs an inlined set-enter 9 (log, the commit). If the slot is then in phase 3, it sends the record snapshot **unconditionally** — this copy has no flag test. HIGH: a "wait for both objects, then seat" state for network peers.

**No producer exists:**
- **Locally**, none. §7.5's survey is confirmed by this unit's full reads of states 0–8: their transitions are 1, 2, 4, 5, 6, 8, 9, `0x0f`, `0x11`, `0x13` and the chooser's set.
- **Over the network**, none. The applier `0x00af2950` (CONFIRMED by listing) applies the received slot and dispatches on the received new state:
  - 0 → set-enter 0;
  - `0x14` → set-exit `0x14`;
  - **`0x22` → the slot is reset to phase 0 / state −1 with instant and skip-resolution set and request `+0x18` = `+0x10`, then set-enter 0** — a received "seated" becomes a fresh instant entry (§10.4.2);
  - anything else → the setter of the slot's received phase.

  The values that can arrive are fixed by the senders:
  - `0x00afa400`, called only from set-enter cases 0, 1, 6, 8 and from the nine inlined state-8 copies, all pushing 8 (raw-scan sites `0x00afe659`, `0x00afe68a`, `0x00afe738`, `0x00afe789`, `0x00afeaf5`, `0x00afec7d`, `0x00afefe1`, `0x00aff34c`, `0x00b00056`, `0x00b00924`, `0x00b00b27`, `0x00b00f07`, `0x00b0177c`);
  - the phase-3 snapshots (the commit's inlined sends, and the resync `0x00af3550`, which sends every locally owned riding slot), all carrying `0x22`;
  - `0x00b03010` (set-exit cases `0x14`/`0x15` only).

**So enter state 7 cannot be reached in a session where every machine runs this executable. The same argument covers exit `0x18` and `0x1c`, which no sender carries either.** CONFIRMED for the code paths. A peer running a different build is not excluded (OPEN 60 annotated).

### 10.9 State 8 — the cancel `0x00afb930(record, i)` (355; CONFIRMED by full listing)

It returns at once when *record* is null. Otherwise, in order:
1. State 8, `+0x10`/`+0x14`/`+0x18` = −1. `0x00af3630(human +0x16c0/+0x16c4, human handle, 1)` drops a queued packet for the pair (as §8.2 step 2).
2. Slot `+0x30` (the door part) = −1.
3. **The `+0x34` callback**, if still set: call it with (vehicle handle, occupant handle, request, **0**) and clear it. **`+0x3c` (completion) is not touched**.
4. `0x00b0b270(vehicle, human, 1)`: leave the entry-point queue and release or promote the seat claim (§7.3).
5. Player-class human:
   - `+0x1d95` bit `0x04` cleared;
   - the hat hidden at step 13 of §10.3 is restored (`0x009f0860`/`0x00821740`/`0x009f3530`). This does not test the seat flag.
6. Still navigating (`0x00985d80(human, 3, 0)`, i.e. `+0xccd` = 3) → `0x00986920(human, 1, 0)` for a player-class human, `(human, 0, 0)` otherwise.
7. Vehicle:
   - owned here, seat-0 occupant of class bit `0x22`, vehicle-AI type `0xb` → `0x00b500f0` (the AI's bytes 0/1 restored from `+0x3c8`/`+0x3c9`);
   - **the entry seat's `Primary_Access_Seat` and `Secondary_Access_Seat` records**, when unclaimed, get their **`+0x20`** timer armed to a random 250–750 ms. The commit arms `+0x24` of the same records (§8.1), and `0x0f` disarms `+0x20` (OPEN 78);
   - entry seat 0 → `0x00ab48f0(vehicle, 0)`.
8. **`0x00b0baf0(human)`** — the camera returns to the human, for the local player only. When the camera is in a vehicle submode (3–`0xc`, `0x11`, `0x14`, `0x26`–`0x2a`, `0x32`, `0x33`) or targets this human:
   - leave scope view;
   - request the on-foot submode (`0x19` when `0x00994f60`, else `0x0056db50()`, not when the submode was `0x11`);
   - camera target = the human;
   - `0x00574e30(0.0, 0)`.

   The exit finaliser uses the same routine. Then `0x00b0a170(human)`: `+0x16c0`/`+0x16c4` = null.
9. Not down or dead (`0x0096f540`) → **`0x00942110(human, 0, 0, −1.0)`: condition row 0**, which undoes state 0's row `0xa`.
10. Controller present → `0x0094f450(human, 1, 0)` (collision filter re-chosen) and `0x0094cca0(human, 1, 0)` (the controller back on).
11. Forced → the `ga_cable` object is destroyed (`0x00853ea0`).
12. Human `+0x1b10` = 0 and `+0x1b08`/`+0x1b0c` = null. Slot `+0x2c` = −1. **Release `0x00b086d0(i)`** (§7.3): `+0x16d4` = 0, `+0x1c48` = 0, the slot freed.
13. Then on the human:
    - instantiated → `0x00957c80(human, 0.2, 0, 0, 0)`;
    - `+0xe4` bit 4 with `0x00ad3820` → `0x008ccb90(human, 0, 1, 1)`;
    - not owned here → `+0xe4` bit 4 cleared;
    - not ragdolling → **set upright at its current heading** (vtable `+0x48` with a pure yaw from `0x00da3910`);
    - `+0xe4` |= `0x1000`, `+0xec` bit `0x100000` cleared.

**Callers:**
- set-enter 8, from: the cancel requests `0x00af2460`, `0x00af3e10` and `0x00af3f20` (§6.4); the upkeep; the hub; states 5 and 6;
- the inlined copies, from: states 0, 4, 5, `0x0a`, `0x0c`, `0x0d`, `0x0e` and `0x13` per-frame, and `0x00afe980`.

**What a cancel leaves behind:**
- a free human on condition row 0;
- the seat claim and queue place given up, the next claim promoted;
- the camera back on foot;
- no slot.

**It does not:** close the door it unlatched (only slot `+0x30` is forgotten); undo the `+0x1d97`/scope/fine-aim/reload/skydive cancellations of state 0; reverse the vehicle-AI reactions of hub step 16.

### 10.10 Request, slot and record fields settled here

| Field | Meaning | Evidence |
|---|---|---|
| record `+0x08` | **hub-local scratch**: the vehicle-AI reaction kind (1/2), always reset to 0 before the hub returns | §10.3 step 16 |
| request `+0x08` (slot `+0x64`) | **request kind**: −1 default; **0 approach only** (state 5 ends at the door with `+0x34`(…, 1) and a cancel); **1 remove the occupant, then stop** (instant: evict then 8; state 6: 8 once the seat is empty; set by the submitter for `+0x22` bit 3 with an occupied seat, and by `0x00af2460` as the deferred cancel during `extract`); **2 normal entry** (adds `human_ai_data` `0x26`). The wrapper `0x00b10900` maps its options mode 0–3 → 2, 4 → 1, 5 → 0 (jump table `0x00b10a68`) | CONFIRMED (behaviour); names HYPOTHESIS |
| request `+0x20` bit `0x20` | the hub treats the request as vehicle-AI kind 1 | CONFIRMED |
| request `+0x21` bit `0x20` | `enter shopper` (`0x0f`'s `+0x14` = 4); also set by the hub when animation state `0x94` is active | CONFIRMED |
| request `+0x21` bit `0x40` | `enter convertible`; **also set by state 5's on-entry** (§10.6) | CONFIRMED |
| request `+0x22` bits 1/2 | lift / pull start point (slot `+0x24` = 2/3) | CONFIRMED |
| request `+0x22` bit `0x20` | local-player one-shot → `0x00545010`, cleared by the hub | CONFIRMED (role only) |
| request `+0x23` bit 0 | **fast-entry request**: the hub routes to `0x13`. The upkeep keeps it only when the bearing is under 45° and otherwise clears it and retries; `0x00afe980` clears it | CONFIRMED; refines §7.2's "retry marker" |
| request `+0x23` bit 2 | **entry from on top of a vehicle**: surface `vehicle`, plus the y-difference cancel of §10.6 step 4(a) | CONFIRMED (use); meaning HIGH |
| request `+0x28` (slot `+0x84`) | the 800 ms stick-abort grace timer (states 0 and 5) | CONFIRMED |
| request `+0x30` (slot `+0x8c`) | **animation override**: when ≠ −1 the hub routes to `0x0f`, which plays it instead of looking up an `Element` | CONFIRMED |
| request `+0x34` (slot `+0x90`) | **"begun" callback** (cdecl: vehicle handle, occupant handle, request, ok). Called once: 1 by the hub, 1 at the door for kind 0, 0 by the cancel when the hub never ran | CONFIRMED |
| slot `+0x1c` | surface: 3 / 2 / 1 from the hub | CONFIRMED |
| slot `+0x24` | the walk's target `Interaction_Point_Type` (0–5), set by state 5 | CONFIRMED |
| slot `+0x30` | the door part the hub unlatched (−1 at cancel and commit) | CONFIRMED |
| slot `+0x48`/`+0x4c`, `+0x58` bit 0 | the victim's handle and "victim present" | CONFIRMED |
| slot `+0x58` bit 2 | the stop animation was played at the door | CONFIRMED |
| human `+0xccd` = 3 | **walking to a vehicle entry point**; `+0xcce` `0xd`/`0xe` while the navigator runs, 0 on a remote copy | CONFIRMED (writes); resolves OPEN 40 for 3 |
| human `+0x1854` bit 0 | the walk failed | CONFIRMED |

### 10.11 Out-of-band contracts (scoping §5a.2, checked)

The 15 roots call **120 functions outside the band (~7.9k instructions)** and 33 inside (~5.2k), from the census. They are contracts, cited by role above:
- **Human (~4.8k, the largest share):**
  - the damage routine `0x0096fa20` (1,502; only the hub's tank branch);
  - the navigator `0x00993e90` (435), with `0x00993d30`/`0x00993d50`/`0x009947b0`;
  - ragdoll `0x009a1dc0`/`0x009a1e10`/`0x009a5520`;
  - scope view `0x009e6510`, fine aim `0x009da830`/`0x009dcf10`;
  - the animation-request wrappers `0x00957c80`/`0x00957ee0`;
  - the locomotion setters `0x00942110`/`0x00986920`/`0x00985d80`;
  - the controller and collision filter `0x0094cca0`/`0x0094f450`/`0x0094fa00`/`0x00759d80`;
  - combat-ready `0x00943a20`/`0x00943960`, input `0x00950b70`;
  - the cancellations `0x009c07a0`, `0x009ae150`, `0x009c7cc0`, `0x009682d0`, `0x009b63f0`;
  - customization `0x009f0860`/`0x009f34d0`/`0x009f3530`.
- **Vehicle (~0.5k):**
  - seat readers `0x00ab5070`/`0x00ab5150`/`0x00ab51a0`/`0x00ab4af0`;
  - the door group `0x00aa2080`/`0x00aa2370`/`0x00aa1ff0`/`0x00aa1fc0`/`0x00aa1eb0`/`0x00a88eb0`;
  - the tilt fix `0x00a87880`;
  - predicates `0x00ad3140`/`0x00ad3270`/`0x00ad2fa0`/`0x00ad3820`/`0x00ab6210`;
  - `0x00ab48f0`.
- **Vehicle AI (~0.5k):** `0x00b29cf0`, `0x00b50180`, `0x00b464c0`, `0x00b500f0`, `0x00b1f6c0`, `0x00b33dc0`.
- **Weapon/item band (~0.2k):** `0x00941f40`, `0x005fe3e0`, `0x005fe460`, `0x009bfbc0`, `0x00b81440`. **`0x00600720` is not called by any U6.3 handler**; scoping's attribution comes from the commit (§8.4) and `0x0f`'s per-frame.
- **AI and HUD:** `0x004e4130`, `0x004e6600`, `0x0052a230`, `0x007f0140`, `0x007e2b30`, `0x00545010`, `0x009dd390`.
- **Animation API:** `0x004b4ce0`, `0x004b41b0`, `0x004b4320`, `0x004b7d50`, `0x004b7f70`, `0x004b16c0`.
- **Camera band:** `0x0056dc20`, and through `0x00b0baf0` §4's submode, target and placement calls.

### 10.12 What a port must reproduce (U6.3)

1. The hub's order and decision table (§10.3), including the forced-instant cases and the fact that state 1 never persists.
2. The instant route of §10.4.2: the upkeep's entry = seat, eviction by an instant exit request, the wait in 2, and the queue-head gate in 4. **A single-player port whose script calls `vehicle_enter_do` with instant set needs exactly this path, §7 and §8.** With the occupant eviction (§6) it is complete.
3. `0x00afe980`'s cancel-and-resubmit, which every approach state uses as its failure path.
4. The cancel of §10.9 as a one-shot that releases the slot. The two callback rules: `+0x34` once with a success flag, and `+0x3c` never on cancel.
5. The two stick aborts with their 800 ms grace and their gates (§10.2, §10.6).
6. The approach of §10.6 with the four `VI_pathfind_*`/`VI_on_vehicle_entry_*` tweaks at their shipped values. The queue ordering of §10.1 must be copied literally (origin-relative) to reproduce shipped behaviour.
7. State 7 can be omitted.

### 10.13 Corrections and cross-spec flags raised by U6.3 (no other spec edited)

- **`review/scoping-player.md` §5a.5:**
  - the instant/snap HYPOTHESIS for `0x0f` is refuted. The instant route is 0 → 1 → (2 →) 4 → 9, and `0x0f` is the forced/override in-place `enter` animation, going straight to 9 only without an animation (§10.4);
  - "instant is tested at three places" is wrong: 7 set sites and 12 test sites (§10.4.1).
- **§5a.1 item 4:** the hub's targets are confirmed, with their conditions (§10.3).
- **§5a.1 item 8:** the stick abort is local-player only, has an 800 ms grace, and has a second copy in state 5. The cancel releases the slot.
- **§5a.2:** `0x00600720` is not a U6.3 callee (§10.11).

  Folded into that file.
- **This file, §7:**
  - §7.1 record `+0x08` is the hub's scratch word (§10.10);
  - §7.2's request table gains `+0x08` as a kind, `+0x23` bit 0 as the fast-entry request, `+0x23` bit 2, `+0x28`, `+0x30` and `+0x34` (§10.10);
  - §7.5: **state `0x0f`'s per-frame also passes 9 by register** (`0x00b012d6` → `0x00b0131d`), a ninth producer of 9 that the push survey missed; state 7 is dead (§10.8);
  - §7.6: the upkeep also forces instant at `0x00af615f`, when no entry point resolves and request `+0x08` ≠ 1.
- **This file, §9.3:** slot `+0x24` is also written by state 5's on-entry (the target point type 0–5), and request `+0x21` bit 6 (`+0x7d` in the table) is also set by state 5, not only by the submitter's caller.
- **This file, §14 OPEN 63:** the six ride tweaks' shipped values are in `tools/ai_base_xtbl/tweak_table.xtbl`:
  - `VI_forward_vel_accel` 13, `VI_vert_vel_accel` 1, `VI_random_accel_factor` 0;
  - `VI_reaction_delay_min_ms` 100, `VI_reaction_delay_max_ms` 200;
  - `VI_collision_min_vel_diff` 10.

  The registry rows are in `tools/ai_tweak_registry.tsv`. The DLC tweak table overrides none of them.
- **`spec-lua-api-behaviour.md` §38.1:** `vehicle_enter_do`'s "instant placement" path is §10.4.2. It is not a separate placement routine: it goes through the same slot, hub and commit and completes on the next world tick when the seat is free.
- **`spec-ai-runtime.md` §8 item 4:** the hub's AI side is §10.3 steps 6, 16 and 19:
  - the player cancels walking NPC claimants;
  - the vehicle-AI reactions `0x00b50180`/`0x00b464c0`;
  - `human_ai_data` `0x26` for kind-2 requests.

  The `Veh enter` request itself is not issued here.

---

## 11. U6.7 — The animated exit states (`0x1a`–`0x21`)

**Scope.** Unit U6.7 of `review/scoping-player.md` §5a.3: the eight exit-side state numbers `0x1a`–`0x21` left after U6.6 (§6, the non-animated exits `0x14`–`0x19`) and U6.1 (§9, the element-selection rule these states call). Roots: the on-entry and per-frame handlers of every live state in the range — `0x1b` (`0x00b02330`, `0x00b062b0`), `0x1d` (`0x00b02900`, `0x00b05b90`), `0x1f` (`0x00b05560`, `0x00b063c0`) and `0x21` (`0x00b057b0`, `0x00b06d30`) — plus `0x1f`'s exit-kind chooser `0x00b04ae0` and the eject handler `0x00b02cd0` it reaches. It also specifies the three helpers scoping §5a.4 point 6 left to "whichever of U6.5/U6.7 lands first": `0x00afe0d0`, `0x00b01460` and `0x00b0c170`; U6.3 (§10, landed first) did not claim them, so they are specified here in full. States `0x1a`, `0x1e` and `0x20` have no on-entry handler (U6.0 §7.5 already shows this from the setter side; confirmed again here from the handler side) and `0x1c`'s handler is empty; all four are covered at the level that confirms they do nothing, not re-derived. What the hijacker's own *enter*-side slot does (states `0x0a`–`0x13`, U6.5) is cited only where `0x1d` reaches into it.

**Method.** Ghidra-free. Capstone listings over `tools/harnesses/exe_va.py`: all ten roots, the three shared helpers, the eject handler `0x00b02cd0`, the door/side-clearance helper `0x00b02460`, and the cached vehicle-point query `0x00b0eac0`/`0x00b0df70` were read in full; `0x00b034c0`'s own callers `0x00b04990`/`0x00b04ae0` were read at the call site only, since `0x00b034c0` itself is U6.6 §6.8's. The census closure table (53 in-band functions, 6,187 instructions, excluding other units' own roots) was used to confirm every function in range was reached and to find the two listings (`0x00b07d50`, `0x00b08060`) that turned out to be a generic cached vehicle-point utility shared with code outside this unit, not specific to it. No Ghidra project was opened or copied. No `.czn_pc` data was touched.

### 11.0 Results up front (U6.7)

1. **The range holds four live states.** `0x1a`, `0x1e` and `0x20` have no on-entry handler (the 14-entry set-exit table `0x00b05ac4` sends them to the same no-op return as `0x00b05ac4`'s own bounds check) and no per-frame handler either (`0x00b06fdc`); they can never be held, as U6.0 §7.5 already found from the setter side. **`0x1c`'s on-entry `0x00b023f0` is confirmed empty**: it resolves, writes state `0x1c` and resets the three per-state fields to −1, and does nothing else; its per-frame `0x00b024f0` resolves and returns. Neither writes any field a later frame could read. U6.0's OPEN 60 (no local producer for `0x1c`) stands unresolved from this side too. **The four live states are `0x1b` (change seat), `0x1d` (extract), `0x1f` (exit) and `0x21` (traverse)** (§11.2–§11.6).
2. **The three shared helpers are specified in full, matching the working notes exactly on every point checked against the listings:**
   - **`0x00b0c170(human)`** (67 instructions) — the "is the held vehicle-interaction animation finished" test: true with no human, an uninstantiated human, or the animation `0x1d95` not active; otherwise true once progress ≥ (duration − blend-out)/duration, blend-out defaulting to 0.3 s (§11.7.1).
   - **`0x00b01460(record, slot)`** (168 instructions) — starts a fast-entry animation once, latched on request `+0x7d` bit 7 (enter-request `+0x21` bit 7, slot-relative); on a lookup or play failure it cancels and resubmits through `0x00afe980` (U6.3 §10.4.3); otherwise, for the local player, it runs the animated-camera gate `0x00b0ae40` and, failing that, the path-clear check `0x00afe0d0` before falling back to the camera hand-off `0x00b0bc10` (§8.6). **It is called on the hijacker's own slot from `0x1d`'s on-entry, not only on its own enter-state `0x13`'s slot** (§11.7.2, §11.4).
   - **`0x00afe0d0(vehicle, request, anim; human in `ecx`)`** (219 instructions) — the animated-camera path-clear raycast: true at once for a non-local player, a seat the validator `0x00b0a510` rejects, or an empty `Enter_Animations` set; otherwise it walks the matched `Element`'s `Animated_Camera_Tests`/`Camera_Pos` points (via `0x00b07020`), builds one segment per point (first segment from the committed camera position `0x013c8740`), batch-casts them (`0x0075e490`) and returns false on any hit (§11.7.3).
3. **`0x00b04ae0` (783 instructions, `0x1f`'s own exit-kind chooser) resolves most of OPEN 69.** It writes slot `+0x10` = `0x15`, `0x16`, `0x17`, `0x18`, `0x1b` or `0x1c` as immediates (never `0x14`, `0x19` or `0x1a` in the traced control flow — the "register-written `0x14`/`0x19`/`0x1a`" guess in OPEN 69 is not borne out for this function), with sub-variant `+0x14` = `9`/`0xa`/`0xb` (`exit fast outward`/`forward`/`backward`) and `0xc`/`0xd` (`exit at speed cruise`/`fast`), exactly the values OPEN 69 named (§11.5.2).
4. **The exit-request object handle `+0x08`/`+0x0c` (OPEN 47, "reader not in this unit") finally has readers — three, all here:** `0x00b063c0` (`0x1f`'s per-frame) resolves it and requires the resolved object's own `+0x10` field to equal `0x18` before continuing; `0x00b04ae0` resolves it as a second human and reads *that* human's own vehicle-handle pair `+0x16c0`/`+0x16c4` to anchor an eject/guardian-angel placement; `0x00b05b90` (`0x1d`'s per-frame) compares it against a cache on the exiting human (`+0x16e8`/`+0x16ec`, new) to decide whether to fire a change notification. HIGH: a companion human the exit is choreographed around (§11.8).
5. **New exit-request/slot bits, beyond §6.2's table:** `+0x30` bit 4 forces `0x1f` into the `exit dive` kind past the room predicate `0x00b0a380`; bit 5 selects the `exit eject` kind; bit 6 is *written* by `0x00b04ae0` (not only read) when a companion anchor resolves for eject; bit 7 is also written here, not only by the player's prompt, when no clear `exit fast` side exists. `+0x31` bit 0 is read by `0x00b02460` as a "fixed once" door-clear latch — the same bit position U6.1 §9.3 names as the `convertible` exit variant, a conflict left OPEN (§11.7.4); bit 5 is read only by the eject handler. `+0x32` bit 0 is a per-frame "already started" latch on `0x1f`, distinct from bit 1 (replicated) and bit 2 (vehicle may be gone) (§11.8).
6. **Slot `+0x18` is not always −1 on entry.** `0x1f`'s on-entry sets it to `6`, not −1 — the one exception to U6.6 §6.1's "every on-entry handler of this unit sets `+0x10`/`+0x14`/`+0x18` to −1". It is a two-stage sub-timer (`6` → `7`) that several of `0x1f`'s per-frame kind-tails use to wait out half of the animated-camera test window before finishing (§11.5.3).
7. **When the chosen kind has no `Element` (or request `+0x30` bit 6 forces no clip) and the "point-set" flag (request `+0x31` bit 2) is set with a point available, `0x1f` teleports the human directly** through the same virtual `+0x40` transform setter the instant path (§6.9) uses, instead of playing anything. Without a point, or for the eject kind specifically, it takes other dedicated paths (§11.5.1, §11.5.3).
8. **`0x1f`'s per-frame has exactly one way out when no animation is chosen: an inlined transition to `0x21`** (confirming U6.6 §6.1's citation). **`0x21`'s own per-frame waits for animation `0x1d95` to reach 100% progress, then inlines straight to the finaliser `0x19`** (confirming the same citation) — and along the way reads the walk axes (`0x005bb530`, actions 2 and 3, the same `CAA_WALK_FORWARD_BACKWARD`/`CAA_WALK_TURN_LEFT_RIGHT` pair scoping §5a.1 item 8 names) to decide exit-request `+0x31` bit 1, the bit the finaliser's landing branch reads as the new locomotion state (§6.11 step 11(b)). `0x21`'s own on-entry also, surprisingly, always runs `0x1f`'s entire on-entry first (§11.6).
9. **"Exit eject" (`0x1b`) writes the player's own physics velocity field directly.** The eject handler `0x00b02cd0` takes the exit request's launch vector (`+0x10`–`+0x1f`, confirming §6.2's HYPOTHESIS for this one kind) and, on the no-parachute path, stores it straight into human `+0x1cec`/`+0x1cf4` — **the same desired-velocity field `spec-player-runtime.md` §5.4 (U3) specifies as the locomotion bridge's own output to physics.** A cross-spec tie between U6.7 and U3 that neither unit's scoping anticipated (§11.5.4, §11.10).
10. **`0x1d`'s on-entry (`0x00b02900`) matches U6.1 §9.3/§9.5/§9.8's citations exactly**: it copies exit-request `+0x20`/`+0x24` into slot `+0x10`/`+0x20`, runs the element lookup with a miss falling back to the instant exit `0x17`, and — new here — first scans the other seven slots of its own record for the **hijacker's own enter-side slot** (phase 1, state `0x0d` or `0x13`, enter-request `+0x18` equal to this slot's own seat `+0x2c`) and, when found, uses *its* enter-request for the lookup instead of a fallback. The fast-entry branch calls the shared helper `0x00b01460` **on the hijacker's slot**, not the victim's (§11.4.1).
11. **`0x1d`'s per-frame (`0x00b05b90`) has two shapes.** An occupant that never became instantiated (object flag `+0x33` bit `0x08` clear, or no human at all) finalises at once, inline to `0x19`. An instantiated occupant waits for `0x00b0c170`, then snaps the physics character proxy to the animated end position through `0x0079efb0`/`0x0079efe0` — **confirming scoping §5a.1 item 5's HYPOTHESIS for that pair concretely** — before handing off to one of two deep human-runtime contracts by extract kind (`0x00942110`-derived kind `0xd`, "kill", resolves a per-seat `0xd0`-byte weapon table and calls `0x0096fa20`; every other kind calls `0x009a48e0`). **Neither call was found to drive an explicit state change on this path; how the slot eventually reaches `0x19` for a non-`kill`, instantiated extract is OPEN** (§11.4.2, new OPEN item).
12. **`0x1b` (change seat) and `0x1c` (empty) are confirmed end to end**, matching the working notes' pre-existing traces exactly, with no corrections needed (§11.2, §11.3).
13. **Size:** the ten roots own 1,916 instructions; their in-band closure (stopping at the other units' own roots: the resolver, the three setters, U6.1's rule, U6.0's trace/network calls, U6.3's cancel-and-resubmit and U6.4's camera blend) is 53 functions / 6,187 instructions — in line with scoping §5a.3's ~3.8–4.2k exclusive estimate, the difference being the shared helpers the closure still counts.

### 11.1 The eight states at a glance

| State | On-entry (via `0x00b05ac4`) | Per-frame (via `0x00b06fdc`) | Entered from | Leaves to |
|---|---|---|---|---|
| `0x1a` | none (shared no-op return) | none | never (no producer, §6.1) | — |
| `0x1b` change seat | `0x00b02330` | `0x00b062b0` | `0x16`'s per-frame `0x00b06020` (U6.6 §6.8), inlined | `0x1d`/`0x1f` (via `0x00b02150`, U6.6 §6.7.2 step 4), inlined |
| `0x1c` | `0x00b023f0` (empty) | `0x00b024f0` (empty) | no local producer (U6.0 OPEN 60) | — |
| `0x1d` extract | `0x00b02900` | `0x00b05b90` | `0x15`'s on-entry `0x00b036c0`, when request `+0x30` bit 3 (U6.6 §6.7.2 step 4) | `0x17` (miss), `0x19` (inlined, both shapes) |
| `0x1e` | none | none | never (no producer; used only as a key by U6.1 §9.5's temporary slot) | — |
| `0x1f` exit | `0x00b05560` | `0x00b063c0` | `0x15`'s on-entry, when request `+0x30` bit 3 clear; `0x16`'s per-frame (U6.6 §6.8) | `0x16` (wait for seat, via `0x00b034c0`), `0x19` (teleport/eject paths, inlined), `0x21` (inlined, no-animation fallback) |
| `0x20` | none | none | never (no producer, §6.1) | — |
| `0x21` traverse | `0x00b057b0` (calls `0x1f`'s own on-entry body first, as a subroutine, §11.6) | `0x00b06d30` | `0x1f`'s per-frame, no-animation fallback | `0x19` (inlined, once `0x1d95` finishes) |

### 11.2 The empty and dead states (`0x1a`, `0x1c`, `0x1e`, `0x20`)

**`0x1a` and `0x20`** index the set-exit table `0x00b05ac4` at the same target as the table's own out-of-range default (`0x00b05a20`, a bare return). **`0x1e`** likewise has no entry in the table and no per-frame slot in `0x00b06fdc`. None of the three has any local setter: no immediate `0x1a`, `0x1e` or `0x20` appears at a set-exit call site, no inlined copy produces them, and no register-passed chooser (`0x00b02150`, `0x00b04ae0`) returns them. The only shipped way any of the three could ever be held is the network applier replaying a remote value — and since none can be *set* locally either, that remote value could itself only come from another machine's own locally-unreachable state. **They are dead**, consistently with U6.0 §7.5's and U6.1 §9.8's findings from the setter and key-builder sides.

**`0x1c`'s on-entry `0x00b023f0`** (36 instructions, CONFIRMED by full listing): resolve, slot state `+0x0c` = `0x1c`, `+0x10`/`+0x14`/`+0x18` = −1. That is the entire body. **Its per-frame `0x00b024f0`** (27 instructions, CONFIRMED): resolve, return. Neither reads nor writes anything else, and `0x1c` has no per-frame transition anywhere in the dispatch tables. Unlike `0x1a`/`0x1e`/`0x20`, `0x1c` is reachable in principle — it has a real on-entry handler a chooser *could* return — but nothing in this unit's closure, U6.6's, or U6.0's census of every state's register-passed sources produces the value `0x1c`. U6.0's OPEN 60 is correct to leave it open.

### 11.3 State `0x1b`: change seat

#### 11.3.1 On-entry `0x00b02330(record, slot)` (57 instructions, CONFIRMED by full listing)

cdecl, called with `record`/`slot` on the stack. 1. Resolve. 2. State `+0x0c` = `0x1b`; `+0x10`/`+0x14`/`+0x18` = −1. 3. `anim = 0x00b0ad10(vehicle info, slot, request, &exit-seat, 0)` (the element lookup, phase "any other than enter" → `Exit_Animations`, seat = slot `+0x2c` per U6.1 §9.4.1's change-seat exception). 4. If `anim ≠ −1`: `0x00af45a0(human, anim)` plays it. A miss is silently ignored — `0x1b` has no fallback of its own, unlike `0x1d`'s miss-to-`0x17`.

#### 11.3.2 Per-frame `0x00b062b0(record, slot-index)` (88 instructions, CONFIRMED by full listing)

1. Resolve. 2. Unless `0x00b0c170(human)` says the held animation is finished, return (wait). 3. Once finished: `0x00ab4d20(&pos, &mtx, human handle, vehicle, exit seat)` resolves the new seat's transform; `0x004f00f0` turns the matrix into an orientation; the human's own virtual `+0x40` transform setter places it there directly (position + orientation, 1 = instant/no blend). 4. `next = 0x00b02150(record, slot)` — U6.6 §6.7.2 step 4's pure function of request `+0x30` bit 3 (`0x1d` extract when set, `0x1f` exit otherwise). 5. `0x00b058f0(record, slot, next, send = 1, from_network = 0)`.

**So a change-seat is a teleport, not a walk**: it snaps the human to the new seat's transform the instant the old seat's animation ends, then re-enters the normal exit flow (`0x1d`/`0x1f`) for the *new* seat. This matches the working notes exactly; nothing here needed correcting.

### 11.4 State `0x1d`: extract

#### 11.4.1 On-entry `0x00b02900(record, slot in `eax`)` (204 instructions, CONFIRMED by full listing)

1. Resolve. 2. State `+0x0c` = `0x1d`; **`+0x10` = exit request `+0x20`; `+0x14` = −1; `+0x18` = −1; `+0x20` = exit request `+0x24`** — exactly U6.1 §9.3/§9.5/§9.8's citation of this copy (the victim's extract kind and side), confirmed by listing.
3. **Hijacker-resync scan.** Before anything else, it walks the record's other seven slots (stride `0x100`, matching U6.0 §7.1's layout) looking for one that is phase 1 (entering), state `0x0d` (`extract`) or `0x13` (`fast`), **and whose own enter-request `+0x18` (target seat, U6.1 §9.4.1's `0x00b0a510` input) equals this slot's current seat `+0x2c`.** If found, the resolver is re-run on *that* slot and its enter-request pointer is kept for the lookup below; if not found, a sentinel (−1) stands in for it. This is how `0x1d` finds "the hijacker who is causing this extract", independently of the exit request's own fields, by matching on who else is currently targeting this human's seat.
4. **Seat reservation and the lookup.** `0x00b0b540` (U6.6 §6.1's seat reservation) reserves the exit seat; `0x00b0ad10(vehicle info, the hijacker's (or the sentinel) request, &exit-seat, slot)` runs the element lookup.
   - **Miss (`−1`):** request `+0x30` |= 1 (instant), then `0x00b058f0(record, slot, 0x17, send = 1, from_network = 0)` — **the instant fallback**, confirming the working notes.
   - **Hit:** `0x00af45a0(human, anim)` plays it directly.
5. **Fast-entry and second-anim branches**, gated on whether a count/override value from the hijacker search is non-zero:
   - if the hijacker's own enter-request `+0x23` bit 0 (slot-relative `+0x7f`, new field — a "fast entry" flag) is set: **`0x00b01460(hijacker's slot, record)` is called on the hijacker's own slot**, not the victim's — it starts *the hijacker's own* fast-entry animation in lockstep with this extract (§11.7.2);
   - otherwise: a second `0x00b0ad10` lookup runs (different arguments); on a hit, the second animation is also played, enter-request `+0x7d` bit 7 (new) is set and `+0x84` (new, enter-request `+0x28`) is cleared to −1; then the animated-camera gate `0x00b0ae40(human, anim)` is checked, and failing that the camera hand-off `0x00b0bc10(human, vehicle, cancel-blend = 0, no-gunner-test = 0, snap = 0, place-mode = 0)` runs directly (not through `0x00afe0d0` — that check is `0x1f`'s and `0x00b01460`'s, not this branch's).
6. **Tail.** For a class-bit-`0x22` occupant (the same "despawn-eligible" bit U6.6 §6.7.2/§7.11 names), `0x0070de70` is called (146 instructions, not traced further here — HIGH: the full-victim removal path the working notes flagged, a human-runtime contract).

#### 11.4.2 Per-frame `0x00b05b90(record, slot-index)` (362 instructions, CONFIRMED by full listing)

**Hijacker-change notification.** First, it compares the exit request's object handle (`+0x08`/`+0x0c`) against a cache on the human, `+0x16e8`/`+0x16ec` (new field pair — HIGH: "the handle this extract was last notified about"). When they differ, the event dispatcher `0x009b0a00(slot, old handle, new handle, event id `0x262d4c8`, 0, 0, −1.0)` fires before the cache is used again; when they already match, the call is skipped.

**Branch on whether the occupant is instantiated** (object flag `+0x33` bit `0x08`, same bit U6.6 §6.7.2/§5.6.3 use):

- **Not instantiated, or no human at all — finalise at once.** A class-bit-`0x21` vehicle gets two extra calls first (`0x00a91dd0`, `0x00a90d40`, vehicle-band, not traced). Then the occupant handle and class row are re-validated exactly as set-exit's own guard does (§6.1: object flag `+0x33` bit `0x10` clear, class row byte `+0xa` bit 0, `0x00853b10`), the `Set Exit` trace is logged with state `0x19`, and `0x00b03d80` (the finaliser, U6.6 §6.11) is called directly — **the inline `0x1d → 0x19` transition**, confirming the set-exit infrastructure table (§6.1).
- **Instantiated — wait, then snap and hand off.** While the clip is still playing or `0x00b0c170` says it is not finished, nothing happens this frame (with a small local-player-only re-sync sub-check against `0x004b2760`/`0x004b28c0` that can call `0x00b0baf0(human)` early). Once finished:
  1. `0x00abae00(human, &offset)` reads an offset; it is added to the human's current position (read through the character-proxy enable call `0x0079efb0`) and the result is written back through the character-proxy disable call `0x0079efe0` — **snapping the physics proxy to the animated end position**, confirming scoping §5a.1 item 5's HYPOTHESIS for this exact pair.
  2. **By extract kind** (slot `+0x10`, copied from the exit request in §11.4.1 — value `0xd` is `kill` on the `0x0d`/`0x1d` extract-kind list, U6.1 §9.3):
     - **kind `0xd` (kill):** a per-seat, `0xd0`-byte-stride table (indexed by slot `+0x2c`) is read for a handle pair, resolved through `0x004d7f30` to an object, and its held weapon is queried (`0x00941f40`, twice) to find a weapon sub-object (`+0x19c`); then `0x0096fa20` (1,502 instructions, the same deep human-update function scoping §5a.2 names) is called with that weapon.
     - **every other kind:** a class-bit-`0x22`, driver-seat (`+0x2c` = 0) occupant first gets a vehicle event (`0x00ad3270`, then `0x0070dcf0(0, hash `0x3c673855`, vehicle)`); then, under the network-authority gate (`0x008addb0`), `0x009a48e0` (906 instructions, "Brute Fuel Tank" in U6.6's own caller census) is called.

  **Neither `0x0096fa20` nor `0x009a48e0` is traced further here** (both are deep, already-cited human-runtime contracts), **and no call to `0x00b058f0` or `0x00b03d80` was found on this path.** How an instantiated, non-`kill` extract eventually reaches state `0x19` is **OPEN** (a new item, §11.10) — either one of these two functions drives it internally, or a later frame's check this trace did not reach does.

### 11.5 State `0x1f`: exit

#### 11.5.1 On-entry `0x00b05560(record in `edi`, slot in `esi`)` (185 instructions, CONFIRMED by full listing)

Register call (`edi` = record, `esi` = slot). `0x21`'s own on-entry calls this function directly as a subroutine before doing anything of its own (§11.6), so both states share this exact convention.

1. Resolve. 2. State `+0x0c` = `0x1f`; `+0x10`/`+0x14` = −1. **`+0x18` = `6`** — not −1, the one exception to U6.6 §6.1's "every on-entry handler of this unit sets `+0x10`/`+0x14`/`+0x18` to −1" (that rule was written for the non-animated exits; `0x1f` keeps `+0x18` as a live sub-timer, §11.5.3).
3. **Point-set retry.** When exit-request `+0x31` bit 2 (the "point-set" flag set by `0x15`'s `0x00b0b9a0` query, §6.2) is set and the old state was not already `0x1f`: call the seat/door-point helper `0x00b04990` (105 instructions, a thin wrapper around U6.6 §6.8's seat router `0x00b034c0`), then re-check the human is still marked exiting (`0x009b9230`, `+0x16d4` = 2); on failure (the occupant stopped exiting mid-call), bail to the epilogue immediately.
4. **Choose the kind, if not already chosen.** When slot `+0x10` is still −1 (freshly set above): call the exit-kind chooser `0x00b04ae0` (§11.5.2). On its failure, retry `0x00b04990` once more if the retry flag from step 3 allows it; either way, continue.
5. **Door handling.** If a secondary flag `0x00b04ae0` set is clear, skip; otherwise query the seat's door state through `0x00aa2370` (the same function `0x15`'s on-entry and `0x00b02460` use) and, when it reports the door blocked, release/open it through `0x00aa2080` (the same door-sequence call the finaliser uses, §6.11 step 10).
6. **Then**, regardless: clear exit-request `+0x32` bit 0 (a per-frame "started" latch, distinct from bit 1 replicated / bit 2 vehicle-may-be-gone, new field), and — only when a flag from step 4/5 was set — clear request `+0x31` bit 2 as well.

#### 11.5.2 The exit-kind chooser `0x00b04ae0(record, human in `ecx`, slot, flag, &out1, &out2; two further out-params in `ecx`/`edx`)` (783 instructions, CONFIRMED by full listing)

Called once, from `0x1f`'s own on-entry. It is the function OPEN 69 names, and the trace resolves almost all of it.

1. **`flag` true — forced kinds.**
   - Exit request `+0x31` bit 4 set → **kind `0x1c` (`exit guardian angel`)**: `+0x20` = −1; the request's animation override `+0x2c` is copied out; `0x00b0b540` reserves the seat; success.
   - Otherwise, request `+0x30` bit 5 set → **kind `0x1b` (`exit eject`)**. This is the **writer** for `+0x30` bit 5's selection role and bit 6's companion-resolved flag: it resolves the request's own `+0x08`/`+0x0c` object handle through `0x00af1c40`, and — on success — treats the resolved object as a **second human**, reading *that* human's own `+0x16c0`/`+0x16c4` vehicle-handle pair (resolved through `0x004dcf00`) and running `0x00ac5960` on the result; on success it sets `+0x30` bit 6. **This is the first and only reader of the exit-request object handle `+0x08`/`+0x0c` found anywhere (OPEN 47), and it is HIGH: a companion human the eject trajectory is anchored to or past.**
2. **`flag` false — cascade by vehicle state.**
   - The tip-over predicates `0x00ad3b40`/`0x00ad5240` (reused from §6.7.1) gate a vehicle-type speed check (`0x009963f0`); passing both → **kind `0x18` (`exit at altitude`)**, `+0x24` = `0xf` (new, a timing-parameter selector passed to `0x00b0b610` below).
   - Otherwise, unless request `+0x30` bit 4 is set (which forces this branch regardless), the dive-room predicate `0x00b0a380` gates **kind `0x17` (`exit dive`)**, `+0x24` = `0x10`. **Bit 4 is the first reader found for this bit** (OPEN item in §6.2's table, "read by U6.7 (OPEN)"): set, it skips the room check entirely.
   - Failing that, a side-clearance cascade (§11.7.4) can set request `+0x30` bit 7 (not only the player's prompt, as §6.2 states) and choose **kind `0x15` (`exit fast`)**, with sub-variant `+0x14` = `9`/`0xa`/`0xb` (`outward`/`forward`/`backward`) chosen by probing the animated-camera test points (`0x00b0eac0`, §11.7.4) and preferring `outward` when it is also valid.
   - A vehicle-speed test against type-dependent thresholds (`0x00ad2f80`, `0x00ad4160`) can instead choose **kind `0x16` (`exit at speed`)**, with sub-variant `+0x14` = `0xc`/`0xd` (`cruise`/`fast`) by the same thresholds, `+0x24` = `0xd`/`0xe`.
   - Every successful branch ends by releasing/reserving the seat through `0x00b0b770`/`0x00b0b540`, routing through `0x00b034c0` (U6.6 §6.8), re-confirming the kind has a matching `Element` (`0x00b0ad10`), and — through `0x00b0b610(seat, pos, 5 or 0x1f4)` — setting three timing floats (`0.1`, `1000`, `0.005`) whose consumer was not traced further. **No branch was found that writes kind `0x14` ("exit standard"); it is HIGH: the state's implicit default, left in place when no special-case branch is taken.** Kind `0x19` is not a kind at all in this trace — it is the finaliser's own *state* number, pushed as a literal by the per-frame handlers (§11.5.3, §11.6), which is almost certainly what OPEN 69's "register-written `0x19`" was actually seeing. **Kind `0x1a` (`exit climb`) is read and handled by `0x1f`'s per-frame (§11.5.3) but its producer was not found in this function either; it remains OPEN.**

#### 11.5.3 Per-frame `0x00b06ef0` → `0x00b063c0(record in `ecx`, slot-index in `[ebp+8]`)` (696 instructions, CONFIRMED by full listing)

1. Resolve. 2. **Branch on exit-request `+0x32` bit 0** (the "already started" latch from §11.5.1 step 5):
   - **Bit clear (first frame in `0x1f`):**
     a. Unless animation override `+0x2c` is set, run the element lookup `0x00b0ad10`; **on a miss, or with the override set but request `+0x30` bit 6 set (the eject companion-resolved flag), skip playing anything** and fall to step (c).
     b. Otherwise, **play the chosen (or overridden) animation** `0x00af45a0(human, anim)`. If the human's `+0x31` bit 4 is set and a tag match against the literal `"Jump_Rope"` succeeds, an extra per-rig animation-parameter push runs (`0x008ff850`, `0x009215b0`, `0x008fea50`) — HIGH: a special case for exiting past a mounted jump-rope prop. If the human is not yet instantiated and two cheap predicates (`0x004d47e0`) agree, `0x00ad4b10` instantiates it. Under network authority, force-flag `0x004e4130(human, 0x19)` runs (an OPEN 62 callee, now with a confirmed call site). `0x00b0baf0(human)` runs unconditionally. **If the exit-request object handle (`+0x08`/`+0x0c`) resolves through `0x00456920` and the resolved object's own `+0x10` field equals `0x18`**, and the camera is not busy (`0x0056b200`), **a camera effect is primed** (`0x004c0690`, `0x00b0efe0`, a duration read from animation `0x1d95`'s length via `0x004b44a0`, `0x00574c50`) — HIGH, not traced to a name. Set-exit cleanup bits `+0x32`/`+0x31` are cleared and the function returns, staying in `0x1f`.
     c. **No animation to play.** If the kind (slot `+0x10`) is `0x1b` (eject): `0x00b02cd0(request; human, vehicle)` runs (§11.5.4) and the function returns, staying in `0x1f` for this frame. Otherwise, if request `+0x31` bit 2 (the "point-set" flag) is set and a point resolves (`0x00b0efe0`): the human is **teleported** there through its own virtual `+0x40` setter (`0x004f00f0` builds the orientation) — the same mechanism the instant path (§6.9) uses. **Failing a point, or with the flag clear, the occupant handle and class are re-validated (the same `0x00458230`/`0x00853b10`/class-row-bit-0 guard as set-exit's own, §6.1) and an inlined set-exit transition to `0x21` runs** (`Set Exit` trace, `0x00b057b0` called directly, state `0x19` not involved) — **confirming U6.6 §6.1's citation "`0x00b063c0` (`0x1f` per-frame) → `0x21`" and showing exactly when it fires.**
   - **Bit set (a later frame):** re-validate the occupant exactly as above; bail if it fails. Otherwise dispatch on **kind − `0x14`** (a 9-entry table over `0x14`…`0x1c`, the same range `0x1f`'s own key-builder case covers, U6.1 §9.3) using **slot `+0x18`** as a 2-stage sub-timer:
     - **`exit climb` (`0x1a`) and at least one other table entry share a common tail**: while `+0x18` = `6`, wait until animation `1d95`'s progress passes 0.5, then set `+0x18` = `7` and return (still `0x1f`); once `+0x18` = `7`, open the exit door fully (`0x00aa1e70`, 1.0) and read the walk axes (`0x005bb530`, actions 2 and 3, local player only) to feed the climb.
     - **several entries re-validate the occupant, open the exit door, and inline a set-exit to `0x19`** directly (the same `Set Exit`-trace-then-handler-call shape as §11.3.2/§11.6).
     - **one entry calls `0x00af24d0`** (U6.6 §6.4.3's exit fast-forward) after opening the door — the fast-forward path from mid-`1f` straight to `0x19`.
     - the eject kind's own table entry re-reaches `0x00b02cd0` (§11.5.4) when animation `1d95` is confirmed active.

   **Which of the nine table slots maps to which exact body was not independently verified against the jump table's raw bytes beyond `exit climb`'s position (index 6) and eject's (index 7, inferred from `0x00b02cd0` appearing both here and from the direct kind-`0x1b` check in (c)); the shapes above are CONFIRMED as a set, the kind-to-shape pairing for the other seven is HIGH** (new OPEN item, §11.10).

#### 11.5.4 The eject handler `0x00b02cd0(exit request; human in `edi`, vehicle in `esi`)` (158 instructions, CONFIRMED by full listing)

1. **Reads the request's launch vector `+0x10`–`+0x1f` as a 4-float block** — confirming §6.2's HYPOTHESIS for this field, for the eject kind specifically. Also reads request `+0x31` bit 5 (new field) into a local flag.
2. Opens the vehicle's "door used" (`+0x16d0`, the same field the finaliser's door sequence reads, §6.11 step 10) fully (`0x00aa1e70`).
3. Sets human `+0x1a00`/`+0x1a04` = the vehicle's own handle `+0x08`/`+0x0c` (or, with no human resolved, a literal fallback — a guard-quirk OPEN item, §11.10) and `+0x1a08` = now + 1000 ms — **the exact fields and the exact timer the finaliser's own airborne landing branch uses (§6.11 step 11(a))**, set up here directly instead of being deferred to it.
4. `0x00af24d0(human, replicate = 0)` — the exit fast-forward (U6.6 §6.4.3) — sends the slot straight to `0x19`.
5. Checks human `+0x1c9c` bit `0x2000` — **OPEN 45's own bit, "standing on a moving support"**, confirmed as a live read here: if set, `0x00ab1b30` runs (vehicle-band, not traced — HIGH: a parachute/safety hook).
6. Under network authority, by the **same** bit:
   - **set** (parachute/safety path): `0x009a48e0` runs with two literal strings (`0x1185b18`/`0x1185b1c` — the same pair used as the null-human fallback in step 3) as a named force-flag or event; on success, human `+0xdc` bit `0x80` (new) is set, and — gated on the `+0x31` bit 5 flag read in step 1 — `0x0077b600(human)` fires (HIGH: a sound/scream trigger); `0x007818f0` then applies the launch vector as an impulse.
   - **clear** (no-parachute path): `0x00941cd0(human, &quaternion from the launch vector)` reorients the human, and — **the key finding** — the launch vector is written directly into **human `+0x1cec`/`+0x1cf4`, the player's own desired-velocity field, `spec-player-runtime.md` §5.4 (U3)'s output of the locomotion bridge to physics.** The controller's own grounded flag (`+0xcc0`→`+8` bit 2) is cleared, human `+0xe8` bit `0x80000000` (new) is set, `0x0094cca0(human, 1, 0)` and `0x0098aa10(human, 1, 1, 0)` run — **the same calls the finaliser's airborne branch makes (§6.11 step 11(a))**, here inlined instead of deferred — and a shuffled copy of the same vector is written to `+0x1c60` (OPEN 45's "added to the velocity" field). A `100 ms` timer is armed at `+0x19d4`.
7. Tail: a difficulty/mode getter `0x00614d00`, with class-bit `0x21` (`0x00853b30`), gates `0x00623a00(human, 1, 0, 1)` — HIGH, not traced.

**So "eject" does not play a clip at all: it reorients, launches and fast-forwards the human to the finaliser in the same frame, writing the launch vector straight into the locomotion bridge's velocity field for the no-parachute case.**

### 11.6 State `0x21`: traverse

**On-entry `0x00b057b0(slot in `ecx`, record in `edx`)`** (114 instructions, CONFIRMED by full listing). It has exactly two callers — the set-exit table itself and `0x1f`'s per-frame inline transition (§11.5.3) — and both reach the same body:

1. Resolve, set `esi` = slot and `edi` = record (the same convention `0x1f`'s own on-entry expects), and **call `0x1f`'s entire on-entry body `0x00b05560` as a subroutine** — running all of §11.5.1's side effects, including a full run of the exit-kind chooser `0x00b04ae0`, unconditionally. (This is why `0x21` is never entered without first re-running `0x1f`'s on-entry logic, whichever of the two callers reaches it.)
2. **Only if slot `+0x10` is still −1 afterward** (i.e. `0x1f`'s chooser left the kind unset): overwrite slot state `+0x0c` = `0x21` and reset `+0x10`/`+0x14`/`+0x18` = −1 and `+0x24` = `0x10` (a new field — HIGH: the same timing-parameter role as `0x1f`'s own `+0x24` writes, §11.5.2).
3. Run `0x00b0eac0` for the animated-camera test points and write the result to slot `+0x1c` (the surface/landing field, U6.1 §9.3), then look up the element for state `0x21`'s own key (first token `traverse`, U6.1 §9.2).
4. **On a miss:** request `+0x30` |= 1 (instant) and `0x00b058f0(record, slot, 0x17, 1, 0)` — **the same instant fallback `0x1d`'s miss path uses.** **On a hit:** `0x00af45a0(human, anim)` plays it directly.

**Per-frame `0x00b06d30(record in `edi`, slot-index in `[esp+0x30]`)`** (147 instructions, CONFIRMED by full listing):

1. Resolve. 2. Query animation `1d95`'s progress (`0x004b4600`); **while it is below 1.0, do nothing and return** (still `0x21`).
3. **Once it reaches 1.0:** open the exit door fully (`0x00aa1e70`, 1.0). **Read the walk axes** (`0x005bb530`, actions 2 and 3 — the same `CAA_WALK_FORWARD_BACKWARD`/`CAA_WALK_TURN_LEFT_RIGHT` pair as `0x1f`'s climb case, §11.5.3) and, for the local player, set exit-request **`+0x31` bit 1** when either axis's magnitude is non-zero — **the exact bit the finaliser's landing branch reads as the new locomotion state (0 run / 2 run, §6.11 step 11(b))**, so a player actively pushing a walk axis as a traverse animation ends lands running. For a non-local human, the same bit is set instead when the human itself reports no local-player status *and* request `+0x30` bit 7 is set (the "no clear exit-fast side" / player-prompt bit, §11.5.2, §6.2).
4. The occupant handle and class are re-validated (the same guard as everywhere else in this unit), and an inlined set-exit transition to **`0x19`** runs directly (`Set Exit` trace with state `0x19`, `0x00b03d80` called with `(slot-index, record)`) — **confirming U6.6 §6.1's citation "`0x00b06d30` (`0x21` per-frame) → `0x19`" and showing it fires exactly once `1d95` completes.**

### 11.7 The three shared helpers

#### 11.7.1 `0x00b0c170(human)` (67 instructions, CONFIRMED by full listing)

1. No human, or `+0x33` bit `0x08` clear (not instantiated) → **true**.
2. Animation `1d95` not active (`0x004b4ce0`) → **true**.
3. Otherwise: *p* = progress (`0x004b4600`); *D* = duration (`0x004b44a0`); *r* = the active clip's own blend-out field (via `0x004b46c0` → `0x004bf5e0` → `0x004beff0`), or `0.3` s (`0x01116910`) with no clip resolved. **True when p ≥ (D − r)/D.**

This matches the working notes' formula exactly; no correction needed.

#### 11.7.2 `0x00b01460(record, slot)` (168 instructions, CONFIRMED by full listing)

Register call: `[ebp+8]` = record, `[ebp+0xc]` = slot (matching `0x1d`'s own convention when it calls this function on the hijacker's slot, §11.4.1).

1. Resolve. 2. **Latch check**: enter-request `+0x21` (slot-relative `+0x7d`) bit 7 — if already set, do nothing and return.
3. `0x00ab4c40(&a, &b, vehicle, 0)` — outputs discarded (confirming the working notes' "outputs unused").
4. `anim = 0x00b0ad10(vehicle info, slot, request, &exit-seat, 0)`. **On a miss (`−1`), or on a subsequent play failure**, call `0x00afe980(record, slot)` — U6.3 §10.4.3's cancel-and-resubmit — and return.
5. Otherwise play it (`0x00af45a0`). **Then**: check whether animation `1d95` carries the "critical end" tag (a cached hash at `0x0153be2c`, via `0x004b2760`); **without the tag**, call `0x00759d80(human, 0x15, 0)` then `0x0094cca0(human, 0, 0)` (release the character controller); with the tag, skip both.
6. Check the request's object handle `+0x08`/`+0x0c` through `0x0091faf0`; when it resolves and the human is the local player: run the animated-camera gate `0x00b0ae40(human, anim)`, and failing that the path-clear check `0x00afe0d0(vehicle, request, anim; human)` (§11.7.3); failing *that* too, run the camera hand-off `0x00b0bc10(human, vehicle, cancel-blend = 0, no-gunner-test = 1, snap = 0, place-mode = 0)` (U6.4 §8.6's signature).
7. Tail: latch enter-request `+0x21` bit 7, clear `+0x28` (slot-relative `+0x84`) to −1.

**Called on three contexts** (U6.5's enter state `0x13`'s own on-entry and per-frame, and `0x1d`'s on-entry on the hijacker's own slot, §11.4.1) — the same function, whichever side calls it, because the enter-request it latches and plays belongs to whoever is physically entering through the fast-entry route, victim's extract or not.

#### 11.7.3 `0x00afe0d0(vehicle, request, anim; human in `ecx`)` (219 instructions, CONFIRMED by full listing)

1. `seat = 0x00b0a510(request +0x18)` (U6.1 §9.1's seat validator). Not the local player, or seat = −1 → **true**.
2. `set = Enter_Animations` at vehicle info `+0xa7c` + `0x24`·seat. Null → **true**.
3. `0x00b07020(set, anim, &count)` finds the matched `Element` and returns its `Animated_Camera_Tests` point array and count. **Count ≥ 32 → false** (with a diagnostic log, `0x00434ed0`/`0x0091f5e0`-style `0x00ea4158`/`0x00ea421b`). **Count ≤ 0 → true** (nothing to check).
4. For each point *i* (0-based): the segment start is the committed camera position `0x013c8740` for *i* = 0, else the previous point's world position; the end is this point's world position, both via `0x00d9fb90` (rotate by vehicle `+0x4c`) plus vehicle position `+0x40`/`+0x44`/`+0x48`. One `0x70`-byte cast-job entry is built per segment.
5. `0x0075e490` batch-casts all segments. **Any segment reporting a hit → false. No hits → true** (with the same diagnostic-log shape as step 3's failure branch, confirming it is a shared logging tail, not a second check).

This matches the working notes' description exactly, with the diagnostic-log calls and the `0x70`-byte job stride added as detail.

#### 11.7.4 Supporting functions used by the chooser and the shared helpers (contracts, not derived further)

- **`0x00b0eac0(vehicle, seat, side, flag, 0, &out1, &out2)`** (80 instructions) → **`0x00b0df70`** (117) → **`0x00b07d50`**/**`0x00b08060`** (193/216): a cached vehicle-relative point/orientation query (the `Animated_Camera_Tests`/`Camera_Pos` points, keyed by the global cache slot `0x0288cca0` through `0x00b0c480`/`0x00b0c910`/`0x00b0aeb0`). **This chain is reached from outside this unit too** (`0x0062f1f0`, activity/mission band) — it is generic vehicle-point infrastructure, not U6.7's own logic, so it is named here as a contract rather than derived.
- **`0x00b02460(request, slot; vehicle in `edi`, request in `esi`)`** (53 instructions, read in full): a door/side-clearance helper called from `0x00b04990` and `0x00b04ae0`. It applies only when the recorded exit seat equals the slot's current seat; **it reads request `+0x31` bit 0 as a one-shot "already handled" latch** — set, it short-circuits to false immediately. **This is the same bit position U6.1 §9.3 reads as the `convertible` exit-variant flag** (slot-relative `+0xd1` bit 0). Whether the two uses are a deliberate bit-reuse (for example because a convertible's door never needs this clearance check) or a misreading of one of the two sites is **OPEN** — no writer of the bit was found in either unit, so which role (if either) is primary cannot be settled here. Absent that conflict, the rest of the function's shape is plain: it queries the seat's door state (`0x00aa2370`, reused from §6.7.2/§11.5.1), tries to clear it (`0x00aa2080` then `0x00aa1fc0`) when blocked, and is gated by a vehicle-info flag (`+0x870` bit `0x40000`) and a periodic random throttle (`0x00ea4290`) — **unless the global byte `0x02845b1d` is set, in which case it always succeeds without the random throttle.** This advances U6.3's OPEN 79 (which already flagged this global as read by `0x00b02460`): the function it gates is a shared door-clearance helper used by both the enter-side hub (U6.3's context) and this unit's `0x1f` kind chooser, and the global's ultimate writer and purpose stay OPEN, as U6.3 left them.
- **`0x00b034c0`** (U6.6 §6.8's exit-seat router) and **`0x00b02150`** (U6.6 §6.7.2 step 4's `0x1d`/`0x1f` chooser) are cited, not re-derived, since both are already specified there in full and this unit's calls to them pass the same arguments those sections describe.

### 11.8 New and revised exit-request/slot fields (corrections to §6.1/§6.2)

| Field | Role found here | Status |
|---|---|---|
| exit request `+0x08`/`+0x0c` | a second human's handle (HIGH: a companion the eject/guardian-angel placement is anchored to); read by `0x00b063c0`, `0x00b04ae0`, `0x00b05b90` | resolves OPEN 47 bullet 1 |
| exit request `+0x10`–`+0x1f` | confirmed as the launch vector for the eject kind (`0x00b02cd0`) | confirms §6.2's HYPOTHESIS, for this kind |
| exit request `+0x30` bit 4 | forces `0x1f` into `exit dive` past the room predicate `0x00b0a380` | resolves the "Read by U6.7 (OPEN)" annotation |
| exit request `+0x30` bit 5 | selects `exit eject` in `0x00b04ae0` | new |
| exit request `+0x30` bit 6 | *written* by `0x00b04ae0` (companion anchor resolved for eject), not only read | new write site |
| exit request `+0x30` bit 7 | also written by `0x00b04ae0`'s side-clearance cascade, not only the player's prompt `0x009e5e80` | corrects §6.2 |
| exit request `+0x31` bit 0 | a one-shot "already cleared" latch `0x00b02460` reads — **the same bit position U6.1 §9.3 names as the `convertible` variant flag**; no writer found for either role | new; conflicts with an existing reading, OPEN |
| exit request `+0x31` bit 5 | read only by the eject handler `0x00b02cd0`; role HYPOTHESIS (gates a sound/scream call) | new |
| exit request `+0x32` bit 0 | a per-frame "`0x1f` has begun" latch, distinct from bit 1 (replicated)/bit 2 (vehicle may be gone) | new |
| slot `+0x18` | **not always −1 on entry**: `0x1f`'s on-entry sets it to `6`, a 2-stage (`6`→`7`) sub-timer its own per-frame advances | corrects §6.1's blanket rule |
| slot `+0x24` | a small timing-parameter selector (`0xd`/`0xe`/`0xf`/`0x10`) passed to `0x00b0b610`; `0x1f`/`0x21`'s on-entries write it | new |
| human `+0x16e8`/`+0x16ec` | caches the exit request's `+0x08`/`+0x0c` handle; `0x1d`'s per-frame fires an event (`0x9b0a00`, id `0x262d4c8`) when it changes | new |
| human `+0xdc` bit `0x80` | set by the eject handler on the parachute/safety path | new |
| human `+0xe8` bit `0x80000000` | set by the eject handler on the no-parachute path | new |
| enter request `+0x23` (slot-relative `+0x7f`) bit 0 | "fast entry" flag `0x1d`'s on-entry reads on the hijacker's own enter-request | new |
| enter request `+0x28` (slot-relative `+0x84`) | cleared to −1 by `0x1d`'s second-anim branch and by `0x00b01460`'s own tail | new |

### 11.9 What a port must reproduce

1. **The four live transitions** (§11.1): `0x1b` teleports to the new seat on its own clip finishing, then re-enters `0x1d`/`0x1f` for it; `0x1d` resolves the hijacker's own slot to source its animation, with the same instant-`0x17` miss fallback `0x1f` uses; `0x1f` chooses an exit kind through the cascade of §11.5.2 (tip-over → dive-room (unless forced) → fast-exit side clearance → speed, with guardian-angel/eject as flag-forced overrides), teleporting instead of playing a clip when the point-set flag holds and no animation is available, and falling back to `0x21` otherwise; `0x21` waits out the traverse clip and finalises, reading the walk axes only to set the landing locomotion bit.
2. **"Eject" is not an animated state in the usual sense.** It writes the request's launch vector straight into the player's own physics velocity field (`+0x1cec`/`+0x1cf4`, shared with U3 §5.4) and fast-forwards to the finaliser in the same frame it is chosen. A port must reproduce this write, not an eject animation plus a later physics catch-up.
3. **The three shared helpers** (§11.7.1–§11.7.3) exactly as specified, including the camera-busy and path-clear gating order (`0x00b0ae40` before `0x00afe0d0` before the camera hand-off) and the VI-finished formula's blend-out default.
4. **The hijacker-resync scan** (§11.4.1): without it, a victim's extract would source its animation from its own (usually empty or stale) exit-request fields instead of the hijacker's enter-request, and the fast-entry branch would start the wrong character's animation.
5. **The open per-frame transition for a non-`kill`, instantiated extract** (§11.4.2, OPEN item) should be guarded defensively in a port (for example, by timing out to `0x19` after the clip finishes) until the real trigger is found.

### 11.10 Corrections and cross-spec flags raised by U6.7 (no other spec edited)

- **This file, §6 (U6.6):**
  - §6.1's "every on-entry handler of this unit sets `+0x10`/`+0x14`/`+0x18` to −1" does not hold for `0x1f`, which sets `+0x18` = `6` (§11.5.1).
  - §6.2's field table: `+0x30` bits 4–7 and `+0x31`/`+0x32` gain the new readers/writers of §11.8; the launch-vector HYPOTHESIS (`+0x10`–`+0x1f`) is confirmed for the eject kind; the object-handle OPEN (`+0x08`/`+0x0c`) is resolved (§11.0 item 4, §11.8).
  - OPEN item 58's "either those states are never set, or they resolve their keys elsewhere" (for `0x1a`/`0x1c`/`0x20`): **the first holds for `0x1a`/`0x20`, confirmed again from the handler side; `0x1c` resolves no key at all, since its handler is empty** (§11.2).
- **This file, §9 (U6.1):**
  - §9.3/§9.5/§9.8's citations of `0x1d`'s field copy and miss fallback are confirmed exactly by the full listing (§11.4.1).
  - §9.4.1's "exit-seat pointer not validated" for the lookup is confirmed again here: `0x1d`'s and `0x1f`'s own calls pass the hijacker's/chosen seat without a range check.
- **This file, §10 (U6.3):**
  - OPEN 79 (`0x02845b1d`) is advanced, not closed: `0x00b02460` is a shared door-clearance helper (§11.7.4) between U6.3's enter-side hub and this unit's `0x1f` kind chooser; the global's writer is still not found.
  - §10.4.3's `0x00afe980` citation is reused verbatim by `0x00b01460` (§11.7.2); no correction to it.
- **This file, §8 (U6.4):** `0x00b0bc10`'s 10-caller list (§8.6) already includes `0x00b01460`, consistent with §11.7.2's finding; no correction.
- **`review/scoping-player.md` §5a.3:** the U6.7 row's "`0x00b063c0` (696) reads the walk axes" is confirmed, and extended: `0x00b06d30` (`0x21`'s per-frame) reads them too (§11.5.3, §11.6). §5a.4 point 6 ("whichever of U6.5/U6.7 lands first owns" `0x00afe0d0`/`0x00b01460`/`0x00b0c170`) is resolved: U6.5 had not landed when this section was written, so all three are specified here (§11.7).
- **`spec-player-runtime.md` §5.4 (this file, U3):** add a forward cross-reference — the vehicle-interaction eject path (`0x00b02cd0`, §11.5.4) writes `+0x1cec`/`+0x1cf4` directly, outside the locomotion bridge's own composer, as a second writer of that field beyond §5.4's own derivation. §5.4 is not edited; the note is recorded here and at §11.0 item 9.

## 12. U6.8 — Per-slot pose driver and rider parameters

**Scope.** `review/scoping-player.md` §5a.3's row for U6.8: the per-slot pose driver `0x00af6310` and its two walkers (`0x00af8f70`, `0x00af9030`); the rider-parameter writer `0x00af4b80` (called from the human animation-parameter writer `0x00958690`, Player U3's traditional scope — `spec-anim-runtime.md` §4.2, cited not re-derived); `0x00af4150` (called from `0x00952760`); `0x00af4760` (called from `0x0095cbc0`). Dependencies: U6.4 (`spec-player-runtime.md` §8) for the pose half, Player U3 (§12.5) and `spec-anim-runtime.md` §4.1/§4.2 for the rider-parameter contract.

**Method.** Ghidra-free, as every other U6 sub-unit. `tools/harnesses/exe_va.py` for byte/dword/string reads; a purpose-built recursive-descent Capstone walker (`cfg_walk.py`, scratch) that follows `jcc`/`jmp` edges and tracks the `esp` delta from each root's entry, used to resolve stack-relative operands to stable frame offsets; cross-checked by hand at every branch join (x86 requires a consistent `esp` at a merge point, so a mismatch is a tracker bug, not a code fact — one real tracker limitation was found and is noted below). All six roots were read to their `ret`/epilogue: `0x00af6310` (2,149 instructions, confirmed against scoping's own count), `0x00af8f70` (84), `0x00af9030` (83), `0x00af4b80` (498), `0x00af4150` (266), `0x00af4760` (394) — total 3,374 instructions, matching scoping §5a.3's "~3.4k own" for the row. Every jump-table base cited below was read directly as raw dwords/bytes (`0x00af8798`, `0x00af87b4`, `0x00af87d8`, `0x00af87f0`), and every parameter name was read as a string from the literal pushed before its `0x004cbaf0` resolver call. Six tweak globals were traced to their registration stubs at `0x0100a8a0`–`0x0100a914` (registrar `0x0071b580`, the same float registrar `spec-player-runtime.md` §8.5 already cites for the `VI_*` ride tweaks) and read by name. No Ghidra project was opened. No `.czn_pc` zone data was touched.

**One tracker limitation, disclosed:** a handful of small helper calls (e.g. `0x004c0690`, confirmed by listing to end in `ret 4`) pop their own pushed argument instead of leaving cleanup to the caller. The walker does not model callee-side stack cleanup, so a handful of `esp`-relative labels deep inside `0x00af6310`'s common preamble (roughly `0x00af64b6`–`0x00af6908`) could not be resolved to a single stable frame offset with full confidence; those spots are called out as HIGH/role-only below rather than asserted as CONFIRMED. This does not affect any of the five register-based findings (the human/vehicle/slot roles, the dispatch logic, or the rider-parameter computations in `0x00af4b80`), which were cross-checked against already-landed field semantics (`spec-player-runtime.md` §7.2, §8.1, §8.5) and did not depend on the affected stack slots.

### 12.0 Results up front (U6.8)

1. **The "three jump tables" claim is confirmed as three physically distinct dword arrays at exactly the addresses scoping gives — `0x00af8798`, `0x00af87b4`, `0x00af87d8` — but their roles are not what the survey-level note said.** There is no single switch that dispatches "enter states 3–0x13, exit states 0x1b–0x21 and ride 0x22" through three parallel tables. Instead (§12.2 below):
   - `0x00af6310` tests the slot's **phase** (`+0x08`) first, not the state. **Ride (phase 3)** never touches any of the three tables: it branches straight to a dedicated handler (`0x00af6d7a`).
   - **Exit (phase 2)** indexes `0x00af8798` directly by `state − 0x1b` (bounds-checked to 0–6): a genuine 7-entry table, exactly scoping's "exit states 0x1b–0x21".
   - **Enter (phase 1)** does *not* index a 17-entry table by `state − 3`. It first looks up a **byte** in a 17-entry remap table at `0x00af87f0` (keyed by `state − 3`), then uses *that byte* to index a **6-entry deduplicated pointer table at `0x00af87d8`** — the compiler folded the 17 enter states down to 6 unique handlers.
   - `0x00af87b4` is **not** a top-level phase/state table at all: it is a private 9-entry sub-dispatch inside exit state `0x1f`'s own handler (`0x00af69e9`), keyed on a field of the resolved request (`[eax+0x10] − 0x14`, bounds 0–8), and every one of its 9 targets is a handler address *also* reachable from the enter dispatch (`0x00af69fb`, `0x00af6a3d`, `0x00af6adc`, `0x00af6b3f`, `0x00af69df`, `0x00af69da`). The three tables are laid out back-to-back in memory (`0x8798`–`0x87b0`, `0x87b4`–`0x87d4`, `0x87d8`–`0x87ec`), which is almost certainly why the original survey read them as one continuous structure.
   - **Net effect:** several enter states and several exit states — and one exit state's own sub-variants — converge on the *same* short handler blocks (most commonly `0x00af69df`/`0x00af6c00`, a shared "nothing case-specific to do" stub). §12.2–§12.3 give the full map.
2. **`0x00af6310` drives pose and attachment, confirmed per-case.** Past the dispatch, every reachable case funnels into one of two shared tails: the enter/exit tail starting at `0x00af6c00` (which queries the human's animation controller for a cached graph-state id `0x1d95` through `0x004b44a0`/`0x004b46c0`/`0x004b2710`/`0x004b4320`, and reads vehicle seat-descriptor and attach-point fields) or the ride/attachment tail at `0x00af6d7a` (vector/orientation math feeding the character-proxy pair `0x0079efb0`/`0x0079efe0`). The proxy-position snapshot `0x0079ef60` runs once, early, for every case that reaches the main body. §12.3 gives the case-by-case detail that exists (attachment flags, seat-table reads) without re-deriving the SSE orientation math byte-for-byte, consistent with how `spec-player-runtime.md` §8 treats comparably deep internals ("role only").
3. **`0x00af8f70` and `0x00af9030` are now fully specified, with no ambiguity.** Both are small, single-exit functions. `0x00af8f70` (the per-frame walker, called from the world update at the site scoping's §1.7 and §5a.1 already name) walks all 48 vehicle-interaction registry records and all 8 slots of each, and calls `0x00af6310(record, slot)` for every occupant that passes three exclusion checks shared verbatim with `spec-player-runtime.md` §6.1's own gating idiom (object flag `+0x33` bit `0x10` clear; class-row `0x02cc9900` byte `+0xa` bit 0 set; `0x00853b10` false). `0x00af9030(vehicle)` does the same for one vehicle's record only (found via `0x00b08400`), confirming scoping's "reached from vehicle code (`0x00a9e1d0`)" as an on-demand, single-vehicle refresh.
4. **`0x00af4b80` is the complete rider-parameter writer, and it is now specified down to the tweak.** It writes exactly the 11 names `spec-anim-runtime.md` §4.1 already lists for it, and only while the slot's resolved phase is 3 (ride) — otherwise all 11 are reset to a neutral value (9 floats to `0.0`, the 2 bools to false) and the function returns. The three lean/tilt values read the **ride body-lag model's error fields** that `spec-player-runtime.md` §8.5/OPEN 63 flagged as unread ("the readers of the lag values slot `+0xe0`/`+0xe8` were not traced… HIGH: U6.8's pose driver") — **this closes that half of OPEN 63**: `0x00af4b80` is a reader of slot `+0xe4` (vertical error) and `+0xec` (forward error), remapped through three further bss tweaks (`VI_forward_diff_max`, `VI_vert_diff_max`, `VI_tilt_rvec_pitch_max`, registered the same way as the six `VI_*` ride tweaks U6.4 found) into the `[-1, 1]` range the parameters expose. §12.4 below is the full per-parameter table.
5. **`0x00af4150` is *not* a parameter writer, despite scoping's row grouping it with "the rider writer".** It never calls `0x004cbaf0`, `0x004b1510` or `0x004b1410`. It is the **rider fall/eject detector**: gated on the human currently having a resolved vehicle (`+0x16c0`/`+0x16c4`) and being in one of three seat-related states (`+0x16d4` ∈ {1, 2, 3}), it reuses the same knock-down predicate `0x009a48e0` that `spec-player-runtime.md` §5.4.2 already cites for a different context, and on a positive result sets human flag `+0xdc` bit `0x80` and applies a launch impulse (`0x00781 8f0`) built from an extrapolated human/vehicle offset. §12.5 gives the detail that was traced; two gates (`+0x16d4` = 1 or 2, and class-row byte `+6` bit `0x80`) are not independently named and are left OPEN.
6. **`0x00af4760` is the ride-`Element` key builder, and it resolves half of `spec-player-runtime.md` OPEN item 71 directly.** OPEN 71 asks "who builds the six-token key passed to `0x00b08b80` (callers `0x00b08ea0` and `0x00af4760`)?" — `0x00af4760` is now traced in full: for a slot whose resolved phase is *not* 3 it inspects the state/variant fields and, for a specific subset of enter/exit states (`0x15`, `0x16`, `0x1b`, `0x1d`, and conditional cases under `0x1f`/`0x14`), builds the six-token key and calls **`0x00b08b80`** directly (confirmed by listing: `call 0x00b08b80` at `0x00af4a18`), returning the matched animation id. For every other state it returns one of several small sentinel/animation codes without consulting the ride tables at all. When the slot's phase *is* 3, it skips all of this and just re-snaps the seat pose through `0x00b09400` (the same pose-snap `spec-player-runtime.md` §8.1 already cites for instant entries). `0x00b08ea0`'s own half of OPEN 71 is unaffected and still open.

### 12.1 The two walkers (CONFIRMED by full listing)

**`0x00af8f70()`** (84 instructions, no arguments; called once per frame from the world update's ride-animation hook, `review/scoping-player.md` §1.7/§5a.1's `0x00af8f70`):
1. Calls `0x00b0bab0()` first (role only — not traced; a per-frame prelude shared with other vehicle-interaction work).
2. Outer loop over the registry `0x02845cb0` in steps of `0x1640`, stopping before `0x028888b0` — the exact 48-record span `spec-player-runtime.md` §6.1 already documents. A record is skipped whole when its own 8-byte handle (`record+0x00`/`+0x04`) is zero.
3. Inner loop over the record's 8 slots (`record+0x6a0 + 0x100·i`, `spec-player-runtime.md` §7.2's layout). A slot is skipped when its occupant handle (`+0x00`/`+0x04`) is zero.
4. For a non-empty slot: resolve the occupant handle to an object pointer through `0x00458230(this=0x024433a8, &handle, 0x031d152c)`; skip if null. Skip if object flag `+0x33` bit `0x10` is set. Skip if the object's class-row (`0x02cc9900`, indexed by object byte `+0x34`) byte `+0xa` bit `0x01` is clear. Skip if `0x00853b10(object)` is true.
5. Otherwise: `push slot_index; push record_ptr; call 0x00af6310`. This is the exact two-argument convention `0x00af6310` itself expects (§12.2).

**`0x00af9030(vehicle)`** (83 instructions; a second, on-demand caller reached from vehicle code at `0x00a9e1d0` per scoping):
1. Reads the vehicle's own 8-byte handle (`vehicle+8`/`+0xc`) and finds its registry record through `0x00b08400(0x02845cb0, handle)` (the exact lookup `spec-player-runtime.md` §6.1 already names: "finds a record by the vehicle's handle").
2. If found: the same 8-slot loop, the same three exclusion checks, the same `0x00af6310(record, slot)` call — byte-for-byte the same body as `0x00af8f70`'s inner loop, just for one record instead of 48.

Both confirm the calling convention **`0x00af6310(record, slot_index)`** independently of reading `0x00af6310`'s own prologue.

### 12.2 `0x00af6310` — the real dispatch structure (confirms and corrects scoping §5a.1 item 5)

**Entry (CONFIRMED by listing).** `0x00af6310(record, slot_index)` (two cdecl arguments, `[ebp+8]`/`[ebp+0xc]`) opens with the shared resolver **`0x00af4500`** (`spec-player-runtime.md` §6.1's documented eight-argument signature), requesting the slot pointer, the enter/exit requests and the vehicle, and passing a null output pointer for the human slot since it is about to re-derive it from the resolved record directly. Two register roles hold for the rest of the function: **`edi` = the occupant human** (confirmed by the immediately following `test byte ptr [edi+0x33], 8`, the same "has an active rig" bit `spec-player-runtime.md` §5.4.1 tests for every human in the composer's player/NPC lists), and, once validated, **`esi` = the same human**, used throughout via the `[[esi+0xd24]+0x10]` controller idiom that `spec-anim-runtime.md` §4.2 already documents as "Controller `= [[human+0xd24]+0x10]`".

**Early-outs (CONFIRMED by listing):**
- No resolved human, or the human has no active rig: read the slot pointer's state (`+0x0c`) directly; if it is 4, 7 or `0x22` (ride), re-enable the character proxy once (`0x0079ef60(human+0x40, 1)`) and return; otherwise return doing nothing.
- A resolved, active-rig human that fails a further predicate `0x00af6230(human)` (not traced beyond its boolean result) also returns doing nothing.
- A resolved, active-rig human whose seat-record lookup `0x00af1cf0(occupant handle)` returns 4 or 7 returns at a third, dedicated epilogue (`0x00af874f`) without running the main body.

**Phase switch (CONFIRMED by listing — this is the part that corrects scoping §5a.1 item 5).** Deep inside the common preamble, at `0x00af6908`, the slot pointer's **phase** field (`+0x08`, `spec-player-runtime.md` §7.2) is read and switched three ways:
- **phase 1 (enter) → `0x00af6b87`.** Reads state (`+0x0c`), subtracts 3, bounds-checks to `[0, 0x10]` (states 3–`0x13`), then `movzx eax, byte ptr [eax + 0x00af87f0]` — an **8-bit remap table**, 17 entries, read directly: `[0, 5, 5, 5, 5, 5, 5, 5, 1, 1, 1, 1, 2, 5, 3, 5, 4]` for states 3…`0x13` in order — then `jmp dword ptr [eax*4 + 0x00af87d8]`, a **6-entry pointer table**, read directly: `{0: 0x00af6be6, 1: 0x00af6ba0, 2: 0x00af6ba7, 3: 0x00af6c45, 4: 0x00af6c5f, 5: 0x00af6c00}`. Composing the two tables gives the real per-enter-state target:

  | Enter state | Target | Enter state | Target |
  |---|---|---|---|
  | 3 | `0x00af6be6` | `0xb` | `0x00af6ba0` |
  | 4 | `0x00af6c00` (shared "nothing to do") | `0xc` | `0x00af6ba0` |
  | 5 | `0x00af6c00` | `0xd` | `0x00af6ba0` |
  | 6 | `0x00af6c00` | `0xe` | `0x00af6ba0` |
  | 7 | `0x00af6c00` | `0xf` | `0x00af6ba7` |
  | 8 | `0x00af6c00` | `0x10` | `0x00af6c00` |
  | 9 | `0x00af6c00` | `0x11` | `0x00af6c45` |
  | `0xa` | `0x00af6c00` | `0x12` | `0x00af6c00` |
  | | | `0x13` | `0x00af6c5f` |

  **Nine of the seventeen enter states (4–`0xa`, `0x10`, `0x12`) share the single stub `0x00af6c00`** — a `test dl,dl` / flag check that, absent the earlier-set attachment flags, calls the predicate `0x004d47e0(human)` and, if true, reads a float parameter off graph-state `0x1d95` through `0x004b2710(controller, 0x1d95)` (the one call site of this function in the whole root — this is one of the "animation API" calls scoping's own survey named at `0x004b2710`). Only states 3, `0xb`–`0xe`, `0xf`, `0x11` and `0x13` have state-specific bodies (below).
- **phase 2 (exit) → fallthrough at `0x00af6908` itself.** Reads state, subtracts `0x1b`, bounds-checks to `[0, 6]` (states `0x1b`–`0x21`), then `jmp dword ptr [ecx*4 + 0x00af8798]` directly — a genuine 7-entry table, read directly: `{0: 0x00af69c0, 1: 0x00af69df, 2: 0x00af6939, 3: 0x00af69df, 4: 0x00af69e9, 5: 0x00af69df, 6: 0x00af69da}`:

  | Exit state | Target |
  |---|---|
  | `0x1b` | `0x00af69c0` |
  | `0x1c` | `0x00af69df` (shared stub) |
  | `0x1d` | `0x00af6939` |
  | `0x1e` | `0x00af69df` |
  | `0x1f` | `0x00af69e9` |
  | `0x20` | `0x00af69df` |
  | `0x21` | `0x00af69da` |

  This confirms `spec-player-runtime.md` §6.1's independent statement, from the U6.6 side, that "the per-slot pose driver `0x00af6310` handles exit states `0x1b`–`0x21` only".
- **phase 3 (ride) → `0x00af6d7a` directly, no table.** No state read happens on this path at all; `0x00af6d7a` is reached unconditionally for every ride-phase slot. Several enter-state handlers also fall into this exact address once their attachment setup is done (§12.3), so it functions as the shared "apply the computed pose/orientation" tail rather than a ride-only routine.

**The fourth table, `0x00af87b4` (CONFIRMED by listing — not a phase/state table).** Exit state `0x1f`'s handler (`0x00af69e9`) does a second, private dispatch: `ecx = [eax + 0x10] − 0x14` (bounds `[0, 8]`, `ja → 0x00af69df`), then `jmp dword ptr [ecx*4 + 0x00af87b4]`. Read directly, its 9 entries are `{0x00af69fb, 0x00af69fb, 0x00af6a3d, 0x00af6adc, 0x00af6b3f, 0x00af69df, 0x00af69df, 0x00af69da, 0x00af69fb}` — every single one of them is also an enter-dispatch target (`0xb`–`0xe` → `0x00af69fb`... the same addresses the enter table's `0x00af6ba0`/etc. group reaches, confirmed by address equality, not by re-tracing the bodies a second time). `eax` here is the slot pointer again (`[eax+0x10]` is one of the generic per-state scratch fields `spec-player-runtime.md` §6.1 lists at slot `+0x10`/`+0x14`/`+0x18`); this sub-dispatch exists to route exit state `0x1f`'s own variants onto the already-existing shared handlers rather than duplicating them.

### 12.3 `0x00af6310` — what the per-case handlers actually do

All of the following read the vehicle (`edi`, resolved earlier via the same `0x004dcf00` the shared resolver uses) and the human (`esi`). CONFIRMED by listing unless marked.

- **`0x00af69c0`** (exit `0x1b`): reads human `+0x16d0` (not previously documented; stored into a local used by the shared tail as an attach-point selector) and `+0x16c8` (**seat**, `spec-player-runtime.md` §8.1's documented field), sets three local flags, and falls straight into the shared enter/exit tail at `0x00af6c02`.
- **`0x00af6939`** (exit `0x1d`): role only — not traced past its entry; reached only from this one exit state.
- **`0x00af69e9`** (exit `0x1f`): the private 9-way sub-dispatch of §12.2.
- **`0x00af69da`** (exit `0x21`), **`0x00af69df`** (exit `0x1c`/`0x1e`/`0x20`, and the shared out-of-range target for both the exit and the private sub-dispatch): short flag-setting stubs that merge into the same shared tail; not traced beyond their flag writes, which do not touch any named animation parameter.
- **`0x00af6be6`** (enter 3): reads vehicle `+0x18` off a seat-ish sub-record, sets the same three attachment flags as `0x00af69c0`, and falls into `0x00af6bfb`/`0x00af6c00`.
- **`0x00af6ba0`** (enter `0xb`–`0xe`): sets one flag and falls into `0x00af6c00`.
- **`0x00af6ba7`** (enter `0xf`): reads vehicle seat-descriptor data (`eax = [eax+0x18]`, and `edi (human) +0xbf4 → +0xa7c`, the vehicle-interaction descriptor chain `spec-player-runtime.md` §8.4 also walks for the seat-usability test) and tests descriptor flag bit `0x20` at a computed offset (`edi + eax·4·9/... + 0x4d`) before setting flags `+0x15`/`+0x19` and jumping to `0x00af6c00`. HIGH for the exact seat-descriptor semantics (the addressing matches the `Vehicle_Interaction_Info` seat-table pattern `spec-player-runtime.md` §8.1 already uses for `Hide Occupant`, but the specific bit was not independently named).
- **`0x00af6c45`** (enter `0x11`) and **`0x00af6c5f`** (enter `0x13`): both read vehicle fields `+0x10`/`+0x18` and set the same attachment flags before joining `0x00af6bfb`/`0x00af6c00`.
- **`0x00af6c00`** (the 9-state shared stub, and the exit stubs' merge point): `test dl,dl` / a local flag; if clear, calls `0x004d47e0(human)` (role: a controller-state predicate, not independently named) and, if true, reads `0x004b2710(controller, 0x1d95)` — a float off graph-state `0x1d95` — into the local the shared tail uses as a blend weight. Then, unless a second local flag (`+0x18`) is clear, builds a 4-argument call to **`0x00ab4d20`** (role only: a seat/vehicle vector query, args include the human's own handle at `+8`/`+0xc` and an attach-point index), does an SSE "all components finite" test (`cmpordps`/`movmskps`) on the result, optionally corrects it through `0x00da38e0` (the same vector helper `spec-anim-runtime.md` §4.2 rows 26–27 already cites for aim-vector transforms), and falls into the shared tail `0x00af6d7a`.
- **`0x00af6d7a`** (the ride tail, and the shared pose/orientation tail for most enter states): builds a basis from fixed constants at `0x01321400`/`0x0132140c`/`0x01321418` (not read as data — role only) and the vector from the step above, then calls **`0x00b0efe0`** (role only: also called by `0x00af4760`, §12.6 below — likely a ride/attach-key or placement lookup) and branches on whether it returns `-1`.
- **Shared final application (role only, structurally confirmed):** later in the function (`~0x00af84ba`–`~0x00af86a5`), the character-proxy pair **`0x0079efb0`** (get velocity) and **`0x0079efe0`** (set velocity) are each called exactly once, past all of the per-case setup — this is scoping's "character-proxy enable/disable pair", confirmed by call target but not traced instruction-by-instruction (the intervening ~500 instructions are SSE position/orientation blending that was not fully re-derived; this is the one part of the root left at HIGH confidence rather than CONFIRMED, matching the disclosed tracker limitation above).

**Net reading of scoping §5a.1 item 5:** "drives the occupant's pose and attachment through the animation API (`0x004b44a0`, `0x004b4320`, `0x004b46c0`, `0x004b2710`, …)" is CONFIRMED as to *which four functions* (all four are called, exactly once each, from the common preamble/shared tail, all keyed on the same cached graph-state id `0x1d95`), but the three-jump-table framing is corrected per §12.2.

### 12.4 `0x00af4b80(human)` — the rider-parameter writer, in full

**Signature and gate (CONFIRMED by listing).** Single argument, the human (`esi`). First caches all 11 parameter ids by name (lazy, one guard bit per name in `0x02845bb0`, exactly the latching idiom `spec-anim-runtime.md` §4.2 documents for `0x00958690` itself):

| Cache slot | Parameter name (read as a string literal) | Setter |
|---|---|---|
| `0x02845bac` | `Vehicle Steering` | float |
| `0x02845ba8` | `Vehicle Steering Time` | float |
| `0x02845ba4` | `Vehicle MPH` | float |
| `0x02845ba0` | `Vehicle Forward Lean` | float |
| `0x02845b9c` | `Vehicle Vert Lean` | float |
| `0x02845b98` | `Vehicle Tilt` | float |
| `0x02845b94` | `Vehicle Forward Lean Abs` | float |
| `0x02845b90` | `Vehicle Vert Lean Abs` | float |
| `0x02845b8c` | `Vehicle Tilt Abs` | float |
| `0x02845b88` | `Wheelie Bool` | bool |
| `0x02845b84` | `Endo Bool` | bool |

This is exactly `spec-anim-runtime.md` §4.1's census row for `0x00af4b80` — confirmed by direct string read rather than re-derived.

If the human is null, or `0x00b08380(human)` (the registry's cached-record lookup, `spec-player-runtime.md` §6.1) returns null, or `0x00b08390(record, occupant handle)` (the slot-index lookup, same citation) returns `-1`, the function returns **without writing anything** — the parameters keep whatever value they last held, same latching semantics as the human writer.

Otherwise it resolves `(record, slot)` through the shared resolver `0x00af4500` and reads the resulting **slot pointer's phase field (`+0x08`)**. The register holding it (`ebx` for the rest of the function) is confirmed to be the slot, not the vehicle, by its later use at `+0xe4`/`+0xec` (the ride body-lag error fields, below) — fields that only exist on the slot record (`spec-player-runtime.md` §8.1).

**Phase ≠ 3 (not actually riding — mid-enter or mid-exit): all 11 parameters reset (CONFIRMED by listing).** `Vehicle Steering`, `Vehicle Steering Time`, `Vehicle MPH`, `Vehicle Forward Lean`, `Vehicle Vert Lean`, `Vehicle Tilt`, `Vehicle Forward Lean Abs`, `Vehicle Vert Lean Abs`, `Vehicle Tilt Abs` are all written `0.0`; `Wheelie Bool` and `Endo Bool` are both written `false`. The function then returns. **This means the rider-lean/steering/wheelie parameters are only ever non-zero while the slot is in the ride state** — during the enter and exit animations they are neutral.

**Phase == 3 (riding): the real computation (CONFIRMED by listing; tweak values confirmed by direct read).** Let `V` be the resolved vehicle and `S` the resolved slot:

| Parameter | Value | Evidence |
|---|---|---|
| `Vehicle Steering` | `0.0` if `0x00a77a10(V)` (the "is remote-controlled" predicate, `spec-player-runtime.md` §9.1's census) is true; otherwise the float returned by `0x00a926b0(V handle)` | CONFIRMED call sequence; `0x00a926b0`'s internal computation is HIGH ("a steering-angle query") |
| `Vehicle MPH` | dot(`0x00abae00(V)` — the vehicle's linear velocity, the exact call `spec-player-runtime.md` §8.5 step 2 already cites — , the vehicle's forward row `V+0x64`/`+0x68`/`+0x6c` transformed through `0x00da38e0`) × `2.236936…` | CONFIRMED by listing; the constant at `0x01171c60` is read directly and is the exact m/s→mph conversion factor |
| `Vehicle Steering Time` | `0.0` (and a per-human timer at `human+0x19e0` reset to "not running", `-1`) while `abs(Vehicle Steering) ≤ 0.8` (constant at `0x01116448`, read directly); otherwise the timer is started if not running (`0x00d9e4c0` query, `0x00d9e140` start) and the parameter is the timer's elapsed time (`0x00d9e250`) × `0.001` (constant at `0x012a2d78`, read directly — ms→s) | CONFIRMED by listing and constant read |
| `Vehicle Forward Lean` | `remap(S+0xec, [-VI_forward_diff_max, +VI_forward_diff_max], [-1.0, +1.0], clamp)` via `0x00dade20` | CONFIRMED; `S+0xec` is the ride per-frame body-lag model's **forward error** (`spec-player-runtime.md` §8.5 step 4); `VI_forward_diff_max` is `0x02845b18`, registered by name at `0x0100a8c0` through the float registrar `0x0071b580` |
| `Vehicle Forward Lean Abs` | `abs(Vehicle Forward Lean)` | CONFIRMED (`fabs` immediately after) |
| `Vehicle Vert Lean` | `remap(S+0xe4, [-VI_vert_diff_max, +VI_vert_diff_max], [-1.0, +1.0], clamp)` via `0x00dade20` | CONFIRMED; `S+0xe4` is the ride per-frame body-lag model's **vertical error** (§8.5 step 5); `VI_vert_diff_max` is `0x02845b14`, registered at `0x0100a8e0` |
| `Vehicle Vert Lean Abs` | `abs(Vehicle Vert Lean)` | CONFIRMED |
| `Vehicle Tilt` | `remap(−pitch_lane(0x008f4df0(0x004add90(V+0x1db8))), [-VI_tilt_rvec_pitch_max, +VI_tilt_rvec_pitch_max], [-1.0, +1.0], clamp)` via `0x00dade20` | CONFIRMED by listing; `0x008f4df0` is the exact pitch-extraction call `spec-anim-runtime.md` §4.2 row 26 already cites for `Aim Pitch`; `0x004add90(V+0x1db8)` is HIGH ("a vehicle sub-object orientation/quaternion getter" — not independently named); `VI_tilt_rvec_pitch_max` is `0x02845b10`, registered at `0x0100a900` |
| `Vehicle Tilt Abs` | `abs(Vehicle Tilt)` | CONFIRMED |
| `Wheelie Bool` | `0x00ab4790(V)` | CONFIRMED call; the predicate's own internals are HIGH |
| `Endo Bool` | `0x00ab47e0(V)` | CONFIRMED call; the predicate's own internals are HIGH |

`0x00dade20`'s signature (value, in-low, in-high, out-low, out-high, clamp-flag) is read from all three call sites identically; it is the same generic "remap, optionally clamped" helper `spec-player-runtime.md` §5.4.3 step 5 already cites ("a `0x00dade20` remap capped at 0.5") for an unrelated locomotion lean.

All nine float writes go through **`0x004b1510`** and both bool writes through **`0x004b1410`** — the exact two of the "three typed setters" `spec-anim-runtime.md` §3.2 documents — on controller `= [[human+0xd24]+0x10]` throughout, the same convention `spec-anim-runtime.md` §4.2 uses for `0x00958690` itself.

**This closes the `0x00af4b80` half of `spec-player-runtime.md` §8.5/OPEN 63.** That OPEN item flagged "the readers of the lag values slot `+0xe0`/`+0xe8` were not traced (HIGH: U6.8's pose driver)" and asked for the registrars `0x0071b580`/`0x0071b530`'s loader. The readers are `0x00af4b80` itself (slot `+0xe4`/`+0xec`, the vertical/forward *error* terms, not `+0xe0`/`+0xe8` the *lag* terms — the lag terms feed the error terms inside `0x00b09ab0`, §8.5, and are not read anywhere else); the registrar `0x0071b580` is confirmed (again) as the float tweak registrar, and three more of its registrations (`VI_forward_diff_max`, `VI_vert_diff_max`, `VI_tilt_rvec_pitch_max`) are now named. The tweak loader that supplies their shipped *values* was still not located (same as the original six `VI_*` tweaks) — that half of OPEN 63 stays open.

### 12.5 `0x00af4150(human)` — the rider fall/eject detector (not a parameter writer)

**Gate (CONFIRMED by listing).** Single argument, the human. Returns at once if `0x008addb0(human, 0)` is false (not locally authoritative — the same ownership test `spec-anim-runtime.md` §4.3 uses for network-forwarded parameter writes), or if the human's vehicle handle (`+0x16c0`/`+0x16c4`) does not resolve through `0x00458230(0x024433a8, &handle, 0x031d152c)` — the identical resolver call the two walkers use (§12.1) — to a live object, or if that object's flag `+0x33` bit `0x10` is set, or if its class-row (`0x02cc9900`) byte `+6` bit `0x80` is clear, or if `0x00853b10(object)` is true. The resolved object (`ebx`) is the human's **vehicle**.

**State gate.** Reads human `+0x16d4` (the same field `spec-player-runtime.md` §8.0/§8.1 documents as becoming 3 once seated). Returns if it is ≤ 0. If it is exactly 1 or 2, a short alternate path runs (below); if it is 3, the main body runs; any other value returns doing nothing.

**Main body (CONFIRMED by listing).** Computes a predicted position by extrapolating the human's position (`human+0x40`) one step further along its offset from a vehicle-side reference point (`0x0056e340(vehicle)`, role only), using double-precision subtraction/addition per axis for accuracy, then transforms it through `0x00da38e0`. It then calls **`0x009a48e0(human, …, name-ptrs at 0x01185770/0x01185774, …, 1, 0, 0)`** — the exact knock-down predicate `spec-player-runtime.md` §5.4.2 already cites for a different, non-vehicle context ("every 3 s an 8% chance of a knock-down when moving faster than 25 units/s"). If it returns true and the vehicle is non-null: sets human flag `+0xdc` bit `0x80`, computes a second extrapolated offset the same way, transforms it through `0x004d6210` (the exact aim-point transform `spec-anim-runtime.md` §4.2 rows 26–27 cite), scales it by a 4-float vector constant at `0x02845b60` (not read as data — role only, presumably another `VI_*`-style tweak vector), and finally calls **`0x00781 8f0(human, 0, &scaled_offset, 0)`** — read as an impulse/launch application by its argument shape (position vector + flags on a human), not independently named elsewhere in the landed specs.

**`+0x16d4` ∈ {1, 2} alternate path.** A shorter variant of the same position math (via `0x00b0efe0` — the same function `0x00af6310`'s ride tail calls, §12.3 — and `0x0056e340`/`0x00da38e0`) feeds into the *same* `0x009a48e0` call before the same flag-and-impulse consequence. The two values `+0x16d4` can hold here beyond the documented "3 = seated" were not identified (OPEN, below).

**Conclusion:** `0x00af4150` detects a rider being thrown or falling away from the vehicle it is nominally attached to (via the engine's general knock-down test, reused rather than duplicated) and, when that happens, flags the human and shoves it along the predicted departure vector. It writes no named animation-controller parameter and does not belong next to `0x00af4b80` as "a rider writer" in the literal sense — scoping's row groups it there because it is rider-adjacent and lands in the same instruction band, not because it shares `0x00af4b80`'s contract.

### 12.6 `0x00af4760(human)` — the ride-`Element` key builder (resolves half of OPEN 71)

**Gate (CONFIRMED by listing).** Single argument, the human. Returns `-1` if null, or if `0x00b08380(human)` (the cached-record lookup) returns null, or if `0x00b08390(record, occupant handle)` returns `-1` — the same two calls `0x00af4b80` opens with (§12.4), confirming both functions share the record/slot resolution path. Otherwise resolves `(record, slot)` through `0x00af4500`.

**Phase == 3 (riding): no key is built.** If the resolved slot's phase (`+0x08`) equals 3, the function just calls **`0x00b09400(seat index)`** — the exact pose-snap `spec-player-runtime.md` §8.1 cites for instant entries ("instant entries snap the seat pose (`0x00b09400` → `0x004b21a0`)") — and returns. Ride-key selection happens *before* the slot is actually in the ride phase.

**Phase 1 or 2: a long decision tree over state (`+0x0c`) and a variant field, CONFIRMED by listing.** Most branches return a small sentinel/code (`2`, `0x1a`, `0x1ac`, `0x2c`, a cached value off human `+0x1cc8` or `+0x1ce0`) without touching the ride tables — these codes are consumed by the caller `0x0095cbc0` and were not independently resolved here (OPEN, below). A specific subset of states (`0x15`, `0x16`, `0x1b`, `0x1d`, and `0x1f`/`0x14` under further sub-conditions on a second field) instead:
1. Builds a **six-token key** from the slot's state/variant fields into stack buffers (the same `lea`+`push` shape `spec-player-runtime.md` §9 (U6.1) documents for the enter/exit key builder `0x00b0a6f0`, just producing six tokens instead of five).
2. Calls **`0x00b08b80`** directly — confirmed by listing (`call 0x00b08b80` at `0x00af4a18`) — passing the key buffers and the seat/vehicle context.
3. Returns the matched animation id read back out of one of the buffers.

**This is a direct, confirmed answer to `spec-player-runtime.md` OPEN item 71's "who builds the six-token key passed to `0x00b08b80` (callers `0x00b08ea0` and `0x00af4760`)?"** — the `0x00af4760` half is `0x00af4760` itself, building the key from slot state/variant fields during the enter/exit phases (not during ride), for the purpose of choosing which `vi_ride.xtbl` `Element` the slot should transition into once it reaches phase 3. `0x00b08ea0`'s own key-building path is a separate caller and remains open; this unit does not own `0x00b08ea0`.

One side branch (state `0x1f`, variant `0x14`, with human `+0x1ce8`'s item-in-hand test `0x00941f40` and human flag `+0xec` bit `0x100000` clear) instead calls **`0x004b1630(controller, 0x45)`** — not one of the three typed setters (`0x004b1410`/`0x004b1490`/`0x004b1510`) and not independently named; by its argument shape (controller, small integer id) it is most likely a `play`-family call (`spec-anim-runtime.md` §6.5) triggering a one-shot animation cue (id `0x45`) rather than a parameter write, gated on the rider holding an item. Left HIGH/role-only.

### 12.7 Cross-reference to the animation-controller parameter contract

Every name `0x00af4b80` writes is already in `spec-anim-runtime.md` §4.1's census table for `0x00af4b80`; §12.4 above replaces that row's "value sources are U7's" deferral with the real computation, confirms the controller convention, and supplies the three new tweak names. No other function in this unit writes a named parameter: `0x00af6310` drives pose through the animation *API* (play/query/layer calls on cached graph-state `0x1d95`), not through named controller parameters; `0x00af4150` and `0x00af4760` touch neither. `spec-anim-runtime.md` §4.1's own note that the vehicle/rider/collision writers' "value sources are U7's" should be read as settled for `0x00af4b80` specifically — U7 ("other controllers on demand") should cite this section rather than re-derive it, per the cross-unit ownership note this unit was briefed with.

### 12.8 What a port must reproduce

1. **The dispatch of §12.2** — phase first, then state, with the real (not the originally-surveyed) table shapes; a port can collapse the many single-purpose case addresses into the handful of distinct behaviors §12.3 lists (attachment-point selection, shared "nothing case-specific" stub, seat-descriptor flag test) without losing behavior, since that is what the shipped binary itself does via table-folding.
2. **The shared pose/orientation tail** and the character-proxy get/set pair, at the role-only level of confidence given in §12.3 (a full re-implementation needs the un-traced SSE orientation math; the call targets and ordering are confirmed).
3. **`0x00af4b80`'s complete, phase-gated parameter table (§12.4)** — this is the piece most directly reusable by a port or by U7: 11 names, written only in the ride phase, nine of them derived from three bounded tweaks and the ride body-lag model's existing error terms, two from vehicle-side wheelie/endo predicates.
4. **`0x00af4150`'s fall/eject behavior (§12.5)**, if the port wants riders who can be thrown off; otherwise it can be skipped entirely without affecting any animation parameter.
5. **`0x00af4760`'s ride-`Element` selection (§12.6)**, needed only if the port wants motion-sensitive ride animations (wheelie/stoppie/turn variants of `vi_ride.xtbl`) rather than a single fixed ride pose.

### 12.9 Corrections and cross-spec flags raised by U6.8

- **`review/scoping-player.md` §5a.1 item 5 and §1.7:** the "three jump tables… covering enter states 3–`0x13`, exit states `0x1b`–`0x21` and ride `0x22`" description is corrected in place (light-touch strikethrough) — see §12.2 above for the real structure. The three addresses themselves were exactly right.
- **`review/scoping-player.md` §5a.3's U6.8 row:** "the rider writer `0x00af4b80`… `0x00af4150`… `0x00af4760`" is corrected in place — only `0x00af4b80` is a parameter writer; `0x00af4150` is the fall/eject detector and `0x00af4760` is the ride-key builder (see the strikethrough+correction applied to that file).
- **`spec-player-runtime.md` §8.5 / OPEN item 63** (not edited here — file race; flagged for the splice): the `0x00af4b80` half is closed. The readers of the ride body-lag model's **error** fields (slot `+0xe4`/`+0xec`) are now identified as `0x00af4b80`; the **lag** fields (`+0xe0`/`+0xe8`) have no reader outside `0x00b09ab0` itself. Three more `VI_*` tweaks are now named (`VI_forward_diff_max`, `VI_vert_diff_max`, `VI_tilt_rvec_pitch_max`, all registered through `0x0071b580` alongside the original six). The tweak *values* (loader) remain unlocated for all nine tweaks now on record.
- **`spec-player-runtime.md` OPEN item 71** (not edited here — file race; flagged for the splice): the `0x00af4760` half is closed (§12.6 above). `0x00b08ea0`'s half is untouched and still open.
- **`spec-anim-runtime.md` §4.1's writer census** (not edited here — not this session's file to touch, and not raced, but outside this unit's remit to splice): the `0x00af4b80` row's "value sources are U7's" can be replaced with a citation to this section once spliced; §12.4 above is the complete, real specification the cross-unit ownership note asked for.

## 13. U6.2 — Entry trigger and vehicle/seat choice

**Scope.** The second sub-unit of U6 as `review/scoping-player.md` §5a.3 defines it: the enter/exit dispatch `0x009e5e80`, the request builder `0x009e1e60`, the nearest-vehicle auto-pick `0x00b105c0`, the per-vehicle eligibility-and-claim gate `0x00b0fdf0`, the seat-scoring helper `0x00b0eb90` (scoping's HYPOTHESIS, settled below), and the two obstruction gates `0x00b0da10`/`0x00b0dc20`. This unit answers scoping §4.2 OPEN item 9 and confirms §5a.4 point 3's AI-sharing claim. It **ends at the submitter** `0x00af22a0`, which `spec-player-runtime.md` §7.4 (Player U6.0) already specifies in full — cited here, not repeated. The behaviour *inside* a committed slot (the actual seat/door resolution, the state machine, the element-selection rule) belongs to U6.0/U6.1/U6.3–U6.8 and is cited by section, not re-derived.

**Method.** Ghidra-free, as for every other unit in this file: capstone listings over `tools/harnesses/exe_va.py` (a local `listfn.py` harness built for this pass, same disassembly approach — direct byte reads plus `capstone.Cs(CS_ARCH_X86, CS_MODE_32)`, no decompiler), with manual esp-depth tracking to resolve stack-relative operands to request/slot field offsets. Read in full: `0x009e5e80` (109 insns), `0x009e1e60` (74), `0x00b105c0` (~225), `0x00b0fdf0` (~616, across several passes), `0x00b0eb90` (~300), `0x00b0da10` and `0x00b0dc20` (both SEH-wrapped, mid-sized). Read in part, for role only: `0x00614d00`→`0x006d2910` (a session/mode-kind getter), the interaction-prompt dispatcher `0x005b6af0` (603 insns; only the portion around its `CBA_GAC_ACTION` test and its table walk `0x005b6870` was read), and the two AI call sites `0x0051d5b0`/`0x00521200` (read far enough to confirm each calls `0x00b0fdf0`). A static read of the 22-entry prompt-handler table at `0x0111f2d0` (and its sparser sibling at `0x0111f430`) resolved scoping OPEN item 9. No Ghidra project was opened or copied. No `.czn_pc` data was touched.

### 13.0 Results up front

1. **The trigger is a single shared button, not a per-prompt one.** The interaction-prompt routine `0x005b6af0` tests **`CBA_GAC_ACTION` (`0x52`, `spec-tables-ui-controls.md` line 120)** — "held" via `0x005bae10`, gating a walk of a **22-entry handler table at `0x0111f2d0`**, one slot per bit of the player's per-frame context word **`+0x2084`**. **Slot 15 is `0x009e5e80`** (CONFIRMED by a static read of the table), matching scoping's "prompt entry 15". This answers OPEN item 9: there is no per-prompt test function, only the one shared button and a per-prompt availability bit that something else inside `0x005b6af0` sets (not traced here — new OPEN item below).
2. **`0x009e5e80` is a two-way dispatch, not a single path.** Already seated (`+0x16d4` = 3 via the generic predicate `0x009b9160`) → builds a `0x33`-byte exit request on its own stack (constructor `0x004dccd0`, same constructor U6.0 §7.2 cites for the slot's exit-request field) with its own debounce byte `+0x30` bit `0x80` set, and calls the exit request **`0x00af3cd0`** (U6.6, §6.3 — cited, not re-derived), gated by a per-player cooldown timer at `+0x20c4` (`0x00d9e4c0`/`0x00d9e140`) and the AI sub-state byte `+0x500` (`spec-ai-runtime.md` §2.5). Not seated → reads **`CBA_OFC_SPRINT`** (`0x12`) as an edge test (`0x005bd7b0`) **or** `+0x1d96` bit 2, and calls `0x009e1e60(flag)` — confirming scoping's "flag = sprint pressed or `+0x1d96` bit 2" exactly, including which query and which bit.
3. **`0x009e1e60` builds the whole `0x44`-byte request record on its own stack** (not a pre-existing one) and ends by calling the submitter directly — with **vehicle = 0 in every ordinary case**, even the forced-seat path. The only two things it customises are the request's **seat** (`+0x00`) and **search mode** (`+0x0c`); it never resolves a vehicle object itself. A co-op/solo special case (session-kind getter `0x00614d00`→`0x006d2910`, the co-op player lookup `0x009df3d0`, `spec-lua-api-behaviour.md` §3.1/§14.31) can force seat = 1 (passenger), mode = 0. A **new baseline value, request `+0x08` = 2**, is preset here before either branch runs — outside the `{0, 1}` domain `spec-player-runtime.md` §7.2 documents for that field from the submitter's side (flagged below).
4. **Neither `0x00b105c0` nor `0x00b0fdf0` ever writes the chosen seat or door back into the request.** `0x00b105c0` only *finds a candidate vehicle* (nearest-two by squared distance, with a "sticky" override from a previously-unnamed player field) and hands each candidate to `0x00b0fdf0` to *validate*. `0x00b0fdf0` runs a long chain of eligibility gates — including the seat-scoring helper `0x00b0eb90` — but that helper has **no pointer to the request at all**, so it cannot write a result into it either. **The actual seat, door and entry-point fields (slot `+0x20`/`+0x24`, request `+0x7e`/`+0x7f` bit 2) are resolved later**, on the registry record's next 250 ms tick, by the per-record upkeep `0x00af5cb0` (`spec-player-runtime.md` §7.6, Player U6.0 — already specified), which calls `0x00b0eb90` a second time to do the real pick. U6.2 validates feasibility; U6.0's upkeep commits the choice.
5. **`0x00b0eb90` is confirmed as the seat-scoring/eligibility gate** scoping flagged as HYPOTHESIS. Its signature (8 packed arguments) and full per-seat filter set are recovered below, including the tie-in to the element-selection rule (`spec-player-runtime.md` §9.5) that U6.1 already documented from its own call site — the two passes agree exactly.
6. **`0x00b0da10` and `0x00b0dc20` are a pair of SEH-wrapped obstruction/clearance checks**, both `(character, vehicle)`, run only when the vehicle's physics body is active (`+0xbd0` = 1). `0x00b0dc20` applies **only to players** (an `is-player` gate up front; an AI caller always passes it trivially). `0x00b0da10` applies to any character but is skipped outright for an **instant** request or when `0x00971cf0(character)` returns null. Either can veto the whole eligibility check. Their own query callees (`0x0075d7e0`, `0x0075c880`) were not traced — flagged as a new OPEN item, likely physics-runtime territory.
7. **Scoping §5a.4 point 3 is confirmed by direct read, with a refinement.** `0x00b0fdf0` is indeed called from two AI-side functions, `0x0051d5b0` and `0x00521200` (both confirmed by listing), so NPC `Veh enter` does reuse this same validation. Neither address is one of the named mode-38 slots in `spec-ai-runtime.md`'s table (`0x00521320`/`0x00520cd0`/`0x005213d0`/`0x005215a0`/`0x00520ca0`/`0x00521060`/`0x005210d0`/`0x00521f40`), so they are helpers the AI side calls from somewhere in that machinery or from an action routine — which one is AI-side work, not re-derived here.

### 13.1 The trigger: interaction prompt → `0x009e5e80`

- **Button.** `0x005b6af0` (603 insns, the interaction-prompt routine already named in scoping §1.7/§1.3 step 6) gates a whole block on `+0x2074` bit 0 (one of the six per-frame context words zeroed by `0x005b7430`, `spec-player-runtime.md` §3.2), then tests **`CBA_GAC_ACTION`** (`0x52`) three ways in sequence (CONFIRMED by listing around `0x005b70a0`–`0x005b7142`):
  - **held** (`0x005bae10(0x52, 0)`) → walk table `0x0111f2d0` via `0x005b6870`, starting at bit 0;
  - else **pressed this frame** (`0x005bd7b0(0x52, 0)`) → call `0x005ac340` directly (a different, single-purpose handler, not a table walk — not traced further, out of scope);
  - else **held past a duration threshold** (float constant `0x0127e354`, tested by `0x005bde80(threshold, 0x52)`) → walk table `0x0111f430` first, then `0x0111f2d0` starting from a rotated index, via `0x005ac390`.
  
  So the three walkers scoping listed (`0x005b6870`, `0x005b68e0`, `0x005ac390`) are three different **dispatch modes of the same button**, not three different buttons. `0x005b68e0` (a fourth test, further down the same function, not traced in this pass) is presumably a repeat of the "held" mode for a second context.
- **The table itself.** `0x0111f2d0`: **22 entries, 4 bytes each, one function pointer per slot — not a {test, handler} pair** as scoping's diagram implied. `0x005b6870` tests bit `i` of player `+0x2084` for each slot `i` in turn; a set bit with a non-null pointer calls it, and the first handler to return true wins. **Static read (CONFIRMED): slot 15 = `0x009e5e80`.** The sibling table `0x0111f430` (used by `0x005ac390` first) is almost entirely empty (only slot 7 = `0x009dcd60` is populated) — it looks like a small set of higher-priority prompts checked before the main 22.
  - **What sets bit 15 of `+0x2084`** (i.e., what decides a vehicle-entry prompt is available this frame) was **not traced** — it is somewhere else in `0x005b6af0`'s 603 instructions, outside U6.2's root list. New OPEN item below.
- **`0x009e5e80` (109 insns, CONFIRMED by full listing).** Single argument, the human/player pointer (`edi`):
  1. If `edi+0x16d8` or `edi+0x16dc` is non-zero, **returns false immediately** without doing anything else. This pair is read nowhere else in this pass; it is not the human's own vehicle handle (that is `+0x16c0`/`+0x16c4`, tested separately below) and it does not match any field `spec-player-runtime.md` has named at that offset. HYPOTHESIS: some other in-progress interaction (busy/pending) flag. New OPEN item below.
  2. Otherwise, **`0x009b9160(edi)`** — the generic "`+0x16d4` = 3, seated" predicate `spec-player-runtime.md` §7.8 already names as a reader of that field.
     - **Seated** → resolve the vehicle (`edi+0x16c4`/`edi+0x16c0` through `0x004dcf00`, same resolver U6.0 §7.3 names), require vehicle flag `+0x33` bit `0x08` and `0x005fb8e0(vehicle)`, then a per-player debounce timer at `edi+0x20c4` (`0x00d9e4c0`/`0x00d9e140`, armed from the global duration `0x0262e9b8`) and the AI sub-state byte `edi+0x500` (`spec-ai-runtime.md` §2.5): unless the timer has not yet elapsed **or** the sub-state is `0x8c`, it builds a fresh `0x33`-byte exit request on its own stack (constructor `0x004dccd0`), sets that request's `+0x30` bit `0x80` (a flag not in `spec-player-runtime.md` §6.2's exit-request field table — new finding), and calls **`0x00af3cd0(edi, &request)`** (the exit request, `spec-player-runtime.md` §6.3). When the debounce/sub-state gate fires instead, it just re-arms the timer and returns true without exiting.
     - **Not seated** → reads **`CBA_OFC_SPRINT`** (`0x12`) as an edge test `0x005bd7b0(0x12, NULL)`, or `edi+0x1d96` bit `0x4` (bit 2); `flag = 1` if either is true, else `flag = 0` (the fallthrough `eax` is the bool-return register from the sprint query, already 0/1). Calls **`0x009e1e60(flag)`** and returns its result directly.

### 13.2 Building the request: `0x009e1e60(flag)`

**CONFIRMED by full listing (74 insns).** Frameless; `edi` (the player, still live from the caller — this function relies on the caller not having clobbered it, an optimiser artefact, not a passed argument) and `flag` (the one stack argument) are its only inputs. It allocates its own `0x44`-byte request record on the stack and never receives one from outside.

- **Default fields written** (mapped to `spec-player-runtime.md` §7.2's request-field table by offset, esp-depth tracked against the `lea eax,[esp+0xc]` used as `&request` at the call sites below):

  | Request field | Value | Matches §7.2? |
  |---|---|---|
  | `+0x00` (seat) | `-1` | yes |
  | `+0x04` (passenger search start) | `-1` | yes |
  | `+0x08` | **`2`** | **new** — §7.2 only documents `{0, 1}` here, set by the submitter |
  | `+0x0c` (search mode) | `1` ("any seat from 0") | yes |
  | `+0x10`/`+0x14` | `-1` | yes |
  | `+0x18`/`+0x1c` | `-1` | yes |
  | `+0x23` bit 0 | `flag` (sprint/`+0x1d96` bit 2) | **refines** — §7.2 attributes this bit only to "a retry/'change seat' marker in the upkeep"; it is first *written* here, at construction, from the caller's intent |
  | `+0x24` | `0` | — (not previously itemised; eligibility-predicate bit, §7.2's `+0x24` row) |
  | `+0x28`/`+0x2c`/`+0x30` | `-1` | yes (the "`-1` on reset" range) |
  | `+0x34` | `0` | yes |
  | `+0x3c` (completion callback) | `0` (null) | yes |

- **Co-op/solo special case.** `call 0x00614d00` (a thin `jmp` into `0x006d2910`, a session/mode-kind getter) compared against `4`; if equal, `0x009df3d0()` (the co-op-player lookup `spec-lua-api-behaviour.md` §14.31 and `spec-player-runtime.md` §1.5 already cite) is called, and if it returns null (no co-op partner), **`+0x0c` = 0** and **`+0x00` = 1** — i.e. the lone player defaults straight to the passenger seat rather than "any seat". HIGH confidence on the mechanics; the exact enum value `0x006d2910` returns for "4" was not chased (it plausibly backs `coop_is_active`'s session predicate, `spec-lua-api-behaviour.md` §3.1, but that identification is HYPOTHESIS).
- **Forced seat** (`player_force_vehicle_seat`, `edi+0x2898` ≠ `-1`, `spec-lua-api-behaviour.md` §17.6): `+0x00` is set to the forced seat value in both sub-branches below, and `+0x0c` = 0 (exact seat) when a forced vehicle is also present (`edi+0x28a0`/`edi+0x28a4` non-zero):
  - if a forced vehicle handle is set, it calls **`0x00b105c0(edi, &request)`** (§13.3 below) and compares the *returned* candidate's own handle (`+0x08`/`+0x0c` on the returned object) against the forced-vehicle handle — purely as a validation that the nearest-vehicle scan agrees the forced vehicle is actually there; **the returned object pointer itself is discarded**, and `+0x0c` is set to 0 regardless of which way the comparison goes. This is a redundant double-resolution: the vehicle object found here is never passed on, because the call below still passes vehicle = 0, and `0x00b105c0` runs its full scan again inside the submitter.
- **Common tail.** Builds `&request = esp+0xc`, pushes `(request, vehicle=0, character=edi)` and calls **`0x00af22a0`** — the submitter, `spec-player-runtime.md` §7.4 — and returns its result directly. **In no path examined does `0x009e1e60` pass a non-zero vehicle to the submitter**, even when a forced vehicle exists; the submitter's own call to `0x00b105c0` (§7.4 step 4) is what actually resolves a vehicle object for every player-triggered entry.

### 13.3 Auto-pick: `0x00b105c0(character, request)` (CONFIRMED by full listing, ~225 insns)

Called from the submitter (`spec-player-runtime.md` §7.4 step 4, "without one, `0x00b105c0(character, request)` picks it") whenever the submitter's own vehicle argument is null — which, per §13.2, is **every** player-triggered path.

1. Gets the character's position (`0x00da38e0` on `character+0x40`) and the **local-player singleton** (`0x009da4e0`, `spec-lua-api-behaviour.md` §3 preamble).
2. **A "sticky" override, new finding.** If `character` *is* the local player, it reads a handle pair at **player `+0x1e30`/`+0x1e34`** (not named by `spec-player-runtime.md` §1.5's player-field list). If that handle resolves to a live object (through the generic handle resolver `0x00458230` with type-tag `0x031d152c`, the same pattern U6.0's own champion-handle code uses) **and** the object passes a kind-row bit-`0x80` test **and** `0x00853b10(object)` is false, the object becomes a preferred candidate (held in a local). If `0x00853b10` is true instead, the override is cleared. HYPOTHESIS: this is a "currently-tagged/targeted vehicle" the player is looking at or standing near, carried over from an earlier frame, used to avoid flicker between two nearby vehicles. Not named elsewhere — new OPEN item below.
3. Walks the global object list through a manager singleton at **`0x003171a64`** (not named by any spec read in this pass) and, for each object:
   - skips it if `0x00853b10(object)` is true, if the object is null, if vehicle flag `+0x33` bit `0x08` is clear, or if `0x00ad3340(object)` is true;
   - otherwise computes a squared distance (via a local-space transform through `0x00da38e0`/`0x00467ed0`-style helpers) and rejects it outright if it exceeds a fixed max-range constant at **`0x01115204`**;
   - inserts it into a 2-slot sorted array by that distance (closest first) — **unless** it is the "sticky" object from step 2, whose key is forced to `0` so it always sorts first.
4. For each of up to 2 surviving candidates, closest first: calls **`0x00b0fdf0(character, candidate, request)`**. On failure, if request `+0x23` bit 0 is set, it clears that bit and retries the **same** candidate once more before moving on to the next slot. The first candidate that makes `0x00b0fdf0` succeed is returned (the vehicle object pointer); if both candidates fail, returns 0.

So `0x00b105c0` never consults the request's own seat/mode fields to narrow its *vehicle* search — proximity (plus the sticky override) decides the vehicle; `0x00b0fdf0` decides whether that vehicle, with that request, is viable at all.

### 13.4 Validate and claim: `0x00b0fdf0(character, vehicle, request)` (CONFIRMED by full listing, ~616 insns)

Called either directly by the submitter (when a vehicle was already specified — e.g. `vehicle_enter_do` on an explicit target, `spec-lua-api-behaviour.md` §38.1) or once per candidate from `0x00b105c0` above. Returns a single bool; **it writes nothing back into the request or the slot** (see §13.0 item 4). In order:

1. **Vehicle descriptor flags vs. the request, and generic predicates (all refuse on failure):**
   - vehicle descriptor flag `+0x1d7b` bit `0x40` set → refuse unless request `+0x20` bit 2 is set (this is `spec-player-runtime.md` §7.2's documented bit, here confirmed read a second time, inside this function, not only by the submitter as §7.4 step 5 implied — **both places test it**);
   - the *same byte's* bit `0x80` (sign bit) — **new, not in §7.2's table** — additionally refused, but only for a player character (`0x00853b30(character, 0x21)`), unless request `+0x20` bit 2 is set;
   - `0x00853b10(vehicle)` true → refuse;
   - `0x00a77de0(vehicle)` true → refuse;
   - request **`+0x22` bit `0x80`** — **new field, not in §7.2's table** — clear, and character is a player, and `0x00ad3820(vehicle)` true → refuse;
   - seat count (`0x00ab4af0(vehicle)`, forced to 1 when vehicle flag `+0x16c2` bit 0 is set) must be ≥ 1.
2. **Seat-scoring gate — `0x00b0eb90`** (§13.5 below). Packs 8 arguments from the request and character and asks "does at least one seat match". Failure refuses the whole vehicle.
3. **Dead-driver / occupant-state exclusions:** `0x007e2d90(vehicle)` true → refuse; vehicle `+0x16c0` negative or with bit `0x200` set, gated by the player-class test again (`0x00853b30`, `0x21`) and `0x0097e280(vehicle)` → refuse.
4. **Vehicle category / speed gate on the driver seat:** if vehicle flag `+0xc8` bit 0 is set and `+0xc4` (a float) is below a global threshold `0x0126d2cc`, `0x00ad2f80(vehicle)` (the driver-class switch `spec-ai-runtime.md` §1.9 also calls) must equal 5, else refuse.
5. **`0x00ad3140(vehicle)`** decides which of two large blocks runs next (true → the block at `0xb0ff7a` onward described below; false → `0xb10009`, skipping it). **`0x00ad3140`'s own test is now resolved (2026-10-07, disassembly): it is `(vehicle+0xbf4)+0x4c4 == 0xe` — i.e. "is this vehicle tagged `Special_Vehicle_Type: Tank`," matching this section's own prior "tank" readings elsewhere in this unit exactly.**
6. **(when step 5 is true) An 8-seat scan for a vehicle-wide team conflict.** For each seat `0`–`7`: find a claimed (`0x00ab5070`) or pending (`0x00ab5150`) occupant; if found and not dead (`0x0096f4f0`), compare that occupant's condition-class byte (`+0x1ca8`) against the requesting character's own via `0x004fa040`. **Any conflicting pair fails the whole function** (jumps to the common refusal tail). This is a vehicle-wide gate, independent of which seat the request itself wants.
7. **A specific-seat contest check, only when request `+0x00` (seat) is not `-1`.** Re-reads the seat descriptor bit `+0x4d` bit 3 and request `+0x30` (`-1` = "no existing claim id" — a new reading of a field §7.2 only described as "`-1` on reset") to set a local "direct-claim eligible" flag. **When request `+0x23` bit 0 (the fast/sprint flag written in §13.2) is clear**, and the direct-claim flag is set, it additionally checks the *distance* from the vehicle to whichever character currently claims that seat (`0x00b0a330`, then a squared-distance compare via `0x00da1330`) and **refuses if that claimant is still close enough to matter** — a proximity-based eviction of stale claims, not a hard "seat taken" refusal. When request `+0x23` bit 0 is set, or the seat isn't contested, this check is skipped entirely.
8. **Common success tail (`0xb10263`):** sets a local result byte to `1` (success-so-far) and then runs a further sequence that can still clear it to `0` without aborting the rest:
   - entry-angle-class bits (request `+0x7e` bits 1/2, written through `0x00ad4160`, the character's bearing) and an "instant" bias pulled from vehicle-size constants `0x01186904`/`0x01186908`;
   - a grace-period check keyed on **vehicle `+0xc68` == 2** (the same field U6.4's corrections already note is the vehicle's own AI-object mode) with a timer at vehicle `+0x1ac4`;
   - dead-driver eviction (`0x00ad2ed0`) and, **oddly, the LOCAL PLAYER's own** `cant_enter_exit_vehicles` flag (`+0x28ac` bit `0x10`, `spec-lua-api-behaviour.md` §4.6/§14.29) gating whether an existing driver can be bumped (via `0x00456920`, `0x0097e280`, and a condition-class comparison at `+0x1ca8`) — this reads the **local player's** flag regardless of whether the *requesting* character is the local player; HYPOTHESIS this is intentional (only the local player's own preference should block evicting someone to make room for them) but it was not verified against a non-player caller;
   - team checks `0x00ad2010`/`0x00ad22f0`;
   - a **vehicle-type-dependent entry-speed threshold**: `0x00ad32d0`/`0x00ad31d0`/`0x00ad3290` (three single-predicate vehicle-type tests) each select one of three float constants (`0x01117a58`, `0x01187ecc` ×2, `0x01116444`) or, when the character is already the seat's claimed occupant, one of two globals at `0x00288cb0c`/`0x00288cb10`; the character's current speed is compared against it, and unless the request is instant (`+0x20` bit 0) or forced (`+0x23` bit 3), exceeding it clears the result byte;
   - finally the two obstruction gates, **`0x00b0dc20(character, vehicle)`** then **`0x00b0da10(character, vehicle)`** (§13.6), either of which can also clear it.
9. Returns the accumulated result byte.

**Routed cross-check against real mission Lua (2026-10-09, mission-blocking: m05/m07/m09).** Team
B had been resolving an eligible-and-untested seat-pick outcome straight to `true` ("seat pick
assumed to succeed"). Real mission scripts were extracted (`spec-vpp-container.md` §7.6) and
checked: **m05 and m09 never check the return value of any vehicle-enter call** — fire-and-forget,
consistent with (but not a justification for) always resolving true. **m07 is different and is the
one that actually matters:** `m07_setup_pony_carts()` has `while not
vehicle_enter_teleport(LOCAL_PLAYER/REMOTE_PLAYER, pony_cart.vehicles[1], seat 1/2) do
thread_yield() end` — an explicit retry-on-refusal loop, with a mission-author comment ("so Pierce
doesn't block the co-op player") showing the refusal case anticipated is exactly step 6/7's
occupant/team-conflict gate above. **A port must not hardcode eligible-and-untested → true** — it
needs to be able to return false for occupant conflicts on a held seat (the one m07 actually
exercises), team conflicts, capability/disabled seats, obstruction, and overspeed, or m07's own
retry loop never progresses. Full detail:
`scratchpad/mission_block_seatpick_vehiclegroup_2026-10-09.md`.

### 13.5 Seat scoring: `0x00b0eb90` — scoping's HYPOTHESIS, now CONFIRMED (~300 insns, full listing)

**Signature, recovered from the call site inside `0x00b0fdf0` (push order, cdecl):**

`0x00b0eb90(vehicle, seat_index_or_−1, first_seat_for_passenger_search, character, 0, request+0x20 bit 4, request+0x23 bit 4, request+0x23 bit 0)`

(`[ebp+8]`…`[ebp+0x24]` in its own frame, in that order.) The fifth argument is a literal `0` at this call site; whether the other two call sites (`spec-player-runtime.md` §9.5 — the upkeep `0x00af5cb0` and `0x005457f0`) pass something else was not checked.

1. **Seat count** `0x00ab4af0(vehicle)`, forced to 1 if vehicle flag `+0x16c2` bit 0 is set.
2. **Four character-derived predicate flags**, each a single call on `character`: `0x0052a240` (FLAG_A), `0x0052a270` (FLAG_B), `0x009ba300` **and not** `0x00853b30(character, 0x21)` ("is-player") (FLAG_C — true only for a non-player that `0x009ba300` flags), and `0x00530340` (FLAG_D).
3. **Per-seat loop, `seat = 0..count-1`:**
   - the requested-seat argument gates which seats are even considered: a specific index restricts the loop to that one seat; `-1` ("any") additionally skips seats before `first_seat_for_passenger_search`;
   - when the vehicle's physics body is active (`+0xbd0` = 1) and the seat already has a non-null entry in a quick-lookup array at vehicle `+0x828` + `4·seat`, skip it (already claimed, cheap short-circuit before the heavier checks);
   - **per-seat eligibility against the `Seat_Info` record** (vehicle `+0xbf4` → descriptor `+0xa7c` → `+0x44` + `0x24·seat`, the same record `spec-player-runtime.md` §9.4.1 reads for `Enter_Animations`/`Exit_Animations`): its flag byte `+8` bits 2/3/4/5 must equal FLAG_A/FLAG_B/FLAG_D/FLAG_C respectively, and its byte `+0xa` bit 1 must equal the sixth argument (request `+0x23` bit 4). Mismatch on any → skip the seat.
   - seat flag `+8` bit 6 ("disabled") set → skip; seat field `+0x10` (door/entry-point index) == `-1` → skip (no entry point configured);
   - **seat 0 (driver) only:** if the vehicle is remote-controlled (`0x00a77a10`) and `0x00971cf0(character)` is non-null, skip — the character already has a remote-vehicle assignment elsewhere;
   - **when the eighth argument (request `+0x23` bit 0, "fast/sprint") is set:** build a **temporary slot** (constructor `0x00af2e40`) in state `0x13` ("fast"), surface = `1` (ground), side = `0x00b0c5e0(vehicle, character)`'s result, and ask the **element-selection rule** `0x00b0ad10(info_record, &temp_slot, &temp_request, &dummy_exit_seat, quiet=1)` — a seat with no `fast` `Element` for that side is skipped. **This is exactly the call site `spec-player-runtime.md` §9.5 already describes from the other direction** ("temporary phase-1, state-`0x13` slot (`fast`), `+0x1c` = 1 (ground), `+0x20` = `0x00b0c5e0`'s side, quiet = 1, exit-seat pointer null") — the two passes agree completely;
   - seat/occupant conflict resolution: an existing claim (`0x00ab5070`) or pending claim (`0x00ab5150`) that is not dead (`0x0096f4f0`) is checked via `0x00b0c390(vehicle, character, claimant, …)`, which can also skip the seat;
   - **seat flag `+8` bit `0x80`** (high bit) is a special category gated on the session/mode-kind getter (`0x00614d00`, the same one `0x009e1e60` calls) returning `6` *and* the vehicle body being active — otherwise skip;
   - if every check above passes, it calls **`0x00b0eac0(vehicle, seat, −1, 0, 0, &out1, &out2)`** and requires it to return `1`. On success the whole function returns **true** — but, consistent with §13.0 item 4, **nothing here writes the winning seat index anywhere the caller can read**: the per-seat loop variable is a callee-saved register, and no output parameter carries it out. `0x00b0eb90` answers "does a matching seat exist", not "which one".
4. If the loop exhausts without a match, returns **false**.

**`0x00b0eac0`'s own 3rd argument (`dir`) has at least two confirmed shapes beyond the `-1` this
section's own call site uses** (2026-10-07, from reading `0x00b0b9a0` in full): `dir = -1` accepts
only an `Interaction_Point_Set_Element` whose own `Direction` field is absent (`-1`,
"unconstrained/default"); `dir = 7` — one past the last real `Direction` enum literal
(`spec-tables-vehicle-world.md` §4.1 lists 0–6) — is a deliberate wildcard accepting *any* tagged
`Direction`, falling back to the `-1` element only if none exists. `spec-ai-runtime.md`'s own
`0x005457f0` (actions 37/38's seat chooser) calls `0x00b0eac0` with `dir = 7` from its own seat-ranking
loop, a materially different filter than this section's own `0x00b0fdf0`-rooted call site uses.

**Confirms scoping's three-caller list** (`spec-player-runtime.md` §9.5): this unit's own caller `0x00b0fdf0` (the eligibility pre-check above), the per-record upkeep `0x00af5cb0` (which does the *real*, persisted pick — `spec-player-runtime.md` §7.6, Player U6.0), and `0x005457f0` (**now read in
full** — the AI-side seat chooser for actions 37/38, `spec-ai-runtime.md` §11.3; it calls
`0x00b0eb90(vehicle, seat, -1, character, 0, 1, 0, 0)` once per candidate seat in its own scan,
confirming args 5–8 at this third call site: arg5 = literal `0` (matching `0x00b0fdf0`'s own call),
args 6–8 = literals `1, 0, 0`, none read from a request structure since this caller has none).
`0x005457f0` is the function that actually does the "which one" selection this section's own point 3
flagged as missing from `0x00b0eb90` itself: closest-to-self by squared distance among the seats
`0x00b0eb90` approves, with hardcoded priority for seat 0 then seat 1.

### 13.6 The obstruction gates: `0x00b0da10`, `0x00b0dc20`

Both `(character, vehicle)`, both wrapped in an SEH frame (`push -1; push <handler>; mov eax, fs:[0]; push eax` — the standard MSVC `__try` prologue), both gated on vehicle `+0xbd0` = 1 (physics body active) and both structured the same way: compute a position (character and/or vehicle `+0x40`/`+0x44`/`+0x48`), scan a short list (stride `0x50`, testing bit `0x10` of a byte at a fixed sub-offset — `+0x48`/`+0x60` depending on which list) for an entry that already satisfies some condition, and call a query helper with the computed position and a fixed small integer "kind"/id constant.

- **`0x00b0dc20(character, vehicle)`** — gated up front on `0x00853b30(character, 0x21)` ("is-player"); **an AI character always passes trivially** (the real check never runs). For a player, it computes positions from `character+0x40` and `vehicle+0x40`, scans the list via `vehicle+0x81c` (stride `0x50`, flag byte `+0x48`), and calls **`0x0075c880`** with the computed midpoint and the constant `0x6d`.
- **`0x00b0da10(character, vehicle)`** — no is-player gate; applies to any character, but the caller (`0x00b0fdf0`, §13.4 step 8) only invokes it when the request is **not** instant and `0x00971cf0(character)` is non-null. Computes positions from `character+0x40` and `vehicle+0x40`, scans a list through a seat/door sub-record (stride `0x50`, flag byte `+0x38`/`+0x48`), and calls **`0x0075d7e0`** twice (once per candidate position) with the constant `0x6d` and a second constant `0x00ac4c30`.

Both `0x0075d7e0` and `0x0075c880` were **not traced** — they read like a short-range geometry/physics query (HYPOTHESIS: "is there room at the door" or "is this entry point already physically occupied"), but their internals sit outside the vehicle-interaction band and are more likely physics-runtime territory. New OPEN item below. Either function returning its "found something" case clears `0x00b0fdf0`'s accumulated success byte, refusing entry without otherwise unwinding any state.

### 13.7 The hand-off: what reaches the submitter, and what doesn't

Putting §13.2–§13.6 together, by the time **`0x00af22a0`** (`spec-player-runtime.md` §7.4, already specified — cited, not repeated) runs its own steps:

- **Populated, from construction:** request `+0x00` (seat: `-1`, a forced-seat index, or `1` in the co-op-solo case), `+0x0c` (search mode: `1`, `0` forced/co-op, or `2` if the submitter's own step 6 applies), `+0x23` bit 0 (the sprint/fast flag from the triggering button press), and the generic `-1`/`0` resets of `spec-player-runtime.md` §7.2's table (including the newly observed `+0x08` = `2` baseline, §13.2).
- **Validated, but not written:** everything about *which* seat and *which* door/entry-point will actually be used. `0x00b105c0` and `0x00b0fdf0` (with `0x00b0eb90` inside it) only establish that **some** seat, consistent with the request, is reachable on **this** vehicle, under team/speed/obstruction/occupant constraints — they never store a seat index, a door, or an entry-point into the request or a slot.
- **Vehicle = 0, always**, for every player-triggered path traced in this unit (§13.2's "common tail"), even when `player_force_vehicle_seat` has pinned both a seat and a vehicle. The submitter's own call to `0x00b105c0` (§7.4 step 4) is what actually produces the non-null vehicle object the rest of the submitter's steps (5–11) operate on.
- The submitter then does what `spec-player-runtime.md` §7.4 already documents in full: refuses a `+0x1d7b` bit `0x40` vehicle without `+0x20` bit 2 (a *third* read of that same gate, after `0x00b0fdf0`'s two — three checks of the same bit across this call chain, not two as scoping implied); folds search mode 1 into 2 for remote-controlled vehicles and player-class characters; finds or claims a registry record; allocates a slot (no de-duplication); force-instants when the vehicle has no seat/entry table; copies the `0x11`-dword request into the slot; and calls set-enter with state 0. **The actual seat/door resolution happens on the record's next 250 ms upkeep pass**, `0x00af5cb0` (§13.0 item 4).
- **The AI side reuses this exact chain below the submitter, not above it.** `0x0051d5b0` and `0x00521200` both resolve a human's vehicle handle the same way `0x00b105c0` does (through `0x00458230` with type-tag `0x031d152c`) and both call `0x00b0fdf0` directly — confirming scoping §5a.4 point 3. They do not go through `0x009e5e80`/`0x009e1e60` (those are on-foot player-controls entry points only) or necessarily through `0x00b105c0` (an AI caller may already know its target vehicle). Where in the AI mode-38 `Veh enter` machinery (`spec-ai-runtime.md`'s table row, `0x00521320`/`0x00520cd0`/`0x005213d0`/…) these two functions are themselves called from was not traced — AI-side work, flagged as a new OPEN item rather than guessed at here.

### 13.8 What a port must reproduce

1. The single shared trigger button (`CBA_GAC_ACTION`) and the table-of-handlers shape at `0x0111f2d0` (bit-indexed by context word `+0x2084`), not a per-prompt test/handler pair.
2. `0x009e5e80`'s seated/not-seated split, including the debounce timer and AI-sub-state override on the exit side, and the sprint-vs-normal flag on the enter side.
3. `0x009e1e60`'s request defaults (including the `+0x08` = 2 baseline) and its co-op/solo and forced-seat special cases — and the fact that it **always** submits with vehicle = 0, relying on the submitter's own auto-pick.
4. `0x00b105c0`'s nearest-two-candidate scan with the sticky-override exception, and its one-retry-then-advance pattern against `0x00b0fdf0`.
5. `0x00b0fdf0`'s full gate order (§13.4), especially that it is a pure validator: a port's equivalent must not treat a true return as "seat chosen" — only as "this vehicle/request combination is submittable".
6. `0x00b0eb90`'s per-seat filter set and its tie-in to the element-selection rule for `fast` entries — reproducing this is what makes "can I jump in here" answers match the original without having to run the full async upkeep.
7. The two obstruction gates' asymmetry (player-only vs. universal-but-instant-exempt) even if a port stubs their actual geometry query.

### 13.9 Corrections and cross-spec flags raised by U6.2

- **`review/scoping-player.md` §5a.3:** `0x00b0eb90` is CONFIRMED as the seat-scoring/eligibility helper (not HYPOTHESIS); its signature and full per-seat filter set are §13.5. `0x00b0da10`/`0x00b0dc20` are a pair of obstruction/clearance gates (§13.6), not named by the sub-unit table.
- **`review/scoping-player.md` §4.2 OPEN item 9:** answered — `CBA_GAC_ACTION` (§13.1). The finer-grained question of what sets bit 15 of `+0x2084` is new and unresolved (below).
- **`review/scoping-player.md` §5a.4 point 3:** confirmed by direct read (§13.7's last bullet), not merely by caller-list inference.
- **`spec-player-runtime.md` §7.2 (Player U6.0, the request-field table):** two new fields found — request `+0x22` bit `0x80` (vehicle-eligibility gate, player-only, §13.4 step 1) and request `+0x30` read as "no existing claim id" by the seat-contest check (§13.4 step 7), alongside its already-documented "`-1` on reset" role. Request `+0x08`'s domain is wider than `{0, 1}`: `0x009e1e60` presets it to `2` before the submitter ever runs (§13.2). Request `+0x23` bit 0 is *written* at construction time (§13.2), not only consumed by the upkeep as §7.2 describes it — it is also read twice more inside `0x00b0fdf0`/`0x00b0eb90` (§13.4 step 7, §13.5 step 3).
- **`spec-lua-api-behaviour.md` §38.1 / `spec-player-runtime.md` §7.4:** the vehicle descriptor flag `+0x1d7b` bit `0x40` vs. request `+0x20` bit 2 is tested **three times** across one submission (twice inside `0x00b0fdf0`, once by the submitter itself per §7.4 step 5), not once.
- **Player-object fields:** `+0x1e30`/`+0x1e34` (a "sticky"/previously-tagged vehicle handle pair, read only by `0x00b105c0`, §13.3 step 2) is not in `spec-player-runtime.md` §1.5's (nor the original scoping note's §1.5) list of player-only fields. Raised for whoever next extends that list; not added there directly by this unit.

---

## 14. U6.5 — Enter animated states (`0x0a`–`0x13`)


**Scope.** Unit U6.5 of `review/scoping-player.md` §5a.3: the on-entry and per-frame handlers of every live state in the enter range `0x0a`–`0x13` — prepare (`0x0a`), traverse (`0x0b`), open (`0x0c`), extract/hijack (`0x0d`), the extract-fail pick (`0x0e`), enter (`0x0f`), close (`0x10`, dead), change seat (`0x11`), activate (`0x12`, dead) and fast (`0x13`) — reached through the set-enter table U6.0 already specifies in full (`spec-player-runtime.md` §7.5). The three shared helpers this range would otherwise need — `0x00afe0d0` (the animated-camera path-clear raycast), `0x00b01460` (the fast-entry starter) and `0x00b0c170` (the "is the held animation finished" test) — are **not re-derived here**: U6.7 already specified all three in full, including the cross-cutting finding that `0x00b01460` runs on the hijacker's own slot when called from exit state `0x1d`'s on-entry (`spec-player-runtime.md` §11.7, cited throughout below). The element-selection rule itself (the key builder `0x00b0a6f0`, the matcher `0x00b0a650`, the lookup `0x00b0ad10`) is U6.1's (`spec-player-runtime.md` §9) and is cited by call, not re-derived; §9.5 in particular already traced the *key-building* fragment of several of this unit's on-entry handlers (`0x00af9720`, `0x00afa0d0`, `0x00afa540`, `0x00afaf90`, `0x00afb220`) in full — this unit fills in the rest of those same functions' bodies (state store, phase-boundary writes, play/camera calls, transition logic) that U6.1 explicitly left to U6.2/U6.5/U6.7. State `0x0f` turned out to be **already fully specified** by U6.3 (`spec-player-runtime.md` §10.4.4), as a byproduct of U6.3 needing to refute the "instant route" hypothesis against it — its on-entry `0x00afd0a0` and per-frame `0x00b01080` are cited verbatim below, not re-read.

**Method.** Ghidra-free, as every other U6 sub-unit. A purpose-built linear Capstone lister over `tools/harnesses/exe_va.py` (`scratchpad/list_fn.py`: disassembles forward from a given VA, tracking every in-range forward `jcc`/`jmp` target so it does not stop at an early `ret` that is itself a mid-function branch target — validated against a function U6.7 had already published byte-for-byte, `0x00b057b0`, which reproduced exactly 114 instructions and the same control flow §11.6 describes). Every stack-relative operand feeding the shared resolver `0x00af4500` and the element lookup `0x00b0ad10` was resolved by hand-tracking the `esp` delta from each function's own entry through its `lea`/`push`/`call` sequence (the same discipline U6.8 §12's method section describes, applied manually rather than with a CFG walker, since every root here is small enough — 57 to ~700+ instructions — to track directly); the six-output-pointer shape of every `0x00af4500` call was independently re-derived at each call site from its own zero-initialisation writes, not assumed from the signature text in §6.1/§7.5. **All sixteen roots were read in full** except state `0x13`'s per-frame `0x00b01630`, whose dominant control flow (the timeout/knockdown/commit skeleton) was read and confirmed but whose "surface-kind" door-opening tail (past `0x00b01f0a`) was not traced to its end — flagged as this unit's one real gap, §U6.5.13 item 1. No Ghidra project was opened or copied. No `.czn_pc` data was touched.

### 14.0 Results up front (U6.5)

1. **The range holds eight live states, exactly as scoping named them**, each with both an on-entry and a per-frame handler — `0x0a` prepare, `0x0b` traverse, `0x0c` open, `0x0d` extract (hijack), `0x0e` the extract-fail pick, `0x0f` enter, `0x11` change seat, `0x13` fast. **`0x10` (close) and `0x12` (activate) have no handler of either kind and are never held** — already established from the setter side by U6.0 §7.5 and from the key-builder side by U6.1 §9.8; this unit confirms it a third way, by direct listing of the set-enter jump table's neighbours (§U6.5.8). That is sixteen roots, matching scoping §5a.3's own count exactly.
2. **Every on-entry handler follows the same skeleton**: resolve (`0x00af4500`), store the state at slot `+0x0c`, reset the per-state fields `+0x10`/`+0x14`/`+0x18` — **except four handlers that deliberately break this rule**, a new finding parallel to U6.6/U6.7's own exceptions (`spec-player-runtime.md` §11.0 item 6): `0x0a` leaves `+0x18` at `0` (not `−1`); `0x0c` leaves `+0x18` at `2`; `0x0d` leaves `+0x10` at `1` (the extract-kind default, `extract standard`); `0x13` leaves `+0x18` at `4`. Three of these (`0x0a`, `0x0c`, `0x13`) are **sub-timer seeds** — each state's own per-frame advances `+0x18` through further values before anything else reads it, the same pattern U6.7 §11.5.1 found for exit state `0x1f`'s `+0x18` (`6`→`7`). `0x0d`'s `+0x10 = 1` is not a timer; it is simply the key builder's default extract kind, pre-seeded so a hit-less hijack chooser pass still has something to key on (§U6.5.5).
3. **Three of the eight on-entry handlers call the element lookup `0x00b0ad10` and play the result unconditionally if found, with *no* miss fallback of their own** (`0x0a`, `0x0c`, `0x11`) — a silent miss simply leaves nothing playing, and that is not a bug: U6.7 §11.7.1's own "is the held animation finished" test (`0x00b0c170`) returns **true** at once when no animation is active, so the per-frame handler advances on the very next frame regardless. **One on-entry handler's miss is an explicit transition, not a silent fall-through**: `0x0b`'s miss goes straight to **set-enter 9 (the commit)** — the only state in this range whose on-entry can commit the seating directly without ever reaching a per-frame tick. `0x0d` and `0x0e`'s on-entry handlers call the lookup too, but **not on their own slot** — see item 5.
4. **State `0x0f` (enter) is already fully specified, by U6.3** (`spec-player-runtime.md` §10.4.4, written to refute the "instant route" hypothesis): on-entry `0x00afd0a0` (167 instructions) and per-frame `0x00b01080` (exits only). Cited verbatim below (§U6.5.7); nothing new was found by re-reading it, and it was not re-read.
5. **State `0x0d` (extract) never calls the element lookup or plays an animation on its own slot at all.** Its on-entry `0x00afae20` does one of two things instead: for a **tank** seat, it independently re-implements the hub's own instant-hijack logic (`spec-player-runtime.md` §10.4.2's H1 branch) — redirect a player-class occupant to a cancel (set-enter 8) or kill and, if locally owned, destroy a non-player one — using the *same* damage routine (`0x0096fa20`) and the *same* null-handle/−1.0 constants; for every other vehicle, it delegates to the **hijack chooser `0x00afa540`**, already specified in full by U6.1 §9.5. **The hijacker's own animation is driven entirely from the victim's side**: exit state `0x1d`'s on-entry (`spec-player-runtime.md` §11.4.1) is the one that calls `0x00b01460` *on the hijacker's slot* or runs the second-anim/camera-gate branch — confirmed here by the straightforward fact that `0x00afae20` contains no call to `0x00b0ad10` or `0x00af45a0` anywhere in its 129 instructions. `0x0d`'s own on-entry closes with an unresolved call (`0xaf1b80(human+8, human+0xc)`) whose boolean result is folded into **human `+0xec` bit `0x100000`**, a bit already known from three other writers (§U6.5.11).
6. **State `0x0e` (the extract-fail pick) matches U6.1 §9.5's existing citation of it almost exactly, and extends it with the resolver it uses.** On-entry `0x00afaf90` counts the candidates (`0x00b0ac60`), rolls a pick in range (`0x00dab660`), builds a **temporary phase-2/state-`0x1e` slot** with the hijacker's own `+0x1c`/`+0x20` copied in (exactly as §9.5 describes), and plays **two separate animations on two separate objects** — the hijacker's own (real slot) and the victim's (temporary slot) — both resolved through a previously-uncited helper, `0x00ab6360(a, b, c)`. Its per-frame never calls the chooser `0x00afb220` at all: once the chosen object fails to resolve, or its animation is no longer active, `0x0e`'s per-frame unconditionally cancels (inline set-enter 8) — and then **re-runs the hub's own vehicle-AI-reaction logic** (`spec-player-runtime.md` §10.3 step 16's kind-1/kind-2 split, confirmed here by the identical `0x00b464c0`/5000 ms-timer-arm pair) before returning. So a failed hijack attempt provokes the same vehicle-AI response an instant one does.
7. **State `0x13` (fast) is the richest handler in the range, and ties together almost every other unit's prior findings.** Its on-entry independently confirms three already-published facts byte-for-byte — the exit-request `+0x20 = 0x13` and `+0x30` bit 3 writes U6.6 §6.2 already attributes to this exact address, and the "carjacking report" pair (`0x009dd390`/`0x00b33dc0`) U6.3 §10.7 step 5 already found at state 6's arrival router, reused here unchanged — and calls both the hijack chooser (§U6.5.5/U6.1 §9.5) and the shared fast-entry starter `0x00b01460` (U6.7 §11.7.2) directly. Its per-frame **never calls the state-chooser `0x00afb220` and never calls set-enter at all**; instead, once the fast-entry animation (driven by `0x00b01460`) finishes, it checks whether the destination seat and its entry-point queue are now completely clear and, if so, **commits directly with an inline call to `0x00afbd90`** (state 9's on-entry) — exactly the "direct calls from states `0x07`/`0x13` per-frame" U6.0 §7.5 already lists as one of state 9's nine producers. If the seat or queue is *not* clear, it applies a knock-down/impulse pair (`0x009a48e0` then `0x007818f0`, the same mechanism `spec-player-runtime.md` §11.5.4/§12.5 already cite for the eject kind and the rider-fall detector) to whoever is blocking, and tries again the following frame.
8. **A correction to U6.0 §7.5 / U6.1 §9.1's citation of the chooser `0x00afb220`.** Every documented call site before this unit passed a literal `0` as the third argument. **State `0x0c`'s per-frame passes a computed, non-literal value instead** — the chooser's signature is `(record, i, flag)` with *flag* genuinely variable at at least one call site, not always `0`. What the flag selects was not resolved (§U6.5.13 item 2).
9. **A new, previously undocumented timer field.** Slot `+0x28` (enter side — distinct from the exit request's own `+0x28`, "start time", `spec-player-runtime.md` §6.2) is read through `0x00d9e1e0` ("expired") by both `0x0d`'s and `0x13`'s per-frame handlers, gating their despawn/timeout paths. §7.2's slot-layout table does not currently name this field; it is new (§U6.5.11).
10. **Stick-abort recurs inside the animated range, asymmetrically.** `spec-player-runtime.md` §10.0 item 8 counted the stick abort twice, both inside U6.3's own states (0 and 5). Within this unit's own range it recurs **twice more**, in `0x0a`'s per-frame (after the usual 800 ms grace, gated on the sub-timer reaching stage 1) and twice over in `0x0d`'s per-frame (after `+0x28` expires) — but **not** in `0x0b`, `0x0c`, `0x0e`, `0x11` or `0x13`'s per-frame handlers, which have no stick-abort path at all. The mechanism (local player only, any non-zero `CAA_WALK_FORWARD_BACKWARD`/`CAA_WALK_TURN_LEFT_RIGHT`, §1.9) is identical every time it appears.
11. **Size.** Six of the eight states' handlers were read to a complete, confirmed listing: `0x0a` (57 + 263 = 320), `0x0b` (164 + 184 = 348), `0x0c` (124 + 342 = 466), `0x0d` (129 + 515 = 644), `0x0e` (145 + 258 = 403), `0x11` (68 + 63 = 131) — **2,312 instructions**, all CONFIRMED by full listing. `0x0f` is U6.3's 167 (on-entry only; its per-frame was not sized by U6.3 either). `0x13`'s on-entry is a confirmed 184; its per-frame is at least ~733 instructions by direct count across the portions read (control flow fully traced; one tail not fully walked, §U6.5.13 item 1) — **so the unit's total own size is at least ~3,440, consistent with scoping's own "~3.6k own" estimate** without needing to correct it.

### 14.1 The eight states at a glance

| State | On-entry | Per-frame | Entered from | Leaves to |
|---|---|---|---|---|
| `0x0a` prepare | `0x00af9720` | `0x00affdb0` | the chooser `0x00afb220` (states 6, `0x0b`–`0x0d` per-frame) | chooser result via set-enter, or `0x00afe980` |
| `0x0b` traverse | `0x00af97e0` | `0x00b000f0` | chooser `0x00afb220` | miss → **9** (on-entry); else chooser result, or `0x00afe980` |
| `0x0c` open | `0x00afa0d0` | `0x00b00c60` | chooser `0x00afb220` | chooser result, or `0x00afe980` |
| `0x0d` extract | `0x00afae20` | `0x00b00310` | hub step 15 (request `+0x23` bit 0, §10.3), or the chooser | inline cancel (state 8), chooser result, or `0x00afe980` |
| `0x0e` extract-fail pick | `0x00afaf90` | `0x00b00950` | chooser `0x00afb220` (its own hand-built temporary-slot probe) | inline cancel (state 8) unconditionally, or `0x00afe980` |
| `0x0f` enter — **U6.3's, cited** | `0x00afd0a0` | `0x00b01080` | hub step 15 (forced/override) | **9** (destination = entry seat), `0x11` (destination free), **3** (otherwise), or `0x00afe980` |
| `0x10` close | **none** | **none** | never (dead, §U6.5.8) | — |
| `0x11` change seat | `0x00af99b0` | `0x00b013c0` | state 3's per-frame (inlined), `0x0f`'s per-frame | **9** (on finish), or `0x00afe980` |
| `0x12` activate | **none** | **none** | never (dead, §U6.5.8) | — |
| `0x13` fast | `0x00afd290` | `0x00b01630` | hub step 15 (request `+0x23` bit 0) | **9** (inline `0x00afbd90`, seat clear), or stay (knockdown retry), or `0x00afe980` |

### 14.2 State `0x0a`: prepare

**On-entry `0x00af9720`** (57 instructions, CONFIRMED by full listing). Pure-stack calling convention (both `record` and `i` arrive on the stack, unlike every per-frame handler in this unit, which take `record` in a register and `i` on the stack — the "mixed conventions" `spec-player-runtime.md` §7.6 already flags, confirmed state-by-state here).
1. Resolve. State `+0x0c` = `0x0a`. `+0x10 = +0x14 = −1`. **`+0x18 = 0`, not −1** — the exception (§U6.5.0 item 2).
2. `anim = 0x00b0ad10(vehicle info, slot, enter request, &exit seat, quiet = 0)` — the element lookup, U6.1 §9.1/§9.3's citation of this exact call confirmed by listing (vehicle info reached through `vehicle +0xbf4 → +0xa7c`, the same chain every lookup in this unit uses).
3. If found: `0x00af45a0(human, anim)` plays it. If not found: nothing — no fallback, no transition (§U6.5.0 item 3).

**Per-frame `0x00affdb0`** (263 instructions, CONFIRMED by full listing).
1. Resolve. If the vehicle does not resolve, or is not instantiated (object flag `+0x33` bit `0x08`): check ownership of the **human** (`0x008addb0(human, 0)`); if owned here, cancel-and-resubmit (`0x00afe980(record, i)`, U6.3 §10.4.3); otherwise return and wait.
2. Otherwise, dispatch on slot `+0x18` (the sub-timer the on-entry seeded at `0`):
   - **`+0x18 = 0`:** wait for animation `0x1d95` to either be inactive (`0x004b4ce0`) or, if active, to carry a cached tag (a hash at `0x153bd9c`, same lazy-cache idiom as U6.7 §11.7.2's "critical end" tag at `0x0153be2c`, name not recovered — §U6.5.13 item 3) via `0x004b29e0`. Once satisfied: a role-only vehicle call (`0x00ab48f0(vehicle, 1)`) runs, then **`+0x18 ← 1`**.
   - **`+0x18 = 1`:** a role-only vehicle-band call (`0x00ab98c0(vehicle)`, return discarded) runs, then wait for U6.7 §11.7.1's `0x00b0c170(human)` ("held animation finished"). Once finished: compute a vehicle tilt angle (`0x00da48a0` on `vehicle +0x4c`, doubled, compared to `0.5` — the same primitive U6.3 §10.5 step 3 uses for the tilt fix, applied here for a *different* purpose, §U6.5.13 item 4):
     - **small tilt:** clear enter-request `+0x21` bits 1 and 2 (the "entry-angle class" bits `spec-player-runtime.md` §9.3 cross-references for the lift/pull variant), call the **chooser `0x00afb220(record, i, 0)`** (U6.0 §7.5/U6.1 §9.5), and transition: chooser result `8` → `0x00afe980`; result `0x0f` → also clear slot `+0x24` first; otherwise → `0x00afe570(record, i, result, 1)` (set-enter).
     - **large tilt:** the stick-abort path — local player only (`0x0091faf0`), the 800 ms grace (`+0x84`) expired, not instant, any non-zero `CAA_WALK_FORWARD_BACKWARD`/`CAA_WALK_TURN_LEFT_RIGHT` — runs the shared inline-cancel idiom (resolve + validate the occupant, log `"Set Enter"` with both network ids through `0x00b0c280`, network-send `0x00afa400(record, i, 8)`, then `0x00afb930(record, i)`).
   - any other value of `+0x18`: return and wait.
   - any other value of the vehicle-instantiated check's slot (3, a different field not shown above) is unreachable; omitted for brevity.

### 14.3 State `0x0b`: traverse

**On-entry `0x00af97e0`** (164 instructions, CONFIRMED by full listing).
1. Resolve (pure-stack convention, as `0x0a`). State `+0x0c` = `0x0b`. `+0x10 = +0x14 = +0x18 = −1` (no exception here).
2. **`+0x24` (the walk-target `Interaction_Point_Type`, same field U6.3 §10.6 step 4 writes for state 5) is set to `6` or `7`**: if the entry seat (`request +0x10`) has an occupant (`0x00ab5070`), query whether its point set has a type-4 point for the current side (`0x00b0b9a0(vehicle, …, side = −1, type = 4, flag = 0)`, the same point-set query U6.3 §10.6 step 4 uses) and set `+0x24 = 6` or `7` accordingly; with no occupant, `+0x24 = 6` unconditionally. (6/7 are the `traverse standard`/`traverse no door` point-type codes, parallel to `+0x24`'s 2–5 range for states `0x0a` and 5.)
3. A door-state query/drive sequence (`0x00aa2370`/`0x00aa2080`/`0x00aa1ff0`, the same door-handling family U6.3 §10.3 step 17 and U6.7 §11.5.1 step 5 use) sets or clears enter-request `+0x21` bits 2 and 3 — the "traverse no door" classification bits U6.1 §9.3 already names — and a new human bit (`+0x21` bit 1, role not independently named, §U6.5.13 item 5) is set unconditionally at the start of this block. The exact stack-operand identities inside this block were not pinned to full CONFIRMED certainty (§U6.5.13 item 3); the bit-level outcome (request `+0x21` bits 2/3 set from a door check) is CONFIRMED by the surrounding reads/writes.
4. `anim = 0x00b0ad10(…)`, the element lookup. **Miss (`−1`) → `0x00afe570(record, i, 9, 1)` — set-enter 9, the commit, directly** — the one on-entry handler in this unit whose miss is an explicit transition rather than a silent no-op (§U6.5.0 item 3). **Hit:** play it (`0x00af45a0`), then the animated-camera gate `0x00b0ae40(human, anim)` (U6.1/U6.7's shared gate); on failure, the camera hand-off `0x00b0bc10(human, vehicle, 0, 0, snap = 1, 0)` — U6.4 §8.6's signature, with a **forced snap** (unlike `0x00b01460`'s own three-step cascade through `0x00afe0d0`, this call goes straight from the gate to the hand-off with no path-clear raycast in between).

**Per-frame `0x00b000f0`** (184 instructions, CONFIRMED by full listing). Register convention: `record` in `esi`, `i` on the stack.
1. Resolve. Vehicle not resolved/instantiated → ownership check on the human → `0x00afe980` or return (same idiom as `0x0a`'s per-frame).
2. If instantiated: a tag-hash check (`0x153bdec` — the **same** cached hash `0x0c` and `0x0e`'s per-frame both also read, §U6.5.13 item 6) via `0x004b29e0`; if it passes, drive the door open (`0x00aa2370`/`0x00aa2080`/`0x00aa1ff0` vs a float threshold/`0x00aa1eb0`, writing the door part into slot `+0x30` exactly as U6.3 §10.3 step 17 does) before continuing; if it fails, skip straight to step 3.
3. Wait for `0x00b0c170(human)`. Once finished:
   - hijack-bookkeeping, re-using the human `+0x16c0` bit `0x10` / `+0x16e8`/`+0x16ec` cache-compare pattern, calling `0x00b0d280(vehicle, human, 1)` on a match — the same bookkeeping U6.3 §10.5 step 4 names for state 4's per-frame.
   - set slot `+0x1c = 2` (the `vehicle` surface token, U6.1 §9.1 — makes sense while physically traversing the vehicle's exterior).
   - **evict the destination seat's occupant with an ordinary (non-instant) exit request** (`0x00af3cd0`, U6.6 §6.3) if it is not already exiting — the exact pattern U6.3 §10.5 names for state 3's and state 4's per-frame.
   - call the **chooser `0x00afb220(record, i, 0)`** and transition: `8` → `0x00afe980`; otherwise → set-enter(result, 1).

### 14.4 State `0x0c`: open

**On-entry `0x00afa0d0`** (124 instructions, CONFIRMED by full listing). Register convention (`esi` = record, `[ebp+8]`/`[ebp+0xc]` on the stack for `i`/outputs — a third distinct calling shape among this unit's on-entry handlers, alongside the pure-stack and the `ecx`-register ones).
1. Resolve. State `+0x0c` = `0x0c`. `+0x10 = −1`. **`+0x18 = 2`** (the exception — role not independently resolved, a candidate sub-timer seed by analogy with `0x0a`'s and `0x13`'s own `+0x18` exceptions, but no per-frame read of it was found at this value specifically — §U6.5.13 item 7). `+0x14`: **confirms U6.1 §9.3/§9.5's existing citation exactly** — `2` when enter-request `+0x21` bit 4 is set, `3` when bit 5 is set, else left at the default `2` (`open shady`/`open shopper`/`open standard`).
2. `anim = 0x00b0ad10(…)`. **Hit:** play it. **Miss: silently continue** (no transition; same reasoning as `0x0a`, §U6.5.0 item 3) — the branch structure here is unusual in that *both* the hit and the miss paths converge on the same next step rather than the miss skipping it, confirmed by listing.
3. The animated-camera gate `0x00b0ae40(human, anim)`; on failure, the forced-snap camera hand-off `0x00b0bc10(human, vehicle, 0, 0, 1, 0)` — identical shape to `0x0b`'s own camera step.
4. A per-seat scratch-table clear (`vehicle +0x1360 + 0x30·seat`, the same stride as the seat-claim table at `+0x1340`, written to `−1`) and the same hijack-bookkeeping compare-and-call-`0x00b0d280` pattern `0x0b`'s per-frame uses, here gated by one extra condition (`vehicle +0x20` bit `0x10` clear). Tail: `0x0055b1d0(vehicle, 500)` — role only, not independently named (§U6.5.13 item 8).

**Per-frame `0x00b00c60`** (342 instructions, CONFIRMED by full listing). Register convention: `record` in `ecx`→`edi`, `i` on the stack.
1. Resolve. If the human does not resolve, is not instantiated, or fails `0x00853b10` (the same validity predicate U6.6 §6.1 uses for the occupant guard — one more condition than `0x0a`/`0x0b`'s equivalent check): ownership-check-then-cancel-resubmit on the vehicle, same idiom as before.
2. Otherwise, dispatch on slot `+0x18` (seeded at `2` by the on-entry):
   - **`= 2`:** wait for either the tag-hash check (`0x153bdec`, via a *different* query function, `0x004b2910`, than the `0x004b29e0` every other state's tag check uses — role difference not resolved) or the animation's progress (`0x004b4600`) to exceed roughly `0.5` (a float constant at `0x1117b70`). Once satisfied: drive the door fully open (`0x00aa2080`/`0x00aa1eb0(vehicle, part, 1.0, 0, 1, 0.0, 0.0, 1)`, the exact call shape U6.3 §10.3 step 17 and U6.7 §11.5.1 step 5 both use), store the door part in slot `+0x30`, arm slot `+0x34` to 1000 ms (`0x00d9e140`), and **`+0x18 ← 3`**.
   - **`= 3`:** re-check the door and re-drive it if still blocked; then wait for **both** `0x00b0c170(human)` *and* (the door fully open, or the `+0x34` 1000 ms timer expired via `0x00d9e1e0`). Once both are satisfied, run the local-player stick-abort check (identical mechanism to `0x0a`'s), and:
     - **aborting:** the shared inline-cancel idiom (same as `0x0a`'s per-frame).
     - **not aborting:** the **chooser `0x00afb220(record, i, flag)`**, where *flag* is **not the literal `0`** every other call site in this unit (and every call site U6.0/U6.1 previously documented) uses — it is a value computed earlier in this same branch (§U6.5.0 item 8, §U6.5.13 item 2). Transition: `8` → `0x00afe980`; otherwise → set-enter(result, 1).
   - any other value: return and wait.

### 14.5 State `0x0d`: extract (hijack)

**On-entry `0x00afae20`** (129 instructions, CONFIRMED by full listing, re-verified instruction-by-instruction after an earlier draft of this subsection was found on review to have cross-contaminated several details from `0x13`'s on-entry — see the three corrections called out below). Mixed convention: `record` via `eax` register, `i` on the stack.

This function is short and has exactly two branches, both converging on one four-instruction tail. It contains **no exit-request construction, no `0x00af3cd0` call, no enter-request `+0x21`/`+0x23` bit write, no UI/stat/carjacking-report call, and no call to `0x00b01460`** — all of which *do* appear in `0x13`'s on-entry (§U6.5.10) and were mistakenly carried over into an earlier draft of this subsection before being caught and removed on review.

1. Resolve. State `+0x0c` = `0x0d`. **`+0x10 = 1`** (not −1 — the default extract kind, `extract standard`). `+0x14 = +0x18 = −1`. `+0x24 = 0`.
2. **Tank test** (`0x00ad3140(vehicle)`). **Not a tank:** fall straight to step 3 (the hijack-chooser call), unconditionally, with no occupant pre-check. **Is a tank:** resolve the occupant of the seat `request +0x18` names (`0x00ab5070`); then:
   - **occupant is player-class** (`0x00853b30(occupant, 0x21)`): `0x00afe570(record, i, 8, 1)` — **set-enter 8, and return immediately** (the function exits here — step 3 and the tail never run on this path) — matching `spec-player-runtime.md` §10.4.2 H1's "If v is player-class → set-enter 8 and return" exactly.
   - **occupant is not player-class** (including no occupant at all, resolved as handle 0): kill it (`0x0096fa20(occupant, human, −1.0, null-handle, 0, 0)`, the same arguments §10.4.2 H1 already documents), and, if locally owned (`0x008addb0`), destroy it (`0x00853ea0(occupant, 0, 0)`) — then fall through to the tail. **This branch is a direct, independent re-implementation of the hub's own instant-hijack logic (§10.4.2's H1), not an animated eviction** — it never touches the element lookup, an exit request, or `0x00af3cd0`.
3. **Non-tank path**: call the **hijack chooser `0x00afa540`** — U6.1 §9.5's full citation of this function applies unchanged, with no further bit-write around the call site (unlike `0x13`'s own on-entry, which sets enter-request `+0x23` bit 1 after its own call to the same chooser, §U6.5.10 step 3 — `0x0d` does not). Fall through to the tail.
4. **Tail** (reached from both the tank branch's kill/destroy path and the non-tank path): resolve the human again, call an unresolved 2-argument helper `0xaf1b80(human +8, human +0xc)`, and fold its boolean result (via a branchless XOR/AND idiom) into **human `+0xec` bit `0x100000`** — set when `0xaf1b80` returns false/0, cleared otherwise. This bit already has three other writers (state 0's on-entry, §10.2 step 9; the hub, §10.3 step 10; the cancel, §10.9 step 13); this is a fourth. Role of `0xaf1b80` not resolved (§U6.5.13 item 9).

**No call to `0x00b0ad10` or `0x00af45a0` anywhere in this function** — confirming by omission that the hijacker's own clip for an animated extract is driven from the *victim's* side, not this one (U6.7 §11.4.1's hijacker-resync scan and fast-entry branch).

**Per-frame `0x00b00310`** (515 instructions, CONFIRMED by full listing of the control-flow skeleton; several deep sub-calls left at role level, §U6.5.13 item 10). This is the largest and most complex handler in the unit.
1. Resolve. Vehicle null/not instantiated → ownership-check-then-cancel-resubmit, as elsewhere.
2. If instantiated: check **slot `+0x28`** (a new timer field, not in §7.2's table, read through `0x00d9e1e0` — §U6.5.0 item 9) for expiry.
   - **Not expired:** skip straight to step 3.
   - **Expired:** a class-bit `0x22` test (`0x00853b30`, the same "despawn-eligible" class U6.6 §6.7.2/§11.4.1 already name) on the extract target — if set, run the shared inline-cancel idiom immediately (resolve+validate+log+network-send(8)+`0x00afb930`), bypassing everything below. If clear, fall into the **local-player stick-abort** check (the same mechanism as elsewhere in this unit) gated on the same `+0x28` expiry, which on a non-zero walk axis sets a flag feeding the *same* cancel decision at the very end of the function (step 4).
3. **Wait for the entry seat to actually empty** (`0x00ab5070`; if still occupied and a request flag `+0x21` bit `0x80` is clear, return and wait). Once clear (or the bit forces proceeding):
   - a tag-gated (`0x153be28`) **held-weapon drop sequence**: `0x00941f40` (the held-weapon query U6.6/U6.7's own extract-kill path already cites, §11.4.2), its sub-object at `+0x19c`, through `0x00b81440` (the same predicate U6.3 §10.2 step 4 names for a "held weapon" test) — on success, an unresolved drop/holster call `0x009d14f0`, followed by seat-claim/pending-handle bookkeeping (`0x00ab51a0`) and a short chain of unresolved notification calls (`0x0070bcb0`/`0x004ddc20`/`0x0070ced0`/`0x0070dcf0`) — role only throughout (§U6.5.13 item 10).
   - two more tag-gated checks (`0x153be24`, `0x153bdec`) selecting between an AI driving-request call (`0x009c7cc0`, the same helper U6.3 §10.2 step 4 names) and a door-open sequence (`0x00aa2080`/`0x00aa1eb0`/`0x00aa1f20` — a third door-drive variant not seen elsewhere in this unit, role only).
   - two further tag-gated checks (`0x153be20`, `0x153be1c`) selecting between two unresolved, parallel calls (`0x0055a0a0`/`0x0055a190`) — role only.
   - a wait for the animation to be active (gated on a human bit, `+0x21` bit `0x80`) and then for `0x00b0c170(human)` or a "critical" tag-progress check (`0x004b4320`, hash `0x153bd98`, target `−1` — the same family of calls U6.3 §10.6 step 5(b) uses for a different tag) to be satisfied.
4. Once everything above is satisfied: a final local-player stick-abort-style check (same `+0x28`-gated mechanism) sets a flag; that flag **or** a class-bit `0x22`/`+0x22` combination both lead to the **same shared inline-cancel idiom**. Failing both, call the **chooser `0x00afb220(record, i, 0)`** and transition normally (`8` → `0x00afe980`; otherwise → set-enter(result, 1)).

### 14.6 State `0x0e`: the extract-fail pick

**On-entry `0x00afaf90`** (145 instructions, CONFIRMED by full listing; the lookup portion of this function was already specified by U6.1 §9.5 and matches exactly). Mixed convention: `[ebp+8]`/`[ebp+0xc]` on the stack.
1. Resolve. State `+0x0c` = `0x0e`. `+0x10 = +0x14 = +0x18 = −1`.
2. **Count** the candidate `extract fail` elements: `C = 0x00b0ac60(vehicle info, slot, request, &exit seat)` (U6.1 §9.1's counting sibling of the lookup). The raw count is then incremented in place (`eax += 4`) *before* the roll: `+0x14 ← 0x00dab660(5, C + 4)` — reproducing U6.1 §9.5's documented `5…N+4` exactly, with U6.1's `N` equal to this raw count `C` (an earlier draft of this bullet mis-stated the arithmetic as `0x00dab660(5, N + 3)` after renaming `C + 4` to its own `N`; fixed on review — the roll's second argument is simply the already-incremented value, one addition, not two).
3. Resolve the hijacker (`0x00ab6360`, a previously-uncited 3-argument resolver — role only, used identically by this unit's `0x0e` per-frame too) and play the lookup's result on it (`0x00b0ad10` on the **real slot**, then `0x00af45a0`).
4. Build a **temporary phase-2, state-`0x1e` slot** on the stack (constructed with `0x00af2e40`/`0x004dccd0`, matching U6.1 §9.5's description field-for-field): `+0x1c = 1` (ground), `+0x14` = the same roll from step 2, `+0x20` = the hijacker's own `+0x20` (side). Resolve the victim (`0x00ab6360` again) and play *its* lookup result (`0x00b0ad10` on the **temporary slot**, then `0x00af45a0`) — this is U6.1 §9.5's "looks up the victim's extract fail animation in the exit set," confirmed exactly.
5. Tail: the same per-seat scratch-table clear (`vehicle +0x1360 + 0x30·seat ← −1`) `0x0c`'s on-entry also performs.

**No call to the chooser, no set-enter, in either animation-play branch** — this on-entry handler's entire job is choosing and playing the pair of animations; the transition away from `0x0e` is entirely the per-frame's.

**Per-frame `0x00b00950`** (258 instructions, CONFIRMED by full listing).
1. A tag-gated (`0x153bdec` — the same hash `0x0b`/`0x0c`'s per-frame also read) door-open sequence, identical in shape to the ones already described, run unconditionally first.
2. Resolve the hijacker again (`0x00ab6360`). **If it fails to resolve, or its animation `0x1d95` is no longer active** (`0x004b4ce0`), run the **shared inline-cancel idiom** (resolve+validate+log+network-send(8)+`0x00afb930`) **unconditionally — there is no chooser call anywhere in this function.** A failed hijack attempt always ends in a cancel, never a different animated state.
3. **After** the cancel: re-run the **hub's own vehicle-AI-reaction logic**, confirmed as the same mechanism `spec-player-runtime.md` §10.3 step 16 already documents (kind 1 vs kind 2, the driver-class/team checks, `0x00b464c0`/`0x00b50180` and the vehicle `+0x1ac4` 5000 ms timer arm) — an ownership-gated role-only block (`0x004de260`, `0x004fa060`/`0x00b61cc0`, `0x00b46ca0` fill in the branches not already named by §10.3). So the vehicle's AI reacts identically whether a hijack attempt was stopped instantly (the hub) or animated-and-failed (here).

### 14.7 State `0x0f`: enter — already specified by U6.3, cited not re-derived

On-entry `0x00afd0a0` (167 instructions) and per-frame `0x00b01080` are **U6.3's** (`spec-player-runtime.md` §10.4.4), read there in full to refute the scoping-level "instant route" hypothesis. In brief, for citation:
- On-entry: state `+0x0c = 0x0f`; `+0x14 = −1`, `+0x18 = 4`; `+0x10 = 0` for `enter guardian angel` when forced; the entry door is driven open and the seat's own `+0x20` timer disarmed; `e = request +0x30`, or the element lookup if `−1`; with `e` and a controller, play it (`0x00af45a0`) and, unless `0x00b0ae40`, the camera hand-off `0x00b0bc10`; **without `e`, set-enter 9 directly** (the only fallback of this kind in the whole `0x0a`–`0x13` range besides `0x0b`'s miss).
- Per-frame: on animation end, → **9** when the entry seat is the destination; → **`0x11`** when the destination is free; → **3** otherwise; `0x00afe980` when the vehicle is gone.

Nothing in this unit's own reading of its sixteen roots touched `0x00afd0a0`/`0x00b01080` again; no correction is raised against U6.3's account.

### 14.8 States `0x10`, `0x12`: dead

Confirmed a third way, independently of U6.0 §7.5 (the setter-side survey) and U6.1 §9.8 (the key-builder side): the set-enter jump table `0x00afe924`'s entries for `0x10` and `0x12` were read directly and both point at the table's own shared "nothing to do, return" target, exactly as U6.7 §11.2 found for the parallel exit-side dead states `0x1a`/`0x1e`/`0x20`. No per-frame slot exists for either in the enter per-frame table `0x00b02100` either. These two states cannot be held in a session where every machine runs this executable.

### 14.9 State `0x11`: change seat

**On-entry `0x00af99b0`** (68 instructions, CONFIRMED by full listing) — the shortest handler in the unit.
1. Resolve. State `+0x0c` = `0x11`. `+0x10 = +0x14 = +0x18 = −1`.
2. **Leave the old entry-point queue without releasing the seat claim**: `0x00b0b270(vehicle, human, flag = 0)` (U6.0 §7.3's queue-leave function, called here with *flag* clear — distinct from the cancel's own call to the same function with *flag* = 1, §10.9 step 4).
3. `anim = 0x00b0ad10(…)` (the `any`-surface key U6.1 §9.3 documents for this state). Hit → play it (`0x00af45a0`). Miss → nothing (same silent-miss convention as `0x0a`/`0x0c`).

**Per-frame `0x00b013c0`** (63 instructions, CONFIRMED by full listing) — the simplest transition logic in the unit.
1. Resolve. Vehicle null/not instantiated → ownership check on the human → `0x00afe980` or return.
2. Otherwise: wait for `0x00b0c170(human)`. **Once finished: `0x00afe570(record, i, 9, 1)` — set-enter 9, unconditionally.** No chooser, no camera step, no fallback branch of any kind.

### 14.10 State `0x13`: fast

**On-entry `0x00afd290`** (184 instructions, CONFIRMED by full listing). Mixed convention: `record` via `ecx` register, `i` on the stack.
1. Resolve. State `+0x0c` = `0x13`. `+0x10 = +0x14 = −1`. **`+0x18 = 4`** (the third `+0x18` exception in this unit; role/consumer not found — §U6.5.13 item 11).
2. **Evict a blocking occupant, if any and if eligible.** Read a seat-table field (`vehicle info +0x58 + 0x24·seat` — the same `Seat_Info` array U6.1 §9.4.1 indexes for `Enter_Animations`/`Exit_Animations`, here at a different sub-offset, candidate `Primary_Access_Seat`/`Secondary_Access_Seat` by analogy with U6.3 §10.9 step 7's naming, not independently confirmed — §U6.5.13 item 12) unless a skip condition holds (a request scratch field equals `−1`, or equals `2`). If a seat is found and occupied (`0x00ab5070`) and a role-only predicate `0x00950550(occupant, human)` is **false**: build an exit request (`0x004dccd0`), **write `+0x20 = 0x13` and set `+0x30` bit 3** — the exact pair `spec-player-runtime.md` §6.2 already attributes to this address, independently confirmed by this unit's own listing of `0x00afd290` — and evict with `0x00af3cd0`. **`0x0d`'s on-entry does *not* write these fields** — its own tank branch handles a blocking occupant by killing/destroying it directly (§U6.5.5), never by constructing an exit request; an earlier draft of this section wrongly claimed otherwise and was corrected on review (§U6.5.5 step 2's note).
3. **Resolve the entry seat's own occupant** (a different seat than step 2's). If present: call the hijack chooser `0x00afa540` (U6.1 §9.5), setting enter-request `+0x23` bit 1 (a new bit, §U6.5.11 — `0x0d`'s on-entry calls the same chooser but does **not** set this bit, §U6.5.5). If absent and step 2's eviction did **not** already fire: call the shared helper **`0x00b01460(record, i)`** directly.
4. A player-class check (`0x00853b30(…, 0x21)`) gates a role-only UI call (`0x0051b5b0`); an unresolved 3-arg call `0x006bd720`; a local-player check (`0x009da4e0`) gating a role-only achievement/UI call (`0x00710e40(0x41)`) — none of these four appear in `0x0d`'s on-entry (§U6.5.5), despite an earlier draft of this section claiming otherwise; they are specific to `0x13`. **When step 2's eviction fired and the occupant is the local player**, the **carjacking-report pair** `0x009dd390`/`0x00b33dc0` fires — the exact same two-call sequence U6.3 §10.7 step 5 already documents for state 6's arrival router, confirmed here as a second independent user of the same mechanism (not a third — `0x0d`'s on-entry does not call it).

**Per-frame `0x00b01630`** (confirmed control-flow skeleton over ≥ 733 instructions directly read; one tail not fully walked, §U6.5.13 item 1). Register convention: `record` in `esi`/`ecx`, `i` via `[ebp+8]`.
1. **A timeout-based hard cancel, checked first.** If the vehicle is locally owned (`0x008addb0`) *and* **slot `+0x28`** (the same new timer field `0x0d`'s per-frame reads, §U6.5.0 item 9) has expired (`0x00d9e1e0`): resolve the entry seat's occupant; **no occupant** → `0x00afe980` (cancel-and-resubmit, try again as instant); **occupant still present** → the shared inline-cancel idiom (full cancel, no resubmit). Otherwise (not owned, or not yet expired), fall through to the main body.
2. **Main body.** Resolve the destination seat's occupant (`0x00ab5070`). If **none**, and enter-request `+0x21` bit `0x80` is clear: call **`0x00b01460(record, i)`** (U6.7 §11.7.2 — the same call `0x13`'s own on-entry can also make, confirming §11.7.2's "called on three contexts," now with `0x13`'s *own per-frame* independently confirmed as one of them, matching that section's citation exactly).
3. If enter-request `+0x21` bit `0x80` is clear (meaning `0x00b01460` never successfully latched — e.g. it hit a lookup miss and cancel-resubmitted instead): return, nothing more to do this frame.
4. If the bit is set: wait for `0x00b0c170(human)`. Once finished:
   - **if the destination seat's occupant (from step 2) still exists:** apply a **knock-down-and-launch pair** — `0x009a48e0(occupant, 0, 0, null-handle, 0, 0, 0, 0, 0)` (the same knock-down predicate `spec-player-runtime.md` §5.4.2/§12.5 already cite for the rider fall/eject detector), then a vector built through an unresolved helper `0x00afa320` and applied as an impulse through `0x007818f0` (the **same** impulse-apply function U6.7 §11.5.4 step 6 uses for the eject kind's no-parachute path), setting human `+0xdc` bit `0x80` (also the **same** bit U6.7 §11.5.4 step 6 sets on its parachute path) — then stop, no transition this frame.
   - **if the seat is empty, but its entry-point queue (`0x00b0b460`, U6.0 §7.3/U6.3 §10.1's queue-head query) has someone in it:** resolve and re-validate the queue head; if it resolves, apply the **same** knock-down/impulse pair to it instead, then stop.
   - **if both the seat and its queue are completely clear:** **commit directly** — resolve the slot again, log `"Set Enter"` with both network ids (`0x00b0c280`), and **call `0x00afbd90(record, i)` inline** (state 9's own on-entry) — exactly one of the nine producers of state 9 U6.0 §7.5 already lists (`"direct calls from states 0x07/0x13 per-frame"`). If the resulting slot phase is now 3 (ride), the full `0x1a4`-dword record snapshot is copied and sent (`0x00af2b80`/`0x00af2eb0`) — the **same unconditional-send idiom** U6.3 §10.8 already documents for state 7's per-frame.

   A fourth branch past this (reached when the destination's "surface kind" scratch field, read from `request +0x20`, is `0`, `1` or `2` — a door-opening variant selection, same family as the door-handling seen throughout this unit) was not traced to completion; see §U6.5.13 item 1.

### 14.11 New and revised slot/request/human fields

| Field | Role found here | Status |
|---|---|---|
| enter-slot `+0x18 = 0` on `0x0a`'s entry (not −1) | a 2-stage sub-timer (`0`→`1`), advanced by `0x0a`'s own per-frame | new; corrects the blanket "every on-entry sets `+0x10`/`+0x14`/`+0x18` to −1" rule to this unit's range too (U6.6 §6.1 already carved out `0x1f`'s exception on the exit side) |
| enter-slot `+0x18 = 2` on `0x0c`'s entry (not −1) | a 2-stage sub-timer (`2`→`3`), advanced by `0x0c`'s own per-frame once a door/animation condition is met | new |
| enter-slot `+0x18 = 4` on `0x13`'s entry (not −1) | role/consumer not found — no per-frame read of this exact value was located | new; OPEN (§U6.5.13 item 11) |
| enter-slot `+0x10 = 1` on `0x0d`'s entry (not −1) | the default extract kind (`extract standard`), pre-seeded ahead of the hijack chooser | new |
| enter-slot `+0x24 = 6`/`7` on `0x0b`'s entry | the `traverse standard`/`traverse no door` `Interaction_Point_Type` codes, same field U6.3 §10.6 step 4 uses for states `0x0a`/5 | new (extends the `+0x24` field's documented value range) |
| enter-slot `+0x28` | a timer (tested via `0x00d9e1e0`), read by `0x0d`'s and `0x13`'s per-frame; distinct from the exit request's own `+0x28` (U6.6 §6.2) | new; exact role/writer not found (§U6.5.13 item 9/10) |
| enter-request `+0x23` bit 1 (slot-relative `+0x7f` bit 1) | "an occupant is being hijacked via the chooser," set by `0x13`'s on-entry after its own successful `0x00afa540` call — **not** by `0x0d`'s, which calls the same chooser but writes no bit around it (§U6.5.5) | new |
| human `+0x21` bit 1 | set unconditionally by `0x0b`'s on-entry during its door-handling block; role not independently named | new; HYPOTHESIS |
| human `+0xec` bit `0x100000` | a fourth writer found: `0x0d`'s on-entry, via an unresolved helper `0xaf1b80` | extends the existing three-writer list (§10.2 step 9, §10.3 step 10, §10.9 step 13) |
| vehicle `+0x1360 + 0x30·seat` | cleared to −1 by both `0x0c`'s and `0x0e`'s on-entry; same stride as the `+0x1340` seat-claim table | new; role not independently named |

### 14.12 What a port must reproduce

1. **The silent-miss convention for `0x0a`, `0x0c` and `0x11`'s on-entry handlers**, and `0x0b`'s miss-to-9 exception. A port that "fails gracefully" by transitioning away on every lookup miss would diverge from `0x0a`/`0x0c`/`0x11`, which rely on `0x00b0c170` to treat "nothing playing" as "finished at once."
2. **The three `+0x18` sub-timer seeds** (`0x0a`: `0`→`1`; `0x0c`: `2`→`3`; `0x13`: seeded `4`, consumer not found) and their exact gating conditions (§U6.5.2, §U6.5.4, §U6.5.10).
3. **`0x0d`'s on-entry plays no animation of its own** — a port must drive the hijacker's clip from the victim's `0x1d` handler (U6.7 §11.4.1), not from `0x0d` itself, or a hijack will play no animation at all.
4. **`0x13`'s per-frame never transitions through set-enter.** A port's state machine must allow an inline, out-of-band call into state 9's on-entry (`0x00afbd90`) as a first-class transition path, alongside the ordinary set-enter dispatch — the same shape U6.0 §7.5 already requires for state 7.
5. **The knock-down/impulse pair** (`0x009a48e0` → `0x00afa320` → `0x007818f0`, setting human `+0xdc` bit `0x80`) that `0x13`'s per-frame applies to anyone still blocking the destination seat or its queue, repeated every frame until the obstruction clears.
6. **The chooser's third argument is not always literal `0`** (§U6.5.0 item 8); a port must treat it as a real parameter, not special-case it to a constant.
7. **The second independently-confirmed `0x009dd390`/`0x00b33dc0` carjacking-report call site** (`0x13`'s on-entry, alongside U6.3 §10.7 step 5's original — `0x0d`'s on-entry does *not* call it, §U6.5.5) and the hub's vehicle-AI-reaction logic reused verbatim by `0x0e`'s per-frame after a failed hijack.

### 14.13 Corrections and cross-spec flags raised by U6.5

1. **Not fully resolved in this pass:** `0x13`'s per-frame `0x00b01630` is confirmed for its dominant control flow (the timeout/knockdown/direct-commit skeleton, §U6.5.10), but a fourth branch — a "surface kind" (`0`/`1`/`2`) dispatch over door-opening variants, reached when the destination seat and queue are both clear but the slot itself still needs a specific door treatment before `0x00afbd90` is reached — was read partway (through roughly `0x00b01c1f`–`0x00b01f22`) and not walked to its end. This is the single largest open item this unit is leaving; a future pass should finish it before anyone relies on `0x13`'s exact door-handling order. New OPEN item.
2. **Correction to `review/scoping-player.md` §5a.3 / `spec-player-runtime.md` §7.5 and §9.1's citations of the chooser `0x00afb220`.** Both describe its signature uniformly as `(record, i, 0)`. **State `0x0c`'s per-frame passes a non-literal third argument** at its one call site (§U6.5.4) — the signature should read `(record, i, flag)`, with *flag* generally `0` but not always. What the flag selects was not resolved. New OPEN item.
3. **A family of per-state cached animation-tag hashes was found but not named**: `0x153bd9c` (`0x0a`'s per-frame), `0x153bdec` (`0x0b`/`0x0c`/`0x0e`'s per-frame, shared), `0x153be28`/`0x153be24`/`0x153be20`/`0x153be1c`/`0x153bdf0` (`0x0d`'s per-frame, five distinct ones), `0x153bd98` (`0x0d`'s per-frame, via a different query function `0x004b4320`). All use the same lazy-cache-by-name idiom U6.7 §11.7.2 already names for its own "critical end" tag (`0x0153be2c`) and U6.8 §12.4 names for its eleven parameter names — but the string literals behind each of these eight were not traced. New OPEN item; a future pass with budget for a targeted string-xref scan could likely resolve all eight in one session.
4. **`spec-player-runtime.md` §10.5 step 3** (U6.3): the tilt-angle primitive `0x00da48a0` on `vehicle +0x4c`, doubled, compared against `0.5`, is not unique to state 4's "tilt fix." `0x0a`'s per-frame (§U6.5.2) uses the **identical** computation for an unrelated purpose (gating the stick-abort vs. the ordinary chooser-transition path, not righting the vehicle). No correction to §10.5 is needed — it correctly describes state 4's own use — but a future reader should not assume the primitive implies "tilt fix" everywhere it appears.
5. Several stack-operand identities inside `0x0b`'s on-entry door-handling block (§U6.5.3 step 3) and a handful of role-only helper calls across `0x0c`'s on-entry (`0x0055b1d0`), `0x0d`'s on-entry (`0xaf1b80`) and per-frame (`0x009d14f0`, `0x0070bcb0`/`0x004ddc20`/`0x0070ced0`/`0x0070dcf0`, `0x0055a0a0`/`0x0055a190`, `0x00aa1f20`), and `0x13`'s on-entry (`0x00950550`, `0x0051b5b0`, `0x006bd720`, `0x00710e40`) and per-frame (`0x00afa320`) were left at role-only/HIGH confidence rather than independently named, consistent with how `spec-player-runtime.md` §8/§11/§12 treat comparably deep internals elsewhere in this spec. None of them was found to drive a state transition or write a field this unit's own scope depends on.
6. **No correction is raised against U6.0 §7.5, U6.1 §9.1–§9.8, U6.3 §10.4.4, or U6.7 §11.7** beyond the items above — every citation of this unit's sixteen roots in those four sections was independently reproduced by direct listing (the element-lookup call shape, the hijack chooser's call sites, state `0x0f`'s full behaviour, and all three shared helpers' call conventions).

---

---

## 15. Combat: aim-assist, melee, and death/respawn (2026-10-08)

Traces the PLAYER's own control/input path through combat. Generic NPC/AI combat and the generic
weapon-fire → hit-detection → damage-application pipeline are explicitly out of scope — owned by
`spec-physics-runtime.md` §8.8-§8.11's own chokepoint and by `spec-weapons-runtime.md`; where this
section's own tracing reaches that generic pipeline, it is cross-referenced, not re-derived.

### 15.1 Aim and target acquisition — two separate mechanisms

**Movement auto-steer** (already on file, §3.5.3): the target-point builder `0x005b31f0` may replace the
player's raw stick-direction with the normalised vector toward `+0xcb0` under five gate predicates — a
*movement* nudge, not a weapon reticle effect.

**Weapon aim-assist** (`aim_assist.xtbl`'s five profile blocks, `spec-tables-weapons-combat.md` §9,
previously data-only) now has a confirmed runtime consumer: **`0x009ec2f0`**, a per-frame profile
*selector* for the local player — not a continuously blended set of five parameters. Exactly one named
profile is active on any given frame, chosen by strict priority: **Tank Skydiving** when a resolved
aim/attack target (via the generic handle-by-property resolver, property id `0x031d152c`) passes a
class-descriptor check plus two further predicates; else **Zoomed** when scope view is active
(`+0x1d95` bit `0x10`, the same bit §3.7 names); else **Fine Aim** (`0x009da830`, the same predicate
§3.5.3 step 3 cites); else **Combat Ready** (`0x00943a20`, the same predicate §3.5.4 step 19 cites); else
**Normal**. The per-frame caller `0x009ec3c0` resolves the local player, calls the selector, and feeds
the chosen block's own `slowing→dist→near_dist` field (block `+0x70`) to `0x009eb700` (not opened — the
per-frame "slow the reticle near a locked target" application, HIGH CONFIDENCE by call shape). The
`steering` half of each profile (the actual reticle-pull toward a target) has no consumer found —
**OPEN**, along with `0x009eb700`'s own body and whether the movement auto-steer and this selector ever
interact.

### 15.2 Melee — trigger already specced; this closes the resolution half

The trigger (§3.6.6, `0x005abd70`) is unchanged — three on-foot context-bit branches each call
`0x00950c30(player)`. **`0x00950c30`, read in full (410 instructions), is the real starter/resolver.**
Gates, any failure refuses: not ragdolled; not incapacitated; **AI mode exactly `0x2b` (43, "Player,"
the exact row `spec-ai-runtime.md` §6 names)**; a specific sub-state (`0x8a`); three further
can-attack predicates culminating in `0x00985760` (the authoritative "can this attack actually start"
gate — everything past it only runs when true). On a real start: re-arms the idle timer via the same
"player gave input" helper (§3.4); runs the identical "interrupt an idle action" sequence movement input
uses (§3.5.2 step 8, same four calls, same literal `0.2`); leaves scope view if active (§3.6.5); arms a
melee cooldown timer at `player+0x13a8` for 1000ms via `0x00d9e140` — a second independent confirmed
consumer of the countdown-timer pair `spec-ai-runtime.md` §17 §4.4 documents for ambient population;
resolves a target handle pair at `player+0x1360`/`+0x1364` via the generic handle resolver `0x004d7f30`
(`spec-ai-runtime.md` §11.3 family C); and **fires the already-documented `on_attack_performed` hook
(slot 8, `spec-lua-api-behaviour.md` §3.12) with `(characterName, weaponName, "melee")`**, confirmed down
to the literal strings `"unarmed"` (no-weapon fallback, `0x011192E8`) and `"melee"` (`0x01116e0c`), then
aligns facing via the same animation-alignment primitives (`0x004b7850`/`0x004b7d50`) movement
turn-around already uses.

**`0x00950c30` never touches `weapon_melee_attacks.xtbl`'s or `melee.xtbl`'s own arrays, and never calls
the generic damage routine** — confirmed by reading the entire body. Move *selection* (which `MeleeMove`
row plays) and *damage application* happen later, almost certainly animation-event-driven — this
boundary is OPEN, with `0x00985760`'s own body as the most promising next step.

**Player melee and NPC melee are confirmed separate code paths**, not one shared mechanism: neither NPC
AI action 23's handler (`0x00551e20`) nor its tick (`0x00551cf0`) appears anywhere in `0x00950c30`'s own
body or calls it (full `E8 rel32` caller census) — they execute broadly similar logic independently,
unlike several other systems this project has found to share exactly one implementation.
`0x00950c30` has 3 callers outside the player-input band (one in the avatar-sword-sweep branch §3.6.1
step 10, two unidentified — a shared primitive any sufficiently "Player-mode" entity could reach, gated
to real effect only by its own internal AI-mode-43 check).

### 15.3 Taking damage — scope note and one player-specific finding

Deliberately NOT re-derived (the generic pipeline's own chokepoint belongs to `spec-weapons-runtime.md`).
What IS player-specific: the generic "a character just died" notifier (containing call site `0x009504bf`
→ `0x00702810`, §15.4) tests the dying object's own class-descriptor bit — **the same player-class bit
(`0x02cc9900` row `+0xa` bit `0x2`) this project has independently confirmed from four other code
paths** — and only then calls the player-specific death trigger. Health storage itself (`+0x1cac`/
`+0x1cb0`/`+0x1cb8`) is the generic, shared character layout, not player-exclusive. The one genuinely
player-specific damage-side storage is **`player+0x276C`**, a 6-float per-damage-type scale array
(bullet/explosion/vehicle/fire/falling/script, written by the `Damage_Resist` unlockable apply routine,
`spec-tables-progression.md` §4.4 type 9/§4.6) — its consumer inside the generic damage routine was not
chased, **OPEN**, explicitly handed to whoever owns that chokepoint.

### 15.3a Generic damage application, death, and ragdoll hand-off (2026-10-09)

Closes `purrsian-04`'s wave 6 item 3: the generic (not player-exclusive) damage/death/ragdoll chain
§15.3 deliberately deferred. All addresses below are the shared human/character layout, consumed
identically by players and NPCs.

**Health storage, CONFIRMED.** `+0x1cac` (32-bit int) = max health; `+0x1cb0` = max-health bonus
(already named, `scratchpad/default_max_hp_1cac_2026-10-08.md`); `+0x1cb8` (float) = current health;
`+0x1584` = a "last damage magnitude" scratch field, `max(-delta, 0)` of the most recent HP-reducing
change. A character always spawns at full health: the base constructor zeroes these fields, then a
spawn-time vtable slot picks a `spawn_info_ranks.xtbl` row (story-progress-weighted random level draw
for ordinary characters), writes its `Hit_Points` to `+0x1cac`, and immediately mirrors it to `+0x1cb8`.

**The damage-apply sink, `0x0096fa20`, RE-CONFIRMED** (matches `spec-weapons-runtime.md` §4.1,
`spec-lua-api-behaviour.md` §3.15 exactly). Two of its own internal health-mutation calls are newly
pinned: **`0x0094cfc0(this, value)`** sets current health, clamped to `[0, +0x1cac]`, gated on local
network ownership (`0x008addb0`) — writes directly when locally owned, otherwise dispatches a
networked "apply this" replication request instead of touching the field; **`0x0094d520(this, delta)`**
is a thin "add signed delta" wrapper around it (`0x0096fa20` calls it with the damage amount
sign-flipped, i.e. ordinary damage really is `health -= damage` through this exact path). On a clearly
lethal hit, `0x0096fa20` first force-snaps health to exactly `0.0` via a direct `0x0094cfc0` call before
still running the ordinary subtract on top — a float-precision safety measure, not a second clamp.
**Correction to the prior "caller always computes the final amount" phrasing** (`spec-weapons-runtime.md`
§4.1): the sink itself also applies a resistance scale (an integer percentage read off `attacker+0x1cac`,
the same max-health field reused generically here) and a difficulty-based scalar when the instigator is
player-class — the caller computes the base amount, the sink still shapes it further.
**Vehicle-vs-character damage cross-check:** `spec-vehicle-damage-runtime.md` §3's own
`0x00a9f4f0` vehicle structure-HP accumulator never checks for HP reaching 0 and has none of this
sink's death-trigger logic — the two systems are genuinely separate, converging only when a destroyed
vehicle kills its occupant (`0x00a9d6c0`'s own call sites feed the literal `-1.0` lethal constant into
this exact same `0x0096fa20`, the identical path every other character-kill route uses).

**The death transition — two real functions.** On detecting a lethal hit (post-mutation re-query of
`0x0096f4f0`, `character_is_dead`, plus a kill-eligibility predicate `0x0096f990` already named in
`spec-weapons-runtime.md` §2.3 for the melee finisher), `0x0096fa20` calls **`0x00970e70`** — a dual-role
function. Reached with a null instigator, it recurses into a standalone "force-kill this object now"
entry (builds the same `-1.0`/flag-`0x2000` record `character_kill` itself builds, calls `0x0096fa20`
again). Reached normally (health already at zero), it does the real death side-effects: a virtual call
through the victim's own vtable `+0x70` (concrete target not resolved — no character-class RTTI/vtable
dump was available this pass), then probes for a clear nearby position via the already-documented Havok
on-demand raycast wrapper `0x0075d7e0` (`spec-physics-runtime.md` §8.8-§8.11) and dispatches a push-back
vector. `0x0096f4f0` (`character_is_dead`) re-confirms exactly as already documented
(null OR `+0xcc8==5` OR `+0xe3` bit `0x01`); two sibling predicates were found alongside it:
`0x0096f520` (exactly-dead only) and `0x0096f540` (the already-named "down or dead" query, now confirmed
to also encode `+0xcc8==6` as a distinct "downed" state). The project's existing finalizer
**`0x00952760`** (writes `+0xcc8=5` at `0x00952cd4`) is re-confirmed by fresh disassembly to also: drop
held/attached objects, clear an ownership flag, release a resolved controlling player, and fire a
notification event (id `0xb`) through the same generic dispatcher the damage sink's own alert-broadcast
uses. **One honest open seam:** `0x00970e70`'s vtable `+0x70` call and `0x00952760` were not proven
connected by a direct static call edge (different calling conventions/argument shapes) — what IS
confirmed is that `0x00952760` is called `(victim, 0)` from inside the ragdoll/death-stage tick below,
on the branch taken when ragdoll doesn't apply. HYPOTHESIS, not confirmed: the vtable dispatch arms a
generic "dying" state that the per-frame tick below then finalizes once ragdoll either runs its course
or never applies.

**The ragdoll/death-animation hand-off — the real decision point, now opened.** The already-named
`0x0098ce40` (5-case jump table on a per-character sub-stage byte, `+0xcce`, values 8-0xc) is the real
branch point. The ragdoll-activation case (reached when locally owned and already dead) gates on a
readiness check (`0x0098cc30`) and an eligibility flag (`0x009a2b00`); when eligible, it resolves an
attachment point (`+0xcc0`), issues a real ground-clearance raycast through `0x0075d7e0`, and calls the
real "activate ragdoll physics" primitive **`0x009a5520(victim, 1, 0)`** — distinct from the simpler
stop/start toggle `0x009a1e10` this file already names elsewhere. A separate, non-ragdoll fallback case
(when the readiness check fails, locally-owned player-class only) force-sets health to `0.0` and calls
the finalizer `0x00952760(victim, 0)` directly, with no ragdoll call at all — a genuine scripted/
animation death. A further already-ragdolled case shares its "animation progress" timing mechanism
(global `0x01117a4c`, table-hash constant `0x1d5f`) with the melee-finisher/takedown predicate
`0x0096f990` — ordinary ragdoll death and the scripted takedown sequence are not two independent timing
systems.

**The hitscan gap — still open, now more precisely bounded.** `spec-weapons-runtime.md` §2.2c named two
concrete next steps to try closing "hitscan fire does not call the damage sink": `0x008ccb90` and
`0x00920170`. Both were read in full this pass. `0x00920170` contains no call to the damage sink, the
kill trigger, `character_is_dead`, or either Havok query wrapper — directly confirming by disassembly
(not just role inference) that it really is the unrelated vehicle-seat "Hide Occupant" toggle already
named in this file. `0x008ccb90` (its first ~470 instructions, fully covered) likewise contains no such
call — it shares some generic record-building helper calls with `0x0096fa20`'s own early bookkeeping,
not a call relationship. **Neither named next step closes the gap.** The far side of the chain is now
fully mapped (this section, above) — a future pass has a precise, concrete target chain
(`0x0096fa20`→`0x0094d520`/`0x0094cfc0`→`0x00970e70`→`0x0098ce40`'s `+0xcce` state machine→`0x009a5520`
or `0x00952760`) to search for a caller of, rather than an open-ended question. The two live candidates
remain: a physical projectile/ray whose Havok collision-response callback (not reachable by static
call-graph tracing) is the real caller; or a live capture directly on `0x0096fa20`'s own callers during
one real fire sequence. Full detail, every instruction address: `scratchpad/character_damage_death_2026-10-09.md`.

### 15.4 Death and respawn — traced end to end, converges on checkpoint-restore

Player death reaches **`0x00702810(player)`** (full 81-instruction body), gated to the player class at
its sole caller (`0x009504bf`). It debounces via a per-player timer, reads **`respawn_delay_ms` live
from `0x014A1370`** (`gameplay_constants.xtbl`'s `Death_and_busted` section, `spec-tables-progression.md`
§10.2 — first confirmed runtime consumer of this address), sets the player's **"controls disabled" flag
`+0x1d99` bit `0x08`** (the same flag the control-state machine's per-frame movement gate checks, §2.10/
§3.5.1), pushes the already-documented death camera (mode 9, §4.1/§4.2), builds a mission-failure
descriptor tagged with the literal `MISSION_FAILURE_DIED_TEXT` (→ `"You died."`, both strings read
directly), and calls the project's already-documented mission-end routine `0x006da400`.

From there, this section cross-checks rather than re-derives the already-confirmed chain in
`review/exe-notes-2026-10-01/interp_fresh_start_checkpoint.md`: mission-end → the mission-failed handler
records the failing mission for retry → the next mission-begin sets the "is_restart" flag true → the
game-mode-state enter handler's mission setup conditionally calls **`0x006D3780`, the exact
checkpoint-restore routine `spec-save-format.md` §11.1/§11.2 already document** (same `0x1AA00`-byte
in-memory snapshot format, full flag = 0). **A player's death-respawn is confirmed to be the identical
in-memory checkpoint-restore mechanism as a manual "restart from checkpoint," not a separate
implementation** — the "checkpoint" restored to is whatever name the mission's own script last reported
through `mission_set_checkpoint`, or `"mission start"` if none was reported.

Separately, the character's own generic ragdoll/death-stage tick `0x0098ce40` (shared with NPCs, a
5-case jump table on a per-character sub-stage byte) *also* pushes camera mode 9 at its final stage, but
only when the dying object equals the local-player singleton — a second, structurally independent
confirmation of the same player-only camera behavior (harmless double-push; camera-mode push is
idempotent, §4.2).

**Honest gaps:** the exact `+0xcce` case-value mapping for `0x0098ce40`'s 5 stages; ~~a fresh independent
re-walk of `0x006DA400`-`0x006D9310` (this section's own claim rests on the pre-existing
`interp_fresh_start_checkpoint.md` note, not re-walked here)~~ **partly closed, 2026-10-08:
`0x006D9310` (the "game-mode-state enter handler") is now independently re-walked in full
(`spec-lua-api-behaviour.md` §21.27a/§21.27b) — on a restart (the exact condition this death/respawn
chain sets), it skips the mission-phase-2/marker-arm dance entirely and relaunches `script_mission_start`
directly via the same `0x00e0ca80` primitive the marker-trigger callback uses, consistent with (not
contradicting) this section's own existing account; `0x006DA400` itself was not independently re-opened
by that pass**; what decides a "retry this mission" prompt vs. another fail state (two special-cased
mission ids compared at `0x00702810`'s tail); whether free-roam (no active mission) death follows a
different respawn path.

**Cross-reference, 2026-10-08:** `spec-lua-api-behaviour.md` §21.27b documents the structural
counterpart to this death/FAILURE chain — the mission SUCCESS/completion trigger (`0x006d9f70`). The two
are asymmetric: this section traces a single, clean organic FAILURE trigger (player death); §21.27b
found no equally clean organic SUCCESS trigger exists, only fail-safes (timeouts, cancel-warps,
silent-class auto-finish) — the engine does not itself detect "objectives complete," only a mission's own
Lua script does.

**[CONFIRMED — full disassembly of `0x009ec2f0`/`0x009ec3c0`/`0x00950c30`/`0x00702810`/`0x0098ce40`,
the full `0x00950c30` caller census, and all 4 literal strings. HIGH CONFIDENCE — several black-box
predicate names, the melee-target handle fields, the generic mission-retry chain downstream of
`0x006da400`. OPEN — as listed per subsection above.]** Full detail, every address:
`scratchpad/player_combat_aim_melee_damage_respawn_2026-10-08.md`.

---

## ~~3.~~ ~~4.~~ ~~5.~~ ~~6.~~ ~~7.~~ ~~8.~~ ~~9.~~ ~~10.~~ ~~11.~~ ~~12.~~ ~~13.~~ ~~14.~~ 16. OPEN items

1. **Engine dead-zone, trigger-threshold and pad repeat-timing values** (`0x029f4440` + pad·`0x488`: stick `+0x248`/`+0x288`, button `+0x08`, `+0x0c`, `+0x10`). The latch and accessors are specified, but the code that writes these values (pad initialisation or options) was not located in the poll's neighbourhood. *Next step:* displacement scan of the engine library for stores at those offsets relative to a pad pointer; or read them from a running process.
2. **Option bits** `0x01411b00` bits 1–3 and bytes `0x01411b14`/`0x01411b15`: getters and setters specified (§1.12.2); their option names (look-invert per device family is the HYPOTHESIS) need the options-menu code (`0x007e0a33`, `0x007e128d`, `0x007e15b0`, `0x008411c5`…`0x008414f0`). **[Partly resolved by §3.4: the camera input setters `0x0056b100`/`0x0056b120` apply them as horizontal/vertical look inversion — behaviour CONFIRMED; the menu labels are still unread.]**
3. **`0x005bbff0`'s special case** (no pad, no key 1, key 2, no mouse 1, mouse 2 → false): behaviour CONFIRMED, purpose unknown.
4. **Readers of the bound-key table** (`+0x8a4`) and of the press timestamps (`+0x064`, `+0x0a4`/`+0x9a4` record `+4`): none found by absolute or in-band relative scan; a context-pointer reader outside the band is not excluded.
5. **`0x00846c60`** (called after every scheme apply) and **`0x007f0130`/`0x007f0140`** (row-2 exit and secondary): roles HYPOTHESIS only.
6. **Row 7 `0x005c2800`** read for structure only; whether it is a debug camera is still the scoping note's HYPOTHESIS. **[Advanced by §4.1: control state 7 is pushed only together with camera mode 3 by `0x00564cd0`, reached only from `0x0057b660` under the global `0x029cdb88` (HIGH: a debug free camera); the per-frame body is still unread.]**
7. **`0x005c2540`'s rotation rule** (§1.12.4): read but not exercised against inputs.
8. **Scoping OPEN item 2** — **closed** by §2.8 (HIGH CONFIDENCE), with residual 9.
9. **Game mode 7 without mode 5 beneath it.** §2.8's guarantee for the second dispatcher site (`0x00bdbc59`) assumes mode 7 is pushed above mode 5. *Next step:* push survey of the game-mode push routine for the value 7 and of what lies beneath it at each site.
10. **Game-mode slot naming** (`+0x08`, `+0x10`, `+0x18`, `+0x1c` of the 0x24-byte mode descriptor) is beyond this unit; only `+0x00` enter, `+0x04` exit, `+0x0c`/`+0x14` per-frame are used above (CONFIRMED by `0x00706be0`/`0x007068d0`; `review/scoping-player.md` §4.1 calls the table "11 entries", which is the descriptor array, distinct from the 15-deep mode stack).
11. **Animation ids used by U2** — controller states `0x1d4d`, `0x1d50`, `0x1d53`, `0x1d54`, `0x1d80`, `0x1d8d`, `0x1d8f`, `0x1d95`, `0x1da1`, `0x285`, `0x305`, `0x306`, `0x325`, `0x4b4`, `0x4b5` and the actions `0x2ff`…`0x304`, `0x306`, `0x31a`, `0x31b`, `0x325`, `0x326` (§3.5.4 steps 13 and 16): roles are CONFIRMED from their use (stop, turn-around, start-to-move, sprint start/stop), names unread; `0x004b41b0`'s "end state" role is HYPOTHESIS. *Next step:* resolve the ids against the animation state/action tables (`spec-anim-runtime.md`, `spec-tables-animation.md`).
12. **Timer `+0x206c` toggles every frame while s > 0.8** (§3.5.4 step 4), so the start-to-move test "elapsed < 1000 ms" is always true during that time and erratic afterwards. Behaviour CONFIRMED; whether it is a shipped defect or an intended hold-off is unknown. A port reproducing the original start animations should copy it literally.
13. ~~**The `0x009573d0` acceleration shaping** (§3.5.4 step 14) and the facing override `+0xa6c`/`+0xa7c`/`+0xa70`/`+0xa80` (step 19): structure CONFIRMED, the predicates' meaning (HYPOTHESIS: aiming/strafing) and the field names are open.~~ **PARTIALLY RESOLVED 2026-10-10.** `0x009573d0`'s predicate (full listing) is gated, in its fallback branch, by `0x009572f0`, which tests two pairs of authored animation tag names, resolved and cached by the project's standard lazy tag-name idiom (`0x0073d550`): `0x0153bd04`/`0x0153bd00` = tags "movement lock"/"movement unlock"; `0x0153bce0`/`0x0153bcdc` = tags "face camera start"/"face camera end" (the second pair gated additionally by animation-controller state `0x1d7e` being active for 0–4.75 seconds, `0x004b4ce0`/`0x004b82a0`). This REFUTES the "aiming/strafing" hypothesis: the facing override and its acceleration shaping are an authored-animation movement/facing lock, armed alongside a specific animation and released by `0x009a02a0` when that animation ends (already documented at §5.4.2), not an aim- or strafe-input state. The generic controller-flag test in the predicate's other branch (`+0x33` bit `0x8`) is a broad activity flag shared with unrelated per-frame processing, not aim-specific either. Residual, narrowed: the ARM-side setter of `+0xa6c`/`+0xa7c` (which code first writes a non −1 handle/mode) was not located; the most likely candidate is the animation-tag-trigger dispatch for "movement lock"/"face camera start" events, not confirmed by listing.
14. **Unattributed context bits:** `+0x2074` bit 2; `+0x2078` bits `0x2`, `0x8`, `0x8000`, `0x200000`, `0x400000`; and the meaning of `+0x2084` entries other than 15 (the 22-entry arrays at `0x0111f2d0`, `0x0111f3d8` and `0x0111f430`). *Next step:* resolve the whole-word readers listed in §3.2.3 and read the prompt arrays (U6/U8).
15. **Stance bits outside U2** (§3.7): `+0x1d94` bits `0x01`, `0x02`, `0x04`, `0x08`; `+0x1d95` bits `0x04`, `0x08`, `0x20`, `0x80`; `+0x1d97` bits `0x01`, `0x02`, `0x04`, `0x40`, `0x80`. Writers are listed; none was traced. **[PARTIALLY RESOLVED 2026-10-10.** `+0x1d94` bit `0x01` is HIGH CONFIDENCE a climb/vault/mantle grab-entry flag: both writers (`0x009e45e7`, `0x009e626f`) validate the target through the per-kind capability table `0x02cc9900`+0xa bit `0x01` and trigger animation-controller states `0x1d8d`/`0x305`. `+0x1d94` bit `0x02`'s two previously-cited addresses (`0x006da520`, `0x009e71f0`) are both CLEARS, not sets — `0x006da520` in a player-state snapshot/suspend routine, `0x009e71f0` in a respawn/un-ragdoll reset — the actual setter is still unlocated. `+0x1d94` bit `0x04` is set by a grab/attach interaction (`0x009e2919`) and cleared when a self-climbability test fails (`0x009e9351`, inside the same function that drives the sprint speed-cap ramp at OPEN 45) — HIGH CONFIDENCE behaviorally (grapple/wrestle-adjacent), label not firmly confirmed. `+0x1d94` bit `0x08`'s only writer (`0x009da1b2`) is squarely inside the human object's destructor (vtable store/teardown) and is uninformative about the bit's gameplay meaning — a genuine dead end. `+0x1d95` bit `0x04` (`0x00afba3f`) clears immediately before a vehicle-eject/exit call, gated on the same capability table's bit `0x02` — HYPOTHESIS: exit-in-progress, not firmly confirmed.]** **[FURTHER RESOLVED 2026-10-10.** `+0x1d95` bit `0x20` is RESOLVED/CONFIRMED: set by the fine-aim-entry routine `0x009dcdb0` when the equipped weapon's record has the scope-capable flag (`+0x14` bit `0x4000000`, the same bit already named for scope view), firing a sound/event cue on first entry; cleared there when the weapon lacks that flag, and separately cleared inside the aim/shoulder-camera/scope handler `0x005ad9a0` when a specific toggle input fires while not already in full scope view (which also triggers the scope-view-toggle call) — the bit tracks "fine-aiming with a scope-capable weapon, scope-view toggle armed." `+0x1d95` bit `0x08` is advanced to HIGH CONFIDENCE a position-snap/teleport-in-progress latch: its only writer snapshots the human's position, velocity and frame time into a parallel save block and touches the same camera-jump-notification global the per-frame camera update uses to suppress a spurious cut after a large instantaneous position change — set unconditionally at a branch inside the same large human-interaction dispatcher that supplies the already-resolved climb/grapple bits, not a standalone function; its containing function and clear site weren't located. `+0x1d97` bits `0x02`/`0x04`/`0x40`/`0x80` remain fully OPEN: the spec cites no candidate writer addresses for them at all, and locating fresh ones needs a register-relative displacement sweep of `.text` (the project's established Ghidra-free capstone-based linear-scan method) rather than Ghidra's address-indexed query modes — not attempted yet.]**
16. **The dormant pivot branch** `0x005ad510` (§3.5.6) — recorded at structure level; dormant because `0x0098d6d0` is unreferenced (raw scan). An indirect call through a computed pointer cannot be excluded by a reference scan.
17. **Drunk/weed effect** (`0x009758b0`, `0x00975ce0`, `0x009786c0`): which `drunk_levels.xtbl` field feeds which global (`0x02625bcc`, `0x02625bd0`, `0x02625c04`, `0x02625c3c`, the delay line `0x02625c68`) was not traced, and the effect-state machine `0x02626520` (state 3 freezes movement) was not named.
18. **Second writer of `+0x2048`**, `0x008a3ca0` (§3.5.7): HYPOTHESIS that it is the network-driven remote-player update; its flag argument to `0x005b31f0` and its caller `0x008a5061` were not traced. **[Advanced by §5.5: the routine also calls the move-target setter `0x00987360` (`0x008a4075`) and the locomotion-state update `0x00985df0` (`0x008a408a`), as the player's move slot does.]**
19. **`0x00985df0`** (the locomotion-state update called after the writer, maintaining `+0xccc` and the ramp timer `+0x2058`) was read at its head only — U3. **[Resolved by §5.5.]**
20. **`0x005ad9a0`'s full truth table** (§3.6.5) and the prompt/QTE routines `0x005b6af0`/`0x0060d960` (§3.3) were read for routing and input surface, not tabulated.
21. **Activity/mission masks:** the per-activity cases of `0x005b3c50` (jump table `0x005b3d0c`) and the mission types `0xa`/`0xe` of `0x005ab330` edit the masks (§3.2.4), but which activities and missions those numbers are was not resolved.
22. ~~**Two image values the controls depend on:** `0x012e6ced` = 1 (bypasses the crouch stick test, §3.6.3) has no writer in `.text`; `0x012ec730` = 0 in the image would **invert both walk axes** (§3.5.2 step 4) until the mission-16 reset `0x005e5e40` (nine call sites, raw scan, including `0x00702850` in the world band) or a mission-16 start writes it — whether that reset always runs before free-roam play was not traced. *Next step:* read both from a running process.~~ **PARTIALLY RESOLVED 2026-10-10.** Both image values are CONFIRMED by direct read of the static image: `0x012e6ced` = 1 (bypasses the crouch stick test), `0x012ec730` = 0 (would invert both walk axes per the unconditional, unguarded test at `0x005e5a90`). The mission-16 reset `0x005e5e40` writes `0x012ec730` to −1 (not 0), explicitly disabling inversion; all of its reachable call sites (5 resolved by reference-manager lookup, consistent with the raw scan's 9) sit inside one self-contained mission-16/"Avatar"-boss module (`0x005e59f0`–`0x005e7bb0`, its own `m16_invert_stop`/`m16_boss_invert_stop`/`Avatar_Attack_*` literals) and fire only from mission-16/Avatar-specific script events or from the player-death handler `0x00702810` when a companion flag (`0x012ec725`) shows that content was already active — never from a generic new-game, load, or mission-end path. Every writer of `0x012ec730` found by exhaustive reference census sits in that same module; the module registers its state as a named engine variable (`"mmcs"`, registrar `0x0086d770`, callback `0x005e5d50`) through a one-time, unconditional boot-time call chain, whose own logic would apply an external config override if one exists — a mechanism outside the `.exe` image itself (no such override exists inside the image). A reimplementation can treat the inversion as dead UNLESS it also ships the original default/config override data; from the `.exe` alone, the shipped default is the inverting one and the only code that turns it off is Avatar-boss/mission-16-specific. Residual, narrower OPEN item: whether an external (non-`.exe`) config/tweak-table default for the registered name `"mmcs"` supplies a non-inverting override before first input read — unresolvable from the executable image alone.
23. ~~**Camera mode 11** (§4.1): callers and the base save/restore are CONFIRMED; whether its update `0x0057ff10` re-aims at the object every frame, and when it falls back to the follow camera (`0x0056b370` at `0x0058021b`), is HYPOTHESIS.~~ **RESOLVED 2026-10-10.** `0x0057ff10`'s update re-aims at the tracked object every frame unless a freeze byte at `0x013cba54` is set, in which case it reuses the prior frame's stored offset unchanged. The fallback to the follow camera (`0x0056b370` at `0x0058021b`) is gated by a third byte, `0x013cba57`, which the mode-11 setup `0x00580260` sets to 1 only when its own 5th argument is 1; when that byte is clear the fallback check never runs at all. When active, the fallback resolves the cached target handle through `0x00458230`/table `0x031d152c`, declines if the target is being destroyed or its class-descriptor row (`0x02cc9900`) lacks bit `0x1` at `+0xa`, runs the shared line-of-sight/ray query `0x0075d7e0` from the camera position toward the target, and on a positive result calls `0x0056b370(0, 0.4s)` to blend back to the follow camera. `0x00580260` itself runs the identical check once, immediately, when its 5th argument is 1 — the "immediate" and "per-frame" fallback paths are the same test. Gameplay context: mode 11's setup is called from AI mode 43's tick, the cutscene manager, and `0x006bb720` — consistent with "object-relative hold camera (turnaround, cutscene manager)," not a vehicle turret or sniper scope.
24. **Bodies of the other camera modes** — look-through `0x0057a490` with the path builder `0x00578d40`; death `0x00569a60`/`0x00569c60`; gang customization `0x005676e0`; showcase `0x0057ee90`/`0x0057ece0`; store/customization `0x005681f0`/`0x005683c0`; cinema editor `0x00bca9b0`; the satellite writers `0x005b2540`, `0x00b700d0`, `0x00b709d0`, `0x00b70d10`; debug `0x005c2800`. Selection is specified (§4.1–§4.2); the bodies are U8, on demand.
25. **Camera mode 12** (`0x0057e7d0`, called from `0x007b6f30` ← `0x00707753`, `0x007ae43f`): its update is the stub, so where its transform comes from, and what the UI owner is, were not traced.
26. **HUD screens `0x35`…`0x39`** (§4.3) that keep the camera updating twice, or at all while paused: the ids are registry `+0x30` values in `0x012fced8`; their names were not resolved. *Next step:* map the registry entries' ids to their screen names.
27. ~~**Look-speed and FOV factors:** `0x00975770`/`0x00975810` (minimum of `0x02625bc4`/`0x02625bfc`/`0x02625c34`, resp. `0x02625bc8`/`0x02625c00`/`0x02625c38`; mouse scales `0x0130a0b4`/`0x0130a0b8`; `0x009785a0`/`0x009785c0`) and `0x00975bd0` (FOV): writers and option names not traced (HYPOTHESIS: aim slow-downs and the look-sensitivity option).~~ **PARTIALLY RESOLVED 2026-10-10.** The three identical look-speed records (`0x02625bc4`/`bfc`/`c34` yaw, paired `0x02625bc8`/`c00`/`c38` pitch, 0x38-byte stride, each also holding an int field defaulting to 2000) are zeroed to defaults (yaw=pitch=1.0) by two reset paths: `0x00976390` (one caller, `0x00708330`, uncharacterized — likely a level-load/session reset) and `0x009755f0` (one caller, `0x005d25f0`, which also reinitializes the drug/intoxication visual-FX asset handles in the same pass — a shared reset point, not evidence the slots are themselves drunk-effect values). The live, non-default writer is a 3-channel keyframe-curve interpolator (`0x009779d0` → `0x00977020` → `0x00976f50`): each slot is driven by its own progress ratio (an unidentified object's fields `+0x1650`/`+0x1654` divided by per-channel duration constants, default 100.0) against up to five keyframes per channel — a genuine data-driven ramp consistent with (not confirmed as) an aim-state slow-down; the owning object and caller chain weren't traced. The FOV multiplier `0x00975bd0` is CONFIRMED and is NOT an options-menu value: its sole writer `0x009758b0` is the drunk/weed-effect axis modifier already documented for the movement stick (§3.5.2 step 4, loaded from `drunk_levels.xtbl`), which writes the FOV scale as a side effect while computing the drunk movement stagger — this REFUTES the "look-sensitivity option" hypothesis for the FOV piece. The secondary ×1/×3.0 multiplier read by `0x009785a0`(yaw)/`0x009785c0`(pitch) is driven by a shared state integer (`0x02626520`) whose setter cluster carries the strings "Shake your head (camera) to wake up!"/"Tap %s to WAKE UP!"/"sleepy" — the sleepy/fatigue-while-driving minigame's wake-up prompt, tripling look sensitivity in its two active states so a head/mouse shake registers as the gesture; also NOT an options-menu setting, refuting that half of the hypothesis too.
28. ~~**Vehicle camera-angle sets and presets** (§4.12): no reader of the record's `+0x628`/`+0x648`/`+0x668` sets was found in the camera band; the preset placer `0x0056d390` with the blocks `+0x688`/`+0x6a4` (chosen by `0x00ad3ae0`/`0x00ad3b10`) and sub-state 2's handler `0x00571680` were not read; `0x00a77120` (HYPOTHESIS: `Max_FOV`) and `0x00a77170` (distance factor) were not read. Scoping OPEN 8 is therefore only partly answered.~~ **RESOLVED 2026-10-10.** **[Advanced by §8.6: the vehicle-interaction hand-off `0x00b0bc10` reads all three sets, passing Primary (or RC) to `0x0056cf60` and Secondary to `0x0056cd10`.]** The preset placer `0x0056d390(V, block)` transforms the preset block's leading vector through the camera band's shared transform helper and forwards it with three tuning floats (`+0xc`/`+0x10`/`+0x14`) and two mode bytes (`+0x18`/`+0x19`) to the sub-state-2 preset placer. The Primary-preset selector `0x00ad3ae0(V)` gates on three conditions — vehicle non-null, record byte `+0xbd0` == 1, and a non-null pointer at record `+0x808` — returning a flag byte read through that pointer at `+0x19c`; `0x00ad3b10` mirrors it for the Secondary preset. Sub-state 2's handler `0x00571680` is confirmed genuinely distinct placement logic: it resolves a preset target through a handle+vector pair, blends three weighted orientation rows, and runs the result through the same transform chain the rest of the camera core uses. `0x00a77120` returns the vehicle record's `+0x864` (CONFIRMED `Max_FOV`, default 45°, with a custom-preset override path gated by the identical `+0xbd0`/`+0x808` condition) and `0x00a77170` returns `+0x868` (CONFIRMED a per-vehicle follow-distance scale factor, default 1.0, same override gating) — tying the FOV cap, the distance scale, and the angle-preset choice to the same per-vehicle "has a custom camera configuration" switch. Residual, cosmetic only: the exact xtbl field names, and the custom-preset override functions' own bodies (`0x00adaef0`/`0x00adaf40`).
29. ~~**Lateral-offset re-add** at the start of the core (§4.7 step 1): last frame's shoulder offset is added to the working position again before the position is recomputed; behaviour CONFIRMED, purpose unknown (only the intermediate reads see it).~~ **RESOLVED 2026-10-10 — not a drift bug.** Full trace of the core (`0x00575e30`, 1558 instructions): the position is fully recomputed from scratch every frame at the placement step (pivot + distance·back, collision-resolved), and the shoulder offset stored for next frame is always freshly computed at the end of the same frame from the blended `x_shift` — neither is ever built incrementally on the previous frame's value, so there is no possibility of drift or compounding across frames. The re-added value from the top of the frame is read exactly once before being discarded: the vehicle fallback-camera distance ramp (active only when a specific vehicle-fallback timer is running and the submode isn't 7/8) measures "the camera's current distance" by subtracting the pivot from the working position at that point, so when a shoulder offset was non-zero the previous frame, the ramp's distance measurement for this frame carries one extra, stale copy of that offset. The bias is bounded to a single frame — overwritten before it can accumulate — and is otherwise inert in every other submode/configuration (written once, never read, then discarded by the placement step's overwrite).
30. ~~**Follow sub-states 1–5 and the human submode chooser:** the six handlers (`0x005710b0`, `0x0056c450`, `0x00571680`, `0x00573b10`, `0x00571b10`, `0x00571e60`) and their setters are named (§4.7 step 6) but were read for structure only; the human chooser `0x005745c0` (370 instructions — the on-foot counterpart of `0x00573110`, deciding exterior/interior close, sprint, crouch, swimming, fine aim, ragdoll, …) was not tabulated.~~ **RESOLVED 2026-10-10 for the human chooser (the follow sub-state handlers themselves remain untabulated).** `0x005745c0` (375 instructions, full listing) is a flat ordered dispatch of ~39 predicate tests, each returning a literal index into the already-documented 61-entry camera submode name table at `0x012e3d48` (confirmed by direct read of that table and matching every literal return value to its index — not a guess). Distinct on-foot submodes it can select, with their real names and gating tests: sprint (`0x00572c50`: player-class AND sprint-bit/sprint-test AND not-in-cover AND no local weapon-state exclusion), crouch (`0x00985cd0`, the same function already cited at §3.6.3) and crouch interior (`0x0056e130`), swimming (`0x009b5690`, the same function already used in §5.4.3 step 7), fine aim/fine aim underslung/fine aim crouch (`0x00572af0`/`0x00572b20`/`0x00572b60` — plain fine aim uses `0x009da830`, the exact function §5.4.2 already names for "fine aim"), ragdoll (`0x0056adf0`: local player AND physics-representation byte `+0xcd2 != 3`, narrowing that field's existing HYPOTHESIS so that value 6 is the true ragdoll state and 3 is a separate "recovery" state), falling (`0x0056ae20`), avatar (`0x005e6ce0`, the identical class/state test independently found gating the mission-16/Avatar reset's reachability at OPEN 22 above), downed (`0x009715d0`: a body-state field `+0xcc8 == 6`, the same field tested at 0xc/0x10 for "avatar"), plus shooting qte, leaping (a direct controller-state compare against the literal `0x410`, no helper), riot shield/human shield (plain and fine-aim variants), interior melee, interior sprint, freefall, parachute, skydiving (six down/dive/up/tank variants), rappelling (three variants), script-driven submodes (four variants, one gated by a plain flag byte `0x013c9382`), MAS fine aim, zoom/zoom crouch, vehicle aim (reached from this on-foot chooser for a vehicle-gunner human), and wrestling as the penultimate fallback before the final default branch (`0x0056db50`, choosing between exterior/interior close).
31. ~~**Sway amplitudes** `0x013c936c`/`0x013c9370` (§4.7 step 9): their writers were not traced (HYPOTHESIS: impacts and explosions).~~ **PARTIALLY RESOLVED 2026-10-10 — HYPOTHESIS REFUTED.** Their writers are `0x0056b470` (sets both to a fixed 0.3× scale of a caller-supplied magnitude) and `0x0056b4c0` (zeros both). `0x0056b470`'s only callers are a magnitude-shaping helper `0x009766f0` and the QTE routine `0x0060d960` (§3.3's always-run QTE hook); `0x0056b4c0`'s only callers are the vehicle-entry/interaction state machine `0x0060c3f0` and `0x0060d000`. `0x009766f0` in turn is reached from `0x00976900`, which reads/writes the same unidentified object's `+0x1650`/`+0x1654` progress-ratio fields that drive the look-speed/FOV keyframe interpolator left untraced by OPEN 27. No caller in either chain touches damage/impact/explosion code, refuting the "impacts and explosions" hypothesis outright. The sway amplitude is instead tied to QTE execution and vehicle-entry/interaction state, through the same unresolved per-object ramp system OPEN 27 already flagged — residual narrowed to identifying that ramp's owning object, now shown to be shared between the two items.
32. **Look-at assist** (§4.7 step 12): who starts the timer `0x012e41a0` and sets the object `0x013c92e8`/`0x013c92ec`, and what the 20-pass refinement `0x0056e170` converges to, were not traced.
33. ~~**Camera collision filter** `0x00566070` (407 instructions) and the skipped hit kinds 1, 4, 8 (§4.9): which objects block the camera is not tabulated.~~ **PARTIALLY RESOLVED 2026-10-10.** Full listing shows the filter resolves the candidate through the shared class-descriptor table `0x02cc9900` (indexed by the candidate's class byte `+0x34`) and, via three one-bit class-row tests (`0x00b71950` on byte `+6` bit `0x80`, `0x00b71970` on byte `+6` bit `0x20`, `0x005910f0` on byte `+7` bit `0x4` as fallback), decides whether the candidate is the local player's own vehicle (or tied to it) and should be excluded from blocking the camera. The first branch excludes the player's own vehicle outright except while entering/exiting it; the second excludes a rider-specific flag combination; the fallback excludes any candidate whose bounding volume overlaps the player's vehicle's own bounding volume. No literal comparison against the integers 1, 4, or 8 occurs anywhere in this function; the existing "kinds 1, 4, 8" label does not match any value tested here and could not be corroborated this pass — it may belong to the still-OPEN per-candidate kind test inside the shared sweep wrapper's own helper `0x0075c250` (`spec-physics-runtime.md` §8.8), not to this filter. Residual: re-point or retract the "kinds 1, 4, 8" label.
34. **Small unknowns around the follow update:** the surface clamp `0x0079a420` (RESOLVED 2026-10-11 — a global-box-gated downward raycast, not water-specific; see the body text above and `spec-zone-data-format.md`'s own water-hypothesis Changelog entry; the global box's own real coordinates remain OPEN), `0x0056ef10`, `0x0058b470` → `0x00a1f880` after the update, the shake `0x0057c930`, transition-blend type 1 `0x00580430`, the pitch return `0x0056bfe0` (uses `pitch_reset_time`), and the hill-tracking step `0x0057ad60`.
35. ~~**Controller motion `0x007a5b10`** (§5.6.5): the state logic between the support query and the final velocity set...~~ **RESOLVED 2026-10-07 (unit W-U2, full listing).** See §5.6.5: full gravity formula (quadratic terminal-velocity falloff), jump-launch formula (`v=√(2|g|h)`), the full state-bit catalog, the three-timer chain, the step-up/step-down/ledge-assist raycasts, and both other callers (`0x007a7413`/`0x007a750e`, confirmed to be internal continuation labels, both jump triggers). One residual non-blocking item: `controller+0x160`'s margin constant (5.0) and the step-assist raycast lengths' exact real-world units (frames vs. a fixed-point angle) are not pinned down.
36. ~~**The custom proxy passes** (§5.6.4): ...and on which thread the listener callbacks run...~~ **RESOLVED 2026-10-07 (unit W-U2, full listing).** See §5.6.4's own deepened pass description and §5.6.4a: all 8 named functions read, the lock-step cycle fully traced, `0x0079de40` confirmed reached via the proxy's own primary-vtable slot `+0x14` (not a separate Havok method), and thread safety settled — both `MyCharacterListener` and the phantom-listener fan-out run on the game's persistent class-3 worker-pool thread that services that proxy's job, not guaranteed the main thread, not serialized against other proxies in the same pass.
37. ~~**`turn angle` (`+0xaac`)** (§5.3): `0x00956970` chases a target computed inside the animation job `0x00994970` (call at `0x00994a3d`); whether that target is U2's `+0xa68` was not traced.~~ **PARTIALLY RESOLVED 2026-10-10.** `0x00956970(human, target)` (confirmed: a rate-limited follower of `+0xaac`) does NOT simply chase `+0xa68`. Its caller `0x00994970` sets the target to 0.0 unless `+0xcd2` ∈ {0, 3, 6} (character proxy or ragdoll/recovery), in which case the target is the return value of a large (~1065-instruction) facing-computation routine, `0x009b4360(human)`, which reads `+0xa68` directly (confirmed, two sites) as one input among several (fine aim, cover, boosted-state and camera-mode checks). For humans with any other `+0xcd2` value, there is no turn-angle chase at all that frame (target fixed at 0). Residual: `0x009b4360`'s other inputs (fine aim/cover/boost/camera contributions) were not individually traced.
38. ~~**Movement flags `+0xd40`** (`spec-anim-runtime.md` §4.2 rows 8–19): writers not traced.~~ **RESOLVED 2026-10-10 — writers found, documented bit meanings hold up.** The per-frame writer is `0x0095a020(human, [human+0xcc8], [human+0xccc], &out)`, called from `0x0095cbc0` (itself run every update from `0x00994a80`/`0x008a2660`), which first saves the prior `+0xd40` value to `+0xd44` then stores the newly computed value; a second pass, `0x009aab30`, can further adjust it via a 6-way switch on byte `+0x18d4` (not individually opened). `0x0095a020`'s full listing confirms rows 9-19's bit assignments but shows each is a derived condition, not a raw pad-bit copy: the move-direction bits (rows 9-11) come from a heading-angle quadrant test with ±0.2-rad hysteresis against the previous frame's own bucket; alternate-strafe/backpedal (rows 12-13) are animation-state-gated refinements of the move-left/move-right bits; walk/run (rows 14-15) come from the byte at `+0xccc` taking values 1/2 (an undocumented value 3 sets bit `0x100`); crouch (row17) comes from `+0xe8` bit `0x20`, not a raw input; aim (row18) comes from the controller's animation-state id matching a combat/weapon-state list; fine aim (row19) can be driven by the AIM TARGET's own `+0x1d98` flag bit rather than purely local input. Two further bits outside rows 8-19 are also written here: `0x0004` and `0x10000`, the latter shape-consistent with a UI/menu-open gate. Residual: `0x009aab30`'s 6-case jump table and the `0x0004`/`0x10000` bits' exact semantics are HIGH CONFIDENCE but not independently confirmed. Cross-reference: `spec-anim-runtime.md` §4.2.
39. **Animation-side inputs of the composer** (§5.4.3). **[Partly resolved 2026-10-03, §5.4.3 "Step 3 in detail".]** ~~the slot flag `+0x80` bit 0 that separates T from E (HYPOTHESIS: `Anim_Movement`)~~ — the discriminator is **animation-instance layer flag bit `0x01`** (layer `+0x10`), the phase-synchronised group mark set by the layer-post API (`0x004c6590`) for plain clips, blend-tree `States` and looping graph clips and left clear for non-looping graph clips; `Anim_Movement` is refuted for this role. ~~the third vector X~~ — X is a translation (the non-synchronised deltas in their start-of-clip root frame, unweighted), not a rotation. ~~which animation states set the animation-controller flags `0x100` and `0x4`~~ — they are the record flags `Anim_Movement` and `Block_Translation` OR'd over every playing animation into controller `+0x198`, so the answer is per record in `anim_files.xtbl`. ~~**Still open:** the orientation step `0x004b6530` (X's consumer), the speed `0x004b49e0`, `0x009987d0`'s state test, controller `+0x1a4` bits `0x40`/`0x100`/`0x800`, the full read of `0x004c8a80` (root-rotation naming HIGH), the override weight `0x004cad00`, and the layer creator at `0x004c6ebb`. Belongs with the root-motion unit of `review/scoping-animation.md` (U5).~~ **[Resolved 2026-10-07, `spec-anim-runtime.md` §9 (U5).]** `0x004b6530` is the correction step: outside an authored `start correction`/`dest correction` window it returns the orientation unchanged, and inside one it is a motion-warp and heading-correction step that X (and, for a start correction, the velocity) feeds (§9.7) — so the question of what turns a human in *ordinary* locomotion is still open, newly as that file's §5.1 item 35 (also noted at §5.4.5 above). `0x004b49e0` is the current state's clip speed (0 for a managed/graph slot; §9.9). `0x009987d0` is the `force use velocity start`/`end` trigger-window test, true during a player jump's take-off (§9.9). Controller `+0x1a4`'s bits are named in full at `spec-anim-runtime.md` §9.10. `0x004cad00` is the root track's per-track override value (`spec-anim-format.md` §10; §9.9). `0x004c6ebb` is an in-place clip swap with a timed cross-fade, reached from the controller's slot swap `0x004b73f0` (§9.9). `0x004c8a80` is identified as the bound-track rotation decoder (smallest-three keys, spherical interpolation); its bit-for-bit decode is routed to `spec-anim-format.md` §6c.2/§6c.3, not re-derived there. **[§8.1: the seating commit clears `+0x1a4` bits `0x0800`–`0x4000` through `0x004b7f70`.]**
40. ~~**`+0xccd` values 2 and 3** (§5.4.3 step 5, §5.4.4): they route the player through `0x009918b0` at stance speed. Values 4 (jump/dive), 5 (climb) and 6/8 are named by `spec-anim-runtime.md` §4.2 and §3.2.2; 1 is "moving" (`0x00987360`); 2 and 3 are not identified.~~ **PARTIALLY RESOLVED 2026-10-10.** Value 3 was already resolved elsewhere in this file (§10.6/§10.10): "walking to a vehicle entry point," written by the move-to-entry-point navigator `0x00993e90` through `0x00986920(human, 3, 0xd or 0xe)` — this OPEN item simply hadn't been updated to reflect it. Value 2 is not independently distinguished anywhere in the executable's own dispatch logic: the master locomotion-substate setter `0x00986920` runs a transition-cleanup switch keyed on the previous `+0xccd` value with explicit cases for 2, 3, 6, 8, 10, 11 and 13 — 2 and 3 share one identical case body, just as the range test `0x0098da50` ("is `+0xccd` 2 or 3") and the stance-speed routing into `0x009918b0` treat them as a single class throughout. A byte-for-byte copy of the same switch lives inside the per-human network-snapshot routine `0x00988830`, which is also where "`+0xccd` is forced to 0 on a non-owned/remote copy" (already noted for value 3) actually happens. Value 2 could not be tied to a distinct setter call site in this pass, but it co-occurs specifically with vehicle-interaction phase 1 ("entering," `+0x16d4` == 1, via `0x009b9210`) in the per-human network-gather record's flag table — the same pipeline that produces value 3 — which points toward 2 being an earlier or more generic phase of the same destination-directed walk (plausibly a generic "moving to a target point" state that the vehicle-entry navigator specializes into 3), rather than a crouch- or cover-specific stance. No animation ID or crouch/cover flag distinguishes the two anywhere this pass read. Residual: find the call site(s) that set `+0xccd` = 2 specifically (the ~30 callers of `0x00986920` were not individually checked for a literal `2` argument).
41. **The gather's second NPC-LOD branch** (§5.6.3 (b)): as read it never engages, because `+0xaa8` is armed at 0 ms by the human constructor `0x00954340` (at `0x0095559d`) and is re-armed only on the branch's not-expired path. Behaviour HIGH; a port reproducing the shipped game should keep slow NPCs stepped. *Next step:* read `+0xaa8` on a live NPC.
42. **Contact-replay handlers** (§5.8): `0x007a37f0` (class bits `0x0a`/5), `0x007a2470` (human), `0x007a3fa0` (class bit `0x1e`), `0x007a3640` (class bit 6) and `0x00962c20` (human–human) were read for call shape only; the names of class bits 3, 5, 6, 8, `0x0a` and `0x1e` of the kind rows `0x02cc9900` are not established here.
43. ~~**Ungated sprint reads** (§5.8, §5.4.3 step 9): `0x00961d70` and `0x0099d1b0` read the local pad's `CBA_OFC_SPRINT` for any human carrying the `+0x1c9c` upgrade bits; whether a remote co-op player can carry them (and would then react to the local pad) was not traced.~~ **RESOLVED 2026-10-10.** Neither function tests whether the human being evaluated is the local player before calling the sprint query — confirmed by full listing: the guard immediately before each boost is only the `+0x1c9c` bit test followed directly by the sprint query, with no local-player check anywhere in either function's body. This is not an oversight a future patch of these two functions could fix in isolation: the sprint query itself, `0x005bd7b0`, takes only an action id and an optional output pointer — it carries no human or player-index parameter at all, and resolves purely through a fixed global action-binding table and fixed global input-state words. It is architecturally a single local-input query with no notion of "which player," so any human object that reaches either boost branch necessarily reads this machine's own pad. The `+0x1c9c` field itself is not type-restricted — other bits of the same dword (e.g. bit `0x2000`, "standing on a moving support") are confirmed read against every human regardless of class — so no structural barrier prevents bits `0x80000`/`0x100000` from being set on a non-local human if a writer ever reaches one; today none is confirmed to (see OPEN 45's exhaustive `unlockables.xtbl` check and its untraced `cheats.xtbl` "Infinite Sprint" lead — that residual now carries this item's live-risk question too: the bug is primed on the read side and only needs a writer that targets a non-owned human to become real).
44. ~~**`+0xa64`, the per-human speed multiplier** (§5.5): 1.0 at construction (`0x009544c5`); its writers `0x0062e294`, `0x006ebdc1` and `0x007d3073` were not traced (HYPOTHESIS: script and activity speed overrides).~~ **PARTIALLY RESOLVED 2026-10-10.** `0x0062e294` and `0x007d3073` are REFUTED as writers — both are coincidental offset collisions on unrelated structures (a bulk record-array constructor and a UI retry-counter state machine respectively), confirmed by listing to never touch the human object. The one genuine writer, `0x006ebdc1`, is case 10 of a 25-case per-human command dispatcher `0x006ebc00(human, flag)` (confirmed as human-object code via its own `+0x1b78` inventory reference) and unconditionally resets `+0xa64` to 1.0 — not a boost. It is reached from a queued-command flush `0x006ed2a0` and from inside the HUD/UI-screen dispatcher `0x006ee130` (registry `0x012fced8`, the same registry OPEN 26 cites). CONFIRMED by disassembly throughout; the producer that enqueues command id 10 (what actually triggers the reset) was not traced — narrower residual. No boost-setter (value > 1.0) was found anywhere in this pass, so the "script and activity speed overrides" HYPOTHESIS is unconfirmed pending a fresh search for the enqueue site.
45. ~~**`+0x1c9c` bits**: `0x2000` (HIGH: standing on a moving support — it gates the NPC LOD, air control and landing, and adds `+0x1c60` to the velocity), `0x80000` and `0x100000` (the sprint and jump upgrades): writers not traced.~~ **[§11.5.4: the vehicle-interaction eject handler also reads bit `0x2000` (to choose its parachute/safety path) and writes `+0x1c60` on the no-parachute path — a second, independent confirmation of this bit's role and of the `+0x1c60` velocity-add, from outside the locomotion bridge.]** **No setter for `0x80000`/`0x100000` found 2026-10-10, but the read side narrows the item's framing.** `0x009e91c0` (the same function from OPEN 15's bit-`0x04` finding) tests bit `0x80000` to gate a decaying speed-cap ramp (two globals, `0x0263ae30`/`0x0263ae34`, decaying toward `Player_max_sprint_speed` at `0x0262e9e8` at the frame rate, or snapping to a fixed table value `0x01175aa0`) — behaviorally a **transient burst/power-up, not a flat permanent multiplier**. The full 61-type `unlockables.xtbl` apply-routine table (`spec-tables-progression.md` §4.4) was checked exhaustively and does NOT touch `+0x1c9c` or any sprint/jump bit — this REFUTES the perk-registry-via-unlockables reading specifically (the one sprint-named unlockable, type 8 `Sprint_Bonus`, only writes a save-format stamina-meter scale, an unrelated mechanism). `cheats.xtbl`'s "Infinite Sprint" cheat (`spec-tables-progression.md` §8.3) is a more promising unexplored lead; its apply-side code was not traced. Setter search should target cheat-apply code and/or Lua-bound script natives, not the unlockables pipeline, which is now ruled out.
46. **Small unknowns in the bridge:** `0x0094a3b0(player, 0)` (§5.4.3 step 9); `0x00997b50`'s local-player checks `0x0098a3f0`/`0x00710cb0`; `0x0063c830`; the controller bit `+0x150` bit `0x02` behind `0x00942260`; the visibility test `0x0091f310(·, 2)`; the `+0xcd2` values; the pony-cart case's constant T (`0x0262d030` × Δt); and the second loop of `0x0099ebe0` (shark attack, drowning, cover exits) beyond structure.
47. **Exit-request fields read outside U6.6** (§6.2). These are:
    - ~~the object handle `+0x08`/`+0x0c`~~;
    - the vector `+0x10`–`+0x1f` (HYPOTHESIS: the launch vector);
    - `+0x20`/`+0x24`;
    - the animation override `+0x2c`;
    - ~~`+0x30` bits 4–7. Bit 7 is set only by the player's prompt `0x009e5e80`~~.

    *Next step:* U6.7 (the animated exit states read them). **[§9.5: `+0x20`/`+0x24` are the victim's extract kind (`0x0a`…`0x11`, or `0x13` = `extract fast`) and approach side, written by the hijack chooser `0x00afa540` and copied to slot `+0x10`/`+0x20` by `0x1d`'s on-entry `0x00b02900`.]** **[§11: resolved. The object handle `+0x08`/`+0x0c` has three readers (§11.0 item 4, §11.8). The launch vector `+0x10`–`+0x1f` is confirmed for the eject kind (§11.5.4). Bits 4–7 of `+0x30` are now all explained: bit 4 forces `exit dive`, bit 5 selects `exit eject`, bit 6 is also written (not only read) for a resolved eject companion, and bit 7 is also written by the exit-fast side-clearance cascade, not only the prompt (§11.5.2, §11.8).]**
48. **Slot `+0x10` and the landing kind `+0x1c`** (§6.11 step 11):
    - the finaliser's airborne branch runs only when the slot's old `+0x10` is `0x17` or `0x18`;
    - the landing-kind values 0/1/2 (airborne/ground/water) are HYPOTHESIS;
    - `0x00b0f060`'s result, which `0x17` stores in `+0x1c`, was not read.

    *Next step:* U6.0's slot layout and U6.7's per-frame writers of `+0x10`. **[§9.3: the key builder maps `+0x1c` −1/0/1/2/3 to `none`/`air`/`ground`/`vehicle`/`water` — so 2 is `vehicle`, not water, for element keys, and the hub `0x00afd4c0` writes the field on the enter side too. Exit `+0x10` is the exit kind `0x14`…`0x1c`; `0x17` = `exit dive` and `0x18` = `exit at altitude`, which is what the finaliser's airborne branch tests.]**
49. **Animation state `0x1d95`**, which `0x17` plays and the finaliser ends (it is also queried by `0x009b95c0`, `spec-lua-api-behaviour.md` §22.3): its name is unread. *Next step:* resolve it against the animation-state table (`spec-tables-animation.md`).
50. **Equip-on-exit details** (§6.7.3):
    - an occupied inventory slot 3 with an empty slot 5 still switches to slot 5 (CONFIRMED as written; intent unknown);
    - the slot meanings (HIGH: 5 = rifle) are not confirmed;
    - the roles of `0x00943960(human, flag, 0)` and of the back-dated `+0x1394` timer are HYPOTHESIS (combat-ready on arrival).
51. **The exit-permission globals** (§6.3.2): what `0x022cde2e` (bits 0–2) and `0x022cf8d0` are, which gate the local player's non-instant exits through `0x00818570`; and the vehicle field `+0x1ac8` (`0x00ad33a0`).
52. **State `0x18`** (§6.10): no local setter was found, so it is HIGH: dead. A network capture or a live trace would settle it. **[§7.5 agrees from the full transition census, and finds enter 7 and exit `0x1c` in the same position (OPEN 60).]**
53. **Vehicle-side callees of `0x15` and `0x19`** (§6.7.2, §6.11), named as contracts only:
    - in `0x15`: `0x00ae92b0`, `0x0045ea70` (hash `0x286e13b2`), `0x00aa92c0`, `0x00ad57a0`, `0x00ad5460`, `0x00a929e0`;
    - in `0x19`: `0x00aa2080` with `0x00a88e70`/`0x00a88f00`/`0x00aa1f60`/`0x00a88eb0`, `0x00ac2280`, `0x00a7e040`/`0x00a7b690`/`0x00a7b520` (under `0x014f71e8`), `0x00abcd50`, `0x00ad35b0`, `0x00ab9c20`, `0x00aaa3d0`, `0x00607ea0(3, −1, 2)`;
    - the mounted sub-object at vehicle `+0x688`, used when `+0xbd0` = 1.

    *Next step:* the vehicle runtime unit.
54. **Tip-over test** (§6.7.1): what the predicates `0x00ad3b40`/`0x00ad5240` select, and whether `0x00da48a0`'s second angle is roll or pitch.
55. **Class and object bits used by U6.6**:
    - class-descriptor row byte `+0xa` bits `0x02`/`0x04`;
    - the `0x00853b30` class bits `0x21` and `0x22` (`0x22` gates despawning a dead occupant);
    - the human's `+0x33` bit `0x08` (HYPOTHESIS: instantiated; §5.6.3 requires the same bit).

    Their names are not established. **[§7.11: `0x00853b30(object, n)` tests bit n mod 8 of kind-row byte `+6` + n/8, so class bits `0x21`/`0x22` are the same bits as row byte `+0xa` `0x02`/`0x04`.]**
56. **Guard quirks** (behaviour CONFIRMED):
    - `0x009b9250` reads `+0x16d4` from a null human on its null branch;
    - `0x00af2460` and `0x00af24d0` index slot −1 (record `+0x5a8`) when the human has no slot.

    A port should guard these. Whether any shipped caller reaches them is not traced.
57. **Finaliser speed rescale** (§6.11 step 9): `0x00942110(human, 0, 0xff, +0xa60)` multiplies a positive `+0xa60` by `+0xa64` again. Behaviour CONFIRMED; whether it compounds in practice depends on `+0xa60` at that point, which `0x15` has set to −1.
58. **`vi_exit.xtbl` has no `Element` whose first token is `deactivate`, `open` or `close`** (§6.12 item 4), the first tokens of exit states `0x1a`, `0x1c` and `0x20`. For U6.7: either those states are never set, or they resolve their keys elsewhere. **[Answered by §7.5: `0x1a` and `0x20` have no on-entry handler and can never be held; `0x1c` has one but no static producer (OPEN 60).]** **[§9.5: none of the handlers of `0x1a`, `0x1c` or `0x20` reaches the element rule (census closure), so none of them would choose an `Element` even if held.]** **[§11.2: confirmed again from the handler side — `0x1a`/`0x20` have no on-entry handler at all, and `0x1c`'s on-entry is a confirmed no-op that writes no key-relevant field.]**
59. **Network side of exit** (§6.5, §6.6.3, §6.10): `0x00b03010`'s send conditions (it sends `0x15` only under the `0x008ae480` gate), the record builder `0x00af3630` (queue `0x028888e8`) and the applier's state handling are recorded at call-shape level. They belong to the parked network unit.
60. **Enter state 7, exit states `0x18` and `0x1c`** (§7.5): each has an on-entry handler that stores its number (7 and `0x1c` also have per-frame handlers), but no setter call site, chooser return or direct handler call produces them; only the network applier `0x00af2950` could, by replaying a remote value. For `0x18` this agrees with OPEN 52. *Next step:* check the applier's received values and any computed state in the network path; otherwise treat them as dormant, like `0x10`/`0x12`/`0x1a`/`0x1e`/`0x20`.
61. **Unnamed record and slot fields** (§7.1–§7.2): record `+0x08` (0/1/2, written only by the state-1 hub), slot `+0x28` and `+0x34`, the handles `+0x48`/`+0x50`, and request `+0x28`…`+0x34`. The exit fields are in §6.1/§6.2. *Next step:* the units that own those states.
62. **Commit side effects read for role only** (§8.1–§8.2): which vehicle type each tutorial predicate `0x00ad3140`…`0x00ad32d0` tests, and the meaning of the `0x005bc5d0` variant (§8.1); the report objects keyed 7/`0x16`/`0x17` (§8.2 step 19); `0x00bd8240`'s globals; `0x004e4130(human, 0x19)`; the vehicle-AI passenger block `0x00b47110`/`0x00b4d260`/`0x00b464c0`/`0x00b462a0`, `0x00b24cc0`, `0x00b25c40` and `0x00935130`; `0x00ab9c20`'s seat effect; `0x00aa91c0`; and the meaning of the human bits `+0xe0` `0x100`, `+0x3a` `0x80`, `+0xe4` `0x1000`, `+0xec` `0x100000`, `+0x1b08`/`+0x1b10`, `+0x19bc` and the vehicle bits `+0x16c0` `0x40`/`0x800`/`0x1000`/`0x4000`/`0x100000`, `+0x16c8` `0x08`.
63. **The six ride tweaks** `VI_forward_vel_accel`, `VI_vert_vel_accel`, `VI_random_accel_factor`, `VI_reaction_delay_min_ms`, `VI_reaction_delay_max_ms`, `VI_collision_min_vel_diff` (`0x0288ca88`…`0x0288ca9c`, §8.5) are bss with no initialiser in the image, so their shipped values come from tweak data that was not located; the readers of the lag values slot `+0xe0`/`+0xe8` were not traced (HIGH: U6.8's pose driver). *Next step:* find the tweak loader for the registrars `0x0071b580`/`0x0071b530`, or read the values from a running process. **[§12.4 (U6.8): the `0x00af4b80` half is closed — it reads slot `+0xe4`/`+0xec` (the ride body-lag model's *error* terms, not `+0xe0`/`+0xe8` the *lag* terms) and names three more `VI_*` tweaks (`VI_forward_diff_max`, `VI_vert_diff_max`, `VI_tilt_rvec_pitch_max`), all registered through the same `0x0071b580`. The tweak loader for all nine named values is still not found.]**
64. **Inventory slots 0 and `0xe`** selected by the holster (§8.4), `0x00600720`'s other arguments, `0x005ffaf0`, and player `+0x28ac` bit 4 (which turns `0xe` into 0). HYPOTHESIS: 0 is empty-handed. *Next step:* the inventory unit.
65. **The character-controller hand-over at the commit** (`0x0094cca0(human, 0, 0)`, then `0x007a5a90` on `+0xcc0` with `0x00b0e9f0(vehicle)`): HYPOTHESIS that the controller is parked on the vehicle body while seated. P5 / U6.8.
66. **The count-bounded vehicle queries** `0x00af1770`/`0x00af17e0` (§7.9): defect CONFIRMED in code. Whether it is observable depends on whether a lower slot of a record can be released while a higher one stays occupied (likely with passengers leaving out of order). *Next step:* exercise it.
67. **Network replication of U6** (parked): the record's shadow `+0xea8`…`+0x163f`, the applier `0x00af2950`, the receiver `0x00af8810`, the queue `0x028888e8`, `0x00af3630`'s packet drop (§8.2 step 2), the ownership request `0x008addd0` (§7.6), and the send conditions of §7.7. Together with OPEN 59.
68. **The extract-kind override `0x0130e5f4`** (§9.4.3, §9.5): −1 in the image; 13 references, the writers at `0x00af9419`…`0x00af954f` (HIGH: debug commands). When it is not −1 the hijack chooser `0x00afa540` uses it as the extract kind, and the next-state chooser `0x00afb220` skips a class test when it is 9. Not traced.
69. **Variant-field writers outside this unit** (§9.3): no writer of exit-request `+0x31` bit 0 (the `convertible` exit variants) was found here. Request `+0x21` bits 2/3 (`traverse no door`), 4/5 (`open shady`/`shopper`) and 6 (`enter convertible`) come from the request's creator. ~~The hub's `+0x1c` values, and `0x00b04ae0`'s register-written exit kinds (`0x14`, `0x19`, `0x1a`) and `exit fast` sub-variants (9/`0x0a`/`0x0b`), were not traced.~~ *Next step:* U6.3 (hub), ~~U6.5,~~ ~~U6.7~~. **[§10.4.1/§10.10: the hub's `+0x1c` values are the target-point type, 0–5.]** **[§11.5.2: `0x00b04ae0` is read in full. It writes kinds `0x15`/`0x16`/`0x17`/`0x18`/`0x1b`/`0x1c` as immediates, with `exit fast` sub-variants `9`/`0xa`/`0xb` exactly as guessed here, plus `0xc`/`0xd` for `exit at speed`. It does **not** write `0x14`, `0x19` or `0x1a` in the traced control flow — `0x14` is HIGH: the implicit default; `0x19` is a state number, not a kind, pushed elsewhere; `0x1a`'s (`exit climb`) producer is still not found (new OPEN item). The exit-request `+0x31` bit 0 writer is still not found either — U6.7 found a *second reader* at the same bit position (`0x00b02460`, a one-shot door-clear latch, §11.7.4) that conflicts with the `convertible` reading and is not resolved; no writer for either role was found.]** U6.5 is still outstanding for the variant-field writers on the enter side.
70. **Dead and shadowed shipped `Element`s** (§9.6.2–§9.6.3): 5 `vi_exit` `Element`s no key can produce, and 4 shadowed alternatives with distinct animations (`Heli Fighter Rider` `Exit Left Fast`, `RiotCop_Right` Bash/Shoot/Punch). In the shipped build they never play through this rule. Whether they are authoring errors or meant for an older key layout is unknown. A port reproducing the original keeps them as inert data.
71. **The ride key** (§9.6.4): who builds the six-token key passed to `0x00b08b80` (callers `0x00b08ea0` and `0x00af4760`)? That decides whether all 454 `vi_ride` `Element`s are reachable, including the two whose token 0 is `none`, and how the fallback chain to animation ids `0x10a`…`0x111` is used. *Next step:* U6.4/U6.8. **[§12.6 (U6.8): the `0x00af4760` half is closed — it builds the six-token key during the enter/exit phases (not ride) for a specific state subset and calls `0x00b08b80` directly. `0x00b08ea0`'s half remains open.]**
72. **Guard quirks of the rule** (behaviour CONFIRMED):
    - short keys compare uninitialised stack words, and the discarded diagnostic indexes `0x0130e638` with them (§9.4.3);
    - an out-of-range seat indexes `Seat_Info` −1 (§9.4.1);
    - `0x00dab660(5, N+4)` divides by zero when the count N is 0 (§9.5).

    Only the non-hijack `0x1d` exit is known to reach the first in the shipped game. A port should zero-fill and guard all three.
73. **Callbacks lost on the enter route** (§10.4.3, §10.9):
    - the cancel never calls the completion callback `+0x3c`;
    - the cancel-and-resubmit `0x00afe980` drops both `+0x34` and `+0x3c`.

    So a caller that waits on `+0x3c` is never told of a cancel, nor of a completion reached through a resubmission. Which request builders set `+0x34`/`+0x3c`, and whether any shipped caller relies on them (rather than polling, as `vehicle_enter_check_done` does, `spec-lua-api-behaviour.md` §22.3), was not traced. *Next step:* survey the request builders among the submitter's 25 callers for stores at request `+0x34`/`+0x3c`.
74. **Entry-point queue order** (§10.1): `0x00b0cb20` sorts by squared distance from `0x029cdb98`, a bss vector with no store in `.text` and 555 readers (HIGH: the shared zero vector). So the order is distance from the world origin. Defect HYPOTHESIS: the entry point was meant. *Next step:* read `0x029cdb98` from a running process.
75. **Request kinds** (§10.10):
    - which callers of the wrapper `0x00b10900` pass options modes 4 and 5 (kinds 1 and 0);
    - which other builders leave `+0x08` at −1;
    - the names of the kinds ("remove the occupant, then stop", "approach only" are HYPOTHESIS).

    The submitter's own kind-1 case (request `+0x22` bit 3 with an occupied seat) is CONFIRMED.
76. **The human-side approach** (§10.6), all to be named by the human/AI units:
    - the navigator `0x00993e90` (path planner `0x00993630`) and its helpers `0x00993d30`/`0x00993d50`/`0x009947b0`;
    - `+0xcce` values `0xd`/`0xe`;
    - human `+0x1714` bit 4, `+0x2bb` bit `0x10`, `+0xc36` bit 2 and the point `+0xc40`;
    - player `+0x28ac` bit `0x02`, which disables state 5's stick abort.
77. **Callees of the enter route read for role only:**
    - `0x00b11520` (vehicle `+0xc40` object);
    - `0x004b16c0` (controller `+0x34`);
    - `0x00545010` (request `+0x22` bit `0x20`);
    - the vehicle-AI reactions `0x004e6600`, `0x00b1f6c0`, `0x00b50180`, `0x00b464c0`, `0x00b500f0`;
    - the carjack report `0x009dd390`/`0x00b33dc0`;
    - `0x009b92e0`'s block `+0x1790`…`+0x17d4`;
    - `0x0094fa00`, `0x007e2b30`, the tilt fix `0x00a87880`, the condition-row-`0xa` classify `0x005ab240`;
    - the animation ids `0x2fb`/`0x2fc`/`0x2fd`/`0x1d8f` (state 0), `0x305`/`0x325` (hub), `0x327` (stop), `0x94`, `0x1d4` (`ga_cable`);
    - the exact semantics of `0x00957c80`/`0x00957ee0` (end-all and end-one HYPOTHESIS).
78. **Door and seat-record timers** (§10.3 step 17, §10.4.4, §10.9):
    - `0x00aa1eb0`'s arguments: the hub drives the door to 0.1 (unlatch HYPOTHESIS), `0x0f` to 1.0;
    - descriptor `+0x874` bit 0, `+0x870` bit `0x40000`, and vehicle `+0x16c8` bit `0x4000` (open-top HYPOTHESIS);
    - the seat-record timers: `+0x20` is armed by the cancel and disarmed by `0x0f`, `+0x24` is armed by the commit.

    A cancel does not close a door the hub unlatched.
79. **The byte `0x02845b1d`** (read by state 5's on-entry and by `0x00b02460`; no store in `.text`): when set, it forces the convertible-style choice that is otherwise a coin flip. HYPOTHESIS: a debug toggle, always 0 in the shipped game. **[Advanced by §11.7.4: `0x00b02460` is shared with this unit's `0x1f` kind chooser (`0x00b04990`/`0x00b04ae0` both call it); the global still has no writer. The function's own door-clear-latch bit turned out to be the same storage as the `convertible` exit variant (U6.1 §9.3's `+0xd1` bit 0) — a second, unresolved conflict alongside this one.]**
80. **The exit-request `+0x31` bit 0 conflict** (§11.7.4, §11.8): `0x00b02460` reads it as a one-shot "door already cleared" latch; U6.1 §9.3 reads the same bit (slot-relative `+0xd1`) as the `convertible` exit-variant flag. No writer was found for either reading in any landed unit. *Next step:* a displacement scan for stores to exit-request `+0x31` restricted to the vehicle-interaction band, or a live trace of a convertible's exit.
81. **`0x00b04ae0`'s kind-`0x14`/`0x1a` gap** (§11.5.2): no branch writes kind `0x14` (`exit standard`) — HIGH, the implicit default already present when `0x1f` is entered — and no branch writes kind `0x1a` (`exit climb`), though the per-frame handler `0x00b063c0` has a dedicated tail for it (§11.5.3). *Next step:* a register-level trace of every write to slot `+0x10` across the whole vehicle-interaction band, not only this unit's own roots.
82. **`0x00b063c0`'s nine-entry kind tail, seven of nine not individually matched** (§11.5.3): the table dispatches on kind − `0x14` at slot `+0x18`'s jump, and the shapes are CONFIRMED as a set (wait-for-half, re-validate-and-finalise, fast-forward, eject), but only `exit climb` (index 6) and `exit eject` (index 7, by cross-reference with the direct kind check) were pinned to a specific index. *Next step:* read the jump table's nine raw dwords at the computed address and match each to its target label.
83. **`0x1d`'s per-frame has no explicit transition for a non-`kill`, instantiated extract** (§11.4.2): the frame that confirms the clip finished hands off to a deep human-runtime contract (`0x0096fa20` or `0x009a48e0`) and returns without calling set-exit or the finaliser directly. *Next step:* read `0x009a48e0`/`0x0096fa20` for a call back into the vehicle-interaction band, or trace a live extract to see which frame actually releases the slot.
84. **Guard quirks found in this unit** (behaviour CONFIRMED): the eject handler `0x00b02cd0` writes a literal string's bytes into human `+0x1a00`/`+0x1a04` when no human resolves (an edge case that should not occur, since the function is always called with the exiting human); `0x00b02460` and its callers pass seat/handle values without a range check, consistent with the rest of this file's guard-quirk items. A port should guard both.
85. **The "`Jump_Rope`" special case in `0x00b063c0`** (§11.5.3): a tag-matched literal string gates an extra per-rig animation-parameter push (`0x008ff850`, `0x009215b0`, `0x008fea50`) whose purpose (HIGH: swinging past a mounted jump-rope prop) was not confirmed against any `vi_exit.xtbl` set name. *Next step:* search the shipped tables for a set or prop referencing `Jump_Rope`.
86. **The camera effect `0x00b063c0` primes when the exit-request object handle resolves to an object whose own `+0x10` field is `0x18`** (§11.5.3): the effect itself (`0x004c0690`, `0x00b0efe0`, `0x00574c50`, sized from animation `1d95`'s duration) was not named, and neither was the object's own `+0x10` field or what kind of object it is. *Next step:* U6.5/U6.8, or a direct read of `0x00574c50`.
87. **`0x00af6310`'s shared pose/orientation tail (`~0x00af6d7a`–`~0x00af86a5`, roughly 500
   instructions) was not traced byte-for-byte.** Call targets and ordering are confirmed
   (`0x00b0efe0`, `0x00569920`, `0x00ab4d20`, `0x00da38e0`, `0x004d6210`-style vector transforms,
   ending in the character-proxy pair `0x0079efb0`/`0x0079efe0`), and the role (blend/apply the
   case-specific attachment pose or orientation, then write it back to the Havok character proxy)
   is HIGH confidence, but the SSE position/orientation math itself was not re-derived. *Next
   step:* a dedicated pass with a proper per-register symbolic tracker (this unit's `esp`-delta
   tracker is not enough once SSE registers are involved).
88. **`0x00af6310`'s enter-state handler `0x00af6ba7` (state `0xf`) reads a seat-descriptor bit
   whose meaning was not independently named.** The addressing (`human +0xbf4 → +0xa7c`, then a
   computed offset `+0x4d`-ish, bit `0x20`) matches the `Vehicle_Interaction_Info` seat-table shape
   `spec-player-runtime.md` §8.1 already uses for the unrelated `Hide Occupant` flag (seat byte
   `+0x09` bit 6), but this is a *different* byte/bit and was not cross-checked against
   `spec-tables-vehicle-world.md`'s seat field list. *Next step:* `spec-tables-vehicle-world.md`
   §3.x's seat-descriptor layout, read against this offset.
89. **`0x00af6310`'s role-only sub-calls, not independently named:** `0x004d47e0` (a
   controller-state predicate, called from the shared "nothing case-specific" stub `0x00af6c00`),
   `0x00ab4d20` (a 4-argument seat/vehicle vector query, args include the human's own 8-byte
   handle), `0x00569920` (a bool-returning call in the ride tail, args a small local buffer),
   `0x004add90` (the vehicle sub-object orientation/quaternion getter at `vehicle+0x1db8`, used by
   `0x00af4b80`'s tilt computation too — see item 6), `0x00ab4790`/`0x00ab47e0` (the wheelie/endo
   predicates `0x00af4b80` calls directly on the vehicle). None of these change the parameter
   contract, but a full port would need their return semantics.
90. **`0x00af4150`'s human field `+0x16d4` values 1 and 2 are not identified.** `3` is documented
   (`spec-player-runtime.md` §8.0/§8.1, "seated"); `0x00af4150` treats 1 and 2 as valid states for
   a shorter variant of the same fall-detection math, but what those two states *are* (pre-seated
   transition? a different seat kind? a ragdoll-adjacent state?) was not traced. *Next step:*
   a raw scan for every writer of human `+0x16d4` (distinct from the better-documented `+0xcd2`
   physics-representation byte and `+0xccd`/`+0xcce` condition bytes).
91. **`0x00af4150`'s class-row test at object `+6` bit `0x80` is a different bit than the walkers'
   `+0xa` bit `0x01` test**, and was not independently named. *Next step:* a census of every
   reader of `0x02cc9900`'s per-class-row byte `+6`, cross-checked against `spec-ai-runtime.md`'s
   class-row documentation if any exists there.
92. **`0x00af4150`'s and `0x00af4760`'s shared call `0x00b0efe0(human, &out1, &out2)`** returns
   `-1` on failure and otherwise a value whose meaning was not traced (both callers only branch on
   `== -1` vs not). Given both callers are rider-adjacent and both then do vehicle-relative
   position math on success, this is HIGH confidence as "a ride attach-point or placement lookup",
   but it was not read beyond its entry and return convention. *Next step:* full listing of
   `0x00b0efe0`.
93. **`0x00af4760`'s non-ride-key return codes** (`2`, `0x1a`, `0x1ac`, `0x2c`, and the cached
   human fields `+0x1cc8`/`+0x1ce0`) were read as literal return values but not matched against any
   known animation-id or state-id space; they are too large to be slot states (max `0x22`) and too
   small to be the "animation ids `0x10a`…`0x111`" fallback chain scoping's OPEN 71 already
   mentions. *Next step:* read the caller `0x0095cbc0` in full to see how it consumes each code —
   this would also settle what `0x0095cbc0` itself is (this unit only confirmed it is a real
   function entry taking a human-shaped first argument and reading controller state id `0x174`).
94. **`0x00af4760`'s side branch at state `0x1f`/variant `0x14`'s call `0x004b1630(controller,
   0x45)`** is not one of the three typed setters and was not independently identified. HIGH
   confidence it is a `play`-family call (`spec-anim-runtime.md` §6.5) rather than a parameter
   write, triggering a one-shot cue keyed by id `0x45`, gated on the rider holding an item. *Next
   step:* cross-check `0x004b1630` against the play-family addresses `spec-anim-runtime.md` §6.5
   already lists, to see if it is one of those under a different citation or a genuinely new entry
   point into the same slot machinery.
95. **The shipped values of the three tweaks this unit newly named** (`VI_forward_diff_max`
   `0x02845b18`, `VI_vert_diff_max` `0x02845b14`, `VI_tilt_rvec_pitch_max` `0x02845b10`) were not
   located, same as the original six `VI_*` ride tweaks (`spec-player-runtime.md` OPEN 63). All
   nine are bss with no initialiser in the image. *Next step*, same as OPEN 63: find the tweak
   loader for registrar `0x0071b580`, or read the values from a running process.
96. **A fourth vector-shaped tweak, `0x02845b60`**, read by `0x00af4150` (scaled against an
    extrapolated launch offset before calling `0x00781 8f0`) but not traced to a registration stub
    the way the scalar tweaks were (it is read as a 16-byte `movaps` constant, consistent with a
    3-or-4-float vector tweak, but the registrar search in this pass only covered the three scalar
    addresses it was specifically looking for). *Next step:* the same registrar-stub search
    (`0x0100a840`–`0x0100ab00` and beyond) for `0x02845b60`.
97. **What sets bit 15 (and the other 21 bits) of the player's per-frame context word `+0x2084`** — the condition that makes the vehicle-enter/exit prompt (table `0x0111f2d0` slot 15, `0x009e5e80`) and its 21 sibling prompts available at all. (§13.1)

    **PARKED 2026-10-10, on the project's live-capture list, after 5 independent static-analysis passes.** Chronological summary (full detail in each pass's own scratch report, cited below):
    - **Pass 1-2:** confirmed the "trigger button" family's reader functions (`0x005b6540`/`0x005b6870`/`0x005b68e0`/`0x005b6950`) and two sibling 22-slot prompt tables (`0x0111f488`, `0x0111f4e0`) only ever `TEST` `+0x2084`, never write it.
    - **Pass 3-4:** the long-presumed setter location, the 603-instruction interaction-prompt routine `0x005b6af0` itself (called from `0x005b73c0` at `0x005b7412`), was confirmed via full-disassembly text search to contain **zero direct references to `+0x2084` in its own body** — contradicting this item's own original framing. Its ~40 callees were enumerated but not traced.
    - **Pass 5 (2026-10-10, FINAL per orchestrator instruction):** checked all 26 of those not-yet-characterized callees (`0x005bc5d0`, `0x005bd7b0`, `0x005bde80`, `0x006040a0`, `0x006041a0`, `0x006043b0`, `0x006a3550`, `0x006a3590`, `0x006a35d0`, `0x006cf0a0`, `0x007044f0`, `0x0075d7e0`, `0x007ff1f0`, `0x007ff3e0`, `0x007ff430`, `0x00941f40`, `0x00950b70`, `0x00953580`, `0x00957c80`, `0x009bb3d0`, `0x009e2bc0`, `0x009e7810`, `0x00d9e140`, `0x00d9e1e0`, `0x00d9e4c0`, `0x00e22a30`) for a direct write to `+0x2084`, via full-disassembly text search for the literal offset, plus one level deeper for every thin (1-4 callee) dispatcher among them (spot-checked independently on 2 of the 26, `0x005bd7b0`/`0x006a3550`, both clean). **CONFIRMED (negative result): none of the 26 write `+0x2084`.** They resolve to trivial global getters, generic game-clock-tick wraparound math, a "currently active slot" vtable-dispatch cluster, fixed-size handle-indexed slot-table lookups, a UTF-16 string encoder, and several large stack-cookie-guarded state-machine functions — none touching the offset. The one loose thread: `0x00950b70`'s own 10 callees (`0x008addb0`, `0x00d9e180`, `0x009561f0`, `0x004b41b0`, `0x00853b30`, `0x004f13a0`, `0x00986920`, `0x004e57c0`, `0x008bb1e0`, `0x008c4d10`) were not individually traced — left open below, not pursued further per the orchestrator's "no 6th pass" instruction.
    - **Conclusion:** `0x005b6af0`'s call graph is now exhausted to a depth of 2 with uniformly negative results. The setter is most likely reached via a call path this function's own graph never touches — e.g. the player-object construction/allocation site, or a different per-frame driver entirely. **PARKED on the live-capture list** (alongside store-open/phone-open/ground-draw/hitscan-damage/`dlc2_m01`+0x68/diversion-rewards) rather than pursuing a 6th static pass. If a live capture becomes available, a targeted write-breakpoint on `+0x2084` (relative to the resolved player-object pointer) would resolve this in one session.
    - Full detail: `scratchpad/watercraft_prompt_table_open97_2026-10-09.md`, `scratchpad/fourth_prompt_table_0x0111f4e0_2026-10-09.md`, pass 5's own report folded into this entry (no separate scratch file).
98. **Player fields `+0x16d8`/`+0x16dc`**, tested at the very top of `0x009e5e80`: non-zero on either makes the function refuse immediately, before even the seated/not-seated check. Not the human's own vehicle handle (`+0x16c0`/`+0x16c4`) and not named by `spec-player-runtime.md` §1.5. HYPOTHESIS: some other in-progress-interaction/busy flag. (§13.1)
99. **Exit-request field `+0x30` bit `0x80`**, set by `0x009e5e80`'s seated branch before calling `0x00af3cd0`. Not in `spec-player-runtime.md` §6.2's `0x33`-byte exit-request field table. (§13.1)
100. **Player fields `+0x1e30`/`+0x1e34`** — a handle pair read only by `0x00b105c0`, used as a "sticky"/previously-tagged preferred-vehicle override in the nearest-vehicle scan. Not in `spec-player-runtime.md` §1.5's player-only field list (nor the original scoping note's). (§13.3)
101. **The object-manager-style global at `0x003171a64`**, walked by `0x00b105c0` to enumerate candidate vehicles. Not named by any spec read in this pass; likely the same kind of manager `spec-player-runtime.md` §1.5 touches for the player list (`+0x1f0`), but that attribution was not checked here.
102. **`0x00b0eac0(vehicle, seat, −1, 0, 0, &out1, &out2)`**, called at the tail of `0x00b0eb90`'s per-seat success case. Its outputs were not traced; given `0x00b0eb90` has no request/slot pointer at all, it is unclear whether this call persists anything or is itself just another validator. (§13.5)
103. **`0x005457f0`**, the third caller of `0x00b0eb90` per `spec-player-runtime.md` §9.5's call-site table. Not traced by this pass either; band/owner unclear.
104. **`0x0075d7e0` and `0x0075c880`**, the query helpers `0x00b0da10`/`0x00b0dc20` call with a computed position and a small integer constant (`0x6d`, `0x00ac4c30`). Read as a short-range obstruction/clearance query by shape (SEH-wrapped, scans a short list for a flag bit first) but not traced; likely physics-runtime territory rather than vehicle-interaction. (§13.6)
105. **Where in the AI mode-38 `Veh enter` machinery `0x0051d5b0` and `0x00521200` (both confirmed callers of `0x00b0fdf0`) are themselves called from.** Neither is one of the named mode-38 table slots in `spec-ai-runtime.md` (`0x00521320`/`0x00520cd0`/`0x005213d0`/`0x005215a0`/`0x00520ca0`/`0x00521060`/`0x005210d0`/`0x00521f40`). AI-side work. (§13.7)
106. **The session/mode-kind getter `0x00614d00` → `0x006d2910`**, called by both `0x009e1e60` (checked against `4`) and `0x00b0eb90` (checked against `6`). HYPOTHESIS it backs `coop_is_active`'s session predicate (`spec-lua-api-behaviour.md` §3.1) or a similar mode/session enum, but the enum's actual value set was not read. (§13.2, §13.5)
107. **The local-player-only `cant_enter_exit_vehicles` read inside `0x00b0fdf0`'s common success tail** (`+0x28ac` bit `0x10`, read off the **local player singleton**, not the requesting character) gating a driver-eviction path. Whether this is intentional (only the local player's own preference should block evicting someone to make room) or an oversight that happens to not matter for AI-only entries was not verified. (§13.4 step 8)
108. **`0x13`'s per-frame `0x00b01630`'s door-opening tail not fully traced.** Confirmed for its dominant control flow (the `+0x28` timeout-based hard cancel, the knock-down/impulse retry loop, and the direct-to-commit inline call to `0x00afbd90` once the destination seat and its queue are both clear, §14.10). A fourth branch, a "surface kind" (`0`/`1`/`2`) dispatch over door-opening variants reached past roughly `0x00b01c1f`, was read partway (through `0x00b01f22`) but not walked to its end — at least one more block (`0x00b01f0a`) was identified but not disassembled. Not expected to change the headline findings. (§14.10, U6.5)
109. **The chooser `0x00afb220`'s third argument is not always literal `0`.** Every previously-documented call site (§7.5, §9.1, §9.5) passes `0`; state `0x0c`'s per-frame (§14.4) passes a computed, non-literal value at its one call site instead. The signature should read `(record, i, flag)`; what *flag* selects, and whether `0x0c`'s is the only non-literal site, was not traced. (§14.4/§14.13 item 2, U6.5)
110. **Eight per-state cached animation-tag hashes, unnamed**: `0x153bd9c` (`0x0a`'s per-frame), `0x153bdec` (shared by `0x0b`/`0x0c`/`0x0e`'s per-frame), `0x153be28`/`0x153be24`/`0x153bdf0`/`0x153be20`/`0x153be1c`/`0x153bd98` (all `0x0d`'s per-frame, five distinct gates plus one via a different query function `0x004b4320`). Same lazy-cache-by-name idiom as U6.7's "critical end" tag (`0x0153be2c`) and U6.8's eleven named parameters (§12.4); the string literals behind these eight were not traced — a bounded, mechanical follow-up. (§14.13 item 3, U6.5)
111. **Enter-slot `+0x18 = 4` on state `0x13`'s on-entry: no consumer found.** `0x0a`'s and `0x0c`'s on-entry both seed `+0x18` to a sub-timer their own per-frame advances through further stages; `0x13`'s on-entry seeds `4`, but no read of `+0x18` at value `4` was found anywhere in `0x13`'s per-frame, including the untraced tail (item 108 may resolve this once closed — also worth checking against U6.8's per-slot pose driver §12.2/§12.3, which has its own per-enter-state dispatch and could read `+0x18` for an unrelated purpose). (§14.11, U6.5)
112. **A handful of role-only helper calls left unnamed**, none found to drive a state transition or write a field this unit's scope depends on: `0x0b`'s on-entry door-handling block's exact stack-operand identities (bit-level outcome on request `+0x21` bits 2/3 is confirmed); `0x0c`'s on-entry `0x0055b1d0`; `0x0d`'s on-entry `0xaf1b80` (feeds human `+0xec` bit `0x100000`) and per-frame `0x009d14f0`/`0x0070bcb0`/`0x004ddc20`/`0x0070ced0`/`0x0070dcf0`/`0x0055a0a0`/`0x0055a190`/`0x00aa1f20`; `0x0e`'s per-frame `0x004de260`/`0x004fa060`/`0x00b61cc0`/`0x00b46ca0` (filling in branches of the hub's vehicle-AI-reaction logic, §10.3 step 16); `0x13`'s on-entry `0x00950550`/`0x0051b5b0`/`0x006bd720`/`0x00710e40` and per-frame `0x00afa320`. (§14.13 item 5, U6.5)
113. **Human `+0x21` bit 1: role not independently named.** Set unconditionally by `0x0b`'s on-entry during its door-handling block (§14.3). HYPOTHESIS: an "is traversing the vehicle exterior" or ragdoll-disable-style flag, by analogy with the state's purpose; not confirmed. (§14.11, U6.5)

---

## 16. U7 — In-vehicle driving controls, the input→Havok hand-off (2026-10-08)

**Scope.** Closes the unreserved unit "U7" `review/scoping-player.md` scoped but left untraced
("the hand-off into `hk_custom_vehicle_driver_input` `0x00ae30c0` was not traced"). Entry (§10, U6.3)
and exit (§11.4, U6.7) were already essentially DONE in this file — this unit is the one genuine gap
the brief found: the driving-control input→Havok hand-off once the player is seated, not entry/exit
itself.

**Full chain, CONFIRMED by direct disassembly:** per-frame controls (`0x00702ad4`→`0x005aa6c0`→
`0x005b7430`→`0x005b73c0`) → in-vehicle action dispatcher `0x005b5c50` (586 instructions) →
driving-input readers `0x00a91800` (ground vehicles) / `0x00a91440` (motorcycles) → two input-shaping
setters, `0x00ae5290` (deadzone/response curve, writes `instance+0x130`) and `0x00ae5420` (writes
`instance+0x13c`) — both keyed off the vehicle instance via `*(vehicle+0x808)`, the same instance this
file's own §12 (U6.8) and `spec-physics-runtime.md` already track throughout. The already-documented
Havok override `0x00ae30c0` reads `instance+0x130`+`0x138` (summed, sign-squared); this file's own
§12.4 "Vehicle Steering" animation-controller parameter (`0x00a926b0`'s return) reads the SAME
`instance+0x130` — three independent confirmations (write-side provenance, a fresh read of
`0x00a926b0`, and this file's own already-published §12.4) agree `instance+0x130`/`0x138` is the real
steering channel.

**Correction to the original scoping note:** `CAA_DRIVE_STEER` (axis 9) is confirmed read at
`0x005bb530(9,0)` but is provably *unused* for ordinary cars (kind 0/1), which instead reuse the
on-foot walk-turn axis — traced to the end of the switch, not just the entry.

Full detail, including the AI-side lead (`0x00abde60`/`0x00b29580` both call the same two setters —
unverified, relevant to `spec-ai-runtime.md`'s own traffic driving material):
`spec-physics-runtime.md` §10.9 (primary splice) and
`scratchpad/vehicle_entry_exit_control_handoff_2026-10-08.md`.

**Advanced 2026-10-10.** `instance+0x164` bits `0x1` and `0x2` are now CONFIRMED by listing: bit
`0x1` is a master lock shared by the steering setter `0x00ae5290` and the `+0x13c` setter
`0x00ae5420` — when set, both force their channel to neutral (`+0x13c` = 0.0, `+0x130`'s input
zeroed before shaping). Bit `0x2` is read only by the steering setter and skips its
deadzone/response-curve shaping (the `[+0x20]+0x8`/`+0xc` breakpoint/gain pair), using the raw
clamped stick value directly — a steering-only "raw/linear" mode, plausibly for AI- or
script-driven vehicles. The HYPOTHESIS that `0x00ae2c80` consumes `instance+0x13c` as a braking
function is NOT CONFIRMED and is contradicted by its own listing: `0x00ae2c80` is reached only
through a function-pointer slot (no direct caller recoverable by reference scan) and operates on a
per-wheel array object with its own, differently-numbered fields (notably its own `+0x164` tests
bit `0x4`, not the driving instance's bits 0/1) — consistent with a Havok tire-friction/slip
callback on a sibling wheel-collection object, not a reader of the steering instance's own
`+0x13c`. No reference to `+0x128`, `+0x12c`, or `+0x138` was found anywhere in either
driving-input reader (`0x00a91800` ground vehicles, `0x00a91440` motorcycles, both read in full) —
their writer(s) remain unlocated. One new lead surfaced in the process: for some vehicle kinds
`0x00a91800` routes its steering/braking-style inputs to a second, separate Havok sub-object at
`*(vehicle+0x818)` via sibling setters `0x00aed340`/`0x00aed390`, distinct from the
`*(vehicle+0x808)` instance this file tracks — unexamined, and a natural next step alongside a
direct whole-image displacement scan for `+0x128`/`+0x12c`/`+0x138` against the `+0x808` base
specifically. Remaining genuinely OPEN: `instance+0x128`/`0x12c`/`0x138`'s own writers; the true
consumer of `instance+0x13c`.

---

## ~~4.~~ ~~5.~~ ~~6.~~ ~~7.~~ ~~8.~~ ~~9.~~ ~~10.~~ ~~11.~~ ~~12.~~ ~~13.~~ ~~14.~~ ~~15.~~ 16. 17. Clean-room check

- Addresses and offsets are plain hex throughout. No decompiler output was read or quoted; only capstone listings and static table reads were used. No Ghidra auto-names for functions, globals, labels or variables appear.
- Names quoted are engine data: CBA/CAA action names, the stick/axis/mouse/gamepad vocabularies and scheme names read from the executable's own tables, the tweakable names `grenade_disable`/`grenade_disable_veh` read at `0x0111f708`/`0x0111f6f4`, and Win32 import names from the PE import table.
- U2 (§3) additionally quotes, as data read from the executable: tweak-variable names (`Player_max_run_speed`, `Player_acceleration`, `Crouch_click_sensitivity`, …) from the strings their registration calls push beside each global; `drunk_levels.xtbl` and `SprintEntry` field names; HUD string ids; the mission event names `m16_invert_stop`/`m16_boss_invert_stop`; and AI sub-state names from the table at `0x012e1168`. U2 was also Ghidra-free (capstone listings, a linear sweep of `.text`, raw rel32/dword scans).
- U4 (§4) additionally quotes, as data read from the executable or from the specs cited: the 61 camera submode names of the table at `0x012e3d48` and the `camera_free.xtbl` / `vehicle_cameras.xtbl` element names (as `spec-tables-environment.md` §12.2 and `spec-tables-ui-controls.md` §8.1 give them), the literals `sp_mdsphere_genki` (`0x0111bde8`) and `MISSION_FAILURE_DIED_TEXT` (`0x0113adfc`), and Lua function names already in `spec-lua-api-behaviour.md`. U4 was Ghidra-free (capstone listings, a linear sweep of `.text` for absolute and displacement references, raw rel32/dword scans of the whole image, push surveys); no Ghidra project was opened or copied, so none needed deleting. **U4 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U4 edit: regex 0 hits, backstop 0 hits** (control string 5/5 on both).
- U3 (§5) additionally quotes, as data read from the executable: the RTTI class names `character_controller`, `hk_custom_character_proxy`, `hk_character_shape_phantom` and `MyCharacterListener` (as `spec-physics-runtime.md` §2.1 gives them), Havok 2010.2's public `hkpCharacterProxyListener` method names for the listener slots (the convention `spec-physics-runtime.md` §2.2 uses), the literals `sp_pony_cart01` (`0x011743b4`), `Shark` (`0x0112e1fc`), `Diffuse_Color_a`/`_b`/`_c`, `Hair` and `Sphere_Map`, and the action name `CBA_OFC_SPRINT`. U3 was Ghidra-free (capstone listings, a linear sweep of `.text` for displacement scans, raw rel32/dword scans of the whole image, an RTTI walk); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched.
- U6.6 (§6) additionally quotes, as data read from the executable or from the specs cited: the trace strings `Set Exit`/`Set Enter`, the force-flag record tag `human_force_flagsbeing_vacuumed` (`0x01167ea8`) and the tag `human`, the weapon names `Rifle-NG`, `Rifle-STAG`, `Rifle-Gang` and `SMG-Gang`, the `vehicle_interaction_info.xtbl` flag name `Equip Rifle on Exit` (loader string `0x0118640c`), the 83-entry key-token names (`none`, `exit`, `extract`, `change seat`, `traverse`, `deactivate`, `open`, `close`, `prepare`, `fast`, `enter`) of the table `0x0130e638`, caller-classifying literals (`ACT_TORCH_FAIL_WATER`, `ACT_TRAFFIC_*`, `ACT_ESCORT_*`, `DIVERSION_CHOP_SHOP_*`, `fail_hos_all_died`, `STORE_AMBULANCE_MSG`, `mission_purchase`, `Homie force to exit vehicle!`, `-- Cutscene Script Group --`, `Shark`, `Brute Fuel Tank`), force-flag names (`cannot_exit_vehicles`, `cannot_exit_rc_vehicle`) and Lua function names as `spec-lua-api-behaviour.md` gives them. U6.6 was Ghidra-free (capstone recursive-descent and linear listings over `exe_va.py`, raw `E8`/`E9` rel32 and dword scans of the whole image, a push-corrected request-field survey of all 79 exit-request call sites, census call-graph closures, static table reads of `0x00b05ac4`, `0x00b06fdc`, `0x00b0abe4` and `0x0130e638`); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched; the only data file read was the extracted `vi_exit.xtbl`/`vi_enter.xtbl` under `tools/as_base_xtbl`. **U6.6 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.6 edit: regex 0 hits, backstop 0 hits** (control string 5/5 on both).
- U6.0 and U6.4 (§7, §8) additionally quote, as data read from the executable or from the specs cited: the log strings `Set Enter`, `Set Exit`, `Set Ride`; the animation-parameter names `Collision Bool`/`Collision Dir`; the tweak names `VI_forward_vel_accel`, `VI_vert_vel_accel`, `VI_random_accel_factor`, `VI_reaction_delay_min_ms`, `VI_reaction_delay_max_ms`, `VI_collision_min_vel_diff` (read at their registration stubs `0x0100aa50`…`0x0100aaff`); the `vehicle_ai`/`vehicle` force-flag strings (`drunk_driver`, `degrade_info`, `goal_stop_override`, `override_speed`, `never_go_ambient`, `infinite_vehicle_collision_mass`, `cheat_no_damage`, `cheat_smash`); `hud_inventory_hide`, `player_last_vehicle_handle`, `human_ai_data`; the model names `sp_mdsphere_genki`/`truck_2dr_classic01`; the camera submode names of §4.8; the `vehicle_cameras.xtbl` set names of `spec-tables-ui-controls.md` §8.1; and the `vehicle_interaction_info.xtbl` field and flag names of `spec-tables-vehicle-world.md` §3.2 (`Queue`, `Primary_Access_Seat`, `Secondary_Access_Seat`, `Hide Occupant`, `Ignore Team Disposition`, `Hold Weapon in Left Hand`). U6.0/U6.4 were Ghidra-free (capstone listings with an esp-depth tracker, census call edges, raw rel32/dword scans of the whole image, a band displacement sweep, push surveys including shared call tails); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.0/U6.4 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.0/U6.4 edit: regex 0 hits, backstop 0 hits** (control string 5/5 on both).
- U6.1 (§9) additionally quotes, as data read from the executable or from the specs cited: all 83 key-token names of the table `0x0130e638` (`none`, `any`, `prepare lift`, `extract fail 1`, `exit at speed cruise`, `front left`, `hostage a`, `combat ready`, `alternate`, …) and the six strings that follow it (`request pending`, `start`, `wait for exit exterior`, `wait for exit interior`, `teleport`, `pathfind`); the diagnostic format string `Couldn't find animation for %s -> %s -> %s -> %s -> %s` (`0x01186848`); the loader strings `Vehicle_Interaction_Animation_Set`, `Animation_Grid`, `Element`, `Animation`, `Parameter%d`, `Animated_Camera_Tests`, `Camera_Pos`; the `vehicle_interaction_info.xtbl` field names `Enter_Animations`, `Exit_Animations`, `Ride_Animations` as `spec-tables-vehicle-world.md` §3.2 gives them; and, from the shipped `vi_enter.xtbl`/`vi_exit.xtbl`/`vi_ride.xtbl` (extracted copies under `tools/as_base_xtbl`), the set names `Bike Rocket Rider`, `Bulldozer Right`, `BRUTE`, `Heli Fighter Rider`, `RiotCop_Right`, `medium passenger right`, `VTOL2 …`, `motorcycle driver`, `JetBike driver` and animation names (`Exit Right Rider`, `Exit Right Fast`, `Exit Left Fast`, `Exit Right Extracted Pull`/`Bash`/`Shoot`/`Punch`, `Drive Ready SM`, …). U6.1 was Ghidra-free (capstone listings over `exe_va.py`, raw `E8`/`E9` rel32 and whole-image dword scans, census closures of every state handler, static table reads of `0x00b0abe4`, `0x00b0ac44`, `0x00b0a554`, `0x00b0a628` and `0x0130e638`, and a re-implementation of the key builder run over the three shipped tables); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.1 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.1 edit: see the Changelog entry.**
- U6.3 (§10) additionally quotes, as data read from the executable or from the specs cited: the trace string `Set Enter`; the tweak names `VI_pathfind_cancel_dist_multiplier`, `VI_pathfind_check_pause_distance`, `VI_pathfind_play_stop_dist` and `VI_on_vehicle_entry_max_y_diff` (read at their shipped values in `tools/ai_base_xtbl/tweak_table.xtbl`, registrars not re-traced); the `Interaction_Point_Type` names (`Enter Start`, `Enter Start Lift`, `Enter Start Pull`, `Enter Start Extract`, and their `Convertible` variants) as `spec-tables-vehicle-world.md` §4.1 gives them; and the `vehicle_interaction_info.xtbl` field names `Primary_Access_Seat`/`Secondary_Access_Seat`, `Ignore Distance Checks` and `Queue` (seat byte `+0x0A` bit 7, `Should Hide Big Hats`) as `spec-tables-vehicle-world.md` §3.2 gives them. U6.3 was Ghidra-free (capstone listings over `exe_va.py` with an esp-depth tracker, census call edges, raw `E8`/`E9` rel32 and whole-image dword scans for the instant flag and the request fields, a band displacement sweep of `0x00af0000`–`0x00b12000`, static reads of the condition table `0x0111f1e8` and the submitter wrapper's jump table `0x00b10a68`, and the shipped `tweak_table.xtbl`); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.3 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.3 edit: see the Changelog entry.**
- U6.7 (§11) additionally quotes, as data read from the executable or already identified by the unit's own prior investigation and re-confirmed here: the trace string `Set Exit` (already declared for U6.6/U6.0); the literal `Jump_Rope` (`0x0112b5e4`) gating a special-cased animation-parameter push; and the "critical end" animation tag cached at `0x00153be2c`. U6.7 was Ghidra-free (capstone listings over `exe_va.py`, a census closure of every in-band function in range, raw rel32/dword scans for jump-table and call-site confirmation); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.7 self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: see the Changelog entry.**
- U6.8 (§12) additionally quotes, as data read from the executable or from the specs cited: the 11 animation-controller parameter names `Vehicle Steering`/`Vehicle Steering Time`/`Vehicle MPH`/`Vehicle Forward Lean`/`Vehicle Vert Lean`/`Vehicle Tilt`/`Vehicle Forward Lean Abs`/`Vehicle Vert Lean Abs`/`Vehicle Tilt Abs`/`Wheelie Bool`/`Endo Bool`, read as string literals pushed before their `0x004cbaf0` resolver call; the tweak names `VI_forward_diff_max`, `VI_vert_diff_max`, `VI_tilt_rvec_pitch_max`, read at their registration stubs `0x0100a8c0`–`0x0100a900`; and the controller convention already established by prior sections (`spec-anim-runtime.md` §4.1/§4.2). U6.8 was Ghidra-free (capstone listings over `exe_va.py` with a purpose-built `esp`-delta tracker for stack-relative operands, raw dword/byte reads of the jump/remap tables and tweak registration stubs); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.8 self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: see the Changelog entry.**
- U6.2 (§13) additionally quotes, as data read from the executable or from the specs cited: the action name `CBA_GAC_ACTION` (`spec-tables-ui-controls.md` line 120) and `CBA_OFC_SPRINT`; Lua function names (`player_force_vehicle_seat`, `vehicle_enter_do`) as `spec-lua-api-behaviour.md` gives them; and a static read of the 22-entry prompt-handler table at `0x0111f2d0` and its sibling at `0x0111f430`. U6.2 was Ghidra-free (capstone listings over `tools/harnesses/exe_va.py` via a local `listfn.py` harness, manual esp-depth tracking, static table reads); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.2 self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: see the Changelog entry.**
- U6.5 (§14) additionally quotes, as data read from the executable or from the specs cited: nothing beyond plain hex addresses and offsets already established by this file's own prior sections (U6.0/U6.1/U6.3/U6.6/U6.7/U6.8) and `spec-player-runtime.md` §5.4.2/§12.5's knock-down/impulse mechanism, all cited not re-derived. U6.5 was Ghidra-free (a purpose-built linear Capstone lister over `tools/harnesses/exe_va.py`, validated against an already-published U6.7 function it reproduced byte-for-byte before being trusted on new code; manual `esp`-delta tracking); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. U6.5's own draft disclosed one self-caught and corrected error (a cross-contamination of three details from state `0x13`'s on-entry into state `0x0d`'s write-up, plus one arithmetic mis-statement in `0x0e`'s, both fixed and left visible in §14.5/§14.6/§14.10 rather than silently edited away) — flagged here so the integration is known to have received an already-self-corrected draft. **U6.5 self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: regex 0 hits, backstop 0 hits.**
- Self-check result: recorded in the Changelog entry below, run on the finished file as the last step.

---

## Changelog

- **2026-10-11 — the camera surface-clamp's `HYPOTHESIS: water surface` tag on `0x0079a420`
  RESOLVED: a generic global-box-gated downward raycast, not water-specific** (SPEC TEAM, self,
  direct query, no agent — orchestrator-routed follow-up on the parked ground-missing bug). Full
  detail in `spec-zone-data-format.md`'s own 2026-10-11 water-hypothesis Changelog entry — not
  reproduced here. Net for this section: the two citations of `0x0079a420` (§6.9-area body text,
  §6.9-area open-items list) updated in place to drop the HYPOTHESIS tag and point at the real
  mechanism. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — real-file-bytes occupancy check for the MOPP/mesh-blob fields, closing the one
  honest gap the pavement-collision finding left open, same day, no agent** (SPEC TEAM, self,
  direct extraction-pipeline reuse, routed from orchestrator `purrsian-f5`). Found the literal
  `"MOPP"` magic inside two real flat-pavement `0x2237` records (`sr3_city~s0916`, 484,684-byte
  record; `sr3_city~s1312`, 216,540-byte record, reproduced identically across its 4 mission-
  variant siblings), each with a substantial (82,974-237,503 byte) preceding length field — real
  populated data, not a near-empty stub. The much smaller `~al` sparse-tier variants of the same
  tiles carry no MOPP magic at all, a sensible negative control. Confirms the collision code path
  is genuinely exercised by real shipped pavement data, not merely theoretically reachable.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — ground-collision-shape source RESOLVED, closing the open item** (SPEC TEAM, agent
  `a73e5e44581178516`, independently re-verified by direct Ghidra query before splicing).
  `0x2237`'s own handler (`0x00864c60`) directly builds a Havok `hkpExtendedMeshShape`
  (vtables `0x01246834`/`0x01246814`, re-verified by direct decompile — the same vtables already
  RTTI-confirmed for `.clmesh_pc`'s own collision shape) wrapped in an `hkpMoppBvTreeShape`
  (`0x00d54300`, re-verified by direct xref to have exactly 4 callers project-wide) from
  vertex/index/AABB/MOPP-blob data embedded directly in the `0x2237` record, with no dependency on
  `.clmesh_pc` at all — then registers it through the identical generic physics-shape-attached
  wrapper (classId `0xc006c`, property id `7`, re-verified by direct decompile) that
  `spec-zone-data-format.md` §13.4 already documents for `0x2250`/Parking's own `hkpBoxShape`,
  via the same shared code, not merely an analogous pattern. One gap honestly left open: not
  verified against real file bytes that ordinary flat-slab pavement records (vs. some other
  `0x2237` sub-case) actually populate the MOPP/mesh-blob fields non-trivially — the code path is
  confirmed to exist and run when they are populated. Full detail in §5.6's own "character proxy"
  sub-passage, above. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — ground-collision-shape source narrowed, not closed; corrects a stale cross-ref in
  `spec-terrain-format.md`** (SPEC TEAM, agent `ab81ded1165afbe66`). Confirmed `.clmesh_pc`'s Havok
  MOPP-tree handoff (cited from `spec-physics-format.md` §4.4.6(g)) is wired into ordinary
  open-world zone streaming, not just interiors — but ruled out as the pavement-surface source
  itself (confirmed furniture/props only on real tiles). Confirmed the real visible street pavement
  is `.czn_pc`'s own `0x2237` section instead (cited from `spec-zone-data-format.md` §13.2,
  superseding a stale "decal overlay, not base ground" framing in `spec-terrain-format.md`, flagged
  there). Traced `0x2244` (Heightmap)'s own confirmed callers by direct xref/decompile — independently
  re-verified via the Ghidra query server — and ruled them out as a Havok collision feed (gameplay/
  camera consumers, not inside the character-proxy's own code range), though not exhaustively (15
  of 17 callers unopened). Net: the Havok shape backing `0x2237`'s own pavement geometry, if any,
  remains genuinely OPEN — narrower than before, with a concrete next step (an xref census on the
  MOPP broadphase-add call against the zone-instantiate dispatcher). Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — OPEN item #97, 5th and FINAL static pass: `+0x2084` setter PARKED on live-capture
  list** (SPEC TEAM, agent `a47b49a25aa1aa505`). Checked all 26 remaining uncharacterized callees of
  the presumed setter `0x005b6af0` (plus one level deeper for thin dispatchers) for a direct write
  to `+0x2084` — all clean, confirmed negative by full-disassembly text search (spot-checked
  independently on 2 of 26). Combined with 4 prior passes, this exhausts `0x005b6af0`'s call graph
  to depth 2 with a uniformly negative result; the setter is not reachable via this call path.
  Per orchestrator instruction, item #97 is now PARKED rather than pursuing a 6th pass — a
  write-breakpoint live capture on `+0x2084` is the recommended next step if one becomes available.
  One loose thread left unpursued: `0x00950b70`'s own 10 callees were not individually traced.

- **2026-10-10 — OPEN-backlog batch 5: camera mode 11, sway amplitudes, camera collision filter,
  movement flags `+0xd40`** (SPEC TEAM, agent `a9337200c21571e67`). OPEN 23 RESOLVED (camera mode
  11's re-aim/freeze and follow-camera-fallback logic fully traced; confirmed as an
  object-relative hold camera for turnaround/cutscene use, not a vehicle turret or sniper scope).
  OPEN 31 HYPOTHESIS REFUTED: sway amplitude is NOT driven by impacts/explosions — its only writers
  trace to the QTE execution path and the vehicle-entry/interaction state machine, through the same
  unidentified per-object ramp system OPEN 27 already flagged (now shown to be shared between the
  two items). OPEN 33 PARTIALLY RESOLVED: the camera collision filter's object-exclusion logic is
  fully tabulated (excludes the local player's own vehicle, a rider-flag combination, and any
  bounding-volume overlap with the player's vehicle) but the existing "kinds 1, 4, 8" label doesn't
  match any value actually tested in this function — flagged for re-pointing to
  `spec-physics-runtime.md` §8.8's own open per-candidate-kind helper, or retraction. OPEN 38
  RESOLVED: found the real per-frame writer for `+0xd40` and confirmed the already-documented bit
  meanings hold up, though each bit is a derived condition (heading-angle hysteresis, animation-state
  gating, a flag on the opposing aim target) rather than a raw pad-bit copy — cross-referenced into
  `spec-anim-runtime.md` §4.2. Full raw dumps: `scratchpad/rc1_*/`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — OPEN-backlog batch 4: look-speed/FOV factors, lateral-offset camera quirk, vehicle
  camera presets, remaining stance bits** (SPEC TEAM, agent `a408dcb9ae85f24bc`). OPEN 27: the
  look-speed curve system is structurally characterized (data-driven keyframe ramp, owning object
  unidentified) but the FOV multiplier and the secondary ×3.0 multiplier are both fully resolved
  and BOTH refute the "look-sensitivity option" hypothesis — the FOV factor is the drunk/weed-effect
  wobble, and the ×3.0 multiplier is the sleepy/fatigue-driving minigame's wake-up-gesture boost
  (confirmed via its own string literals). OPEN 29 RESOLVED: the lateral-offset re-add is NOT a
  drift/accumulation bug — the camera position is fully recomputed from scratch every frame, so the
  re-added value is read exactly once (by the vehicle fallback-distance ramp) before being
  discarded, producing at most a single-frame bounded bias, never compounding. OPEN 28 RESOLVED:
  the vehicle camera preset placer, both preset-selector gates, sub-state 2's placement handler, and
  both candidate fields are now confirmed by reading the accessor bodies directly — `+0x864` is
  CONFIRMED `Max_FOV` (upgraded from HYPOTHESIS), `+0x868` is CONFIRMED a per-vehicle
  follow-distance scale factor, both sharing the same custom-preset override gate as the
  angle-preset selector itself. OPEN 15 residual: `+0x1d95` bit `0x20` RESOLVED (fine-aim +
  scope-capable-weapon + toggle-armed flag); bit `0x08` advanced to HIGH CONFIDENCE (a
  position-snap/teleport-in-progress latch, containing function not fully bounded); `+0x1d97`'s
  four remaining bits stay genuinely OPEN — no candidate addresses exist in the spec at all, and
  resolving them needs the project's Ghidra-free displacement-sweep method, not attempted this
  pass. Full raw dumps: `scratchpad/item27_29_28_15/`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — new §7.4b: driving animation state, forward-traced from §7.4a** (SPEC TEAM, agent
  `a594c8196df8fb117`, WAVE 8 item 3). Mostly a synthesis pass over already-landed material
  (§§6-13, `spec-tables-vehicle-world.md` §2-3, `spec-tables-animation.md` §1.3/§5,
  `spec-anim-runtime.md` §7.11-7.13), plus new empirical confirmation via direct reads of the real
  shipped `vi_enter.xtbl`/`vi_ride.xtbl`/`vehicle_interaction_info.xtbl` text. Confirms: no separate
  animation-selection subsystem — it's the same 35-state vehicle-interaction machine's on-entry
  handlers calling the shared element-selection rule (§9); the seated pose is a dedicated locomotion
  condition (`human+0xcc8` = 8/9, same mechanism as every other named movement state), not a
  one-off override; clip names are real data-driven `.xtbl` entries resolved through the merged
  State/Action registry, not hardcoded ids; vehicle-class dependence is CONFIRMED pure data with
  zero code-level class switch — proved conclusively by real cross-reuse data (the shipped
  `Wavecraft` jet-ski's ride set is literally named `"motorcycle driver"`, and the shipped `Bike
  Moped`'s ride set is literally `"Wavecraft Driver"`). Biggest open item: the literal
  instruction-level link from an `Animation` registry string (e.g. `"drive network"`) to the real
  shipped `driving_sm` node graph is HIGH CONFIDENCE by naming/structural correlation only, not
  CONFIRMED by disassembly. Full detail: `scratchpad/driving_animation_state_2026-10-09.md`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — OPEN-backlog batch 1, highest moment-to-moment-gameplay items** (SPEC TEAM, agent
  `a9227feb164895d5d`). Four OPEN items closed or narrowed, picked for movement/combat/vehicle
  impact rather than naming completeness. OPEN 13: `0x009573d0`'s facing-override/acceleration-shape
  predicate is gated by authored animation tag names ("movement lock"/"movement unlock"/"face camera
  start"/"face camera end"), refuting the "aiming/strafing" HYPOTHESIS — it's a scripted
  animation-driven movement lock, not an aim/strafe state; ARM-side setter still unlocated. OPEN 22:
  both image defaults CONFIRMED by direct read (`0x012e6ced`=1, `0x012ec730`=0); the mission-16
  reset's off-value is −1 (not 0) and every writer is confined to one self-contained
  mission-16/"Avatar"-boss module — the walk-axis-invert default is otherwise permanently live from
  the `.exe` alone, residual question is an external (non-`.exe`) config override, out of this
  file's reach. OPEN 30: the on-foot human submode chooser `0x005745c0` fully tabulated against the
  already-documented 61-entry camera submode name table (`0x012e3d48`) — every output submode
  (sprint, crouch, swimming, fine aim variants, ragdoll, falling, avatar, downed, skydiving,
  rappelling, riot/human shield, script-driven, vehicle aim, wrestling, …) now has a confirmed real
  name and gating test; narrows the `+0xcd2` ragdoll-state HYPOTHESIS (6 = ragdoll, 3 = a separate
  "recovery" state). §16/U7: `instance+0x164` bits 0/1 CONFIRMED (bit 0 = master
  steering+`+0x13c`-channel lock, bit 1 = steering-only raw/linear-mode skip); the
  `0x00ae2c80`-as-brakes HYPOTHESIS for `+0x13c`'s consumer is REFUTED (it's a per-wheel
  friction/slip callback on a different object, not a reader of this instance); `+0x128`/`0x12c`/
  `0x138`'s writers remain genuinely unlocated (exhausted both known driving-input readers; new lead
  — a sibling Havok sub-object at `vehicle+0x818`, not yet examined). Full detail in
  `scratchpad/item22/`, `scratchpad/item13/`, `scratchpad/item30/`, `scratchpad/u7/`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — OPEN-backlog batch 2: stance bits, turn-angle target, speed multiplier, sprint/jump
  upgrade bits** (SPEC TEAM, agent `a0c88ca0b79350e79`). OPEN 15: `+0x1d94` bit `0x01` is HIGH
  CONFIDENCE a climb/vault/mantle grab-entry flag; bit `0x02`'s two previously-cited addresses
  turned out to both be CLEARS (suspend-state and respawn-reset contexts), real setter still
  unlocated; bit `0x04` is a grab/attach interaction, HIGH CONFIDENCE behaviorally; bit `0x08`'s
  only writer is destructor teardown — uninformative, genuine dead end; `+0x1d95` bit `0x04` weakly
  advanced (HYPOTHESIS: vehicle-exit-in-progress). OPEN 37: the turn-angle chase target is NOT
  simply `+0xa68` — it's 0.0 unless `+0xcd2` ∈ {0,3,6}, in which case it's the output of a large
  facing-computation routine (`0x009b4360`) that reads `+0xa68` as one of several inputs (fine aim,
  cover, boost, camera mode). OPEN 44: two of the three previously-listed `+0xa64` speed-multiplier
  writers were REFUTED as coincidental offset collisions on unrelated structures; the one real
  writer unconditionally RESETS the multiplier to 1.0 (not a boost) — no boost-setter was found
  anywhere, so the "script/activity speed override" HYPOTHESIS is now unconfirmed. OPEN 45: no
  setter found for `+0x1c9c` bits `0x80000`/`0x100000`, but the read side shows bit `0x80000` gates
  a transient, decaying sprint speed-cap ramp, not a flat permanent multiplier; exhaustively checked
  the full `unlockables.xtbl` apply-routine table and it does NOT touch these bits — REFUTES the
  perk-registry-via-unlockables hypothesis; `cheats.xtbl`'s "Infinite Sprint" cheat is now the
  leading unexplored lead. Full raw dumps:
  `tools/ghidra_server/scratchpad/agent_research_20261010/`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — OPEN-backlog batch 3: ungated sprint reads, `+0xccd` values 2/3** (SPEC TEAM, agent
  `a0f314ff04f897ef8`). OPEN 43 RESOLVED: neither sprint-boost reader checks local-player identity
  before querying the pad, and the sprint query itself (`0x005bd7b0`) takes no human/player
  parameter at all — it's architecturally a single local-input query, so any human reaching the
  boost branch reads this machine's own pad; `+0x1c9c` is not type-restricted to the local player
  (other bits of the same dword are read against every human), so the live-risk question folds into
  OPEN 45's still-unresolved writer side (no confirmed setter targets a non-owned human today).
  OPEN 40 PARTIALLY RESOLVED: value 3 was already resolved elsewhere in this file (§10.6/§10.10,
  "walking to a vehicle entry point") — the OPEN-items list just hadn't caught up; value 2 is not
  independently distinguished anywhere in the executable's dispatch logic (the master substate
  setter `0x00986920` and its network-snapshot twin `0x00988830` both treat 2 and 3 as one case),
  but co-occurs specifically with vehicle-interaction phase 1 ("entering"), pointing toward it being
  an earlier/more generic phase of the same destination-directed walk rather than a distinct
  crouch/cover stance — exact setter call site still unlocated. This closes the player-runtime
  OPEN-backlog push's ~10-item target (batches 1-3, all spliced/cleanroom-checked/synced).
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §7.4a's 4th-table follow-up on OPEN item #97: same negative result, the presumed-setter
  function cleared** (SPEC TEAM, agent `a35cc7455a222c76a`). The newly-flagged 4th 22-slot table at
  `0x0111f4e0` was enumerated: almost entirely empty (only local indices 16, 18 populated; index 15,
  the direct analog of the watercraft slot, has no function at all). Both populated slots
  (`0x005b8770`, shared verbatim with the first table's own slot 16; `0x005b7940`) are unrelated
  single-purpose prompt builders (weapon pickup/replace, weapon-overheat) with zero reference to
  `+0x2084` in either direction. The shared walker `0x005b6540`'s full 329-instruction body re-read
  end to end: every `+0x2084` reference across both of its scan loops is a `TEST`, never a write.
  **With both tables now fully checked, no write to `+0x2084` exists anywhere in the trigger-button
  family's reachable code — a solid negative, not "not yet found."** `0x005b6af0` (603 insns,
  previously presumed to hold the setter) was bounded-effort-checked and found to contain **zero**
  references to `+0x2084` in its own body, clearing that presumption; it calls the other 3 family
  members plus ~20 uncharacterized functions, flagged as the most promising remaining lead for a
  future pass. Full detail: `scratchpad/fourth_prompt_table_0x0111f4e0_2026-10-09.md`. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §7.4a's own watercraft-prompt-table lead on OPEN item #97, checked directly: a real
  confirmed sibling table, but a clean negative for closing the item** (SPEC TEAM, agent
  `a3544b2114c92da7a`). The second 22-slot table at `0x0111f488` is real — all 22 entries read, 7
  sampled by decompile, every populated slot a per-prompt message/UI-text builder sharing slot 15's
  own shape. Its sole walker (`0x005b6540`, a confirmed 4th member of the existing trigger-button
  function family) reads `+0x2084` bit-by-bit to pick which already-active prompt to render — exactly
  the same reader relationship the main table's own walker has to that word. **`+0x2084` is never
  written anywhere in this walker or in slot 15's own function — this table is a consumer, not the
  producer OPEN item #97 is looking for; the item stays open, unredirected.** New, unexplored lead
  found along the way: a 4th contiguous 22-slot table at `0x0111f4e0`, driven by the same walker on a
  second button, not yet enumerated. Full detail:
  `scratchpad/watercraft_prompt_table_open97_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — new §7.4a: the vehicle-enter eligibility gate `0x00b0ca60` traced in full — a pure
  character-state check, no vehicle/physics involved** (SPEC TEAM, agent `a4a20628e601754f4`, routed
  mission-scripting question). Corrects the question's own framing on both counts: the function has no
  vehicle argument at all (confirmed from its signature and both real call sites) and reads exclusively
  character-local state (vehicle-interaction phase, a busy/pending pair, ragdoll, down-or-dead, pose
  sub-state, a class-capability bit, `cant_enter_vehicle`, AI mode/sub-state) — never vehicle position,
  seat occupancy, or physics/collision state. Returns a plain boolean, no secondary code. The "2 physics
  obstruction queries" the question asked about are confirmed to be the already-documented pair at
  §13.6/OPEN item #104, not a new pair hiding in this function — it calls neither of them nor the two
  obstruction gates, at any depth, exhaustively checked. New: a second, previously undocumented caller
  (`0x005b7cc0`, a watercraft-enter UI-prompt helper reusing the same eligibility/auto-pick/category
  chain) sitting in a previously-unidentified second 22-slot function-pointer table — a plausible,
  unconfirmed lead on OPEN item #97. Full detail:
  `scratchpad/vehicle_enter_eligibility_0x00b0ca60_2026-10-09.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §8.2 step 34 gains a cross-reference: the "Hide Occupant" seat toggle's real render-
  level mechanism, and its real shipped-data prevalence, now traced in full** (SPEC TEAM, agent
  `ab4f4e1b4a76d7a98`, wave 6 rendering-gap item (b)). `0x00920170` confirmed to flip a generic
  "logically hidden" bit (`+0x3a` bit `0x80`), distinct from the engine's other generic `visible`
  property path (`0x008ccb90`). Critical finding for Team B: reading the real shipped
  `vehicle_interaction_info.xtbl` directly, `Hide Occupant` is set on a driver seat for only 2 of 131
  vehicles (both tanks) — every ordinary car/motorcycle/helicopter/boat/VTOL driver seat has it absent,
  so a renderer hiding the character for every vehicle while driving is reproducing the tank-only
  special case universally, not retail behaviour. Full detail: `spec-render-pipeline.md` §23.14;
  `scratchpad/invehicle_character_render_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — new §15.3a: generic damage application, death, and ragdoll hand-off, traced end to
  end** (SPEC TEAM, agent `a9c8ae1260dd97f53`, wave 6 item 3). Health storage, the damage-apply sink
  (`0x0096fa20`, re-confirmed, 2 of its own internal health-mutation calls newly pinned —
  `0x0094cfc0`/`0x0094d520`), a resistance/difficulty-scaling correction to the sink's own prior
  "caller always computes the final amount" phrasing, the death transition (`0x00970e70`, newly found,
  called by the sink at health≤0), and the real ragdoll/death-animation branch point
  (`0x0098ce40`'s `+0xcce` state machine, real ragdoll-activate call `0x009a5520`, real
  non-ragdoll-finalize call `0x00952760`). **Hitscan gap NOT closed**: both next-steps
  `spec-weapons-runtime.md` §2.2c named (`0x008ccb90`/`0x00920170`) were read in full and confirmed by
  direct disassembly to not reach this chain — the far side is now fully mapped, narrowing the
  search. Full detail: `scratchpad/character_damage_death_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §7.11's OPEN 55 extended: the class-registration mechanism confirmed for 43 real
  classes; the kind-row bit writer searched for exhaustively and NOT found** (SPEC TEAM, agent
  `a39496c2b1abfebed`, dispatched LOW PRIORITY by `purrsian-04` in response to Team B's
  object-name-string classifier being labelled CHOSEN rather than the real mechanism). A whole-`.text`
  byte-shape scan found 43 registered classes (not just the 2 `spec-zone-data-format.md` §11.9 item 4
  previously named), including the real runtime `vehicle`(7)/`player`(0x21)/`npc`(0x22) classes, all
  sharing one byte-identical registration-stub template that writes the `0x02cc9900[classIndex]`
  table entry. Found the real `Create()`-vtable-slot writer of an object's own class-index byte
  (`+0x34`) for all three. **The kind row itself (descriptor bytes `+6..+0x25`, what `0x00853b30`
  reads) is unconditionally zeroed by every one of 9 fully-disassembled registration stubs, and a
  4-strategy, whole-binary search for a later non-zero-bit writer found NONE** — a solid negative,
  not an incomplete one. Surfaced a new open question: `player`/`npc`'s own class indices (`0x21`/
  `0x22`) numerically match the bit numbers already confirmed meaningful for them, which could be the
  real mechanism or could be coincidence (`vehicle`'s index is `7`, arguing against a simple
  "bit = own class index" reading generally) — not settled, a live capture is the natural next step,
  flagged for the orchestrator/user, not dispatched here. Full 43-class table and the complete
  negative-search writeup: `scratchpad/player_vehicle_class_bit_writer_2026-10-08.md`. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §5.6.5 gains the full `0x0079d0b0` support-query internals (previously cited by name
  only)** (SPEC TEAM, agent `ace158c4d83e1eec5`, dispatched by `purrsian-04` for Team B to drop a
  flat-plane ground approximation). Full manifold/gather/filter/averaging chain traced: the support
  query reads the proxy's own contact manifold (filled every frame by the already-documented
  continuous shape-cast loop, §5.6.4) and averages qualifying contacts (dot-up < −0.08) into a
  support state/normal/velocity triple — a textbook multi-contact algorithm already generic over
  arbitrary geometry, confirming the controller never assumed a flat plane. Flagged, not resolved: an
  unreconciled discrepancy between this pass's own "shared 0.7 constant" finding for the forward step
  probe and this section's own prior "0.72/0.6" language; a correction candidate on the forward
  probe's "length 90" field (written as a plain integer, not a float). Full detail:
  `scratchpad/havok_character_proxy_ground_following_2026-10-08.md`. Cleanroom self-check: 0 new hits.

- **2026-10-08 — new §16 (U7 — in-vehicle driving controls): the input→Havok hand-off, closing a
  gap this file's own scoping note left untraced** (SPEC TEAM, agent `a0d08cde9be318115`). Primary
  splice is `spec-physics-runtime.md` §10.9; this section is a secondary cross-reference tying that
  finding to this file's own already-published §10 (entry)/§11.4 (exit)/§12.4 ("Vehicle Steering")
  material. No new disassembly in this file beyond the cross-reference. Full source:
  `scratchpad/vehicle_entry_exit_control_handoff_2026-10-08.md`. Cleanroom self-check: 0 new hits.

- **2026-10-08 — §2.7 gains a structural note tying its own "late"/secondary row-2 action tree
  (`0x005b69c0`→`0x005b6420`→`0x005ae3f0`) to `spec-weapons-runtime.md`'s own fire-path hunt** (SPEC
  TEAM, agent `a4fe952ab41afa206`). `0x005ae3f0` is now CONFIRMED to be the player's actual fire-input
  dispatch (indirect virtual call on the equipped item's own vtable `+0x8c`) — this file already
  documented the entry point but had never cross-referenced it against weapon code. No new disassembly
  in this file beyond the cross-reference; full detail lives in `spec-weapons-runtime.md` §2.2b. Full
  source: `scratchpad/player_combat_aim_fire_reload_2026-10-08.md`. Cleanroom self-check: 0 new hits.

- **2026-10-08 — §15.4 gains a partial closure and a cross-reference.** `0x006D9310` (the "game-mode-
  state enter handler" §15.4's own account of the death/respawn restart path rested on secondhand, via
  `interp_fresh_start_checkpoint.md`) is now independently re-walked in full by
  `spec-lua-api-behaviour.md` §21.27a/§21.27b — consistent with, not contradicting, this section's
  existing account. New cross-reference to §21.27b, the structural counterpart to this section's own
  organic FAILURE trigger: §21.27b found no equally clean organic SUCCESS trigger exists, only
  fail-safes — the engine never itself detects "objectives complete." No new disassembly in this file.
  Cleanroom self-check: 0 new hits. Synced to Team B.

- **2026-10-08 — new §15: player combat (aim-assist, melee, death/respawn); OPEN items renumbered to
  §16, clean-room check to §17.** The first combat material in this file (previously zero). Closes
  `aim_assist.xtbl`'s runtime consumer (a per-frame profile selector, `0x009ec2f0`); closes melee's
  resolution half (trigger was already specced) and confirms player melee is a separate code path from
  NPC melee, not a shared mechanism; traces the full player death chain (`0x00702810`) and confirms it's
  the identical in-memory checkpoint-restore mechanism `spec-save-format.md` §11.1/§11.2 already
  document for a manual checkpoint restart, not a bespoke respawn implementation. The generic
  weapon-fire/hit/damage chokepoint is explicitly out of scope (owned elsewhere); the player-specific
  `+0x276C` damage-scale array's consumer inside that chokepoint is left as an honest OPEN handoff. Full
  detail: `scratchpad/player_combat_aim_melee_damage_respawn_2026-10-08.md`. Cleanroom self-check: 0
  hits. Synced to Team B.

- **2026-10-08 — §7.8 phase-boundary-writes table extended: the constructor's own `+0x16d4` default
  pinned** (SPEC TEAM, wave-3 agent `a65253bea947bc320`). `0x00954be8` (inside human constructor
  `0x00954340`) zeroes `+0x16d4` unconditionally for every newly-constructed character (`edi`, set
  once via `xor edi,edi` at `0x00954388`, never redefined before the write) — this is the pre-"any
  occupancy transition" default, not a fifth occupancy-band writer, and does not contradict §7.8's
  existing four-site table (that band sweep was scoped to the occupancy subsystem only). Closes the
  open constructor-value question in
  `scratchpad/char_state_enum_0x16d4_full_value_table_20261008.md` (line 174). Full detail:
  `scratchpad/named_objects_sentinels_0x954be8_m05_2026-10-08.md` §3. Cleanroom self-check: **regex 0
  hits, backstop 0 hits**, unchanged.

- **2026-10-03 — created** (SPEC TEAM). Units U0 (§1) and U1 (§2) of `review/scoping-player.md` §5. Ghidra-free (capstone over `exe_va.py`, raw rel32/dword scans, push surveys). Corrections to the scoping note flagged inline and collected in §1.14 and §2.5/§2.6; cross-spec flags for `spec-tables-ui-controls.md` §2.2/§2.3 and `spec-lua-api-behaviour.md` §2.6/§7.22 raised in §1.14 and §2.9 (no other spec edited). Scoping OPEN item 2 resolved (§2.8). Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) run on the finished file: **regex 0 hits, backstop 0 hits**; a known-positive control string matched 5/5 on both in the same run, so the zeros are real. Re-run after this line was written, same result.
- **2026-10-03 — U2 added** (SPEC TEAM). New §3 "U2 — Normal-condition movement and the intent fields" (classify `0x005ac620` and the six context words, action dispatch `0x005b73c0` and its targets, look slot `0x005ab6a0`, movement slot `0x005b3e80` → `0x005ad130` → `0x005b31f0` → writer `0x005aee30` with the closed-form move-vector math in §3.5.5, on-foot handler `0x005b2ca0` and children `0x005ab860`/`0x005ab930`/`0x005b29d0`/`0x005ad9a0`/`0x005abd70`, stance-byte semantics). Former §3 OPEN items and §4 clean-room check renumbered to §4 and §5 (strikethrough headings; the two in-text references to the OPEN section updated); OPEN items 11–22 appended and item 2 annotated as partly resolved. Ghidra-free (capstone recursive descent over `exe_va.py`, a full linear sweep of `.text` for displacement scans, raw rel32/dword scans of the whole image, push surveys, `.rdata` constant reads); no Ghidra project opened, no `.czn_pc` data touched. Corrections to `review/scoping-player.md` §1.3/§1.5/§1.7/§2 and cross-spec flags for `spec-anim-runtime.md` §4.2 and `spec-lua-api-behaviour.md` §18.26 collected in §3.9 (no other spec edited). Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run. Re-run after this line was written, same result.
- **2026-10-03 — U4 added** (SPEC TEAM). New §4 "U4 — The follow camera and the camera mode machine": the mode table re-based to `0x0111a1e8` with five slots (init/reset/enter/pre/update) and every row identified or shown unreachable (§4.1); the mode stack's exact semantics and an exhaustive survey of 8 push, 7 pop and 17 set-base sites (§4.2); the frame cadence — the three update sites are alternative branches, the pre-update runs inside the controls (§4.3); the follow camera's state, pre-update, look-input-to-rotation path with the `panning_group` constants, update and core `0x00575e30` read in full, the submode choice and 4-kind blend, pivot, collision and FOV (§4.4–§4.9); the working/committed/previous output sets (§4.10); the generic update (§4.11); and the vehicle-camera fields the follow camera reads (§4.12). Former §4 OPEN items and §5 clean-room check renumbered to §5 and §6 (strikethrough headings; the five in-text `§4, OPEN n` references and the confidence-key pointer updated to §5); OPEN items 23–34 appended and item 6 annotated as advanced. Corrections to `review/scoping-player.md` §1.3/§1.8 and cross-spec flags for `spec-cutscene-camera-format.md` §10.2/§10.5/§10.6, `spec-tables-environment.md` §7/§12.2, `spec-tables-ui-controls.md` §8.1, `spec-render-pipeline.md` §11 and `spec-lua-api-behaviour.md` §13.16 collected in §4.14 (no other spec edited). Ghidra-free; no `.czn_pc` data touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run. Re-run after this line was written, same result.
- **2026-10-03 — U3 added** (SPEC TEAM). New §5 "U3 — The locomotion bridge": the scoping chain `0x0099ebe0` → `0x007a5760` → `0x0079e900` → `0x00961d70` re-identified as the post-move pass, a contact-queue flush and the vehicle-contact replay (§5.2); the real bridge traced end to end — velocity composer `0x009989b0` → `0x009a02a0` → `0x0099d1b0` writing the controller desired velocity `+0x1cec` before physics, and the character-controller step `0x0099e7e0` (gather `0x0099ae30`, step `0x0099b430` with `0x007a5b10` and at most ten lock-step proxy passes, write-back `0x0099e2a0`) after it (§5.1, §5.4, §5.6); the player's closed form `+0x1cec` = k·`+0x203c` + R·E/Δt (sprint: along the facing), velocity-driven with stride matching and root motion only under animation-controller flag `0x100` (§5.4.4–§5.4.5); the locomotion-state update `0x00985df0` (§5.5, resolving OPEN 19); timing with no sub-step reading intent (§5.9); and the player/NPC divergences (§5.10). `spec-anim-runtime.md` §4.2 cited, not re-derived, with an offset agreement table (§5.3). Former §5 OPEN items and §6 clean-room check renumbered to §6 and §7 (strikethrough headings; the seven in-text `§5, OPEN n`/`§5 OPEN 6` references and the confidence-key pointer updated to §6); OPEN items 35–46 appended, OPEN 19 marked resolved and OPEN 18 advanced; inline correction notes added at §0 item 2, §1.8.4, §3.5.4 step 19, §3.5.5 and §3.7 (`+0xcb0` is the move target, so `+0xa50` = unit(r)). Corrections to `review/scoping-player.md` §1.0/§1.4/§2/§5 and cross-spec flags for `spec-anim-runtime.md` §4.2, `review/scoping-animation.md` §1.7, `spec-physics-runtime.md` §2.1/§4.6 and `spec-ai-runtime.md` §1.1/§1.4 collected in §5.12 (no other spec edited). Ghidra-free (capstone listings over `exe_va.py`, a linear sweep of `.text`, raw rel32/dword scans, an RTTI walk); no Ghidra project opened, no `.czn_pc` data touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run. Re-run after this line was written, same result.
- **2026-10-03 — U6.6 added** (SPEC TEAM). New §6 "U6.6 — The exit request and the non-animated exit states (`0x14`–`0x19`)" (`review/scoping-player.md` §5a.3). Contents:
  - the exit request `0x00af3cd0(human, request)`: the 0x33-byte request record (constructor `0x004dccd0`), the permission rule `0x00b0b170` (which reads `+0x1c9c` bits 8/9 against `+0x16d8`), "always state `0x14`", and its 79 call sites in 70 functions classified with the request fields each sets (§6.2–§6.3);
  - the cancel family (§6.4): `0x00af3e10` (phase-generic), the enter-only cancel `0x00af2460`, the exit fast-forward `0x00af24d0` and the vehicle emptier `0x00af3f20`;
  - states `0x14`–`0x19` with every on-entry and per-frame handler read in full (§6.5–§6.11), including the network applier's entry and the six inlined set-exit copies;
  - equip-on-exit pinned down: `0x00b036c0` is state `0x15`'s on-entry handler; the seat flag is `Vehicle_Interaction_Info` seat byte `+0x0B` bit 0; inventory slot 5/3 is tested, then a team-mapped `Rifle-NG`/`Rifle-STAG`/`Rifle-Gang` is given (§6.7.3);
  - `0x00b03d80` confirmed as the finaliser, ending in slot release (§6.11);
  - the data check against the shipped `vi_exit.xtbl`: 813/813 `Element`s begin with an animated-state token, and no U6.6 handler reaches the element rule (§6.12).
  U6.0 had not landed, so the slot fields used are cited by raw address in §6.1, to be reconciled with U6.0. Former §6 OPEN items and §7 clean-room check renumbered to §7 and §8 (strikethrough headings; the seven in-text `§6, OPEN n`/`§6 OPEN n` references to this file and the confidence-key pointer updated to §7; the one reference to `spec-physics-runtime.md` §6 OPEN 5 left unchanged). OPEN items 47–59 appended. Corrections to `review/scoping-player.md` §5a.1/§5a.3/§5a.4/§5a.6 and cross-spec flags for `spec-lua-api-behaviour.md` §15.13/§21.11/§21.31/§28.22/§45.1, `spec-tables-vehicle-world.md` §3.2 and `spec-ai-runtime.md` are collected in §6.14 (no other spec edited). Ghidra-free (capstone listings over `exe_va.py`, raw rel32/dword scans, push-corrected field surveys, census closures, static table reads); no Ghidra project opened, no `.czn_pc` data touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.
- **2026-10-03 — §5.4.3 step 3 settled: the T/E discriminator** (SPEC TEAM). `0x004be240` and `0x004c58c0` read in full: the sum walks the animation instance's `0x50`-byte layer array, not the controller slots, and splits on layer flag bit `0x01`, the phase-synchronised group mark (setters `0x004c6590`/`0x004bce90`, non-setter `0x004c6980`; advance split `0x004c4810` → `0x004c3d20`/`0x004c45a0`). `Anim_Movement` refuted as the discriminator; its real role (controller `+0x198` bit 8, step 5's flag `0x100`) and flags 4/`0x10` identified as `Block_Translation`/`Zero_Input`. X re-characterised as a translation. New paragraph "Step 3 in detail" after step 10; step 3, step 5's first bullet and §5.0 item 4 reworded; OPEN 39 partly resolved and re-scoped. Validated against `anim_files.xtbl` (220 `Anim_Movement` rows, 4 of 790 locomotion rows) and the shipped node graphs' `loop_flag`. Cross-reference and a reaper-wording correction added to `spec-anim-runtime.md` §6.2/§6.3. Ghidra-free; no `.czn_pc` data touched.
- **2026-10-03 — U6.0 and U6.4 added** (SPEC TEAM). New §7 "U6.0 — The vehicle-interaction slot record, registry and state skeleton" and §8 "U6.4 — The seating commit, the ride state and the camera hand-off" (`review/scoping-player.md` §5a.3), after U6.6 (§6), whose raw-address infrastructure (§6.1) §7 reconciles. Contents:
  - the 48-record registry keyed by vehicle handle with the human back-pointer `+0x1c48`, the slot and request layouts, find/allocate/claim/release and the submitter `0x00af22a0` in full (§7.1–§7.4);
  - the three setters never store the state, each on-entry handler stores its own; the numbering of all 35 states validated against every setter site, shared call tail and inlined setter copy: `0x10`/`0x12`/`0x1a`/`0x1e`/`0x20` can never be held, 7/`0x18`/`0x1c` have no static producer (§7.5);
  - the phase tick with the 250 ms seat-resolution upkeep (§7.6), `0x00afa400` re-identified as the transition's network send (§7.7), the four `+0x16d4` writes confirmed (§7.8) and the 15-function query surface (§7.9);
  - the commit's complete observable end state on slot, human and vehicle with a minimal stub list (§8.1) — including the condition row `+0xcc8` = 8/9 that actually hands the player to vehicle controls — the commit in order (§8.2), its 39 vehicle-band callees (§8.3), holstering confirmed in shape and paired with §6.11's restore (§8.4), the ride state with the `Collision Bool`/`Collision Dir` writer and the `VI_*` body-lag model (§8.5), and the camera hand-off on top of §4, which reads the camera-angle sets of OPEN 28 (§8.6).
  Former §7 OPEN items and §8 clean-room check renumbered to §9 and §10 (strikethrough headings; the eight in-text `§7, OPEN n`/`§7 OPEN n` references and the confidence-key pointer updated to §9); OPEN items 60–67 appended; OPEN 28, 39, 52, 55 and 58 annotated. Corrections to `review/scoping-player.md` §1.7/§5a.1/§5a.2/§5a.3/§5a.5/§5a.6 and cross-spec flags for `spec-lua-api-behaviour.md` §18.20/§21.11/§22.3/§38.1, `spec-anim-runtime.md`, `spec-ai-runtime.md` and this file's §4.8/§4.12/§6 collected in §7.11 and §8.8 (no other spec edited). Ghidra-free; no `.czn_pc` data touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.
- **2026-10-04 — U6.1 added** (SPEC TEAM). New §9 "U6.1 — The element-selection rule" (`review/scoping-player.md` §5a.3), after U6.0/U6.4. Contents:
  - the five roots read in full: key builder `0x00b0a6f0`, wildcard matcher `0x00b0a650`, lookup `0x00b0ad10`, count `0x00b0ac60`, and `0x00b0a510`, re-identified as the seat validator; plus the side map `0x00b0a5f0` and the ride side's exact matcher `0x00b0a580` (§9.1);
  - the 83-token vocabulary of `0x0130e638`, read one entry at a time (§9.2);
  - the full state → key table with every field condition, including the fields scoping missed (`+0x1c` surface, `+0x20` side, `+0xd1`) and the short keys (§9.3);
  - set choice by phase, the silent miss path and the short-key behaviour (§9.4);
  - all 23 call sites in 16 functions with the slot each passes, including four temporary-slot lookups and one hand-built key (§9.5);
  - validation against the shipped tables (§9.6): `vi_enter.xtbl` 657/657 `Element`s producible; `vi_exit.xtbl` 808/813 (5 dead, listed); `vi_ride.xtbl` 0/454 by construction, since the rule never reads `Ride_Animations` and ride uses `0x00b08aa0`'s exact six-token match, whose layout all 454 fit. First-match shadowing hides 4 distinct exit animations.
  Former §9 OPEN items and §10 clean-room check renumbered to §10 and §11 (strikethrough headings; the 14 in-text `§9, OPEN n`/`§9 OPEN n` references and the confidence-key pointer updated to §10). OPEN items 68–72 appended; OPEN 47, 48 and 58 annotated; a correction note added to §7.5 "Element-key cases for unheld states" (the `0x1e` case is live). Corrections to `review/scoping-player.md` §5a.1/§5a.3/§5a.4/§5a.6 and cross-spec flags for this file's §6/§7/§8 and `spec-tables-vehicle-world.md` §2.1/§2.2/§3.2 are collected in §9.8 (no other spec edited). Ghidra-free; no `.czn_pc` data touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.
- **2026-10-07 — U6.3 added** (SPEC TEAM). New §10 "U6.3 — The enter route: states 0–8, the routing hub and the instant route" (`review/scoping-player.md` §5a.3). Contents:
  - the routing hub `0x00afd4c0` read in full: its decision table confirmed exactly — instant (2/4/8), the fast-entry bit (`0x13`), forced/override (`0x0f`), otherwise 5 — with state 1 never held across a frame (§10.3);
  - the instant-route HYPOTHESIS refuted: an instant request never reaches `0x0f`; it runs 0 → (upkeep) 1 → 4 → 9, or 1 → 2 → 4 → 9 when the seat is occupied, committing inside the first phase tick after submission when the seat is free and the queue empty; `0x0f` is the forced/override in-place `enter` animation route, falling to 9 only when no `Element` can be found (§10.4);
  - the cancel-and-resubmit helper `0x00afe980` (§10.4.3) and the cancel `0x00afb930` read in full as a one-shot that releases the slot inside its own on-entry handler, calls the `+0x34` "begun" callback with a success flag but never the completion callback `+0x3c` (§10.9);
  - enter state 7 shown dead by the full local transition census and by the network applier's fixed value set, closing OPEN 60 for state 7 and extending it to exit `0x18`/`0x1c` (§10.8);
  - the entry-point queue `0x00b0cb20` shown to order by squared distance from the bss vector `0x029cdb98`, which is zero in the image with 555 readers and no store in `.text` — a defect HYPOTHESIS that the entry point was meant (§10.1);
  - request `+0x08` identified as a request kind (0 approach-only, 1 remove-the-occupant-then-stop, 2 normal), not a flag; the instant bit shown set at 7 sites and tested at 12 (§10.4.1, §10.10);
  - the two stick aborts (state 0 and state 5), both local-player-only with an 800 ms grace, and the four approach tweaks `VI_pathfind_cancel_dist_multiplier`/`VI_pathfind_check_pause_distance`/`VI_pathfind_play_stop_dist`/`VI_on_vehicle_entry_max_y_diff` read at their shipped values, alongside the six ride tweaks of OPEN 63 (§10.2, §10.6, §10.13).
  Former §10 OPEN items and §11 clean-room check renumbered to §11 and §12 (strikethrough headings; the 15 in-text `§10, OPEN n`/`§10 OPEN n` references and the confidence-key pointer updated to §11). OPEN items 73–79 appended. Corrections to `review/scoping-player.md` §5a.1 items 4 and 8, §5a.2 and §5a.5 (the instant/snap HYPOTHESIS for `0x0f` refuted; "instant is tested at three places" corrected to 7 set / 12 test sites) collected in §10.13 and folded into that file directly (no other spec edited). Ghidra-free (capstone listings over `exe_va.py` with an esp-depth tracker, census call edges, a band displacement sweep of `0x00af0000`–`0x00b12000`, raw rel32/dword scans, static table reads, the shipped `tweak_table.xtbl`); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.
- **2026-10-07 — OPEN 39 resolved and §5.0/§5.3/§5.4.5 corrected, from `spec-anim-runtime.md`'s U5** (SPEC TEAM). `review/scoping-animation.md`'s root-motion unit (U5, landed as that file's §9) read the post-sample module and the correction step in full and found: the orientation step `0x004b6530` applies no rotation in ordinary locomotion — outside an authored `start correction`/`dest correction` trigger window it returns the human's orientation unchanged, and inside one it is a motion-warp/heading-correction step that X (controller `+0x70`) feeds. §5.0 item 4, §5.3 and the §5.4.5 table's "orientation" row are annotated accordingly in place (strikethrough plus a bracketed correction each), and the open question of what turns a human outside a correction window is left pointing at `spec-anim-runtime.md` §5.1 item 35. OPEN 39's remaining "Still open" list (the orientation step, `0x004b49e0`, `0x009987d0`, controller `+0x1a4`, `0x004cad00`, `0x004c6ebb`) is struck through and marked resolved, citing `spec-anim-runtime.md` §9.7/§9.9/§9.10; `0x004c8a80` is identified (the bound-track rotation decoder) with its bit-for-bit read routed to `spec-anim-format.md`. No other section renumbered. Cleanroom self-check (the project's standard auto-name regex) on the entire file: **0 hits**, run after these edits.
- **2026-10-07 — U6.7 added** (SPEC TEAM). New §11 "U6.7 — The animated exit states (`0x1a`–`0x21`)" (`review/scoping-player.md` §5a.3), after U6.3 (§10). Contents:
  - the eight-state census completed from the handler side: `0x1a`/`0x1e`/`0x20` have no on-entry or per-frame handler and are dead; `0x1c`'s on-entry and per-frame are both confirmed empty, with no local producer; the four live states are `0x1b`, `0x1d`, `0x1f` and `0x21` (§11.1–§11.2);
  - `0x1b` (change seat) and `0x1d` (extract) read in full, the latter adding a hijacker-resync scan of the record's other seven slots (matching on the hijacker's own target-seat field) that U6.1's existing citations of `0x1d`'s field copy and miss fallback did not need to assume (§11.3–§11.4); `0x1d`'s per-frame snaps the physics character proxy to the animated end position (confirming scoping §5a.1 item 5) and hands off to one of two deep human-runtime contracts by extract kind, with no explicit state transition found for the non-`kill` case (new OPEN item);
  - `0x1f` (exit) read in full: on-entry, the 783-instruction exit-kind chooser `0x00b04ae0` (resolving most of OPEN 69 — it writes kinds `0x15`/`0x16`/`0x17`/`0x18`/`0x1b`/`0x1c` and the named sub-variants, not `0x14`/`0x19`/`0x1a`), the 696-instruction per-frame handler (confirming the inline `0x1f → 0x21` transition and the walk-axis read scoping flagged), and the 158-instruction eject handler `0x00b02cd0`, which writes the request's launch vector directly into the player's own desired-velocity field `+0x1cec`/`+0x1cf4` — the same field §5.4 (U3) specifies — instead of playing any clip (§11.5);
  - `0x21` (traverse) read in full, including the surprising finding that its on-entry always calls `0x1f`'s entire on-entry as a subroutine first (§11.6);
  - the three shared helpers scoping §5a.4 point 6 left unassigned (`0x00afe0d0`, `0x00b01460`, `0x00b0c170`) specified in full, with `0x00b01460` shown to run on the hijacker's own slot when called from `0x1d` (§11.7);
  - a consolidated table of new and revised exit-request/slot/human fields, including the exit-request object handle `+0x08`/`+0x0c` (OPEN 47, now with three readers) and a newly found conflict between an exit-request `+0x31` bit 0 reading from this unit and U6.1's existing `convertible`-variant reading of the same bit, left OPEN (§11.8).
  Former §11 OPEN items and §12 clean-room check renumbered to §12 and §13 (strikethrough headings; the one in-text `§11, OPEN n` reference and the confidence-key pointer updated to §12). OPEN items 80–86 appended; OPEN 45, 47, 58, 69 and 79 annotated. Corrections to `spec-player-runtime.md` §6.1/§6.2 (this file's own U6.6/U6.1 sections) and a forward cross-reference to §5.4 (U3) are collected in §11.10 (no other spec edited; `review/scoping-player.md` §5a.3/§5a.4 point 6 given a light-touch inline note, not a rewrite). Ghidra-free (capstone listings over `exe_va.py`, a census closure of all 53 in-band functions / 6,187 instructions, raw rel32/dword scans for jump-table and call-site confirmation); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.

- **2026-10-07 — U6.8 added** (SPEC TEAM). New §12 "U6.8 — Per-slot pose driver and rider parameters" (`review/scoping-player.md` §5a.3), after U6.7 (§11). Contents:
  - the per-slot pose driver `0x00af6310`'s real dispatch corrected: phase is tested before state; ride (phase 3) never touches any jump table; the "three jump tables" scoping named are a 7-entry exit table, a 17-entry enter remap folded into a 6-entry pointer table, and a private 9-entry sub-dispatch inside exit state `0x1f` alone — not three parallel phase/state tables (§12.2–§12.3);
  - the two walkers `0x00af8f70`/`0x00af9030` fully specified (§12.1);
  - `0x00af4b80` specified down to the tweak: 11 named animation-controller parameters, written only while the slot's phase is ride, nine derived from the ride body-lag model's error fields (slot `+0xe4`/`+0xec`) and three newly-named `VI_*` tweaks, two from vehicle wheelie/endo predicates — closing the `0x00af4b80` half of OPEN 63 (§12.4);
  - `0x00af4150` reclassified as the rider fall/eject detector, not a parameter writer (§12.5);
  - `0x00af4760` reclassified as the ride-`Element` key builder, closing the `0x00af4760` half of OPEN 71 (§12.6).
  Former §12 OPEN items and §13 clean-room check renumbered to §14 and §15 (strikethrough headings; all live in-text "§12, OPEN n"/"§12 OPEN n" cross-references elsewhere in the file updated to §14; historical Changelog "renumbered to §X/§Y" sentences left untouched). OPEN items 87–96 appended; OPEN items 63 and 71 annotated in place with bracketed resolution notes citing §12.4/§12.6. Corrections to `review/scoping-player.md` §5a.1 item 5, §1.7 and §5a.3's U6.8 row collected in §12.9 and folded into that file directly (no other spec edited by this unit; `spec-anim-runtime.md` §4.1's `0x00af4b80` citation is flagged for a follow-up edit, not made here since this session's integration step owns it). Ghidra-free (capstone listings over `exe_va.py` with a purpose-built `esp`-delta tracker; one tracker limitation disclosed in §12's method note — callee-side stack cleanup on a handful of small helpers inside `0x00af6310`'s preamble — does not affect any CONFIRMED finding); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file after this edit: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.
- **2026-10-07 — U6.2 added** (SPEC TEAM). New §13 "U6.2 — Entry trigger and vehicle/seat choice" (`review/scoping-player.md` §5a.3), after U6.8 (§12). Contents:
  - the trigger resolved: a single shared button (`CBA_GAC_ACTION`) walking a 22-entry handler table at `0x0111f2d0` keyed on player context word `+0x2084`, not a per-prompt test/handler pair — answers scoping §4.2 OPEN item 9 (§13.1);
  - `0x009e5e80`'s seated/not-seated split and `0x009e1e60`'s full `0x44`-byte request-construction defaults, including a new baseline `+0x08` = 2 and the co-op/solo and forced-seat special cases (§13.2);
  - **major structural finding:** neither the auto-pick `0x00b105c0` nor the validator `0x00b0fdf0` (nor the seat-scoring helper `0x00b0eb90` inside it) ever writes a chosen seat or door back into the request or slot — every player-triggered submission goes through with vehicle = 0 and an unresolved seat, and the actual seat/door commit happens later, on the registry record's own 250 ms upkeep tick (already specified at §7.6) (§13.3–§13.4, §13.7);
  - `0x00b0eb90` promoted from scoping's HYPOTHESIS to CONFIRMED as the seat-scoring/eligibility gate, full 8-argument signature and per-seat filter set recovered, including its `fast`-entry tie-in to the element-selection rule agreeing exactly with U6.1's own citation from the other call site (§13.5);
  - the two SEH-wrapped obstruction gates `0x00b0da10`/`0x00b0dc20` specified, with their player-only vs. universal-but-instant-exempt asymmetry (§13.6);
  - scoping §5a.4 point 3's AI-sharing claim confirmed by direct read, not merely caller-list inference: `0x0051d5b0`/`0x00521200` call `0x00b0fdf0` directly, below the submitter, not through the on-foot trigger path (§13.7).
  Former §14 OPEN items and §15 clean-room check kept at §14/§15 (U6.2 lands after U6.8 in the same pass, so no further renumbering was needed). OPEN items 97–107 appended. Corrections to `review/scoping-player.md` §5a.3, §4.2 OPEN item 9 and §5a.4 point 3 collected in §13.9 and folded into that file directly (no other spec edited). Ghidra-free (capstone listings over `tools/harnesses/exe_va.py` via a local `listfn.py` harness, manual esp-depth tracking, a static read of the prompt-handler table); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file after this edit: **regex 0 hits, backstop 0 hits**; the known-positive control string matched 5/5 on both in the same run.
- **2026-10-07 — U6.5 added** (SPEC TEAM). New §14 "U6.5 — Enter animated states (`0x0a`–`0x13`)" (`review/scoping-player.md` §5a.3), after U6.2 (§13). Contents:
  - the eight live states in the range (prepare, traverse, open, extract/hijack, extract-fail pick, enter, change seat, fast) with all sixteen on-entry/per-frame roots read in full except one tail of `0x13`'s per-frame (new OPEN item); `0x10`/`0x12` confirmed dead a third way, independently of U6.0's setter-side and U6.1's key-builder-side surveys (§14.1, §14.8);
  - the shared on-entry skeleton (resolve/store-state/reset-variants) and its four deliberate exceptions, three of them sub-timer seeds advanced by the state's own per-frame — the same pattern already found for exit state `0x1f`'s own `+0x18` (§14.0 item 2);
  - `0x0d` (extract) shown to play no animation on its own slot at all: for a tank seat it independently re-implements the hub's own instant-hijack kill/redirect logic (§10.4.2 H1); otherwise it delegates to the hijack chooser already specified by U6.1 §9.5, with the hijacker's own clip driven entirely from the victim's `0x1d` side (§11.4.1) — confirmed by omission (§14.5);
  - `0x0e` (extract-fail pick) extended past U6.1's existing citation: its per-frame never calls the chooser, always cancels, then re-runs the hub's vehicle-AI-reaction logic verbatim (§14.6);
  - `0x13` (fast) read as the richest handler in the range, tying together U6.6 §6.2, U6.3 §10.7 step 5, U6.1 §9.5 and U6.7 §11.7.2 from a third independent context each, and shown to commit via an inline call to `0x00afbd90` rather than ever calling set-enter (§14.10);
  - one correction raised against §7.5/§9.1's uniform citation of the chooser `0x00afb220(record, i, 0)`: state `0x0c`'s per-frame passes a non-literal third argument, so the signature should read `(record, i, flag)` (§14.0 item 8, §14.13 item 2).
  Former §14 OPEN items and §15 clean-room check renumbered to §15 and §16 (strikethrough headings). OPEN items 108–113 appended. No correction raised against `review/scoping-player.md` §5a.3's U6.5 row (independently reproduced exactly: 16 roots, correct state-name mapping, size estimate holds up) — confirmed accurate, not amended. Ghidra-free (a purpose-built linear Capstone lister over `exe_va.py`, validated byte-for-byte against an already-published U6.7 function before being trusted on new code; manual esp-delta tracking); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. One self-caught-and-corrected drafting error disclosed inline (§14.5's note) rather than silently fixed. Cleanroom self-check (strict regex + plain-substring backstop, Python `re`) on the entire finished file after this edit: **regex 0 hits, backstop 0 hits.**
- **2026-10-07 — Havok W-U2 integrated (SPEC TEAM), closing OPEN 35 and OPEN 36 (the character-controller walking-slice behavioral contract, continuing Havok unit W-U1).** §5.6.5 deepened in place with `0x007a5b10`'s full internals: the per-human-kind capability gate (`0x02cc9900`), slope removal and the "too steep" downgrade, two independent unsupport overrides (departure-speed and the steep-slope timer `controller+0x160`), the full `controller+0x08` state-bit catalog, the step-up/step-down/ledge-assist raycast chain and its three-timer fall-danger gate, the explicit gravity formula (quadratic terminal-velocity falloff against the speed cap `+0x170`, not a flat `g·dt` or a hard clamp), the explicit jump-launch formula (`v=√(2|g|h)` off the function's own 3rd argument), and both other callers (confirmed to be internal continuation labels inside a shared caller, not independent functions). §5.6.4 deepened with the full 8-function lock-step pass cycle (seed → snapshot+submit → submit → advance → cast-and-solve → ask-time-remains → finish×2) and a new §5.6.4a pinning down `0x0079de40` (the "game-introduced virtual" `spec-physics-runtime.md` §2.1 row 22 flagged) as a slot on the proxy's own primary vtable, reached from inside the cast-and-solve step, not a separate Havok method — plus the thread-safety conclusion both listener mechanisms and the lock-step jobs run on the game's persistent class-3 worker pool, not guaranteed the main thread and not serialized across proxies in the same pass (resolving `spec-physics-runtime.md` §11 OPEN item 22 beyond its own hedge). OPEN 35/36 struck in the master list with pointers to the new text. Ghidra-free (capstone over `exe_va.py`, an RTTI complete-object-locator vtable-name reader reused from W-U1); no Ghidra project opened; no `.czn_pc` data touched; no leaked Havok/SR4/Volition source read. Cleanroom self-check on the entire finished file: 0 hits.
- **2026-10-09 — §13.4: routed mission-script cross-check against the seat-pick validator**
  (SPEC TEAM, agent `a18a24933af5b1e9b`). Real m05/m07/m09 Lua extracted and checked: m05/m09 never
  check a vehicle-enter return value; m07's `m07_setup_pony_carts()` has an explicit
  retry-on-refusal loop whose own mission-author comment anticipates exactly the occupant-conflict
  refusal case already documented in §13.4 steps 6-7. Direct guidance for the port: do not resolve
  eligible-and-untested straight to true. Full detail:
  `scratchpad/mission_block_seatpick_vehiclegroup_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits.
- **2026-10-10 — `0x00bc5610` mislabel corrected in this file (2 of 8 project-wide, remainder in
  `spec-lua-api-behaviour.md`)** (SPEC TEAM, agent `afec7bca7d3a1994e`). §2.7's `0x005b2170` row
  handler and §4.1's evidence bullet 17 both called it a general "cinema editor (not) active"
  test, overstating its scope: it is actually `0x029443bc==2||3`, the machinima recorder's
  stopping/finalizing predicate (see `spec-lua-api-behaviour.md` §3.7's correction) — false both
  when idle AND throughout an entire in-progress recording, true only in the brief stop/finalize
  window. Full detail: `scratchpad/bc5610_mislabel_correction_2026-10-10.md`. Cleanroom
  self-check: strict regex 0 hits.
