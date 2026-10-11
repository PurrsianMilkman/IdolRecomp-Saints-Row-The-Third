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
