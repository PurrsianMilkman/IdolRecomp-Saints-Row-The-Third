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
two.

**Button-map route tried 2026-10-10 — the pause menu's own binding landed by this method (see
`spec-frontend-menu-runtime.md` §7), but the phone's does NOT follow the same pattern.** The
project's real 166-entry CBA action table (read directly from `0x01122160`) confirms a distinct
action, `CBA_GAC_CELL_OPEN` (value `0x57`), sitting immediately after `CBA_GAC_PAUSE_MENU`
(`0x55`) and `CBA_GAC_MAP_MENU` (`0x56`) — the pause action that DID resolve cleanly to a traced
per-frame query → dialog chain. But an exhaustive check of every call site of all three known
input-query primitives (`0x005bb530`/`0x005bdfe0`/the newly-identified `0x005bbdd0` — 156 call
sites across 39 functions, all fully disassembled) found that none of them passes `0x57` anywhere
in the binary. **This is a clean, exhaustive negative, not an unexplored gap**: the phone's
top-level open is NOT reachable through any currently-known CBA-table query primitive, even though
its own dedicated action code exists in the table. **OPEN**, best-supported remaining leads: a
fourth, still-undiscovered query primitive reading the same table by index, or a bypass of the
semantic CBA layer entirely via a raw physical-button/edge-bit path.

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
(line 366). **RESOLVED 2026-10-10: `cell_cheats_activate_cheat` is a directly Lua-bound native**
(`0x0083b600`, sub-registrar `0x0083b6e0`). It applies a coop-disabled gate and a
confirmation-dialog flow itself, then hands off to `0x0083af50` for the actual on/off toggle and a
`"cell_cheats_cheat_activated"` event dispatch — bounds-checked, no crash shape found. Full
detail: `spec-lua-api-behaviour.md` §45.8. Cross-check with the cheats.xtbl record layout (§8 of
that file's progression-table sibling) is OPEN — not confirmed to be the same structure.

## 7. The `cell_missions` document's internal structure — CONFIRMED

Reached via `cell_menu_handle_state(ID_MISSIONS)` → `push_screen("cell_missions")` (per §3). Full
read of `cell_missions.lua`.

- **Init/data source.** `cell_missions_init()` (`cell_missions.lua:19`) resolves the document
  (`vint_document_find("cell_missions")`) and issues exactly one
  `vint_dataresponder_request("cell_missions_list_items_dr", "cell_missions_add_menu_items", 0)`
  (`cell_missions.lua:50`) — the same C-side-dataresponder pattern `cell_phone` uses for its
  contact/vehicle lists, not a bespoke mission-progress native. The callback
  `cell_missions_add_menu_items(handle, mission_name, display_name, contact_name, contact_image,
  is_new, in_progress)` (`cell_missions.lua:110`) is called once per row and also carries an
  `in_progress` flag per row, latched into the single module-level `Mission_in_progress` boolean
  (last call wins — the C side evidently only ever sends `in_progress=true` on the one active-
  mission row).
- **No separate tabs — one list, one alternate state.** There is no active/completed/side-mission
  tab structure. `cell_missions_update()` (`cell_missions.lua:116`) only ever branches on
  `Mission_in_progress`: if true, it hides `contact_grp`, shows `current_mission_txt`/
  `in_progress_txt` with `Data[1].display_name`, and does not draw the list at all; if false, it
  draws the full list via `List:draw_items(Data, 1, 4)` (a 4-row visible window). So "in a
  mission" is a full-screen replacement state, not a tab.
- **Selecting a mission.** `cell_missions_button_a` (`"select"` event, `cell_missions.lua:178`)
  does not call any dedicated native to start the mission. It posts the choice back through the
  same dataresponder channel: `vint_dataresponder_post("cell_missions_list_items_dr", "Choose",
  Data[List.current_idx].mission_name)` (`cell_missions.lua:186`), then posts
  `vint_dataresponder_post("cell_menu_state_dr", "Set", ID_MAIN)` (remembers "cell main" as the
  re-entry page) and exits the whole phone subtree. All mission-activation logic (GPS/waypoint
  set, actually starting the mission) lives entirely on the C side of the `"Choose"` dataresponder
  command — there is no further Lua-visible call chain.
- **Map shortcut.** A dedicated `"map"` input (`cell_missions_button_map`,
  `cell_missions.lua:161`) also exits the whole subtree to game (same 3-deep pop as a mission
  selection), after posting `ID_MAIN` to `cell_menu_state_dr` — pressing the map hotkey from the
  missions list backs all the way out rather than pivoting into `pause_map` within the same stack.
- **Early-exit guard.** If the dataresponder returns zero rows, `cell_missions_unlock_controls()`
  (`cell_missions.lua:213`) does not subscribe any input and instead spawns
  `thread_new("cell_missions_exit_early")`, which waits a frame and transitions straight back to
  `cell_menu_main` (1-deep pop) — the same "abort if no data" shape as `cell_phone`'s
  `cell_phone_exit(false)`, implemented as an auto-exit-to-main rather than an immediate abort.
- **Exit convention.** Matches §4 exactly: `cell_missions_exit_to_main()`
  (`cell_missions.lua:200`) is a 1-deep `pop_screen()` back to the icon grid (used for the
  `"back"` button and the no-data auto-exit); `cell_missions_exit_to_game()`
  (`cell_missions.lua:204-208`) is the uniform 3-deep pop used by both the map-hotkey and
  mission-selection paths.

## 8. The `pause_map` document's internal structure — CONFIRMED

Reached via `cell_menu_handle_state(ID_MAP)` → `push_screen("pause_map")`. Full read of
`pause_map.lua` (1747 lines); no separate map-rendering helper module is `require`/`dofile`'d —
all map-icon color/shape tables (`PAUSE_MAP_ICON_COLORS`, `Pause_map_icons_rectangle`,
`PAUSE_MAP_ICON_SMALL`) are defined inline at the top of this same file (`pause_map.lua:42-174`).

- **Init.** `pause_map_init()` (`pause_map.lua:287`) resolves the document and several fixed
  handles (`map_mask`, `filter_list`, `hint_bar_bg`, `cursor_tool`), builds a `Vdo_cell_filter` for
  the icon-category filter UI and a `Vdo_city_control` for the district-control meter, and
  registers `Pause_map_filter:filter_callback_set(game_pause_map_filter)` (`pause_map.lua:304`) —
  `game_pause_map_filter` is the already-documented native (`spec-lua-api-behaviour.md`) that
  actually changes which icon categories the C-side map renderer shows.
- **Markers/world-data — four separate subscriptions, not one dataresponder.** Unlike
  `cell_phone`/`cell_missions`, the map's live data arrives continuously via
  `vint_dataitem_add_subscription`/`vint_datagroup_add_subscription`, not a one-shot
  `vint_dataresponder_request`:
  - `PAUSE_MAP_CURSOR` → `pause_map_cursor_update` (`pause_map.lua:353`, handler at `:1293`) —
    cursor position, GPS-active flag, current `x_func` (none/place-bookmark/remove-bookmark/taxi),
    district/hood name and control percentage.
  - `PAUSE_MAP_HIGHLIGHT1`/`PAUSE_MAP_HIGHLIGHT2` → `pause_map_highlight_update`
    (`pause_map.lua:382-383`, handler at `:898`) — the two-dataitem swap driving the per-icon
    hover tooltip (icon bitmap, up to 4 text lines, GPS-target/store/crib/MAD-icon flags,
    taxi-availability).
  - `map_filter` datagroup → `pause_map_filters_populate` (`pause_map.lua:384`, handler at `:676`)
    — populates the category filter list (activities/stores/collectibles/missions/etc.),
    terminated by a sentinel `PFT_NONE` row.
  - `map_district_names` datagroup → `pause_map_district_overlay_update` (`pause_map.lua:385`,
    handler at `:1407`) — per-district name/percentage-owned overlay labels, cloned once per
    district the first time its dataitem handle is seen.

  This is a structurally different data-population pattern from both `cell_phone` and
  `cell_missions` — continuous per-frame dataitem push rather than a one-shot populate-list
  request — consistent with the map needing to track a moving world cursor rather than a static
  list.
- **Interactions, each resolving to an already-documented native** (`spec-lua-api-behaviour.md`
  §31): waypoint/GPS (gamepad `"select"`/PC right-click → `pause_map_set_gps`, `0x007dba10`);
  bookmark (gamepad `alt_select`/PC middle-click → `pause_map_add_bookmark`, `0x007df8a0`); pan
  (PC mouse-drag → `pause_map_drag_map`, `0x007d9910`); zoom (PC-only click targets →
  `pause_map_zoom(true/false)`, `0x007dbcc0`); category filter (d-pad/F1 moves the filter cursor,
  selection round-trips through the `game_pause_map_filter` callback registered at init); a
  "Stag" gang-territory-takeover sub-mode gated by `pause_map_is_stag_mode()` (`0x007d98b0`),
  confirming via a yes/no popup and granting the reward through `pause_map_stag_takeover_do_reward`
  (`0x007de1c0`).
- **Exit convention — same shape as §4, scaled by which button closed it.** `pause_map_input`
  (`pause_map.lua:649`) branches on the invoking event: `"back"` closes to the icon grid (posts
  `ID_MAIN`); `"map"` (the dedicated hotkey, pressed again while already on the map) closes but
  remembers the map itself as the re-entry page (posts `ID_MAP`). Both funnel through
  `pause_map_trans_out_complete()` (`pause_map.lua:641-647`), which always pops the map layer
  itself once and, only on the full phone-exit path, pops two more layers — the same uniform
  1-deep-vs-3-deep split documented in §4, expressed as a single shared exit function with a
  conditional tail rather than two separately named functions.

## 9. The `cell_stats` document's internal structure — CONFIRMED, structurally the simplest of
the three traced this pass

Reached via `cell_extras_handle_state(ID_STATS)` → `push_screen("cell_stats")`, sibling of
`cell_cheats` under the Extras icon (per §6). Full read of `cell_stats.lua` (324 lines) — a flat,
paginated label/value readout with no sub-pages, no categories, and no natives of its own beyond
the generic UI framework calls.

- **Init/data source.** `cell_stats_init()` (`cell_stats.lua:17`) resolves the document and issues
  one `vint_dataresponder_request("cell_stats_populate", "cell_stats_populate", 0)`
  (`cell_stats.lua:51`) — same one-shot dataresponder pattern as `cell_phone`/`cell_missions`,
  naming its own populate callback. The C-side responder named `"cell_stats_populate"` is the
  connection point to the game's underlying stat-tracking system; cross-referenced rather than
  re-derived — `spec-tables-progression.md` already documents the 217-row `stats.xtbl` table and
  its by-stat-id `complex` handler dispatch (44 named ids), which almost certainly backs whatever
  this responder reads. No native comparable to `cellphone_dial` is called directly from this file
  — the entire stat system crosses from C to Lua through this single dataresponder callback.
- **Callback shape.** `cell_stats_populate(display_name, has_value, value_str)`
  (`cell_stats.lua:86`) appends one row per call into `Cell_stats_data[]`; `has_value` gates
  whether a row is a plain label (section header) or a label+value pair. `Num_stat_data`
  increments per row and `Cell_stats_num_pages` is derived as `Num_stat_data / 10` (`10` =
  `CELL_STATS_NUM_LINES`, `cell_stats.lua:1,52`) — the "categories" are flattened into one
  continuous paged list by the C side before Lua ever sees them; there is no Lua-side
  grouping/category-tab structure beyond the page boundaries.
- **Paging, not tabs.** `cell_stats_show_page(page_idx)` (`cell_stats.lua:95`) is the only
  internal-structure mechanism: it redraws 10 fixed label/value line objects per page and updates
  a page-indicator string plus a scrollbar-position formula. `cell_stats_nav_left`/
  `cell_stats_nav_right` (`:191`/`:199`) move between pages; on PC, dragging the scrollbar header
  or clicking left/right arrow bitmaps does the same. There is no selectable/actionable item
  anywhere in this document — the `"select"` input is explicitly bound to a no-op
  (`cell_stats_nav_nothing`, `cell_stats.lua:29,207`), confirming this screen is read-only.
- **Exit convention — one level deeper than `cell_phone`.** `cell_stats_button_b` (`"back"`,
  `cell_stats.lua:218`) → a 1-deep `pop_screen()` back to `cell_extras`; the map hotkey
  (`:210`) → a **4-deep** `pop_screen()` ("stats"/"extras"/"main"/"cell frame") — one layer deeper
  than `cell_phone`'s 3-deep exit-to-game, consistent with `cell_stats` sitting one `push_screen`
  deeper in the stack than `cell_phone` does (nested under Extras, same depth as `cell_cheats`).
  This is the first of the documents traced whose exit-to-game pop count differs numerically from
  §4's 3-deep baseline, and it differs for the structurally expected reason (extra nesting level),
  not an inconsistency.

**Net result from this pass:** unlike `cell_cheats`, none of these three documents produced a new
unresolved native lead — `cell_missions` and `cell_stats` both go entirely through the generic
`vint_dataresponder_request`/`_post` framework with no dedicated native of their own; `pause_map`'s
natives were all already fully specced in `spec-lua-api-behaviour.md` §31. Remaining undocumented
siblings at this point: `cell_saintsbook`, `cell_rewards`, `cell_music_menu`/`_sub`/`_playlist`,
`cell_camera`, `cell_extras`'s own top-level page, `store_crib_cash`.

## 10. The `cell_saintsbook` document's internal structure — CONFIRMED, one new native-lead pair

Reached via `cell_menu_handle_state(ID_SAINTSBOOK)` → `push_screen("cell_saintsbook")` (per §3),
mission-completion-gated at data-population time. Full read of `cell_saintsbook.lua` (1946 lines).

- **Three internal pages in one VINT document — a horizontal filmstrip, structurally distinct from
  both `cell_phone`'s toggle-visibility pages and every other sibling's separate-`push_screen`
  pattern.** `SCREEN_TYPE_TOP`/`SCREEN_TYPE_MAIN`/`SCREEN_TYPE_SUB` (`cell_saintsbook.lua:8-10`) are
  positioned side by side at x-offsets `0`/`850`/`1700` and the active page is switched by sliding
  the shared `content` container with a tween (`cell_saintsbook_transition`, `:326-338`), driven by
  `cell_saintsbook_active_screen_set(screen_type)` (`:311-319`), which also swaps a per-screen
  `{nav, select, back, exit}` callback table (`Saintsbook_screens`, `:1923-1945`).
- **Top page = unlockable category list.** Populated by
  `vint_dataresponder_request("saintsbook_filters", "cell_saintsbook_top_populate", 0)` (`:408`).
  Categories: `SAINTSBOOK_ITEM_TYPE_ASSASSINATIONS=1`/`CHOPSHOP=2`/`CHALLENGE=3`. A category row is
  skipped entirely if `targets_unlocked==0` (`:1780-1783`) — the mission-completion gate from §3
  manifests here as "row never appears," not a disabled/greyed row.
- **Main page = flat scrollable entry list within one category.** Selecting a category
  (`cell_saintsbook_top_select`, `:679`) issues a second dataresponder request,
  `vint_dataresponder_request("saintsbook_categories", "cell_saintsbook_populate", 0, item_type)`
  (`:712`). The single generic callback `cell_saintsbook_populate(item_type, param1..param13)`
  (`:1805`) branches its field layout by `item_type` (challenge rows carry
  contact/target/units-progress/cash/respect/completed; hitman/chopshop rows carry
  contact/blurb/target/location/cash/respect/completed/`group_index`/`target_index`/`is_selected`).
- **Sub page = per-entry detail view**, reached by selecting a main-list row
  (`cell_saintsbook_sub_open_stage_1`, `:1081`/`:1225`): full target name/image, contact
  name/image, cash+respect reward preview, blurb text; a GPS-target/location block for
  assassination/chopshop entries, a progress meter for challenges.
- **Selecting an entry on the sub page is a target-marking toggle, not a launch or purchase.**
  `cell_saintsbook_sub_select` (`:1660`): no-ops if the entry is already completed; for
  assassinations calls `sb_select_hitman_target(item.group_index, item.target_index)` (`:1677`);
  for chopshop calls `sb_select_chop_shop_vehicle(item.group_index, item.target_index)` (`:1691`).
  **RESOLVED 2026-10-10: both are directly Lua-bound natives** (sub-registrar `0x00840420`,
  alongside `sb_saintsbook_is_available`) — real state-mutators toggling a per-manager-singleton
  "active target" selection, fully bounds-checked on both indices, no crash shape found. The
  chop-shop variant additionally encodes an AI-partner-claim rule the hitman variant lacks:
  selecting a target the partner has already taken pops a warning dialog instead of selecting it.
  Full detail: `spec-lua-api-behaviour.md` §45.8. No `select` action exists for challenge-type
  entries (they fall through with no effect).
- **Back-navigation depth differs per page, unlike the other siblings' uniform "back=1-up."**
  `"back"` from TOP exits the whole sub-document directly to the icon grid (there is no level
  above TOP to back into); `"back"` from MAIN returns to TOP; `"back"` from SUB returns to MAIN.
  The dedicated `"map"` hotkey exits straight to game from any of the 3 pages uniformly.
- **Exit convention — matches §4 exactly.** `cell_saintsbook_exit_final_to_menu` (`:273-275`) is a
  1-deep `pop_screen()` back to the icon grid; `cell_saintsbook_exit_final_to_game` (`:277-282`) is
  the uniform 3-deep pop. Both route through a shared close/transition path that also posts
  `vint_dataresponder_post("cell_menu_state_dr", "Set", ID_MAIN)` to remember the re-entry page —
  the same re-entry-memory idiom `cell_missions` uses (§7).

## 11. `store_crib_cash` — CONFIRMED, confirms the "crib cash transfer" hypothesis, simplest
document traced so far, exit convention does NOT fit the §4 baseline

Reached via `cell_menu_handle_state(ID_CASH)` → `push_screen("store_crib_cash")` (per §1/§3). Full
read of `store_crib_cash.lua` (288 lines, filename matches exactly).

- **What it actually is.** A one-shot animated cash-transfer screen — moving money the player has
  waiting at a bank/crib stash (`cash_from_bank`, capped at `transfer_cap`) into the player's
  actual cash total — not a generic store and not a crib-customization purchase screen. There is
  no selectable list/menu anywhere in the file; the only hint-bar entry is a single
  `CTRL_MENU_BUTTON_A` "`CRIB_CASH_DO_TRANSFER`" hint (`store_crib_cash_init`, `:37`).
- **Data population.** One `vint_dataresponder_request("store_crib_cash_request_data",
  "store_crib_cash_populate", 0)` (`:58`); callback `store_crib_cash_populate(cash_from_bank,
  player_cash, daily_cash, transfer_cap)` (`:144`) fills 4 label fields.
- **The transfer auto-fires on open; it is not player-button-gated in the normal case.** Init
  wires the intro tween's `end_event` to the empty-account popup if `cash_from_bank<=0`, else to
  `store_crib_cash_cb` (`:69-74`). That callback (`:87`) — reached automatically once the intro
  tween finishes, not from a button — calls `crib_get_cash_stash()` (source comment: "`-- C
  function call`", `:91`), which is **already documented** in `spec-lua-api-behaviour.md` §57.3:
  despite its name it returns no value; it collects a stashed-cash global straight into the
  player's cash balance and zeroes the stash. The real cash change has therefore already happened
  by the time `thread_new("store_crib_cash_transfer")` (`:92`, body `:196-263`) starts — that
  thread is a **purely cosmetic** counting-up animation with no gameplay effect of its own. If
  `cash_from_bank<=0`, an "`MENU_ACCOUNT_EMPTY`" warning popup shows instead (`:96-134`).
- **Exit convention — does NOT fit the §4 1-deep/3-deep(or 4-deep) baseline at all.**
  `store_crib_cash_exit_final` (`:189-191`) is a **single 1-deep `pop_screen()`**,
  unconditionally, regardless of which input triggered the close — there is no 3-deep "exit to
  game" path anywhere in this file; it always returns to `cell_menu_main`, never pops the whole
  phone subtree. This is the first of the traced documents whose exit shape is simpler than the
  established baseline rather than just deeper (cf. `cell_stats`'s 4-deep, §9) — structurally
  consistent with it being a brief in-and-out utility screen rather than a destination with its
  own sub-navigation.
- **No new native leads** — `crib_get_cash_stash` was the only native call in the file and it's
  already specced.

## 12. `cell_rewards` — CONFIRMED, REFUTES the "passive viewer" hypothesis: it is an actual
in-phone cash-purchase store for permanent unlocks

Reached via `cell_menu_handle_state(ID_REWARDS)` → `push_screen("cell_rewards")` (per §3). Full
read of `cell_rewards.lua` (591 lines).

- **What it shows/does.** `cell_rewards` is a real cash-purchase store for permanent
  account-level unlocks ("rewards"), built directly on the shared `store_common.lua` framework
  (`Vdo_mega_list`, `Vdo_store_popup`, `Store_header`, `Store_common_player_cash`) — the same
  generic list/purchase-popup machinery the clothing/vehicle/weapon stores use — titled
  `"STORE_UPGRADES_TITLE"` (`:53`). It is not read-only: items are purchased with cash through
  this screen, not merely displayed. This plausibly connects to `spec-activity-runtime.md`'s
  diversion-reward material as the UI surface where a diversion-unlocked reward becomes
  purchasable — but that connection is not independently verified here (the unlock-state fields
  `is_locked`/`level_required` are read by this screen's dataresponders; whatever system sets
  them is out of this file's scope).
- **Two-level menu, both levels hosted inside the single `store_common` `Vdo_mega_list` widget**
  (push/pop sub-menu within one list, not separate `push_screen`s) — a third internal-structure
  pattern distinct from both `cell_saintsbook`'s filmstrip and `cell_phone`'s toggle-visibility
  pages. Category list via `vint_dataresponder_request("cell_rewards_category_dr",
  "cell_rewards_add_category", 0)` (`:89`); selecting a category issues a second dataresponder,
  `vint_dataresponder_request("cell_rewards_reward_dr", "cell_rewards_add_reward", 0,
  Cell_rewards_category_id)` (`:418`), whose callback carries a `trumps` field (CRC name of a
  prerequisite reward — a dependency chain between rewards).
- **Selection/purchase chain — fully traced, no native calls, same "post and let C handle it"
  shape as `cell_missions`'s `"Choose"`.** `cell_rewards_purchase_select` (`:303`): no-op if
  locked/already-purchased; shows an insufficient-funds popup if the price exceeds the player's
  cash; otherwise shows a yes/no confirm popup. On confirm (`cell_rewards_purchase_final`,
  `:358`), it posts `vint_dataresponder_post("cell_rewards_reward_dr", "Purchase",
  Menu_data[current_index].id)` (`:381`) — **no native is called directly; cash deduction and the
  unlock grant both happen entirely on the C side of that dataresponder post.**
- **`cell_rewards_button_a` is referenced but never defined in this file — already resolved
  elsewhere, cross-referencing only.** Independently RESOLVED in `spec-lua-api-behaviour.md` §61:
  every sibling screen defines its own local `*_button_a` mouse-click-to-gamepad-press shim;
  `cell_rewards` is the one sibling that never did — a real missing-function script bug, not a
  hidden native.
- **Exit convention — matches §4 exactly, with one OPEN side-note.** `cell_rewards_exit_to_main`
  (`:504-506`) = 1-deep pop, used when `Rewards_from_menu` is true (normal icon-grid entry).
  `cell_rewards_exit_to_game` (`:508-512`) = the uniform 3-deep pop, used when
  `Rewards_from_menu` is false. A source comment (`:134`) implies some other screen can
  `push_screen("cell_rewards")` directly with `Rewards_from_menu` already false, bypassing the
  icon grid entirely — **OPEN**, not traced: what calls into `cell_rewards` that way is a concrete
  next lead.

**Net result from this pass:** `cell_saintsbook` produced a new native-lead pair
(`sb_select_hitman_target`/`sb_select_chop_shop_vehicle`); `store_crib_cash` and `cell_rewards`
both went entirely through already-documented natives or the generic dataresponder
request/post framework with no native of their own. `store_crib_cash`'s exit convention is the
first that doesn't fit the established 1-deep/3-deep(/4-deep) pattern at all (always 1-deep,
regardless of close trigger). Remaining untraced siblings: `cell_music_menu`/`_sub`/`_playlist`,
`cell_camera`, `cell_extras`'s own top-level page.

## 13. The `cell_extras` document's own top-level page — CONFIRMED, no additional icons beyond
Cheats/Stats

Reached via `cell_menu_handle_state(ID_EXTRAS)` → `push_screen("cell_extras")` (per §3). Full read
of `cell_extras.lua` (184 lines).

- **It is a minimal 2-entry icon grid, not a general "extras" hub.** `ID_CHEATS=0`/`ID_STATS=1`
  (`cell_extras.lua:1-2`) are the only two entries in the module-level `Data` table (`:4-14`) —
  there is no third icon, no hidden/locked slot, and no mission-completion gate anywhere in this
  file (contrast `cell_menu_main`'s own `Data[ID].is_enabled` gating, §3): Cheats and Stats are
  unconditionally both present once Extras itself is reached.
- **Init/data population.** `cell_extras_init()` (`:22`) resolves the document, builds a
  `Vdo_cell_menu:new("cell_menu")` populated directly from the static `Data` table (`:37-38`) —
  no dataresponder call at all, since the 2 entries are hard-coded, not server/C-populated.
- **Dispatch.** `cell_extras_button_a` → `cell_extras_handle_state(current_id)` (`:107-113`),
  structurally identical to `cell_menu_main`'s own dispatcher (§3): `ID_STATS` →
  `push_screen("cell_stats")`; `ID_CHEATS` → `push_screen("cell_cheats")` (`:142-155`) — this is
  the exact call site §6/§9 already cite as the parent of both children.
- **Exit convention — matches §4 exactly, no deviation.** `cell_extras_button_b` → a 1-deep
  `pop_screen()` back to the icon grid; `cell_extras_button_map` → the uniform 3-deep pop. No
  re-entry-memory `cell_menu_state_dr` post is issued anywhere in this file, unlike several
  siblings (`cell_missions`, `cell_saintsbook`) — Extras always re-enters fresh at the icon grid
  rather than remembering itself as the last-open page.
- **No new native leads** — no native call appears anywhere in this file outside the generic UI
  framework.

## 14. `cell_music_menu` / `cell_music_sub` / `cell_playlist` — the music chain, CONFIRMED, two
exit-convention wrinkles (an extra nesting level and a "back stays on the chooser" shape)

Reached via `cell_menu_handle_state(ID_MUSIC)` → `push_screen("cell_music_sub")` (per §3 — the
icon grid pushes `cell_music_sub` directly, not `cell_music_menu`; `cell_music_sub` is the actual
landing page, fanning out to either `cell_music_menu` (radio) or `cell_playlist` (mixtape)). Full
read of all three files (`cell_music_sub.lua` 180 lines, `cell_music_menu.lua` 210 lines,
`cell_playlist.lua` 1119 lines).

- **`cell_music_sub` — a 2-entry icon-grid chooser, structurally identical in shape to
  `cell_extras` (§13).** `ID_RADIO=0`/`ID_MIXTAPE=1` (`cell_music_sub.lua:1-14`), same
  static-`Data`-table pattern, no dataresponder, no gating. `cell_music_sub_handle_state(state)`
  (`:141-152`): `ID_RADIO` → `push_screen("cell_music_menu")`; `ID_MIXTAPE` →
  `push_screen("cell_playlist")` — the actual category-chooser; `cell_music_menu` is specifically
  the radio-station sub-page, not a generic category page.
- **`cell_music_menu` — the radio station grid.** `cell_music_menu_init()` (`:12`) builds a
  4-column/3-row icon grid, populated via
  `vint_dataresponder_request("cell_music_menu_populate", "cell_music_menu_add_station", 0)`
  (`:38`). Selecting a station (`:97-110`) calls `cell_music_menu_toggle_station(Grid_data[idx].id)`
  (`:101`) — the already-documented binding (`spec-lua-api-behaviour.md` §59.2: toggles a
  station's hidden/disabled bit; re-enabling the currently-tuned station retunes the radio) — a
  **toggle-availability control, not a "tune to this station now" action.**
- **`cell_playlist` — the mixtape/track editor, by far the most complex of the three, a dual-list
  (left/right) widget pair, not a simple list.** `cell_playlist_init()` (`:126`) builds two
  `Vdo_mega_list`s and issues two dataresponder requests against the same channel with different
  enum args (station list / song list). The left list drills into a selected station's available
  tracks; the right list is the player's actual playlist, with an in-place reorder mode and item
  removal. Highlighting any track auto-previews it via a 0.5s-delayed `playlist_play_track`
  (already documented, with a real null-read crash on out-of-range/negative index and a 16-bit
  index-truncation aliasing bug); changing selection away from a track calls `playlist_stop_track`
  (also already documented). `cell_playlist_save()` (`:924`) calls `cell_playlist_save_list`
  (already documented, two real memory-safety shapes) on every exit path, so leaving the playlist
  by any route always persists the current order first.
- **Chain/exit convention — confirms the "category then station/playlist" structure, with two
  wrinkles beyond the §4 baseline.** (1) Backing out of `cell_music_menu` or `cell_playlist` does
  NOT pop all the way to the icon grid, nor even re-push `cell_music_sub` — `cell_music_sub` is
  never popped off the stack by the fan-out, so "back" from either child is just a 1-deep pop that
  reveals the already-live `cell_music_sub` screen still resident underneath. (2) The dedicated
  map hotkey from any of the three instead exits the entire phone subtree, and because
  `cell_music_menu`/`cell_playlist` both sit one `push_screen` deeper than the 3-deep baseline
  (under the `cell_music_sub` chooser layer), their own exit-to-game pops are correspondingly
  deeper: `cell_music_menu`'s is 4-deep ("music"/"music sub"/"main menu"/"cellphone frame"), and
  `cell_playlist`'s is **also 4-deep** ("Playlist Editor"/"music Sub screen"/"main"/"cell frame")
  — the same extra-nesting pattern already seen for `cell_stats` under Extras (§9). All three
  documents post the `cell_menu_state_dr` re-entry-memory idiom before any full-exit-to-game pop.
- **No new native leads.** Every music-chain-specific native
  (`cell_music_menu_toggle_station`/`playlist_play_track`/`playlist_stop_track`/
  `cell_playlist_save_list`) is already documented in `spec-lua-api-behaviour.md`.

## 15. `cell_camera` — CONFIRMED: a single camera-enable toggle screen, not a live
photo/pose/filter screen

Reached via `cell_menu_handle_state(ID_CAMERA)` → `push_screen("cell_camera")` (per §3). Full read
of `cell_camera.lua` (222 lines).

- **What it actually is — REFUTES a "zoom/filters/pose" hypothesis.** This screen is a single-row
  options list with exactly one entry: a binary enable/disable toggle (`cell_camera.lua:5-7`).
  There is no zoom control, no filter list, no pose selector, and no "take photo" action anywhere
  in this file — flipping this one toggle is the entire interactive surface. A dedicated d-pad-down
  hint on this page (via the already-documented `game_get_key_name_for_action`,
  `spec-lua-api-behaviour.md` §8.23) shows which physical button takes a screenshot, but that
  action is NOT triggered from this Lua file — it's a display-only hint for a binding that lives
  elsewhere, outside this screen's own input tracker.
- **Init/data population.** `cell_camera_init()` (`:13`) calls `cell_camera_is_enabled()`
  (`:18`) — the already-documented query (§59.1: reads a Lua-side-only global, zero-filled at
  start, so "never set" and "explicitly disabled" are indistinguishable) — to set the toggle's
  initial state. No dataresponder call anywhere in this file.
- **Button→native chain.** `cell_camera_nav_left_right` (`:122-138`) calls
  `cell_camera_enable(true)`/`cell_camera_enable(false)` depending on the toggle's selected state
  — the already-documented binding (§59.1: writes a global consumed only by its own getter, no
  engine-side consumer found anywhere else in the binary — confirmed Lua-side-only state).
- **Exit convention — matches §4 exactly, same shape as `cell_extras`.** `cell_camera` sits
  directly under `cell_menu_main`, same nesting depth as `cell_phone`/`cell_missions`, so the
  3-deep baseline applies unmodified (no extra-nesting wrinkle like `cell_stats`/`cell_playlist`).
- **No new native leads.** `cell_camera_is_enabled`, `cell_camera_enable`, and
  `game_get_key_name_for_action` are all already fully documented.

**Net result from this pass — closes out the full phone-menu document survey.** None of the
three remaining siblings produced a new native lead — unlike `cell_cheats` (§6) and
`cell_saintsbook` (§10), every native touched here was already in `spec-lua-api-behaviour.md`.
Two exit-convention wrinkles fold into the established pattern rather than breaking it:
`cell_music_menu`/`cell_playlist` both exit-to-game one level deeper (4-deep) for the same
structural reason as `cell_stats` (an intermediate chooser screen counts as a stack layer); and
the music chain's own "back" button never pops all the way to the icon grid, since the chooser
screen (`cell_music_sub`) stays resident underneath its own children rather than being popped by
the fan-out. `cell_extras`'s own top-level page is a minimal, ungated 2-entry icon grid with a
completely standard §4-shaped exit.

## 16a. Contacts screen and homie/contact calls — port recipe (2026-10-12, FINAL)

Sources: §5 (Lua side), `spec-tables-traffic-ai.md` §15 (`homies.xtbl` record), tranche 13 E.1 / tranche 19
H.1 notes, plus a fresh read-only decompile of the C side (helper scripts `tools/scratchpad/CellContacts1..3.java`).

**Key correction.** The phone's "contacts" (book) page is **not** fed by `contacts_sr3.xtbl`
(`spec-tables-vehicle-world.md` §17). It is fed by the **homie record table** loaded from `homies.xtbl`
(+ DLC `<framework>_homies.xtbl`): base array `0x022AD780` (count `0x022AD758`), DLC array `0x022B0180`
(count `0x022AD75C`), stride `0x180`. `contacts_sr3.xtbl` (Name/Image/Persona) is a separate roster with no reader
on this path (its consumer stays OPEN; most plausibly mission-giver contact art/voice). **[CONFIRMED.]**

### 16a.1 The phone-book array (C side)

Built by `0x0083d3c0`; array `0x023156A8`, entry stride `0x18`, count `0x02315690` (no capacity check seen — OPEN).

| Entry off | Meaning | Source |
|---|---|---|
| `+0x00` | name text key | record `+0x28` (`Display_Name` key) |
| `+0x04` | description key | record `+0x2C` (`Display_Desc` key) |
| `+0x08` | byte "listed" | 1 for every entry written |
| `+0x09` | byte "callable" | see status table |
| `+0x0A` | byte "extra" | `0x007e7ba0` (1 only for status 5/6 with record byte `+0x178` = 1); not passed to Lua |
| `+0x10` | image CRC | record `+0x30` (`Image_name`) |
| `+0x14` | homie index | combined record index (`0x007e5ae0`), or **−1** = special "hitman target" entry |

**Ordering:** (1) if a hitman-target contact is active (`0x006b2030` non-zero), entry 0 is it (name/desc taken from the
object `0x006a7f10` returns, `+0x14` = −1, listed + callable); (2) then every homie record in **table order** — base
rows in `homies.xtbl` file order, then DLC rows — via the iterator `0x007e5b80`/`0x007e5bb0`. No sorting.

**Inclusion gate per record:** the DLC gate byte `+0x24` must pass (`0x0045b3a0`); record status `+0xBC` ≠ 7; and if
status = 0, the record's name-hash unlock test `0x0071dfd0(+0x20)` must be true. Callable byte by status:

| `+0xBC` status | listed | callable (`+0x09`) |
|---|---|---|
| 0, 1, 4 | yes | 1 unless the homie's character is already spawned and alive near the player (`0x007e7b30` non-zero → 0) |
| 2, 3, 5, 6 | yes | 0 |
| 8 | yes | 1 |
| 7 | no | — |

(Status-value names are OPEN; 1 and 8 behave as "unlocked", 0 as "unlocked only when the unlock flag is set".)

**Populate responder:** registration `0x0083df80` binds data-responder `"cell_phone_pad_populate"` to handler
`0x0083dba0`. With list id 0 (`CELL_HOMIE_LIST`) it calls the Lua callback once per entry whose `+0x08` = 1 with
`(entry_index, name_key, desc_key, image_crc, callable_bool)` — the exact signature of
`cell_phone_populate_list(homie_index, homie_name_crc, description_crc, image_crc, is_available)`. Note the Lua
`homie_index` is the **phone-book entry index**, not the record index. With list id 1 it walks the garage vehicle
list `0x014A1D00` (§5 garage page), storing each row pointer in `0x02315428[n]` (count `0x02315698`) and passing
`(n, display_name_string, value at row +0x74)`. **[CONFIRMED.]**

### 16a.2 Call dispatch — `cellphone_dial` (`0x0083d930`)

State word `0x02315688`: 0 idle, 1 connected, 2 busy, 3 "record status 5" special, 4 (set elsewhere, OPEN).
1. Copy the two callback names (32 bytes each) to `0x023153F8` / `0x023153D8`; current entry `0x0231568C` = entry[idx]
   (no bound check — a port must bound-check).
2. Entry `+0x14` = −1 → hitman-target call → **connect**.
3. Record = homie table[`+0x14`]. If record status = 5 → state 3, play Cell Phone event `0x59B17C4E`, Lua callback(−1).
4. Else connect only if **all** of: `0x007e74f0(record)` (available: DLC gate ok; not blocked by mission — see 16a.3;
   override `+0xC0`: 2 → yes, 1 → no, 0 → status 0 with unlock flag, or status 1 / 8); entry `+0x09` set; context gate
   `0x0083d800(record)` (16a.3).
5. Otherwise **busy**: state 2, play "UI_Cell_Line_Busy" (switch `0xD46667BB`) on the "busy signal" object, callback(−1).
6. **Connect**: ring timer `0x0231541C` = 1.0, play Cell Phone event `0x604161D4` (id in `0x02315424`), state 1,
   `0x02315420` = 1. The connect callback to Lua (`CELL_CALL_CONNECTED`/`CELL_CALL_GARAGE_SHOW`) is issued later from
   the update neighbourhood `0x0083d150`–`0x0083d3b0` (outside a defined function; exact timing OPEN).

**Hang-up / completion** (`0x0083d090`, the `cellphone_end_call` neighbourhood): stops the ring sound; if state is 1
or 4: hitman entry → `0x006aac60(0)`; else, if the record's follower-cap check `0x0083d040` passes **and** the record
lacks flag `player selects vehicle` (`+0xF1` bit `0x10`) → **summon** `0x007e2c70(index)`. State → 0.

[2026-10-20 Team B CHOSEN Q: FINAL (CONFIRMED by Team D emulation) — **ring, connect and summon timing.** The phone update `0x0083d150` (update entry of the "cell_phone" module, descriptor at `0x011614e0`) runs while the module is loaded and the call handle `0x02315424` is non-zero, and switches on the state word through the table at `0x0083d39c`. Every timer step subtracts a fixed **1/30 s** (`0x0132a0ac`) per update, not the frame time. State 1: the dial set the ring count `0x02315420` = 1 and the timer 1.0. When the timer drops below 0, a second ring plays (event `0x604161D4`), the timer is set back to 1.0 and the count drops to 0. On the next update with count 0 the call **connects**: a hitman entry plays its line and goes to state 4; a homie row plays its answer line through `0x007e6320` (the row's `Audio` event, with a switch chosen by the follower-cap result), fires the first Lua callback (`0x0083cfe0`, argument 1 for a garage-pick row, else 0) and goes to state 4. State 4, on the next update: hitman -> `0x006aac60(0)`; a homie row that passes the cap check `0x0083d040`: garage-pick rows wait for `cellphone_choose_vehicle`, `emergency` rows -> `0x007e2bd0`, `taxi` rows -> `0x007e2c20`, all others **summon** `0x007e2c70(index)`; then state 5 with a 3.0 s timer (`0x01116444`). States 2 and 3 (busy, downed) go to state 5 the same way. State 5: when the 3.0 s run out, the second Lua callback (`0x0083cf80`, end of call) fires and the state returns to 0. So the summon comes about 31 updates (about 1.03 s at 30 updates per second) after the dial plus two updates, and the phone closes 3.0 s later. There is no 2000 ms talk delay. [2026-10-20 per Team D emulation: FINAL] Emulated from state 1, ring count 1, timer 1.0, non-zero handle, homie id 3, dt = 0.033333335 (the initial `.data` value of `0x0132a0ac`; whether it is overwritten at run time was not checked, and the module update rate was not traced, so if it runs once per frame the timers are frame-locked). Because dt is slightly above 1/30 the timer goes strictly below 0 on update 30: update 30 (t 1.000 s) rings (`0x0083cf30`), ring count 0, timer 1.0; update 31 (1.033 s) homie lookup `0x007e5b40`, start conversation `0x007e6320`, connect `0x0083cfe0`, state 4; update 32 (1.067 s) summon, state 5, close timer 3.0 (`0x01116444`); update 123 (4.100 s) close `0x0083cf80`, state 0, which is 91 updates (3.033 s) after the summon because the timer must go strictly below 0. Exactly one connect, one summon and one state 0. Summon flag variants: record flags `+0xF1` = 0 -> `0x007e2c70`; `+0xF1` bit 7 -> `0x007e2bd0`; `+0xF2` bit 0 -> `0x007e2c20`; `+0xF1` bit 4 -> no summon. When the call record's homie id (`+0x14`) is -1, the connect is the call-by-name routine `0x0070dd50`, and state 4 then calls `0x006aac60` instead of a summon.]

**Garage-pick homie** (record flag `player selects vehicle`, `+0xF1` bit `0x10`): the call pivots to the garage page;
`cellphone_choose_vehicle` (`0x0083d5e0`) validates the chosen garage row (vehicle slot `+0x0E`, DLC gate `+0x1C`),
stores it as the delivery vehicle (`0x007e5c70` → `0x022AD760`), summons `0x007e2c70(index)`, then — unless the
`"Vehicle_delivery"` tutorial/flag is already set (`0x0071d180`) — fires the delivery hint (`0x0094a800`).

**Summon** `0x007e2c70(index)`: if `0x022AD6CC` = 0 (no homie call already in flight), run `0x007e30b0` (clear/prepare)
then `0x007e3ba0(index)` (spawn-and-drive-up). The spawn itself is driven by the record: `Humans` (up to 8
characters), the weighted `Vehicles` pick (2 entries, weight sum `+0x6C`), and `Flags` byte `+0xF1`/`+0xF2` effects
(`clear gang notoriety` 0x01, `clear police notoriety` 0x02, `form own squad` 0x04, `give random weapons` 0x08,
`repair players vehicle` 0x20, `turn to ped after driveup` 0x40, `emergency` 0x80; `taxi` / `requires saints`).
**The inner bodies of `0x007e30b0`/`0x007e3ba0` (arrival logic, despawn) are OPEN.**

### 16a.3 Limits

- **Follower cap** (`0x0083d040`): skipped entirely for service calls (record `+0xF1` & `0x43`: either notoriety-clear
  flag or `turn to ped after driveup`). Otherwise compare the player's max-followers `0x0097e780(player)` with current
  followers `0x0097e260(player)`; if full, **dismiss one** (`0x009db1b0(player, 0)`) and re-check; still full → no summon.
  The numeric max is OPEN (Lua `party_allow_max_followers` `0x00a59380` toggles the per-player flag at `+0x28AC` bit 0;
  `party_add_ignore_limits` bypasses it).
- **Mission disable:** `0x007e6240(record)` → blocked when any of six named missions/activities is active
  (three string constants at `0x011169A4`/`0x0111F720`/`0x0111F724`, plus `dlc3_m01`..`dlc3_m03`), or when any mission in
  the record's `Blocked_for_Mission` list is active-and-not-complete. A blocked record answers **busy**.
- **Context gate** (`0x0083d800`): requires `0x007e2d60()` (no global phone-lock `0x022AD2D8` and no call in flight
  `0x022AD6CC`); notoriety-clear records (`+0xF1` & 3) pass immediately; others apply a distance box against the
  constant `0x013B8770` and, for garage-pick homies, a vehicle-availability check (`0x00bb88f0(5)+(6)`, `0x005f9070`).
  The return-value polarity of each branch is not recoverable from the decompile — **OPEN**.
- **Cooldown:** no timer was found on the dial path; the only "in-flight" lock is `0x022AD6CC`. Any re-call cooldown is
  OPEN (likely inside `0x007e3ba0`).
- **Cash cost:** none on the homie path. Only the garage page charges (repair cost, Lua-side dialog, §5).
- **Script overrides:** `homie_mission_unlock` (`0x00a4cf70`) / `homie_mission_lock` (`0x00a4cf40`) write record
  `+0xC0` = 2 / 1.

### 16a.4 Lua natives

| Native | Address | Role |
|---|---|---|
| `cellphone_dial` | `0x0083d930` | 16a.2 |
| `cellphone_end_call` | `0x0083d130` | hang-up wrapper (body near `0x0083d090`) |
| `cellphone_choose_vehicle` | `0x0083d5e0` | garage-pick summon |
| `homie_mission_unlock` / `_lock` | `0x00a4cf70` / `0x00a4cf40` | availability override |
| `party_allow_max_followers` | `0x00a59380` | follower-cap flag |
| `party_dismiss_all` | `0x00a593f0` | clears followers |
| `party_set_dismissable` / `_recruitable` | `0x00a59430` / `0x00a59460` | party globals |
| data responder `cell_phone_pad_populate` | reg `0x0083df80`, handler `0x0083dba0` | 16a.1 |

### 16a.5 Port recipe

1. Load `homies.xtbl` (+ DLC) into records per `spec-tables-traffic-ai.md` §15; keep `status` (+0xBC) and override (+0xC0, default 0).
2. On phone open, rebuild the phone book (16a.1): optional hitman entry first, then records in table order, applying the inclusion and callable rules.
3. Serve `cell_phone_pad_populate` list 0 with `(entry_idx, name_key, desc_key, image_crc, callable)` for each listed entry.
4. On Lua `cellphone_dial(entry_idx, cb1, cb2)`: bound-check `entry_idx`; run steps 2–6 of 16a.2; report busy via `cb1(-1)`.
5. On connect + hang-up: if `player selects vehicle`, go to garage page and summon from `cellphone_choose_vehicle`; else check follower cap (16a.3, dismissing one if full) and summon.
6. Summon: refuse while a call is in flight; spawn the record's humans in a weighted-pick vehicle, drive up to the player, apply the `Flags` effects, then join the party (service-type rows: perform effect and leave).
7. No cash charge; mission blocks answer busy.

OPEN: status-value names; arrival/despawn bodies (`0x007e30b0`, `0x007e3ba0`); max-follower number; any cooldown; `0x0083d800` branch polarity; connect-callback timing; `contacts_sr3.xtbl` consumer.

## 16. Open items

1. The literal native binding(s) that push `cell_foreground`/`cell_background`/`cell_menu_main`
   onto the screen stack (§2) — same unresolved shape as the pause menu's own open binding was
   (`spec-frontend-menu-runtime.md` §7/§9, where it has since been LANDED). **Still OPEN for the
   phone specifically, after both the screen-push side (all ~35 `0x007b4750` call sites, 2026-10-10)
   AND the input side (the dedicated `CBA_GAC_CELL_OPEN` action exists, `0x57`, but none of 156
   input-query call sites reads it, 2026-10-10) were exhausted — see §2.** Next step: a 4th
   undiscovered query primitive, or a raw physical-button bypass of the CBA layer.
2. ~~`cell_cheats_activate_cheat`/`sb_select_hitman_target`/`sb_select_chop_shop_vehicle`'s own
   identities~~ **RESOLVED 2026-10-10 — all three are directly Lua-bound natives, fully
   characterized, no crash shape found in any of them.** Full detail:
   `spec-lua-api-behaviour.md` §45.8 (cross-referenced at §6/§10). One residual: whether
   `cell_cheats_activate_cheat`'s own flag-byte table is the same structure as the cheats.xtbl
   record array documented in the progression-tables spec — not confirmed.
3. ~~The 8 sibling documents beyond `cell_phone` and `cell_cheats`~~ **RESOLVED 2026-10-10 — the
   full phone-menu document survey is now closed.** All remaining siblings are traced to this
   depth: `cell_missions` (§7), `pause_map` (§8), `cell_stats` (§9), `cell_saintsbook` (§10),
   `store_crib_cash` (§11), `cell_rewards` (§12), `cell_extras`'s own top-level page (§13), the
   music chain `cell_music_menu`/`_sub`/`cell_playlist` (§14), and `cell_camera` (§15). No
   documents remain untraced.
4. **`cell_rewards`'s own `Rewards_from_menu`-bypass caller (§12)** — a source comment implies
   some other screen can push `cell_rewards` directly (not via the icon grid), which would make
   its 1-deep "back" path exit somewhere other than the icon grid; not traced.

## 17. Clean-room compliance note

Plain hex addresses and real Lua identifiers (variable/function names read directly from the
shipped, extracted script source) throughout. No Ghidra decompiler auto-names
(`FUN_`/`DAT_`-style), no pseudocode, no leaked Volition/Havok/Audiokinetic/RAD proprietary
material.

## Changelog

- **2026-10-10 — §6/§10/§16: all 3 accumulated native leads resolved, no crash shapes** (SPEC
  TEAM, agent `a6d64fe32294cd9e6`). `cell_cheats_activate_cheat`, `sb_select_hitman_target`, and
  `sb_select_chop_shop_vehicle` are all directly Lua-bound natives, now fully documented in
  `spec-lua-api-behaviour.md` §45.8. All three are fully bounds-checked — a clean negative on the
  crash-shape front, in direct contrast to `cellphone_dial`'s own unbounded index/unchecked
  dereference (§45.4). The chop-shop native encodes a genuine new gameplay rule (AI-partner-claim
  rejection) the hitman native lacks. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-10 — new §13/§14/§15: `cell_extras`, the music chain, `cell_camera` traced — CLOSES
  THE FULL PHONE-MENU DOCUMENT SURVEY** (SPEC TEAM, agent `a8de47913e44dd0d9`). `cell_extras`'s
  own page is a minimal, ungated 2-entry icon grid (Cheats/Stats only). The music chain
  (`cell_music_sub` chooser → `cell_music_menu` radio grid / `cell_playlist` dual-list mixtape
  editor) revealed two exit-convention wrinkles: an extra 4-deep exit-to-game (same structural
  cause as `cell_stats`'s), and a "back never pops the chooser" shape unique to this chain.
  `cell_camera` REFUTES a "zoom/filters/pose" hypothesis — it's a single enable/disable toggle,
  nothing more. None of the three produced a new native lead; every native touched was already
  documented. All 9 phone sub-documents beyond `cell_phone`/`cell_cheats` are now traced to the
  same depth. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new §10/§11/§12: `cell_saintsbook`, `store_crib_cash`, `cell_rewards` traced to
  the same depth as the prior worked examples** (SPEC TEAM, agent `a365e7ed46821da60`).
  `cell_saintsbook` is a 3-page horizontal filmstrip (top/main/sub) and produced a new native-lead
  pair (`sb_select_hitman_target`/`sb_select_chop_shop_vehicle`, not in
  `spec-lua-api-behaviour.md`). `store_crib_cash` confirms the "cash transfer" hypothesis exactly
  (auto-fires on open via the already-documented `crib_get_cash_stash`) and is the first document
  whose exit convention doesn't fit the established 1-deep/3-deep(/4-deep) pattern at all — always
  1-deep regardless of trigger. `cell_rewards` REFUTES the "passive viewer" hypothesis — it's a
  real cash-purchase store for permanent unlocks built on the shared `store_common.lua` framework,
  with a new OPEN lead on an alternate non-icon-grid entry path. Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new §7/§8/§9: `cell_missions`, `pause_map`, `cell_stats` traced to the same depth
  as `cell_phone`/`cell_cheats`** (SPEC TEAM, agent `a38d75135a77afe04`). `cell_missions` and
  `cell_stats` go entirely through the generic `vint_dataresponder_request`/`_post` framework with
  no dedicated native of their own (no new lead, unlike `cell_cheats_activate_cheat`); `pause_map`
  uses a structurally different continuous-push data pattern (4 dataitem/datagroup subscriptions,
  not a one-shot request) and all of its natives were already fully specced in
  `spec-lua-api-behaviour.md` §31. `cell_stats` exits to game at 4-deep (not the 3-deep baseline),
  consistent with sitting one `push_screen` deeper under Extras. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §2/§7: button-map route tried (pause menu's own binding landed by this method),
  but phone's own open binding still not found — second clean exhaustive negative** (SPEC TEAM,
  agent `adacb57c01da892c6`). The pause menu's `CBA_GAC_PAUSE_MENU` action resolved cleanly to a
  traced query→dialog chain (`spec-frontend-menu-runtime.md` §7). The phone's own dedicated action,
  `CBA_GAC_CELL_OPEN` (`0x57`), exists in the real CBA table but is never read by any of the 156
  call sites across all 3 known input-query primitives — the phone's top-level open is confirmed
  unreachable through both the screen-push side (prior pass) and the semantic input-action layer
  (this pass). Remaining leads: a 4th undiscovered query primitive, or a raw physical-button
  bypass. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

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
