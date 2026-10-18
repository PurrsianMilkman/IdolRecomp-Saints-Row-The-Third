// EngineState slice: stores, PCU/PCR customization, garage, crib, gang customization, shops.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateStoresData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateStoresData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// Sec27.5 `vcust_set_camera_pos`.
struct VcustCamera {
    OpenValue<bool> targetPresent{"current vehicle-customization target identity (0x00812e40)",
                                  "spec-lua-api-behaviour.md Sec27.5"};
    OpenValue<bool> targetValid{"target capability+class gate (+0x33 bit 0x10 clear AND class bit 0x80@+6 set)",
                                "spec-lua-api-behaviour.md Sec27.5"};
    OpenValue<bool> targetAlive{"0x00853b10 alive (true = alive; see Sec27.5's own resolved polarity note)",
                                "spec-lua-api-behaviour.md Sec27.5"};
    // The real CRC-32 of the preset name (sr3save::nameHash - CONFIRMED match to 0x00d9e8b0, see this
    // header's own top note); the 0x00a9b080 positioning math itself is OPEN and not simulated - this is
    // the one real, checkable number this project can reproduce exactly.
    uint32_t lastAppliedPresetHash = 0;
    bool isStandardActive = true; // 0x01300b88, CONFIRMED file value 1
};

// Sec27.9 `garage_preview_vehicle`.
struct GaragePreview {
    OpenValueMap<int32_t> vehicleTypeAtIndex{"0x014a1a80[index] (vehicle-type/model id table)",
                                             "spec-lua-api-behaviour.md Sec27.9"};
    OpenValue<int32_t> previewCache{"0x014a1d1c (cached preview model id)", "spec-lua-api-behaviour.md Sec27.9"};
    uint64_t refreshCount = 0; // times 0x005f8670 would have fired - internals OPEN, counted only
};

// Sec32.5 `shop_enable_nearest`: a test-only shop fixture (this project
// has no spatial/world-position system - the real "first within 15
// units, else closest within 75" distance search, and arg 1's own role
// in seeding the search origin, are not modeled; tests configure each
// candidate shop's own distance-tier flags directly). Iteration order =
// registration order, standing in for "the world object manager's shop
// sub-list" scan order.
struct Shop {
    bool withinFifteen = false;
    bool withinSeventyFive = false;
    bool disabled = true; // this project's own "closed by default" choice - no CONFIRMED initial state given
};

// --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec38/Sec39/Sec40
// ("ranking tranches 06/07/08") - see each field's own doc comment for
// citation/reasoning, and lua_spec_confirmed_stubs.cpp's own per-batch
// header comments for which of each tranche's 25 names this pass
// implements and why the rest stay generic stubs. -------------------

// vcust_preview_wheel_sizing (Sec38.5): the mandated crash guard. All 4
// vcust_preview_*/vcust_purchase_wheels entries share "no live
// customization target -> the vehicle pointer is zero, dereferenced
// unconditionally" (CONFIRMED, Sec38.5's own intro paragraph); this
// pass implements only vcust_preview_wheel_sizing (the one explicitly
// mandated), so this field is scoped to it alone - no real garage/
// customization-UI state exists elsewhere in this project to unify it
// with. See `targetLive`'s own doc comment just below for why this is
// a CHOSEN plain default rather than OPEN.
//
// UPDATE (Batch W2-5, 2026-10-08, wave 2 fan-out, spec-lua-api-
// behaviour.md Sec52.1): Sec52.1's own headline finding confirms this
// batch's own guess above was right in spirit - "only 2 of the 9
// vcust_* names in [tranche 15] actually check that a live
// 'customization vehicle' exists before using it ... a binding layer
// should simply treat every name in this family as 'requires a live
// customization vehicle' and refuse up front." `targetLive` is
// therefore now the SHARED gate every vcust_* stub in
// lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp checks
// before touching any of this struct's (or its own) further fields -
// not re-derived as a second, parallel flag. Its own default (false)
// and reasoning are UNCHANGED from the original note above.
struct VcustPreview {
    // CHOSEN default false, not OPEN: this project has no garage/
    // customization-UI subsystem anywhere in scope, so no code path
    // here ever establishes a live target - "false" is the honest
    // depiction of THIS host's own behavior (never live), not a
    // guess about the real engine's own variable at-rest state
    // (same "never-populated registry is the honest default"
    // precedent as EngineState::resolveTeamId()'s sentinel-9 default).
    // A test may still set this true to exercise the non-guarded path.
    bool targetLive = false;
    uint64_t wheelSizingNullTargetGuardCount = 0; // HOST-SAFETY, test-observable only - not a real engine field

    // --- Batch W2-5 additions below (2026-10-08): per-name HARDENING
    // refusal counters (HOST-SAFETY, test-observable only - not real
    // engine fields) for every further vcust_* name this wave-2 batch
    // implements against the SAME targetLive gate above. See each
    // stub_vcust_*'s own doc comment (b25 file) for which counter it
    // increments and its own spec citation. ---
    uint64_t previewColorNoTargetRefusedCount = 0;        // vcust_preview_color, Sec38.5
    uint64_t previewPaletteNoTargetRefusedCount = 0;      // vcust_preview_palette, Sec38.5
    // vcust_preview_palette's own test-populated "does this (zone,
    // palette) pair resolve" registry (no real per-zone palette-list
    // loader in scope) - keyed by "zone:palette" string.
    OpenValueMap<bool> zonePaletteCount{"vcust_preview_palette's own per-zone palette-list resolve",
                                        "spec-lua-api-behaviour.md Sec38.5"};
    uint64_t previewPaletteOobRefusedCount = 0;           // vcust_preview_palette's own unchecked zone/palette/colour indices, Sec38.5
    uint64_t purchaseWheelsNoTargetRefusedCount = 0;      // vcust_purchase_wheels, Sec38.5
    uint64_t adjustCameraAngleNoTargetRefusedCount = 0;   // vcust_adjust_camera_angle, Sec52.1
    uint64_t previewRimSizingNoTargetRefusedCount = 0;    // vcust_preview_rim_sizing, Sec52.1
    uint64_t previewRimSizingOobRefusedCount = 0;         // vcust_preview_rim_sizing's own unchecked combined size+style index, Sec52.1
    uint64_t purchaseComponentNoTargetRefusedCount = 0;   // vcust_purchase_component, Sec52.1
    uint64_t purchaseComponentStaleIndexRefusedCount = 0; // vcust_purchase_component's own named OOB finding, Sec52.1 (see selectedComponentItem below)
    uint64_t purchasePaintNoTargetRefusedCount = 0;       // vcust_purchase_paint, Sec52.1
    uint64_t purchasePaintNegativePriceRefusedCount = 0;  // vcust_purchase_paint's own "no price validation at all", Sec52.1
    uint64_t purchasePerformanceUpgradeNoTargetRefusedCount = 0; // vcust_purchase_performance_upgrade, Sec52.1
    uint64_t selectPaintSlotNoTargetRefusedCount = 0;     // vcust_select_paint_slot, Sec52.1
    uint64_t selectPaintSlotOobRefusedCount = 0;          // vcust_select_paint_slot's own unchecked slot index, Sec52.1
    uint64_t setUnderglowOobRefusedCount = 0;             // vcust_set_underglow's own unchecked palette-colour index, Sec52.1 (no target-gate per spec text - applies colour BEFORE charging either way)
    uint64_t showNosHydraulicWarningNoTargetRefusedCount = 0; // vcust_show_nos_hydraulic_warning, Sec52.1

    // vcust_purchase_component (Sec52.1 MEMSAFE - the batch's own named
    // finding): "a globally-tracked 'currently selected item' index...
    // file-initialized to -1 and is reset back to -1 on every exit from
    // this function - so calling it twice in a row, or calling it at
    // all before a fresh selection, computes an 'item' pointer 60 bytes
    // before the real item array [and can] charge[] that garbage price
    // to the player and then write[] a bit into the dword immediately
    // preceding the array - a real heap-corruption shape." HARDENED
    // here: modelled as a test-populated resolver (no raw array this
    // project sizes) - selectedComponentItemIndex is this project's own
    // mirror of the real stale/-1-reset global (CONFIRMED shape: -1 at
    // rest, and reset to -1 after every call below, win or lose);
    // selectedComponentItemPrice is populated ONLY by a prior, still-
    // fresh selection (no in-scope producer sets one - a test calls
    // EngineState::selectVcustComponentForTesting() to simulate "a
    // selection was just made"). A read against an unresolved/-1 index
    // is refused (no charge, no write) - exactly covering the real
    // bug's own two trigger conditions (double-call, or no prior
    // selection) without reproducing the OOB read+write itself.
    int32_t selectedComponentItemIndex = -1;
    OpenValueMap<double> selectedComponentItemPrice{
        "vcust_purchase_component's own 'currently selected item' price field (60 bytes before the real array on a stale/-1 index)",
        "spec-lua-api-behaviour.md Sec52.1"};
    uint64_t purchaseComponentChargedCount = 0;
    double purchaseComponentTotalCharged = 0.0;

    // vcust_change_top_level_menu (Sec52.1 CRASH - "a new 'poisons its
    // own next call' shape"): "An out-of-range absolute selection
    // stores null into the 'current top-level node' global
    // unconditionally - and the very next relative (+/-1) call
    // dereferences that same global with no check at all." HARDENED:
    // an out-of-range absolute selection REFUSES the store (leaves the
    // prior node alone) rather than poisoning it with null; a relative
    // call with no current node REFUSES rather than dereferencing it.
    OpenValue<int32_t> currentTopLevelNode{"vcust_change_top_level_menu's own 'current top-level node' global",
                                            "spec-lua-api-behaviour.md Sec52.1"};
    uint64_t changeMenuAbsoluteCount = 0;
    uint64_t changeMenuRelativeCount = 0;
    uint64_t changeMenuAbsoluteOutOfRangeRefusedCount = 0;   // HARDENED (named divergence)
    uint64_t changeMenuRelativeNoNodeRefusedCount = 0;       // HARDENED (named divergence)

    // vcust_preview_component (Sec19.11) / vcust_revert_color (Sec19.10)
    // / vcust_is_camera_tool_mode (Sec26.16): no CRASH flagged for these
    // 3 in scope - plain CHOSEN/OPEN state, same discipline as every
    // other field in this struct.
    int32_t previewedComponentIndex = -1;
    uint64_t previewComponentMode0Count = 0;
    uint64_t previewComponentMode2Count = 0;
    uint64_t previewComponentUnhandledModeCount = 0;
    double currentColorValue = 0.0;
    double originalColorValue = 0.0;
    bool colorRevertGateClear = true; // 0x022cf90b "gate flag clear" - CHOSEN default matching file-zero-fill, Sec19.10
    uint64_t revertColorCount = 0;
    OpenValue<bool> cameraToolMode{"vcust_is_camera_tool_mode backing accessor (0x006948c0)",
                                   "spec-lua-api-behaviour.md Sec26.16"};

    // --- Further per-name state the b25 stubs read/write directly
    // (HOST-SAFETY/CHOSEN fields, test-observable only - see each
    // stub_vcust_*'s own doc comment for citation). ---
    bool wheelPreviewCommitted = false;        // vcust_purchase_wheels: CONFIRMED "commits regardless of charge outcome"
    double cameraAngle = 0.0;                  // vcust_adjust_camera_angle
    uint64_t adjustCameraAngleCount = 0;
    int32_t rimSizingIndex = -1;                // vcust_preview_rim_sizing
    uint64_t previewRimSizingCount = 0;
    double paintTotalCharged = 0.0;             // vcust_purchase_paint
    uint64_t purchasePaintCount = 0;
    double performanceUpgradeTotalCharged = 0.0; // vcust_purchase_performance_upgrade
    uint64_t purchasePerformanceUpgradeCount = 0;
    int32_t selectedPaintSlot = -1;              // vcust_select_paint_slot
    uint64_t selectPaintSlotCount = 0;
    int32_t underglowColorIndex = -1;            // vcust_set_underglow
    uint64_t setUnderglowCount = 0;
    uint64_t showNosHydraulicWarningCount = 0;   // vcust_show_nos_hydraulic_warning
};

// store_weapon_purchase_ammo (Sec38.3) / shared by any future
// *_purchase_*-style entry: this project's own minimal "player purse"
// stand-in - no real currency/economy system anywhere in this project.
// CONFIRMED: no affordability check in store_weapon_purchase_ammo at
// all; it charges the price and records the purchase even when the
// item lookup FAILS (a confirmed real quirk, not a bug to guard
// against). `tag` matches the real notification mechanism's own
// literal string (Sec38.3: `"weapon-ammo"`); `itemResolved` is always
// false from this host (no real store-list table in scope - the
// honest default for a never-populated registry, same precedent as
// EngineState::resolveTeamId()/findVdoObject()).
struct PurchaseRecord {
    std::string tag;
    double amountCharged = 0.0;
    bool itemResolved = false;
};

// store_gang_show_question_marks / store_gallery_download_hide_list
// (Sec39.5): two further CONFIRMED-new null-dereference defects,
// guarded/logged rather than reproduced as crashes (this task's own
// explicit instruction - these are NOT the mandated faithful-quirk
// kind, they are plain original-game bugs this host must not
// simulate). store_gallery_download_hide_list's local-player half
// reuses the existing hasLocalPlayer() accessor above (the SAME
// "player 1 exists" concept cellphone_animate_start_do already reuses
// it for, Sec28.23) rather than inventing a second, separate local-
// player flag.
struct StorePreviewGuards {
    // CHOSEN default false, not OPEN, same reasoning as VcustPreview::
    // targetLive above: this project has no gang-customization-preview
    // or gallery-list subsystem anywhere in scope, so neither asset
    // ever resolves from this host's own default state. A test may
    // set either true to exercise the non-guarded path.
    bool gangPreviewAssetOrFallbackResolved = false;
    bool gangPreviewShowingQuestionMarks = false; // CHOSEN write-only toggle, this project's own stand-in
    uint64_t gangQuestionMarksNullGuardCount = 0;
    bool galleryListObjectResolved = false;
    uint64_t galleryHideListNullGuardCount = 0;
};

// store_common_rotate_mouse_drag (Sec39.5): "reads mouse-delta values
// from three fixed stack slots, but only copies the real values in
// when a specific input-mode bit is set ... when clear, the stack
// slots are never initialized at all ... rotate[s] ... by a garbage
// angle derived from that uninitialized stack memory. A faithful
// reimplementation must treat 'mouse delta unavailable' as zero, not
// skip the rotation or read whatever happens to be on the stack."
// CHOSEN (host-safety stand-in for undefined behaviour, not a spec
// fact, per this task's own explicit instruction): this host has no
// real mouse-delta-capture layer at all (not modeled anywhere else in
// this codebase either), so BOTH the "bit clear" case (real
// uninitialized memory) and the "bit set" case (a real value this
// host has no source for) land on the same explicit, defined 0 -
// never raw/undefined C++ memory, never an invented nonzero value.
struct StoreCommonRotateMouseDrag {
    double lastDeltaX = 0.0, lastDeltaY = 0.0, lastDeltaZ = 0.0;
    uint64_t callCount = 0;

    // store_common_mouse_zoom (Sec11.9, 0x0080dcd0, batch 3, 2026-10-08):
    // CONFIRMED to scale the SAME 3rd snapshot value this struct's own
    // lastDeltaX/Y/Z above already stand in for (0.0, CHOSEN - "this
    // project has no real mouse-delta-capture layer anywhere in scope,"
    // see this struct's own doc comment above) by a fixed -0.05
    // multiplier and the optional Lua argument, then accumulates into a
    // single global pending camera-zoom delta (0x00567c00). Under this
    // project's own CHOSEN 0 stand-in for the unavailable delta, the
    // accumulator's real numeric contribution is always 0 - tracked
    // anyway (rather than omitted) so a test can observe that the write
    // path itself genuinely ran, consistent with this project's
    // "accumulate even when the real contribution is a stated 0"
    // convention (cf. debrisFlowRecycleCount_ elsewhere in this header).
    double pendingZoomDelta = 0.0;
    uint64_t zoomCallCount = 0;
};

// pcu_is_bra_category / pcu_is_underwear_category (Sec40.5): "dereference
// a null category record when the index is out of range" (CONFIRMED
// real defect). This project's own minimal stand-in for the real
// category table: a test populates each known index's own "kind" tag
// directly (no real category table in scope) - an index a test never
// populated takes the same "out of range" guarded path a real
// out-of-bounds index would.
struct PcuCategoryTable {
    static constexpr int32_t kOther = 0, kBra = 1, kUnderwear = 2; // CHOSEN tags, this project's own enum
    OpenValueMap<int32_t> kindByIndex{"PCU category record kind, by index", "spec-lua-api-behaviour.md Sec40.5"};
    uint64_t categoryIndexOobGuardCount = 0;
};

// pcu_wear_store_outfit / pcu_purchase_outfit (Sec40.5/Sec40.6): one
// shared catalog-outfit list, this project's own minimal stand-in (a
// test populates each list position's own raw flag word directly - no
// real catalog table in scope). CONFIRMED real indexing inconsistency:
// pcu_wear_store_outfit numbers qualifying nodes by testing "any flag
// bit, excluding one specific bit" while pcu_purchase_outfit (and,
// out of this pass's own implemented scope, pcu_get_num_items_owned)
// number the SAME list by testing "bit 0 only" - two INDEPENDENT
// filters over one shared list, each with its own 0-based "Nth
// qualifying node" numbering; this header deliberately does NOT unify
// them into one shared counting helper (this task's own explicit
// instruction). WHICH single bit the text's own "excluding one
// specific bit" names is not given by address - read here as bit 0
// (0x1), the SAME bit pcu_purchase_outfit/pcu_get_num_items_owned
// test exclusively: a stated, reasoned interpretation of the text's
// own direct juxtaposition, NOT an independently confirmed address-
// level fact.
struct PcuCatalogOutfits {
    OpenValueMap<uint32_t> flagsByIndex{"PCU catalog-outfit node raw flag word, by list position",
                                        "spec-lua-api-behaviour.md Sec40.5/Sec40.6"};
};

// pcu_purchase_slot / pcu_purchase_outfit (Sec40.5): "both IGNORE the
// inventory-append function's 'full' return value - a player who is
// already at capacity (2048 items or 128 outfits) is charged the full
// price and receives nothing silently" (CONFIRMED real logic defect,
// the mandated faithful quirk). This project's own minimal stand-in:
// only COUNTS are tracked (no individual item/outfit identity - no
// real item/outfit catalog in scope beyond PcuCatalogOutfits above).
// Reuses its own `cashBalance` rather than playerCash() above - PCU is
// explicitly "local-player-only" (Sec40.5) and this project does not
// claim the two purchase systems (weapon-store cash vs. PCU cash)
// share one real wallet anywhere in scope.
struct PcuInventory {
    static constexpr int kOwnedItemCapacity = 2048;   // CONFIRMED
    static constexpr int kSavedOutfitCapacity = 128;  // CONFIRMED
    int ownedItemCount = 0;
    int savedOutfitCount = 0;
    double cashBalance = 0.0; // CHOSEN starting balance - no real initial value given anywhere in scope
    uint64_t purchaseSlotChargedWhileFullCount = 0;
    uint64_t purchaseOutfitChargedWhileFullCount = 0;
    int lastWornCatalogPosition = -1; // -1 = none worn yet; pcu_wear_store_outfit's own test-observable result
    // pcu_purchase_outfit's own test-observable resolution via its OWN
    // "bit 0 only" rule - kept as a SEPARATE field from
    // lastWornCatalogPosition above (not unified) specifically so a
    // test can directly compare the SAME raw Lua index resolving to
    // TWO DIFFERENT catalog-list positions across the two functions,
    // proving the mandated divergence rather than merely asserting it.
    int lastPurchasedOutfitCatalogPosition = -1;
};

// --- Batch 2026-10-03, spec-lua-api-behaviour.md Sec49 ("ranking
// tranche 16") - see each accessor's own doc comment below, and
// lua_spec_confirmed_stubs.cpp's own Sec49 batch header comment, for
// which of its 25 names this pass implements: the 8 explicitly
// mandated crash guards/faithful quirks, PLUS `spawning_boats` (the
// one name this batch's own fresh mission-drive run found a REAL
// measured hit for - every other name was zero real hits, the same
// outcome Sec38/Sec39/Sec40's own batch already found for their
// combined 75 names). ------------------------------------------------

// shop_purchase_purchase_shop (Sec49.1, one-name registrar
// `0x00a03600`, `ui`). No Lua arguments - the real function resolves
// "the" shop via an AMBIENT trigger-handle lookup (CONFIRMED: "a
// trigger-handle lookup that returns null when the trigger is stale
// or absent"), not a Lua-visible name/handle argument. This project
// has no spatial/trigger-overlap system (same boundary as
// shop_enable_nearest's own test-only Shop fixture above) -
// `triggerResolves` is this project's own CHOSEN plain stand-in for
// "the ambient lookup currently resolves to a shop," default false
// (the honest "nothing wired up" answer, same precedent as
// VcustPreview::targetLive above). CONFIRMED real defect pair
// (Sec49.1): a null shop pointer is dereferenced unconditionally on
// the very next read (guarded here, HOST-SAFETY, via
// nullShopGuardCount); separately, once a shop DOES resolve, every
// side effect except the one-time ownership bit (`owned`) - the
// payment, the trigger-disable, the owned-object broadcast, and the
// property fly-by - repeats on EVERY subsequent call with no "already
// purchased" guard. Implemented faithfully (NOT "fixed" into a
// once-only purchase) via the four *RepeatCount fields below, which
// increment on every resolved call regardless of `owned`.
struct ShopPurchase {
    bool triggerResolves = false;    // CHOSEN default false, see comment above
    bool owned = false;              // CONFIRMED: the ONE guarded bit
    double price = 0.0;              // CHOSEN, test-settable - no real price table in scope
    uint64_t nullShopGuardCount = 0; // HOST-SAFETY - test-observable only, not a real engine field
    uint64_t paymentRepeatCount = 0;        // CONFIRMED defect: repeats every resolved call
    uint64_t triggerDisableRepeatCount = 0; // CONFIRMED defect: repeats every resolved call
    uint64_t ownedBroadcastRepeatCount = 0; // CONFIRMED defect: repeats every resolved call
    uint64_t flyByRepeatCount = 0;          // CONFIRMED defect: repeats every resolved call
};

// store_stronghold_game_purchase_upgrade (Sec49.1, `ui`). No Lua
// arguments - same ambient-context situation as shop_purchase_
// purchase_shop above (`strongholdResolves`, CHOSEN default false).
// CONFIRMED (Sec49.1, reconciled 2026-10-03 against Sec8.27/Sec26.28):
// charges the stronghold's price UNCONDITIONALLY once a stronghold
// resolves (no affordability check - same shape as store_weapon_
// purchase_ammo's own unconditional charge, Sec38.3), then attempts a
// level-up whose own gate - a session exists AND this machine is host
// - is CONFIRMED false in single player (EngineState::coopLocalIsHost(),
// Sec8.27/Sec26.28's exhaustive negative check) - so the level-up
// silently no-ops, every time, in this host as in real single-player
// play: cash is always deducted, the stronghold level never changes.
// Separately CONFIRMED: "No local-player null check either" - a
// second, independent crash shape, guarded here (HOST-SAFETY) via
// hasLocalPlayer() (reused - the SAME "player 1 exists" concept
// store_gallery_download_hide_list's own guard already reuses,
// Sec39.5/Sec28.23).
struct StrongholdPurchaseUpgrade {
    bool strongholdResolves = false; // CHOSEN default false, see comment above
    double price = 0.0;              // CHOSEN, test-settable - no real price table in scope
    uint64_t chargeCount = 0;                // CONFIRMED: charged unconditionally once resolved
    uint64_t levelUpGateFailedNoopCount = 0; // CONFIRMED: session-exists-and-host gate false in single player
    uint64_t localPlayerNullGuardCount = 0;  // HOST-SAFETY - test-observable only, not a real engine field
};

// Shop/crib walk-in entry -> "press E" prompt -> document open.
//
// UPDATED 2026-10-11 (commit b8c17ec, same day as this task): the CRIB
// half of this is NO LONGER a stand-in - spec-store-runtime.md Sec2's
// "CLOSED, 2026-10-11" paragraph gives the full, every-hop-confirmed
// chain: press `0x52` -> `0x005b6870` (the real 22-entry bit-walk) ->
// `0x0111f2d0[8]` (`0x009da580`, prompt text `"HUD_USE_CRIB"`) ->
// `0x005eee70` -> `0x0080cc90(this=0x022ccef8)` - the already-named
// "store_crib" static object - -> `0x007b5fe0(this)`, the GENERIC
// per-object document-open call (the structural equivalent, for an
// arbitrary document object, of what `0x0083ECC0` does by hardcoded name
// specifically for `store_common`). SHOP's own entry was explicitly NOT
// separately identified that pass (no `"HUD_USE_STORE"`-named slot found
// among the ones sampled) - it almost certainly goes through the exact
// same `0x0080cc90`/`0x007b5fe0` generic-open pattern on the
// `store_common` static object instead, from a different, still
// unidentified array index (spec-store-runtime.md Sec2's own "cheap,
// bounded follow-up" framing) - so SHOP entry remains the CHOSEN
// stand-in this file originally landed, labeled per this project's
// established "CHOSEN, static search exhausted" convention (HANDOFF.md):
// TODO: real native trigger condition (which array index) unresolved for
// SHOP specifically, see spec-store-runtime.md Sec2.
//
// What neither half of this models (both before and after the above
// update) is the interaction-prompt table-walk/bit-availability
// machinery itself (`0x005b6870`'s own 22-entry bit-walk, or what sets
// bit 8/whichever shop bit in the player's `+0x2084` context word) -
// this project has no C++ implementation of that engine-side table at
// all (it is not Lua-bound). Instead, for BOTH shop and crib, this
// reuses the SAME two EXISTING generic mechanisms end to end: the
// trigger-volume overlap/geometry machinery just above
// (registerTriggerGeometry()/triggerGeometryMap()/
// TriggerVolumeContainsPoint(), already wired into tools/sr3_viewer.cpp's
// own per-frame trigger poll) for "is the player inside" (standing in,
// for both cases alike, for the still-unmodeled "+0x2084 bit N is set"
// condition - CHOSEN for both, this part did not change with b8c17ec),
// and recordHudPrompt() (Sec13.3's hud_prompt native, stub_hud_prompt in
// src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp) for "show the
// press-E prompt" - the SAME HUD-prompt-show call the vehicle-enter
// prompt documentation (spec-player-runtime.md Sec13.1) describes this
// engine using for its own "press E to enter a vehicle" case, reused
// here rather than inventing a second prompt-show path. What DID change
// with b8c17ec is which document name the press actually opens, and
// whether that open-target attribution is REAL or CHOSEN - see
// ShopCribTriggerKind and pressShopCribInteract() below.
//
// Deliberately does NOT encode on-disk trigger "kind" (spec-save-
// format.md Sec9.4: kind 3 = "crib purchase," for an UNOWNED crib only -
// the owned-crib entry kind is itself still unidentified per that
// section's own note) - a caller marks any trigger name it already knows
// is a walk-in shop or crib door via registerShopCribTrigger(),
// independent of which on-disk kind index (if any) it would carry; the
// kind this struct DOES care about is shop-vs-crib, because (post-
// b8c17ec) that now selects a genuinely different, differently-sourced
// open-target document name.
enum class ShopCribTriggerKind {
    kShop = 0, // opens "store_common" - CHOSEN target+trigger-index (see above)
    kCrib = 1, // opens "store_crib" - REAL confirmed target+chain (b8c17ec)
};

// customization_outfit_wear (Sec13.4): arg 1 is a data-table key hashed
// via sr3save::nameHash (CONFIRMED match to 0x00d9e8b0, see this
// header's own top comment) - NOT a live-object resolve. Bit 0x1
// (default on) applies locally + always replicates (Sec13.4's own
// text: the record-and-replicate call is entirely inside the apply
// delegate, keyed by the hash, no per-object double-gate at this
// level); bit 0x2 (default on) additionally applies to the co-op
// player representation, gated on the SAME playerRig().coopPlayerPresent
// this project already models for the identical real global
// (0x009df3d0, Sec13.29 item 8 / Sec14.31's own "dual local-player
// representation" finding - the SAME real global, reused directly, not
// re-derived).
struct CustomizationOutfitWearRequest {
    uint32_t hashedName = 0;
    double variant = 0.0;
    int64_t flags = 3;
    bool secondRepresentationApplied = false;
};

// customization_item_wear (Sec14.12) / customization_item_revert
// (Sec14.13): share the SAME dual local-player customization state as
// customization_outfit_wear above (Sec14.31's own cross-batch note) -
// the much larger PCU/PCR family (backlog) will need to look at this
// same modelling choice. Record-only (the real item/variant/color
// resolve chain and the 63-slot item list are OPEN/not modelled).
struct CustomizationItemWearRequest {
    std::string item, variant, color;
    bool immediateWear = true;
    int64_t flags = 3;
    bool refreshThumbnails = false;
    bool appliedPrimary = false;
    bool appliedSecondRepresentation = false;
};

// pcr_apply_morph_value/pcr_get_morph_value (Sec11.3/Sec11.4,
// 0x00834fe0/0x00832880): CONFIRMED resolver shape - arg 1 resolves
// against a small named session/context table (0x00831650) whose real
// CONTENTS this spec text does not supply (same "no real table data in
// scope" boundary as EngineState::resolveTeamId()'s team-name registry/
// PcuCategoryTable above - a never-populated registry is this project's
// own honest default, not a simplification of the resolve contract
// itself); once resolved, the morph-slot argument (arg 2, type-
// polymorphic: a number reads as an index, anything else as a category
// name) resolves the SAME way against a second, per-session table whose
// contents are equally not given. Both maps are test-populated (same
// convention as PcuCategoryTable::kindByIndex) - an unpopulated
// session/slot is the CONFIRMED "not found" case (Sec11.3's own text:
// the setter does nothing further; Sec11.4's own text: the getter
// returns zero Lua values), not an OPEN refusal, so OpenValueMap<bool>
// is used here purely as a test-populated presence registry via
// .known()/.get(), never via a direct .get() that could throw on a
// genuinely-unpopulated (not just "never resolved") key.
struct PcrMorphState {
    OpenValueMap<bool> sessionResolves{"pcr session/context resolves (0x00831650 case-insensitive scan)",
                                        "spec-lua-api-behaviour.md Sec11.3/Sec11.4"};
    OpenValueMap<bool> slotResolves{"pcr morph slot resolves (index-or-category-name scan)",
                                    "spec-lua-api-behaviour.md Sec11.3/Sec11.4"};
    // Keyed by pcrMorphKey(sessionLower, slotKey). CONFIRMED write
    // target: "writes the raw value into the live morph-value array" -
    // this project's own flat stand-in for the real singleton's +0x2088
    // array (0x009da4e0, this document's own front-matter addendum to
    // Sec11). Absent = never written (read side defaults 0.0, matching
    // the real getter's OWN "returns 0.0 if either pointer is null"
    // fallback, 0x009f5c00).
    std::unordered_map<std::string, double> values;
    // Sec11.3's own "mark this character as having a pending morph
    // recompute" dedup queue (0x00831e90, "splice-to-front-with-
    // membership-check, i.e. queue this, no-op if already queued") -
    // collapses losslessly to one idempotent bool per session, since
    // this project does not model a real queue draining mechanism
    // anywhere (nothing in scope ever reads this queue back).
    std::unordered_map<std::string, bool> pendingRecompute;
};

// pcu_is_streaming_done (Sec11.5, 0x0082ad90): CONFIRMED two
// independent pending-work checks ANDed together (both must be clear to
// report "done") - which real asset class each one tracks is OPEN
// (Sec11.5's own text), so both stay OpenValue, refusing honestly until
// a test sets them, rather than guessing a domain split.
struct PcuStreaming {
    OpenValue<bool> rowCounterPending{"pcu per-row streaming counter/flag pending (0x009f0aa0 row check)",
                                      "spec-lua-api-behaviour.md Sec11.5"};
    OpenValue<bool> listMembershipPending{"pcu circular-list membership pending (0x009f07d0)",
                                          "spec-lua-api-behaviour.md Sec11.5"};
};

// store_set_camera_pos (Sec11.10, 0x00830670): CONFIRMED resolve via a
// hash+linear-scan against a fixed 60-byte-stride preset table whose
// real row CONTENTS this spec text does not supply (same never-
// populated-registry convention as PcrMorphState above) - test-
// populated presence only; the deep camera-math/field-layout internals
// (3-way flag gate, per-row distance/pitch/yaw fields) are OPEN and not
// reproduced (this project has no camera/world-position system for the
// store/character-preview UI anywhere in scope).
struct StoreCameraPreset {
    OpenValueMap<bool> presetResolves{"store camera preset name table scan (0x00830670 hash+linear-scan)",
                                      "spec-lua-api-behaviour.md Sec11.10"};
    std::string lastAppliedPreset; // "" = none applied yet
    uint64_t appliedCount = 0;
};

// =====================================================================
// Batch W2-5 (2026-10-08), wave 2 of the parallel fan-out - Customization
// frontends: PCU/PCR/customization_* (src/lua_spec_confirmed_stubs_b24_
// wave2_pcu_pcr.cpp) + vcust/store/garage/crib/gang (src/lua_spec_
// confirmed_stubs_b25_wave2_vcust_store_garage.cpp). spec-lua-api-
// behaviour.md Sec19/Sec23/Sec26/Sec38-Sec60 (various ranking tranches -
// see each b24/b25 stub's own doc comment for its exact citation).
//
// HARDENING BATCH: this wave owns 28 crash-shaped findings (the most of
// any wave-2 batch), 4 of them the brief's own NAMED memory-safety bugs
// (gang_customization_confirm_gang_sign/_select_gang_sign/_select_
// vehicle, vcust_purchase_component - the last one's own state lives on
// VcustPreview above, extended in place rather than duplicated). EVERY
// one of the 28 is bound-checked/refused here, never reproduced - see
// each counter's own doc comment below for which stub increments it and
// the spec line it diverges from. Reuses wave-1 state where the SAME
// real mechanism is already modelled (VcustPreview::targetLive above,
// PcuInventory/PcuCategoryTable/PcuCatalogOutfits/playerCash()/
// purchaseLedger() below/above, PcrCustomizationState's existing 63-slot
// list) rather than re-deriving a parallel model - see each struct's own
// "reused from" note.
// =====================================================================

// customization_create_character / customization_creation_is_open /
// customization_screen_is_ready (Sec60.1).
struct CustomizationCreationState {
    // customization_create_character: CONFIRMED "runs the same 3-call
    // sequence the engine's own ... co-op character-creation flow runs,
    // ending in a step that reads the session pointer's +0x60 field
    // with no null test - any single-player call ... faults." HARDENED:
    // reuses the existing coopSession().present (CONFIRMED false by
    // default in single player, Sec8.27/Sec26.28 - applySpecInitialState
    // already seeds it) as the no-session refusal gate, rather than
    // inventing a separate flag.
    uint64_t noSessionRefusedCount = 0;
    uint64_t sessionPresentAttemptedCount = 0; // session-present path - deep internals OPEN, counted only
    // customization_creation_is_open: CONFIRMED "always returns true in
    // single player" - a real logic defect in the original (a polling
    // loop never exits) that this task's own brief states explicitly
    // IS the faithful behaviour here, not a bug to fix. No state
    // needed - the stub returns a literal true.
    uint64_t creationIsOpenQueryCount = 0;
    // customization_screen_is_ready: no body given in spec scope beyond
    // "resolves in full, no crash shape" (Sec60.1) - modelled as a
    // plain OPEN flag, not invented (same "mechanism confirmed, no body
    // given" convention as pcrPlasticSurgeonMode_ above).
    OpenValue<bool> screenIsReady{"customization_screen_is_ready backing flag", "spec-lua-api-behaviour.md Sec60.1"};
};

// PCR voice (Sec23.1 pcr_set_voice / Sec43.3 pcr_get_voice/pcr_cleanup_
// player_voice): +0x2088+0x10 cached voice id, -1 = none (CHOSEN
// sentinel). The 7-entry by-name resolver (0x009ed7a0) and the "Phone_
// Call" cue handle are test-populated/counted (no real voice-cue table
// loaded in scope).
struct PcrVoiceState {
    int32_t cachedVoiceIndex = -1;
    bool haveCachedVoice = false;
    OpenValueMap<bool> voiceTagResolves{"pcr_set_voice's own by-name voice resolver (0x009ed7a0), indices 0-6",
                                        "spec-lua-api-behaviour.md Sec23.1"};
    uint64_t playCurrentCount = 0;
    uint64_t resolvedAndAppliedCount = 0;
    uint64_t unresolvedTagCount = 0;
    uint64_t phoneCallHandleOpenCount = 0; // CONFIRMED: "Phone_Call" handle opened unconditionally once resolved, Sec23.1
    uint64_t cleanupCount = 0; // pcr_cleanup_player_voice - clears cachedVoiceIndex, replicated when host (Sec43.3)
};

// PCR triangle cursor (Sec19.3 pcr_set_triangle_cursor_pos / Sec43.3
// pcr_get_triangle_cursor_pos): CONFIRMED (Sec43.3's own correction)
// the setter TRUNCATES both values to an integer before storing -
// fractional positions are lost on write, not just on read.
struct PcrTriangleCursor {
    int32_t x = 0;
    int32_t y = 0;
    bool known = false;
};

// PCR report family (Sec19.5 pcr_report_composite_items, Sec23.3 pcr_
// report_composite_colors, Sec23.4 pcr_report_avail_morphs, Sec43.3
// pcr_report_hair_colors/_hair_items/_anims/_eye_colors/_skin_colors):
// every one dispatches a named Lua callback via the project's already-
// established "spawns a script coroutine" mechanism (Sec43.1 - this
// resolves the former "0x00e0xxxx event-report family" wording, Sec26.
// 23). This project has no real Lua-coroutine-dispatch model, and no
// in-scope producer for the underlying per-category/per-item records
// either, so every report function here is COUNTED (attempted, plus
// resolved/unresolved where the text gives a resolver), never actually
// invoking a callback - same "mechanism confirmed, no in-scope
// producer" convention as VintDataResponderRecord::dispatchAttempts/
// QteSlotEntry above.
struct PcrReportCounters {
    uint64_t compositeItemsReportCount = 0;
    uint64_t compositeColorsReportCount = 0;
    uint64_t availMorphsReportCount = 0;
    uint64_t hairColorsReportCount = 0;
    uint64_t hairItemsReportCount = 0;
    // pcr_report_hair_items (Sec43.3 CRASH): "Maps the category through
    // a 3-entry table with NO range check (unlike pcr_change_hair/pcr_
    // get_hair_info, which do)." HARDENED: bound-checked here (0-2),
    // matching its own sibling functions' existing convention.
    uint64_t hairItemsOobRefusedCount = 0;
    uint64_t animsReportCount = 0;
    uint64_t eyeColorsReportCount = 0;
    uint64_t skinColorsReportCount = 0;
    uint64_t reportCategoryUnresolvedCount = 0; // shared "category didn't resolve" counter across the whole family
};

// pcr_set_random_composite (Sec19.4): per-category eligible-row count -
// this project's own test-populated stand-in for the real per-category-
// definition table scan (0x0045b3a0 validity predicate, OPEN).
struct PcrRandomComposite {
    OpenValueMap<int32_t> eligibleRowCount{
        "pcr_set_random_composite's own per-category eligible-row scan (0x0045b3a0 validity predicate)",
        "spec-lua-api-behaviour.md Sec19.4"};
    uint64_t appliedCount = 0;
    uint64_t categoryUnresolvedCount = 0;
    uint64_t noEligibleRowsCount = 0; // CONFIRMED: an empty eligible set is a real, reachable no-op, not a crash
};

// pcr_apply_preset (Sec23.8) / pcr_apply_regional_preset (Sec43.3):
// named-preset/region resolvers - test-populated (no real preset-slot
// array or region table loaded in scope).
struct PcrPresetState {
    OpenValueMap<bool> presetResolves{"pcr_apply_preset's own named preset-slot scan (0x02300858)",
                                      "spec-lua-api-behaviour.md Sec23.8"};
    OpenValueMap<bool> regionResolves{"pcr_apply_regional_preset's own region/preset name resolvers",
                                      "spec-lua-api-behaviour.md Sec43.3"};
    uint64_t appliedCount = 0;
    uint64_t regionalAppliedCount = 0;
    // pcr_apply_regional_preset (Sec43.3 CRASH): "Neither the preset-
    // apply step nor the morph-apply step null-checks the local player,
    // so a valid region/preset pair with no local player crashes."
    // HARDENED: reuses hasLocalPlayer() as the refusal gate.
    uint64_t regionalNoLocalPlayerRefusedCount = 0;
};

// pcr_purchase_tattoo (Sec43.3 CRASH - "No sign check, so a negative
// price PAYS the player instead of charging them; no affordability
// check; no player null check."). HARDENED: refuses a negative price
// (the task brief's own explicit instruction) and reuses hasLocalPlayer()
// for the null-player half; reuses playerCash()/purchaseLedger() rather
// than a separate wallet (this is a cash-only transaction, Sec43.3).
struct PcrTattooPurchase {
    uint64_t chargedCount = 0;
    uint64_t negativePriceRefusedCount = 0; // HARDENED divergence (named in the task brief)
    uint64_t noLocalPlayerRefusedCount = 0; // HARDENED (unchecked local player)
};

// pcr_hair_randomize_all / pcr_lips_randomize (Sec43.3 CRASH, both
// divide-by-zero on an empty palette) / pcr_randomize_eye_colors
// (Sec43.3, data-gated 600-entry stack array, not a divide-by-zero).
// Palette sizes are test-populated (no real palette data loaded in
// scope) - HARDENED: an empty (or never-set) palette REFUSES the draw
// instead of dividing by zero.
struct PcrRandomizePalettes {
    OpenValue<int32_t> lipsColorPaletteSize{"pcr_lips_randomize's own lips-colour palette (PRNG divisor hi-lo+1)",
                                            "spec-lua-api-behaviour.md Sec43.3"};
    OpenValue<int32_t> hairColorPaletteSize{"pcr_hair_randomize_all's own hair-colour table (PRNG divisor hi-lo+1)",
                                           "spec-lua-api-behaviour.md Sec43.3"};
    uint64_t lipsAppliedCount = 0;
    uint64_t lipsEmptyPaletteRefusedCount = 0; // HARDENED divergence (real: divide-by-zero)
    uint64_t hairAppliedCount = 0;
    uint64_t hairEmptyPaletteRefusedCount = 0; // HARDENED divergence (real: divide-by-zero)
    uint64_t eyeColorsAppliedCount = 0;        // pcr_randomize_eye_colors - real selection internals OPEN, counted only
};

// pcr_change_skin_color (Sec19.15): CONFIRMED "bounds-checks the index
// against a live count global (0x02300850, itself gated on a non-null
// table pointer 0x0230084c)." Test-populated (no real skin-colour
// table loaded in scope) - HARDENED: refuses an unpopulated, negative,
// or out-of-range index. This is a DEDICATED field, not reused from
// PcrRandomizePalettes above - CORRECTED (adversarial review): a prior
// version of this stub's gate read pcrRandomizePalettes().
// hairColorPaletteSize (a HAIR-family field, the wrong category
// entirely, belonging to pcr_hair_randomize_all) and never actually
// compared the index against any bound at all (only checked "known"
// and "negative," no upper-bound comparison) - a real fidelity bug,
// not a memory-safety one (every write still lands inside this
// project's own fixed-size state).
struct PcrSkinColorChange {
    OpenValue<int32_t> tableSize{"pcr_change_skin_color's own skin-colour table count (0x02300850, gated on non-null 0x0230084c)",
                                 "spec-lua-api-behaviour.md Sec19.15"};
    uint64_t appliedCount = 0;
    uint64_t oobRefusedCount = 0; // HARDENED (fidelity fix): refuses an unpopulated/negative/out-of-range index - the prior version never checked the upper bound at all
};

// pcr_creation_complete (Sec43.3 CRASH - "if the [dialog] pool is
// exhausted, the allocator returns null and the caller dereferences it
// unchecked. In single player, 'ready' is always true (no remote player
// exists)."). HARDENED: refuses to open the waiting dialog when the
// (test-populated) pool reports exhausted.
struct PcrCreationComplete {
    OpenValue<bool> dialogPoolExhausted{"pcr_creation_complete's own fixed dialog pool (allocator result)",
                                        "spec-lua-api-behaviour.md Sec43.3"};
    uint64_t readyCount = 0;              // CONFIRMED: single player is always ready (no remote player, Sec43.3)
    uint64_t poolExhaustedRefusedCount = 0; // HARDENED divergence counter
};

// ---- PCU (clothing store/wardrobe) family, Sec19/Sec26/Sec40/Sec43 ----
// One worn-clothing slot, 0-23 (CONFIRMED range, Sec40.5) - the PCU-
// family per-character clothing list (0x022d0920-family), a SEPARATE
// storage from PcrCustomizationState's own 63-slot PCR composite-item
// list (Sec19.16's own explicit "do not conflate the two" caution).
struct PcuClothingSlot {
    int32_t itemId = -1; // -1 = empty (CHOSEN sentinel, same convention as PcrCustomizationItem)
    int32_t variantIndex = -1;
    int32_t colorIndex = -1;
    bool isNew = false; // owned "new" flag, byte +9 (pcu_clear_item_new, Sec43.2)
};
// pcu_category_nav_clear_slot (Sec43.2 CRASH): the 16-entry area
// descriptor array (0x022ffcf0) - CONFIRMED count, same array pcu_set_
// active_category/pcu_get_categories_in_area/pcu_get_areas (Sec19.1/
// Sec26.14/Sec26.15) also read - test-populated by name (no real area/
// category data loader in scope).
struct PcuAreaDescriptor {
    std::string name;
    bool known = false;
};
struct PcuClothingState {
    static constexpr int kSlotCount = 24;           // 0-23, CONFIRMED (Sec40.5)
    static constexpr int kBackpackSlot = 0x17;       // 23 (Sec40.5)
    static constexpr int kUnderwearSlot = 0x13;      // 19 (Sec40.5)
    static constexpr int kAreaDescriptorCount = 16;  // CONFIRMED bound (Sec43.2) - the HARDENING target
    std::array<PcuClothingSlot, kSlotCount> slots{};
    std::array<PcuAreaDescriptor, kAreaDescriptorCount> areas{};

    int32_t previewedSlot = -1; // 0x01300c6c sentinel (Sec19.2/Sec19.12/Sec19.13)
    bool listChanged = false;   // "list changed" tracking, cleared by pcu_discard_slot (Sec26.12)
    std::string activeAreaName; // 0x022ffd4c cached area name (Sec19.1)
    bool activeAreaIsBackpacks = false;
    bool wardrobeMode = false;  // 0x022ffd44 (Sec26.15)

    // --- pcu_set_active_category (Sec19.1) / pcu_clear_preview_slot
    // (Sec19.2) / pcu_get_slot_info+pcu_set_slot_info (Sec19.12/19.13)
    // / pcu_discard_slot (Sec26.12) / pcu_wear_current_clothing (Sec26.
    // 11) call counters - deep per-field internals OPEN past entry/
    // resolve checks (see each stub's own doc comment). ---
    uint64_t setActiveCategoryCount = 0;
    uint64_t clearPreviewSlotCount = 0;
    uint64_t getSlotInfoFoundCount = 0;
    uint64_t getSlotInfoNotFoundCount = 0;
    uint64_t setSlotInfoAppliedCount = 0;
    uint64_t setSlotInfoOobRefusedCount = 0; // HARDENED: slot/category id bound (< 0x18, Sec19.12's own CONFIRMED check reproduced, not a new divergence)
    uint64_t discardSlotCount = 0;
    uint64_t wearCurrentClothingCount = 0;

    // --- HARDENING counters (pcu_category_nav_clear_slot / pcu_create_
    // outfit - 2 of this batch's 28 crash-shaped findings live here). ---
    uint64_t categoryNavOobRefusedCount = 0;  // pcu_category_nav_clear_slot (Sec43.2 CRASH: 16-bit index, no bound/null check)
    int32_t categoryNavBackupSlot5Item = -1;  // single-level backup (Sec43.2's own "loses the original slot-5 item" note)
    bool categoryNavBackupValid = false;
    uint64_t createOutfitPoolExhaustedRefusedCount = 0; // pcu_create_outfit (Sec43.2 CRASH: null write on an empty block/node pool)
    uint64_t createOutfitCapRefusedCount = 0;           // the 128-saved-outfit cap (Sec43.2: real engine "orphans the already-allocated node" instead)
    int32_t nextCreatedOutfitIndex = 0;
    uint64_t deleteOutfitCount = 0;

    // pcr_add_default_clothing (Sec43.3): "Refreshes worn slot 10"
    // first (CONFIRMED real, distinct effect) before its destructive
    // owned-inventory wipe (see pcrAddDefaultClothingWipeCount()'s own
    // doc comment, and PcuInventory::ownedItemCount below - the
    // ACTUAL wipe target). The slot-10 refresh's own real garment
    // content is OPEN (no default-garment table loaded in scope, same
    // "no in-scope producer" gap the wipe's own re-add step has) - so
    // only the real, test-observable attempt is counted here, not
    // invented content; `slots[10]` itself is left untouched rather
    // than guessed at.
    uint64_t addDefaultClothingSlot10RefreshCount = 0;
};

// pcu_get_num_items_owned / pcu_get_owned_item_info / pcu_is_current_
// item_worn (Sec40.5/Sec40.6 - the 3 of the "15 pcu_* names" that ARE
// individually spelled out in spec scope, so no AGG marker). pcu_get_
// num_items_owned reuses the EXISTING pcuCatalogOutfits() (b04,
// Sec40.5/40.6) "bit 0 only" numbering rather than a new catalog model
// - CONFIRMED "numbers the identical catalog list by testing bit 0
// only," the SAME filter pcu_purchase_outfit already applies there.
struct PcuOwnedItemQueries {
    // pcu_get_owned_item_info (Sec40.5 CRASH): "dereferences a null item
    // record when the item index is out of range (once any item is
    // owned), and separately its variant search scans with <= instead
    // of < [a one-past-the-end read]." Test-populated per-item record
    // (no real owned-item array in scope) - HARDENED: an unresolved
    // index refuses (no push) rather than reading past the end.
    OpenValueMap<bool> ownedItemResolves{"pcu_get_owned_item_info's own per-item-index owned record",
                                         "spec-lua-api-behaviour.md Sec40.5"};
    uint64_t ownedInfoFoundCount = 0;
    uint64_t ownedInfoOobRefusedCount = 0; // HARDENED
    // pcu_is_current_item_worn (Sec40.6): "returns ZERO Lua values (not
    // a boolean) on one of its paths - a real return-arity irregularity
    // a caller must handle." Reproduced faithfully (not hardened away -
    // this is a return-shape quirk, not a memory-safety bug): the
    // "no item/slot resolved" path returns 0 values; a resolved slot
    // returns its worn boolean.
    uint64_t currentItemWornFoundCount = 0;
    uint64_t currentItemWornZeroValuesCount = 0; // CONFIRMED return-arity quirk, faithfully reproduced
};

// The 7 AGG pcu_* names (Sec40.5/Sec40.6 - aggregate-only, no individual
// per-function body given in spec scope): pcu_get_variants/pcu_get_
// wear_options/pcu_report_outfits are the family's own "pure Lua-
// callback iterators ... call back once per qualifying element and
// return nothing themselves" (Sec40.6); pcu_is_item_needed_for_outfit/
// pcu_remove_clothing/pcu_set_slots_empty/pcu_wear_outfit get only the
// generic "the id argument means a different index space per function"
// + "local player never null-checked" facts (Sec40.5). AGG discipline
// (this batch's own explicit instruction): implement only what the
// aggregate text supports - a thin, argument-consuming shell plus a
// call counter each, no invented per-function logic or return shape
// beyond what Sec40.5/40.6 actually states.
struct PcuAggregateCounters {
    uint64_t getVariantsCallbackDispatchCount = 0;
    uint64_t getWearOptionsCallbackDispatchCount = 0;
    uint64_t reportOutfitsCallbackDispatchCount = 0;
    uint64_t isItemNeededForOutfitQueryCount = 0;
    uint64_t removeClothingCount = 0;
    uint64_t setSlotsEmptyCount = 0;
    uint64_t wearOutfitCount = 0;
};

// pcu_bra_required / pcu_can_create_outfit / pcu_clear_item_new / pcu_
// clear_obscured_slots / pcu_delete_outfit / pcu_get_item_defaults /
// pcu_get_items_in_category / pcu_get_num_items_category (Sec43.2).
struct PcuCreatorMisc {
    // pcu_bra_required (Sec43.2): "local player byte +0xa41 == 1 (the
    // gender byte, Sec16.8 - 'player is female'). No player null check
    // (crash shape)." HARDENED: reuses hasLocalPlayer(); reuses the
    // EXISTING PcrCustomizationState::genderByte (Sec16.8, already
    // modelled) rather than a parallel gender field.
    uint64_t braRequiredQueryCount = 0;
    // pcu_can_create_outfit (Sec43.2): "1 or 3 values (ok, or false+
    // title+body for 'naked'/empty block pool). Incomplete guard -
    // doesn't check the free-node pool or the 128-saved-outfit cap."
    // Reproduced faithfully (the incompleteness itself is CONFIRMED
    // real behaviour, not a bug this project hardens away - the actual
    // pcu_create_outfit call this "guard" precedes IS separately
    // hardened above).
    OpenValue<bool> blockPoolEmpty{"pcu_can_create_outfit's own free-block-pool-empty test",
                                   "spec-lua-api-behaviour.md Sec43.2"};
    uint64_t canCreateOutfitOkCount = 0;
    uint64_t canCreateOutfitEmptyPoolCount = 0;
    uint64_t clearItemNewCount = 0;
    uint64_t clearObscuredSlotsCount = 0;
    OpenValueMap<bool> jobQueueHasRoom{"pcu_clear_obscured_slots's own job-queue-full test (kinds 6/7)",
                                       "spec-lua-api-behaviour.md Sec43.2"};
    uint64_t clearObscuredSlotsQueueFullCount = 0; // CONFIRMED: a full queue silently drops the job (not a crash)
    uint64_t getItemDefaultsOkCount = 0;
    uint64_t getItemDefaultsOutOfRangeCount = 0; // CONFIRMED: -1 return on out-of-range (not a crash by itself; the variant-0 null read is data-gated, not reproduced)
    // pcu_get_items_in_category (Sec43.2 CRASH - real infinite-loop
    // hang in "owned instances" mode on a bad/missing callback name or
    // a full 256-record coroutine table). HARDENED: this project never
    // actually dispatches a real Lua coroutine (Sec43.1 - "mechanism
    // confirmed, no in-scope producer"), so the hang's own trigger
    // condition cannot occur here; the 3 reporting modes are counted,
    // never looped.
    uint64_t itemsInCategoryMode0Count = 0; // "Outfits" by name
    uint64_t itemsInCategoryMode1Count = 0; // "owned instances" (the real hang's own mode)
    uint64_t itemsInCategoryMode2Count = 0; // "variants"
    uint64_t numItemsCategoryOwnedCount = 0;    // arg2 == -1 branch
    uint64_t numItemsCategoryCatalogCount = 0;  // arg2 != -1 branch
};

// ---- store_* family (Sec26/Sec38/Sec39/Sec49), b25 file ----
struct StoreFrontendState {
    // store_dlc_make_purchase (Sec26.8): CONFIRMED hardcoded stub -
    // "always reports a purchase result of exactly 0, for any DLC id,
    // and never performs any actual purchase, lookup, or network
    // call." Reproduced faithfully (not a crash, not hardened).
    uint64_t dlcMakePurchaseCount = 0;
    // store_weapon_purchase_weapon (Sec26.7): reuses playerCash()/
    // purchaseLedger() for the grant path; the 9-entry category table
    // is CONFIRMED (Sec26.7's own literal table) and reproduced here.
    OpenValueMap<bool> weaponOwnedByCategoryIndex{
        "store_weapon_purchase_weapon's own per-(category,index) ownership predicate (0x005ee650)",
        "spec-lua-api-behaviour.md Sec26.7"};
    uint64_t weaponPurchaseGrantedCount = 0;
    uint64_t weaponPurchaseAchievementSweepCount = 0;
    uint64_t weaponPurchaseUnmatchedCategoryCount = 0;
    // store_weapon_change_weapon (Sec38.3 CRASH - "A category match
    // selects one of two fixed arrays by item index with no bounds
    // check at all - an arbitrary script-supplied index reads an
    // arbitrary nearby dword as an item pointer."). CHOSEN: reuses the
    // SAME 9-entry category table store_weapon_purchase_weapon
    // documents (both are weapon-store UI siblings; the real per-
    // category array sizes are not independently given in scope, so
    // this project test-populates "does (category,index) resolve" and
    // refuses an unresolved pair - HARDENING the unchecked index
    // without inventing a fixed capacity the spec doesn't give).
    OpenValueMap<bool> weaponChangeSlotResolves{"store_weapon_change_weapon's own per-(category,index) preview slot",
                                                 "spec-lua-api-behaviour.md Sec38.3"};
    uint64_t weaponChangeCount = 0;
    uint64_t weaponChangeOobRefusedCount = 0; // HARDENED
    bool postBgCovered = false; // store_weapon_process_post_bg_covered (Sec38.3) - Lua-side-only state, no engine consumer in scope

    // store_vehicle_* (Sec38.4).
    int32_t vehicleStoreMode = 0; // CONFIRMED: 0 = take-over-pending, any-other = store-current (sets mode to literal 1 on success, Sec38.4)
    uint64_t vehicleChangeModeCount = 0;
    uint64_t vehicleRetrieveCarAndExitCount = 0;
    OpenValue<bool> selectionLocksControls{"store_vehicle_selection_should_lock_controls backing query",
                                           "spec-lua-api-behaviour.md Sec38.4"};

    // Sec39.5 UI cluster.
    bool characterLineupLoaded = false; // store_character_lineup_loaded, 0x022ccc52
    OpenValue<int32_t> clothingStoreId{"store_clothing_get_store_id's own un-opened callee result (0x0080d810)",
                                       "spec-lua-api-behaviour.md Sec39.5"};
    uint64_t dlcIsOfferFreeCount = 0; // store_dlc_is_offer_free, NOOP (CONFIRMED always false)
    uint64_t dlcQueueIconCount = 0;   // store_dlc_queue_icon, NOOP (CONFIRMED no side effects, no return)

    // Sec52.1/T15 AGG.
    uint64_t vehicleNoVehicleExitCount = 0;
    uint64_t vehicleNotifyScreenCoveredCount = 0;

    // Sec49.1/T16.
    uint64_t cribInitCribGarageCount = 0;
    OpenValueMap<bool> galleryCharacterIndexResolves{"store_gallery_display_character's own bounds-checked list-index",
                                                     "spec-lua-api-behaviour.md Sec49.1"};
    uint64_t galleryDisplayCharacterAppliedCount = 0;
    uint64_t galleryDisplayCharacterOobRefusedCount = 0;
    uint64_t galleryDownloadShowListCount = 0;
    // store_gallery_download_show_list (Sec49.1 CRASH): "reuses the
    // exact null-dereference shape tranche 09 B.12 already found: the
    // dialog-pool allocator can return null on exhaustion." HARDENED:
    // same test-populated dialogPoolExhausted convention as
    // pcrCreationComplete() above.
    OpenValue<bool> downloadDialogPoolExhausted{"store_gallery_download_show_list's own dialog-pool allocator result",
                                                "spec-lua-api-behaviour.md Sec49.1"};
    uint64_t galleryDownloadShowListPoolExhaustedRefusedCount = 0; // HARDENED
    uint64_t galleryUploadScheduledCount = 0; // store_gallery_upload_character - "a deferred-upload scheduler"
    uint64_t gangBeginExitCount = 0;
    uint64_t gangBgCoveredCount = 0;
    OpenValue<bool> gangUnlockedGate{"store_gang_is_unlocked's own player cash/unlock-bit gate", "spec-lua-api-behaviour.md Sec49.1"};
    uint64_t gangIsUnlockedQueryCount = 0;
    uint64_t strongholdUpgradeEndFlybyCount = 0;
    // store_stronghold_upgrade_end_flyby (Sec49.1 CRASH): "collects
    // every 'unlocked at this level' entry into a 256-slot stack array
    // with no cap on the match count." HARDENED: caps the collection at
    // 256 explicitly rather than reproducing the unbounded write.
    static constexpr int kUnlockedAtLevelCapacity = 256;
    uint64_t strongholdUpgradeEndFlybyOverflowRefusedCount = 0; // HARDENED
    OpenValueMap<bool> garageAllowDenylist{"store_vehicle_allow_garage's own 13-named-mission denylist",
                                           "spec-lua-api-behaviour.md Sec49.1"};
    uint64_t vehicleAllowGarageCount = 0;
    uint64_t vehicleDoReturnToCribCount = 0; // "screen-stack pop with a confirmed-dead secondary branch"
};

// ---- garage_* family (Sec23.10/Sec41.7/Sec53.4), b25 file ----
struct GarageEntry {
    bool present = false;
    int32_t garageType = 0;
};
struct GarageFrontendState {
    static constexpr int32_t kEntryCapacity = 168; // CONFIRMED 168-slot array (Sec53.4, correcting Sec27.9's "unbounded" note)

    // "Currently selected garage vehicle" - two-part identity pair
    // (0x014a1d28/0x014a1d2c, Sec23.10), DISTINCT from GaragePreview's
    // own 0x014a1d1c/0x014a1a80[index] above (an adjacent but separate
    // mechanism, Sec23.10's own "new global pair, not previously
    // catalogued") - test-populated (no real garage/vehicle-pool
    // loader in scope).
    bool hasSelectedVehicle = false;
    OpenValue<double> selectedVehicleRepairCost{"garage's own currently-selected-vehicle repair cost (0x00a9bbe0)",
                                                "spec-lua-api-behaviour.md Sec23.10"};
    uint64_t repairVehicleChargedCount = 0;
    double repairVehicleTotalCharged = 0.0;
    // garage_repair_vehicle (Sec41.7 CRASH): "if that handle is unset,
    // stale, dead, or not actually a vehicle, the vehicle pointer is
    // left null - and the cost-calculation callee dereferences it
    // unconditionally." HARDENED: refuses (no charge, no repair) when
    // hasSelectedVehicle is false, matching the sibling garage handler
    // the spec itself cites as the correct fix template.
    uint64_t repairVehicleNoSelectionRefusedCount = 0; // HARDENED

    std::array<GarageEntry, kEntryCapacity> entries{};
    int32_t entryCount = 0;       // CONFIRMED "count" field garage_back_to_customize's own sibling bounds correctly (< count)
    int32_t selectedEntryIndex = -1;

    uint64_t clearNewCount = 0;
    // garage_clear_new (Sec53.4 CRASH): "the bound check accepts
    // index == count, not just indices strictly below it." HARDENED:
    // uses a strict < bound, matching the sibling garage_back_to_
    // customize's own correct form (the spec's own cited fix template).
    uint64_t clearNewOobRefusedCount = 0; // HARDENED
    uint64_t backToCustomizeCount = 0;
    uint64_t backToCustomizeOobRefusedCount = 0;
    OpenValueMap<bool> canCustomizeMissionDenylist{"garage_can_customize's own 12-mission denylist (0x005f87c0)",
                                                   "spec-lua-api-behaviour.md Sec53.4"};
    uint64_t canCustomizeQueryCount = 0;
    uint64_t canDeleteQueryCount = 0;
    uint64_t removeVehicleCount = 0;
    // garage_remove_vehicle (Sec53.4 CRASH - "a real double-release
    // logic defect ... never clears the 'selected entry' global ... A
    // second call before a fresh selection ... hands the same record
    // back to its free pool a second time."). HARDENED: refuses a
    // second call when selectedEntryIndex is already -1 (cleared after
    // the first successful removal, unlike the original).
    uint64_t removeVehicleDoubleReleaseRefusedCount = 0; // HARDENED
    uint64_t vehicleLoadPendingQueryCount = 0;
};

// ---- crib_* family (Sec23.19/Sec57.3) + building_purchase_is_crib_
// being_purchased (Sec59, AGG), b25 file ----
struct CribFrontendState {
    // crib_has_garage_with_vehicles_of_type (Sec23.19): player's current
    // crib record (+0x209c, corrected per Sec23.19's own in-place
    // correction) - test-populated (no real crib/garage-vehicle-list
    // loader in scope).
    bool hasCribRecord = false;
    OpenValueMap<int32_t> garageVehicleCountByType{"crib garage-vehicle-list count by vehicle type (0x005facf0)",
                                                    "spec-lua-api-behaviour.md Sec23.19"};
    uint64_t hasGarageWithVehiclesTrueCount = 0;
    uint64_t hasGarageWithVehiclesFalseCount = 0;

    // crib_get_cash_stash (Sec57.3): naming trap - "returns no value at
    // all; it instead collects a stashed-cash global into the player's
    // cash balance and zeroes the stash" - reproduced faithfully
    // (0 Lua values, not the stash amount); reuses playerCash().
    double cashStash = 0.0;
    uint64_t cashStashCollectedCount = 0;
    uint64_t cashStashNoLocalPlayerForwardCount = 0; // CONFIRMED: forwarded (and discarded) when not locally applicable - not a crash

    OpenValue<bool> garageDisabled{"crib_is_garage_disabled backing flag", "spec-lua-api-behaviour.md Sec57.3"};
    OpenValue<bool> isStronghold{"crib_is_stronghold's own upgrade-level hash set (+0x148/+0x14c/+0x150)",
                                 "spec-lua-api-behaviour.md Sec57.3"};

    // crib_purchase_purchase_crib (Sec57.3 CRASH - "if nothing is
    // validly selected ... the crib pointer is left null and the VERY
    // NEXT INSTRUCTION reads price through it ... the player's cash is
    // ALWAYS DEDUCTED BEFORE the function checks whether the crib is
    // already owned."). Test-populated selection (no real crib-
    // selection/purchase-screen loader in scope). HARDENED: refuses
    // (no charge at all) when nothing is validly selected - the charge-
    // before-ownership-check quirk ITSELF is preserved faithfully for a
    // valid selection (it's a logic defect, not a memory-safety one;
    // the task's hardening mandate targets the null-pointer crash, not
    // this ordering).
    bool hasSelectedPurchaseCrib = false;
    OpenValueMap<double> cribPurchasePrice{"crib_purchase_purchase_crib's own selected-crib price field",
                                           "spec-lua-api-behaviour.md Sec57.3"};
    bool cribAlreadyOwned = false;
    uint64_t purchaseCribChargedCount = 0;
    double purchaseCribTotalCharged = 0.0;
    uint64_t purchaseCribNoSelectionRefusedCount = 0; // HARDENED (named CRASH)

    uint64_t purchasingUnlockCount = 0;          // crib_purchasing_unlock, AGG
    uint64_t isCribBeingPurchasedQueryCount = 0; // building_purchase_is_crib_being_purchased, AGG
};

// ---- gang_customization_* family (Sec53.1/Sec56.1), b25 file - 4 of
// the batch's own 4 NAMED memory-safety findings live here. ----
struct GangCustomizationState {
    // --- Vehicle family: 3-slot array, CONFIRMED bound (Sec53.1: "a
    // binding layer must reject any slot outside 0-2 for every
    // gang_customization_*_vehicle name - the single clearest 'accept
    // exactly this tiny range' fix this whole series has produced"). ---
    static constexpr int kVehicleSlotCount = 3;
    std::array<int32_t, kVehicleSlotCount> vehicleSlotItemIndex{{-1, -1, -1}}; // -1 = empty
    // Preview-release baseline. -1/unset + previewedVehicleBaselineSet
    // == false = "nothing previewed yet" (CONFIRMED zero-fill default -
    // this is the ONE case Sec56.1's 2nd crash fires on: revert_vehicle
    // running before the baseline is EVER set writes null into the slot
    // and hands it to a null-unsafe callee - see revert_vehicle's own
    // doc comment). previewedVehicleBaselineStale models a SEPARATE,
    // not-yet-hardened bug (Sec53.1's OWN 2nd bug, in select_vehicle's
    // preview-release path: "used to resolve, now doesn't" keeps using
    // a zeroed working pointer anyway and writes a flag bit through
    // null+0x16c0) - CONFIRMED a different function and a different
    // condition from revert_vehicle's null-baseline crash; revert_
    // vehicle must NOT key its own guard on this field (a prior version
    // of this stub did, incorrectly - see its doc comment).
    int32_t previewedVehicleBaselineIndex = -1;
    bool previewedVehicleBaselineSet = false;
    bool previewedVehicleBaselineStale = false; // reserved for the SEPARATE select_vehicle preview-release bug above - not yet hardened anywhere, not read by revert_vehicle
    uint64_t selectVehicleCount = 0;
    uint64_t selectVehicleOobRefusedCount = 0;          // HARDENED (named finding, Sec53.1)
    uint64_t confirmVehicleCount = 0;
    uint64_t confirmVehicleOobRefusedCount = 0;         // HARDENED (Sec53.1/Sec56.1)
    uint64_t revertVehicleCount = 0;                    // genuine, real reverts ONLY - never incremented on a refusal
    uint64_t revertVehicleOobRefusedCount = 0;          // HARDENED (Sec53.1/Sec56.1)
    uint64_t revertVehicleNullBaselineRefusedCount = 0; // HARDENED (THE NAMED FIX, Sec56.1's 2nd crash: baseline never set at all -> null write into the slot -> null-unsafe preview-vehicle-spawner callee at +0x24)

    // --- Gang sign family: position-to-record map, bound genuinely
    // OPEN (Sec56.1's own residual "the declared size of the sign
    // position-to-record map" item - NOT invented here) - test-
    // populated resolver, refused when a position does not resolve.
    // Mode 1 of select_gang_sign is a CONFIRMED DETERMINISTIC null-
    // vtable call in the original (not data-gated: "every mode-1 call
    // crashes, full stop") - refused unconditionally here (HARDENED,
    // named finding), never reproduced under any input. ---
    OpenValueMap<bool> signPositionResolves{"gang-sign position-to-record map (declared size OPEN, Sec56.1)",
                                            "spec-lua-api-behaviour.md Sec56.1"};
    int32_t confirmedSignPosition = -1;
    bool confirmedSignPositionSet = false;
    uint64_t selectGangSignMode0Count = 0;
    uint64_t selectGangSignMode0UnresolvedRefusedCount = 0; // HARDENED
    uint64_t selectGangSignMode1RefusedCount = 0;           // HARDENED (named finding - deterministic crash, never reproduced under any input)
    uint64_t selectGangSignUnhandledModeCount = 0;
    uint64_t confirmGangSignCount = 0;
    uint64_t confirmGangSignUnresolvedRefusedCount = 0; // HARDENED (named finding, Sec56.1: "a single out-of-range call can corrupt a save slot")
    uint64_t revertGangSignCount = 0;
    uint64_t revertGangSignNoPriorBuildRefusedCount = 0; // HARDENED (deferred null shape, Sec56.1)
};

struct EngineStateStoresData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using VcustCamera = sr3luahost::VcustCamera;
    using GaragePreview = sr3luahost::GaragePreview;
    using Shop = sr3luahost::Shop;
    using VcustPreview = sr3luahost::VcustPreview;
    using PurchaseRecord = sr3luahost::PurchaseRecord;
    using StorePreviewGuards = sr3luahost::StorePreviewGuards;
    using StoreCommonRotateMouseDrag = sr3luahost::StoreCommonRotateMouseDrag;
    using PcuCategoryTable = sr3luahost::PcuCategoryTable;
    using PcuCatalogOutfits = sr3luahost::PcuCatalogOutfits;
    using PcuInventory = sr3luahost::PcuInventory;
    using ShopPurchase = sr3luahost::ShopPurchase;
    using StrongholdPurchaseUpgrade = sr3luahost::StrongholdPurchaseUpgrade;
    using ShopCribTriggerKind = sr3luahost::ShopCribTriggerKind;
    using CustomizationOutfitWearRequest = sr3luahost::CustomizationOutfitWearRequest;
    using CustomizationItemWearRequest = sr3luahost::CustomizationItemWearRequest;
    using PcrMorphState = sr3luahost::PcrMorphState;
    using PcuStreaming = sr3luahost::PcuStreaming;
    using StoreCameraPreset = sr3luahost::StoreCameraPreset;
    using CustomizationCreationState = sr3luahost::CustomizationCreationState;
    using PcrVoiceState = sr3luahost::PcrVoiceState;
    using PcrTriangleCursor = sr3luahost::PcrTriangleCursor;
    using PcrReportCounters = sr3luahost::PcrReportCounters;
    using PcrRandomComposite = sr3luahost::PcrRandomComposite;
    using PcrPresetState = sr3luahost::PcrPresetState;
    using PcrTattooPurchase = sr3luahost::PcrTattooPurchase;
    using PcrRandomizePalettes = sr3luahost::PcrRandomizePalettes;
    using PcrSkinColorChange = sr3luahost::PcrSkinColorChange;
    using PcrCreationComplete = sr3luahost::PcrCreationComplete;
    using PcuClothingSlot = sr3luahost::PcuClothingSlot;
    using PcuAreaDescriptor = sr3luahost::PcuAreaDescriptor;
    using PcuClothingState = sr3luahost::PcuClothingState;
    using PcuOwnedItemQueries = sr3luahost::PcuOwnedItemQueries;
    using PcuAggregateCounters = sr3luahost::PcuAggregateCounters;
    using PcuCreatorMisc = sr3luahost::PcuCreatorMisc;
    using StoreFrontendState = sr3luahost::StoreFrontendState;
    using GarageEntry = sr3luahost::GarageEntry;
    using GarageFrontendState = sr3luahost::GarageFrontendState;
    using CribFrontendState = sr3luahost::CribFrontendState;
    using GangCustomizationState = sr3luahost::GangCustomizationState;

public:
    VcustCamera& vcustCamera() { return vcustCamera_; }
    GaragePreview& garagePreview() { return garagePreview_; }

    // Sec28.13 `crib_weapon_add_enable` / Sec28.14 `crib_weapon_add_disable`: 0x012ecb34, CONFIRMED file value 1.
    bool& cribWeaponAddEnabled() { return cribWeaponAddEnabled_; }

    // =====================================================================
    // Ranking tranche 04 (spec-lua-api-behaviour.md Sec32), 25 names - batch
    // 2026-10-02. Every name below resolves through this project's existing
    // objectResolves() shared-resolver simplification (same "one-map-for-
    // all-resolvers" convention the Sec27/Sec28 batch already established) -
    // no new resolver concept is introduced.
    // =====================================================================

    // Sec32.1 `store_interface_is_active`: 0x022cce86 bit 0x1. OPEN writer
    // (7 direct references, all reads, per spec - not traced).
    OpenValue<bool>& storeInterfaceActive() { return storeInterfaceActive_; }
    std::vector<Shop>& shops() { return shops_; }
    uint64_t& shopEnableRecordCount() { return shopEnableRecordCount_; } // the host-replicated opcode 0x45 sub-tag 7 record; no host/authority layer modeled, counted unconditionally
    VcustPreview& vcustPreview() { return vcustPreview_; }
    void recordVcustPreviewWheelSizingNullTargetGuard() { ++vcustPreview_.wheelSizingNullTargetGuardCount; }
    double& playerCash() { return playerCash_; }
    std::vector<PurchaseRecord>& purchaseLedger() { return purchaseLedger_; }

    // New-game-with-no-save starting cash, raw units (cash x100, matching
    // the same x100 scale as cash_add/playerCash() elsewhere and the
    // save-file field's own encoding, spec-save-format.md Sec10.3/§10.1).
    // spec-frontend-menu-runtime.md Sec9 item 1: the real engine's own
    // write-site for this default is NOT confirmed - two independent search
    // passes (data: m01.lua/player_presets.xtbl/xtbl corpus census; engine:
    // both cash-setter layers' full caller lists + the player-record
    // constructor chain down to its placement constructor) came up empty.
    // This is Team B's CHOSEN application point (EngineState's own default-
    // constructed value, used whenever no save snapshot gets applied via
    // applySaveSnapshotFields()/lua_save_game_bridge.cpp, i.e. the "new
    // game, no save present" case this project already distinguishes from
    // "continue"), not a confirmed reproduction of the real engine's
    // mechanism. Value: 5700 raw (= $57.00), spec-frontend-menu-runtime.md
    // Sec9 item 1 / spec-save-format.md line ~736 ("new game 5,700 (=
    // 57.00)").
    static constexpr double kNewGameStartingCashCents = 5700.0;

    // New-game-with-no-save auto-granted unlocks (spec-frontend-menu-
    // runtime.md Sec9 item 1, CLOSED 2026-10-11): the real base
    // unlockables.xtbl has exactly ONE row with <Auto_Unlock> set at all
    // (HomieSlot1, Category=Homies) - the same population spec-save-
    // format.md Sec10.6's own empirical finding gives for the new-game
    // save's state set A (purchased/active): exactly 1 bit. HIGH
    // CONFIDENCE, not disassembly-confirmed end-to-end (the exact runtime
    // function that reads the parsed <Auto_Unlock> enum at +0xF4 and sets
    // the +0xE0/+0xE1 state bits accordingly was not located this pass -
    // see the spec's own "Unknowns" note). Team B's CHOSEN application
    // point (same status as kNewGameStartingCashCents just above): scans
    // the CALLER-SUPPLIED parsed unlockables table for every row with
    // autoUnlock != None and marks each one purchased, by name (no
    // confirmed hash-to-bit-index mapping exists to do otherwise) - this
    // is deliberately data-driven, NOT a hardcoded "HomieSlot1" string, so
    // it stays correct if the shipped unlockables.xtbl data ever changes.
    std::unordered_set<std::string>& newGamePurchasedUnlocks() { return newGamePurchasedUnlocks_; }
    const std::unordered_set<std::string>& newGamePurchasedUnlocks() const { return newGamePurchasedUnlocks_; }
    StorePreviewGuards& storePreviewGuards() { return storePreviewGuards_; }
    StoreCommonRotateMouseDrag& storeCommonRotateMouseDrag() { return storeCommonRotateMouseDrag_; }
    PcuCategoryTable& pcuCategoryTable() { return pcuCategoryTable_; }
    PcuCatalogOutfits& pcuCatalogOutfits() { return pcuCatalogOutfits_; }
    PcuInventory& pcuInventory() { return pcuInventory_; }
    ShopPurchase& shopPurchase() { return shopPurchase_; }
    StrongholdPurchaseUpgrade& strongholdPurchaseUpgrade() { return strongholdPurchaseUpgrade_; }
    const std::unordered_map<std::string, ShopCribTriggerKind>& shopCribTriggerMap() const { return shopCribTrigger_; }
    bool shopCribPromptShowing() const { return !activeShopCribTrigger_.empty(); }
    const std::string& activeShopCribTrigger() const { return activeShopCribTrigger_; }

    // pcr_change_hair/pcr_change_composite/pcr_get_player_preset/
    // pcr_set_identity (Sec16.6/16.7/16.8/16.8a) - see PcrCustomizationState's
    // own doc comment above for the full shared-state/cross-batch reasoning.
    PcrCustomizationState& pcrCustomization() { return pcrCustomization_; }

    // store_common_bg_anim_complete (Sec16.14): global 0x022ccee8, set to
    // 1 unconditionally, no arguments.
    bool& storeCommonBgAnimComplete() { return storeCommonBgAnimComplete_; }

    // store_weapon_hide_weapon (Sec16.15): global handle 0x022ce900 - no
    // in-scope writer ever sets it non-null (same "mechanism confirmed, no
    // in-scope producer" convention), so a test sets storeWeaponPreviewActive
    // directly to exercise the detach branch. The 3 always-reset globals
    // (0x01300aa0/0x022ce904/+0x90d) collapse to one flat reset, matching
    // this header's "record the real branch, not unreadable bookkeeping"
    // convention.
    bool& storeWeaponPreviewActive() { return storeWeaponPreviewActive_; }
    int32_t storeWeaponPreviewSlot() const { return storeWeaponPreviewSlot_; }
    uint64_t storeWeaponDetachCount() const { return storeWeaponDetachCount_; }
    bool& customizationRevertPending() { return customizationRevertPending_; }
    uint64_t& customizationItemRevertCount() { return customizationItemRevertCount_; }
    uint64_t& customizationUiRefreshTailCount() { return customizationUiRefreshTailCount_; }

    // crib_disable_interface (Sec13.24): a single global byte - CHOSEN
    // false ("not disabled").
    bool& cribInterfaceDisabled() { return cribInterfaceDisabled_; }

    // =====================================================================
    // Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11 ("ranks 151-250
    // tranche, part D") + Sec12 ("ranks 151-250 tranche, part A") - PCR
    // quick-queries + vehicle/HUD/effect cluster. See each stub's own doc
    // comment (lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp) for the
    // full per-function citation/reasoning this section's fields rest on.
    // Per-character/per-vehicle fields for this batch live on
    // CharacterState/VehicleState above (own doc comments there); this
    // section holds only the EngineState-level (singleton/global) slots.
    // =====================================================================

    // pcr_is_plastic_surgeon (Sec11.1, 0x00830d10): CONFIRMED a single
    // global byte flag, pushed directly - no spec-stated construction
    // value, OPEN until a test sets it.
    OpenValue<bool>& pcrPlasticSurgeonMode() { return pcrPlasticSurgeonMode_; }

    // pcr_mark_changed (Sec11.2, 0x00831020): CONFIRMED unconditional write
    // of a dirty bit - no read-side OPEN question (every call sets it, no
    // gate), so a plain bool stands in rather than an OpenValue.
    bool& pcrDirty() { return pcrDirty_; }
    PcrMorphState& pcrMorph() { return pcrMorph_; }
    PcuStreaming& pcuStreaming() { return pcuStreaming_; }
    StoreCameraPreset& storeCameraPreset() { return storeCameraPreset_; }
    CustomizationCreationState& customizationCreation() { return customizationCreation_; }
    PcrVoiceState& pcrVoice() { return pcrVoice_; }
    PcrTriangleCursor& pcrTriangleCursor() { return pcrTriangleCursor_; }
    PcrReportCounters& pcrReportCounters() { return pcrReportCounters_; }
    PcrRandomComposite& pcrRandomComposite() { return pcrRandomComposite_; }
    PcrPresetState& pcrPreset() { return pcrPreset_; }
    PcrTattooPurchase& pcrTattooPurchase() { return pcrTattooPurchase_; }
    PcrRandomizePalettes& pcrRandomizePalettes() { return pcrRandomizePalettes_; }
    PcrSkinColorChange& pcrSkinColorChange() { return pcrSkinColorChange_; }
    PcrCreationComplete& pcrCreationComplete() { return pcrCreationComplete_; }

    // pcr_add_default_clothing (Sec43.3): CONFIRMED a genuinely destructive
    // effect - "empties the player's entire owned-clothing inventory (sets
    // the owned count to 0) before re-adding only the default garments."
    // The task brief's own explicit instruction: preserve this FAITHFULLY
    // (it is intentional engine behaviour for new-character creation, NOT a
    // bug to harden away) - flagged clearly here so nobody "fixes" it later
    // by mistake. Reuses the existing PcuInventory::ownedItemCount below
    // (NOT PcuClothingState::slots, the WORN slots - a completely
    // different storage, Sec19.16's own "do not conflate the two" caution)
    // as the actual owned-item count this wipes to 0. CORRECTED
    // (adversarial review): a prior version of this stub's code wiped
    // PcuClothingState::slots (all 24 WORN slots) instead of
    // PcuInventory::ownedItemCount, landing this deliberately-preserved
    // bug on the wrong object entirely - the real "do not fix" wipe never
    // actually happened. Fixed to target the field this comment (and
    // PcuClothingState::addDefaultClothingSlot10RefreshCount's own doc
    // comment) already correctly named.
    uint64_t& pcrAddDefaultClothingWipeCount() { return pcrAddDefaultClothingWipeCount_; }

    // pcr_strip_clothing (Sec26.2) / pcr_restore_clothing (Sec23.2): plain
    // call counters - both functions' own deep internals (the "mark dirty
    // vs. clear" OPEN question, Sec26.2; the full re-sync chain, Sec23.2)
    // are OPEN past their own entry checks, so only the gated call itself
    // is counted, honestly, per this project's established convention.
    uint64_t& pcrStripClothingCount() { return pcrStripClothingCount_; }
    uint64_t& pcrRestoreClothingCount() { return pcrRestoreClothingCount_; }
    PcuClothingState& pcuClothing() { return pcuClothing_; }
    PcuOwnedItemQueries& pcuOwnedItemQueries() { return pcuOwnedItemQueries_; }
    PcuAggregateCounters& pcuAggregate() { return pcuAggregate_; }
    PcuCreatorMisc& pcuCreatorMisc() { return pcuCreatorMisc_; }
    StoreFrontendState& storeFrontend() { return storeFrontend_; }
    GarageFrontendState& garageFrontend() { return garageFrontend_; }
    CribFrontendState& cribFrontend() { return cribFrontend_; }
    GangCustomizationState& gangCustomization() { return gangCustomization_; }

    // on_purchase (Sec18.30): the named-hook-slot-array family's first
    // confirmed GLOBAL-anchored instance (base 0x026e9528, slot 4) -
    // alongside the already-documented per-object character/mover/vehicle
    // bases. Same empty-string-clears-slot convention as every other hook
    // field in this header.
    std::string& onPurchaseCallback() { return onPurchaseCallback_; }

protected: // private in EngineState before the split
    VcustCamera vcustCamera_;
    GaragePreview garagePreview_;
    bool cribWeaponAddEnabled_ = true; // Sec28.13/Sec28.14, CONFIRMED file value 1

    // --- ranking tranche 04, Sec32 - batch 2026-10-02 - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    OpenValue<bool> storeInterfaceActive_{"store-interface active (0x022cce86 bit 0x1)", "spec-lua-api-behaviour.md Sec32.1"};
    std::vector<Shop> shops_;
    uint64_t shopEnableRecordCount_ = 0;

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec38/Sec39/Sec40
    // ("ranking tranches 06/07/08") - see each public accessor's own doc
    // comment above. ---
    VcustPreview vcustPreview_;
    // New-game-with-no-save default (see kNewGameStartingCashCents's own
    // doc comment above, spec-frontend-menu-runtime.md Sec9 item 1): CHOSEN
    // application point, real write-site still OPEN.
    double playerCash_ = kNewGameStartingCashCents;
    std::vector<PurchaseRecord> purchaseLedger_;
    // grantNewGameAutoUnlocks()'s own backing store - see that method's doc
    // comment near kNewGameStartingCashCents above.
    std::unordered_set<std::string> newGamePurchasedUnlocks_;
    StorePreviewGuards storePreviewGuards_;
    StoreCommonRotateMouseDrag storeCommonRotateMouseDrag_;
    PcuCategoryTable pcuCategoryTable_;
    PcuCatalogOutfits pcuCatalogOutfits_;
    PcuInventory pcuInventory_;

    // --- Batch 2026-10-03, spec-lua-api-behaviour.md Sec49 ("ranking
    // tranche 16") - see each public accessor's own doc comment above. ---
    ShopPurchase shopPurchase_;
    StrongholdPurchaseUpgrade strongholdPurchaseUpgrade_;
    // Shop/crib walk-in press-E entry - see registerShopCribTrigger()'s own
    // doc comment above for the full reasoning (crib's own open-target/chain
    // is REAL confirmed as of commit b8c17ec; shop's remains CHOSEN).
    std::unordered_map<std::string, ShopCribTriggerKind> shopCribTrigger_;
    std::string activeShopCribTrigger_;
    PcrCustomizationState pcrCustomization_;              // Sec16.6/16.7/16.8/16.8a
    bool storeCommonBgAnimComplete_ = false;              // Sec16.14
    bool storeWeaponPreviewActive_ = false;               // Sec16.15
    int32_t storeWeaponPreviewSlot_ = -1;
    uint64_t storeWeaponDetachCount_ = 0;
    uint64_t storeWeaponHideWeaponCallCount_ = 0;
    std::vector<CustomizationOutfitWearRequest> customizationOutfitWearRequests_;
    std::vector<CustomizationItemWearRequest> customizationItemWearRequests_;
    bool customizationRevertPending_ = false;
    uint64_t customizationItemRevertCount_ = 0;
    uint64_t customizationUiRefreshTailCount_ = 0;
    bool cribInterfaceDisabled_ = false;

    // --- Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11+Sec12 - see
    // each public accessor's own doc comment above for citation/reasoning. ---
    OpenValue<bool> pcrPlasticSurgeonMode_{"pcr_is_plastic_surgeon global byte", "spec-lua-api-behaviour.md Sec11.1"};
    bool pcrDirty_ = false;
    PcrMorphState pcrMorph_;
    PcuStreaming pcuStreaming_;
    StoreCameraPreset storeCameraPreset_;
    std::optional<bool> missionStorePurchased_;
    std::optional<bool> missionPropertyPurchased_;

    // --- Batch W2-5 (2026-10-08), wave 2 fan-out - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    CustomizationCreationState customizationCreation_;
    PcrVoiceState pcrVoice_;
    PcrTriangleCursor pcrTriangleCursor_;
    PcrReportCounters pcrReportCounters_;
    PcrRandomComposite pcrRandomComposite_;
    PcrPresetState pcrPreset_;
    PcrTattooPurchase pcrTattooPurchase_;
    PcrRandomizePalettes pcrRandomizePalettes_;
    PcrSkinColorChange pcrSkinColorChange_;
    PcrCreationComplete pcrCreationComplete_;
    uint64_t pcrAddDefaultClothingWipeCount_ = 0;
    uint64_t pcrStripClothingCount_ = 0;
    uint64_t pcrRestoreClothingCount_ = 0;
    PcuClothingState pcuClothing_;
    PcuOwnedItemQueries pcuOwnedItemQueries_;
    PcuAggregateCounters pcuAggregate_;
    PcuCreatorMisc pcuCreatorMisc_;
    StoreFrontendState storeFrontend_;
    GarageFrontendState garageFrontend_;
    CribFrontendState cribFrontend_;
    GangCustomizationState gangCustomization_;
    std::string onPurchaseCallback_;
};

} // namespace sr3luahost
