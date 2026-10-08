# Saints Row: The Third — Player Runtime: Input Query Contract and Control-State Machine

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** player runtime, units **U0** (input query contract) and **U1** (control-state machine) of the proposed order in `review/scoping-player.md` §5. This is the first spec file for the player system; later units (U2 normal-condition movement, U3 locomotion bridge, U4 follow camera, …) are expected to be appended here as further sections. **U2 was added as §3 on 2026-10-03** (the OPEN items and clean-room check moved to §4 and §5). **U4 was added as §4 on 2026-10-03** (the OPEN items and clean-room check moved to §5 and §6). **U3 was added as §5 on 2026-10-03** (the OPEN items and clean-room check moved to §6 and §7). **U6.6 was added as §6 on 2026-10-03** (the OPEN items and clean-room check moved to §7 and §8). **U6.0 and U6.4 (the first and fifth sub-units of U6, `review/scoping-player.md` §5a.3) were added as §7 and §8 on 2026-10-03, after U6.6** (the OPEN items and clean-room check moved to §9 and §10). **U6.1 (the element-selection rule, `review/scoping-player.md` §5a.3) was added as §9 on 2026-10-04** (the OPEN items and clean-room check moved to §10 and §11).
**Scope:** everything between the engine library's device block and the ~180 gameplay functions that ask "is this action pressed / held / how far is this axis deflected": the per-frame latch that copies device state into the game's control context at `0x01411b18`, the full layout of that context, the two runtime binding tables (`0x0140ff98`, 164 × `0x20`; `0x01411418`, 34 × `0x34`) and the step that fills them from `control_schemes.xtbl` / `control_binding_sets.xtbl`, the complete query API with its signatures, return values, "out" codes, dead-zone and range handling, and the input-consumption model; then the 8-row control-state machine at `0x012e6bb8` with its request stack, dispatch, push/pop/reset, and the row handlers' entry points. It does **not** specify what the per-frame player controls *do* with the input (that is U2 onward; only the dispatch skeleton of state 2's handler is recorded here, §2.7), nor the engine library's device drivers below the device block (XInput/DirectInput/raw-input code — only the *semantics* the context receives from them are pinned down, §1.3).
**Method:** Ghidra-free. Direct reads of `SaintsRowTheThird.exe`'s bytes through `tools/harnesses/exe_va.py`, a private recursive-descent capstone disassembler (jump tables followed by hand from their static targets), a raw `E8`/`E9` rel32 scan of `.text` for every call site of every function cited, a raw dword scan of `.text` for every data reference to every global cited, push-argument surveys at call sites, and static reads of the data tables (`0x012e6bb8`, `0x0111f1d0`, `0x012e6c58`, the CBA/CAA/stick/mouse vocabulary tables). Every function named in §1–§2 was read in full in its own listing unless marked otherwise. No Ghidra project was opened or copied, so none needed deleting. No `.czn_pc` data was touched; nothing in U0/U1 reached it.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear anywhere below; behaviour is described in prose and tables, and every function, global and field is cited by its plain hex address or offset. Action, axis, key, mouse-button, stick and scheme names (`CBA_OFC_SPRINT`, `CAA_CAMERA_ROTATE`, `LEFT_STICK_DIR_UP`, `MOUSE MIDDLE`, `General Controls`, …) are the engine's own strings read from its own static tables, quoted as data — the same convention every other spec in this project applies. Win32 import names (`ClipCursor`, `IsIconic`, `GetClientRect`) are read from the PE import table.
**Confidence key:** **CONFIRMED** (read directly in a listing, a static table read, or an exhaustive reference scan this pass); **HIGH CONFIDENCE** (follows from structure, call shape or a vocabulary match, but not traced end to end); **HYPOTHESIS** (plausible, unconfirmed); **OPEN** (collected in §10, with a next step where one exists).
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
- The dead-zone and threshold *values* are written by engine-library code outside the poll that this pass did not locate (§10, OPEN 1).

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
- **`0x005b2170`** (rows 0/1 per-frame; not a defined function in the shared project): if a cutscene is in progress (`0x00720640`), consume-pressed `CBA_SKIP_MODAL_SEQUENCE` (`0x8e`) and, if it fired, tail-call the skip request `0x00720830`; done. Otherwise: camera pre-update `0x00565c60`, `0x0056b220(−1.0)`; when a local player exists and the cinema editor (`0x00bc5610`) is not active, call the movement routine `0x005aee30` with the player's own position (`+0x40`/`+0x44`/`+0x48`) and 0 — i.e. no new intent — then the stance resets `0x00985df0(player, 0, 1, 1)` and `0x009fef60`, and if `0x00611650(player)` is true copy "consume-pressed `CBA_MENU_DP_DOWN` (`0x71`)" into bit `0x08` of `0x0140e684`. Finally, if the cinema editor is active and `0x00707490()` is true, tail-call the camera update `0x00566a40`. **CONFIRMED.**
- **`0x005ac4f0`** (row 2 exit): if `0x007f0130()` then `0x007f0140(1)`. HYPOTHESIS: closes an on-screen prompt layer.
- **`0x005b7430`** (row 2 per-frame, 248 instructions) — the skeleton, **CONFIRMED** by full listing, consistent with `review/scoping-player.md` §1.3 with these additions:
  - it clears byte `0x0140e6e5` first, and maintains two flags: `0x0140e6f8` "**controls ran this frame**" (set after the paused check, cleared by every early-out branch; read by the secondary hook) and a local "**run the camera hook**" flag;
  - paused path (`0x00707490` or `0x00831f70`): cutscene-skip test as above, camera defaults `0x0056b220(−1.0)`/`0x0056b3b0(1.0)` when `0x00831f70` is true, clear `0x0140e6f8`, return;
  - pre-hook `[0x0111f1d0 + 12·[0x012fd5c8]]` with the local player, unguarded (§2.8);
  - gates in order: menu `0x007c1ec0` (zero the six context words, clear latched, zero the four control floats, movement routine with own position) → dead `0x0096f4f0` (non-terminal) → camera mode 3 → cinema editor → cutscene (skip via `0x8e`) → script-controlled (`+0x28ac` bit `0x02`: when context bit 1 is set, copy "consume-pressed `CBA_MENU_DP_DOWN`" into `0x0140e684` bit `0x08`; when rig flag `0x10` is set, zero `+0xa50`…`+0xa5c`, `+0xa60`, `+0xa68`) → controls disabled (`+0x1d99` bit `0x08`: movement routine with own position) → otherwise, unless rig flag `0x1000000`, the action dispatch `0x005b73c0`;
  - tail: return if `0x012fd5c8` is −1; camera hook `[… + 8]` when the local flag is set and the slot is non-null, else `0x0056b220(−1.0)`/`0x0056b3b0(1.0)`; camera pre-update `0x00565c60` unless paused; post-hook `[… + 4]` only if `0x0140e6f8` is still set.
- **`0x005b69c0`** (row 2 secondary): if `0x0140e6f8` is set, run the late context dispatch `0x005b6420` — `0x005ae280` when `0x009b91e0(player)`; else context bit 2 on foot with no vehicle and no rig flag → `0x005ae3f0` (reads `CBA_OFC_ATTACK_PRIMARY`/`SECONDARY` analog values against 0.4); bit 4 in vehicle → `0x005b4e60`; **bit 8 → `0x005ac250`, which reads `CBA_SWC_EXIT_SATELLITE_MODE`, `CBA_MENU_ABORT`, `CBA_SWC_FIRE_GUIDED` and `CBA_SWC_FIRE_FREE`** — so the scoping note's unnamed "third context" (context bit 8) is the **satellite-weapon control context** (HIGH CONFIDENCE from the action names); otherwise `0x009fef60`. If the flag is clear: `0x007f0130`/`0x007f0140(1)` as in the exit, then `0x009da750`. **CONFIRMED** for the dispatch.
- **`0x005c2800`** (row 7 per-frame, 412 instructions; listed, not read line by line): builds the camera orientation (`0x013c8790`…`0x013c87b0`) and position (`0x013c8750`) itself and, under a mode-3 / pause test, calls the camera update `0x00566a40`. HYPOTHESIS (unchanged from scoping): a free/debug camera.

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
| `+0x2074` | small flag word. Bit 1 gates the `CBA_GAC_ACTION` stage of the prompt routine (`0x005b70c6`); bit 4 gates `CBA_GAC_RECRUIT_DISMISS` in `0x005ad830` and is also read at `0x0083d8aa`; bit 2 has no reader found by displacement scan (§10, OPEN 14) |
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
| `0x2`, `0x8`, `0x8000`, `0x200000`, `0x400000` | — | not attributed: whole-word readers at `0x005ae777`, `0x005aead5`, `0x005b6eac`, `0x005b7018` were not resolved bit by bit (§10, OPEN 14) |

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

The two setters: **`0x0056b100(x)` stores x into `0x013c8990` if `0x005be940()` returns 1, else −x; `0x0056b120(y)` stores −y into `0x013c8994` if `0x005be920()` returns 1, else y.** So the yaw input is +rot normally and −rot with the first option; the pitch input is +elev normally and −elev with the second. `0x005be940`/`0x005be920` choose bytes `0x01411b15`/`0x01411b14` while the camera is mouse-driven (`+0x9f6`) and bits 1/2 of `0x01411b00` otherwise (§1.12.2). **This confirms U0's HYPOTHESIS as behaviour: those options are horizontal and vertical look inversion, one pair per device family** (the options-menu labels are still unread — §10, OPEN 2).

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
4. Timers: controller flag 4 → `+0x2068` set already expired. **While s > 0.8, timer `+0x206c` alternates each frame between "started now" and "unset"** (`0x005aef9f`…`0x005aefb3`; §10, OPEN 12). `+0xd1c` (current speed, `spec-anim-runtime.md` §4.2 row 20) > 0.7 (float) → `+0x2068` = now + 500 ms.
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
- **3:** `0x00564cd0` pushes camera mode 3 and control state 7 (§2.6) and `0x00564d40` pops both; their only entries are tail jumps from `0x0057b660` (taken when the global `0x029cdb88` is non-zero and the mode is not already 3) and `0x0057b680` (when it is 3). Neither of those two has a direct caller or a dword reference (whole-image scans), so they are reached through a table this pass did not find — HYPOTHESIS: a debug command, `0x029cdb88` being a development switch (233 readers; written only by `0x00d9f250`). This advances §10 OPEN 6.
- **4:** pushed at `0x005b25d7` inside condition row 13's look slot `0x005b2540` (row 13 is the satellite condition, §3.9) right after `0x00b6fb90` initialises the satellite controller `0x0130eee8`; popped from the controller's teardowns `0x00b700d0` (`0x00b70160`) and `0x00b709d0` (`0x00b70b5b`; `satellite_weapon_mode_exit`, `spec-lua-api-behaviour.md` §12). `0x005b2540`, `0x00b700d0`, `0x00b709d0` and `0x00b70d10` write the working outputs themselves, which is why the row is empty; the streaming focus uses the player's own position in this mode (scoping §1.6).
- **6:** pushed by `0x00579ef0` (at `0x0057a2fc`; callers `0x00a27dfc`, `0x00a469ac` in the Lua band) and by the small wrapper `0x00579ec0` (at `0x00579edb`, right after calling the path builder `0x00578d40`, which sums an array of segment durations into a timed path; the wrapper has no direct caller or dword reference); popped by `camera_end_look_through` `0x00a44140` (`0x00a44280`, `spec-lua-api-behaviour.md` §13.16, whose "`0x00564910(6)`" this resolves) and `0x00a27c90` (`0x00a27cd5`), each right after `0x0056cc30`.
- **9:** pushed by `0x00702810` (uses `MISSION_FAILURE_DIED_TEXT`; caller `0x009504bf`) and by the local-player branch of `0x0098ce40` (`0x0098d592`); never popped — the camera reset clears it at respawn. The enter stores the normalised horizontal offset from the local player to the previous camera position (`0x013c8800`) in `0x013c9240`.
- **11:** `0x00580260(object, …, position, rows, …, flag)` saves the current base in `0x013cba50` and sets base 11 (with a line-of-sight query `0x0075d7e0` first when its last argument is 1). Callers: AI mode 43's tick `0x004f1440` (`0x004f21fa`, passing the current working position `0x013c8750` and rows `0x013c8790`), the cutscene manager (`0x007291fa`, `0x0072b6df`) and `0x006bbbe0`. The same AI tick restores the saved base with `0x0057ef00` (`0x004f1526`) next to releasing the HUD element `0x012e1b88`. The update can return to the follow camera by itself (`0x0056b370` at `0x0058021b`).
- **13:** `0x005674d0(1)` from `0x0083ac4b` and `0x005674d0(0)` from `0x0083a89f`, both in the gang-customization UI code (`spec-lua-api-behaviour.md` §53.1's registrar `0x0083a900`).
- **15:** `0x0057eb10(position, rows, …)` from the stronghold-unlock helper `0x005f1cc0` (`0x005f1fa0`, `spec-lua-api-behaviour.md` crib unlock), `0x008123de` and `0x00a035cc`; restored by `0x0057ea90` (`0x005f0b0a`, `0x008127d0`, `0x00a00653`) and `0x0057eae0` (`0x00811f26`), each gated by `0x0057ead0`.
- **16:** `0x00567f80` has 16 callers (`0x005fa9af`…`0x008206a3`), all but two in the store/customization UI band `0x0080c000`–`0x00821000`; its init calls `0x0080c470` and `0x0080b920` in the same band.
- **17:** `0x00bc6a70`/`0x00bc6160` are in the cinema-editor band (`0x00bc5610` is the editor-active test of §2.7).

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

**`0x00577cb0(step)`** (82 instructions; CONFIRMED): unless the local player has `+0x1d95` bit `0x10` (scope view), `0x00994f60(player)` holds, `0x00614d00()` is 5 or `0x013c9363` is set, set the base FOV `0x013c8a44` to the blended `base_fov`. Run the core — or, when the pre-update set `0x013c9388`, skip it once and clear the flag. Then a surface clamp: `0x0079a420` on the working position returns two heights; if the camera is below their sum + 0.75 and less than 25 below it, it is raised to that level (HYPOTHESIS: water surface). Then `0x0056ef10`, zero the look input, move the swing request to "last frame" and reset it to 1.0, and reset the swing rate to `default_swing_rate`.

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
- **This file, §2.7 / §10 OPEN 6:** control state 7's camera is debug mode 3, entered only through `0x0057b660`, gated by `0x029cdb88` (§4.1).

---

## 5. U3 — The locomotion bridge

**Scope.** Everything between the intent fields U2 writes (`+0x2048`, `+0x203c`, `+0xa50`, `+0xa60`, `+0xa68`) and a moved capsule: which human field the character controller is actually driven by, who composes it, how the controller hands it to the Havok-side character proxy, what comes back (velocity, position, support, contact events), and when each step runs in the frame. The traditional U3 scope of `review/scoping-player.md` §5 — `0x00958690`'s reading of the intent fields into the named control parameters — is `spec-anim-runtime.md` §4.2 and is cited, not re-derived (§5.3). Havok's own step, solve and integrate code is out of scope; the Havok side of the boundary is `spec-physics-runtime.md` §2 and §4, cited by row. The game's custom proxy passes (`0x0079b000`–`0x0079e000`) and the controller's motion routine `0x007a5b10` are recorded at the contract level only; their internals belong to the physics character-controller unit (P5, `spec-physics-runtime.md` §2 preamble).
**Method.** As U0–U4: Ghidra-free. Capstone recursive-descent listings over `exe_va.py` — `0x0099ebe0` (1482 with the one jump-table case at `0x0099efbf` added by hand), `0x00961d70` (980) and `0x007a5760` (239) match the scoping census; `0x0099d1b0` (1022), `0x009a02a0` (593), `0x009989b0` (430), `0x0099ae30` (423), `0x00985df0` (409), `0x0099e2a0` (336), `0x009918b0` (330), `0x007a52c0` (319), `0x0099b430` (285), `0x0095f5b0` (194), `0x00997b50` (172), `0x0079e900` (39), `0x0099e7e0` (25) and the small accessors were read in full; `0x007a5b10` (1467), `0x007a2960` (578), `0x0079de40` (536), `0x004c58c0` (324), `0x007a45f0` (288), `0x007a4bc0` (226), `0x007a2170` (201), `0x0079f970` (156) and the replay handlers `0x007a37f0`/`0x00962c20`/`0x007a3fa0`/`0x007a2470`/`0x007a3640` were read for structure, call shape and their boundary fields only. A full linear sweep of `.text` for displacement scans of every human field named below, raw `E8`/`E9` rel32 and whole-image dword scans for every function and global cited, an RTTI walk of the four character classes' vtables, and `.rdata` constant reads. No Ghidra project was opened. No `.czn_pc` data was touched.

### 5.0 Results up front (U3)

1. **The scoping note's chain is not the locomotion bridge [corrects scoping §1.0, §1.4, §2 and this unit's brief].** `0x0099ebe0` is the **post-move per-human pass** (position sync from the proxy, water, death, attachments) and reads none of the intent fields; `0x007a5760` is **not the character controller** but a parameterless **flush of two deferred contact-event queues**; and `0x0079e900` → `0x00961d70` is **the replay of one queued "character touched a vehicle" event** — vehicle-hits-pedestrian damage, knock-down and AI events, plus the upgraded-sprint ram that pushes the car — run once per frame after all movement, never per sub-step, and writing **no velocity or displacement** to the proxy (§5.2, §5.8).
2. **The real bridge** (CONFIRMED by listing of the world update, §5.1): controls write intent (U2, `0x00702ad4`) → AI and per-human updates → animation jobs, including `0x00958690` and the controller update (`0x00702b9f`) → **velocity composer** `0x009989b0` (`0x00702bb7`) → `0x009a02a0` → **`0x0099d1b0`**, which writes the controller's desired velocity `+0x1cec` → Havok world step `0x0075ac20` (`0x00702c46`), which does **not** move characters → **character-controller step `0x0099e7e0`** (`0x00702c55`): gather `0x0099ae30`, step `0x0099b430` (the controller's motion routine `0x007a5b10` sets the proxy velocity, then at most ten lock-step integrate passes), write-back `0x0099e2a0` (`+0xd04` velocity, `+0xd1c` speed, landing and falling) → `0x00998850` → … → `0x0099ebe0` (`0x00702c9b`): position `+0x40` ← proxy, then the contact flush `0x007a5760`.
3. **The contract is one vector.** The character controller is driven only by human `+0x1cec`/`+0x1cf0`/`+0x1cf4`, the **controller desired velocity** (world space, units/s), recomposed every frame by `0x0099d1b0`. For the player on foot it is **`+0xa50` · `+0xa60` + R·E/Δt**, where `+0xa50` = unit(r) and `+0xa60` = k·\|r\| (k = `+0xa64`, 1.0 unless scripted) — i.e. **k · `+0x203c`**, U2's smoothed vector — plus the rotated per-frame translation E of the non-locomotion animation layers; **sprinting replaces the direction by the facing axis `+0x64`**. **The desired-move block `+0x2048` (D) is not read anywhere in the bridge**; it only feeds the animation parameters `desired speed` and `desired direction` (`spec-anim-runtime.md` §4.2 rows 6, 21b, 21c). (§5.4.)
4. **Root motion versus velocity — answered from the movement side** (the open question of `review/scoping-animation.md` §1.7). Horizontal motion is **velocity-driven**: the speed is `+0xa60`, and the root translation T of the phase-synchronised (cyclic) animation layers — the locomotion cycles — is used only to adapt the animation playback rate `+0x1a90` (clamped to [0.1, 1.5]) so the stride matches the speed. It is **root-motion-driven** only while the animation controller's flag `0x100` is set — i.e. while a playing animation record carries `Anim_Movement` (then the velocity is R·T/Δt). The translation E of the non-synchronised layers (non-looping graph clips) is **always** added — that is how climbs and vaults move the body vertically. Orientation is animation-driven and applied before physics; the capsule never rotates (§5.4.3, §5.4.5).
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

§4.2's note that "the sources of those fields are U3's subject": position `+0x40` comes from the proxy (`0x0096e710`, §5.7), the orientation axes `+0x4c`/`+0x58`/`+0x64` from the animation's root rotation (§5.4.1 step 3), `+0xd04`…`+0xd1c` from the proxy (§5.6.6); the movement flags `+0xd40` were not traced (OPEN 38).

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

The pass functions are the custom proxy's own cast-and-solve machinery (`0x0079b000`–`0x0079e000`), which calls into Havok range A (`0x00c83xxx`); they are P5 material, read here only for call shape (OPEN 36). `0x00702760` (→ `0x005aa170`, a control-band timer tick) also runs inside this routine and is unrelated.

#### 5.6.5 Controller motion `0x007a5b10` (1467 instructions) — contract CONFIRMED as call shape; internals P5 (OPEN 35)

`thiscall` on the controller, `ret 0x1c`: (human, &velocity, float, Δt, b1, b2, b3). It clears state bit `0x80` and `+0x10`, and reads the proxy velocity and position (`0x00c835b0`, `0x00c83570`). **Unless b1, or b2 and b3, are set, it skips the support query and treats the human as supported on an up normal (0, 1, 0)**; otherwise `0x0079d0b0` (the proxy's support check) returns the support state, surface normal and surface velocity, and a surface whose normal · up is below 0.7 has the into-slope part of the desired velocity removed (written back into the record's copy), below 0.55 also setting state bit `0x80` ("too steep"). The rest is the controller's own state logic (timers `+0x154`/`+0x158`/`+0x15c`, `+0x160`, the cap `+0x170`, ray queries through `0x0075d7e0`), and it **ends by setting the proxy's linear velocity** (`0x00c835c0`, called at `0x007a73a5`). It reads no intent field of the human. Its two other callers (`0x007a7413`, `0x007a750e`) are outside this frame path and were not traced.

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
2. **Local player:** `0x00574e30(0.0, 0)` (camera band). Also `0x007f0140(1)` (§10, OPEN 5), and `0x009dcf10` when in fine aim (`0x009da830`).
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
3. **Eligibility** `0x00b0ca60(character, forced, request +0x24 bit 0)`, forced = instant or request `+0x23` bit 3. Refused → return false.
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

The band sweep found no other store to `+0x16d4` (the four scoping §5a.1 item 6 listed). Readers outside the band treat `+0x16d4` = 3 as seated (`0x009b9160`) and 1/2 as "entering or exiting" (`0x009b91e0`). **Value 2 means exiting, not mid-entry**, as scoping §5a.1 item 6 said; this refines `spec-lua-api-behaviour.md` §21.11 and §22.3.

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
6. **The camera hand-off reads the vehicle's camera-angle sets.** `0x00b0bc10` passes `Primary_Camera_Angle` (or `RC_Camera_Angle` for a remote-controlled vehicle) and `Secondary_Camera_Angle` (descriptor `+0x628`/`+0x668`, `+0x648`) to the camera band's `0x0056cf60`/`0x0056cd10`. **This answers the core of §10 OPEN 28 and scoping OPEN 8** (§8.6).
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
34. Human `+0xe4` bit 4 and `0x00ad3820`: `0x008ccb90(human, 0, 1, 1)`. The seat's `Hide Occupant` flag (seat byte `+0x09` bit 6): `0x00920170(human, 0)` and slot `+0x58` |= 8. State `0x15` undoes it with `0x00920170(human, 1)` (§6.1).
35. Slot `+0xf4` timer; human `+0xe4` bit 4 (not owned); forced flag call.
36. Human `+0xe4` |= `0x1000`, `+0xec` bit `0x100000` cleared, `0x00bd8240(human, vehicle)`, human `+0x19bc` = −1.

**Callers.**
- The setter's case 9 (`0x00afe7b2`) follows the commit with the network send, gated on *flag* and phase 3. After step 33 the phase is 2, so no seat snapshot is sent then.
- The per-frame handlers of states 7 and `0x13` call the commit directly (`0x00affd47`, `0x00b01aa6`). Each first logs `"Set Enter"` with state 9 and then repeats the send itself — an inlined copy of the setter's case 9.

### 8.3 The vehicle-band callees

The census lists **39** distinct callees in `0x00a70000`–`0x00af0000`, which confirms scoping §5a.2's count.
- **Predicates, 18:** class `0x00ad2f80`; seat count `0x00ab4af0`; seat occupants `0x00ab5070`/`0x00ab5150`; remote control `0x00a77a10`; type tests `0x00ad3140`, `0x00ad3160`, `0x00ad31d0`, `0x00ad3200`, `0x00ad3220`, `0x00ad3270`, `0x00ad3290`, `0x00ad32d0`, `0x00ad33a0`, `0x00ad3590`, `0x00ad3820`, `0x00ab6cd0`, `0x00aef8b0`.
- **The other 21 write the vehicle state** listed in §8.1:
  - `0x00ac2180`, `0x00ad5460`, `0x00ab6cf0`, `0x00ad35b0`, `0x00ab48f0`, `0x00ab4680`, `0x00aa2310`;
  - the door group `0x00aa2080`, `0x00a88e70`, `0x00a88f00`, `0x00a88eb0`, `0x00aa1f60`;
  - `0x00ab9c20`, `0x00ab9ea0`, `0x00abcd50`, `0x00a90c70`, `0x00aa9350`;
  - the force flags `0x00a7e040`, `0x00a7b690`, `0x00a7b520`;
  - and `0x00a794a0`, the HUD lookup.
- **Outside that band,** the vehicle-AI object takes seven more: `0x00b2baf0`, `0x00b2e510`, `0x00b24cc0`, `0x00b25c40`, `0x00b29cf0`, `0x00b4d260` and its siblings, and `0x0092cdb0` (all identified by their `vehicle_ai` force-flag strings).

**The exit finaliser `0x00b03d80` (state `0x19`) calls the same routines** (§10, OPEN 53): `0x00ab9c20`, `0x00abcd50`, `0x00ad35b0`, `0x00ad3590`/`0x0094f3e0`, the door group, the force flags, `0x00607ea0` (with (3, −1, 2) against the commit's (3, 5, 2)), `0x00b85fa0`, and `0x00ac2280` beside the commit's `0x00ac2180`. HIGH that these are the commit's undo pairs. U6.6 should check them against this list.

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
   - These are the three sets §4.12 found unreferenced by the camera band itself (§10 OPEN 28).
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
- **This file, §4.12 and §10 OPEN 28:** the camera-angle sets are read by the vehicle-interaction hand-off `0x00b0bc10` (§8.6). §4.12's "not referenced by the camera band" stays true of the camera band itself. **§4.8:** the hand-off's class table maps class 2 to the airplane submodes, 3/4 to the helicopter ones and 5 to the watercraft ones (§8.6 step 3). **§10 OPEN 39:** the commit clears animation-controller `+0x1a4` bits `0x0800`–`0x4000` through `0x004b7f70`. **§10 OPEN 17 (drunk effect):** the driver's drunkenness is applied to the vehicle as the `drunk_driver` multipliers (§8.1).
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

## ~~3.~~ ~~4.~~ ~~5.~~ ~~6.~~ ~~7.~~ ~~8.~~ ~~9.~~ 10. OPEN items

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
13. **The `0x009573d0` acceleration shaping** (§3.5.4 step 14) and the facing override `+0xa6c`/`+0xa7c`/`+0xa70`/`+0xa80` (step 19): structure CONFIRMED, the predicates' meaning (HYPOTHESIS: aiming/strafing) and the field names are open.
14. **Unattributed context bits:** `+0x2074` bit 2; `+0x2078` bits `0x2`, `0x8`, `0x8000`, `0x200000`, `0x400000`; and the meaning of `+0x2084` entries other than 15 (the 22-entry arrays at `0x0111f2d0`, `0x0111f3d8` and `0x0111f430`). *Next step:* resolve the whole-word readers listed in §3.2.3 and read the prompt arrays (U6/U8).
15. **Stance bits outside U2** (§3.7): `+0x1d94` bits `0x01`, `0x02`, `0x04`, `0x08`; `+0x1d95` bits `0x04`, `0x08`, `0x20`, `0x80`; `+0x1d97` bits `0x01`, `0x02`, `0x04`, `0x40`, `0x80`. Writers are listed; none was traced.
16. **The dormant pivot branch** `0x005ad510` (§3.5.6) — recorded at structure level; dormant because `0x0098d6d0` is unreferenced (raw scan). An indirect call through a computed pointer cannot be excluded by a reference scan.
17. **Drunk/weed effect** (`0x009758b0`, `0x00975ce0`, `0x009786c0`): which `drunk_levels.xtbl` field feeds which global (`0x02625bcc`, `0x02625bd0`, `0x02625c04`, `0x02625c3c`, the delay line `0x02625c68`) was not traced, and the effect-state machine `0x02626520` (state 3 freezes movement) was not named.
18. **Second writer of `+0x2048`**, `0x008a3ca0` (§3.5.7): HYPOTHESIS that it is the network-driven remote-player update; its flag argument to `0x005b31f0` and its caller `0x008a5061` were not traced. **[Advanced by §5.5: the routine also calls the move-target setter `0x00987360` (`0x008a4075`) and the locomotion-state update `0x00985df0` (`0x008a408a`), as the player's move slot does.]**
19. **`0x00985df0`** (the locomotion-state update called after the writer, maintaining `+0xccc` and the ramp timer `+0x2058`) was read at its head only — U3. **[Resolved by §5.5.]**
20. **`0x005ad9a0`'s full truth table** (§3.6.5) and the prompt/QTE routines `0x005b6af0`/`0x0060d960` (§3.3) were read for routing and input surface, not tabulated.
21. **Activity/mission masks:** the per-activity cases of `0x005b3c50` (jump table `0x005b3d0c`) and the mission types `0xa`/`0xe` of `0x005ab330` edit the masks (§3.2.4), but which activities and missions those numbers are was not resolved.
22. **Two image values the controls depend on:** `0x012e6ced` = 1 (bypasses the crouch stick test, §3.6.3) has no writer in `.text`; `0x012ec730` = 0 in the image would **invert both walk axes** (§3.5.2 step 4) until the mission-16 reset `0x005e5e40` (nine call sites, raw scan, including `0x00702850` in the world band) or a mission-16 start writes it — whether that reset always runs before free-roam play was not traced. *Next step:* read both from a running process.
23. **Camera mode 11** (§4.1): callers and the base save/restore are CONFIRMED; whether its update `0x0057ff10` re-aims at the object every frame, and when it falls back to the follow camera (`0x0056b370` at `0x0058021b`), is HYPOTHESIS. *Next step:* full listing of `0x0057f7b0`/`0x0057ff10` and `0x00580260`.
24. **Bodies of the other camera modes** — look-through `0x0057a490` with the path builder `0x00578d40`; death `0x00569a60`/`0x00569c60`; gang customization `0x005676e0`; showcase `0x0057ee90`/`0x0057ece0`; store/customization `0x005681f0`/`0x005683c0`; cinema editor `0x00bca9b0`; the satellite writers `0x005b2540`, `0x00b700d0`, `0x00b709d0`, `0x00b70d10`; debug `0x005c2800`. Selection is specified (§4.1–§4.2); the bodies are U8, on demand.
25. **Camera mode 12** (`0x0057e7d0`, called from `0x007b6f30` ← `0x00707753`, `0x007ae43f`): its update is the stub, so where its transform comes from, and what the UI owner is, were not traced.
26. **HUD screens `0x35`…`0x39`** (§4.3) that keep the camera updating twice, or at all while paused: the ids are registry `+0x30` values in `0x012fced8`; their names were not resolved. *Next step:* map the registry entries' ids to their screen names.
27. **Look-speed and FOV factors:** `0x00975770`/`0x00975810` (minimum of `0x02625bc4`/`0x02625bfc`/`0x02625c34`, resp. `0x02625bc8`/`0x02625c00`/`0x02625c38`; mouse scales `0x0130a0b4`/`0x0130a0b8`; `0x009785a0`/`0x009785c0`) and `0x00975bd0` (FOV): writers and option names not traced (HYPOTHESIS: aim slow-downs and the look-sensitivity option).
28. **Vehicle camera-angle sets and presets** (§4.12): no reader of the record's `+0x628`/`+0x648`/`+0x668` sets was found in the camera band; the preset placer `0x0056d390` with the blocks `+0x688`/`+0x6a4` (chosen by `0x00ad3ae0`/`0x00ad3b10`) and sub-state 2's handler `0x00571680` were not read; `0x00a77120` (HYPOTHESIS: `Max_FOV`) and `0x00a77170` (distance factor) were not read. Scoping OPEN 8 is therefore only partly answered. *Next step:* a whole-image displacement scan for `+0x628`…`+0x687` restricted to code that loads the record through `+0xbf4`. **[Advanced by §8.6: the vehicle-interaction hand-off `0x00b0bc10` reads all three sets, passing Primary (or RC) to `0x0056cf60` and Secondary to `0x0056cd10`; the preset blocks `+0x688`/`+0x6a4` and sub-state 2 are still unread.]**
29. **Lateral-offset re-add** at the start of the core (§4.7 step 1): last frame's shoulder offset is added to the working position again before the position is recomputed; behaviour CONFIRMED, purpose unknown (only the intermediate reads see it).
30. **Follow sub-states 1–5 and the human submode chooser:** the six handlers (`0x005710b0`, `0x0056c450`, `0x00571680`, `0x00573b10`, `0x00571b10`, `0x00571e60`) and their setters are named (§4.7 step 6) but were read for structure only; the human chooser `0x005745c0` (370 instructions — the on-foot counterpart of `0x00573110`, deciding exterior/interior close, sprint, crouch, swimming, fine aim, ragdoll, …) was not tabulated.
31. **Sway amplitudes** `0x013c936c`/`0x013c9370` (§4.7 step 9): their writers were not traced (HYPOTHESIS: impacts and explosions).
32. **Look-at assist** (§4.7 step 12): who starts the timer `0x012e41a0` and sets the object `0x013c92e8`/`0x013c92ec`, and what the 20-pass refinement `0x0056e170` converges to, were not traced.
33. **Camera collision filter** `0x00566070` (407 instructions) and the skipped hit kinds 1, 4, 8 (§4.9): which objects block the camera is not tabulated.
34. **Small unknowns around the follow update:** the surface clamp `0x0079a420` (HYPOTHESIS: water surface), `0x0056ef10`, `0x0058b470` → `0x00a1f880` after the update, the shake `0x0057c930`, transition-blend type 1 `0x00580430`, the pitch return `0x0056bfe0` (uses `pitch_reset_time`), and the hill-tracking step `0x0057ad60`.
35. **Controller motion `0x007a5b10`** (§5.6.5): the state logic between the support query and the final velocity set — gravity, jumping, the timers `+0x154`/`+0x158`/`+0x15c`, `+0x160`, the cap `+0x170`, state bits `0x02`/`0x08`/`0x10`/`0x20`/`0x40` — and its two other callers `0x007a7413`/`0x007a750e`. Physics unit P5. *Next step:* full listing together with the support-state values `0x0079d0b0` returns.
36. **The custom proxy passes** (§5.6.4): `0x0079bb00`, `0x0079dd50` → `0x0079c5f0`, `0x0079ddc0` → `0x0079d7c0`, `0x0079b560`, `0x0079bbb0`, `0x0079b5b0`, the cast-job submitter `0x0079b470` and the manifold update `0x0079de40`, and on which thread the listener callbacks run (the queue adders take no lock) — the character case of `spec-physics-runtime.md` §6 OPEN 5. P5.
37. **`turn angle` (`+0xaac`)** (§5.3): `0x00956970` chases a target computed inside the animation job `0x00994970` (call at `0x00994a3d`); whether that target is U2's `+0xa68` was not traced.
38. **Movement flags `+0xd40`** (`spec-anim-runtime.md` §4.2 rows 8–19): writers not traced.
39. **Animation-side inputs of the composer** (§5.4.3). **[Partly resolved 2026-10-03, §5.4.3 "Step 3 in detail".]** ~~the slot flag `+0x80` bit 0 that separates T from E (HYPOTHESIS: `Anim_Movement`)~~ — the discriminator is **animation-instance layer flag bit `0x01`** (layer `+0x10`), the phase-synchronised group mark set by the layer-post API (`0x004c6590`) for plain clips, blend-tree `States` and looping graph clips and left clear for non-looping graph clips; `Anim_Movement` is refuted for this role. ~~the third vector X~~ — X is a translation (the non-synchronised deltas in their start-of-clip root frame, unweighted), not a rotation. ~~which animation states set the animation-controller flags `0x100` and `0x4`~~ — they are the record flags `Anim_Movement` and `Block_Translation` OR'd over every playing animation into controller `+0x198`, so the answer is per record in `anim_files.xtbl`. **Still open:** the orientation step `0x004b6530` (X's consumer), the speed `0x004b49e0`, `0x009987d0`'s state test, controller `+0x1a4` bits `0x40`/`0x100`/`0x800`, the full read of `0x004c8a80` (root-rotation naming HIGH), the override weight `0x004cad00`, and the layer creator at `0x004c6ebb`. Belongs with the root-motion unit of `review/scoping-animation.md` (U5). **[§8.1: the seating commit clears `+0x1a4` bits `0x0800`–`0x4000` through `0x004b7f70`.]**
40. **`+0xccd` values 2 and 3** (§5.4.3 step 5, §5.4.4): they route the player through `0x009918b0` at stance speed. Values 4 (jump/dive), 5 (climb) and 6/8 are named by `spec-anim-runtime.md` §4.2 and §3.2.2; 1 is "moving" (`0x00987360`); 2 and 3 are not identified.
41. **The gather's second NPC-LOD branch** (§5.6.3 (b)): as read it never engages, because `+0xaa8` is armed at 0 ms by the human constructor `0x00954340` (at `0x0095559d`) and is re-armed only on the branch's not-expired path. Behaviour HIGH; a port reproducing the shipped game should keep slow NPCs stepped. *Next step:* read `+0xaa8` on a live NPC.
42. **Contact-replay handlers** (§5.8): `0x007a37f0` (class bits `0x0a`/5), `0x007a2470` (human), `0x007a3fa0` (class bit `0x1e`), `0x007a3640` (class bit 6) and `0x00962c20` (human–human) were read for call shape only; the names of class bits 3, 5, 6, 8, `0x0a` and `0x1e` of the kind rows `0x02cc9900` are not established here.
43. **Ungated sprint reads** (§5.8, §5.4.3 step 9): `0x00961d70` and `0x0099d1b0` read the local pad's `CBA_OFC_SPRINT` for any human carrying the `+0x1c9c` upgrade bits; whether a remote co-op player can carry them (and would then react to the local pad) was not traced.
44. **`+0xa64`, the per-human speed multiplier** (§5.5): 1.0 at construction (`0x009544c5`); its writers `0x0062e294`, `0x006ebdc1` and `0x007d3073` were not traced (HYPOTHESIS: script and activity speed overrides).
45. **`+0x1c9c` bits**: `0x2000` (HIGH: standing on a moving support — it gates the NPC LOD, air control and landing, and adds `+0x1c60` to the velocity), `0x80000` and `0x100000` (the sprint and jump upgrades): writers not traced.
46. **Small unknowns in the bridge:** `0x0094a3b0(player, 0)` (§5.4.3 step 9); `0x00997b50`'s local-player checks `0x0098a3f0`/`0x00710cb0`; `0x0063c830`; the controller bit `+0x150` bit `0x02` behind `0x00942260`; the visibility test `0x0091f310(·, 2)`; the `+0xcd2` values; the pony-cart case's constant T (`0x0262d030` × Δt); and the second loop of `0x0099ebe0` (shark attack, drowning, cover exits) beyond structure.
47. **Exit-request fields read outside U6.6** (§6.2). These are:
    - the object handle `+0x08`/`+0x0c`;
    - the vector `+0x10`–`+0x1f` (HYPOTHESIS: the launch vector);
    - `+0x20`/`+0x24`;
    - the animation override `+0x2c`;
    - `+0x30` bits 4–7. Bit 7 is set only by the player's prompt `0x009e5e80`.

    *Next step:* U6.7 (the animated exit states read them). **[§9.5: `+0x20`/`+0x24` are the victim's extract kind (`0x0a`…`0x11`, or `0x13` = `extract fast`) and approach side, written by the hijack chooser `0x00afa540` and copied to slot `+0x10`/`+0x20` by `0x1d`'s on-entry `0x00b02900`.]**
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
58. **`vi_exit.xtbl` has no `Element` whose first token is `deactivate`, `open` or `close`** (§6.12 item 4), the first tokens of exit states `0x1a`, `0x1c` and `0x20`. For U6.7: either those states are never set, or they resolve their keys elsewhere. **[Answered by §7.5: `0x1a` and `0x20` have no on-entry handler and can never be held; `0x1c` has one but no static producer (OPEN 60).]** **[§9.5: none of the handlers of `0x1a`, `0x1c` or `0x20` reaches the element rule (census closure), so none of them would choose an `Element` even if held.]**
59. **Network side of exit** (§6.5, §6.6.3, §6.10): `0x00b03010`'s send conditions (it sends `0x15` only under the `0x008ae480` gate), the record builder `0x00af3630` (queue `0x028888e8`) and the applier's state handling are recorded at call-shape level. They belong to the parked network unit.
60. **Enter state 7, exit states `0x18` and `0x1c`** (§7.5): each has an on-entry handler that stores its number (7 and `0x1c` also have per-frame handlers), but no setter call site, chooser return or direct handler call produces them; only the network applier `0x00af2950` could, by replaying a remote value. For `0x18` this agrees with OPEN 52. *Next step:* check the applier's received values and any computed state in the network path; otherwise treat them as dormant, like `0x10`/`0x12`/`0x1a`/`0x1e`/`0x20`.
61. **Unnamed record and slot fields** (§7.1–§7.2): record `+0x08` (0/1/2, written only by the state-1 hub), slot `+0x28` and `+0x34`, the handles `+0x48`/`+0x50`, and request `+0x28`…`+0x34`. The exit fields are in §6.1/§6.2. *Next step:* the units that own those states.
62. **Commit side effects read for role only** (§8.1–§8.2): which vehicle type each tutorial predicate `0x00ad3140`…`0x00ad32d0` tests, and the meaning of the `0x005bc5d0` variant (§8.1); the report objects keyed 7/`0x16`/`0x17` (§8.2 step 19); `0x00bd8240`'s globals; `0x004e4130(human, 0x19)`; the vehicle-AI passenger block `0x00b47110`/`0x00b4d260`/`0x00b464c0`/`0x00b462a0`, `0x00b24cc0`, `0x00b25c40` and `0x00935130`; `0x00ab9c20`'s seat effect; `0x00aa91c0`; and the meaning of the human bits `+0xe0` `0x100`, `+0x3a` `0x80`, `+0xe4` `0x1000`, `+0xec` `0x100000`, `+0x1b08`/`+0x1b10`, `+0x19bc` and the vehicle bits `+0x16c0` `0x40`/`0x800`/`0x1000`/`0x4000`/`0x100000`, `+0x16c8` `0x08`.
63. **The six ride tweaks** `VI_forward_vel_accel`, `VI_vert_vel_accel`, `VI_random_accel_factor`, `VI_reaction_delay_min_ms`, `VI_reaction_delay_max_ms`, `VI_collision_min_vel_diff` (`0x0288ca88`…`0x0288ca9c`, §8.5) are bss with no initialiser in the image, so their shipped values come from tweak data that was not located; the readers of the lag values slot `+0xe0`/`+0xe8` were not traced (HIGH: U6.8's pose driver). *Next step:* find the tweak loader for the registrars `0x0071b580`/`0x0071b530`, or read the values from a running process.
64. **Inventory slots 0 and `0xe`** selected by the holster (§8.4), `0x00600720`'s other arguments, `0x005ffaf0`, and player `+0x28ac` bit 4 (which turns `0xe` into 0). HYPOTHESIS: 0 is empty-handed. *Next step:* the inventory unit.
65. **The character-controller hand-over at the commit** (`0x0094cca0(human, 0, 0)`, then `0x007a5a90` on `+0xcc0` with `0x00b0e9f0(vehicle)`): HYPOTHESIS that the controller is parked on the vehicle body while seated. P5 / U6.8.
66. **The count-bounded vehicle queries** `0x00af1770`/`0x00af17e0` (§7.9): defect CONFIRMED in code. Whether it is observable depends on whether a lower slot of a record can be released while a higher one stays occupied (likely with passengers leaving out of order). *Next step:* exercise it.
67. **Network replication of U6** (parked): the record's shadow `+0xea8`…`+0x163f`, the applier `0x00af2950`, the receiver `0x00af8810`, the queue `0x028888e8`, `0x00af3630`'s packet drop (§8.2 step 2), the ownership request `0x008addd0` (§7.6), and the send conditions of §7.7. Together with OPEN 59.
68. **The extract-kind override `0x0130e5f4`** (§9.4.3, §9.5): −1 in the image; 13 references, the writers at `0x00af9419`…`0x00af954f` (HIGH: debug commands). When it is not −1 the hijack chooser `0x00afa540` uses it as the extract kind, and the next-state chooser `0x00afb220` skips a class test when it is 9. Not traced.
69. **Variant-field writers outside this unit** (§9.3): no writer of exit-request `+0x31` bit 0 (the `convertible` exit variants) was found here. Request `+0x21` bits 2/3 (`traverse no door`), 4/5 (`open shady`/`shopper`) and 6 (`enter convertible`) come from the request's creator. The hub's `+0x1c` values, and `0x00b04ae0`'s register-written exit kinds (`0x14`, `0x19`, `0x1a`) and `exit fast` sub-variants (9/`0x0a`/`0x0b`), were not traced. *Next step:* U6.3 (hub), U6.5, U6.7.
70. **Dead and shadowed shipped `Element`s** (§9.6.2–§9.6.3): 5 `vi_exit` `Element`s no key can produce, and 4 shadowed alternatives with distinct animations (`Heli Fighter Rider` `Exit Left Fast`, `RiotCop_Right` Bash/Shoot/Punch). In the shipped build they never play through this rule. Whether they are authoring errors or meant for an older key layout is unknown. A port reproducing the original keeps them as inert data.
71. **The ride key** (§9.6.4): who builds the six-token key passed to `0x00b08b80` (callers `0x00b08ea0` and `0x00af4760`)? That decides whether all 454 `vi_ride` `Element`s are reachable, including the two whose token 0 is `none`, and how the fallback chain to animation ids `0x10a`…`0x111` is used. *Next step:* U6.4/U6.8.
72. **Guard quirks of the rule** (behaviour CONFIRMED):
    - short keys compare uninitialised stack words, and the discarded diagnostic indexes `0x0130e638` with them (§9.4.3);
    - an out-of-range seat indexes `Seat_Info` −1 (§9.4.1);
    - `0x00dab660(5, N+4)` divides by zero when the count N is 0 (§9.5).

    Only the non-hijack `0x1d` exit is known to reach the first in the shipped game. A port should zero-fill and guard all three.

---

## ~~4.~~ ~~5.~~ ~~6.~~ ~~7.~~ ~~8.~~ ~~9.~~ ~~10.~~ 11. Clean-room check

- Addresses and offsets are plain hex throughout. No decompiler output was read or quoted; only capstone listings and static table reads were used. No Ghidra auto-names for functions, globals, labels or variables appear.
- Names quoted are engine data: CBA/CAA action names, the stick/axis/mouse/gamepad vocabularies and scheme names read from the executable's own tables, the tweakable names `grenade_disable`/`grenade_disable_veh` read at `0x0111f708`/`0x0111f6f4`, and Win32 import names from the PE import table.
- U2 (§3) additionally quotes, as data read from the executable: tweak-variable names (`Player_max_run_speed`, `Player_acceleration`, `Crouch_click_sensitivity`, …) from the strings their registration calls push beside each global; `drunk_levels.xtbl` and `SprintEntry` field names; HUD string ids; the mission event names `m16_invert_stop`/`m16_boss_invert_stop`; and AI sub-state names from the table at `0x012e1168`. U2 was also Ghidra-free (capstone listings, a linear sweep of `.text`, raw rel32/dword scans).
- U4 (§4) additionally quotes, as data read from the executable or from the specs cited: the 61 camera submode names of the table at `0x012e3d48` and the `camera_free.xtbl` / `vehicle_cameras.xtbl` element names (as `spec-tables-environment.md` §12.2 and `spec-tables-ui-controls.md` §8.1 give them), the literals `sp_mdsphere_genki` (`0x0111bde8`) and `MISSION_FAILURE_DIED_TEXT` (`0x0113adfc`), and Lua function names already in `spec-lua-api-behaviour.md`. U4 was Ghidra-free (capstone listings, a linear sweep of `.text` for absolute and displacement references, raw rel32/dword scans of the whole image, push surveys); no Ghidra project was opened or copied, so none needed deleting. **U4 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U4 edit: regex 0 hits, backstop 0 hits** (control string 5/5 on both).
- U3 (§5) additionally quotes, as data read from the executable: the RTTI class names `character_controller`, `hk_custom_character_proxy`, `hk_character_shape_phantom` and `MyCharacterListener` (as `spec-physics-runtime.md` §2.1 gives them), Havok 2010.2's public `hkpCharacterProxyListener` method names for the listener slots (the convention `spec-physics-runtime.md` §2.2 uses), the literals `sp_pony_cart01` (`0x011743b4`), `Shark` (`0x0112e1fc`), `Diffuse_Color_a`/`_b`/`_c`, `Hair` and `Sphere_Map`, and the action name `CBA_OFC_SPRINT`. U3 was Ghidra-free (capstone listings, a linear sweep of `.text` for displacement scans, raw rel32/dword scans of the whole image, an RTTI walk); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched.
- U6.6 (§6) additionally quotes, as data read from the executable or from the specs cited: the trace strings `Set Exit`/`Set Enter`, the force-flag record tag `human_force_flagsbeing_vacuumed` (`0x01167ea8`) and the tag `human`, the weapon names `Rifle-NG`, `Rifle-STAG`, `Rifle-Gang` and `SMG-Gang`, the `vehicle_interaction_info.xtbl` flag name `Equip Rifle on Exit` (loader string `0x0118640c`), the 83-entry key-token names (`none`, `exit`, `extract`, `change seat`, `traverse`, `deactivate`, `open`, `close`, `prepare`, `fast`, `enter`) of the table `0x0130e638`, caller-classifying literals (`ACT_TORCH_FAIL_WATER`, `ACT_TRAFFIC_*`, `ACT_ESCORT_*`, `DIVERSION_CHOP_SHOP_*`, `fail_hos_all_died`, `STORE_AMBULANCE_MSG`, `mission_purchase`, `Homie force to exit vehicle!`, `-- Cutscene Script Group --`, `Shark`, `Brute Fuel Tank`), force-flag names (`cannot_exit_vehicles`, `cannot_exit_rc_vehicle`) and Lua function names as `spec-lua-api-behaviour.md` gives them. U6.6 was Ghidra-free (capstone recursive-descent and linear listings over `exe_va.py`, raw `E8`/`E9` rel32 and dword scans of the whole image, a push-corrected request-field survey of all 79 exit-request call sites, census call-graph closures, static table reads of `0x00b05ac4`, `0x00b06fdc`, `0x00b0abe4` and `0x0130e638`); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched; the only data file read was the extracted `vi_exit.xtbl`/`vi_enter.xtbl` under `tools/as_base_xtbl`. **U6.6 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.6 edit: regex 0 hits, backstop 0 hits** (control string 5/5 on both).
- U6.0 and U6.4 (§7, §8) additionally quote, as data read from the executable or from the specs cited: the log strings `Set Enter`, `Set Exit`, `Set Ride`; the animation-parameter names `Collision Bool`/`Collision Dir`; the tweak names `VI_forward_vel_accel`, `VI_vert_vel_accel`, `VI_random_accel_factor`, `VI_reaction_delay_min_ms`, `VI_reaction_delay_max_ms`, `VI_collision_min_vel_diff` (read at their registration stubs `0x0100aa50`…`0x0100aaff`); the `vehicle_ai`/`vehicle` force-flag strings (`drunk_driver`, `degrade_info`, `goal_stop_override`, `override_speed`, `never_go_ambient`, `infinite_vehicle_collision_mass`, `cheat_no_damage`, `cheat_smash`); `hud_inventory_hide`, `player_last_vehicle_handle`, `human_ai_data`; the model names `sp_mdsphere_genki`/`truck_2dr_classic01`; the camera submode names of §4.8; the `vehicle_cameras.xtbl` set names of `spec-tables-ui-controls.md` §8.1; and the `vehicle_interaction_info.xtbl` field and flag names of `spec-tables-vehicle-world.md` §3.2 (`Queue`, `Primary_Access_Seat`, `Secondary_Access_Seat`, `Hide Occupant`, `Ignore Team Disposition`, `Hold Weapon in Left Hand`). U6.0/U6.4 were Ghidra-free (capstone listings with an esp-depth tracker, census call edges, raw rel32/dword scans of the whole image, a band displacement sweep, push surveys including shared call tails); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.0/U6.4 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.0/U6.4 edit: regex 0 hits, backstop 0 hits** (control string 5/5 on both).
- U6.1 (§9) additionally quotes, as data read from the executable or from the specs cited: all 83 key-token names of the table `0x0130e638` (`none`, `any`, `prepare lift`, `extract fail 1`, `exit at speed cruise`, `front left`, `hostage a`, `combat ready`, `alternate`, …) and the six strings that follow it (`request pending`, `start`, `wait for exit exterior`, `wait for exit interior`, `teleport`, `pathfind`); the diagnostic format string `Couldn't find animation for %s -> %s -> %s -> %s -> %s` (`0x01186848`); the loader strings `Vehicle_Interaction_Animation_Set`, `Animation_Grid`, `Element`, `Animation`, `Parameter%d`, `Animated_Camera_Tests`, `Camera_Pos`; the `vehicle_interaction_info.xtbl` field names `Enter_Animations`, `Exit_Animations`, `Ride_Animations` as `spec-tables-vehicle-world.md` §3.2 gives them; and, from the shipped `vi_enter.xtbl`/`vi_exit.xtbl`/`vi_ride.xtbl` (extracted copies under `tools/as_base_xtbl`), the set names `Bike Rocket Rider`, `Bulldozer Right`, `BRUTE`, `Heli Fighter Rider`, `RiotCop_Right`, `medium passenger right`, `VTOL2 …`, `motorcycle driver`, `JetBike driver` and animation names (`Exit Right Rider`, `Exit Right Fast`, `Exit Left Fast`, `Exit Right Extracted Pull`/`Bash`/`Shoot`/`Punch`, `Drive Ready SM`, …). U6.1 was Ghidra-free (capstone listings over `exe_va.py`, raw `E8`/`E9` rel32 and whole-image dword scans, census closures of every state handler, static table reads of `0x00b0abe4`, `0x00b0ac44`, `0x00b0a554`, `0x00b0a628` and `0x0130e638`, and a re-implementation of the key builder run over the three shipped tables); no Ghidra project was opened or copied, so none needed deleting; no `.czn_pc` data was touched. **U6.1 self-check (strict regex + plain-substring backstop, Python `re`) on the entire file after the U6.1 edit: see the Changelog entry.**
- Self-check result: recorded in the Changelog entry below, run on the finished file as the last step.

---

## Changelog

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
