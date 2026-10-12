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

**Advanced 2026-10-10 — the push-screen primitive itself is now fully decompiled, narrowing but
not closing the search.** The two primitives everything in this architecture ultimately routes
through are: `0x007b4640` (push by resolved screen-definition pointer — de-dupes against the
active screen array, gates on a depth/flag check, and, if the newly-pushed entry's own flag bit
`0x2` is set, raises visibility-change notification `0x007b4300`) and `0x007b4750` (a thin wrapper
that resolves a numeric screen code against its own registry before tail-calling `0x007b4640` —
this is the exact "shared open UI screen code N primitive" mode 6's own enter handler already
uses, above). An exhaustive pass over all ~35 call sites of these two functions ruled out every
candidate that could be independently characterized: the world-map GPS ping overlay (`0x0083ba10`,
screen code `0x1a`), a world-interact-trigger popup (`0x00a02dc0`, code `0x33`), a horde-mode-only
screen (`0x006e8a10`, code `0x3e`), the co-op network-replication receive side of
`hud_push_screen` (`0x008b8b90`), and the co-op host-migration "return to main menu" path
(`0x007adce0`, codes `3`/`4`, reaching the already-documented `"Pause_menu_save_for_server_drop"`
hook). Two further call sites (`0x005ee2c0`, `0x00614530`) share a distinct shape — a generic
per-object bitfield test followed by either pushing a pre-cached screen pointer or deferring via a
pending byte, each reached only through a bare data/function-pointer reference rather than a
resolvable direct call — plausibly backing a sub-dialog confirm/cancel flow (e.g.
`pause_options_display_cal`) rather than the top-level opener, not confirmed. **Separately,
exhaustive grep over the complete extracted `interface.vpp_pc` Lua layer (~400 files) found no
`push_screen("pause_menu_common")` or `push_screen("pause_menu_top")` call anywhere, and `hud.lua`'s
own input tracker never subscribes to the `"pause"` event** — a clean confirmation, not merely an
absence, that the open transition is unreachable from Lua and must be native-only. The literal
binding remains OPEN. The phone/cellphone menu (a structurally parallel, separately pushed
screen-stack subtree — see `spec-cellphone-menu-runtime.md`) shows the exact same "unreachable
from Lua" shape for its own top-level open, suggesting one shared native dispatcher mechanism
opens both pause and phone, gated on different conditions — plausible, not proven.

**Exhausted 2026-10-10 — every known `0x007b4750` call site is now individually characterized,
and NONE of them is the pause-menu or phone-menu open; the search pivots to the input side.** The
dozen previously-untraced sites all resolve cleanly to other subsystems: `0x007bd890`/
`0x007bd910`/`0x007bd980` are three sibling setup stubs (one dispatcher, `0x006d8fb0`) for a
single parameterized confirm/message-box screen (code `0x38`, three button-layout subtypes);
`0x007c0130`/`0x007c01c0`/`0x007c0240`/`0x007c02d0` are four more siblings in the same message-box
family (codes `0x35`/`0x37`/`0x36`/`0x39`), each reached from multiple unrelated gameplay/mission
call sites rather than one dedicated opener; `0x007c0d50` (code `0x43`) is an isolated UI-flag
screen; `0x005f0770` (code `0x33`) is a third member of a "bare data-ref, per-object class-flag
test" shape already seen in `0x005ee2c0`/`0x00614530` above — plausibly more sub-dialog confirm
flows, still not a top-level opener; `0x006c9400` (code `0x40`) is an object-condition/percentage
warning popup; `0x006ee130` (code `0x3f`) is an activity/diversion-completion results screen
reached only through a per-entity state-dispatch table; `0x0080cc90` (code `0x28`) is a
world-interactable (shop-type) open/close toggle; `0x007af660`/`0x007af6c0` both push codes `9`
then `10` — the SAME two screen codes mode 6's control-scheme registrar already uses, above — a
controller-connect/disconnect reconfiguration-prompt pair, not pause/phone. The previously-uncertain
`0x0072d660` is now CONFIRMED to genuinely call `0x007b4750` (code `0x44`): it is exactly the
already-documented 20-entry cutscene-lifecycle dispatcher on `0x0153b520`
(`spec-lua-api-behaviour.md` §26.25), gated on cutscene-only flags — a real call site, just an
unrelated subsystem. **The next step is no longer more `0x007b4750` call-site enumeration (that
list is exhausted) but tracing backward from the input side instead** — either the shared
game-pause depth counter's ~60 push sites (§37.3) or the control-action/button-map registration
for the conventional pause/menu button, neither chased yet. A recurring "is screen N already
pushed" helper trio (`0x007b3ba0`/`0x007b3c50`/`0x007e1f40`) showed up across several of the
ruled-out subsystems and is itself unopened — tracing its own callers exhaustively might surface
additional push-adjacent call sites that don't go through `0x007b4750` directly.

**LANDED 2026-10-10, same rewrite — the pause-menu-open binding, via the input side.** The
project's standing input-action vocabulary is a 166-entry `{value, name}` CBA table, read directly
from the real image at `0x01122160` (not merely cited): entries `0x55`/`0x56`/`0x57` are
`CBA_GAC_PAUSE_MENU`/`CBA_GAC_MAP_MENU`/`CBA_GAC_CELL_OPEN` respectively, sitting consecutively in
the `CBA_GAC_*` cluster. A third input-query primitive was identified alongside the two already
documented (`0x005bb530`, `0x005bdfe0`): `0x005bbdd0(actionId)`, single argument, at least some
call sites discarding its return value — an edge "acknowledge/consume" idiom rather than a pure
query. A per-frame update routine, `0x00673160` — reached only through a function-pointer table at
`0x01131bb4`, not a direct call site, consistent with a per-object virtual/dispatch update method
— checks a flag at its own object's `+0x21c`; when clear (menu not already open), it acknowledges
`CBA_GAC_MAP_MENU` (`0x56`) via `0x005bbdd0` (return discarded), then queries `CBA_GAC_PAUSE_MENU`
(`0x55`) via `0x005bdfe0(0x55, 0)`. When that query returns non-zero, it calls
`0x007c1ef0(1, 0, 0, 0)`, which runs three negative guard checks (`0x007e1f40`, `0x007aaf00`,
`0x007afb90` — each already known elsewhere as "already in a blocking UI/cutscene state" tests)
and, when none fire and two further one-shot flag bytes are both clear, calls the project's own
already-confirmed shared game-pause push primitive, `0x007074a0(4, 1)` (giving the previously
only-hypothesized mode-4 sentinel its first concrete call site, §9 item 5), then builds and shows
a UI dialog whose literal name string, read directly from `.rdata` at `0x01157184`, is
`"dialog_pause_show"`. **HIGH CONFIDENCE** (full disassembly of the chain; the "this is the
pause-open binding" reading rests on three independent corroborating facts — the action name, the
dialog string, and reuse of the already-confirmed pause-push primitive with its
previously-hypothesis-only mode-4 argument — but has not been confirmed by a live-game input test,
so it is not bumped to CONFIRMED). The phone-menu's own open binding was NOT found by the same
method: `CBA_GAC_CELL_OPEN` (`0x57`) exists in the table but not one of the 156 call sites across
all three input-query primitives passes it anywhere in the binary — a clean, exhaustive negative,
not an unexplored gap (§9 item 8, `spec-cellphone-menu-runtime.md` §2/§7).

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

1. ~~Where fresh-game player-record defaults (cash 5700, wardrobe, the 350-entry unlockables table)
   actually get written when no save is loaded~~ **NARROWED 2026-10-10 — one of the two named
   candidate paths is now a CONFIRMED negative, the other traced to a concrete constructor address
   without finding the literal:**
   - **Candidate (a), the save-load gate's failure path, is a CONFIRMED dead end** (independently
     re-verified against the live Ghidra server before splicing). `0x00B94B90`'s header check
     (`0x00B97310`, `*(buffer+8) != 94`) on failure only tears down a pre-existing save-request
     object and returns — it never calls the load driver `0x00B98C00`, confirmed directly: the
     `CALL 0x00b98c00` instruction sits on a separate, later branch, past an intervening `RET` on
     the failure path. Nothing here constructs a fresh player record.
   - **Candidate (b), the player-record constructor, was traced to its end — CONFIRMED for the
     chain, OPEN for the literal.** The live player-record pointer (global `0x0262edfc`, getter
     `0x009da4e0`) is written exactly once, at an early engine/session-init stage (`0x0091f550` →
     `0x00941c90` → `0x009e1fc0`, caller chain re-verified directly against the Ghidra server),
     independent of "New Game" vs "Continue" — architecturally unrelated to the menu routine
     `0x007ae950` already cleared in §2. `0x009e1fc0` builds the object through the generic entity
     factory (`0x00456e30`/`0x00456be0`) using class id `0x21`, confirmed as the engine's "player"
     class by its descriptor constructor `0x009e8400` (literal string `"player"`, re-verified
     directly). That class's real per-instance constructor is `0x009e8660` → `0x009e85f0` (fixed
     `0x28D0`-byte pool allocator) → `0x009e85a0` → `0x009de970` (the actual placement constructor).
     `0x009de970` and its five direct callees (`0x00954340` — the shared "character" base
     constructor, `0x0056db50`, `0x009db9c0`, `0x00da4130`, `0x00d9e140`) were exhaustively searched
     for the literal `0x1644` (5700 decimal) and for any write to the confirmed live cash field at
     player-record `+0x1ca0` (the same field §10.1/§10.3 of `spec-save-format.md` names at save
     offset `0x4A70` ÷100, clamped ±2,000,000,000 by `0x00960160`): **none found.**
   - **Still OPEN (cash only — see below, CLOSED 2026-10-11, for unlockables/wardrobe):** the
     actual write site of the 5700 default. HYPOTHESIS, not confirmed: since this engine routes
     essentially every other cash change (purchases, rewards, cheats) through one shared
     `cash_adjust`-tagged setter (`0x0094d920` family) rather than a raw field write, the starting
     5700 may likewise be granted by an intro-mission/tutorial Lua script call into that same
     native, rather than hard-coded in engine construction code — the most promising next lead, not
     traced. A literal 5700 disguised as a different encoding (e.g. `57*100` via two immediates, or
     a float constant) inside `0x00954340` would also not have been caught by this pass's own
     literal-grep method.

   **Correction before chasing that lead: the raw save value 5700 is cash ×100 — the real,
   player-visible starting amount is $57.00** (`spec-save-format.md` line ~736, "new game 5,700
   (= 57.00)"; `0x0094d920`'s own ×100 scaling, §18.1 of `spec-lua-api-behaviour.md`, confirms a
   script-level grant would pass `57`, not `5700`). This was checked and found to matter: a search
   for the literal `5700` across the whole Lua corpus finds nothing relevant.

   **Further narrowed 2026-10-10 (second pass, data side + broader engine census):**
   - **The prologue/bank-heist mission file itself, re-confirmed and re-checked.** There is no
     separate `m00`/prologue-named mission file in the Lua corpus — `m01.lua` IS the bank-heist/vault
     opening, confirmed by its own tweak-parameter names (`M01_VAULT_TARGET_TIME1/2`,
     `M01_seconds_to_wait_for_gat_to_kill_guard`, `M01_Get_Robbed_Line_Time`, `M01_HELI_DAMAGE`), not
     a later mission mistaken for it. It contains zero occurrences of the string `"cash"` and zero
     `cash_add`/`game_award_cash` calls. **[CONFIRMED — script text; this is the right file, not a
     wrong-file false negative.]**
   - **`player_presets.xtbl` (the shipped source of the new-game gender/ethnicity default,
     `spec-save-format.md` §10.4) has no cash field.** Its 100-byte `Preset` record is 24/25 dwords
     mapped (`spec-tables-customization.md` §16.2): name/display hashes, preset-grid and
     regional-preset arrays, hair/hair-length/hair-colour/skin-colour, up to 5 composite layers, and
     the `Race`/`Gender` bytes at `+0x5C`/`+0x5D` — nothing else. **[CONFIRMED — disassembly +
     empirical, cited section.]** A corpus-wide grep of every `tools/*xtbl*` file for
     `starting_cash`/`initial_cash`/`Starting_Money`-shaped names found nothing; the only `Cash`
     fields anywhere in the base xtbl set are `gameplay_constants.xtbl`'s cash-pickup glow colour and
     its unrelated `Coop_Meta_Game > Reward_Cash` (500, a co-op point-to-cash rate) — neither is a
     new-game default. **No player-defaults-style xtbl exists in this extraction. [CONFIRMED —
     corpus-wide grep, honest negative.]**
   - **Full xref census of the shared cash setter, both layers, independently re-verified against
     the live Ghidra server before splicing (41 uses/32 functions for `0x0094d920`, 9 uses/8
     functions for `0x0094a800` — matches exactly).** Three of the strongest-looking candidates were
     decompiled and ruled out by address: `0x009e2440` (the respect-award routine) calls the setter
     only on its level-cap overflow branch — a confirmed "respect converts to cash past level 50"
     mechanic, not a grant; `0x00703840` calls it twice, each time with the live cash field
     (player-record `+0x1ca0`) *negated* first — a cash-loss/fine routine (busted/hospitalized
     penalty), the opposite of a grant; `0x009e1fc0` (the already-traced player-pointer constructor)
     was re-read in full with neither setter call nor the literal `0x1644` found. **[CONFIRMED —
     disassembly, for all three.]**
   - **One further level of the constructor chain exhausted.** `0x009e85a0` calls only `0x009de970`
     — nothing else. `0x009e85f0` (previously documented only as "a fixed `0x28D0`-byte pool
     allocator") turns out to also call one previously-undocumented sibling, `0x005f6640`,
     immediately beforehand — a 16-bit linked free-list allocator-bookkeeping routine, no
     cash-setter call, no `5700`/`0x1644` literal. This closes the "a sibling call at this link does
     the defaults init" possibility specifically. **[CONFIRMED — disassembly.]**
   - **Still genuinely OPEN, and the strongest remaining lead:** none of the above is where the 5700
     default comes from. A true whole-binary immediate-operand scan for `0x1644` is not possible
     with this project's current Ghidra server tooling (`func`/`xref`/`ptrs`/`range`/`calls`/`lua`/
     `str` modes only — no constant-search mode exists; stated honestly, not fabricated). The single
     best untried lead, not chased this pass: **`0x00708330`**, the game-start step
     `spec-save-format.md` §11.1 (step 3a) already identifies as the place that decides "fresh game"
     vs. "loaded save" and is independently established elsewhere (`spec-lua-api-behaviour.md`
     §16.8) as the step that applies the `player_presets.xtbl` default identity when no save exists.
     If the cash default is set anywhere outside the raw object constructor, this specific
     "apply fresh-game defaults" step — not the constructor chain, now fairly exhaustively checked —
     is the most promising place to look next.

   **`0x00708330` traced in full, 2026-10-10 — the fresh-game branch is CONFIRMED to NOT write any
   of the three defaults.** Opened whole (full disassembly + decompile, single pass): the documented
   save-vs-fresh-game gate is confirmed exactly at `0x0070867e`
   (`TEST byte ptr [EAX+0xd4],0x4`/`JZ 0x007086a1`, independently re-verified against the live
   Ghidra server before splicing), and across the whole function's ~140 callees, zero are the
   cash-setter family (`0x0094d920`/`0x0094a800`), zero reference the unlockables table's base
   address (`0x01522750`), and zero are `pcr_add_default_clothing` (`0x008306a0`) — also
   independently re-verified (grepped the full disassembly dump directly, confirmed zero
   occurrences). **The fresh-game branch itself (`0x007086a1`-`0x00708701`) does exactly four
   things, none of them cash/unlockables/wardrobe:** (1) two gate checks; (2) `0x00827010`
   (decompiled in full) — CONFIRMED to be exactly the `player_presets.xtbl` default-identity
   applier already named in `spec-lua-api-behaviour.md` §16.8, purely visual/mesh-component
   appearance swapping, no cash/clothing-inventory/unlockables reference anywhere in its own
   ~21-callee list; (3) a one-byte fresh-game flag clear plus a conditional call to `0x00b9a250`
   (confirmed, re-verified: `0x00708330` is its ONLY caller anywhere in the binary) — session/
   world-state global reset, no player-record offsets touched; (4) `0x0084bb20`/`0x009e17f0` —
   newly characterized as the "place the player entity into its starting zone" step (writes
   player-record `+0x27b4`), unrelated to economy/inventory. **This closes out `0x00708330` as a
   candidate — the single best untried lead named above is now exhausted with a clean negative**,
   on top of the save-load gate, the constructor chain, and both cash-setter layers' full caller
   censuses already checked.

   **Bottom line for Team B after THREE independent search angles (data: Lua scripts + xtbl tables;
   engine: both cash-setter layers' full caller lists + the constructor chain; and now the complete
   fresh-game world-start bring-up sequence) all coming up empty: the $57.00 new-game starting cash
   is most likely not "granted" by any script call, data-table default, or any of this function's
   own sub-steps that Team B can discover and replicate.** Recommended path, now on a broader
   evidentiary base: **hardcode $57.00 (raw 5700, ×100 scale) as the new-game starting cash
   constant** rather than continuing to search for a "real" data source. No further candidate call
   site was surfaced by this pass to keep looking — this question is now considered exhausted by
   static analysis short of a whole-binary constant scan (tooling this project's Ghidra server
   doesn't support) or a live capture. **Correction: the "very likely the unlockables/wardrobe
   defaults too" extrapolation above was wrong for wardrobe — see below, CLOSED 2026-10-11 — real
   script call sites exist for wardrobe; only the cash default itself stays genuinely ungranted by
   any discoverable source.**

   **Unlockables (`Auto_Unlock`) and wardrobe defaults — CLOSED 2026-10-11 (orchestrator-routed
   self-pick, direct harness query + direct file reads, no agent dispatched).**

   *Auto_Unlock.* The unlockables row-reader (`0x0071E5E0`, read in full) zeroes the state bytes
   `+0xE0`/`+0xE1`/`+0xE2` (`spec-tables-progression.md` §4.3) unconditionally at parse time
   (`EBX=0` from `XOR EBX,EBX` at entry, written through at `0x0071e637`-`0x0071e643`) and writes
   the parsed `Auto_Unlock` enum to `+0xF4` separately (`0x0071e64f`/`0x0071e81d`) — **confirmed by
   direct disassembly: the loader itself never conditionally sets the "unlocked" state bit from
   `Auto_Unlock`'s value; that application happens in a separate, not-yet-located runtime step.**
   However, a direct read of the real base `unlockables.xtbl`
   (`tools/ai_base_xtbl/unlockables.xtbl`) shows exactly ONE row with `Auto_Unlock` set at all
   (`Silently`, matching `spec-tables-progression.md`'s own "only `Silently`... 1 [row]" count) —
   and that row is **`HomieSlot1`** (`Category=Homies`, `Is_DLC=Sometimes`), the same name
   `spec-save-format.md`'s own save-id table already resolves at save slot 103. This lines up
   exactly with `spec-save-format.md` §10.6's own empirical finding that the new-game save's state
   set A (purchased/active) has population **exactly 1**. **HIGH CONFIDENCE, not disassembly-
   confirmed end-to-end: the single Auto_Unlock row and the single new-game active bit are the same
   unlockable** (`HomieSlot1` — the player's first homie-follower slot is silently active from the
   very first save) — the runtime function that actually reads `+0xF4` and sets `+0xE0`/`+0xE1`
   accordingly was not located this pass (checked: not in the row reader itself; a precise address
   search would need either an xref census keyed on a non-fixed per-record offset, not attempted
   with current tooling, or a live capture of a fresh-game boot).

   *Wardrobe defaults.* `pcr_add_default_clothing` (`0x008306a0`) — already fully characterized
   behaviorally in `spec-lua-api-behaviour.md` §43.1 (empties the owned-clothing inventory, then
   re-adds only the default garments for the player's gender/ethnicity) but previously marked OPEN
   for "whether any shipped script calls it on an established character." A direct project-wide
   grep of the real Lua corpus (`scratchpad/ui_lua_dumps/*.lua`) finds exactly **one** real call
   site: `store_character.lua`'s `store_character_lineup_to_customization()` — the character
   "lineup" screen's own transition handler, which applies the player's chosen preset
   (`pcr_apply_preset(preset_name, "figure", regional_preset_name, false)`) and immediately calls
   `pcr_add_default_clothing()` with the script's own comment: *"We want the character to have
   clothes on at the top menu, and we need to change to preset clothing."* This is the character-
   creation/preset-selection screen shown at new-game start (not the in-game plastic surgeon —
   the sibling function `store_character_creation_strip_clothes()` explicitly gates on
   `pcr_is_plastic_surgeon() == false`, confirming the creation-flow scripts distinguish the two
   contexts). **CONFIRMED (one real call site, in the expected new-character-creation context) —
   this closes the wardrobe-default half of this item: the default outfit is applied by this one
   script call, driven by whichever lineup preset the player picked, not by a separate xtbl default
   or a hardcoded engine step.** Only ONE call site was found in this pass's accessible corpus — an
   earlier note in `spec-lua-api-behaviour.md` §43.1/§43.2 cites "Team B reports 2 call sites in 2
   scripts"; this pass's own grep does not reproduce the second one, flagged honestly rather than
   assumed to exist.

   - **Evidence:** direct disassembly of the unlockables row reader (`0x0071E5E0`, full body, via
     the Ghidra query server); a direct read of the real base `unlockables.xtbl` file on disk
     (`tools/ai_base_xtbl/unlockables.xtbl`) for the `Auto_Unlock` row census; a project-wide grep
     of the real extracted Lua corpus for `pcr_add_default_clothing` call sites, with the matching
     function's own surrounding context read for confirmation.
   - **Confidence:** CONFIRMED for the loader's own zero-init behavior (no conditional state-bit
     write from `Auto_Unlock` inside `0x0071E5E0`) and for the one real `pcr_add_default_clothing`
     call site and its new-character-creation context. HIGH CONFIDENCE, not disassembly-end-to-end,
     for `HomieSlot1` being the specific unlockable behind the new-game set-A bit (strong
     correlation: unique `Auto_Unlock` row count matches unique new-game active-bit count exactly,
     but the connecting runtime function was not found).
   - **Unknowns:** the exact runtime function that reads `Auto_Unlock` (`+0xF4`) and applies it to
     the state bits — not located; whether a second `pcr_add_default_clothing` call site exists
     outside this pass's accessible Lua corpus (DLC scripts or otherwise), per the earlier "2 call
     sites in 2 scripts" note this pass could not reproduce; whether `pcr_add_default_clothing`'s
     own gender/ethnicity default-garment table has its own separate xtbl source (not traced this
     pass — `spec-lua-api-behaviour.md` §43.1 already covers the native's own internal mechanism).
2. ~~The literal pause-menu-open input binding~~ **RESOLVED (HIGH CONFIDENCE) 2026-10-10 — pivoted
   to the input side after the screen-push side was exhausted (see the trail below), and landed
   it.** The pause-menu-open binding is keyed off the CBA action table's `0x55` (85) entry
   (confirmed in the real 166-entry table at `0x01122160`, immediately adjacent to `0x56`/`0x57` —
   see §7 for the full chain and the table region). This resolves this item for the pause side; the
   phone side's own binding is still OPEN (new item below).

   **Narrowed 2026-10-10 (earlier the same day), then exhausted on the screen-push side:** the push
   primitives themselves (`0x007b4640`/`0x007b4750`) are fully decompiled; EVERY known call site of
   either (~35 total) was individually characterized and none of them is the pause-menu or
   phone-menu open; confirmed unreachable from Lua (exhaustive grep over the full UI Lua layer).
   This exhaustion is what forced the pivot to the input side that resolved the item above.
3. The real identity of UI screen codes `5`/`0xb`/`0x11`/`9`/`10`/`0x19` the various dispatchers above
   branch on (§6/§7).
4. `0x006fe180`'s own body — the consumer of new-game's 20-byte transition-request template (§2).
5. `0x0024e4860`'s `+0x27c` field as "the requested game-mode" (mode 4) — the sentinel-value coincidence
   is suggestive, no consumer traced (§1/§2).
6. `0x0087f440`'s call chain as single-player session construction — a concrete new lead for an existing
   open item in the session/coop material, explicitly not chased further (§2).
7. `0x007aebb0`'s real-world exposure as a machinima/developer tool (§5); `0x007aec20` (§6); `0x007b4750`'s
   own body (§7).
8. **The phone/cellphone-menu-open input binding — OPEN, clean exhaustive negative (2026-10-10).**
   The CBA table confirms a distinct action valued `0x57` (87), `CBA_GAC_CELL_OPEN`, immediately
   after the pause/map-menu actions — but not one of the 156 call sites across all three known
   input-query primitives (`0x005bb530`/`0x005bdfe0`/the newly-identified `0x005bbdd0`, §7) passes
   `0x57` anywhere in the binary. This is a clean negative, not an unexplored gap: the phone's open
   binding is NOT reachable through any currently-known CBA-table query primitive. Best-supported
   remaining lead, not yet attempted: either a fourth, still-undiscovered query primitive reading
   the same table by index, or a bypass of the semantic CBA layer entirely via a raw
   physical-button/edge-bit path (as opposed to the remappable action layer) — see also
   `spec-cellphone-menu-runtime.md` §2/§7.

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

- **2026-10-11 — §9 item 1: unlockables (`Auto_Unlock`) and wardrobe defaults CLOSED** (SPEC TEAM,
  self, direct Ghidra query + direct file reads, no agent dispatched). The unlockables row reader
  (`0x0071E5E0`) zeroes the state bits unconditionally and never applies `Auto_Unlock` itself — but
  the real base `unlockables.xtbl`'s only `Auto_Unlock` row (`HomieSlot1`, `Silently`) matches the
  save format's own empirical "exactly 1 active bit at new game" finding (HIGH CONFIDENCE link, not
  disassembly-confirmed end-to-end). Wardrobe: found the one real `pcr_add_default_clothing()` call
  site in the accessible Lua corpus, `store_character.lua`'s lineup-to-customization handler —
  confirms the default outfit comes from the new-game character-creation preset screen, not a
  separate xtbl default. Corrected the stale "very likely… not granted" extrapolation for wardrobe
  in place; left the cash-default conclusion (hardcode $57.00) untouched, since that part is still
  genuinely unresolved. Cleanroom: no leaked source, Ghidra auto-names used only per this file's own
  established main-exe convention.

- **2026-10-10 — §7/§9: pause-menu-open input binding LANDED (HIGH CONFIDENCE), via the button-map
  route; phone's own binding still OPEN, clean negative** (SPEC TEAM, agent `adacb57c01da892c6`).
  Read the real 166-entry CBA action table directly from `0x01122160`: `CBA_GAC_PAUSE_MENU`
  (`0x55`), `CBA_GAC_MAP_MENU` (`0x56`), `CBA_GAC_CELL_OPEN` (`0x57`) sit consecutively. Identified
  a third input-query primitive (`0x005bbdd0`) alongside the two already documented. Traced the
  full chain: per-frame dispatch `0x00673160` queries `CBA_GAC_PAUSE_MENU` via `0x005bdfe0`, calls
  `0x007c1ef0` on a hit, which (after 3 guard checks) calls the already-confirmed shared pause-push
  primitive `0x007074a0(4, 1)` — giving the previously-hypothesis-only mode-4 sentinel its first
  concrete call site — and shows a dialog literally named `"dialog_pause_show"` in `.rdata`. Phone's
  own binding (`CBA_GAC_CELL_OPEN`, `0x57`) was checked the same way and NOT found: none of 156
  call sites across all 3 known query primitives passes it — a clean exhaustive negative, not an
  unexplored gap; best remaining lead is a 4th undiscovered query primitive or a raw
  physical-button bypass of the CBA layer entirely. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-10 — §7/§9: all ~35 push-screen call sites exhausted, none is the pause/phone open —
  search pivots to the input side** (SPEC TEAM, agent `a9f56b402bc4788b5`). Traced all 12
  remaining `0x007b4750` call sites plus re-checked one uncertain lead (`0x0072d660`, confirmed to
  genuinely call it, for the unrelated cutscene-lifecycle dispatcher). Every one resolves cleanly
  to a different subsystem: a generic parameterized message-box family (6 sites), object-condition/
  activity-result popups (2 sites), a single-purpose UI flag toggle, a world-interactable
  shop-type toggle, the already-documented controller-reconfiguration prompt pair (same codes
  `9`/`10` as mode 6), and the cutscene dispatcher. **No input-query or per-frame-tick call was
  found feeding any of them** — the literal pause/phone-open binding is NOT reachable via further
  screen-push enumeration; the concrete next step is now the shared pause-depth counter's ~60 push
  sites (§37.3) or the control-action/button-map registration directly, not more `0x007b4750`
  callers. Full raw research: `scratchpad/phone_pause_research_2026-10-10/` (shared with the prior
  pass). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §7/§9: pause-menu push-primitive decompiled, search narrowed but not closed; new
  phone-menu cross-reference** (SPEC TEAM, agent `a9f302a03e2e9c69c`). Fully decompiled both
  push-screen primitives (`0x007b4640`/`0x007b4750`) and ruled out 6 of their ~35 call sites as
  unrelated subsystems (map ping, world trigger, horde, co-op replication, host migration).
  Confirmed by exhaustive grep over the complete `interface.vpp_pc` Lua layer that the pause-open
  transition is unreachable from Lua — native-only, as suspected. A dozen untraced call sites
  remain as the concrete next step. New companion file `spec-cellphone-menu-runtime.md` documents
  the phone menu's full screen/document structure, which shares the same "unreachable from Lua"
  open-transition shape. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check
  0 hits.

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
- **2026-10-10 — §9 item 1 (fresh-game player-record defaults) NARROWED** (SPEC TEAM, agent
  `a2f20c66910b6c11a`). Candidate (a), the save-load gate's failure path, is now a CONFIRMED dead
  end — independently re-verified: it only tears down a pre-existing save-request object and
  returns, never calls the real load driver. Candidate (b), the player-record constructor, traced
  all the way to its real placement constructor (`0x009de970`, via the engine's generic entity
  factory, class id `0x21` confirmed as "player" by its own descriptor string) — but the literal
  cash default `5700`/`0x1644` was not found anywhere in that constructor or its 5 direct callees.
  New HYPOTHESIS for the next pass: since this engine routes other cash changes through one shared
  `cash_adjust`-tagged setter, the starting 5700 may be granted by an intro-mission/tutorial Lua
  script call rather than hard-coded in engine construction code. Unlockables/wardrobe defaults not
  reached this pass. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — §9 item 1: starting-cash search exhausted on both the data and engine sides —
  recommending Team B hardcode $57.00** (SPEC TEAM, agent `ad80b15c3f97bb244`). Correction before
  this pass started: the raw save value 5700 is cash ×100 — the real starting amount is $57.00, not
  $5700 (so a Lua-side grant would read `cash_add(57,...)`, not `5700`). Re-confirmed `m01.lua` IS
  the bank-heist prologue (not a later mission) and has zero cash code; confirmed
  `player_presets.xtbl`'s full record has no cash field, and no player-defaults-style xtbl exists
  anywhere in the extraction. Full xref census of both cash-setter layers (41 uses/32 functions,
  9 uses/8 functions — independently re-verified against the live Ghidra server before splicing)
  found no new-game-reachable caller among the 3 strongest candidates checked. One further
  constructor-chain level exhausted (a previously-undocumented sibling, `0x005f6640`, turned out to
  be pool-allocator bookkeeping). **Recommended for Team B: hardcode $57.00 as the new-game
  starting-cash constant** rather than keep searching — a well-evidenced negative across the two
  most plausible source locations, not an unexplored gap; one untried lead (`0x00708330`, the
  fresh-game-defaults application step) flagged for anyone who wants to keep looking. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — §9 item 1: `0x00708330` exhausted as a clean negative, closing the fresh-game
  defaults search** (SPEC TEAM, agent `abe4d271a077162e5`). Opened the last untried lead in full:
  the fresh-game branch (`0x007086a1`-`0x00708701`, gate independently re-verified at `0x0070867e`)
  does exactly 4 things — two gate checks, applying the `player_presets.xtbl` identity default
  (pure appearance, no economy/inventory reference), a session/world-state flag reset
  (`0x00b9a250`, re-verified as having `0x00708330` as its only caller anywhere in the binary), and
  placing the player in its starting zone — none of them cash, unlockables, or wardrobe. Zero
  occurrences of the cash-setter family, the unlockables table base address, or
  `pcr_add_default_clothing` anywhere in the function's ~140 callees, independently re-verified
  against the live Ghidra server. This closes the question to a well-evidenced exhausted state
  across 3 independent search angles (data, both cash-setter layers, and now this function) —
  hardcoding $57.00 stands as the recommendation, with no further candidate surfaced. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
