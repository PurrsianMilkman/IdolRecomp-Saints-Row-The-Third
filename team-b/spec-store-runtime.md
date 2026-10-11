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

## 7. Open items

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
   **`store_weapon` now traced to full depth (§6), 2026-10-10.** Remaining untraced: vehicle/
   clothing/gang/tattoo/stronghold/crib/DLC/gallery/demo.

5. `Vdo_weapon_radial`'s own object methods (`slot_disable`/`slot_enable`/`weapon_highlight`/
   `show_dpad_menu`/`store_mode_enable`/`get_selected_slot`, used throughout §6.2) were not traced
   down to their own native bodies — out of scope for the menu/purchase/preview trace, same shape
   as `store_common_rotate_mouse_drag` above.
6. `store_weapon_change_weapon`'s own per-frame C-side consumer (the code that actually reads the
   preview state this native writes and positions/renders the model) was not traced —
   `spec-lua-api-behaviour.md`'s existing entry already flags this as not traced, reconfirmed
   rather than resolved here.

## 8. Clean-room compliance note

Plain hex addresses and real Lua identifiers (variable/function names read directly from the
shipped, extracted script source) throughout. No Ghidra decompiler auto-names
(`FUN_`/`DAT_`-style), no pseudocode, no leaked Volition/Havok/Audiokinetic/RAD proprietary
material.

## Changelog

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

- **2026-10-10 — §7 item 1: the Lua-coroutine-handoff lead exhausted, a clean negative** (SPEC
  TEAM, agent `acdc7f7db3671dc77`). Fully re-derived the prompt's 3 cache-revalidation readers
  (`0x005f0130`/`0x005f0a60`/`0x005f1e50`) past the shallow depth a prior pass stopped at. None
  reads the trigger's `trigger_action_param` field, none checks a button press, none calls the
  trigger-specific Lua hook dispatcher. The one coroutine-spawn call reachable from this cluster
  is generic UI-document commit plumbing (fires on ~100+ unrelated call sites, keyed off a
  different field entirely), not a player-confirm handoff. This specific lead is now closed out
  negatively rather than left unexplored — the store-open trigger chain remains genuinely OPEN.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §7 item 1: store-open trigger narrowed, not resolved — `Trigger_Volume`'s
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

- **2026-10-10 — §4/§7: `store_weapon_purchase_weapon`'s cash-charge question RESOLVED** (SPEC
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
