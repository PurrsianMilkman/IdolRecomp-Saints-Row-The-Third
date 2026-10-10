# Cellphone / pause-cell menu runtime

**Scope.** The in-game "cell" menu system — the icon-grid screen the player reaches from the
pause-adjacent UI, and its nine-plus sub-documents (map, missions, saintsbook, rewards, music,
camera, phone/contacts, extras, cheats, stats, cash store). This is a structurally parallel,
separately pushed screen-stack subtree to the pause menu proper (`spec-frontend-menu-runtime.md`
§7), sharing the same underlying push-screen primitives (`0x007b4640`/`0x007b4750`) but with its
own screen-code enum, its own transition/tween primitive, and its own pop convention. Individual
Lua-bound natives this subtree calls into (`cellphone_dial`, `cellphone_choose_vehicle`, etc.) are
already documented in `spec-lua-api-behaviour.md`'s cellphone material — this file connects those
natives to the real UI call chain that reaches them, rather than re-deriving the natives
themselves.

**Confidence key:** CONFIRMED (read directly from the real extracted Lua source,
`scratchpad/ui_lua_dumps/`, ~400 files covering the full `interface.vpp_pc` Lua layer); HIGH
CONFIDENCE (follows from structure but not independently exercised); OPEN (flagged honestly, with
a concrete next step where one exists).

## 1. Screen-code enum and document table — CONFIRMED

Read directly from `cell_foreground.lua`:

```
ID_MAP=0  ID_MISSIONS=1  ID_SAINTSBOOK=2  ID_REWARDS=3  ID_MUSIC=4  ID_CAMERA=5  ID_PHONE=6  ID_CASH=7  ID_EXTRAS=8

CELL_SCREEN_MAIN=1       → doc "cell_menu_main"      (the 3x3 icon-grid you open into)
CELL_SCREEN_MAP=2        → doc "pause_map"
CELL_SCREEN_MISSIONS=3   → doc "cell_missions"
CELL_SCREEN_SAINTSBOOK=4 → doc "cell_saintsbook"
CELL_SCREEN_REWARDS=5    → doc "cell_rewards"
CELL_SCREEN_MUSIC=6      → doc "cell_music_menu"
CELL_SCREEN_MUSIC_SUB=7  → doc "cell_music_sub"
CELL_SCREEN_PLAYLIST=8   → doc "cell_playlist"
CELL_SCREEN_CAMERA=9     → doc "cell_camera"
CELL_SCREEN_PHONE=10     → doc "cell_phone"          (contacts/garage/call)
CELL_SCREEN_EXTRAS=11    → doc "cell_extras"
CELL_SCREEN_CHEATS=12    → doc "cell_cheats"
CELL_SCREEN_STATS=13     → doc "cell_stats"
CELL_SCREEN_CASH=14      → doc "store_crib_cash"
```

**Correction to how this subtree is commonly described:** "contacts/map/missions/cheats" are not
four tabs of one VINT document — they are four of **nine-plus separate pushed screens**, each its
own VINT document, reached through a shared icon-grid main menu (`cell_menu_main`). "Cheats" is
nested one level deeper still, inside `cell_extras` (the "Extras" icon leads to
`cell_cheats`/`cell_stats`). "Contacts" specifically is the `cell_phone` document
(`CELL_SCREEN_PHONE`), which itself has 3 sub-pages implemented as **three groups within the one
document** (toggled visibility plus a slide tween, not three separate pushed screens) — this is
the one place in the whole subtree where "multiple pages in one VINT doc" genuinely applies;
everywhere else is a separate `push_screen`.

## 2. Top-level open — OPEN (same shape as the pause menu)

`cell_foreground_init()`/`cell_background_init()`/`cell_menu_main_init()`
(`cell_foreground.lua:113`, `cell_menu_main.lua:122`) set up the outer frame and icon grid, but
**nothing in any extracted Lua file ever calls `push_screen("cell_foreground")`,
`push_screen("cell_background")`, or `push_screen("cell_menu_main")`** — confirmed by the same
exhaustive grep method used for the pause menu (`spec-frontend-menu-runtime.md` §7). The
top-level open is native-only, exactly like the pause menu's own unresolved binding.

**Exhausted 2026-10-10 — the full `0x007b4750` call-site list (all ~35 sites, shared with the
pause menu's own search) has now been individually traced, and none of them opens
`cell_foreground`/`cell_background`/`cell_menu_main` either.** Every site resolves to a different
subsystem entirely — a generic message-box family, object-condition/activity-result popups, a
world-interactable shop-type toggle, the already-documented controller-reconfiguration prompt
pair, and the cutscene-lifecycle dispatcher (full breakdown: `spec-frontend-menu-runtime.md` §7/§9).
The hypothesis that pause and phone share one native dispatcher gated on context remains unproven
either way — this pass simply closes out the screen-push call-site list without separating the
two. **OPEN**, and the next step is no longer more screen-push enumeration (exhausted) but the
input side directly: either the shared pause-depth counter's push sites or the control-action/
button-map registration for whichever button conventionally opens this subtree — the same pivot
now recorded for the pause menu.

## 3. Icon-grid dispatch — CONFIRMED

Once open, `cell_menu_main_button_a` → `cell_menu_handle_state(state)` (`cell_menu_main.lua:275`)
is the one dispatcher for all 8 icons — a clean `if`/`elseif` chain of
`push_screen("pause_map"|"cell_missions"|"cell_saintsbook"|"cell_rewards"|"cell_music_sub"|"cell_phone"|"cell_extras"|"store_crib_cash")`,
each gated on a `Data[ID].is_enabled` flag computed in `populate_data_is_enabled()`
(mission-completion-gated: `m02`/`m03` unlock the saintsbook/other icons, via the `cell_is_mission_complete`
native family already documented in `spec-lua-api-behaviour.md` §27.2).

## 4. Exit convention — CONFIRMED

Every sub-screen exits via a symmetric `pop_screen()` stack-unwind with explicit comments naming
each popped layer, e.g. `cell_phone.lua:939` `cell_phone_exit_to_game()`:

```lua
pop_screen() -- cell phone
pop_screen() -- cell main
pop_screen() -- cell fore/background
```

This 3-deep pop pattern for "exit the whole subtree to gameplay" is uniform across every sibling
screen (`cell_missions.lua:205-207`, `cell_rewards.lua:509-511`, `cell_saintsbook.lua:279-281`,
`cell_music_menu.lua:84-87`, `cell_extras.lua:101-104`), versus a 1-deep `pop_screen()` for "back
to the phone's own main icon grid" from within any sub-screen.

## 5. The `cell_phone` document's internal structure — CONFIRMED

Full read of `cell_phone.lua`:

- **Init.** `cell_phone_init()` resolves the document (`vint_document_find("cell_phone")`), builds
  three `Vdo_input_tracker`s (book/garage/call), and requests homie data via
  `vint_dataresponder_request("cell_phone_pad_populate", "cell_phone_populate_list", 0, CELL_HOMIE_LIST)`.
  If the result is empty it **immediately aborts** (`cell_phone_exit(false)`) with the comment
  "no homie data... cannot enter cellphone" — the contact-list data itself is owned by a C-side
  dataresponder, not Lua.
- **Page switch.** `cell_phone_menu_show(menu_id, direction)` is the 3-page switcher: hides/shows
  `book_screen_grp`/`garage_screen_grp`/`call_screen_grp`, re-subscribes the matching input
  tracker, and optionally plays a slide tween (`cell_screen_trans_anim`) with callback
  `cell_phone_menu_trans_cb`.
- **Dial chain (contacts → native), fully traced.** `cell_phone_book_button_a` (bound to the
  `"select"` event, line 687) checks `is_available`, unsubscribes input, then calls
  `cellphone_dial(homie_index, "cell_phone_dial_connection_cb", "cell_phone_call_hangup_cb")` —
  reaching the already-documented native `0x0083d930`. The async result lands in
  `cell_phone_dial_connection_cb(success)` (line 330), branching on `CELL_CALL_INVALID`/
  `CELL_CALL_CONNECTED`/`CELL_CALL_GARAGE_SHOW` to either show a wrong-number call screen, a
  normal in-call screen, or pivot into the garage sub-page.
- **Garage/vehicle-delivery chain, fully traced.** `cell_phone_garage_button_a` (line 776) reads
  the selected vehicle's repair cost from `Garage_list:return_selected_data()`: if the player can't
  afford it, shows a cash-warning dialog; if free, calls `cellphone_choose_vehicle(data.id)`
  directly (the already-documented native `0x0083d5e0`) then exits to game; if repairs are needed,
  shows a confirmation dialog whose callback (`cellphone_garage_get_car_dialog_cb`, line 824) calls
  the same `cellphone_choose_vehicle` on confirm. **This is the full call chain from "press A in
  the garage list" to the already-documented native.** Vehicle-list population uses the same
  dataresponder pattern, different enum argument:
  `vint_dataresponder_request("cell_phone_pad_populate", "cell_phone_garage_populate", 0, CELL_VEHICLE_LIST)`
  (`CELL_VEHICLE_LIST=1` vs. `CELL_HOMIE_LIST=0`).
- **Hang-up.** `cell_phone_call_hangup_cb(success)` (line 877) branches `CELL_CALL_HANGUP` → exit
  to game, `CELL_CALL_RETURN_TO_PHONE` → back to the book page — confirming `cellphone_dial`'s own
  3rd-argument async-callback contract from the call site, not just its body.

## 6. The `cell_cheats` sub-screen — CONFIRMED structure, one new native lead

Reached via `cell_extras_handle_state(ID_CHEATS)` → `push_screen("cell_cheats")`
(`cell_extras.lua:151`). Internally a 2-level menu (category list → cheat list within category)
driven by the same dataresponder pattern (`vint_dataresponder_request("cell_cheats_populate", ...)`).
Toggling a cheat: `cell_cheats_button_a` → `cell_cheats_activate_cheat(Sub_menu[selection].cheat_index)`
(line 366). **`cell_cheats_activate_cheat`'s own definition was not found in `cell_cheats.lua`** —
either a shared cheats-support Lua module not yet read, or (given the naming convention matches
`cellphone_dial`/`cellphone_choose_vehicle`'s own style) itself a directly Lua-bound native not
currently listed in `spec-lua-api-behaviour.md`'s cellphone-native section. **OPEN** — a concrete
lead for whoever extends that section next.

## 7. Open items

1. The literal native binding(s) that push `cell_foreground`/`cell_background`/`cell_menu_main`
   onto the screen stack (§2) — same unresolved shape as the pause menu's own open binding
   (`spec-frontend-menu-runtime.md` §7/§9). **Exhausted on the screen-push side 2026-10-10:** all
   ~35 known `0x007b4750` call sites traced, none is this binding — pivot to the input side
   (pause-depth push sites or control-action/button-map registration), not attempted yet.
2. `cell_cheats_activate_cheat`'s own identity — likely a directly Lua-bound native, not yet in
   `spec-lua-api-behaviour.md`'s cellphone-native section (§6).
3. The 8 sibling documents beyond `cell_phone` and `cell_cheats` (`pause_map`, `cell_missions`,
   `cell_saintsbook`, `cell_rewards`, `cell_music_menu`/`_sub`/`_playlist`, `cell_camera`,
   `cell_extras`'s own top-level page, `cell_stats`, `store_crib_cash`) have not had their own
   internal page structure traced to this depth — `cell_phone` and `cell_cheats` were the two
   worked examples this pass covered.

## 8. Clean-room compliance note

Plain hex addresses and real Lua identifiers (variable/function names read directly from the
shipped, extracted script source) throughout. No Ghidra decompiler auto-names
(`FUN_`/`DAT_`-style), no pseudocode, no leaked Volition/Havok/Audiokinetic/RAD proprietary
material.

## Changelog

- **2026-10-10 — §2/§7: screen-push call-site list exhausted, no hit for the phone's top-level
  open** (SPEC TEAM, agent `a9f56b402bc4788b5`). Shared the pause menu's own full `0x007b4750`
  call-site trace (all ~35 sites now individually characterized); none opens
  `cell_foreground`/`cell_background`/`cell_menu_main`. Confirms the search must pivot to the input
  side for both pause and phone — further screen-push enumeration is exhausted. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new file** (SPEC TEAM, agent `a9f302a03e2e9c69c`). First map of the cellphone/cell
  menu subtree: screen-code enum and document table, icon-grid dispatch, uniform exit/pop
  convention, and the `cell_phone` document's full internal page-switch/dial/garage/hang-up chain
  traced to the already-documented natives `cellphone_dial`/`cellphone_choose_vehicle`. New
  cross-reference from `spec-frontend-menu-runtime.md` §7. Largest open items: the top-level open
  binding (unreachable from Lua, same shape as the pause menu) and `cell_cheats_activate_cheat`'s
  own identity. Full raw research: `scratchpad/phone_pause_research_2026-10-10/`. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
