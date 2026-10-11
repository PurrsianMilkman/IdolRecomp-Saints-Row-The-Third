# Shared walk-in-store purchase framework (`store_common`)

**Scope.** `store_common.lua` — the shared library/base VINT document every walk-in free-roam
shop (weapon, vehicle customization, clothing, gang customization, tattoo parlor, image
fusion/stronghold, crib, DLC, gallery, demo) builds on. This file documents the shared framework
only: generic list/menu navigation, the breadcrumb header, the yes/no purchase popup, and the
cash/respect data-item plumbing. The per-type stores' own item lists, 3D-preview cameras, and
individual purchase natives (`store_weapon_purchase_weapon`, `vcust_purchase_component`,
`pcr_purchase_tattoo`, `pcu_purchase_slot`/`pcu_purchase_outfit`, etc.) are already individually
documented in `spec-lua-api-behaviour.md`'s numbered tranches — this file connects those natives
to the real UI call chain that reaches them, rather than re-deriving the natives themselves.
Per-type stores (weapon/clothing/vehicle/gang/tattoo/etc.) beyond this shared foundation are
future work.

**Confidence key:** CONFIRMED (read directly from the real extracted Lua source,
`scratchpad/ui_lua_dumps/`); HIGH CONFIDENCE (follows from structure but not independently
exercised); OPEN (flagged honestly, with a concrete next step where one exists).

## 1. Entry/init — `store_common` is a library module layered on its OWN pushed VINT document,
not a document per-type stores extend

`store_common_init()` (`store_common.lua:70-154`) resolves a real VINT document named
`"store_common"` (`Store_common_doc_handle = vint_document_find("store_common")`, `:75`) and
builds every shared widget against it: the hint bar/rotate-hint (`Vdo_hint_bar`), the purchase
popup (`Vdo_store_popup`), the color grid (`Vdo_grid_list`), a spinner, the shared list
(`Active_list`, a `Vdo_mega_list` named `"list_1"`), and the breadcrumb/cash/respect header
(`Store_header`, a `Vdo_store_header`). It also wires the generic input tracker
(`pause`/`map`/`select`/`back`/4 nav directions, `:124-133`) and, on PC, a second mouse-drag input
tracker for rotating the 3D preview (`Character_mouse_input_tracker`, `:138-151`).

**No per-type store file ever calls `store_common_init()` itself** (confirmed by grep across all
~400 extracted files — the only two hits are the function definition in `store_common.lua:70` and
an identically-named duplicate embedded in `cell_rewards`'s own copy of the file,
`cell_rewards__store_common.lua:70`). Each per-type store's own `_init()` function (e.g.
`store_weapon_init`, `store_weapon.lua:113`) instead **assumes `store_common`'s globals already
exist** and configures them directly: it reads/writes `Input_tracker`, `Mouse_input_tracker`,
`Store_common_hint_bar`, `Store_common_popup`, `Store_common_color_grid`, and crucially sets the
3 configuration hooks `store_common.lua:31-33` declares as globals —
`Store_common_populate_list_cb` (the list-population callback), `Store_common_exit_cb` (what
"back out of the top-level menu" does), and (implicitly, by filling `Menu_data`) the item data
itself. `store_weapon_init` sets `Store_common_populate_list_cb = store_weapon_populate_list` and
`Store_common_exit_cb = store_weapon_exit` (`store_weapon.lua:142-143`). This is exactly the same
shape as `cell_rewards` building on `store_common` for its own "cell phone cash store" screen
(`spec-cellphone-menu-runtime.md` §12) — confirming `store_common` is a genuinely shared
library/document pair, not a one-off.

**The lifecycle mechanism (who actually calls `store_common_init()`) is the SAME unresolved
native-only shape the phone menu's own top-level open has (`spec-cellphone-menu-runtime.md` §2).**
No extracted Lua file ever calls `push_screen("store_common")` or any `push_screen("store_weapon"
|"store_vehicle"|"store_clothing"|"store_tattoo"|"store_gang"|"store_stronghold")` either (grep
across all `store_*`/`vdo_store_*` dumps found zero hits for any of these — the only
`push_screen("store_*")` call sites anywhere are `cell_menu_main.lua:314`'s own
`push_screen("store_crib_cash")` and `main_menu_top.lua:509`'s `push_screen("store_dlc")`, neither
of which is a walk-in free-roam shop). Every VINT document in this engine appears to get its
`<docname>_init()` called automatically by the engine when that document is pushed (the same
convention `cell_phone_init`/`cell_missions_init` etc. rely on) — so `store_common` and the
specific per-type document are both pushed, and both auto-init, from a native/world-trigger call
site this pass did not trace. **OPEN, same shape as the already-OPEN phone-menu top-level open** —
worth a future pass specifically hunting the world-space "walk into shop" trigger → `push_screen`
call chain, likely NOT through the same CBA input-action table the phone-open search already
exhausted (a shop trigger is a world-collision event, not a button press).

**What gets configured per store type.** Currency/display is generic in the shared header
(`Vdo_store_header:set_price`/`set_respect`/`set_level`/`enable_price_respect`,
`vdo_store_header.lua:166-255`) — every store can show a cash price, a respect reward, OR a
"level required" gate (the respect display slot is literally reused for the level-requirement
text, `:244-255`), toggled per item/per store. `store_common_init()` itself subscribes to the
player's live cash/respect data items unconditionally
(`vint_dataitem_add_subscription("sr2_local_player_respect", ...)`/
`"sr2_local_player_status_infrequent"`, `store_common.lua:121-122`) — **every walk-in store always
has both a live cash readout and a live respect-meter available**, regardless of whether that
particular store type actually spends respect as a currency (none of the traced store types do —
see §4). Branding/title is via `Store_header:push_title()`/`pop_title()`, a breadcrumb stack
(`vdo_store_header.lua:217-229`), pushed by `store_common_push_menu`/`store_common_button_a` on
every menu-depth change (`store_common.lua:383`, `:492`).

## 2. Item list population — the same `vint_dataresponder_request` one-shot pattern as the phone
menu, issued per-store, not by `store_common` itself

`store_common.lua` owns the shared `Vdo_mega_list` instance (`Active_list`) and the generic
rendering/navigation around it (`store_common_nav_up_down`, `store_common_nav_left_right`,
`store_common_mouse_click`, etc., `:604-820`), but **issues no dataresponder request of its own** —
list population is entirely delegated to `Store_common_populate_list_cb`, a per-store-type
callback set at that store's own `_init()`. Each per-type store issues its own named
dataresponder channel, exactly the `cell_phone`/`cell_missions` one-shot-request shape documented
in `spec-cellphone-menu-runtime.md` §5/§7: e.g.
`vint_dataresponder_request("store_weapon_populate_upgrades", "store_weapon_upgrade_add_items", 0,
Weapon_grid_data.category, Store_weapon_current_info.id)` (`store_weapon.lua:1782`), whose
callback (`store_weapon_upgrade_add_items`, `:1589`) appends one row per call into a local
menu-data table that is then handed to `Store_common_populate_list_cb` → ultimately
`Active_list:draw_items(...)`. Nested menu depth (category → sub-category → item) is tracked by
`store_common`'s own 3 parallel stacks (`Store_common_stack_id`/`_index`/`_menu`, `:66-308`)
rather than separate `push_screen` calls — the same "push/pop sub-menu within one list" pattern
`spec-cellphone-menu-runtime.md` §12 already identified for `cell_rewards`'s own 2-level
category/reward menu (unsurprising, since `cell_rewards` is itself built on this exact
framework).

## 3. Selection/preview — price/respect display is automatic per-row; 3D preview cameras are
per-store, cross-referenced not re-derived

Highlighting a row calls that row's own `on_nav` callback if present, else the menu table's
`default_on_nav` (`store_common_nav_up_down`, `:619-629`) — this is where each store type updates
its own price/stat/preview display (not generic to `store_common` itself). Pressing select
(`store_common_button_a`, `:409-521`) branches on the row's `type` field
(`TYPE_TOGGLE`/`TYPE_TOGGLE_COLOR`/plain button), calls the row's `on_select`, and — only if a
sub-menu exists (`sub_menu` or `on_sub_menu_fill`) — additionally repositions the preview camera
via `store_set_camera_pos(Menu_data[current_index].camera)` or, for vehicles specifically,
`vcust_set_camera_pos(Menu_data[current_index].vcamera, true)` (`:465-471`). Both of these, plus
the mouse-drag-to-rotate/zoom handlers `character_mouse_drag`/`store_common_mouse_scroll` that
call `store_common_rotate_mouse_drag()`/`store_common_mouse_zoom()` (`store_common.lua:773`,
`:870`, `:881`), are **already documented**: `store_common_mouse_zoom` (`0x0080dcd0`) and
`store_set_camera_pos` (`0x00830670`) in `spec-lua-api-behaviour.md` §11.9/§11.10;
`store_common_rotate_mouse_drag` itself remains explicitly flagged OPEN there (same wrapper as
`store_common_mouse_zoom`, "not decompiled this pass," §11.9/§11.13) — not re-derived this pass
either, still a live open item in the pre-existing document, now cross-referenced rather than
duplicated.

## 4. The purchase/confirm flow — THE CENTRAL FINDING: cash deduction happens through a directly
Lua-bound native call, per item-type family, NOT a dataresponder post

This is the opposite pattern from `cell_rewards`'s "post and let C handle it" shape
(`spec-cellphone-menu-runtime.md` §12) — every traced walk-in store purchase instead ends in a
**direct native call visible in Lua**, and the insufficient-funds/confirm-dialog scaffolding is
generic (shared `Yes_no_choices`/`Ok_choice` tables and `Vdo_store_popup`,
`store_common.lua:41-60`) but the actual trigger logic is duplicated per store type, not
centralized in `store_common` itself:

- **Insufficient funds.** Every traced store does its own `if Store_common_player_cash >=
  price then ... else ...` check client-side before ever showing a popup (e.g.
  `store_weapon_upgrade_purchase`, `store_weapon.lua:1672`; `store_tattoo_purchase`,
  `store_tattoo.lua:128`) — on failure it shows the shared `Ok_choice` popup with
  `"HUD_SHOP_INSUFFICIENT_FUNDS"`. **This is purely a UI-side gate; the underlying purchase
  natives themselves generally do NOT enforce affordability** — confirmed for
  `pcr_purchase_tattoo` ("no affordability check," `spec-lua-api-behaviour.md` §43.3, line 15394),
  `store_weapon_purchase_ammo` ("no affordability check in this function at all," §38.3, line
  15070), and `vcust_purchase_wheels` (same, §38.5, line 15094) — a reimplementation that skips
  the client-side gate (e.g. a modified/patched client, or a desync in a reimplementation) can
  drive these natives into negative cash.
- **Confirm dialog.** On sufficient funds, the shared `Yes_no_choices` popup
  (`Store_common_popup:nav_enable(true, <store>_grid_confirm, <store>_popup_nav)`) is shown; its
  confirm handler is where the actual purchase native is called.
- **The actual cash deduction + item grant, one native per item-type family, confirmed by exact
  Lua source line and cross-referenced to the already-decompiled native body:**
  - **Weapons:** `store_weapon_grid_confirm` (`store_weapon.lua:1735-1794`) calls
    `store_weapon_purchase_weapon(category, id, is_cache, price)` (`:1759`) — a directly Lua-bound
    native (`0x008172d0`, `spec-lua-api-behaviour.md` §26.7). **Resolved 2026-10-10: it does
    charge cash — it was never missing, it was one call-graph level deeper than the first pass
    checked.** The real-purchase branch calls `0x00816f00(weaponRecord, mode=0, price)`
    unconditionally, before even branching on the ownership-check result; `0x00816f00` is itself a
    dual-mode helper (the SAME function the preview path also calls, with mode=1) whose mode-0
    branch negates/scales/clamps the price and calls the shared cash-adjust primitive
    `0x0094d920` with reason code `0xb` — the exact call shape `pcr_purchase_tattoo` already uses,
    and the same family as `cash_add`'s reason `13`. A "satchel"/thrown-weapon special case
    instead charges through `0x00816430`, the same primitive `store_weapon_purchase_ammo`
    tail-calls into with its own price argument. So weapon purchases are charged exactly like
    every sibling store family below — this native is not an exception, just one the first pass
    under-read.
  - **Weapon ammo:** `store_weapon_ammo_purchase` (`store_weapon.lua:1102`) calls
    `store_weapon_purchase_ammo(id, thrown_wpn_id, clip_size, price)` (`:1129`) — native
    `0x008165c0`, already documented (§38.3): explicitly "falls through to charging the price and
    recording the purchase **even on failure**" of the item-grant step — a confirmed real
    overcharge-on-failure bug.
  - **Vehicle customization:** `store_vehicle_component_confirm_final`
    (`store_vehicle.lua:626-638`) calls `vcust_purchase_component()` (`:628`, comment: "Stop
    previewing the component, keep it and deduct cash") — native `0x0081fXXX` family, already
    documented (§52.1): confirmed to charge cash from a **stale -1 selection-index global** that
    is reset to -1 on every exit, making a double-call (or a call before a fresh selection) read
    60 bytes before the real item array and charge/write garbage — "the most severe finding" in
    that tranche (real out-of-bounds read-and-write, not just a crash).
  - **Tattoos:** `store_tattoo_item_purchase_final` (`store_tattoo.lua:170-212`) calls
    `pcr_purchase_tattoo(Store_tattoo_current_price)` (`:198`) — native `0x008333a0`, already
    documented (§43.3): "only charges cash... no item, composite, or stat is touched," with "no
    sign check, so a negative price pays the player instead of charging them."
  - **Clothing:** `store_clothing_item_purchase_final` (`store_clothing.lua:1507-1549`) calls
    either `pcu_purchase_slot(slot_index)` or `pcu_purchase_outfit(outfit_id)` (`:1535`/`:1537`)
    depending on whether a single item or a whole outfit was bought — both already documented
    (§40.5): both "ignore the inventory-append function's 'full' return value" — a player at the
    2048-item/128-outfit cap "is charged the full price and receives nothing silently."
  - **Respect, not cash, flows the other way.** Every traced purchase confirm handler also calls
    `game_award_respect(...)` on success (`store_weapon_grid_confirm` doesn't directly, but
    `store_clothing_item_purchase_final:1531`, `store_vehicle_component_confirm2:611`,
    `store_tattoo_item_purchase_final:194` all do) — **respect is granted TO the player as a
    purchase reward, never spent as a currency**, despite `store_common`'s header having a fully
    generic "set_respect as a price" capability (`vdo_store_header.lua:203-213`) that no traced
    store type actually exercises that way.

**Net answer to the reimplementation-critical question:** a from-scratch client must implement
real cash-deduction logic triggered by these specific native call sites (one native per
item-type family: weapon/ammo/vehicle-component/tattoo/clothing-slot/clothing-outfit), each with
its own argument shape and its own (generally absent) server-side affordability check — this is
**not** a single generic "purchase" entry point, and **not** a dataresponder-post handoff like
`cell_rewards`. The client-side insufficient-funds gate in Lua is cosmetic only; a
reimplementation that wants parity with the shipped bugs (or wants to fix them) must decide
per-native whether to replicate retail's missing affordability/append-success checks.

## 5. Exit convention — single self-pop plus an automatic cascade from the shared base document,
structurally distinct from the phone menu's fixed-depth pops

`store_common_button_b` (`:523-559`) is the generic "back" handler: if a sub-menu is open it pops
one level of the internal stack (`store_common_back_menu`, `:561-602`); **if already at the
top-level menu** (`store_common_stack_get_menu() == nil`), it instead calls
`Store_common_exit_cb()` — the per-store-type hook set at init (e.g.
`Store_common_exit_cb = store_weapon_exit`, `store_weapon.lua:143`).

Each per-type exit handler shows its own confirm-to-leave dialog (skipped when entering from a
context that doesn't need one, e.g. `Store_weapon_loaded_from_crib`, `store_weapon.lua:486-497`),
then on confirm calls exactly **one** `pop_screen()` for its OWN document
(`store_weapon_exit_final`, `:559`; `store_weapon_crib_exit`, `:524`) — never a fixed 2/3/4-deep
pop like the phone-menu baseline (`spec-cellphone-menu-runtime.md` §4). The second layer (the
shared `store_common` document itself) is popped **automatically**, not by an explicit call in
the per-type file: `store_common_gained_focus()` (`store_common.lua:828-830`) — "If
`store_common` has just gained focus, it means we've left whatever store we were in. Unload
ourself." — fires when `store_common` becomes the new top-of-stack document after the per-type
doc pops, and itself calls `pop_screen()` unconditionally. **This is a genuinely different exit
shape from the phone menu's own uniform depth convention: a 1-deep explicit pop plus a 1-deep
implicit cascade triggered by a document-focus event, rather than either store knowing the
other's existence or a hardcoded pop count.** Consistent with stores being entered from
world-space triggers (so there is no fixed "return to icon grid" destination the way the phone
subtree always has `cell_menu_main`) — exiting a store returns straight to free-roam gameplay,
matching `store_common_play_anim_out` (`:832-835`) and the generic "exit the interface" comment
pattern seen at every per-type `pop_screen()` call site.

**Correction, 2026-10-10:** the claim above ("every traced purchase confirm handler also calls
`game_award_respect`") is not quite universal — `store_weapon.lua` is a real exception, confirmed
by grep (zero hits): weapon purchases grant no respect reward at all, unlike clothing/vehicle/
tattoo. See §6.

## 6. `store_weapon.lua` — full menu-depth trace: dual entry context, 4-tier structure, and the
complete purchase surface

**Scope.** This section documents `store_weapon.lua` to the same document-level depth as the
phone menu's own worked examples, building on §1-§5's generic `store_common` framework (not
re-derived here).

### 6.1 Entry context — confirmed dual: walk-in free-roam shop AND an in-crib "weapon cache,"
distinguished by one flag set at init

`store_weapon_init()` (`store_weapon.lua:113`) sets `Store_weapon_loaded_from_crib =
store_common_crib_is_loaded()` (`:117`), which is literally `vint_document_find("store_crib") ~=
0` (`store_common.lua:821-823`) — i.e. "is a document named `store_crib` currently on the screen
stack." This single check branches almost the entire file:

- `Store_weapon_is_cache = Store_weapon_loaded_from_crib` (`:120`) — when entered from the crib,
  the store is **always** in "cache" mode (equip-what-you-own), never "store" mode (buy-new).
- Highlight color differs (`COLOR_STORE_REWARDS_PRIMARY` vs `COLOR_STORE_WEAPON_PRIMARY`,
  `:126-130`).
- The crib branch (`:226-308`) skips the 4-item top-level menu entirely: it hides `Active_list`
  (`:237`), sets a different hint set, re-tints/hides several weapon-store-brand background
  elements and substitutes the Saints-crib background (`bg_saints_set_type`, `:293`), pushes title
  `"WPNSTOR_TITLE_WEAPON_CACHE"` and disables the price/respect header
  (`Store_header:enable_price_respect(false)`, `:283`), and sets `Current_tier = RADIAL_TIER`
  directly (`:308`) — bypassing `MENU_TIER`.
- The non-crib branch (`:310-378`) builds the real top-level static menu `Menu_data` (4 entries,
  `:311-317`, see §6.2) and calls `store_weapon_populate_list(Menu_data, 1)` (`:368`), leaving
  `Current_tier = MENU_TIER` (`:375`).
- `store_weapon_intro_anim_complete()` (`:386-401`) confirms the crib skip is real at runtime,
  not just cosmetic: non-crib calls `store_unlock_controls()` only; crib calls
  `store_weapon_cache_open_radial()` + `store_weapon_unlock_radial()` directly (`:395-396`),
  landing the player straight in the cache's category radial with no top-menu stop, unless
  `Store_weapon_cache_no_weapons` is set (player owns literally nothing — see §6.4).

Both contexts load the **same three-document stack**: `store_common` (always, per §1) underneath
`store_weapon`, and — only in the crib case — `store_crib` beneath that (its presence is exactly
what `store_common_crib_is_loaded()` tests for). A walk-in entry has no `store_crib` document at
all.

### 6.2 Menu structure — 4 real UI tiers, tracked by one local `Current_tier` integer
(`store_weapon.lua:26-31`): `MENU_TIER=1`, `RADIAL_TIER=2`, `WEAPON_TIER=3`, `LEVEL_TIER=4`,
`GRENADE_UPGRADE_TIER=5`

**Tier 1 — top-level menu (walk-in only).** A static 4-entry table, NOT dataresponder-populated
(`Menu_data`, `:311-317`): `STORE_WEAPON_AMMO` → `store_weapon_ammo_open_radial`,
`STORE_WEAPON_BUY_UPGRADE` → `store_weapon_purchase_open_radial`, `STORE_WEAPON_CACHE` →
`store_weapon_cache_open_radial`, `STORE_EXIT` → `store_weapon_exit`. Rendered via
`store_weapon_populate_list` (`:678-711`), which just calls `Active_list:draw_items(list_data,
current_index, ...)` — no `vint_dataresponder_request` at this tier at all, since the 4 rows are
hardcoded.

**Tier 2 — category radial** (the same `Vdo_weapon_radial` dpad widget reused for 3 different
sub-modes, selected by which function opened it):
- *Ammo mode* — `store_weapon_ammo_open_radial` (`:750-790`):
  `Store_weapon_inventory.selected_cb = store_weapon_ammo_purchase`, `.nav_cb =
  store_weapon_ammo_nav`; issues `vint_dataresponder_request("store_weapon_populate_ammo",
  "store_weapon_ammo_add_items", 0)` (`:766`) into local
  `Store_weapon_ammo_info`/`Store_weapon_thrown_ammo_info` tables; disables slots 0-1
  (melee/pistol — can't purchase ammo for some categories, confirmed by the `slot < 2` guard in
  `store_weapon_ammo_nav`/`store_weapon_ammo_purchase`). Callback signature:
  `store_weapon_ammo_add_items(unused, icon_name, unused, price, clip_size, current_amount,
  max_amount, id, thrown_wpn_id)` (`:1078`).
- *Purchase/upgrade mode* — `store_weapon_purchase_open_radial` (`:1176-1239`): `.selected_cb =
  store_weapon_grid_open`, `.nav_cb = store_weapon_purchase_nav`; pre-populates and
  slot-enables/disables all 7 weapon categories up front by issuing
  `vint_dataresponder_request("store_weapon_populate_weapons", "store_weapon_grid_add_items", 0,
  Store_weapon_is_cache, category)` once per category (`:1223`).
- *Cache mode* — `store_weapon_cache_open_radial` (`:1800-1855`): same `.selected_cb =
  store_weapon_grid_open`, `.nav_cb = store_weapon_cache_nav`; hides the dpad thrown-weapon
  sub-menu (`show_dpad_menu(false)`, `:1815`, cache has no "cache a grenade" concept) and does the
  identical per-category pre-check.

**Tier 3 — weapon grid (3×3 color grid, `Store_common_color_grid`).** Selecting a category in
tier 2 calls `store_weapon_grid_open(slot)` (`:1253-1339`): issues
`vint_dataresponder_request("store_weapon_populate_weapons", "store_weapon_grid_add_items", 0,
Store_weapon_is_cache, category, slot-8)` (`:1272`; the trailing `slot-8` is "only used for
grenade slot index" per the file's own comment), then
`Store_common_color_grid:draw_items(Weapon_grid_data, Weapon_grid_idx, 3, 3, 120, 100, 40, nil,
nil, false)` (`:1288`). Row callback shape from `store_weapon_grid_add_items(name_str, icon_name,
price, own, equipped, id, ammo_current, ammo_max, unlimited_ammo, dual_wield, upgrade_level,
desc_crc)` (`:1435`). **For `category == "thrown"`, the grid is bypassed entirely** —
`store_weapon_grid_open` immediately calls `store_weapon_grid_selected("select")` on itself
(`:1323`) and sets `Current_tier = GRENADE_UPGRADE_TIER` instead of `WEAPON_TIER`
(`:1322/1331`) — thrown weapons go straight from the category radial to tier 4.

**Tier 4 — upgrade-level list (`LEVEL_TIER`), reusing `Active_list`.** Selecting a weapon in the
grid (non-cache) calls `store_weapon_grid_selected("select")` (`:1466`) →
`store_weapon_upgrade_show(category, id)` (`:1573-1587`): issues
`vint_dataresponder_request("store_weapon_populate_upgrades", "store_weapon_upgrade_add_items", 0,
category, id)` (`:1579`) into a fresh `Store_weapon_building_menu` table, then
`store_common_push_menu(Store_weapon_building_menu)` (`:1580`, the shared sub-menu-stack push from
§2/§5). Row callback shape: `store_weapon_upgrade_add_items(desc_crc, price, is_purchased,
is_locked)` (`:1589-1621`) — each row IS one weapon "level" (level 1 = the base weapon itself,
higher levels = stat upgrades), carrying its own `on_select = store_weapon_upgrade_purchase` and
`on_cancel = store_weapon_upgrade_revert` (`:1604-1605`).

**Net menu depth for a real purchase:** top menu (1) → category radial (2) → weapon grid (3) →
level list (4) → confirm popup → native call. 4 real tiers deep, with the detail/upgrade screen
itself being `LEVEL_TIER`'s megalist rather than a distinct screen/document.

### 6.3 The full purchase surface — every purchasable thing, button→native chain

This store has **no separate "attachment"/scope sub-store** — there is exactly one generic
purchase model (a weapon's "levels," where level 1 is the base weapon and further levels are paid
upgrades, all going through the same native), plus ammo, plus a non-purchase "equip from cache"
path:

1. **New weapon (level 1) or a stat upgrade (level 2+).** Both go through the identical chain —
   there is no code-level distinction between "buying the weapon for the first time" and "buying
   its next upgrade level," both are just "purchasing an unpurchased level row":
   `store_weapon_upgrade_purchase(menu_data)` (`store_weapon.lua:1665-1693`) — guards
   `menu_data.is_purchased`/`is_locked` (`:1668`), then client-side cash gate
   (`Store_common_player_cash >= menu_data.price`, `:1672`) → shows `Yes_no_choices` confirm
   popup, `nav_enable(true, "store_weapon_grid_confirm", "store_weapon_popup_nav")` (`:1678`).
   → `store_weapon_grid_confirm(event)` (`:1735-1794`) on Yes: calls
   `store_weapon_purchase_weapon(Weapon_grid_data.category, Store_weapon_current_info.id,
   Store_weapon_is_cache, menu_data.price)` (`:1759`) — native `0x008172d0`, already fully
   documented in `spec-lua-api-behaviour.md` §26.7 (confirmed as of 2026-10-10 to charge cash via
   its callee `0x00816f00`'s mode-0 branch → shared cash-adjust primitive `0x0094d920`, reason
   `0xb`). Refreshes the level-list afterward via the same `"store_weapon_populate_upgrades"`
   channel (`:1782`) and also re-issues `"store_weapon_populate_ammo"` with a shared no-op
   callback `"store_common_do_nothing"` purely to "reload the ammo purchases for thrown weapons"
   (`:1787`).
   **No `game_award_respect` call anywhere in `store_weapon.lua`** (confirmed by grep — zero
   hits) — unlike `store_clothing`/`store_vehicle`/`store_tattoo` (§4), which call
   `game_award_respect` on every successful purchase. Weapon purchases grant no respect reward —
   a genuine exception to §4's "every" framing, corrected above.
2. **Ammo.** `store_weapon_ammo_purchase(slot)` (`:1102-1170`) — no confirm popup at all, purchase
   happens immediately on radial-slot selection. Inline client-side gate:
   `ammo_info.ammo_current < ammo_info.ammo_max` AND `Store_common_player_cash >=
   ammo_info.price` (`:1124-1125`) → `store_weapon_purchase_ammo(ammo_info.id,
   ammo_info.thrown_wpn_id, ammo_info.clip_size, ammo_info.price)` (`:1129`) — native
   `0x008165c0`, already documented §38.3 ("falls through to charging the price and recording the
   purchase even on failure" — confirmed real overcharge bug). On full ammo or insufficient
   funds, shows the shared `Ok_choice` popup (`:1148-1167`).
3. **Equip-from-cache (NOT a purchase, price forced to 0).**
   `store_weapon_grid_selected("select")` (`:1466`), cache branch (`:1487-1518`): calls
   `store_weapon_purchase_weapon(Weapon_grid_data.category, weapon_info.id, Store_weapon_is_cache,
   0)` (`:1491`) directly — same native as item 1 above, but with a hardcoded `price = 0` and no
   confirm popup at all (selecting an owned weapon in the cache just equips it). Since the native
   unconditionally calls its cash-charge callee regardless of mode per §4's finding, this is a
   real "charge $0" call rather than a code path that skips the native.

**No distinct attachment/scope purchase exists in this file.** There is exactly one upgrade
mechanism (the level list), not a separate attachment sub-store.

### 6.4 Weapon preview/equip mechanics — passive 3D model-swap preview only; no test-fire, no
mouse-rotate, no shared camera-reposition call

`store_weapon_change_weapon_preview(category, id, dual_wield, is_cache, upgrade_level)`
(`:2199-2209`) is the single preview entry point, called from: initial grid-open
(`store_weapon_grid_open`, `:1311`), grid navigation (`store_weapon_grid_nav`, `:642`), every
upgrade-level highlight (`store_weapon_upgrade_nav`, `:1626`), and reverting out of the level
list (`store_weapon_upgrade_revert`, `:1716`). It sets `Store_weapon_is_loading = true`
(input-lock) unless only an upgrade-level swap is happening, then calls
`store_weapon_change_weapon(category, id, dual_wield, is_cache, upgrade_level)` — native
`0x00815f40`, already documented (not re-derived here): purely UI preview state, consumed by the
store's own per-frame code, not traced.

**Confirmed deviations from the generic `store_common` preview framework (§3):**
- `store_weapon.lua` never calls `store_set_camera_pos` or `vcust_set_camera_pos` anywhere
  (grep: zero hits) — the generic "`sub_menu`/`camera` field on a `Menu_data` row repositions the
  preview camera" mechanism from `store_common_button_a` is simply unused here; none of this
  store's rows carry a `camera`/`vcamera` field. Camera positioning for the weapon preview, if
  any, is entirely inside the native `store_weapon_change_weapon`'s own per-frame C-side consumer,
  not driven from Lua.
- `store_weapon_mouse_drag`/`store_weapon_mouse_drag_release` (`:2324-2328`) are empty stub
  functions — **no mouse-drag-to-rotate exists for the weapon preview**, unlike the generic
  `character_mouse_drag`/`store_common_rotate_mouse_drag` rotate-preview mechanism §3 documents
  for other store types. `store_weapon_mouse_scroll` (`:2321-2322`) is likewise an empty stub —
  no zoom either (a dead comment confirms this was deliberately cut: `-- SEH: 6/28/11: cut
  zoom/rotate`, `:164`/`:1372-1374`/`:1399-1401`).
- **No test-fire/equip-before-buying mechanic found anywhere in the file** — the only "equip"
  path is the cache's own direct-equip-from-owned-items flow (§6.3 item 3), which requires
  already owning the weapon, not a try-before-buy feature.

Net: weapon preview is a static, non-interactive 3D model swap only (model + level variant), one
dimension simpler than other store types' camera-reposition/rotate/zoom framework.

### 6.5 Exit convention — matches the shared 1-explicit-pop + 1-automatic-cascade pattern exactly
in both entry contexts; the crib flag changes *when a confirm dialog shows* and *what's left on
the stack after the cascade*, not the pop mechanics

`store_weapon_exit()` (`:484-520`) branches on `Store_weapon_loaded_from_crib`:
- **Crib path** (`:486-497`): **no confirmation dialog at all.** Plays audio, locks controls,
  plays slide-out animations whose tween end-event is `"store_weapon_crib_exit"` (`:495`) →
  `store_weapon_crib_exit()` (`:522-525`) = a bare `pop_screen()`, nothing else.
- **Walk-in path** (`:498-515`): shows the shared `Yes_no_choices` confirm popup (text varies by
  sub-mode, `:502-506`) → on Yes, `store_weapon_exit_final(event)` (`:527-560`) plays an exit
  animation and calls `pop_screen()` once (`:559`).

Both branches call **exactly one** `pop_screen()` for the `store_weapon` document itself —
identical to the shared §5 convention, no deviation in pop count. The shared `store_common`
document's own automatic cascade (`store_common_gained_focus`) fires unconditionally afterward
in both cases, unchanged. **What the crib flag actually changes:** (1) whether a confirm-to-leave
prompt interrupts the exit at all (skipped in-crib), and (2) what document is left after the
cascade — a walk-in exit lands on free-roam gameplay; an in-crib exit lands back on the
`store_crib` document that `store_common_crib_is_loaded()` detected at init, since that document
was never touched by either pop.

## 7. `store_clothing.lua` — full menu-depth trace: single walk-in/wardrobe-toggle entry context,
area→category→item(→detail) structure, dual-purchase-native surface, and a confirmed
3D-turntable preview (unlike `store_weapon.lua`)

**Scope.** This section documents `store_clothing.lua` to the same document-level depth as §6's
`store_weapon.lua` worked example, building on §1-§5's generic `store_common` framework (not
re-derived here). `store_character.lua` was checked and confirmed to be a genuinely separate
system (the character-creator/"plastic surgeon" `pcr_` native family, `spec-lua-api-behaviour.md`
§43.3) — not traced further here.

### 7.1 Entry context — no crib/cache split like weapons; instead a single document with a
runtime store↔wardrobe mode toggle, plus 6 cosmetic sub-brand skins

`store_clothing_init()` (`store_clothing.lua:107`) sets `Store_clothing_loaded_from_crib =
store_common_crib_is_loaded()` (`:112`) — structurally the same crib-detection one-liner §6.1
found for weapons — but it does **not** gate a cache-vs-store mode split the way weapons does.
Instead it gates a different flag: `Store_clothing_is_wardrobe = Store_clothing_loaded_from_crib`
(`:168`, set inside `store_clothing_set_store`) — "wardrobe" here means "browsing/re-wearing
already-owned items, no purchase UI," the clothing-store equivalent of the weapon cache's "equip
what you own" mode. Unlike weapons, **wardrobe mode is not crib-exclusive**: a walk-in store also
exposes a `"MENU_SWAP_WARDROBE"` top-level menu item (`store_clothing_mode_set_init`,
`:1760`/`:1814`) that calls `store_clothing_switch_mode()` (`:1866`) to flip
`Store_clothing_is_wardrobe` and re-run `store_clothing_mode_set_init()` live, without leaving the
document. So where weapons have two separate *entry contexts* (walk-in vs. crib, fixed for the
document's lifetime), clothing has **one entry context with a live in-document mode toggle** —
crib entry simply forces wardrobe mode permanently (no `"MENU_SWAP_WARDROBE"`/`"STORE_EXIT"`/
`"STORE_CHARACTER_GALLERY"` items are added to the top menu when `Store_clothing_loaded_from_crib`,
`:1752-1757`).

A second entry-time branch is purely cosmetic: `Store_index = store_clothing_get_store_id()`
(`:136`, the native `0x0080db90` documented in `spec-lua-api-behaviour.md` §39.5 — itself deferring
to an un-opened callee `0x0080d810`, still OPEN there) selects one of 6 sub-brand skins (`CRIB`,
`LETS_PRETEND`, `NOBODY_LOVES_ME`, `LEATHER_AND_LACE`, `PLANET_SAINTS`, `IMPRESSIONS`,
`store_clothing.lua:18-23`/`:25-32`) that only changes background art, logo peg, highlight color,
and entry/exit animations (`store_clothing_set_store`, `:163-406`) — overridden to `CRIB`
unconditionally when entered from the crib (`:139-141`). This is the clothing-store equivalent of
weapons' `COLOR_STORE_WEAPON_PRIMARY` tint swap, just with 6 skins instead of 1, and has no effect
on menu structure or the purchase surface.

Both contexts load the same two-document stack as weapons: `store_common` always underneath
`store_clothing`, and — crib case only — `store_crib` beneath that. The native/world-trigger call
site that actually pushes these documents remains the same unresolved OPEN item §8#1 already
tracks (now parked on the live-capture list) — not re-investigated this pass.

### 7.2 Menu structure — area → category → item (→ color/style/wear-option detail), tracked by
the same `store_common` sub-menu-stack mechanism; a separate outfit sub-tree; no fixed tier count
the way weapons has 4

Unlike `store_weapon.lua`'s `Current_tier` integer with 5 named tiers, `store_clothing.lua` only
declares 2 (`MENU_TIER=1`, `COLOR_TIER=2`, `:97-99`) — `Current_tier` here exists solely to
disambiguate the color-grid picker from everything else for mouse-click routing
(`store_clothing_mouse_click`, `:2239-2248`); menu *depth* itself is tracked entirely by
`store_common`'s generic push/pop sub-menu stack (§2/§5), not by a tier integer, since clothing's
depth is data-driven (how many areas/categories a given store has) rather than a fixed design.

**Tier 1 — top-level menu: areas.** `store_clothing_mode_set_init()` (`:1716-1782`) issues
`pcu_get_areas(store_clothing_name, "store_clothing_area_add")` (`:1742`) where
`store_clothing_name` is literally `"wardrobe"` or `"DEBUG_CLOTHING_STORE"` (`:1726-1734`) —
**confirming the wardrobe/store split is also a distinct native-side area-set selector, not just
a UI label change.** Callback: `store_clothing_area_add(area_index, label_crc, label_str,
has_new_items)` (`:638-660`), each row carrying `on_sub_menu_fill = store_clothing_category_get_all`.
Static rows appended after: `"CUST_ICON_OUTFITS"` (wardrobe only, opens the separate outfit
sub-tree, §7.2.1), `"MENU_SWAP_WARDROBE"`, `"STORE_CHARACTER_GALLERY"` (→
`push_screen("store_gallery")` after an online/privilege check, `:1860`), `"STORE_EXIT"` (store
mode only).

**Tier 2 — category.** Selecting an area calls `store_clothing_category_get_all(menu_data)`
(`:662-677`): issues `pcu_get_categories_in_area(Store_clothing_area,
"store_clothing_category_add")` (`:664`) and `pcu_set_active_category(...)` (`:666`,
server/model-side "what's currently being browsed" state, mirrored again on category-nav at
`:995`). **If an area has exactly one category, this tier is skipped entirely** — it falls
straight through to tier 3 (`:669-673`), the same "collapse a trivial single-entry tier" shape
weapons' thrown-weapon category shows (§6.2). Callback: `store_clothing_category_add(cat_index,
label_crc, label_str, has_new_items)` (`:701-723`), row carrying `on_sub_menu_fill =
store_clothing_item_get_list`.

**Tier 3 — item list.** `store_clothing_item_get_list(menu_data)` (`:725-758`) issues
`pcu_get_items_in_category(menu_data.id, "store_clothing_item_add")` (`:739`) — the same native
§43.2 already documents as having a real infinite-loop hang in one of its reporting modes (now
cross-referenced, not re-derived). Callback signature is the widest in the file:
`store_clothing_item_add(item_id, label_crc, label_str, is_outfit, price, style,
inc_wardrobe_instance, is_available, is_reward, is_unusable_dlc, is_new, is_dlc, variant_num,
variant_lbl_crc, variant_lbl, is_current, brand_id, disable_removal)` (`:790-868`) — 18 arguments,
each list row potentially being a single item OR a whole pre-built "outfit" entry (`is_outfit`),
distinguished per-row, not per-category. A synthetic `"HAIR_CAT_NONE"` ("remove this slot") row is
auto-inserted first unless `disable_removal` or this is an outfit list (`:795-813`).

**Tier 4 — item detail (non-outfit rows only).** Selecting a single item (not outfit) opens
`store_clothing_item_details(menu_data)` (`:1090-1202`), which builds a small on-the-fly toggle
menu, **not drawn through another `vint_dataresponder_request` round-trip for its own rows** —
color/style/wear-option sub-pickers are themselves populated via one-shot callback iterators
(`pcu_report_item_colors`→`store_clothing_color_add` only when the color-grid is actually opened,
`:1391`; `pcu_get_variants`→`store_clothing_style_add`, `:1138`; `pcu_get_wear_options`→
`store_clothing_wear_option_add`, `:1156` — all three already documented in
`spec-lua-api-behaviour.md` §40.5). This tier's final row is the actual purchase/wear trigger
(§7.3).

**7.2.1 — the separate outfit sub-tree (wardrobe only).** `store_clothing_wardrobe_outfit_menu_fill`
(`:1703-1714`) is a 4-item static menu reachable only from wardrobe mode's `"CUST_ICON_OUTFITS"`
row: *View Outfits* (→ `store_clothing_outfit_get_all`, lists via `pcu_report_outfits`→
`store_clothing_outfit_add`, already documented §40.5), *Create Outfit* (→
`store_clothing_outfit_create`, gated by `pcu_can_create_outfit()` — the already-documented
incomplete-guard native §43.2 flags — then a virtual-keyboard name entry feeding
`pcu_create_outfit("VKEYBOARD_RESULTS")`, the already-documented null-write-on-exhausted-pool
native), *Delete Outfit* (→ `pcu_delete_outfit`), *Remove All Clothing* (→ `pcu_remove_clothing`,
the already-documented "crashes on a null local player" native, §40.5). This sub-tree is
entirely orthogonal to the area→category→item tree — it operates on whole saved outfits, not
individual slots.

**Net menu depth for a real item purchase:** area (1) → category (2, sometimes skipped) → item
list (3) → item detail (4, color/style/wear-option sub-toggles + purchase row) → confirm popup →
native call. Structurally the same depth as weapons (§6.2's "4 real tiers"), but data-driven
rather than fixed, and with an entirely separate outfit sub-tree weapons has no equivalent of.

### 7.3 Purchase surface — confirmed: `pcu_purchase_slot`/`pcu_purchase_outfit` are chosen
automatically by row kind, not by a UI choice; a third, non-purchase "wear from wardrobe" path
exists; no color/variant purchase charge beyond the item price

`store_clothing_item_purchase_final(event)` (`:1507-1549`) is the sole call site for both purchase
natives, and the dispatch is **entirely automatic, keyed off which kind of row the player drilled
into** — there is no "buy as outfit vs. buy individual pieces" UI choice exposed to the player:
`Store_clothing_outfit_id == -1` calls `pcu_purchase_slot(Store_clothing_item_info.slot_index)`;
otherwise it calls `pcu_purchase_outfit(Store_clothing_outfit_id)` (`:1534-1538`).
`Store_clothing_outfit_id` is set to -1 whenever a single-item row is navigated
(`store_clothing_item_nav`, `:885`) and to the outfit's real id whenever an outfit-flagged row is
navigated instead (`:953`) — so the branch is decided purely by **which list row the player was
last looking at**, confirming both already-documented natives (§40.5) are real, both reachable,
and the dispatch between them is automatic rather than player-chosen.

Both branches are preceded by the standard client-side gates already generalized in §4: a
category-capacity check against 150 items (`pcu_get_num_items_category`, `:1337`), a
total-inventory check against 2048 items (`pcu_get_num_items_owned`, `:1350` — matching the
2048-item cap §40.5 already documents server-side), then the cash gate
(`item.item_price > Store_common_player_cash`, `:1363`), then the shared `Yes_no_choices` confirm
popup (`:1375-1378`). On confirm, `game_award_respect(Store_clothing_item_info.item_style, nil,
nil, true)` fires unconditionally before the purchase native itself runs (`:1531`), even on the
`pcu_purchase_outfit` branch — buying a whole outfit grants only the currently-displayed row's own
style value as respect (confirmed correct by the data contract — the outfit row's own `menu_data`
carries an aggregate `style` value, not a bug).

**No undocumented purchase natives found** — `pcu_purchase_slot`/`pcu_purchase_outfit` (both §40.5)
are the only two cash-charging calls in the file (grep confirmed).

**A third, non-purchase path exists and is new to this write-up: "wear from wardrobe," zero cost,
zero native overlap with purchasing.** In wardrobe mode, the item-detail menu's final row is
`store_clothing_item_wear` (`on_select`, `:1187`/`:1206-1212`) instead of
`store_clothing_item_purchase` — it calls only `pcu_wear_current_clothing()` (no price, no
`pcu_purchase_*` call at all) then pops two menu levels. This is the clothing-store equivalent of
weapons' "equip from cache, native call with price forced to 0" (§6.3 item 3), but structurally
cleaner: clothing's wear path doesn't even touch the purchase native, it calls a dedicated
wear-only native. **`pcu_wear_current_clothing` does not appear in `spec-lua-api-behaviour.md`
under that exact name** (grepped, no hits) — **new native lead**, called
`store_clothing.lua:769`/`:1208`. (Its sibling `pcu_wear_outfit`/`pcu_wear_store_outfit` — the
owned/catalog outfit-preview appliers — ARE already documented in §40.5, called from the
background preview thread, §7.4, not from a menu `on_select`.)

**Wardrobe also has a separate "discard" (destroy, not just un-equip) path**,
`store_clothing_item_discard`/`_discard_confirm_1`/`_discard_confirm_2` (`:1216-1329`), calling
`pcu_discard_slot(slot_index, wardrobe_instance)` — a second, lower-priority new lead (not found
in `spec-lua-api-behaviour.md` under its own exact name; likely close kin to
`pcu_remove_clothing`'s already-documented "clearable store-entry slot" semantics, §40.5, but not
individually specced). It has two escalating confirm dialogs: a generic discard-warning, with a
second extra warning specifically when `pcu_is_item_needed_for_outfit(slot_index)` (already
documented, §40.5) reports the item is referenced by a saved outfit — discarding it anyway would
silently break that outfit, a real (if non-crashing) data-integrity gap this file is aware of and
warns about, but does not prevent.

**Color/variant selection is preview-only, folded into the item price — no separate charge.**
`store_clothing_color_select`/`store_clothing_color_nav`/`store_clothing_style_nav`/
`store_clothing_wear_option_nav` (`:1384-1403`, `:998-1006`, `:1571-1602`) only ever write into
`Store_clothing_item_info` and flag `Store_clothing_waiting_update_value` for the background
preview thread (§7.4) — none of them call a purchase native or change `item.item_price`. A
reimplementation should treat color/variant/wear-option choice as pure client-side preview state
folded into the single price already charged by `pcu_purchase_slot`, not a separately-priced
add-on.

### 7.4 Preview mechanics — CONFIRMED: this store uses the full generic `store_common`
camera/mouse-rotate framework §3 documents, in direct contrast to `store_weapon.lua`'s confirmed
deliberate opt-out

Three independent confirmations that clothing is a real 3D character turntable, unlike weapons:

1. **`character_enable_mouse_drag(true)` is called 3 times** — at init (`:153`), at
   `store_clothing_gained_focus()` (`:603`), and disabled at cleanup
   (`character_enable_mouse_drag(false)`, `:507`). This is the exact shared primitive
   `store_common.lua:899` exposes, which gates whether `character_mouse_drag`/
   `character_mouse_drag_release` (`store_common.lua:877-888`) — the functions that call
   `store_common_rotate_mouse_drag()` (§3's already-cross-referenced, still-OPEN rotation native)
   — are actually wired to the mouse-drag input region. **Grepped `store_weapon.lua` for
   `character_enable_mouse_drag`: zero hits** — direct, confirmed contrast with §6.4's weapon
   finding that rotation was deliberately cut there.
2. **`store_set_camera_pos("body")` is called twice**, both on returning to a body-wide view:
   `store_clothing_restore_items` (`:1700`, fired whenever the top-level area menu is refreshed)
   and `store_clothing_switch_mode` (`:1893`, on toggling store↔wardrobe). This is precisely the
   generic camera-reposition call §3 documents and §6.4 confirmed `store_weapon.lua` never calls
   at all.
3. **The gamepad rotate hint is actively managed**: `{CTRL_BUTTON_RS, "STORE_ZOOM_ROTATE"}` is
   set and shown/hidden per-platform throughout (`store_clothing_set_store`, `:184-199`;
   `store_clothing_set_hints`, `:1689-1692`) — on PC it's explicitly hidden (`:198`) since PC uses
   mouse-drag instead (confirmed by item 1), on console it's shown. Weapons' own file never sets
   up this hint at all (dead-cut per its own comment, §6.4).

A separate, file-local `store_clothing_mouse_drag`/`store_clothing_mouse_drag_release`/
`store_clothing_mouse_scroll` set (`:2279-2298`) exists but is **not** the character-rotate
mechanism — grepped and read in full, these are scoped exclusively to the color-grid's own
scrollbar widget — a second, unrelated mouse-drag consumer that happens to share the generic
naming convention. A reimplementation must not conflate "clothing store has its own mouse-drag
handler" with "clothing store implements its own rotation" — rotation itself is 100% the shared
`store_common`/`character_mouse_drag` path, confirmed by item 1 above.

**Preview update is a background-thread poll, not an immediate per-selection call** —
`store_clothing_item_preview_update()` (`:1614-1659`) runs forever in its own coroutine
(`thread_new`, started once at `:220`), polling `Store_clothing_waiting_update_value` every 0.2s
(gated on `pcu_is_streaming_done()`, the same streaming-gate the §6.4-adjacent cache logic also
uses) and dispatching to one of three already-documented appliers depending on which of 3
pending-states is set: `pcu_set_slot_info`/`pcu_set_slots_empty` (single item),
`pcu_wear_store_outfit` (catalog outfit preview), `pcu_wear_outfit` (owned/saved outfit preview,
with a `finalize` boolean distinguishing "just previewing" from "actually committing the wear" —
set true only after the explicit wear-confirm dialog, `:2004`). This decoupled poll-thread shape,
rather than a direct call from the nav callback, is new relative to weapons' synchronous
`store_weapon_change_weapon_preview` call (§6.4) and worth flagging as a genuine architectural
difference, not just more code — a faithful reimplementation of clothing preview timing needs the
same ~0.2s-latency async poll, not an instant swap.

### 7.5 Exit convention — matches the shared pattern with one real clothing-specific deviation:
wardrobe-from-store exit doesn't pop at all, it just switches mode back

`store_clothing_exit()` (`:518-549`) branches three ways, one more than weapons' two:
- **Crib path** (`:521-531`): no confirm dialog, same shape as weapons' crib exit — plays a
  slide-out animation whose end-event is `store_clothing_crib_exit` (`:551-554`) = a bare
  `pop_screen()`.
- **Wardrobe-reached-from-a-walk-in-store path** (`:534-537`): **`store_clothing_exit()` returns
  without ever popping anything** — it just calls `store_clothing_switch_mode()` to flip back to
  store mode in place. This is the one real structural deviation from the shared §5 pattern:
  hitting "back" at the top of the wardrobe's menu stack, when wardrobe was reached via the
  in-store toggle (not via the crib), does not exit the document at all, it exits *wardrobe mode*
  and lands back on the store's own top-level menu — the player must hit back a second time, now
  in store mode, to actually trigger the walk-in confirm-popup path below.
- **Walk-in store-mode path** (`:539-548`): the standard shared `Yes_no_choices` confirm popup →
  `store_clothing_exit_final(event)` (`:556-590`) → exactly **one** `pop_screen()` (`:589`),
  identical to §5/§6.5's convention.

All three paths are consistent with the already-documented `store_common_gained_focus()`
automatic cascade (§5) for the shared base document — unaffected by clothing's extra mode-toggle
branch, since that branch never touches the document stack at all. `store_clothing_cleanup()`
(`:478-516`) additionally calls `pcu_clear_preview_slot()` on document unload specifically to
"remove anything that hasn't been purchased" — a clothing-specific belt-and-suspenders
preview-rollback step weapons' own exit/cleanup has no equivalent of (weapons' preview is a
passive display with no "slot" state to roll back).

### 7.6 Net comparison with `store_weapon.lua`

Clothing is a **strict superset in UI surface** (6 skin palette, live store↔wardrobe toggle,
separate outfit-management sub-tree, color/style/wear-option sub-pickers, 3D turntable with
rotate/zoom) but is **simpler in purchase-native dispatch**: exactly one automatic if/else
(outfit-id sentinel) picks between the two already-documented purchase natives, versus weapons'
single shared "levels" purchase mechanism reused for both base-item and upgrade buys. Both stores
share the identical shared-framework exit/popup/cash-gate conventions from §1-§5; clothing's one
real deviation (wardrobe-mode "back" not popping the document) is a new, confirmed finding not
previously documented.

## 8. `store_vehicle.lua` — full menu-depth trace: a triple entry-context axis (crib × walk-in ×
live garage/customize toggle), the richest purchase surface of the three traced stores (5
distinct cash-charging natives), full live 3D preview across paint/wheels/underglow, and a
confirmed exit-convention deviation (`pop_screen()` is never called anywhere in this file)

**Scope.** This section documents `store_vehicle.lua` to the same document-level depth as §6/§7's
worked examples, building on §1-§5's generic `store_common` framework (not re-derived here). The
purchase-confirm chain `store_vehicle_component_confirm_final` → `vcust_purchase_component`
(`:626-638`), already summarized in §4, is incorporated below rather than re-derived.

### 8.1 Entry context — THREE overlapping axes, richer than either sibling store: crib-vs-walk-in,
garage-vs-customize mode, and which mode the document started in (controls exit behavior)

`store_vehicle_init()` (`:2411-2559`) sets `Store_vehicle_loaded_from_crib =
store_common_crib_is_loaded()` (`:2417`, the same one-liner §6.1/§7.1 found) — but unlike either
weapon or clothing, a **second**, independent flag is resolved from a live native query, not just
the crib check:

```lua
if store_vehicle_get_state() == MODE_VCUST and Store_vehicle_loaded_from_crib == false then
    Store_vehicle_is_garage = false
else
    Store_vehicle_is_garage = true
end
Store_vehicle_begin_from_garage = Store_vehicle_is_garage
```
(`:2421-2427`). `store_vehicle_get_state` (`0x008133c0`, already documented
`spec-lua-api-behaviour.md` §10.1) is a trivial 0/1 getter over global `0x022cdf08`, written by
exactly 3 functions including `store_vehicle_change_mode` (`0x00815120`, §38.4) — i.e. a real
piece of engine-side session state set by whatever world trigger launched this store, not a
Lua-local flag. The boolean logic above means `Store_vehicle_is_garage` starts **false** (pure
vehicle-customization/"rim jobs" mode) in exactly one case — walked into a dedicated
customization shop, not the crib — and **true** in every other case (crib always; a walk-in
garage building where the engine reports `MODE_GARAGE`).

This produces three confirmed, distinct entry shapes:
- **A — walk-in customization-only shop** (`Store_vehicle_loaded_from_crib == false`, engine
  state `MODE_VCUST`): begins directly in customize mode (`Store_vehicle_is_garage = false`),
  background set via `bg_saints_set_type(BG_TYPE_DEFAULT, true, 1280, 0)` (`:2499`), plays
  `"enter_rim_jobs"` audio (`:2512`).
- **B — walk-in garage building** (not crib, engine state `MODE_GARAGE`): begins in garage mode
  (`Store_vehicle_is_garage = true`, `Store_vehicle_begin_from_garage = true`), plays
  `"Enter_Garage"` audio (`:2508`), slides the background in (`bg_saints_slide_in(1280, 0)`,
  `:2497`).
- **C — in-crib garage** (`Store_vehicle_loaded_from_crib == true`): always begins in garage mode
  regardless of engine state (the `and Store_vehicle_loaded_from_crib == false` clause forces
  this), hides the vehicle-store's own background group and reuses the Saints crib background
  instead (`:2524-2550`), and loads a third document beneath `store_vehicle`/`store_common`:
  `store_crib` — the same stack shape §6.1/§7.1 found for weapons/clothing.

**All three contexts expose a live, bidirectional mode toggle, same shape as clothing's wardrobe
toggle (§7.1) but on a different axis (garage↔customize, not store↔wardrobe):**
`store_vehicle_switch_mode()` (`:3091-3160`) flips `Store_vehicle_is_garage` in place and calls
`store_vehicle_change_mode(MODE_VCUST)` or `store_vehicle_change_mode(MODE_GARAGE)` (the
already-documented native, §38.4) without ever touching the document stack. It is reachable
from: the top-level area menu's own `"GARAGE_CRIB_NAME"` row (customize→garage, gated on
`store_vehicle_allow_garage()` — the already-documented 13-mission denylist native,
`spec-lua-api-behaviour.md` §49.1 — `store_vehicle_build_areas`, `:2335-2341`); and the garage
vehicle-options popup's `GARAGE_SELECT_CUSTOMIZE` choice (garage→customize, `store_garage_select`,
`:498-502`, skipped straight through if `garage_get_repair_cost() == 0`, else gated behind a
"repair before customizing" notice popup, `:503-529`).

**`Store_vehicle_begin_from_garage` (cached at init, never updated) is what actually controls
exit behavior**, not the live `Store_vehicle_is_garage` — see §8.5. This 3-way entry shape (crib ×
walk-in × live mode-toggle, with the *starting* mode specifically cached for exit-time comparison)
is structurally richer than weapon's clean binary split (§6.1) or clothing's single-toggle shape
(§7.1).

### 8.2 Menu structure — area (tier 1) → one of 6 row-kind dispatches (tier 2+), with a real
tier-1 special case for the wheel area and a flat, non-nested garage list

**Tier 1 — areas**, `store_vehicle_build_areas()` (`:2326-2362`): issues
`vint_dataresponder_request("vcust_populate_menu", "store_vehicle_add_area", 0,
STORE_VEHICLE_POPULATE_AREAS)` (`:2330`, `STORE_VEHICLE_POPULATE_AREAS = 0`). Callback
`store_vehicle_add_area(display_name, index, ignored, ignored2, is_wheel_menu)`
(`:2373-2385`) — **a real tier-1 special case, not generic dispatch:** when the engine flags a
row `is_wheel_menu == true`, it is given a **static** `item.sub_menu = Store_vehicle_axle_menu`
(a 3-entry Lua table literal, `:1198-1203`) instead of the generic `on_sub_menu_fill =
store_vehicle_build_current_menu` every other area row gets — the "Wheels" area is hardcoded Lua
structure, not server/C-driven row-kind dispatch, the only area that works this way.
`store_vehicle_area_on_enter` (`:2387-2392`) resets the preview camera to
`vcust_set_camera_pos("Standard")` (`:2389`, already documented §27.5) on every area-tier visit.

**Tier 2 — "current menu," one dataresponder channel reused for every non-wheel area**,
`store_vehicle_build_current_menu` (`:2313-2323`):
`vint_dataresponder_request("vcust_populate_menu", "store_vehicle_populate_menu", 0,
STORE_VEHICLE_POPULATE_CURRENT_MENU)` (`:2316`, `=1`). Callback
`store_vehicle_populate_menu(display_name, cat_type, id, price, current, level, desc, respect,
palette_first)` (`:2237-2310`) dispatches on `cat_type` into **6 distinct row kinds**, each
wiring different `on_select`/`on_nav`/`on_sub_menu_fill` handlers onto the same megalist row
shape:
- `STORE_VEHICLE_TYPE_SLOT` (`=1`) — `on_nav = store_vehicle_nav_slot` (repositions the camera
  per-slot via `vcust_adjust_camera_angle(menu_data.id)`, `:198`, already documented §52.1),
  `on_sub_menu_fill = store_vehicle_build_menu` (tier 3, a literal vehicle part/attachment slot,
  e.g. a bumper/spoiler mount).
- `STORE_VEHICLE_TYPE_COMPONENT` (`=2`) — same tier-3 fill, no camera-adjust nav.
- `STORE_VEHICLE_TYPE_COLOR_REGION` (`=3`) — the row's `id` field doubles as a sentinel: `id ==
  STORE_VEHICLE_UNDERGLOW_COLOR` (literal `-2`) routes straight to `on_select =
  store_vehicle_populate_underglow_grid` (a dedicated grid, bypassing the generic
  region→palette→color chain entirely); every other color-region row gets `on_sub_menu_fill =
  store_vehicle_select_region` instead.
- `STORE_VEHICLE_TYPE_COLOR_PALETTE` (`=4`) — `palette_first` (an 8th callback argument) selects
  between `on_select = store_vehicle_populate_trim_palette_menu` (palette-as-menu, for rim/trim
  finishes) or `store_vehicle_populate_color_grid` (palette-as-grid, for body paint) — the same
  finish ever presented two different UI shapes depending on server data, not a fixed per-area
  rule.
- `STORE_VEHICLE_TYPE_PERFORMANCE` (`=7`) — a **leaf row, not a sub-menu**: `on_nav =
  store_vehicle_upgrade_nav`, and if the row's `level` already equals `item.max_level`
  (hardcoded `4`, `:2264`) it's shown owned/maxed with no `on_select` at all; otherwise
  `on_select = store_vehicle_purchase_upgrade`. Also sets
  `Store_vehicle_building_menu.performance_menu = true`, consumed later for a PC-specific
  double-column list-selection workaround (`store_vehicle_populate_list`, `:2839-2844`).
- `STORE_VEHICLE_TYPE_PERFORMANCE_SLOT` (`=8`) — a **container** row (`on_sub_menu_fill =
  store_vehicle_build_menu` again, `item.max_level` hardcoded `1`) — i.e. the performance tree is
  itself 2 real tiers deep: a slot (engine/suspension/armor/NOS-class category) containing a flat
  list of `TYPE_PERFORMANCE` level rows, structurally the same "levels" shape §6.2/§6.3
  documented for weapons, reused here for vehicle performance upgrades specifically.

**Tier 3 — the generic item-population entry point**, `store_vehicle_build_menu(menu_data)`
(`:670-718`): issues `vint_dataresponder_request("vcust_populate_menu",
"store_vehicle_populate_items_menu", 0, STORE_VEHICLE_POPULATE_COMPONENT_MENU,
Store_vehicle_show_slot)` (`:707`, `=2`) for a slot/component row, OR re-issues the tier-2
channel itself with `STORE_VEHICLE_POPULATE_COLOR_REGION` (`=4`, `:686`) when backing into a
color-region row's own sub-menu (a real "skip this menu if only 1 entry" collapse:
`#Store_vehicle_building_menu == 1` at `:689-701` jumps straight to the palette/color grid,
flagged via `Store_vehicle_region_menu_bypassed`, the same single-entry-tier-collapse shape §6.2/
§7.2 already found for weapons' thrown-category and clothing's single-category area). Callback
`store_vehicle_populate_items_menu(display_name, cat_type, id, price, current, level, desc,
respect)` (`:720-744`) — each row is a purchasable/equippable component, `on_select =
store_vehicle_select_component`, `on_nav = store_vehicle_nav_component`, `on_back =
store_vehicle_revert_component` (the generic try-before-buy revert hook, §8.4).

**Wheel sub-tree — 2 further real tiers, entirely separate from the generic channel above.**
Selecting an axle in `Store_vehicle_axle_menu` (`:1198-1203`) calls `store_vehicle_fill_wheel_menu`
(`:1106-1110`), which issues a **different** channel:
`vint_dataresponder_request("vcust_populate_wheel_menu", "store_vehicle_populate_wheel_menu", 0,
7, true, Store_vehicle_chosen_axle)` (`:1108`, numeric mode `7` = "`VCUST_INTF_POPULATE_WHEEL_
MENU`" per the file's own comment). The callback (`:1136-1189`) builds a **static local
sub-menu**, not server-driven rows: rim-style button → `store_vehicle_build_rim_menu` (opens a
dedicated 3rd-level color-grid screen, `:786-803`, loading the `VEHICLE_CUST_RIM_PEG` asset peg
first); tire-tread button → `store_vehicle_build_tire_menu` (`:879-923`, loads
`VEHICLE_CUST_TIRE_PEG`); a `TYPE_TOGGLE` rim-size row (5 fixed profile presets,
`Store_vehicle_rim_sizes`, `:112-120`); up to 2 `TYPE_SLIDER` rows (wheel size/width, only added
if the vehicle's own min≠max range allows it, `:1161-1174`); and the purchase button itself. The
rim-style grid is itself a further sub-tier: categories via `vcust_populate_wheel_grid_menu` mode
`9` (`:818`) plus a parallel spinner-category scan via mode `12` (`:824`), each category
populated via `store_vehicle_populate_rim_grid` (`:827-849`).

**Net menu depth for a real purchase:** area (1) → current-menu dispatch (2) →
component/slot items or color-region sub-menu (3) → [color-palette/color-grid or rim/tire grid]
(4, sometimes 5 for the rim-style grid specifically) → confirm popup → native call. Depth is
comparable to weapons' 4 tiers and clothing's 4, but **the number of structurally distinct
tier-2→tier-N shapes (6 row-kind dispatches plus a fully separate wheel sub-tree) is larger than
either sibling store** — clothing has exactly 1 area→category→item→detail shape plus one
separate outfit sub-tree (§7.2); weapons have exactly 1 "levels" shape (§6.2).

**Garage mode is structurally flat, not nested like customize mode.** `garage_build_areas()`
(`:2210-2226`) issues one `vint_dataresponder_request("garage_populate",
"garage_populate_car_list", 0)` (`:2214`) producing a single flat list of owned vehicles
(`garage_populate_car_list`, `:2190-2207`) — no area/category tiers at all. Selecting a vehicle
calls `garage_select` → `store_vehicle_garage_vehicle_select()` (`:239-326`), which opens a
**popup**, not a sub-menu: `Garage_select_choices` built dynamically per-vehicle from
`garage_get_repair_cost()`/`garage_can_customize()`/`garage_can_delete()` (all 3 already
documented, `spec-lua-api-behaviour.md` §23.10/§53.4) offering Retrieve (always) / Repair (if
cost > 0) / Customize (if allowed) / Delete (if allowed) / Cancel.

### 8.3 Purchase surface — 5 distinct cash-charging natives, more than either sibling store
(weapons: 1+ammo; clothing: 2), every one gated by the shared client-side cash check and confirm
popup from §4

1. **Component/attachment purchase** (bumpers, spoilers, body-kit pieces, any
   `STORE_VEHICLE_TYPE_SLOT`/`COMPONENT` leaf). `store_vehicle_select_component(menu_data)`
   (`:329-363`) → client cash gate (`Store_common_player_cash >= menu_data.price`, `:331`) →
   confirm popup → `store_vehicle_component_confirm`/`_confirm2` (`:557-624`, with an extra
   NOS/hydraulics performance-warning sub-popup gated by `vcust_show_nos_hydraulic_warning()`,
   `:575`, already documented §52.1) → `store_vehicle_component_confirm_final` (`:626-638`) →
   **`vcust_purchase_component()`** (`:628`, no arguments — already documented §52.1 as the
   stale `-1` selection-index out-of-bounds bug, re-examined below). `game_award_respect
   (Store_vehicle_component_selected.style)` fires at `:611`, before the native call, only when
   `price ~= 0`.
2. **Wheels (rim + tire + size/width, charged as ONE combined purchase).**
   `store_vehicle_purchase_wheels(menu_data)` (`:1019-1057`) sums 3 independent sub-prices via
   `store_vehicle_update_total_price()` (`:993-1017`: rim + tire + size, size itself shared
   between the rim-size toggle and both sliders) → cash gate → confirm →
   `store_vehicle_purchase_wheels_final` (`:1060-1099`) → `game_award_respect(respect)`
   (`:1086`) then **`vcust_purchase_wheels(price)`** (`:1087`, already documented §38.5 — no
   affordability check in the native itself, commits the preview regardless of charge success).
   **Free items skip the popup entirely**: `if price == 0 then vcust_purchase_wheels(price)
   return end` (`:1025-1027`) — a direct, un-confirmed native call exactly like weapons'
   price-forced-to-0 cache-equip path (§6.3 item 3).
3. **Body paint / trim finish.** Reached either via the color grid
   (`store_vehicle_color_selected`, `:1462-1493`, popup → `store_vehicle_purchase_color_final`)
   or via the palette-as-menu path for rim/trim finishes (`store_vehicle_trim_palette_selected`,
   same shape) — both converge on `store_vehicle_purchase_color_final(event)` (`:1275-1363`):
   `game_award_respect(Store_vehicle_color_respect)` (`:1332`) then
   **`vcust_purchase_paint(Store_vehicle_color_price)`** (`:1333`, already documented §52.1 —
   "no price validation whatsoever, a negative price credits the player"). A **single-palette-
   choice collapse** exists here too: `store_vehicle_populate_palette_grid` (`:1378-1432`) skips
   the palette grid and goes straight to the confirm popup when `Store_vehicle_multiple_palettes
   == false` (`:1386-1416`).
4. **Underglow color — a structurally separate, simpler purchase, not routed through the generic
   palette/region chain at all.** `store_vehicle_underglow_color_selected` (`:1867-1919`) →
   confirm → `store_vehicle_underglow_purchase_color_final` (`:1939-1999`):
   `game_award_respect(Store_vehicle_color_respect)` (`:1977`) then
   **`vcust_set_underglow(index - 1, Store_vehicle_color_price)`** (`:1981` — a genuinely
   different native from the other 4, already documented §52.1: "applies its colour change
   before charging... no purchase callback or telemetry at all, unlike its three siblings"). This
   file's own call site confirms that write-up's "applies before charging" framing: the color is
   already selected/previewed via the grid nav before this function ever runs.
5. **Performance upgrade (engine/suspension/armor/NOS-class levels).**
   `store_vehicle_purchase_upgrade(menu_data)` (`:2004-2027`) → cash gate → confirm popup (text
   is a generic `"STORE_TEXT_CONFIRM_PURCHASE_BLANK"`, no item-name interpolation unlike every
   other purchase type here) → `store_vehicle_purchase_upgrade_final` (`:2045-2086`):
   `game_award_respect(Store_vehicle_purchase_item.style)` (`:2069`) then
   **`vcust_purchase_performance_upgrade(Store_vehicle_purchase_item.id)`** (`:2070`, already
   documented §52.1 as "the **only** member of the family that validates its result before
   charging anything" — the one purchase native in this whole store with a real server-side
   guard).
6. **Garage repair — a 6th cash-charging call site, but not a `vcust_*` native and not gated by
   the shared §4 pattern the same way.** `garage_repair_vehicle()` (already documented
   `spec-lua-api-behaviour.md` §41.7 — "the player is charged before the repair actually runs,
   with no visible affordability check in this function") is called from
   `store_garage_customize_repair_confirm` (`:414`) and `store_garage_select`'s own
   `GARAGE_SELECT_REPAIR` branch (`:483`) — this file's own client-side cash check does exist
   (`Store_common_player_cash < garage_get_repair_cost()`, `:400`/`:482`) before either call
   site, so the shared §4 "UI gate is cosmetic, native doesn't check" framing applies here too,
   just via a differently-named native family (`garage_*`, not `vcust_*`).

**`vcust_revert_wheels`** (`store_vehicle.lua:1142,1145,1149,1164,1172,1176,1198-1203`) — the
`on_cancel` hook for every row of the wheel sub-menu (rim-style, tire-tread, rim-size toggle,
both sliders, the purchase button itself, and all 3 axle rows) — **grepped
`spec-lua-api-behaviour.md`, zero hits — new, previously-undocumented native lead.** HIGH
CONFIDENCE it mirrors `vcust_revert_color`'s own shape (§19.10: restore a stored "original" value,
gated on a byte flag) given the identical `on_cancel`-hook role, but not independently traced to
its own C body this pass.

**No undocumented purchase-charging natives found** beyond the one new preview-revert lead above
— all 5 `vcust_*` cash-charging calls plus `garage_repair_vehicle` were already individually
specced in `spec-lua-api-behaviour.md` before this pass (§38.4/§38.5/§41.7/§52.1), a contrast
with clothing's 2 new leads (§7.3) and weapons' 0 new leads (§6.3) — this store's purchase-native
surface, despite being the largest of the three, happened to be fully pre-covered by prior
tranches.

### 8.4 Preview mechanics — CONFIRMED the richest of the three: live try-before-buy on 4 of 5
purchase types, full generic camera/rotate framework, PLUS per-feature camera repositioning
weapons/clothing have no equivalent of

**Try-before-buy, with automatic revert, is real and wired per purchase type** (contrast with
clothing's single "preview-only, folded into price" framing, §7.3 — here it's a genuine
multi-step live-edit-then-commit-or-cancel flow):
- Component: `on_nav = store_vehicle_nav_component` calls `vcust_preview_component(0, item.id)`
  (`:209`, already documented §19.11) on every row highlight — updating price/respect display
  live; `on_back = store_vehicle_revert_component` (`:233-236`) re-previews the originally-
  equipped item and clears the header if the player backs out without buying.
- Wheels: every rim/tire/size/width row's own nav/update callback calls one of
  `vcust_preview_component(2, 1, idx-1)` (tire, `:963`), `vcust_preview_rim_sizing(menu_data.
  current_value - 1)` (`:1116`, already documented §52.1 — null-dereference hazard with no live
  target), or `vcust_preview_wheel_sizing(size, width)` (`:1123`/`:1130`, already documented
  §38.5 — unconditional null dereference with no live target at all) — live, per-slider-drag
  preview; cancel/back calls the new-lead `vcust_revert_wheels` on every row.
- Paint/trim: `vcust_preview_color(index - 1)` (`:1738`/`:2925`, already documented §38.5) and
  `vcust_preview_palette(0, item.id, selection-1)` (`:2911`/`:1640`, already documented §52.1)
  fire on every grid-nav/menu-nav tick; `vcust_revert_color()` (already documented §19.10) fires
  on every color/palette-tier cancel (`:1255`, `:1479`, `:1792`, `:1947`, `:1962`).
- Underglow: no distinct preview native found — `store_vehicle_underglow_color_grid_nav`
  (`:1863-1865`) only calls the generic `store_vehicle_grid_nav` cursor-move helper, with no
  `vcust_preview_*`/`vcust_revert_*` call anywhere in the underglow functions (grepped).
  Underglow's own cancel path (`store_vehicle_underglow_purchase_color_final`'s `event == "back"`
  branch, `:1946-1948`) calls the generic `vcust_revert_color()` instead — confirmed reuse of the
  paint-revert native for a feature with no distinct preview call of its own, a real asymmetry
  worth flagging for a reimplementation (underglow previews are not independently revertible
  from paint's).
- Performance upgrades: **no preview/revert mechanism found at all** — `store_vehicle_upgrade_nav`
  (`:2105-2118`) only updates the price/respect/description text display, never calls any
  `vcust_preview_*`/`vcust_revert_*` native (grepped, zero hits) — consistent with this being a
  numeric stat upgrade with no visual model change to preview, the one purchase type in this
  store that behaves like weapons' own non-visual upgrade-level purchase (§6.3).

**The full generic `store_common` camera/mouse-rotate framework (§3) is confirmed in use, same
as clothing (§7.4), in direct contrast to weapons' confirmed opt-out (§6.4):**
`character_enable_mouse_drag(true)` is called once at init (`:2558`) and `(false)` at cleanup
(`:2625`) — the same primitive §7.4 found gates `character_mouse_drag`/
`store_common_rotate_mouse_drag` (§3's still-OPEN rotation native).
`store_vehicle_mouse_scroll`/`_drag`/`_drag_release` (`:3327-3348`) are, exactly like clothing's
own same-named set (§7.4), **scoped only to the color grid's own scrollbar widget** (checked
against `Store_common_color_grid.scrollbar.tab.handle`), not the character/vehicle-rotate
mechanism — a reimplementation must not conflate the two, same caution §7.4 already raised for
clothing.

**Beyond the generic framework, this store has its own per-feature camera repositioning weapons/
clothing have no equivalent of:** `vcust_set_camera_pos("Standard")` resets to a default view on
every area-tier entry (`store_vehicle_area_on_enter`, `:2389`); the axle-select menu's own 3
static rows each carry a `vcamera` field (`"Both_axle"`/`"Front_axle"`/`"Rear_axle"`,
`:1200-1202`) consumed by the generic `store_common_button_a`'s `vcust_set_camera_pos
(Menu_data[current_index].vcamera, true)` call (§3); and `vcust_adjust_camera_angle(menu_data.id)`
(`:198`, already documented §52.1 — a real null-dereference crash on a failed
customization-vehicle resolve) repositions the camera per-slot on every
`STORE_VEHICLE_TYPE_SLOT` row highlight. No equivalent per-row camera-angle native exists in
either `store_weapon.lua` or `store_clothing.lua`.

### 8.5 Exit convention — a REAL, confirmed deviation from §5/§6.5/§7.5: `pop_screen()` is never
called anywhere in this file

**Grepped the entire file for `pop_screen`: zero hits.** This is a genuine structural difference
from both weapons (§6.5) and clothing (§7.5), which both call exactly one `pop_screen()` for
their own document on a confirmed exit. `store_vehicle.lua` instead ends every exit path in one
of two ways, both of which hand the actual stack-pop off elsewhere:

- **To a native that pops the screen stack internally.** `store_vehicle_exit_final`
  (`:2632-2673`, the walk-in/garage-mode "really leave" confirm handler) calls
  `store_vehicle_retrieve_car_and_exit()` (`:2672` — already documented
  `spec-lua-api-behaviour.md` §38.4: "pops the top of the UI screen stack (and a second time if a
  specific screen id is also on the stack)"). `store_vehicle_crib_exit()` (`:171-173`, the
  in-crib tween end-event) calls `store_vehicle_do_return_to_crib()` (already documented §49.1:
  "screen-stack pop with a confirmed-dead secondary branch"). Both are native-side pops, invisible
  from the Lua call graph alone.
- **To `vint_document_unload()` directly, bypassing `pop_screen()` entirely.**
  `store_vehicle_play_exit_anim()` (`:3225-3241`) and the separately-reachable
  `store_vehicle_exit_begin_bg_clear()` (`:3428-3443`, confirmed live via its tween
  `end_event_twn:set_end_event("store_unload_doc")`, `:3437`) both end in `store_unload_doc()`
  (`:3243-3247`) → **`vint_document_unload()`** — a different primitive from `pop_screen()`,
  called with no visible Lua caller for `store_vehicle_play_exit_anim` itself (grepped — only its
  own definition; it is reached either as a timeline-driven animation-event callback outside the
  Lua call graph, or is genuinely dead code; not resolved further this pass, OPEN).

**The mode-switch-instead-of-exit deviation, keyed off `Store_vehicle_begin_from_garage` (cached
at init, NOT the live `Store_vehicle_is_garage`) — this store's equivalent of clothing's
"wardrobe-mode back doesn't pop" finding (§7.5), but on the garage/customize axis instead:**
- `store_vehicle_exit_final`: "if we began in garage mode, are now in customize mode, and the
  player isn't leaving with a retrieved car (`Garage_leave_with_car ~= true`), don't exit at all
  — just `store_vehicle_switch_mode()` back to garage" (`:2660-2668`).
- `store_vehicle_no_vehicle_exit_final` (the garage-mode exit-confirm handler, `:2675-2726`): the
  mirror case — "if we began in customize mode and are now in garage mode, call
  `garage_back_to_customize()` instead of exiting" (`:2703-2719`), itself a native, already
  documented §53.4.
- **Crib exit is unconditional and dialog-free, same shape as weapons/clothing's own crib paths
  (§6.5/§7.5):** `store_vehicle_exit(menu_data, leave_with_car)` (`:2732-2789`) checks
  `Store_vehicle_loaded_from_crib` first (`:2739-2749`) — if in crib and still in garage mode, it
  calls `store_vehicle_return_to_crib()` (`:175-190`, plays an anim ending in
  `store_vehicle_crib_exit`) with no confirm popup at all; if in crib but switched to customize
  mode, it calls `store_vehicle_switch_mode()` back to garage instead (mirroring the
  toggle-not-exit deviation above) — only a non-crib, non-mode-mismatched exit ever shows the
  shared `Yes_no_choices` confirm popup (`:2764-2788`).

**Net: this store genuinely does not follow §5's "exactly one `pop_screen()`" convention at all**
— every real exit is either a native-internal pop (hidden from Lua) or a direct
`vint_document_unload()` call, and the mode-toggle-instead-of-exit deviation (keyed off the
*cached starting mode*, not the live mode) adds a third branch neither weapon's simple crib/
walk-in split nor clothing's single wardrobe-toggle flag has an equivalent of. The shared
`store_common_gained_focus()` automatic cascade (§5) is unaffected either way — it fires on
whatever document becomes top-of-stack after either the native pop or the direct unload, same as
always.

**On the stale `-1` selection-index bug (§4's `vcust_purchase_component` finding) — this pass's
own contribution is negative, not a new trigger.** The index `vcust_purchase_component` resets is
reset *inside the native itself* on every call (per the existing §52.1 text), not by anything in
this file's entry/exit flow — `store_vehicle.lua` only ever sets a *fresh* index via
`vcust_preview_component(0, id)` mode-0 calls (`store_vehicle_nav_component:209`,
`store_vehicle_build_menu:710`), and every reachable Lua path into
`store_vehicle_component_confirm_final` (`:626-638`) is preceded by one of those two calls in the
same UI session. No double-invoke path, and no interaction between this file's crib/garage/
mode-toggle exit logic and the native's own internal state, was found.

### 8.6 Net comparison with `store_weapon.lua`/`store_clothing.lua`

Vehicle customization is a **strict superset of both siblings in every dimension measured so
far**: richest entry context (3 overlapping axes vs. weapons' clean binary split and clothing's
single toggle), richest purchase surface (5 distinct `vcust_*` cash-charging natives plus a 6th
`garage_*` one, vs. weapons' 1+ammo and clothing's 2), richest preview mechanics (full generic
camera/rotate framework PLUS 3 kinds of per-feature camera repositioning no sibling has, live
try-before-buy on 4 of 5 purchase types with per-type revert natives), and it is the only one of
the three with a confirmed, file-wide deviation from the shared `pop_screen()` exit convention
(§5/§6.5/§7.5) — every exit here is either a native-internal pop or a direct
`vint_document_unload()` call. Despite this breadth, its *native* surface turned out to be the
most thoroughly pre-documented of the three: only one new lead (`vcust_revert_wheels`) emerged,
versus clothing's two (§7.3) — the structural/UI-flow findings in this section are new, but the
underlying C-side natives were almost all already specced by prior ranking-tranche passes
(§38.4/§38.5/§52.1).

## 9. `store_tattoo.lua` — full menu-depth trace: the simplest entry context of the four traced stores, a non-dataresponder item-list population mechanism, a single purchase native, and a confirmed menu-depth pop-count bug on the opacity-slider path

**Scope.** This section documents `store_tattoo.lua` to the same document-level depth as §6/§7/§8's worked examples, building on §1-§5's generic `store_common` framework (not re-derived here). The purchase-confirm chain `store_tattoo_item_purchase_final` → `pcr_purchase_tattoo` (`:170-212`), already summarized in §4 ("only charges cash... no sign check, so a negative price pays the player instead of charging them"), is incorporated below rather than re-derived.

### 9.1 Entry context — no crib/cache split, no mode toggle: a single, context-free document

Unlike weapon (§6.1, crib-vs-walk-in binary split), clothing (§7.1, live store↔wardrobe toggle), and vehicle (§8.1, three overlapping axes), `store_tattoo.lua` has **no crib detection at all** — grepping the whole file for `crib` returns zero hits, and `store_common_crib_is_loaded()` is never called. `store_tattoo_init()` (`:515-625`) sets `Store_common_populate_list_cb = store_tattoo_populate_list` and `Store_common_exit_cb = store_tattoo_exit` (`:523-524`, the same per-store hook-wiring shape §1 documents), locks controls, sets up the color-grid/popup/hint-bar widgets, and plays a fixed set of background animations (`tattoo_bg_anim_in`/`tattoo_bg_anim`/`skull_anim`/`rustys_anim`/`needle_anim`, `:584-607`) with audio event `"enter_rustys_needle"` (`:609`) — there is exactly one entry context, one set of colors (`COLOR_PLAYER_CREATION_PRIMARY/SECONDARY/TERTIARY`, `:576-579` — the same palette the character-creator PCR screens use, consistent with tattoos being a PCR-native-backed feature per §16.7's category table), and no cosmetic sub-brand skin selection (contrast clothing's 6 skins, §7.1). This is the simplest of the four traced stores on this axis.

`character_enable_mouse_drag(true)` is called once at init (`:624`) and `(false)` at cleanup (`:649`) — confirms the full generic rotate-preview framework is wired (§9.4), matching clothing/vehicle, in contrast to weapons' confirmed opt-out (§6.4).

### 9.2 Menu structure — 3 real tiers (main menu → static zone list → server-populated item list), plus an optional color-grid and/or opacity-slider sub-step; the item/color tiers use a DIFFERENT population mechanism than every sibling store

`Current_tier` only declares 2 named values, `MENU_TIER=1`/`COLOR_TIER=2` (`:16-18`), used purely to disambiguate mouse-click routing between the megalist and the color grid (`store_tattoo_mouse_click`, `:806-822`) — exactly the same "tier integer exists for mouse-click disambiguation, not depth tracking" shape §7.2 found for clothing. Real menu depth is tracked by `store_common`'s generic push/pop sub-menu stack (§2/§5).

**Tier 1 — main menu.** A static, hardcoded 5-row table, `Store_tattoo_main_menu` (`:502-510`), carrying `on_enter = store_tattoo_return_main` (`:503`, resets the preview camera to `store_set_camera_pos("body")` every time this tier is (re-)entered, `:444-446`): 4 body-area rows (`CUST_TATTOO_HEAD`/`UPPER_BODY`/`ARMS`/`LEGS`, each with a static `sub_menu` field pointing at its own zone-group table) plus `STORE_EXIT` (`on_select = store_tattoo_exit`). Populated via `store_tattoo_populate_list(Menu_data, 1)` (`:568`, called directly from init — no dataresponder round-trip at this tier, same as weapons' hardcoded tier 1, §6.2).

**Tier 2 — zone list, also static Lua data, NOT dataresponder-populated.** Each of the 4 area rows' `sub_menu` is itself a static Lua table literal: `Store_tattoo_head` (2 rows: head top, neck, `:456-459`), `Store_tattoo_upper_body` (6 rows: entire/upper/lower back, entire/upper/lower chest, `:461-475`), `Store_tattoo_arms` (8 rows: entire/upper/lower arm + hand, both sides, `:477-495`), `Store_tattoo_legs` (2 rows: leg right, leg left, `:497-500`) — 18 zone rows total. Each carries `on_sub_menu_fill = store_tattoo_menu_build`, a `tattoo_area` string (the exact PCR category name, e.g. `"tattoo upper back"`, matched case-insensitively against `pcr_change_composite`'s own 34-entry category table, §16.7), and a `camera` string (e.g. `"upper back"`) consumed by the generic `store_common_button_a` sub-menu-open camera-reposition mechanism (§3) — confirmed, not re-derived, since weapons never populate this field at all (§6.4) while vehicle/clothing do.

Several rows in the upper-body and arms groups also carry a `replace` field (e.g. `"tattoo entire back"` lists `replace = {"tattoo upper back", "tattoo lower back"}`, `:462-467`) that looks like it should clear overlapping zones when one is chosen — **but this is dead/vestigial data**: `store_tattoo_menu_build`'s own `menu_data.replace` handling block is entirely commented out (`:349-358`), with an inline developer comment explaining why: *"SEH 6/22/2011 - FrankM said we shouldn't replace other tattoo slots; they should be allowed to overlap."* Tattoos in different nominally-overlapping zones (e.g. "entire back" and "upper back") can coexist; the `replace` table on each row is inert leftover data from a reversed design decision, not a bug to reproduce functionally — a reimplementation should treat overlapping zone tattoos as fully independent, matching the live game's actual (reversed) behavior, not the dead `replace` field's apparent intent.

**Tier 3 — item list, a genuinely different population mechanism from every sibling store.** Selecting a zone row calls `store_tattoo_menu_build(menu_data)` (`:331-382`): first snapshots the zone's current composite state for revert purposes via `pcr_get_composite_info(area)` (`:345`, already documented §23.7) into `Store_tattoo_reverted`, resets the color-grid state, inserts a synthetic `"CUST_ITEM_NOTHING"` row (`tattoo_item = -1`, the "remove this tattoo" option, `:372-378` — the exact same synthetic-first-row shape §7.2 found for clothing's `"HAIR_CAT_NONE"`), then calls **`pcr_report_composite_colors(area, true, "store_tattoo_add_color")`** (`:366`) and **`pcr_report_composite_items(area, "store_tattoo_item_add")`** (`:380`) — both already-documented natives (§23.3/§19.5) that drive their callback directly, by string name, with NO `vint_dataresponder_request` wrapper anywhere in this file. **Grepped the entire file for `vint_dataresponder_request`: zero hits** — every other traced store (weapon §6.2, clothing §7.2, vehicle §8.2) issues at least its lower tiers through that generic one-shot-request primitive; tattoo's item/color population instead goes straight through the `pcr_report_*` callback-string convention, a structurally distinct list-population mechanism from the shared framework §2 describes as the norm. Row callback signature: `store_tattoo_item_add(tattoo_index, name_crc, name_str, current, colorized, alpha_select, price, respect)` (`:301-329`) — each row carries `price`/`respect` (nilled out if 0, `:307-313`), `colorized`/`alpha_select` booleans that drive the optional sub-steps below, `on_nav = store_tattoo_item_nav`, `on_select = store_tattoo_item_set`, `on_cancel = store_tattoo_item_revert`.

**Optional sub-step A — color grid (display-mode swap, NOT a stack push).** If the selected item's `colorized` flag is set, `store_tattoo_item_set` (`:246-261`) calls `store_tattoo_color_grid_show` (`:76-90`): hides `Active_list`, shows a 9×7 `Store_common_color_grid` populated from the `pcr_report_composite_colors` callback (`store_tattoo_add_color(color_index, label_crc, label_str, red, green, blue, max_colors, is_current)`, `:107-122`), sets `Current_tier = COLOR_TIER` — this is purely a display-mode toggle (no `store_common_push_menu` call), confirmed by reading the function in full.

**Optional sub-step B — opacity slider + purchase button, a REAL stack push.** If `alpha_select` is set (checked either directly from the item row, or after a color is picked, `store_tattoo_color_select:158`), `store_tattoo_composite_show_alpha_menu` (`:63-74`) builds a 2-row menu (a `TYPE_SLIDER` opacity control, `on_value_update = store_tattoo_composite_alpha_change`; a `"STORE_PURCHASE_ITEM"` button, `on_select = store_tattoo_composite_alpha_done`) and calls **`store_common_push_menu(New_menu_data)`** (`:73`) — a genuine push onto the shared sub-menu stack, unlike the color grid above. This distinction (color grid = display swap; opacity menu = real stack push) is the root cause of §9.3's confirmed pop-count finding.

**Net menu depth for a real purchase:** main menu (1, static) → zone list (2, static) → item list (3, `pcr_report_*`-populated) → [color grid, display-only] → [opacity menu, real push] → confirm popup → native call. Structurally the shallowest of the four stores (3 real tiers vs. weapons'/clothing's/vehicle's 4), and the only one whose lower tiers skip `vint_dataresponder_request` entirely.

### 9.3 Purchase surface — exactly ONE cash-charging native in the whole file; a confirmed menu-depth pop-count inconsistency on the opacity-slider path

**Exactly one purchase native, the simplest surface of the four traced stores.** Every reachable path — plain item, color-only, opacity-only, color+opacity — converges on `store_tattoo_purchase()` (`:124-143`, client-side cash gate `Store_tattoo_current_price > Store_common_player_cash` → insufficient-funds popup, else the shared `Yes_no_choices` confirm) → `store_tattoo_item_purchase_final` (`:170-212`) on Yes: `game_award_respect(Store_tattoo_current_respect, nil, nil, true)` (`:194`) then **`pcr_purchase_tattoo(Store_tattoo_current_price)`** (`:198`) — native `0x008333a0`, already fully documented §43.3 (cash-only charge, no sign check). Compare weapons' 1+ammo (§6.3), clothing's 2 (§7.3), vehicle's 5+1 (§8.3) — tattoo has no equivalent of a separate color/variant-priced add-on charge: color and opacity selection never touch `Store_tattoo_current_price`, confirming (same as clothing §7.3's finding) that color/variant choice is folded entirely into the single item price already charged.

**Confirmed, code-level menu-depth pop-count bug: the purchase-confirm handler's hardcoded double-pop under-pops by one level whenever the opacity-slider sub-step was used.** `store_tattoo_item_purchase_final` unconditionally calls `store_common_back_menu()` twice (`:206-207`) after every successful purchase, with no branch on which sub-path was taken. Tracing real stack depth at the moment of purchase:
- **Plain item, or colorized-only (no opacity slider):** two real stack pushes occurred to reach the item list (main-menu→zone-list via the static `sub_menu` field, zone-list→item-list via `on_sub_menu_fill`); the color grid itself is a display swap, not a push (§9.2). Depth at purchase = 2. Two pops correctly return to depth 0 — the main menu. **Consistent.**
- **Opacity-slider used (`alpha_select`, with or without `colorized`):** `store_tattoo_composite_show_alpha_menu`'s own `store_common_push_menu` call (`:73`) adds a THIRD real push on top of the same two above. Depth at purchase = 3. Two pops return only to depth 1 — **the zone list, not the main menu.**

This means: buying a tattoo that uses the opacity slider leaves the player one menu level deeper than buying any other kind of tattoo (sitting on the zone list, e.g. "Upper Body," instead of back at the top-level area menu) — a real, confirmed-from-source UI state inconsistency a from-scratch reimplementation should either reproduce faithfully or consciously decide to fix. This is a code-logic finding (traced the exact push/pop call sites), not independently exercised at runtime this pass.

**No new undocumented natives.** All `pcr_*` calls in the file — `pcr_change_composite` (§16.7), `pcr_purchase_tattoo` (§43.3), `pcr_get_composite_info` (§23.7), `pcr_report_composite_colors` (§23.3), `pcr_report_composite_items` (§19.5) — were already individually documented in `spec-lua-api-behaviour.md` prior to this pass. `pcr_change_hair`/`pcr_get_hair_info` (§16.6/§23.6) also appear (`:622`/`:766`/`:782`) but inside two functions, `store_tattoo_hair_save_current`/`store_character_hair_revert_to_saved` (`:762-785`), that are **dead code in this file**: both are defined but never called anywhere except one commented-out call site (`--store_tattoo_hair_save_current()`, `:621`), consistent with the adjacent dev comment *"not hiding hair now, but we may want to add this back."* No removal-of-a-specific-tattoo / placement-size-variant purchase mechanic beyond price/color/opacity was found.

### 9.4 Preview mechanics — CONFIRMED live try-before-buy, more aggressive than clothing's poll-thread: navigating onto an item APPLIES it to the live character model immediately (debounced), not just a camera/visual preview

Tattoo preview is not a separate cosmetic-only display — `pcr_change_composite` (the same native that performs the real, permanent application) is called directly from navigation, before any purchase confirmation:
- **Item-list navigation** — `store_tattoo_item_nav(menu_data)` (`:285-299`) kills any pending debounce thread, updates the price/respect header, then starts a fresh one: `Store_tattoo_nav_thread = thread_new("store_tattoo_item_delayed_set")`. `store_tattoo_item_delayed_set` (`:279-283`) waits `delay(0.4)` then calls **`pcr_change_composite(Store_tattoo_current_area, Store_tattoo_current_item, Store_tattoo_current_color)`** — i.e. highlighting a different tattoo design for ~0.4s with no further input live-applies it to the character model.
- **Color-grid navigation** — `store_tattoo_color_grid_nav` (`:92-105`) does the identical kill-and-restart-debounce-thread pattern.
- **Opacity-slider drag** — `store_tattoo_composite_alpha_change` (`:57-61`, the slider's `on_value_update`) calls `pcr_change_composite(..., Store_tattoo_current_alpha)` on every frame of drag, with NO debounce at all — opacity previews instantaneously.
- **Revert on cancel** — `store_tattoo_item_revert()` (`:263-277`), wired as `on_cancel` on every item-list row, kills any pending debounce thread then replays `pcr_change_composite` for every entry in `Store_tattoo_reverted` (the zone's pre-edit snapshot captured by `pcr_get_composite_info` at tier-3 entry, §9.2) — a real restore-to-original, not just a header-text reset. `store_tattoo_cleanup()` (`:640-654`) also calls `store_tattoo_item_revert()` unconditionally on document unload (`:643`) — a belt-and-suspenders rollback matching clothing's own `pcu_clear_preview_slot()` cleanup call (§7.5).

**The full generic `store_common` camera/mouse-rotate framework (§3) is confirmed in use, matching clothing (§7.4)/vehicle (§8.4), contrasting weapons' opt-out (§6.4):** `character_enable_mouse_drag(true)`/`(false)` at init/cleanup (§9.1); `store_set_camera_pos("body")` fired on every main-menu (re-)entry (`store_tattoo_return_main`, `:444-446`) and once at the very first frame (`store_tattoo_set_start_camera`, `:448-454`, also calls `store_start_lighting_change()` — a lighting-rig adjustment call no sibling store's own init sequence was found to make); per-zone `camera` fields consumed by the generic sub-menu-open reposition call (§9.2); the gamepad rotate hint (`CTRL_BUTTON_RS`/`"STORE_ZOOM_ROTATE"`) shown on console, explicitly hidden on PC (`:541-549`, `:722-728` — identical shape to clothing/vehicle, PC uses mouse-drag instead).

One file-local quirk not seen named this way in the other three stores: `store_tattoo_mouse_move` (`:824-849`, the hint-bar/list mouse-hover handler) unconditionally calls **`store_common_mouse_zoom(0.0)`** (`:827`, already documented §11.9/§11.10) on every mouse-move tick over the UI chrome — i.e. hovering over the hint bar or list resets the preview camera's zoom level to 0 as a side effect, not an explicit player zoom action. Not independently confirmed against the other stores' own mouse-move handlers this pass (out of scope), flagged only as an observed-in-this-file behavior.

### 9.5 Exit convention — matches the shared §5 pattern exactly; no crib variant exists (consistent with §9.1's "no crib context at all")

`store_tattoo_exit(menu_data)` (`:424-439`) — called either as `Store_common_exit_cb` (back out of the top-level menu) or directly from the `STORE_EXIT` row's `on_select` (`:509`) — always shows the shared `Yes_no_choices` confirm popup (`"MENU_TITLE_WARNING"`/`"STORE_EXIT_WARNING_BODY"`), unconditionally (no crib bypass, unlike weapon/clothing/vehicle's own crib-skips-the-dialog branches, because no crib entry context exists here at all, §9.1). On confirm, `store_tattoo_exit_final(event)` (`:388-422`) plays an exit audio event (`"UI_Store_Exit"`), locks controls, plays `tattoo_bg_anim_out`, and calls **exactly one `pop_screen()`** (`:420`) — identical to §5's shared "one explicit pop + automatic `store_common_gained_focus()` cascade" convention, with no deviation of the kind §7.5 (wardrobe mode) or §8.5 (mode-toggle-instead-of-exit, `pop_screen()` never called) found for its siblings. Tattoo is the only one of the four traced stores with a completely uniform, single-branch exit path.

### 9.6 Net comparison with `store_weapon.lua`/`store_clothing.lua`/`store_vehicle.lua`

Tattoo is the **simplest of the four traced stores on every structural axis measured**: no crib/cache split (weapon has one, §6.1), no live mode toggle (clothing's wardrobe toggle §7.1, vehicle's garage/customize toggle §8.1), exactly one purchase native (vs. weapon's 1+ammo, clothing's 2, vehicle's 5+1), and the single most uniform exit convention (one confirm popup, one `pop_screen()`, no branch at all — vehicle by contrast never calls `pop_screen()` anywhere, §8.5). It is nonetheless NOT a strict subset in every dimension: its item/color-list population mechanism (`pcr_report_*` callback-string natives, zero `vint_dataresponder_request` calls) is structurally distinct from all three siblings' shared dataresponder-channel convention (§2), and its live try-before-buy preview (direct per-nav `pcr_change_composite` calls with a 0.4s debounce thread, reverted via a `pcr_get_composite_info` snapshot) is a different mechanism from both clothing's async 0.2s poll-thread (§7.4) and vehicle's per-feature `vcust_preview_*`/`vcust_revert_*` native pairs (§8.4), despite producing a similar player-facing "preview before you buy" effect. The one genuinely new defect-shaped finding is local to this file: the purchase-confirm handler's hardcoded two-pop exit sequence (`:206-207`) silently under-pops by one level whenever the opacity-slider sub-step was used, a real (if cosmetic/non-crashing) menu-depth inconsistency with no counterpart error in weapon/clothing/vehicle's own confirm handlers.

## 10. `store_crib.lua` — full menu-depth trace: a pure navigation-hub document with zero purchase natives of its own, a single flat gate-evaluated tier, and the file that actually puts `store_crib` on the stack beneath weapon/clothing/vehicle

**Scope.** This section documents `store_crib.lua` to the same document-level depth as §6-§9's worked examples, building on §1-§5's generic `store_common` framework (not re-derived here).

### 10.1 Entry context — no crib/cache split and no live mode toggle, because this document IS the crib context every sibling store detects; a single, always-identical entry

`store_crib_init()` (`:109-206`) resolves `Store_crib_doc_handle = vint_document_find("store_crib")` (`:120`) — this is literally the document whose presence `store_common_crib_is_loaded()` tests for (`vint_document_find("store_crib") ~= 0`, `store_common.lua:821-823`, already documented §6.1/§7.1/§8.1). Sets the same per-store hook-wiring shape §1 describes: `Store_common_populate_list_cb = store_crib_populate_list`, `Store_common_exit_cb = store_crib_exit` (`:128-129`). Unloads the full-screen map (`pause_map_dump()`, `:116`), sets background type `BG_TYPE_STRONGHOLD` (`bg_saints_set_type`, `:134`), colors the shared header `COLOR_STORE_REWARDS_PRIMARY/SECONDARY/TERTIARY` (`:173` — the same palette weapon's crib branch, §6.1, uses), and posts audio event `"Enter_Crib"` (`:198`). `character_enable_mouse_drag(false)` is called once (`:205`) and never set true anywhere in the file (confirmed by grep) — no rotate-preview framework is ever engaged (see §10.4).

**This file's own push logic is the mechanism that keeps `store_crib` on the document stack beneath a sub-store, confirming §6.1/§7.1/§8.1's cross-reference from the other side.** Selecting a top-menu row calls `store_crib_open_screen(menu_data)` (`:505-527`): it does **not** call `pop_screen()` for `store_crib` itself — it locks input (`store_lock_controls()`, `:512`), plays a slide-out animation (`store_crib_back_out_anim`, `:353-370`), and only on that animation's tween-end-event does `store_crib_back_out_anim_complete` (`:375-402`) call `push_screen(Store_crib_screen_to_push)` (`:397`, falling back to `pop_screen()` only if the push fails, `:399-401`) — pushing `"store_weapon"`/`"store_clothing"`/`"store_gang"`/`"store_stronghold"` **on top of** the still-resident `store_crib` (and `store_common`) documents. `store_crib_open_garage(menu_data)` (`:537-548`) takes a parallel path for vehicles: it sets `Transition_to_garage_type = menu_data.garage_type` (`:544`), plays the same back-out animation, and on completion calls **`store_crib_init_crib_garage(Transition_to_garage_type)`** (`:386`) instead of a plain `push_screen` — a native, never defined in any extracted Lua file (confirmed by grep across all `scratchpad/ui_lua_dumps/*.lua`), already documented in `spec-lua-api-behaviour.md` §49.1 (line 15763) as "a three-candidate garage-node picker keyed by crib upgrade level." This is the crib-side half of §8.1's "Context C — in-crib garage" entry shape for `store_vehicle.lua`.

**No crib/cache split, no mode toggle — there is exactly one entry shape, because this document can only ever BE the crib context.** Contrast weapon's binary walk-in/cache split (§6.1), clothing's single-toggle (§7.1), and vehicle's triple axis (§8.1): `store_crib.lua` has nothing analogous to branch on. `Store_common_exit_cb` is reset to `store_crib_exit` on every re-focus (`store_crib_waiting_for_fade`, `:263`), so returning from a sub-store re-arms the same callback each time rather than re-initializing fresh — consistent with `store_crib` persisting underneath the sub-store the whole time, not being popped and re-pushed.

A `COMPLETION_CASH` top-menu row is present only as **dead, commented-out data** (`:46-50`), with an in-source dev comment: *"cash MOVED TO CELL PHONE, MAYBE IT WILL COME BACK?"* `store_crib_open_cash(menu_data)` (`:529-535`) — a live function that would `push_screen(menu_data.screen)` — is consequently unreachable from this file's own menu table. This directly corroborates §1's own finding (line ~52-53) that the only live `push_screen("store_crib_cash")` call site anywhere in the extracted Lua is `cell_menu_main.lua:314` — i.e. the crib's own cash sub-screen is opened exclusively from the cell phone, never from this hub, confirming that earlier cross-file observation from the crib side.

### 10.2 Menu structure — exactly ONE real tier, built fresh each time as a static Lua table entirely gated by direct native boolean calls; no `vint_dataresponder_request`, no `pcr_report_*`

`store_crib_build_top_menu()` (`:42-107`) constructs `Store_crib_top_menu` (a module-level global, `:10`) from scratch on every call, appending rows only when their native gate returns true:
- `"MENU_SWAP_WARDROBE"` → `store_crib_open_screen`, screen `"store_clothing"` (`:53-56`, unconditional).
- 4 garage rows, gated first by `crib_is_garage_disabled() ~= true` (`:58-59`, already documented §57.3), then individually by `crib_has_garage_with_vehicles_of_type(GVT_CARS|GVT_HELICOPTERS|GVT_AIRPLANES|GVT_WATERCRAFT)` (`:61,67,73,79` — native `0x0080e490`, already documented §23.19) → `store_crib_open_garage`, screen `"store_vehicle"` (`:61-82`).
- `"WPNSTOR_TITLE_WEAPON_CACHE"` → `store_crib_open_screen`, screen `"store_weapon"` (`:86-89`, unconditional).
- `"GANG_CUST_TITLE"` → `store_crib_open_screen`, screen `"store_gang"`, gated by `store_gang_is_unlocked()` (`:92-95` — already documented §49.1, "a player cash/unlock-bit gate for the gang store").
- `"MENU_STRONGHOLD_CUSTOMIZATION"` → `store_crib_open_screen`, screen `"store_stronghold"`, gated by `crib_is_stronghold() and Completion_is_client() == false` (`:98-101` — `crib_is_stronghold` already documented §57.3; `Completion_is_client` already documented §10.2, confirmed false in single player).
- `"STORE_EXIT"` → `store_crib_exit` (`:104-105`, unconditional, always last).

Populated via `store_crib_populate_list(Menu_data, 1)` (`:196`, called directly from init) → `Active_list:draw_items(...)` (`:464`) — **zero `vint_dataresponder_request` calls and zero `pcr_report_*` calls anywhere in the file** (confirmed by grep for `vint_dataresponder_request`, `pcr_report_`, `pcu_`, `vcust_` — all zero hits). This is one step more extreme than weapon's hardcoded tier 1 (§6.2) or tattoo's hardcoded tiers 1-2 (§9.2): in `store_crib.lua` there is only one tier, and it is hardcoded/gate-evaluated too — the simplest list-population shape of all five traced stores.

The menu is **rebuilt** (not just repositioned) specifically on return from `"store_stronghold"`: `store_crib_waiting_for_fade()` (`:236-333`) calls `store_crib_build_top_menu()` again (`:286`) only when `Previous_screen == "store_stronghold"` (`:285-292`) — because a stronghold upgrade can change which rows are valid (e.g. unlock a new garage row) — but reuses the existing `Store_crib_top_menu` table unchanged when returning from `store_weapon`/`store_clothing`/`store_gang`/`store_vehicle`, just repositioning the cursor to `Store_crib_current_menu_selection` (`:329`).

### 10.3 Purchase surface — NONE: zero cash-charging natives in the entire file

Grepped the whole file for `purchase`/`cash`/`respect`/`charge` (case-insensitive): the only hits are the dead `COMPLETION_CASH` row and its comment (`:46,49`), a comment labeling the shared confirm widget "purchase popup" (`:181`), and `Store_header:set_respect(nil)` inside the purely-cosmetic `store_crib_header_revert` (`:478-481`, clears a display field, charges nothing). **This is a genuine structural difference from every one of the four prior worked examples**, each of which has at least one direct Lua-bound cash-charging native (§4's summary table; §6.3 weapon, §7.3 clothing, §8.3 vehicle, §9.3 tattoo). `store_crib.lua` charges nothing itself — it routes to the five stores that do (`store_weapon`/`store_clothing`/`store_vehicle`/`store_gang`/`store_stronghold`).

**Checked for the obvious candidate and ruled it out.** `crib_purchase_purchase_crib` — the native that actually buys a crib/building, already documented `spec-lua-api-behaviour.md` §57.3 with a confirmed null-pointer-crash-plus-charge-before-ownership-check defect — is never called from `store_crib.lua` (confirmed by grep); it lives instead in a sibling file, `scratchpad/ui_lua_dumps/building_purchase.lua` (confirmed by grep — this is the only file in the extracted Lua set calling it; now fully traced in its own file, `spec-building-purchase-runtime.md`, since it's a different system in kind — acquisition, not interior customization — not a new section here). This makes sense structurally: `store_crib.lua` is the *interior* customization menu for a crib the player already owns; the "buy this building" flow is a separate file. **CORRECTED 2026-10-10 (an earlier draft of this entry wrongly grouped 4 more natives with it):** `crib_unlock_strongold`/`crib_purchasing_unlock`/`crib_disable_interface`/`crib_weapon_add_enable`/`crib_weapon_add_disable` are **not** called from `building_purchase.lua` at all (re-checked by grep — zero hits) — they're called from mission/city scripts instead (`scratchpad/lua_dumps/m01.lua:1011,1256`, `scratchpad/lua_dumps/sr3_city.lua:46,218,223,228`), a story-progression/mission-scripted unlock mechanism unrelated to the interactive purchase-building UI flow. Only `crib_purchase_purchase_crib` and `shop_purchase_purchase_shop` are called from `building_purchase.lua` (`spec-building-purchase-runtime.md` §4).

**No new undocumented purchase-adjacent natives** — the five gating natives this file does call (`crib_is_garage_disabled`, `crib_has_garage_with_vehicles_of_type`, `store_gang_is_unlocked`, `crib_is_stronghold`, `Completion_is_client`) were all already individually documented prior to this pass (§57.3, §23.19, §49.1, §57.3, §10.2 respectively). `store_crib_init_crib_garage` (§10.1 above) was also already documented, §49.1.

**Secondary, non-purchase native leads not found documented in `spec-lua-api-behaviour.md` (checked by grep, zero hits for each):** `pause_map_dump`/`pause_map_restore` (`:116`/`:220`, unload/reload the full-screen map around the crib UI session), and `bg_saints_set_type`/`bg_saints_show`/`bg_saints_slide_in`/`bg_saints_set_background` (`:134,269,348,349,366,392,516,523`, the crib/garage background-scene state machine). None of these charge cash or grant items — they are scene/background plumbing — so they do not affect §10.3's "zero purchase natives" finding, but are flagged here as honest new leads since the brief asked for any uncited native call-site. Likely shared across many non-store UI files (not crib-specific), so probably lower priority than the per-store purchase natives §4/§6.3/§7.3/§8.3/§9.3 already nailed down.

### 10.4 Preview mechanics — none: no 3D item preview, no camera repositioning, no color grid, no rotate/zoom

There is nothing to preview — this file lists other stores to enter, not purchasable items. Grepped for `store_set_camera_pos`/`vcust_set_camera_pos`/`Store_common_color_grid`: zero hits. `character_enable_mouse_drag(false)` is called once at init (`:205`) and never called with `true` anywhere in the file — a harder opt-out than weapon's §6.4 (which at least leaves the generic subscription machinery present and only stubs the per-event handlers; crib disables character mouse-drag outright and never re-enables it). The only "preview" in this file is the 3D **background scene** itself (the crib set, slid in/out via `bg_saints_slide_in`/`bg_saints_set_background` and the 2D UI slide tweens `store_crib_in_anim`/`store_crib_out_anim`/`Store_crib_title_in_anim`), not an item preview of the kind §3/§6.4/§7.4/§8.4/§9.4 document for the purchasable-item stores.

### 10.5 Exit convention — matches the shared §5 pattern exactly, single unconditional branch, tied with tattoo (§9.5) for the simplest of the five traced stores

`store_crib_exit()` (`:418-426`) always shows the shared `Yes_no_choices` confirm popup (`"MENU_TITLE_WARNING"`/`"MENU_EXIT_CRIB"`) — unconditional, with no crib-vs-walk-in branch of the kind weapon/clothing/vehicle have (§6.5/§7.5/§8.5), because there is nothing to branch on: this file only ever has the one context (§10.1). On confirm, `store_crib_exit_final(event)` (`:432-456`) calls **exactly one** `pop_screen()` (`:455`) — identical to §5's shared convention, no deviation, the same shape §9.5 found for tattoo. (A second `pop_screen()` call exists in the file, `:400`, but is a failure-path fallback inside `store_crib_back_out_anim_complete` — only reached if `push_screen` of the chosen sub-store fails — not part of the normal exit path.)

**One layer-up wrinkle worth recording: `store_crib_gained_focus()` (`:229-234`) is itself a focus-driven handler, operating one nesting level above `store_common_gained_focus()` (§5).** When a child store (`store_weapon`/`store_clothing`/`store_vehicle`/`store_gang`/`store_stronghold`) pops itself off, `store_crib` becomes top-of-stack again and the engine's own per-document focus convention (§1, §5) fires `store_crib_gained_focus`, which spawns a `store_crib_waiting_for_fade` thread (`:233`) rather than directly repopulating the list — that thread waits for `sfx_faded_in()` (`:237`), resets the header, conditionally replays entry animations keyed on `Previous_screen` (`:285-315`), and only then calls `store_crib_populate_list(Menu_data, Store_crib_current_menu_selection)` (`:329`). This is the same general VINT document-focus-event mechanism §5 already established for `store_common`, now confirmed operating independently one layer further up the stack (child-store-pops → crib regains focus), not a new primitive.

### 10.6 Net comparison with `store_weapon.lua`/`store_clothing.lua`/`store_vehicle.lua`/`store_tattoo.lua`

`store_crib.lua` is categorically different from all four prior worked examples on the one axis that defines them: it has **zero** purchase natives, **zero** preview/camera mechanics, and **zero** `vint_dataresponder_request`/`pcr_report_*` list-population calls — where weapon/clothing/vehicle/tattoo are purchase surfaces with 1, 2, 5(+1), and 1 cash-charging natives respectively (§6.3/§7.3/§8.3/§9.3), `store_crib.lua` is a pure navigation hub that routes to those stores (and to `store_gang`/`store_stronghold`) rather than charging anything itself. Its single menu tier is a static table rebuilt by direct native boolean gates (`crib_is_garage_disabled`, `crib_has_garage_with_vehicles_of_type` ×4, `store_gang_is_unlocked`, `crib_is_stronghold` + `Completion_is_client`) — more extreme than weapon's or tattoo's merely-hardcoded top tier, since here there is no deeper, dataresponder-populated tier at all. Its exit convention is the simplest/most uniform of all five (one unconditional confirm popup, one `pop_screen()`, no branch) — tied exactly with tattoo (§9.5), and a sharp contrast with vehicle's own "`pop_screen()` never called anywhere" deviation (§8.5). Its real significance for Team B is structural rather than mechanical: `store_crib.lua`'s own `store_crib_open_screen`/`store_crib_open_garage` functions are the concrete, previously-uncited mechanism that keeps `store_crib` resident on the document stack beneath `store_weapon`/`store_clothing`/`store_vehicle` while those are open — confirming, from the crib side for the first time, what §6.1/§7.1/§8.1 could previously only infer indirectly via `store_common_crib_is_loaded()`'s own `vint_document_find("store_crib")` check on the sub-store side.

## 11. Open items

1. **PARKED 2026-10-10 — on the project's live-capture list, alongside the phone-open binding.**
   Three independent search angles exhausted (screen-push call-site enumeration, the native
   `trigger_action` dispatch itself, and the Lua-coroutine-handoff lead) — static analysis is at
   diminishing returns here; needs a live/dynamic capture or a from-scratch native xref sweep not
   yet attempted. **The native/world-trigger call site that pushes `store_common` + a specific per-type store
   document onto the screen stack (§1).** Exhaustively confirmed absent from all extracted Lua
   (no `push_screen("store_common"|"store_weapon"|"store_vehicle"|"store_clothing"|"store_tattoo"
   |"store_gang"|"store_stronghold")` anywhere) — the same unresolved native-only shape as the
   phone menu's own top-level open (`spec-cellphone-menu-runtime.md` §2), but for a different
   trigger class (world-space shop entry, not a button press) so the phone's own exhausted
   CBA-table search is unlikely to directly apply.

   **Narrowed 2026-10-10, not resolved — traced the `Trigger_Volume` `trigger_action` dispatch
   one hop further, found a real `Entries:Stores` category in the data but no matching
   `push_screen` call in the native code.** The real `triggers.xtbl` content
   (`tools/as_base_xtbl/triggers.xtbl`, 20 real rows) confirms a dedicated `Entries:Stores`
   category (`store ownership`, `player creation`, `garage mechanic`) — supporting the data-side
   shape of "one action type, parameterized per placement" this hypothesis predicted. The
   registrar that populates the `0x026232a8` array's native-handler fields,
   `0x0093afe0(slotIndex, handlerStruct*)` (previously unidentified, `spec-lua-api-behaviour.md`
   §1.14), has exactly 14 real call sites covering 14 of 20 slots; decompiling essentially all
   ~19 resulting handler functions found **none that reads `trigger_action_param` (`+0xe8`) as a
   plain string and calls `push_screen` or the screen-stack primitives with it.** Slot 0's
   handlers are confirmed to be nothing more than the generic Lua `on_trigger`/`on_trigger_exit`
   trampoline; three handlers (`0x005f0770`, `0x00a02dc0`, `0x006d47b0`) instead read `+0xe8`/
   `+0xec` as a 64-bit name-hash pair and surface a generic "press-to-use" UI prompt (screen code
   `0x33`, independently flagged in `spec-frontend-menu-runtime.md` as "a world-interact-trigger
   popup") rather than opening a document directly; the remaining slots are unrelated
   lighting/validation behavior. **Net: the native `trigger_action` dispatch, as fully traced, is
   HIGH CONFIDENCE a UI-prompt/gating layer at most, not the final `push_screen` call — the
   hypothesis is not confirmed, and this item remains genuinely OPEN.**

   **Expanded 2026-10-10: the prompt's player-confirm-hands-off-to-a-Lua-coroutine hypothesis is
   NOT confirmed — closed out negatively, not left unexplored.** The three per-frame
   cache-revalidation readers (`0x005f0130`, `0x005f0a60`, `0x005f1e50`) were each fully
   re-derived past the prior pass's shallow depth. None of the three ever reads the trigger's
   `+0xe8` field at all; none calls any of the established button-press-query primitives
   (`0x005bb530`/`0x005bdfe0`/`0x005bbdd0`); none calls the trigger-specific Lua hook dispatcher
   or its name-resolver. `0x005f0130` is a per-frame UI-field populator: it re-resolves the
   prompt's target object through the same generic name-hash lookup the dispatch handlers use,
   re-checks the same disabled-bit/class-capability gate, then stages ten display fields (an
   object handle, a stat value, two scaled distance-in-centimeters integers read from the
   resolved object's own `+0xe0` and the player's `+0x1ca0`, and up to four name/percentage/
   string fields) through a generic one-field push/commit pair, ending in an unconditional call
   into a shared field-commit dispatcher with roughly a hundred unrelated call sites across the
   whole binary. That shared dispatcher's own last step does call the same thread-table/
   coroutine-spawn primitive already confirmed for ordinary trigger-enter/exit hooks, but it
   supplies the UI object's own separate name field as the lookup key, never the trigger's
   authored `trigger_action_param`, and it fires unconditionally on every commit from every one of
   those unrelated call sites — generic UI-document plumbing, not a player-confirm handoff.
   `0x005f0a60` only re-validates the cached trigger handle and, when an unrelated global state
   value reaches a fixed marker, stops an associated sound and clears the cache — dismissal on a
   state change, not a confirm path. `0x005f1e50` is a Lua-callable native that gates on a plain
   integer distance comparison (the same two distance fields `0x005f0130` reads) and then computes
   screen-space position for the prompt's icon — again no button check, no coroutine spawn, no
   `trigger_action_param` read. **Net: this prompt's full display/validity/positioning plumbing is
   now exhaustively read and contains no player-confirm handler and no name-keyed Lua hand-off of
   any kind; if a distinct "player pressed confirm" native exists for this prompt, it is some
   other, still-unidentified function reachable from neither of these three readers.** This
   specific lead is now closed out negatively, not merely unexplored — the walk-in-store-open
   trigger chain remains genuinely OPEN. Also still unresolved: which of the 20 real row names
   (including the 3 `Entries:Stores` rows) hashes to which 0–19 slot was not computed; 5 slots
   have no native override at all; no real `.czn_pc` placed instance with a store-naming
   `trigger_action_param` string was found to cross-check empirically.
2. ~~`store_weapon_purchase_weapon`'s own cash-charge call site~~ **RESOLVED 2026-10-10 — it does
   charge cash, via `0x00816f00`'s own internal mode-0 branch calling the shared cash-adjust
   primitive `0x0094d920` (reason `0xb`), the same mechanism `pcr_purchase_tattoo` uses. See §4.
3. **`store_common_rotate_mouse_drag`'s own body** — still OPEN from `spec-lua-api-behaviour.md`
   §11.9/§11.13 (not independently re-derived this pass; cross-referenced only, per this pass's
   own scope).
4. **Per-type stores beyond this shared foundation.** ~~weapon/vehicle/clothing/gang/tattoo/
   stronghold/crib/DLC/gallery/demo's own individual list-population/preview/exit specifics~~
   **`store_weapon` (§6), `store_clothing` (§7), `store_vehicle` (§8), `store_tattoo` (§9), and
   `store_crib` (§10) now traced to full depth, 2026-10-10 — this completes the orchestrator's
   requested per-type store arc, PARKED here.** `store_crib` is confirmed structurally distinct
   from the other four: it is a pure navigation hub with zero purchase/preview natives of its own
   (§10.3/§10.4), and its own push/open functions are the mechanism that keeps the `store_crib`
   document resident beneath weapon/clothing/vehicle, confirming §6.1/§7.1/§8.1's
   `store_common_crib_is_loaded()` cross-reference from the crib side. Remaining untraced, all
   explicitly deferred: gang/stronghold/DLC/gallery/demo. **`building_purchase.lua` (the
   crib/property acquisition flow this item had flagged as adjacent-and-untraced) is now ALSO
   traced** — see its own new file, `spec-building-purchase-runtime.md`, 2026-10-10. (Note: an
   earlier draft of this item incorrectly claimed `crib_unlock_strongold`/`crib_purchasing_unlock`/
   `crib_disable_interface`/`crib_weapon_add_enable`/`crib_weapon_add_disable` were also called
   from `building_purchase.lua` — corrected in §10.3 above: those 5 are mission/city-script-called,
   unrelated to this UI flow; only `crib_purchase_purchase_crib` lives there.)

5. `Vdo_weapon_radial`'s own object methods (`slot_disable`/`slot_enable`/`weapon_highlight`/
   `show_dpad_menu`/`store_mode_enable`/`get_selected_slot`, used throughout §6.2) were not traced
   down to their own native bodies — out of scope for the menu/purchase/preview trace, same shape
   as `store_common_rotate_mouse_drag` above.
6. `store_weapon_change_weapon`'s own per-frame C-side consumer (the code that actually reads the
   preview state this native writes and positions/renders the model) was not traced —
   `spec-lua-api-behaviour.md`'s existing entry already flags this as not traced, reconfirmed
   rather than resolved here.
7. **`pcu_wear_current_clothing`** (`store_clothing.lua:769`/`:1208`) — a new, undocumented
   native lead: the wardrobe "wear item" call, distinct from the already-documented
   `pcu_wear_outfit`/`pcu_wear_store_outfit`. Not yet traced to its C body.
8. **`pcu_discard_slot`** (`store_clothing.lua:1294`/`:1326`) — a second, lower-priority new
   native lead, likely close kin to `pcu_remove_clothing`'s already-documented "clearable
   store-entry slot" semantics but not individually specced under its own name.
9. **`vcust_revert_wheels`** (`store_vehicle.lua:1142,1145,1149,1164,1172,1176,1198-1203`) — a
   new, undocumented native lead: the wheel sub-menu's shared `on_cancel` hook across all its
   rows (rim-style, tire-tread, rim-size toggle, both sliders, the purchase button, all 3 axle
   rows). HIGH CONFIDENCE it mirrors `vcust_revert_color`'s own shape, not independently traced
   to its own C body.
10. **`store_vehicle_play_exit_anim`'s own caller** (§8.5) — reached with no visible Lua caller;
    plausibly a timeline-driven animation-event callback outside the Lua call graph, or genuinely
    dead code. Not resolved.

## 12. Clean-room compliance note

Plain hex addresses and real Lua identifiers (variable/function names read directly from the
shipped, extracted script source) throughout. No Ghidra decompiler auto-names
(`FUN_`/`DAT_`-style), no pseudocode, no leaked Volition/Havok/Audiokinetic/RAD proprietary
material.

## Changelog

- **2026-10-10 — correction to the §10.3/§11-item-4 `building_purchase.lua` claim, plus
  `building_purchase.lua` now fully traced in its own new file** (SPEC TEAM). The §10 splice's own
  §10.3 and §11 item 4 wrongly stated that `crib_unlock_strongold`/`crib_purchasing_unlock`/
  `crib_disable_interface`/`crib_weapon_add_enable`/`crib_weapon_add_disable` were called from
  `building_purchase.lua` alongside `crib_purchase_purchase_crib` — re-checked by grep while
  tracing that file: those 5 natives are **not** in `building_purchase.lua` at all; they're called
  from mission/city scripts (`scratchpad/lua_dumps/m01.lua`, `scratchpad/lua_dumps/sr3_city.lua`),
  a mission-scripted unlock mechanism unrelated to this UI flow. Only `crib_purchase_purchase_crib`
  and `shop_purchase_purchase_shop` are actually called from `building_purchase.lua`. Text
  corrected in place in §10.3/§11. **`building_purchase.lua` itself is now fully traced** — see
  new file `spec-building-purchase-runtime.md` (the crib/property acquisition flow) plus two newly
  resolved dataresponder-reply natives in `spec-lua-api-behaviour.md` §62
  (`crib_purchase_get_crib_info`/`shop_purchase_get_shop_info`, both sharing null-crash shapes with
  their already-documented purchase-native siblings). Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-10 — new §10: full document-level trace of `store_crib.lua`, fifth per-type store
  deep dive, first confirmed non-purchase hub — PARKS the per-type store arc** (SPEC TEAM, agent
  `a30520c99cbd7a0c5`). **Headline structural finding:** unlike weapon/clothing/vehicle/tattoo
  (§6-§9), `store_crib.lua` charges nothing and previews nothing — zero cash-charging natives,
  zero `store_set_camera_pos`/`vcust_set_camera_pos`/color-grid calls, and zero
  `vint_dataresponder_request`/`pcr_report_*` list-population calls anywhere in the file (all
  confirmed by grep). It is a pure navigation hub: a single static, native-boolean-gated
  top-level menu (`store_crib_build_top_menu`, `:42-107`) routing to
  `store_weapon`/`store_clothing`/`store_vehicle`/`store_gang`/`store_stronghold`. **New
  cross-confirmation:** `store_crib_open_screen`/`store_crib_open_garage` (`:505-548`) push the
  chosen sub-store's screen WITHOUT first popping `store_crib`, confirming from the crib side (for
  the first time) the stacking shape §6.1/§7.1/§8.1 had only inferred from the sub-store side via
  `store_common_crib_is_loaded()`. The crib-garage path additionally resolves to the
  already-documented native `store_crib_init_crib_garage` (§49.1), never defined in any extracted
  Lua file. Confirmed the dead `COMPLETION_CASH` top-menu row (commented out, `:46-50`)
  corroborates §1's finding that `store_crib_cash` is opened exclusively from the cell phone
  (`cell_menu_main.lua:314`), never from this hub. Exit convention matches §5/§9.5 exactly (one
  unconditional confirm popup, one `pop_screen()`) — tied with tattoo for simplest of the five.
  **New open-item lead:** `building_purchase.lua`, an untraced adjacent file holding the actual
  crib-ownership-purchase native (`crib_purchase_purchase_crib`, §57.3) and four related `crib_*`
  natives, none of which `store_crib.lua` itself ever calls. No new undocumented *purchase-surface*
  natives found; flagged (non-purchase, lower priority) two groups of scene-management natives not
  yet individually specced: `pause_map_dump`/`pause_map_restore` and
  `bg_saints_set_type`/`bg_saints_show`/`bg_saints_slide_in`/`bg_saints_set_background`. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits. **This completes the
  orchestrator's requested per-type store arc (weapon→clothing→vehicle→tattoo→crib); the list is
  now PARKED per the orchestrator's own instruction — DLC/gallery/demo/gang/stronghold remain
  explicitly deferred.**

- **2026-10-10 — new §9: full document-level trace of `store_tattoo.lua`, fourth per-type store
  deep dive** (SPEC TEAM, agent `a88abe635cee0ed7b`). Confirmed the simplest entry context of the
  four traced stores: no crib/cache split, no live mode toggle, single uniform exit convention.
  Mapped 3 real menu tiers (static main menu → static zone list → `pcr_report_*`-populated item
  list) plus optional color-grid (display-swap) and opacity-slider (real stack-push) sub-steps.
  **New structural finding:** this store never calls `vint_dataresponder_request` (zero hits) —
  its item/color tiers use the bare `pcr_report_composite_items`/`pcr_report_composite_colors`
  callback-string convention instead, a genuinely different list-population mechanism from
  weapon/clothing/vehicle's shared dataresponder-channel norm (§2). Confirmed exactly one purchase
  native (`pcr_purchase_tattoo`, already documented §43.3) — the simplest purchase surface of the
  four. **New defect-shaped finding:** the purchase-confirm handler's hardcoded two-level
  `store_common_back_menu()` pop (`:206-207`) silently under-pops by one level whenever the
  opacity-slider sub-step was used (that sub-step adds a third real stack push the fixed pop-count
  doesn't account for), leaving the player on the zone list instead of the main menu after certain
  purchases. Confirmed live try-before-buy preview (debounced per-navigation `pcr_change_composite`
  calls, revert via a `pcr_get_composite_info` snapshot) — a third distinct preview mechanism
  alongside clothing's poll-thread (§7.4) and vehicle's preview/revert native pairs (§8.4).
  Confirmed the zone-table `replace` (overlap-clearing) field is dead/vestigial data — its handling
  code is entirely commented out in `store_tattoo_menu_build`, per an in-source 2011 developer
  comment reversing that design decision; tattoos in nominally-overlapping zones are allowed to
  coexist. No new undocumented natives — `pcr_change_hair`/`pcr_get_hair_info` appear in the file
  but inside dead code (defined, never called except one commented-out site). Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new §8: full document-level trace of `store_vehicle.lua`, third per-type store
  deep dive** (SPEC TEAM, agent `a4cca7c4ebc32c7d8`). Confirmed the richest entry context of the
  three traced stores: THREE overlapping axes (crib-vs-walk-in, engine-reported
  garage-vs-customize mode, and a live bidirectional mode toggle keyed off the CACHED starting
  mode for exit purposes) — not a clean binary split like weapons (§6.1) nor a single toggle
  like clothing (§7.1). Mapped 6 distinct tier-2 row-kind dispatches plus a fully separate static
  wheel sub-tree. Confirmed 5 distinct cash-charging `vcust_*` natives (component/wheels/paint/
  underglow/performance-upgrade) plus a 6th `garage_*` repair charge — all but one already
  individually documented in `spec-lua-api-behaviour.md` from prior tranches. **One new native
  lead found:** `vcust_revert_wheels` (the wheel sub-menu's shared `on_cancel` hook, zero hits in
  the existing spec). Confirmed the full generic 3D camera/rotate framework is in use (matching
  clothing, contrasting weapons) PLUS 3 kinds of per-feature camera repositioning (area-reset,
  per-axle `vcamera` fields, per-slot angle adjust) neither sibling store has. **Real
  exit-convention deviation, new finding:** `pop_screen()` is never called anywhere in this file
  (grepped, zero hits) — every exit is either a native-internal screen-stack pop
  (`store_vehicle_retrieve_car_and_exit`/`store_vehicle_do_return_to_crib`, both already
  documented) or a direct `vint_document_unload()` call, a genuine structural difference from
  §5/§6.5/§7.5's shared "exactly one `pop_screen()`" convention. Re-examined the existing
  `vcust_purchase_component` stale `-1` selection-index bug (§4/§52.1): confirmed negative — the
  index reset happens inside the native itself, not in this file's entry/exit flow; every Lua
  path into the purchase confirm is preceded by a fresh preview-set call in the same session, no
  new trigger found. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new §7: full document-level trace of `store_clothing.lua`, second per-type
  store deep dive** (SPEC TEAM, agent `ae24c9d1309661f54`). Confirmed `store_character.lua` is a
  genuinely separate system (the PCR character-creator family), not a clothing sibling. Unlike
  weapons, clothing has no crib/cache entry-context split — instead a single document with a
  LIVE in-document store↔wardrobe mode toggle. Mapped a data-driven area→category→item(→detail)
  tree plus a fully separate outfit-management sub-tree. Confirmed `pcu_purchase_slot`/
  `pcu_purchase_outfit` dispatch is automatic (keyed off which row kind was last navigated), not a
  player UI choice. **Two new native leads found**: `pcu_wear_current_clothing` (wardrobe
  "wear item," distinct from the already-documented wear-outfit natives) and `pcu_discard_slot`
  (lower priority). Confirmed clothing uses the FULL generic 3D-turntable preview framework
  (camera reposition + mouse-rotate + zoom hint) — direct contrast to `store_weapon.lua`'s
  confirmed deliberate opt-out. Found one real exit-convention deviation: backing out of
  in-store wardrobe mode doesn't pop the document at all, it just switches mode back — a second
  "back" press is needed to actually trigger the walk-in exit-confirm path. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new §6: full document-level trace of `store_weapon.lua`, first per-type store
  deep dive** (SPEC TEAM, agent `aeece5f4cd128bd7d`). Confirmed a dual entry context (walk-in shop
  vs. in-crib "weapon cache," gated by a trivial one-liner checking for a `store_crib` document on
  the stack — not a native, no new lead). Mapped the full 4-tier menu with exact dataresponder
  channel/callback signatures at each tier. Resolved the "implied upgrade purchases" question:
  "buy new weapon" and "buy next upgrade level" are literally the same purchase native and code
  path (a weapon's level list), not two separate systems — no separate attachment/scope store
  exists. New finding: `store_weapon.lua` never calls `game_award_respect` (zero hits), a real
  exception to §4's "every traced purchase confirm handler" framing — corrected there. Preview is
  confirmed simpler than the generic framework (no camera reposition, no rotate, no zoom — a dead
  code comment confirms these were deliberately cut; no test-fire-before-buying). Exit convention
  matches the shared pattern exactly; the crib flag only changes whether a confirm dialog shows
  and what's left on the stack after the cascade. No new undocumented natives found. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §9 item 1: the Lua-coroutine-handoff lead exhausted, a clean negative** (SPEC
  TEAM, agent `acdc7f7db3671dc77`). Fully re-derived the prompt's 3 cache-revalidation readers
  (`0x005f0130`/`0x005f0a60`/`0x005f1e50`) past the shallow depth a prior pass stopped at. None
  reads the trigger's `trigger_action_param` field, none checks a button press, none calls the
  trigger-specific Lua hook dispatcher. The one coroutine-spawn call reachable from this cluster
  is generic UI-document commit plumbing (fires on ~100+ unrelated call sites, keyed off a
  different field entirely), not a player-confirm handoff. This specific lead is now closed out
  negatively rather than left unexplored — the store-open trigger chain remains genuinely OPEN.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §9 item 1: store-open trigger narrowed, not resolved — `Trigger_Volume`'s
  `trigger_action` dispatch is a UI-prompt/gating layer at most, not the `push_screen` call**
  (SPEC TEAM, agent `a22eefdc9e0e38dbf`). Found the real `triggers.xtbl` content (confirming a
  dedicated `Entries:Stores` category) and the previously-unidentified native-handler registrar
  (`0x0093afe0`), then decompiled essentially all ~19 resulting handler functions — none reads
  `trigger_action_param` as a document-name string and calls `push_screen` with it. The dispatch
  instead surfaces a generic "press-to-use" UI prompt (screen code `0x33`) for several slots;
  everything else is unrelated lighting/validation. Hypothesis NOT confirmed, honestly reported as
  such — item stays OPEN with a narrower next step (expand the prompt's own confirm-step callees,
  or check for a Lua-coroutine hand-off supplying a hook name instead of a screen name).
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §4/§9: `store_weapon_purchase_weapon`'s cash-charge question RESOLVED** (SPEC
  TEAM, agent `a612799c38f12d7b8`). Re-derived both `0x008172d0` and its callee `0x00816f00` from
  scratch: the real-purchase branch calls `0x00816f00(weaponRecord, mode=0, price)`
  unconditionally; that dual-mode function's mode-0 branch charges the price through the shared
  cash-adjust primitive `0x0094d920` (reason `0xb`), the same mechanism `pcr_purchase_tattoo`
  uses. The prior pass's gap was purely call-graph depth, not a real missing charge — weapon
  purchases are charged exactly like every sibling store family. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — new file.** First map of the shared walk-in-store purchase framework
  (`store_common.lua`): confirmed it is a library module layered on its own separately-pushed
  VINT document (not something per-type stores `require`/extend), with the same unresolved
  native-only top-level-open shape as the phone menu's own open binding. Central finding: store
  purchases go through a directly Lua-bound native per item-type family (weapon/ammo/vehicle-
  component/tattoo/clothing), each independently already documented in `spec-lua-api-behaviour.md`
  — the opposite pattern from `cell_rewards`'s dataresponder-post shape. One new open gap flagged:
  `store_weapon_purchase_weapon`'s existing write-up doesn't describe where/whether it charges
  cash, unlike every sibling purchase native. Exit convention confirmed structurally distinct from
  the phone menu's fixed-depth pops (1 explicit pop + 1 automatic cascade from the shared base
  document's own focus-gained handler). Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.
