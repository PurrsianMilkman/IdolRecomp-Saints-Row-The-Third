# Saints Row: The Third — Front-End / Menu Runtime Architecture (`SaintsRowTheThird.exe`)

**Scope.** The front-end/menu architecture: the 11-slot top-level game-mode table and the UI screen-stack
it sits alongside, the main menu's "new game"/"horde start"/"continue" flows and the primitives they
share, the shared fade-completion teardown and its interaction with the pre-existing game-pause depth
counter, and the pause menu's own place in this picture. This is architectural glue connecting several
previously-separate individual Lua-native entries in `spec-lua-api-behaviour.md` to each other and to
`spec-save-format.md`'s own already-documented save/load chain — individual Lua-native bodies are cited
by section number there, not duplicated here.

**Method.** Live disassembly/decompilation of the shipped `SaintsRowTheThird.exe` (PC retail) via this
project's persistent read-only Ghidra server (`tools/ghidra_server/query.py`, `func`/`xref` modes),
invoked only through PowerShell with hidden windows, never a raw `analyzeHeadless.bat` call and never
Ghidra from Bash. No leaked Volition/Havok/Audiokinetic/RAD material. All addresses plain hex; `FUN_`/
`DAT_` are bare evidence-anchor names, never treated as real identifiers.

**Confidence key:** **CONFIRMED** (disassembly/decompile read directly), **HIGH CONFIDENCE** (strong
call-shape/context evidence, callee body not independently opened), **HYPOTHESIS** (plausible,
materially uncertain), **OPEN** (not resolved — reported honestly rather than guessed).

---

## 1. The top-level "which screen/mode is active" architecture

- **The 11-slot game-mode table**, `0x01503bc0` (0x24 bytes/slot, 9 dwords, bound `< 0xb`), registered by
  `0x00706a40(index, callbackBlock, byteFlag)`. Ten of its eleven registration call sites are named:
  `0x005d3ac0`, `0x00707840`, `0x00bdc120` (mode 7, Bink movie), `0x007041b0` (mode 5, gameplay),
  `0x007a8530` (mode 8, async player-teleport), `0x007ad410`, `0x007af0f0`, `0x007b06f0`, `0x007e1d10`
  (**mode 6 — control-scheme/options UI, resolved 2026-10-08**), `0x0088cf70` (network/session
  mode-manager glue). Mode `0xa` (10) is the shutdown mode (`spec-lua-api-behaviour.md` §26.25).
- **The mode stack itself**, `0x01503b50`, current index `0x012f4a80`, pumped by `0x00706be0` (phase 1:
  top-of-stack's `+0x0c` slot every frame; phase 2: every stacked mode's `+0x1c` slot). Push/pop requests
  queue at `0x012f4a94`. Sibling accessors in the same family: `0x00706a10` (zero-init a callback-block
  local), `0x00706ab0` (read current/top mode, pre-existing), **`0x00706b90`** and **`0x00706fe0`**
  (newly observed 2026-10-08, from `main_menu_continue`'s own body — `0x00706b90()` returns a mode value
  compared against 3; `0x00706fe0(1, 3)` is called when it is not already 3, shape consistent with "push
  mode 3 unless already there," matching the registrar's own "a mode already on the stack is not pushed
  twice" rule. **Bodies not independently decompiled — the "push" reading is HIGH CONFIDENCE from call
  shape and context, not CONFIRMED from their own bytes.**
- **Mode 3 — HYPOTHESIS, newly motivated: the loading-screen mode.** `main_menu_continue`'s per-frame
  body calls `0x00706b90`/`0x00706fe0(1,3)` immediately after calling the save-apply entry point
  `0x00B947A0` — i.e. right as a level-change has just been kicked off. A plausible candidate for the
  "loading screen" game-mode `spec-lua-api-behaviour.md` §55.1 already flags as OPEN there (discussing
  states 2/5/6, not 3) — adjacent to, not the same as, that open item.
- **Mode 4 — HYPOTHESIS, weaker: possibly "new-game/level-transition requested."** See §2 below; the
  evidence is an object-field sentinel-value coincidence (`0xb`/11, matching the mode table's own
  11-slot bound), not a traced consumer — explicitly weaker than the mode-3 candidate.
- **The game-pause depth counter** (pre-existing, `spec-lua-api-behaviour.md` §37.3): ~60 push/pop sites,
  11 reason codes, one shared counter, no Lua-registered pusher/popper. `get_frame_time` is never gated
  by it; a separate scaled "game" millisecond counter is. §4 below adds a concrete worked instance.
- **The UI screen-stack singleton** (pre-existing, §2.8/§2.9): `push_screen`/`pop_screen`'s own object,
  methods `0x007b4790`/`0x007b4640`/`0x007b4ad0`. The pause menu is architecturally a screen on this
  stack, not a slot of the 11-mode table (§4).

---

## 2. New game / horde start, traced in full — `0x007c9d20` → `0x007ae950`

```
main_menu_new_game (0x007c9d20):
    0x00dfde50(arg)         ; trivial pointer-diff helper, unrelated bookkeeping
    0x007ae950()            ; the real routine
    return 0

main_menu_horde_start (0x007c9d60) -> 0x006e6630():
    0x014f3d34 := 1         ; horde-mode flag, read by ~20 functions in 0x006e4xxx-0x006e9xxx
    0x014f3d2c := 0
    jmp 0x007ae950()        ; tail-call into the SAME routine new game uses
```

`0x007ae950` (`0x007ae950`), full body:

1. `if (0x024d4461 == 1) 0x00867740();` — if the network module is up (the engine-wide "network
   module up" byte, `spec-lua-api-behaviour.md`'s network-module section), tear it down first
   (`0x00867740`, the already-documented shutdown wrapper).
2. `0x0086bbd0(&0x014ff338);` — copies a 20-byte global template (two qwords + one dword) from
   `0x014ff338` into a local stack buffer.
3. The caller overwrites bytes 4-6 of that local copy to `{10, 0, 0}` and passes its address to
   `0x006fe180` (a large, not-fully-traced consumer — OPEN).
4. `0x006fe1d0(0);` — writes `0.0` into the *global* float `0x014ff348` (offset `+0x10` of the same
   20-byte template region) — confirmed trivial: `0x014ff348 = (float)param; if (param<0) 0x014ff348
   += 4294967296.0;`.
5. `0x022425dc := 1; 0x022425dd := 1;` — two shared one-shot flags. `0x022425dd` is also read/cleared
   by `main_menu_continue` (§3) — strong evidence these are a generic "a menu-driven game-start/
   transition has been requested" pair, not new-game-specific.
6. `0x0088ba10(4, 2);` — **the shared level-transition gate**, confirmed by its own 3 call sites:
   `0x00B947A0` (the save-apply function `spec-save-format.md` §11.1/§15 already names), `0x007ae950`
   (this routine) and `0x007aebb0` (§5). Gated on a busy byte `0x024e4824` (also read/cleared by
   `0x0088bdf0`, the already-documented "mode-manager teardown" from the network-module section). On the
   open gate it: (a) re-initializes an object at `0x024e4860` via `0x0088b6b0` — zeroes fields
   `+0x00`..`+0x6c`, sets `+0x27c := 0xb` (11) and `+0x280 := -1`; (b) reads the horde flag via
   `0x006e44f0`; (c) builds a descriptor through `0x007029b0(1,2,1,1,0,1,1,1,horde)` → `0x007029c0`; (d)
   calls `0x0087f440` with 13 arguments including the horde flag and the built descriptor — this call
   shape and the surrounding `0x0088bxxx`/`0x0088cxxx` address neighborhood sits squarely inside the
   network/session "mode-manager" subsystem `spec-lua-api-behaviour.md` already documents (instance
   `0x01304ab4`, state table `0x01304a30`, restarted by `0x0088c090`). **HYPOTHESIS, not traced further:
   this call plausibly constructs or updates the single-player offline session that document flags as
   unresolved ("whether starting a single-player game runs the installer `0x0087efe0` and creates a
   one-member host session... cannot be read from these dumps") — a concrete new lead for that open item,
   handed off rather than chased, to avoid duplicating the session/coop team's own territory.** (e)
   finally overwrites the just-initialized object's own `+0x27c`/`+0x280` fields with the caller's two
   literal arguments — `4` and `2` for new game. **The `0xb`/11 sentinel default coincides with the
   11-slot mode table's own bound, which is why mode 4 is listed as a weak HYPOTHESIS in §1 — no
   consumer of `0x024e4adc`/`0x024e4ae0` (the two fields' absolute addresses) was found reading them as a
   simple literal-address access; any real consumer must take the object's base `0x024e4860` as a
   register and add the offset at runtime, which an xref scan cannot attribute. Left OPEN.**
7. `0x00da7930(&0x02242558, "sr3_city", 0x80);` — `strncpy`+NUL-terminate; hardcodes the literal
   world name `"sr3_city"` into the shared pending-zone-name buffer `0x02242558`. The **same** buffer is
   used by `main_menu_continue` (writes the save file's own zone string here — `spec-save-format.md`
   §11.1 step 3a) and by `0x007aebb0` (§5) — confirming this is a generic "destination zone for the next
   level-change" buffer, not new-game-specific.
8. `0x007ae0a0(1);` — the shared input-lock dispatcher (§6); for new game this is the call site that
   reaches the Lua hook string `"main_menu_new_game_block_input"` (confirmed literal, at `0x01155bf4`),
   via the generic coroutine-launch primitive `0x00e0ca80` (the SAME primitive
   `spec-lua-api-behaviour.md` §36.2 already documents for mission-start invocation).
9. `0x0059f8c0(0xFFFFFFFF, 0x007b4cb0, 1);` — requests a screen **fade-out** through the already-
   documented fade primitive (§26.24: `(durationMs, completionCallback, flag)`). Duration `0xFFFFFFFF`
   (as signed, `-1`) is a literal not resolved against §26.24's own "trunc(seconds × 1000)" convention —
   OPEN (possibly a sentinel for "use the default/UI-script-chosen duration" rather than a literal
   `-1 ms`). Completion callback `0x007b4cb0` is traced in full in §4.
10. `0x00b97270();` → `memset(&0x0290df88, 0, 0xE20)` — **a clean, concrete link between new-game's
    CODE and the already-documented new-game DATA signature**: `0x0290DF88` is exactly the 904-dword
    block `spec-save-format.md` §11.3 names as entry 29's own "inert storage… a pass-through: whatever a
    file carries is loaded into the global and re-emitted on the next save" — new game zeroes it
    directly, consistent with (and explaining the code behind) that section's own empirical note that
    this block reads all-zero in fresh/new-game saves.
11. `0x00d2f540();` → `return 1;` — a linker-folded dead stub (also called from ~17 unrelated sites
    including the player-death handler `0x00702810`); a no-op in the shipped build.

**Net reading of new game at the EXE level:** a short, almost entirely "flag-setting and hand-off"
routine — it tears down networking if active, stages a hardcoded destination world name and two generic
transition flags, nudges a shared session/mode-manager object, fires one input-lock Lua hook, and fades
the screen out. **It does not touch player stats, inventory, wardrobe, or the mission system in any
way** — none of `spec-save-format.md` §7.5/§10.x's "new game defaults" (cash 5700, the 350-entry
unlockables table, wardrobe defaults) are written here. Those defaults must come from whichever code
path actually constructs a fresh player-record object when no save is loaded — most plausibly inside the
world/level-start driver (`0x00B98C00`/`0x00B94B90`→`0x00B97310`, per `spec-save-format.md` §11.1 step
4-5) taking its "no file, build defaults" branch, or the player object's own constructor. **Neither was
traced — flagged OPEN, exact entry points named for whoever picks it up.**

**The highest-value negative result of this whole investigation: the new-game code path never calls the
mission-bind function `0x006d6920`, and never touches the mission-phase machine at all.** An exhaustive
xref of `0x006d6920` finds exactly 7 callers, every one already inside the mission-flow cluster itself
(`0x006d7120`, `0x006d7990`, `0x006d7b40`, `0x006d82a0`, `0x006d8450`, `0x006db140`, plus one
undefined-code caller) — **none in `0x007ae950`, none anywhere in the menu/UI address range.** The EXE's
own "new game" code does not select or bind a first mission by name. Whatever puts the player into
mission `m01` must be a **Lua/data-level fact** rather than a hardcoded EXE call. `0x006D9310` (the phase-2/
restart enter-handler, `spec-lua-api-behaviour.md` §21.27a/b) is even more isolated: its only reference
in the whole binary is the data pointer stored at `0x012f473c`, confirming it too is reached solely
through the mission engine's own internal state table, never from menu code.

**Narrowed, 2026-10-08:** the exact zone and marker are now identified — mission 1's own dedicated
per-mission data zone, `sr3_city~fm01.czn_pc` (inside `sr3_city_0.vpp_pc/m01.str2_pc/`), carries a
player-start-shaped `Navpoint` marker named `Start_player_nav` (`spec-zone-data-format.md` §11's own
correction paragraph, `scratchpad/first_mission_bootstrap_dataside_2026-10-08.md`). HIGH CONFIDENCE
(not an EXE re-trace of mission 1's own bind call): mission 1 most likely does NOT use the ordinary
walk-into-marker mechanism (`spec-lua-api-behaviour.md` §36.2's `0x006d5460`) at all — it is the one
mission with no free-roam period before it, and its zone carries no mission-start-shaped
`Trigger_Volume` and no `scripted_mission_start`-classed object, consistent with `0x006D9310` taking
its own already-documented direct-relaunch bypass branch instead.

---

## 3. Load / Continue, traced in full — `0x007ae420`

`main_menu_continue` (confirmed identity: its own body contains the literal on-disk dialog string
`"MAIN_MENU_CONTINUE_UNAVAILABLE"`, line `0x007ae4d2`/`0x007ae4e1`). Full per-frame body:

1. One-time/idle housekeeping (`0x007b6f30`; a cached-flag early-out via `0x014ff67d`/`0x022425fa`/
   `0x022425fb` gating a call to `0x007adc00`, not traced further — OPEN).
2. If byte `0x02242604` is set and the current UI selector (`0x012fced8`) equals a specific screen object
   (`0x02296bf0`), and a cached directory count `0x012fcaa0` is still `-1`: re-reads the save directory
   (`0x00B94760`, allocate/read, 24 slots × `0x18` bytes at `0x02242608` — matches `spec-save-format.md`
   §2.1/§2.2's own 24-slot directory layout).
3. **Save-slot pick loop** (`0x02242604`/`0x02242605` state bits): walks all `0x012fcaa0` directory
   entries (stride `0x60`, matching §2.1's already-documented slot stride), skipping any slot whose own
   `+0x5c`/`+0x5d` bytes are non-zero (the exact "load error" flag `spec-save-format.md` §11.1 step 2c
   already documents). **This independently confirms, from the menu side, that `+0x5c`/`+0x5d` is read
   back by the UI to filter the continue picker, exactly as that section's own parenthetical already
   states.** Picks the slot with the highest "last played" field.
4. If nothing qualifies: fires the Lua hook `"main_menu_controller_selected"` (via `0x00e0ca80`) and
   shows the one-button `MENU_TITLE_NOTICE` / `MAIN_MENU_CONTINUE_UNAVAILABLE` dialog (`0x0084a1b0` ×2,
   `0x007c3de0`).
5. On a valid slot: `0x00b95210(selectedSlotIndex, 1);` — **the exact "step 1. request" address**
   `spec-save-format.md` §11.1 already names.
6. On success: sets `0x022425ea := 1`, calls the shared input-lock dispatcher `0x007ae0a0(1)` (§6) —
   the call site that reaches `"save_load_lock_input"` (confirmed literal, `0x01155bdc`) — then requests
   the identical fade-out (`0x0059f8c0(-1, 0x007b4cb0, 1)`) new game uses.
7. **Per-frame continuation** (re-entered every frame while a load is pending): once the pending-flag
   `0x012fcefc` clears, it reads/clears `0x022425ea`/`0x022425ec`/`0x022425f8`, and — only on the branch
   where `0x022425ea` was set — calls `0x00b947a0();` **directly** (`0x007ae620`) — **the exact "step
   3a. apply" address** `spec-save-format.md` §11.1 already names. **This is the first time this call
   has been shown reached from the menu side rather than inferred structurally.**
8. Otherwise (the `else` arm at `0x007ae63c`): clears the shared flag `0x022425dd` (the SAME flag new
   game sets — confirming it really is a generic "a transition was just requested" one-shot, consumed
   here), reads/pushes the mode stack (`0x00706b90`/`0x00706fe0(1,3)`, mode-3 HYPOTHESIS in §1), and
   falls through to more UI bookkeeping (disconnect-reason dialogs, not traced further — OPEN, appears
   co-op/host-migration-adjacent, out of this document's scope).

**Net comparison, new game vs. continue:** both funnel into the same two shared primitives
(`0x0088ba10` level-transition gate; the fade-out+`0x007b4cb0` teardown) and the same two shared flags/
buffer (`0x022425dc`/`dd`, `0x02242558`), but new game **writes a literal string** into the zone buffer
and calls the gate with literal `(4, 2)`, while continue **reads a save file's own header** (via
`0x00B95210`→`0x00B947A0`, which — per `spec-save-format.md` §11.1 step 3a — copies the buffer's own
`+0x14` zone string and, once the level is ready, consults the game-flow object's `+0xD4` bit 2 to
decide whether to actually apply the loaded save or fall back to a fresh start). **`spec-save-format.md`
§11.1 described this mechanism from the save-apply side without a traced caller; this document supplies
the exact calling instruction on the menu side (`0x007ae620`), closing that gap.**

---

## 4. The shared fade-completion teardown — `0x007b4cb0` → `0x007b4990`

Both new game and continue (and, per its own caller list, also cutscene-start `0x00725df0`, the
screen-fade machinery `0x005a0600`, several `0x007bdxxx`/`0x007c0xxx` UI-flow functions, and
`0x00614530`) request their fade-out with completion callback `0x007b4cb0`, which unconditionally calls
`0x007b4990(-1)`:

- Takes an implicit `this` pointer to a UI object (confirmed shape from all its callers: always
  `0x02247b80`, a global "dialog pool"-shaped object with a count at `+0x24`, a parallel array at
  `+0x30`, and per-entry class-flag bytes at each entry's own `+0x1e`/`+0x1c`).
- If a count field (`+0x24`) is positive: for every tracked entry **except** the one matching the passed
  "keep" handle (looked up via `0x007b3ae0`), if the entry's own `+0x1e` bit `0x1` is set, calls two
  virtual methods on it (`+0x20` with argument `1`, then `+0x24` with no arguments — a "deactivate, then
  destroy/close" pair). Clears the tracked-entry count and resets bookkeeping; re-adds the "keep" entry
  if it was found among the ones just walked.
- Calls the already-documented deadline/timestamp setter `0x00d9e140` (`spec-lua-api-behaviour.md`
  §1.2/§11.13/§22.2/§21.27), then **`0x007b3e80`** and **`0x007b3de0`**:
  - `0x007b3e80`: scans the SAME object's three entry collections (the `+0x24`-counted array, the
    `+0x148`-counted array, and two single-pointer slots at `+0x16c`/`+0x170`) for any entry whose own
    `+0x1c` byte has bit `0x8` set. If any is found (**and** a global gate `0x00867830` returns false —
    not traced further, OPEN, plausibly "are we mid-shutdown"), and this differs from the object's own
    cached `+0x17a` byte: **pushes game-pause reason `0xd`** via `0x007074a0(0xd, 0)` — the exact
    already-documented push-site family from §37.3. If the flag is going the other way, it calls
    `0x00707490` (query: "is the pause currently active for this reason/at all" — not independently
    confirmed) and, if true, **pops** via `0x00707540(0xd)` — again an already-documented pop site.
  - `0x007b3de0`: the structural mirror, testing bit `0x40` instead of `0x8` at the same `+0x1c` byte,
    and driving a *different* side effect (`0x007f1070(1)`/`0x007f1070(0)` — not traced further, OPEN;
    plausibly a HUD-hide or input-focus toggle rather than the pause depth, since it does not call
    `0x007074a0`/`0x00707540` at all).
- Finally closes the underlying critical section and returns.

**Reading:** this is the engine's generic "a UI transition just finished — close every dialog that
shouldn't survive it, and update the shared pause depth to match whichever dialogs with the `0x8` class
bit are still open" step. It is **not** mission- or menu-specific; new game, continue, cutscene-start and
several other UI flows all route their own fade-completion through this exact function. This is a
concrete, previously-untraced instance filling in one of the ~60 push/pop sites §37.3's census already
counted but did not individually characterize.

---

## 5. `0x007aebb0` — a sibling to new game, probably a developer/machinima jump-to-zone helper

```
0x007aebb0(char *zoneName):
    0x00da7930(&0x02242558, zoneName, 0x80);  ; same shared zone buffer, but caller-supplied name
    0x0088ba10(4, 2);                            ; the IDENTICAL literal args as new game
    0x007ae0a0(1);                               ; the same input-lock dispatcher
    0x0059f8c0(-1, 0x007b4cb0, 1);             ; the same fade+teardown
```

Structurally this is new game's *exact* sequence, generalized to take any zone name instead of
hardcoding `"sr3_city"`. Its own callers are `0x00bc6350` (inside the address neighborhood of the
machinima registrar `0x00bc68a0`, `spec-lua-api-behaviour.md` §56) and one untyped data reference at
`0x007aefd8`. **HYPOTHESIS, not confirmed: a developer/machinima "load any named zone as if starting a
new game there" helper, not part of the ordinary player-facing new-game/continue flow.** Its exact
exposure (a debug menu? a machinima tool-only Lua entry?) was not traced.

---

## 6. `0x007ae0a0` — the shared input-lock dispatcher

Called by new game, continue, `0x007aebb0` (§5), `0x007aec20` (a fourth sibling, not traced), and three
sites in the `0x008b9xxx`/`0x008ba170` range (which also independently write the shared flag
`0x022425dc` — consistent with being more instances of the same "menu is transitioning" family, not
traced further). Body:

1. If a screen-visibility query (`0x007b3b70`, object `0x02296bf0`) is true: calls `0x007ca730` with the
   caller's own argument (purpose not traced — OPEN).
2. If the current UI-screen selector (`0x012fced8`) equals code `5`, **and does not** equal code `0x11`
   (17): fires the Lua hook `"main_menu_new_game_block_input"` through `0x00e1a1b0`/`0x00e0ca80`/
   `0x00e0cd00` (the UI-context-resolve → coroutine-create → dispatch triple `spec-lua-api-behaviour.md`
   §36.2/§11.13 already documents for a different purpose).
3. If the selector equals code `0xb` (11): fires `"save_load_lock_input"` through the same triple,
   additionally pushing the caller's own boolean argument (`1` from both new-game's and continue's call
   sites) via `0x00e0ce00`.

**Reading:** a single dispatcher that decides which of two input-lock Lua hooks (if any) applies, based
on which numbered UI screen is currently active — **not** which menu action triggered it. New game and
continue both call it with argument `1`, but only the one whose *current screen code* matches actually
fires its hook; the screen codes `5`/`0x11`/`0xb` themselves were not identified against any named UI
document — **OPEN.**

---

## 7. The pause menu's own place in this architecture

**The pause menu is a UI screen pushed onto the already-documented screen-stack
(`spec-lua-api-behaviour.md` §2.8/§2.9), not a distinct entry in the 11-slot top-level game-mode stack
(§1).** No Lua-registered pause-menu native (`pause_menu_accept_options`, `pause_menu_control_scheme_init`,
`pause_menu_update_option`, `pause_menu_restore_defaults`, `pause_menu_quit_game_internal`,
`pause_menu_has_seen_display_cal_screen` — all already on file in `spec-lua-api-behaviour.md`) opens or
closes the menu itself; they are all sub-screen option-editors operating *inside* an already-open pause
menu. "Paused" at the engine level is the pre-existing, already-documented **shared game-pause depth
counter** (§37.3: ~60 push/pop sites, 11 reason codes, bookkeeping-only — a pop decrements the depth
without checking its own reason was the one pushed). §4 above adds one freshly-traced, concrete worked
example: the shared fade-completion teardown (`0x007b4cb0`→`0x007b3e80`/`0x007b3de0`) pushes/pops pause
reason `0xd` keyed on a UI dialog-pool's own class-flag bits — i.e. **opening certain classes of UI
dialog auto-raises the shared pause depth, independent of whether the dialog is the pause menu
specifically.** The literal "pause button → open pause menu" input binding itself was not located —
**OPEN** (see §9).

Mode 6 (`0x007e1d10`, resolved this pass) is the **control-scheme/key-bindings configuration UI**, not
the pause menu as a whole — it loads `control_scheme_text.xtbl` and dispatches UI screen codes 9/10 (one
controller) or `0x19` (25, two controllers, gated on flag `0x0229cd98`) via the shared dialog-open
primitive `0x007b4750`.

---

## 8. Address index

| Address | Role | Confidence |
|---|---|---|
| `0x007c9d20` | `main_menu_new_game` (Lua-bound) | CONFIRMED |
| `0x007c9d60` | `main_menu_horde_start` (Lua-bound) | CONFIRMED |
| `0x007ae950` | the shared new-game routine both tail into | CONFIRMED |
| `0x006e6630` | horde-start's own flag-setter before tailing into `0x007ae950` | CONFIRMED |
| `0x014f3d34` / `0x014f3d2c` | horde-mode flag / companion dword | CONFIRMED |
| `0x0086bbd0` | 20-byte struct copy (template → local) | CONFIRMED |
| `0x014ff338` | the 20-byte "world/zone transition request" template global | CONFIRMED (shape); role OPEN |
| `0x006fe180` | consumer of the patched local copy | NOT independently traced — OPEN |
| `0x006fe1d0` | sets global float `0x014ff348 := (float)arg` | CONFIRMED |
| `0x022425dc` / `0x022425dd` | shared one-shot "menu transition requested" flags | CONFIRMED |
| `0x0088ba10` | shared level-transition gate (3 callers: `0x00B947A0`, `0x007ae950`, `0x007aebb0`) | CONFIRMED |
| `0x024e4824` | busy/gate byte for `0x0088ba10`; cleared by `0x0088bdf0` | CONFIRMED |
| `0x024e4860` | object `0x0088ba10` initializes/updates (`+0x27c`/`+0x280` <- caller args) | CONFIRMED (shape); "= game-mode request" HYPOTHESIS |
| `0x007029b0` / `0x007029c0` / `0x0087f440` | descriptor-build chain inside `0x0088ba10`, session/mode-manager neighborhood | NOT independently traced — OPEN, handed to session/coop team |
| `0x02242558` | shared pending destination-zone-name buffer (0x80 bytes) | CONFIRMED |
| `0x007ae0a0` | shared input-lock dispatcher (§6) | CONFIRMED |
| `0x01155bf4` `"main_menu_new_game_block_input"` / `0x01155bdc` `"save_load_lock_input"` | the two Lua input-lock hook names | CONFIRMED |
| `0x0059f8c0` | fade-out request primitive (pre-existing, §26.24) | CONFIRMED (cited) |
| `0x007b4cb0` → `0x007b4990` | shared fade-completion teardown (§4) | CONFIRMED |
| `0x02247b80` | UI dialog-pool object `0x007b4990`/`0x007b3e80`/`0x007b3de0` operate on | CONFIRMED (shape) |
| `0x007b3e80` / `0x007b3de0` | dialog-class-bit-driven pause push/pop (reason `0xd`) / a second, non-pause side effect | CONFIRMED |
| `0x007074a0` / `0x00707540` | game-pause push/pop (pre-existing, §37.3) | CONFIRMED (reconfirmed) |
| `0x00b97270` | `memset(0x0290df88, 0, 0xE20)` — zeroes save-format entry 29's own "inert" block | CONFIRMED; cross-links `spec-save-format.md` §11.3 |
| `0x00d2f540` | dead/linker-folded `return 1` stub, called at the end of new game | CONFIRMED |
| `0x006d6920` | mission-bind/phase-8 function — 7 xrefs, all internal to the mission-flow cluster, NONE from menu code | CONFIRMED (negative result) |
| `0x006D9310` | phase-2/restart enter-handler — 1 xref total, a data pointer at `0x012f473c` | CONFIRMED (negative result) |
| `0x007ae420` | `main_menu_continue` (identified by its own on-disk string) | CONFIRMED |
| `0x02242608` / `0x012fcaa0` | save-slot directory array / count, read by continue's picker | CONFIRMED |
| `+0x5c`/`+0x5d` of each slot descriptor | load-error flag, filters the continue picker | CONFIRMED from the menu side |
| `0x00B95210` | save-file read request (pre-existing, §11.1 step 1) | CONFIRMED (reached directly from `0x007ae420`) |
| `0x012fcefc` | pending-load flag continue polls every frame | CONFIRMED |
| `0x00B947A0` | save-apply / level-change start (pre-existing, §11.1 step 3a) | CONFIRMED (reached directly from `0x007ae420`) |
| `0x00706a10` / `0x00706a40` / `0x00706ab0` / `0x00706b90` / `0x00706fe0` / `0x00706be0` | the 11-slot game-mode table's init/register/read/push family | CONFIRMED (first four bodies); `0x00706b90`/`0x00706fe0` call-shape only |
| `0x007e1d10` | mode-6 registrar — control-scheme/options UI | CONFIRMED |
| `0x007e1a70` / `0x0057bbf0` (via `0x007e1ab0`) / `0x007e1ac0` | mode-6's enter / leave / per-frame slots | CONFIRMED (enter and per-frame read; leave is a thunk) |
| `0x007b4750` | shared "open UI screen code N" primitive mode-6's enter handler uses | NOT independently traced — OPEN |
| `0x0229cd98` | "second controller / split-screen" flag mode-6's enter handler branches on | CONFIRMED (shape) |
| `0x007aebb0` | sibling to new game, parameterized by zone name (§5) | CONFIRMED (body); real-world exposure HYPOTHESIS |
| `0x007aec20` | a fourth sibling in the same `0x007ae0a0`-caller family | NOT traced — OPEN |

---

## 9. OPEN items

1. Where fresh-game player-record defaults (cash 5700, wardrobe, the 350-entry unlockables table)
   actually get written when no save is loaded — not in `0x007ae950` itself (§2); must live in the "no
   file, build defaults" branch of `0x00B98C00`/`0x00B97310` or a player-object constructor, neither
   opened.
2. The literal pause-menu-open input binding — which input handler pushes the pause-menu screen, and
   which specific one of the ~60 pause-depth push sites it uses (§7).
3. The real identity of UI screen codes `5`/`0xb`/`0x11`/`9`/`10`/`0x19` the various dispatchers above
   branch on (§6/§7).
4. `0x006fe180`'s own body — the consumer of new-game's 20-byte transition-request template (§2).
5. `0x0024e4860`'s `+0x27c` field as "the requested game-mode" (mode 4) — the sentinel-value coincidence
   is suggestive, no consumer traced (§1/§2).
6. `0x0087f440`'s call chain as single-player session construction — a concrete new lead for an existing
   open item in the session/coop material, explicitly not chased further (§2).
7. `0x007aebb0`'s real-world exposure as a machinima/developer tool (§5); `0x007aec20` (§6); `0x007b4750`'s
   own body (§7).

---

## 10. Confidence summary

**CONFIRMED (full disassembly/decompile read):** the new-game routine `0x007ae950` end to end;
horde-start's own flag-set-then-tail-call; the shared level-transition gate `0x0088ba10` and its 3
callers; the shared zone-name buffer and input-lock dispatcher; the fade-completion teardown
`0x007b4cb0`/`0x007b4990` and its dialog-pool-driven pause push/pop; `main_menu_continue`'s full
per-frame body including its direct calls into `0x00B95210`/`0x00B947A0`; mode-6's registration and
enter/per-frame handlers; the save-format cross-link via `0x00b97270`'s `memset`; the clean negative
result that `0x006d6920`/`0x006D9310` have no menu-side callers at all.

**HIGH CONFIDENCE (strong call-shape/context evidence, callee body not independently opened):** mode 3
as the loading-screen mode; `0x00706b90`/`0x00706fe0` as a mode-stack read/push pair; `0x007aebb0` as
new-game's sequence generalized to an arbitrary zone.

**HYPOTHESIS (flagged explicitly, not settled):** `0x024e4860`'s `+0x27c` field as "the requested
game-mode" (mode 4); `0x0087f440`'s call chain as single-player session construction;  `0x007aebb0`'s
real-world exposure as a machinima/developer tool.

**OPEN (honest negatives, not guessed):** see §9.

---

## 11. Clean-room compliance note

All findings above come from live disassembly/decompilation of the shipped `SaintsRowTheThird.exe` (PC
retail) via this project's persistent read-only Ghidra server (`tools/ghidra_server/query.py`,
`func`/`xref` modes), invoked only through PowerShell with hidden windows, never a raw
`analyzeHeadless.bat` call and never Ghidra from Bash. No leaked Volition/Havok/Audiokinetic/RAD source
of any kind was consulted or referenced. Function/variable names are this project's own established
convention (`FUN_<addr>`, `DAT_<addr>`) or Lua-binding names already published in
`spec-lua-api-behaviour.md`; on-disk string literals quoted above (e.g.
`"main_menu_new_game_block_input"`, `"MAIN_MENU_CONTINUE_UNAVAILABLE"`) are read directly from the
executable's own data sections, not sourced externally.

---

## Changelog

- **2026-10-08 — §2's closing paragraph narrowed: the exact mission-1 zone and marker are now
  identified** (SPEC TEAM, agent `a30695cf2a54f0459`). `sr3_city~fm01.czn_pc` carries a
  player-start-shaped `Navpoint` named `Start_player_nav`; HIGH CONFIDENCE mission 1 uses
  `0x006D9310`'s own direct-relaunch bypass rather than the ordinary walk-into-marker mechanism. Full
  detail: `spec-zone-data-format.md` §11, `scratchpad/first_mission_bootstrap_dataside_2026-10-08.md`.
  Cleanroom self-check: 0 new hits.

- 2026-10-08: file created (SPEC TEAM, front-end/menu-flow dispatch). Traced the front-end menu state
  machine (11-slot game-mode table, mode 6 resolved, modes 3/4 hypothesized), the full new-game routine
  (`0x007ae950`) and its clean negative result against the mission-bind function (`0x006d6920` has no
  menu-side callers), the full `main_menu_continue` body and its direct, newly-confirmed calls into
  `spec-save-format.md`'s already-documented save-read/save-apply chain (`0x00B95210`/`0x00B947A0`), and
  the shared fade-completion teardown's own dialog-class-driven game-pause push/pop — a concrete worked
  instance of the pre-existing pause-depth-counter mechanism. Full detail:
  `scratchpad/frontend_menu_flow_2026-10-08.md`. Cleanroom self-check: 0 hits. Synced to Team B.
