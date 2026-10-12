# Building purchase runtime — `building_purchase.lua` (crib/property acquisition flow)

**Scope.** This file documents `scratchpad/ui_lua_dumps/building_purchase.lua` (265 lines) — the
flow that runs when the player buys a building/crib/store they do not yet own, reached from the
open-world "press X to buy this building" prompt. This is a genuinely different system from
`store_crib.lua`'s interior-customization hub (`spec-store-runtime.md` §10): `store_crib.lua` has
zero purchase natives of its own (§10.3) and is never the caller of this file; this file is the
answer to §10.3's/§11 item 4's own open lead ("`building_purchase.lua` is itself untraced").
Builds on `spec-store-runtime.md` §1-§5's generic VINT/dataresponder vocabulary where the same
mechanisms recur, but is not a `store_common` consumer and has no menu depth to trace — the
subsection breakdown below is adapted to a single linear flow instead of §6-§10's menu-tier
template.

## 1. Entry context — one Lua file, two live modes (crib vs. shop), selected once at init by a
single native boolean read

`building_purchase_init()` (`:16-53`) reads `Building_purchase_is_crib =
building_purchase_is_crib_being_purchased()` (`:19`, native `0x005eed90`, already documented
`spec-lua-api-behaviour.md` line 16419 as "true iff a 64-bit handle pair is non-zero — HIGH
CONFIDENCE the crib currently being purchased") exactly once, into a module-level local never
re-read from the native again — every later branch in the file (`update_building_purchase_text`,
`building_purchased`, the two info-dataresponder requests) tests this cached Lua boolean, not the
native. There is no third mode and no re-entry check: whichever the native answered at `init` time
governs the whole screen's lifetime.

Hides the base UI (`ctrl_info_grp`/`guns`/`main`, `:22-24`) until `building_purchased` reveals them
again (`:218-220`), subscribes an `"all_unassigned"` input blocker (`"bp_nothing"`, `:34`) and a
pause handler (`:27`), sets up a `Vdo_city_control` district-control-meter widget (`:32`), and (PC
only) a mouse-click-to-confirm tracker over a full-screen clickable mask (`:45-52`) whose click
handler is just `building_purchase_clean` (`:259-261`) — the mouse path and the gamepad "select"
path (subscribed later, `:241`) are two independent routes to the same exit function.

## 2. The info request — one `vint_dataresponder_request`, branching only on crib-vs-shop

Immediately after the mode read, `init` posts exactly one dataresponder request (`:36-40`):
`vint_dataresponder_request("crib_purchase_get_crib_info", "update_building_purchase_text", 0)`
for crib mode, or the same with `"shop_purchase_get_shop_info"` for shop mode. Both names are
newly-traced natives (not previously in `spec-lua-api-behaviour.md`), full characterization in
`spec-lua-api-behaviour.md` §62 (spliced alongside this file). In short: each resolves a target
object (the pending-purchase crib via the same 64-bit handle pair `building_purchase_is_crib_being_purchased`
tests, or the pending-purchase shop via a separate trigger-based resolver) and reports it back
through 10 (crib) or 11 (shop) typed reply fields, landing as `update_building_purchase_text`'s own
11 positional parameters (10 sent for crib mode — `is_store` is never sent on that branch, §62.1).
**Both info natives share real null-pointer-crash shapes with their already-documented purchase-native
siblings** (`crib_purchase_purchase_crib` §57.3, `shop_purchase_purchase_shop` line ~15761) — see
§62.1/§62.2 for the resolve-failure reachability detail, independently spot-checked against the
Ghidra query server during this splice (both confirmed: the crib handler's resolved pointer and the
shop handler's resolved pointer are each dereferenced with no null check on the very next
instruction). This is the file's only `vint_dataresponder_request` call and its only
server-populated data — everything else in the file is either a hardcoded Lua literal or a direct
native read.

## 3. The confirm dialog — insufficient-funds short-circuit, else a Yes/No gate before any charge

`update_building_purchase_text` (`:82-142`) takes 11 parameters (`building_name, description_txt,
store_cost, player_cash, profit, control_pct, control_pct_new, control_pct_increase, district,
discount, is_store`, `:82`) and stores them into `Building_purchasing_data` (`:83-96`, with
`profit` only copied for the shop branch, `:94-96` — matching §62.1's finding that the crib reply
never sends a meaningful profit value). Four fields default (with a `debug_print` warning each
time) if the responder didn't supply them: `control_pct`/`control_pct_new` → 0, `profit` → 10,
`control_district` → `"EMPTY DISTRICT NAME"`, `discount` → 10 (`:98-122`) — a defensive fallback
layer that papers over exactly the "hardcoded 0"/"no district" cases §62.1/§62.2 already show the
info natives can legitimately produce, so in practice some of these warnings are expected noise on
a crib purchase, not a sign of a real data gap.

Then a single branch (`:124-141`): if `player_cash < store_cost`, shows a message-only dialog
(`"HUD_SHOP_INSUFFICIENT_FUNDS"`, callback `"building_not_enough_money"` — just a `pop_screen()`,
`:68-70`); otherwise shows a Yes/No confirmation (`dialog_box_confirmation`, callback
`"building_purchased"`) with crib-specific (`"BUILDING_PURCHASE_CRIB"`) vs. generic
(`"BUILDING_PURCHASE_BUILDING"`) body text, both carrying the building name and formatted cost as
insert values. **No cash is touched and no purchase native has been called yet at this point** —
this is purely a display/gate step.

## 4. The purchase surface — exactly 2 natives, chosen by the same cached mode flag, both already
documented as defective

`building_purchased(result, action)` (`:144-248`) only proceeds past a `result == 0` (Yes) check —
a No answer is a bare `pop_screen()` (`:245-247`), no different from any other dialog cancel. On
Yes, it runs a long sequence of **pure UI setup** (title text + autoscale, `:150-161`; initial
control-meter position, `:164`; a tween end-event wire-up for the control-meter's animated fill,
`:166-168`; control-increase/district text, `:171-184`; the crib-only hidden/shop-only-visible
cash-per-hour text, `:186-194`; the shop-only (and `is_store`-gated) discount text, `:196-204`; a
gamepad-only hint button, `:206-215`; re-showing the base UI groups, `:217-220`; and finally
kicking off the entry animation, `:222-232`) **before** the actual purchase call — all of this UI
work happens unconditionally, whether or not the purchase call that follows succeeds.

The purchase call itself (`:234-239`) is a single two-way branch on the same cached
`Building_purchase_is_crib`: `crib_purchase_purchase_crib()` for crib mode, `shop_purchase_purchase_shop()`
for shop mode — **zero arguments either way**, both relying entirely on the same engine-side
"currently selected for purchase" state the info natives in §2 already read from. Both are
pre-existing, already-documented defects, not new findings of this pass:
`crib_purchase_purchase_crib` (`spec-lua-api-behaviour.md` §57.3) is CONFIRMED to charge cash
*before* checking ownership, on top of its own null-pointer-crash shape; `shop_purchase_purchase_shop`
(line ~15761) is CONFIRMED to have a real double-charge defect on repeat calls, on top of a
null-dereference crash on a stale/absent shop trigger. The in-source dev comment at `:234`
("maybe a programmer should look at this, and figure out why we need two?") is the original
developers' own acknowledgment that this crib/shop duplication was never cleaned up — Team B
should treat the two purchase paths as independently-defective siblings, not as one mechanism with
two names.

Immediately after the purchase call, regardless of its outcome (the Lua side has no return value
to check — both purchase natives return nothing to this call site), the gamepad "select" input and
the mouse-click tracker are subscribed (`:241-244`) — from this point on, either input routes to
`building_purchase_clean`.

## 5. Exit convention — three named callbacks, one real pop, one deliberately-suppressed pop, no
branch on crib-vs-shop

- **`building_purchase_clean()`** (`:59-66`): plays a confirm sound, calls `game_autosave()`, then
  exactly one `pop_screen()`. This is the normal "player pressed select/clicked after a successful
  purchase" exit — reached only via the two subscriptions set up at the end of §4, i.e. only after
  a purchase call has already fired. No branch on crib-vs-shop; the autosave runs unconditionally
  for both.
- **`building_not_enough_money()`** (`:68-70`): exactly one `pop_screen()`, no autosave, no sound
  call of its own. This is the insufficient-funds dialog's callback (§3) — reached *without* any
  purchase native ever being called, since the insufficient-funds branch short-circuits before the
  Yes/No confirm step entirely.
- **`building_purchase_cleanup()`** (`:72-79`): unsubscribes every tracked input event
  (`Building_purchase_subs`) but explicitly does **not** call `pop_screen()` — an in-source dev
  comment (`:77-78`) explains this is invoked from the engine's own VINT-document-destroy teardown,
  and popping here on top of that teardown's own pop would double-pop the screen stack. This is the
  same general hazard class `spec-store-runtime.md` §5/§8.5 document for the store-common family
  (teardown-vs-explicit-pop ordering), now confirmed present in this file too, already correctly
  avoided by this file's own author.

No crib-vs-shop branch appears anywhere in the exit convention — contrast `store_weapon`/
`store_clothing`/`store_vehicle` (`spec-store-runtime.md` §6.5/§7.5/§8.5), which all branch their
exit behavior on crib-vs-walk-in context. This file's two modes differ only in *which* info native
is requested (§2) and which purchase native is called (§4); every other mechanic (confirm dialog,
UI animation, exit) is shared, unbranched code.

## 6. Net comparison with `store_crib.lua` (`spec-store-runtime.md` §10)

Both files are "simple" by store-runtime.md's own metric (zero/near-zero menu tiers), but for
opposite reasons: `store_crib.lua` is simple because it is a pure navigation hub with **zero**
purchase natives (§10.3) — it routes to five sub-stores that do the charging. `building_purchase.lua`
is simple because it is a pure linear confirm-then-charge flow with **no menu at all** — one
dataresponder request, one Yes/No gate, one purchase call. Where `store_crib.lua`'s exit convention
is the project's simplest (one unconditional confirm popup, §10.5), this file's exit convention has
three distinct named callbacks for three distinct reachability cases (success/insufficient-funds/
engine-teardown) — more exit surface than any single-tier store despite having no menu. Both files
now independently confirm the same `0x005eed90` handle-pair global's role (`store_crib.lua` never
reads it directly; this file's `Building_purchase_is_crib` cache is this project's first Lua-level
confirmation of what §16419 could previously only state from the native side).

## 7. Open items

1. Which of `crib_purchase_get_crib_info`'s two C-side registrations (`0x005f09a0`'s mismatched
   4-value/count-5 descriptor array vs. `0x00a03e40`'s correct 10-value array) actually governs at
   runtime — both point at the same handler (`0x005f0130`), so this likely has no Lua-visible
   effect, but the module-init load order between the two registrars was not resolved
   (`spec-lua-api-behaviour.md` §62.1).
2. `shop_purchase_get_shop_info`'s own shop resolver, `0x00a02ce0`, was read only at its call site,
   not independently traced to its own body — HIGH CONFIDENCE it is the same trigger-handle
   resolver family `shop_purchase_purchase_shop` already uses, not CONFIRMED (§62.2).
3. Whether `building_purchase_init`'s single `building_purchase_is_crib_being_purchased()` read can
   ever race with the engine changing which building is "selected for purchase" mid-screen (e.g. the
   player backing out and re-triggering a different building without this screen tearing down first)
   — not investigated; the Lua code gives no indication it re-reads the native, which is either fine
   (screen is always torn down and recreated per trigger) or a latent staleness hazard.
4. The `Vdo_city_control`/`Vdo_hint_button`/`Vdo_input_tracker` object-method internals used
   throughout §1/§4 were not traced to their own native bodies — out of scope for this file-level
   trace, same deferral `spec-store-runtime.md` §11 items 3/5/6 already use for analogous
   VDO-wrapper objects.

## City takeover economy — port recipe (2026-10-12, FINAL)

Static disassembly only, read-only project. This section joins up material that already sits in
`spec-save-format.md` §10.5/§12.4, `spec-tables-progression.md` (reward types 31/48/49/58/59,
`Gameplay_Constants/Cribs`) and `spec-lua-api-behaviour.md` §31. New material is marked NEW.

### T.1 Object model (three world-object lists on the world singleton `0x03171a64`)

| List (count / index array) | Object | Fields that matter |
|---|---|---|
| `+0x198` / `+0x190` | **takeover region** (class string `city_takeover_region`, 154 of them, save entry 22) | `+0x3a` flags: bit `0x02` = owned (the saved bit), bit `0x01` = synced copy; `+0x3c` float **weight = zone property `city_takeover_area`**; `+0x44/+0x48` polygon `city_takeover_points` (8-byte points); `+0x4c` parent hood; `+0x50` = hood's district; `+0x58/+0x5c` **linked gameplay object handle = property `city_takeover_gameplay`** (a shop, crib, flashpoint or activity); `+0x40` byte `city_takeover_winding`. Loader `0x005e9640`, which also adds the region's area into the parent hood's `+0x60`. NEW |
| `+0x114` / `+0x10c` | **hood** (neighbourhood zone) | zone properties read by `0x0084d120`: `hood_display_tag`, `hood_spawn_category`, `hood_alt_spawn_category`, `hood_flags`, `hood_island_type`, **`hood_income`** (float dollars, stored ×100 as an integer number of cents at `+0x58`), `hood_takeover_areas` (`+0x50`/`+0x54`), `hood_bsp_tree`; `+0x44` = parent district; `+0x60` = sum of the areas of its regions. NEW |
| `+0x180` / `+0x178` | **shop** | `+0x88` → its `shop_names.xtbl` row; `+0x8c` → its hood (and `+0x44` of that is the district); `+0x92` bit 0 = owned; `+0x94` float = income scalar. NEW |
| *(district)* | object at hood `+0x44` | recomputed lazily by `0x0084ccc0` while its dirty bit `+0x8c&1` is set: `+0x90` = sum of the areas of regions with bit `0x01`, **`+0x94` = sum of owned areas (bit `0x02`)**, `+0x98` = sum of all areas. Only regions that pass the region-kind gate and are not destroyed are counted. Zone property `district_income` exists (descriptor `0x01301acc`) but **the income routine never reads it** (OPEN: what consumes it). NEW |

`shop_names.xtbl` (loader `0x00a00cf0`, rows `Shop_Names`, row stride `0x34`, reader `0x00a00b90`), NEW:
`Name`, `Localized_Name`, `Ownership/Cost` → `+0x10` (dollars), **`Ownership/Income` → `+0x14`
(dollars per payout)**, `Ownership/Discount` → `+0x18` and `Ownership/Total_Owner_Discount` → `+0x1c`
(both stored scaled by a constant at `0x01117dc8`), `Ownership/Reward` → `+0x20`, `Use_Message`,
`Minimap_Icon`, `Shop_Type`. At run time `+0x08` is the number of shops in the franchise and `+0x0c`
the number owned (the discount tier in `spec-lua-api-behaviour.md` §62.2 compares them). The base
copy of `shop_names.xtbl` is not in `tools/ai_base_xtbl/`, so the per-shop Income values are OPEN.

### T.2 (a) Income per payout — `0x009ec4d0`, CONFIRMED (disassembly). NEW

All money uses the engine's cents type: set-raw `0x00960160` clamps to ±2·10⁹; add `0x00960190`
saturates the same way; set-dollars `0x009601d0` multiplies by 100 (dollar inputs beyond ±20,000,000
saturate).

```
total_cents = 0
for each hood H:
    owned  = 0
    denom  = H.area_sum                       // hood +0x60
    for each region R of H (hood_takeover_areas) that is live and region-kind:
        G = R.linked_gameplay_object
        if G is live, not destroyed, and of the SHOP kind:
            denom -= R.area                   // shop regions pay through the shop, not the hood
        else if R.owned:
            owned += R.area
    if denom > 0.01:                          // constant 0x012a2dd0
        total_cents += round( owned/denom * (H.income_cents / 100.0) ) * 100
for each shop S with S.owned:
    total_cents += round( S.income_scalar * S.row.Income ) * 100
total_cents += player.weekly_payments_dollars * 100      // player +0x27ac, u16
return total_cents
```

- Each hood's income is **pro-rated by the area fraction the player owns**, with whole-dollar rounding
  per hood. There is no separate district multiplier and no bonus for owning 100% of anything. The only
  multipliers are the per-shop scalar and the stronghold reward below.
- **Shop scalar `+0x94`**: reward type 59 `Crib_Cash_Stronghold_Scalar` (`0x00a01730`) adds
  `scalar` to `+0x94` of every shop whose hood's district equals the reward's `district`. In the base
  `unlockables.xtbl` the `Income_*` rows use `scalar` 0.05 per district (Downtown, Northwest,
  Southwest, …). The starting value of `+0x94` (presumably 1.0) was not traced (OPEN).
- **Weekly_Payments** (reward 31, case `0x1f` of `0x0071f170`) adds `Amount` to player `+0x27ac`
  (saved `0x4A6E`). It then **immediately credits the whole new income figure to the stash once**
  (`0x005eeec0`).

### T.3 (b) Payout timer and stash — `0x009ec800`, CONFIRMED (disassembly). NEW

1. **Interval** = tweak `Cash_payout_interval_minutes` (registered by `0x010079b0` into
   `0x0263b164`; base `tweak_table.xtbl` value **15**) × 60,000 ms. **Clock:** the engine millisecond
   timer `0x01320d94`. This is real elapsed time (it wraps at 1.8·10⁹), *not* the in-game time of day.
   The interval is not an in-game hour. Whether that counter stops while the game is paused was not
   checked (OPEN; see `spec-player-runtime.md` "Timers").
2. **Arming:** `0x009ec4b0`, called from `0x00708330` (the world start/load path), arms the timer for
   one interval. **Tick:** `0x009ec800` is called once per gameplay frame from the main update
   `0x00702a50`, on the branch that runs only when `0x00707490` returns false. When the timer has
   expired it computes T.2, calls `0x005eeec0(income)` and re-arms the timer for one interval.
3. **The cash goes to the crib stash, not the wallet.** `0x005eeec0` does
   `stash = min(stash + income, cap)`. The stash is global `0x014a0fe4` (cents; saved as
   `0x4F58`). `cap = round(Max_Stash[n] × limit_scalar)` dollars, where `n` is the number of owned
   cribs (`0x014a0f80` on host or single player, `0x00b97320` on a co-op client) and `Max_Stash` is
   `Gameplay_Constants/Cribs/Money_Storage` at `0x014a0f50`. Base values for n = 0…10:
   **0, 5000, 15000, 30000, 50000, 75000, 105000, 140000, 180000, 225000, 500000**. If n ≥ 11 the cap
   is 0. `limit_scalar` is `0x012ecb38`, a float that starts at 1.0. Reward type 58
   `Crib_Cash_Limit_Scalar` adds to it (`0x005eeb30`). **Income beyond the cap is lost.**
4. **"Stash nearly full" nag:** the cap is checked every frame. When the stash exceeds **75%** of it
   (constant `0x012a2ed8` = 0.75), the latch `0x0263b174` is still 0, and `0x005ef440` finds a crib
   (only on map `sr3_city`), nag row 15 `crib_stash` fires (`0x00705350(15, 0)`). The latch is never
   reset, so this happens once per session.
5. **Collection:** `crib_get_cash_stash` (from `store_crib_cash.lua:91`, the phone or crib "cash"
   screen) moves the stash into player cash (`+0x1ca0`, cents) and sets the stash to 0
   (`spec-lua-api-behaviour.md`, crib natives).

### T.4 (c) District ownership. NEW except where cited

- **What counts:** district % = owned region area ÷ all region area (`0x0084ce80` =
  `+0x94/+0x98`, 0 when the total is 0.01 or less). Weights are the regions' `city_takeover_area`,
  so regions do not count equally. The hood-level pause-map query does the same thing
  (`spec-lua-api-behaviour.md` §31.2). The purchase screen's control/new/increase percentages use
  `+0x90/+0x98` (§62.1/§62.2).
- **Capture:** region "captured" is `0x005e8b30`. It sets `+0x3a` bits, marks the hood/district dirty
  (`0x0084aeb0`), replicates opcode `0x45` sub-type `0x1d`, and recomputes statistics 58–61 (the four
  takeovers-completed counters), 29 (neighborhoods controlled) and 69 (cash earned per day).
  **`0x005e8fb0(gameplay id, flag)` captures every region whose `city_takeover_gameplay` equals that
  id.** It is called by: shop purchase `0x00a03490` (after charging `Cost`, it also sets the shop
  owned bit, raises the franchise owned count, raises stat 23 and recomputes stat 69 through
  `0x00a01680`); crib unlock `0x005f1cc0`; flashpoint completion `0x006a75e0`/`0x008d8420`;
  activity success `0x007be600`; and the network handler `0x005e9990`. A stronghold is captured
  through its crib/mission object in the same way. **A property, flashpoint or activity therefore
  adds exactly the area of the region(s) linked to it.**
- **Auto-complete (reverse direction), `0x005e97a0`:** used by reward 49
  `Auto_Complete_City_Takeover_All` (`0x0071e580`, every region) and by reward 48 (stag-map district
  pick, §31.5/§31.6). If the region is not owned, it is captured. Then, by the linked object's kind,
  the routine buys the shop for free (`0x00a01680`), completes the flashpoint (`0x008d81e0`), unlocks
  the crib (`0x005f1cc0`, gated by `0x005eeb10`), or completes the linked activity-kind object
  (`0x0061bb50`). It finishes with `0x008b85d0`, the co-op sync.
- **Effects at 100%: none found in the engine.** No capture-path routine changes a gang owner,
  spawn category or notoriety, or grants an unlock, when a district reaches 1.0. The only
  consequences are the statistics above and the higher income from T.2. **OPEN:** any gang or
  notoriety switch is either script-driven (the `on_district_changed` hook, `get_hood_owner`) or uses
  `hood_owner`/`hood_alt_spawn_category` in a path not traced here.

### T.5 (d) Reporting and display. NEW

- `get_world_income_dollars` (`0x007db9c0`, UI registrar): returns T.2's total ÷ 100 (integer
  division) as a Lua number. `pause_map.lua:392` shows it as "income per day".
- `store_crib_cash_request_data` (reply handler `0x0080e0e0`, bound by `0x0080e3ab`) replies with
  `{stash $, player cash $, income $ (T.2), transfer cap $}`. These match
  `store_crib_cash_populate(cash_from_bank, player_cash, daily_cash, transfer_cap)`.
- Statistic 69 "cash earned per day" (`0x00713785`) = T.2 ÷ 100 as a float. The `TUT_CASH_INCOME`
  tutorial text (`0x0071784a`) formats the same value.
- Every "per day" label refers to one payout. There is no game-day accumulation.

### T.6 Port recipe

1. Load the region, hood and shop objects with the zone properties in T.1. Build each hood's area
   sum, and each district's sums, from the regions.
2. Load the `Max_Stash` table, the `Cash_payout_interval_minutes` tweak (15) and the `Cost`/`Income`
   values from `shop_names.xtbl`.
3. Keep the state: the per-region owned bit (save entry 22), the stash (`0x4F58`), the weekly-payments
   value (`0x4A6E`), the per-shop scalar (re-applied from unlockables), the limit scalar (1.0 plus the
   reward-58 sums) and the owned-crib count.
4. On purchase or completion of any linked object, capture all regions with that gameplay id and mark
   their district dirty.
5. Run a real-time timer of `interval × 60000` ms. On expiry, compute T.2, then
   `stash = min(stash + income, cap)`, and re-arm.
6. Fire the `crib_stash` nag once when the stash exceeds 75% of the cap. Move the stash into the
   wallet only through `crib_get_cash_stash`.
7. Expose T.2 ÷ 100 through `get_world_income_dollars` and the crib-cash reply. Expose district %
   as owned area ÷ total area.

**OPEN:** the per-shop `Income` values (base `shop_names.xtbl` was not extracted); the starting
value of the shop scalar `+0x94`; whether the payout clock pauses with the game; any gang or
notoriety change at 100% (script side); what consumes `district_income`.

## 8. Clean-room compliance note

Plain hex addresses and real Lua identifiers (read directly from the extracted script source)
throughout. No Ghidra decompiler auto-names, no pseudocode, no leaked proprietary material.

## Changelog

- **2026-10-10 — new file: full document-level trace of `building_purchase.lua`, the crib/property
  acquisition flow, plus two newly-traced dataresponder-reply natives spliced into
  `spec-lua-api-behaviour.md` §62** (SPEC TEAM, agent `a6b4f2310e085a60a`). Resolves
  `spec-store-runtime.md` §10.3/§11 item 4's own open lead. Headline findings: both of the file's
  two info-dataresponder natives (`crib_purchase_get_crib_info`, `shop_purchase_get_shop_info`)
  share real null-pointer/null-dereference crash shapes with their already-documented
  purchase-native siblings (`crib_purchase_purchase_crib` §57.3, `shop_purchase_purchase_shop` line
  ~15761) — independently spot-checked against the Ghidra query server during this splice,
  confirmed; the crib info native is registered twice with mismatched descriptor-array sizes
  (runtime effect OPEN); the shop info native's description text is always the same fixed
  localization tag regardless of which shop. No new purchase-surface natives beyond the two
  already-documented purchase calls. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
  check 0 hits.
- **2026-10-12 — new section "City takeover economy — port recipe (2026-10-12, FINAL)"** (SPEC TEAM).
  It covers per-payout income computation (`0x009ec4d0`), the 15-minute real-time payout into the capped
  crib stash (`0x009ec800`/`0x005eeec0`), region/hood/district/shop zone properties, the capture
  paths (`0x005e8fb0`) and the display hooks. Scripts: `tools/scratchpad/Takeover2.java`,
  `Takeover5.java`. Cleanroom self-check: strict regex 0 hits.
