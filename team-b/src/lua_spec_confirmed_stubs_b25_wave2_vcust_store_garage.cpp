// lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp
//
// Wave 2, Batch W2-5 (2026-10-08) of the post-wave-1 parallel fan-out -
// "vcust/store/garage/crib/gang" half of a 2-file split (the other half,
// PCU/PCR/customization_*, is lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.
// cpp - see that file's own top comment for the shared batch-level
// provenance/reuse notes, not repeated here). Scope: vcust_* (Sec19/Sec26/
// Sec38/Sec52), store_* (Sec26/Sec38/Sec39/Sec49), garage_* (Sec23/Sec41/
// Sec53), crib_* (Sec23/Sec57), gang_customization_* (Sec53/Sec56), and
// building_purchase_is_crib_being_purchased (Sec59). 60 names.
//
// HARDENING POLICY: this file owns ALL 4 of the task brief's own NAMED
// memory-safety findings:
//   - gang_customization_select_vehicle (Sec53.1): unchecked 0-2 slot index
//     -> arbitrary-address write, "the single clearest 'accept exactly this
//     tiny range' fix this whole series has produced."
//   - gang_customization_confirm_gang_sign / gang_customization_select_
//     gang_sign (Sec56.1): an unbounded sign-list index (read+persisted-
//     write) and a DETERMINISTIC null-vtable call on mode 1 ("every mode-1
//     call crashes, full stop").
//   - vcust_purchase_component (Sec52.1): an out-of-bounds read AND write
//     from a stale -1 "currently selected item" index, "the most severe
//     finding in this tranche."
// All 4 are hardened below with an explicit refusal path AND a faithful
// valid-input path, each covered by tests/synthetic_luahost_wave2_w25b_
// test.cpp (both paths, per the task brief's own explicit instruction).
// 12 further CRASH-flagged names in THIS file (vcust_preview_color/
// _preview_palette/_purchase_wheels/_change_top_level_menu/_show_nos_
// hydraulic_warning, store_weapon_change_weapon, store_gallery_download_
// show_list, store_stronghold_upgrade_end_flyby, garage_repair_vehicle/
// _clear_new/_remove_vehicle, crib_purchase_purchase_crib,
// gang_customization_revert_vehicle) are hardened the same way (bound-
// check/refuse, never reproduce) - see each stub's own doc comment.
//
// REUSE: EngineState::vcustPreview() (wave-1 Batch 4, extended in place for
// this wave rather than duplicated - see engine_state.h's own "Batch W2-5
// additions" note on VcustPreview), playerCash()/purchaseLedger() (shared
// wallet), hasLocalPlayer(), coopSession()/coopLocalIsHost(), garagePreview()
// (Batch 2's own Sec27.9 struct - left untouched, a SEPARATE mechanism from
// this batch's own garageFrontend(), per Sec23.10's own "new global pair,
// not previously catalogued" finding).
#include "sr3player/vitals.h"
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// These are nested types of EngineState (declared in its own public
// section, engine_state.h) - aliased here, unqualified, purely so this
// file's own stub bodies below can name them exactly as written without an
// `EngineState::` prefix at every use site.
using StoreFrontendState = EngineState::StoreFrontendState;
using GarageFrontendState = EngineState::GarageFrontendState;
using GangCustomizationState = EngineState::GangCustomizationState;

// =========================================================================
// vcust_* family (15 names) - Sec19.10/19.11, Sec26.16, Sec38.5, Sec52.1.
// Sec52.1's own headline finding: "only 2 of the 9 vcust_* names [in that
// tranche] actually check that a live 'customization vehicle' exists ...
// a binding layer should simply treat every name in this family as
// 'requires a live customization vehicle' and refuse up front." This file
// applies that uniformly via the shared VcustPreview::targetLive gate
// (reused from wave-1 Batch 4's vcust_preview_wheel_sizing, extended in
// place) for every vcust_* entry point below that the spec text confirms
// dereferences the customization-vehicle chain.
// =========================================================================

// vcust_revert_color (Sec19.10, 0x0081f890): no arguments, no return.
// CONFIRMED: "if a single-byte gate flag (0x022cf90b) is clear, copies a
// stored 'original color' value back into the live/current value slot and
// calls a notify/apply routine with a literal 1 second argument." No CRASH
// flagged for this name - the gate itself (colorRevertGateClear) is the
// REAL guard, reproduced as-is, not hardened.
int stub_vcust_revert_color(lua_State* L) {
    logCall(L, upLog(L), "vcust_revert_color", upStateTag(L));
    auto& vp = upState(L)->vcustPreview();
    if (vp.colorRevertGateClear) { // CONFIRMED real gate, Sec19.10 - not a hardening target
        vp.currentColorValue = vp.originalColorValue;
        ++vp.revertColorCount;
    }
    return 0;
}

// vcust_preview_component (Sec19.11, 0x0081f2e0): 4 numbers - arg1
// mandatory (mode selector; only 0 and 2 are handled, anything else is a
// silent no-op, CONFIRMED by the decompiled control flow). No return.
int stub_vcust_preview_component(lua_State* L) {
    logCall(L, upLog(L), "vcust_preview_component", upStateTag(L));
    int64_t mode = truncateEa2596(lua_tonumber(L, 1));
    int64_t arg2 = truncateEa2596(optionalNumberDefault(L, 2, 0.0));
    (void)optionalNumberDefault(L, 3, 0.0);
    (void)optionalNumberDefault(L, 4, 0.0);
    auto& vp = upState(L)->vcustPreview();
    if (mode == 0) {
        vp.previewedComponentIndex = static_cast<int32_t>(arg2); // CONFIRMED: caches the index when below the byte-sized count field (count OPEN, no real asset table in scope)
        ++vp.previewComponentMode0Count;
    } else if (mode == 2) {
        ++vp.previewComponentMode2Count; // CONFIRMED: further branches on arg2 (0/1/2) into the 0x022cfxxx preview-state cluster - deep per-field semantics OPEN
    } else {
        ++vp.previewComponentUnhandledModeCount; // CONFIRMED: "any other value is a silent no-op"
    }
    return 0;
}

// vcust_is_camera_tool_mode (Sec26.16, 0x00818390): no arguments. Return: 1
// boolean. CONFIRMED trivial zero-argument accessor passthrough.
int stub_vcust_is_camera_tool_mode(lua_State* L) {
    logCall(L, upLog(L), "vcust_is_camera_tool_mode", upStateTag(L));
    lua_pushboolean(L, upState(L)->vcustPreview().cameraToolMode.get() ? 1 : 0); // OPEN until set
    return 1;
}

// vcust_preview_color (Sec38.5 CRASH, 0x0081f860): 1 number (color index,
// unchecked). No return. CONFIRMED: "All four [vcust_preview_*/_purchase_
// wheels] resolve the paint/part sub-object as vehicle+0x688 only when the
// vehicle's body is active, and as a null pointer otherwise, then
// dereference an offset of that result unconditionally - a vehicle that
// isn't fully set up crashes." HARDENED: refuses when VcustPreview::
// targetLive is false (the shared gate every vcust_* name in this family
// now checks, per Sec52.1's own directive).
int stub_vcust_preview_color(lua_State* L) {
    logCall(L, upLog(L), "vcust_preview_color", upStateTag(L));
    double colorIndex = lua_tonumber(L, 1); // CONFIRMED: never bounds-checked in the original - no real palette size given in scope either, so only the target gate is hardened here
    auto& vp = upState(L)->vcustPreview();
    if (!vp.targetLive) {
        ++vp.previewColorNoTargetRefusedCount; // HARDENED (Sec38.5 shared null-deref hazard)
        return 0;
    }
    vp.currentColorValue = colorIndex;
    return 0;
}

// vcust_preview_palette (Sec38.5 CRASH, 0x0081f8d0): 3 numbers (zone,
// palette, color index - all unchecked). No return. CONFIRMED "Reads the
// zone record and indexes into its palette list BEFORE checking whether
// the vehicle/zone resolved at all - a null zone record crashes even when
// the overall 'material' check that follows would have caught it." Also
// CONFIRMED a real inconsistency: "the palette is looked up for the given
// zone argument, but the actual paint call applies it to the CURRENT zone
// global instead" - reproduced faithfully (a logic quirk, not a memory-
// safety one). HARDENED: refuses on no live target OR an unresolved
// (zone,palette) pair rather than reading past a null/foreign zone record.
int stub_vcust_preview_palette(lua_State* L) {
    logCall(L, upLog(L), "vcust_preview_palette", upStateTag(L));
    int64_t zone = truncateEa2596(lua_tonumber(L, 1));
    int64_t palette = truncateEa2596(lua_tonumber(L, 2));
    int64_t colorIndex = truncateEa2596(lua_tonumber(L, 3));
    auto& vp = upState(L)->vcustPreview();
    if (!vp.targetLive) {
        ++vp.previewPaletteNoTargetRefusedCount; // HARDENED
        return 0;
    }
    std::string key = std::to_string(zone) + ":" + std::to_string(palette);
    if (colorIndex < 0 || !vp.zonePaletteCount.known(key) || !vp.zonePaletteCount.get(key)) {
        ++vp.previewPaletteOobRefusedCount; // HARDENED (unchecked zone/palette/colour indices)
        return 0;
    }
    // CONFIRMED quirk preserved: applies to the CURRENT zone, not the
    // argument zone (Sec38.5's own "real inconsistency" finding).
    vp.currentColorValue = static_cast<double>(colorIndex);
    return 0;
}

// vcust_purchase_wheels (Sec38.5 CRASH, 0x0081fe70): 1 number (price). No
// return. CONFIRMED: "Charges the price (no affordability check, no null
// check on the local player) ... Commits the previewed wheel configuration
// regardless of whether a price was charged - and two of the three
// committed-object pointers are dereferenced with no null check." HARDENED:
// refuses (no charge, no commit) when no live customization target exists;
// reproduces the "no affordability check" quirk faithfully (a logic quirk,
// not hardened away) via playerCash()/purchaseLedger().
int stub_vcust_purchase_wheels(lua_State* L) {
    logCall(L, upLog(L), "vcust_purchase_wheels", upStateTag(L));
    double price = lua_tonumber(L, 1);
    EngineState* st = upState(L);
    auto& vp = st->vcustPreview();
    if (!vp.targetLive) {
        ++vp.purchaseWheelsNoTargetRefusedCount; // HARDENED
        return 0;
    }
    st->debitPlayerCash(price); // CONFIRMED: no affordability check - faithfully preserved
    EngineState::PurchaseRecord rec;
    rec.tag = "vehicle-cust"; // CONFIRMED literal tag, Sec38.5
    rec.amountCharged = price;
    rec.itemResolved = true;
    st->purchaseLedger().push_back(rec);
    vp.wheelPreviewCommitted = true; // CONFIRMED: commits regardless of charge outcome
    return 0;
}

// vcust_adjust_camera_angle (Sec52.1, 0x0081fxxx approx): CONFIRMED "real
// null-dereference crash when the customization vehicle fails to resolve
// ... additionally reads the current menu node global with no null check
// of its own, a second independent crash in the same function." HARDENED:
// refuses on no live target; the menu-node null read is avoided by this
// project's own OpenValue (currentTopLevelNode, throws OPEN rather than
// crashing - a DIFFERENT, already-safe-by-construction divergence, not an
// invented new check).
int stub_vcust_adjust_camera_angle(lua_State* L) {
    logCall(L, upLog(L), "vcust_adjust_camera_angle", upStateTag(L));
    double delta = lua_tonumber(L, 1);
    auto& vp = upState(L)->vcustPreview();
    if (!vp.targetLive) {
        ++vp.adjustCameraAngleNoTargetRefusedCount; // HARDENED
        return 0;
    }
    vp.cameraAngle += delta;
    ++vp.adjustCameraAngleCount;
    return 0;
}

// vcust_change_top_level_menu (Sec52.1 CRASH, "poisons its own next call"):
// 1 relative-or-absolute-mode number + 1 optional step count. CONFIRMED:
// "An out-of-range absolute selection stores null into the 'current top-
// level node' global unconditionally - and the very next relative (+/-1)
// call dereferences that same global with no check at all." HARDENED: an
// out-of-range absolute selection REFUSES the store (leaves the prior node
// untouched) rather than poisoning it with null; a relative call with no
// current node REFUSES rather than dereferencing it.
int stub_vcust_change_top_level_menu(lua_State* L) {
    logCall(L, upLog(L), "vcust_change_top_level_menu", upStateTag(L));
    int64_t mode = truncateEa2596(lua_tonumber(L, 1));
    int64_t step = truncateEa2596(optionalNumberDefault(L, 2, 1.0));
    auto& vp = upState(L)->vcustPreview();
    // CHOSEN: "absolute" selection is a non-negative mode value (an
    // explicit top-level node index); "relative" is a signed +/-1-style
    // step - the exact real-engine discriminant (relative-or-absolute-mode
    // number) was not independently re-derived field-by-field in scope, so
    // this project reads mode >= 0 as absolute, matching the spec's own
    // "relative (+/-1)" phrasing for the other case.
    if (mode >= 0) {
        static constexpr int32_t kTopLevelNodeCountBound = 16; // CHOSEN bound: no real top-level-menu node count given in scope - a conservative, non-zero bound so a plainly-absurd index (e.g. a huge or negative value) is refused rather than guessed real
        if (mode >= kTopLevelNodeCountBound) {
            ++vp.changeMenuAbsoluteOutOfRangeRefusedCount; // HARDENED (named divergence: real engine stores null here)
            return 0;
        }
        vp.currentTopLevelNode.set(static_cast<int32_t>(mode));
        ++vp.changeMenuAbsoluteCount;
        return 0;
    }
    if (!vp.currentTopLevelNode.known()) {
        ++vp.changeMenuRelativeNoNodeRefusedCount; // HARDENED (named divergence: real engine dereferences null here)
        return 0;
    }
    vp.currentTopLevelNode.set(vp.currentTopLevelNode.get() + static_cast<int32_t>(step));
    ++vp.changeMenuRelativeCount;
    return 0;
}

// vcust_preview_rim_sizing (Sec52.1, 0x0081fxxx approx): CONFIRMED "real
// null-dereference crash when the customization vehicle fails to resolve"
// plus a "completely unchecked numeric index [combined size+style index]
// into a fixed-size table." HARDENED: refuses on no live target OR an
// unresolved combined index.
int stub_vcust_preview_rim_sizing(lua_State* L) {
    logCall(L, upLog(L), "vcust_preview_rim_sizing", upStateTag(L));
    int64_t size = truncateEa2596(lua_tonumber(L, 1));
    int64_t style = truncateEa2596(lua_tonumber(L, 2));
    auto& vp = upState(L)->vcustPreview();
    if (!vp.targetLive) {
        ++vp.previewRimSizingNoTargetRefusedCount; // HARDENED
        return 0;
    }
    static constexpr int64_t kRimSizingTableBound = 256; // CHOSEN conservative bound - real fixed-table size not given in scope
    int64_t combined = (size << 8) | (style & 0xff);
    if (combined < 0 || combined >= kRimSizingTableBound) {
        ++vp.previewRimSizingOobRefusedCount; // HARDENED
        return 0;
    }
    vp.rimSizingIndex = static_cast<int32_t>(combined);
    ++vp.previewRimSizingCount;
    return 0;
}

// vcust_purchase_component (Sec52.1 MEMSAFE - one of this batch's 4 NAMED
// memory-safety findings, "most severe finding in this tranche"): no
// arguments; operates on a globally-tracked "currently selected item"
// index. CONFIRMED: "That index is file-initialized to -1 and is reset
// back to -1 on every exit from this function - so calling it twice in a
// row, or calling it at all before a fresh selection, computes an 'item'
// pointer 60 bytes before the real item array. If the garbage bytes there
// happen to decode as an unowned item with a positive price, the function
// charges that garbage price to the player and then writes a bit into the
// dword immediately preceding the array - a real heap-corruption shape."
//
// HARDENED (the named fix): no raw array is modelled - the "currently
// selected item" is a test-populated resolver
// (VcustPreview::selectedComponentItemPrice, by index string). This stub
// reads VcustPreview::selectedComponentItemIndex (the SAME -1-at-rest/
// reset-after-every-call mirror of the real global) and ONLY charges+
// writes when that index is non-negative AND a price was actually
// populated for it (a test calls EngineState::selectVcustComponentFor
// Testing() to simulate a just-made selection - see that method's own doc
// comment, engine_state.h). Every exit - success or refusal - resets the
// index to -1, reproducing the real global's own "reset on every exit"
// behaviour exactly, so the SAME two trigger conditions the real bug has
// (double-call, or no prior selection) are the ones this stub refuses.
int stub_vcust_purchase_component(lua_State* L) {
    logCall(L, upLog(L), "vcust_purchase_component", upStateTag(L));
    EngineState* st = upState(L);
    auto& vp = st->vcustPreview();
    if (!vp.targetLive) {
        ++vp.purchaseComponentNoTargetRefusedCount; // HARDENED
        vp.selectedComponentItemIndex = -1; // CONFIRMED: reset on every exit
        return 0;
    }
    int32_t index = vp.selectedComponentItemIndex;
    std::string key = std::to_string(index);
    if (index < 0 || !vp.selectedComponentItemPrice.known(key)) {
        ++vp.purchaseComponentStaleIndexRefusedCount; // HARDENED - THE NAMED FIX (real engine: OOB read+write from a stale -1 index)
        vp.selectedComponentItemIndex = -1; // CONFIRMED: reset on every exit, win or lose
        return 0;
    }
    double price = vp.selectedComponentItemPrice.get(key);
    st->debitPlayerCash(price);
    ++vp.purchaseComponentChargedCount;
    vp.purchaseComponentTotalCharged += price;
    vp.selectedComponentItemIndex = -1; // CONFIRMED: reset on every exit, including success
    return 0;
}

// vcust_purchase_paint (Sec52.1, 0x0081fxxx approx): CONFIRMED "no price
// validation whatsoever - a negative price credits the player's cash, and
// a price of exactly zero still fires the stat/callback/telemetry side
// effects as if a real purchase happened." Reproduced faithfully for a
// resolved target (the quirk is explicitly NOT in this batch's own 4 named
// findings and is a logic quirk, not a memory-safety bug) - but the task
// brief's own general hardening directive ("refuse or no-op out-of-range
// input") still applies to the shared null-dereference hazard: HARDENED on
// no live target.
int stub_vcust_purchase_paint(lua_State* L) {
    logCall(L, upLog(L), "vcust_purchase_paint", upStateTag(L));
    double price = lua_tonumber(L, 1);
    EngineState* st = upState(L);
    auto& vp = st->vcustPreview();
    if (!vp.targetLive) {
        ++vp.purchasePaintNoTargetRefusedCount; // HARDENED (shared null-deref hazard)
        return 0;
    }
    st->debitPlayerCash(price); // CONFIRMED: no floor, no validation - faithfully preserved, even for price <= 0
    ++vp.purchasePaintCount;
    vp.paintTotalCharged += price;
    return 0;
}

// vcust_purchase_performance_upgrade (Sec52.1, 0x0081fxxx approx):
// CONFIRMED "the ONLY member of the family that validates its result
// before charging anything." Reproduced faithfully: refuses (no charge) on
// no live target, matching the real engine's own ALREADY-guarded behaviour
// here (not a new divergence this project invents).
int stub_vcust_purchase_performance_upgrade(lua_State* L) {
    logCall(L, upLog(L), "vcust_purchase_performance_upgrade", upStateTag(L));
    double price = lua_tonumber(L, 1);
    EngineState* st = upState(L);
    auto& vp = st->vcustPreview();
    if (!vp.targetLive) {
        ++vp.purchasePerformanceUpgradeNoTargetRefusedCount; // CONFIRMED: the real engine ALREADY validates here (not a new hardening divergence)
        return 0;
    }
    st->debitPlayerCash(price);
    ++vp.purchasePerformanceUpgradeCount;
    vp.performanceUpgradeTotalCharged += price;
    return 0;
}

// vcust_select_paint_slot (Sec52.1, 0x0081fxxx approx): CONFIRMED "real
// null-dereference crash ... completely unchecked numeric indices into
// fixed-size tables." HARDENED: refuses on no live target OR an out-of-
// range slot index.
int stub_vcust_select_paint_slot(lua_State* L) {
    logCall(L, upLog(L), "vcust_select_paint_slot", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& vp = upState(L)->vcustPreview();
    if (!vp.targetLive) {
        ++vp.selectPaintSlotNoTargetRefusedCount; // HARDENED
        return 0;
    }
    static constexpr int64_t kPaintSlotBound = 16; // CHOSEN conservative bound - real fixed-table size not given in scope
    if (slot < 0 || slot >= kPaintSlotBound) {
        ++vp.selectPaintSlotOobRefusedCount; // HARDENED
        return 0;
    }
    vp.selectedPaintSlot = static_cast<int32_t>(slot);
    ++vp.selectPaintSlotCount;
    return 0;
}

// vcust_set_underglow (Sec52.1, 0x0081fxxx approx): CONFIRMED "applies its
// colour change BEFORE charging, trusts a script-supplied price with no
// floor, and has no purchase callback or telemetry at all, unlike its
// three siblings" - also takes a palette-colour index with no documented
// precondition on the live target (unlike its siblings, the spec text does
// not flag this one with the shared null-deref hazard specifically, so no
// targetLive gate is applied here - only the index itself is hardened).
int stub_vcust_set_underglow(lua_State* L) {
    logCall(L, upLog(L), "vcust_set_underglow", upStateTag(L));
    int64_t colorIndex = truncateEa2596(lua_tonumber(L, 1));
    double price = optionalNumberDefault(L, 2, 0.0);
    EngineState* st = upState(L);
    auto& vp = st->vcustPreview();
    static constexpr int64_t kUnderglowPaletteBound = 32; // CHOSEN conservative bound - real palette size not given in scope
    if (colorIndex < 0 || colorIndex >= kUnderglowPaletteBound) {
        ++vp.setUnderglowOobRefusedCount; // HARDENED
        return 0;
    }
    vp.underglowColorIndex = static_cast<int32_t>(colorIndex);
    st->debitPlayerCash(price); // CONFIRMED: applied AFTER the colour change, no floor - faithfully preserved
    ++vp.setUnderglowCount;
    return 0;
}

// vcust_show_nos_hydraulic_warning (Sec52.1 CRASH, 0x0081fxxx approx):
// CONFIRMED "real null-dereference crash when the customization vehicle
// fails to resolve." HARDENED: refuses on no live target.
int stub_vcust_show_nos_hydraulic_warning(lua_State* L) {
    logCall(L, upLog(L), "vcust_show_nos_hydraulic_warning", upStateTag(L));
    auto& vp = upState(L)->vcustPreview();
    if (!vp.targetLive) {
        ++vp.showNosHydraulicWarningNoTargetRefusedCount; // HARDENED
        return 0;
    }
    ++vp.showNosHydraulicWarningCount;
    return 0;
}

// =========================================================================
// store_* family (23 names) - Sec26.7/26.8, Sec38.3/38.4, Sec39.5, Sec49.1,
// Sec52.1/T15.
// =========================================================================

// store_dlc_make_purchase (Sec26.8, 0x007c5b30): 1 number (DLC id), read
// but never used. Return: 1 number, always 0.0. CONFIRMED hardcoded stub -
// "always reports a purchase result of exactly 0, for any DLC id, and
// never performs any actual purchase, lookup, or network call." Reproduced
// verbatim.
int stub_store_dlc_make_purchase(lua_State* L) {
    logCall(L, upLog(L), "store_dlc_make_purchase", upStateTag(L));
    (void)lua_tonumber(L, 1); // CONFIRMED: read but never used
    ++upState(L)->storeFrontend().dlcMakePurchaseCount;
    lua_pushnumber(L, 0.0); // CONFIRMED hardcoded always-0.0
    return 1;
}

// store_weapon_purchase_weapon (Sec26.7, 0x008172d0): 1 string (weapon
// category, matched against a real 9-entry table), 1 number (weapon
// index), 1 boolean, 1 number (kept as a native double - NOT rounded,
// Sec26.7's own confirmed idiom variant). No return. CONFIRMED: the 9-entry
// table is {unarmed,melee,pistol,smg,shotgun,rifle,explosive,special,
// thrown}; the boolean true takes a short preview-only path; false runs
// the real purchase (grant if not owned, else an achievement sweep).
int stub_store_weapon_purchase_weapon(lua_State* L) {
    logCall(L, upLog(L), "store_weapon_purchase_weapon", upStateTag(L));
    std::string category = argString(L, 1);
    int64_t index = truncateEa2596(lua_tonumber(L, 2));
    bool previewOnly = lua_toboolean(L, 3) != 0;
    double floatValue = lua_tonumber(L, 4); // CONFIRMED: kept as a native double, no round-cast
    (void)floatValue;
    static const char* kCategories[9] = {"unarmed", "melee", "pistol", "smg", "shotgun", "rifle", "explosive", "special", "thrown"};
    int32_t categoryIdx = -1;
    for (int32_t i = 0; i < 9; ++i) if (caseInsensitiveEquals(category, kCategories[i])) { categoryIdx = i; break; }
    auto& store = upState(L)->storeFrontend();
    if (categoryIdx < 0) { ++store.weaponPurchaseUnmatchedCategoryCount; return 0; } // CONFIRMED: unmatched -> silent no-op
    if (previewOnly) return 0; // CONFIRMED: "takes a short preview/toggle only path ... and returns"
    std::string key = category + ":" + std::to_string(index);
    bool owned = store.weaponOwnedByCategoryIndex.known(key) && store.weaponOwnedByCategoryIndex.get(key);
    if (!owned) {
        store.weaponOwnedByCategoryIndex.set(key, true); // CONFIRMED: "grant/give this weapon" primitive
        ++store.weaponPurchaseGrantedCount;
    } else {
        ++store.weaponPurchaseAchievementSweepCount; // CONFIRMED: already-owned -> achievement/completion-tracking pass
    }
    return 0;
}

// store_vehicle_change_mode (Sec38.4, 0x00815120): 1 number. No return.
// CONFIRMED: "A no-op if the number already equals the current mode flag.
// Mode 0 takes over a pending vehicle ... any other value ... sets the
// mode flag to the LITERAL 1 on success regardless of the argument's
// actual value."
int stub_store_vehicle_change_mode(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_change_mode", upStateTag(L));
    int64_t mode = truncateEa2596(lua_tonumber(L, 1));
    auto& store = upState(L)->storeFrontend();
    if (mode == store.vehicleStoreMode) return 0; // CONFIRMED no-op
    if (mode == 0) {
        store.vehicleStoreMode = 0; // CONFIRMED: take-over-pending path
    } else {
        store.vehicleStoreMode = 1; // CONFIRMED: literal 1 regardless of the argument's actual value
    }
    ++store.vehicleChangeModeCount;
    return 0;
}

// store_vehicle_retrieve_car_and_exit (Sec38.4, 0x00813020): no arguments,
// no return. CONFIRMED: "Takes over a pending vehicle if the mode flag is
// 1, then pops the top of the UI screen stack (and a second time if a
// specific screen id is also on the stack)."
int stub_store_vehicle_retrieve_car_and_exit(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_retrieve_car_and_exit", upStateTag(L));
    auto& store = upState(L)->storeFrontend();
    if (store.vehicleStoreMode == 1) ++store.vehicleRetrieveCarAndExitCount; // CONFIRMED gate; deep screen-stack internals OPEN
    return 0;
}

// store_vehicle_selection_should_lock_controls (Sec38.4, 0x00813170): no
// arguments. Return: 1 boolean, a pure query (details HYPOTHESIS beyond the
// control-flow shape).
int stub_store_vehicle_selection_should_lock_controls(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_selection_should_lock_controls", upStateTag(L));
    lua_pushboolean(L, upState(L)->storeFrontend().selectionLocksControls.get() ? 1 : 0); // OPEN until set
    return 1;
}

// store_weapon_change_weapon (Sec38.3 CRASH, 0x00815f40): a slot-category
// string + a mandatory item-index number + 2 booleans + an optional
// number. No return. CONFIRMED: "A category match selects one of two fixed
// arrays by item index with no bounds check at all - an arbitrary script-
// supplied index reads an arbitrary nearby dword as an item pointer. Also
// dereferences the local player with no null check on one path." HARDENED:
// test-populated "does (category,index) resolve" registry (CHOSEN reuse of
// the SAME 9-entry category table store_weapon_purchase_weapon documents,
// a sibling weapon-store UI function - see this file's own top comment);
// refuses an unresolved pair; reuses hasLocalPlayer() for the unchecked-
// player path.
int stub_store_weapon_change_weapon(lua_State* L) {
    logCall(L, upLog(L), "store_weapon_change_weapon", upStateTag(L));
    std::string category = argString(L, 1);
    int64_t itemIndex = truncateEa2596(lua_tonumber(L, 2));
    (void)lua_toboolean(L, 3);
    (void)lua_toboolean(L, 4);
    (void)optionalNumberDefault(L, 5, 0.0);
    EngineState* st = upState(L);
    auto& store = st->storeFrontend();
    std::string key = category + ":" + std::to_string(itemIndex);
    if (!store.weaponChangeSlotResolves.known(key) || !store.weaponChangeSlotResolves.get(key)) {
        ++store.weaponChangeOobRefusedCount; // HARDENED (named CRASH: unchecked index into a fixed array)
        return 0;
    }
    if (!st->hasLocalPlayer().known() || !st->hasLocalPlayer().get()) {
        ++store.weaponChangeOobRefusedCount; // HARDENED (shared counter: the "unchecked local player" path)
        return 0;
    }
    ++store.weaponChangeCount;
    return 0;
}

// store_weapon_process_post_bg_covered (Sec38.3, 0x00815460): no
// arguments, no return. CONFIRMED: "Writes a single global byte that the
// per-frame render-scene setup copies into renderer state alongside fog/
// atmosphere parameters." Lua-side-only state, no engine consumer in this
// project's scope.
int stub_store_weapon_process_post_bg_covered(lua_State* L) {
    logCall(L, upLog(L), "store_weapon_process_post_bg_covered", upStateTag(L));
    upState(L)->storeFrontend().postBgCovered = true;
    return 0;
}

// store_character_lineup_loaded (Sec39.5, 0x0080d4b0): no arguments, no
// return. CONFIRMED: sets a single latch byte (0x022ccc52 := 1).
int stub_store_character_lineup_loaded(lua_State* L) {
    logCall(L, upLog(L), "store_character_lineup_loaded", upStateTag(L));
    upState(L)->storeFrontend().characterLineupLoaded = true;
    return 0;
}

// store_clothing_get_store_id (Sec39.5, 0x0080db90): no arguments. Returns
// 1 number, the result of an un-opened callee (0x0080d810, OPEN).
int stub_store_clothing_get_store_id(lua_State* L) {
    logCall(L, upLog(L), "store_clothing_get_store_id", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->storeFrontend().clothingStoreId.get())); // OPEN until set
    return 1;
}

// store_dlc_is_offer_free (Sec39.5, 0x007c5b00): reads and discards a
// number argument. CONFIRMED NOOP: "always returns false, regardless of
// input."
int stub_store_dlc_is_offer_free(lua_State* L) {
    logCall(L, upLog(L), "store_dlc_is_offer_free", upStateTag(L));
    (void)optionalNumberDefault(L, 1, 0.0);
    ++upState(L)->storeFrontend().dlcIsOfferFreeCount;
    lua_pushboolean(L, 0); // CONFIRMED NOOP, exact documented return shape
    return 1;
}

// store_dlc_queue_icon (Sec39.5, 0x007c5b60): reads a number and a string
// argument and discards both. CONFIRMED NOOP: "no side effects, no return
// value."
int stub_store_dlc_queue_icon(lua_State* L) {
    logCall(L, upLog(L), "store_dlc_queue_icon", upStateTag(L));
    (void)optionalNumberDefault(L, 1, 0.0);
    (void)optionalStringDefault(L, 2, "");
    ++upState(L)->storeFrontend().dlcQueueIconCount;
    return 0; // CONFIRMED NOOP, exact documented return shape (no value)
}

// store_vehicle_no_vehicle_exit / store_vehicle_notify_screen_covered
// (Sec52.1/T15 AGG): no individual per-function body given in spec scope
// (not among the Sec52.1 vcust_* write-ups, and not individually named
// elsewhere) - AGG discipline: thin, argument-consuming shells with a call
// counter each, no invented signature/logic beyond "store-vehicle-screen
// UI state functions, 2 calls in the already-specced registrar 0x008152a0."
int stub_store_vehicle_no_vehicle_exit(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_no_vehicle_exit", upStateTag(L));
    ++upState(L)->storeFrontend().vehicleNoVehicleExitCount;
    return 0;
}
int stub_store_vehicle_notify_screen_covered(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_notify_screen_covered", upStateTag(L));
    (void)optionalBoolDefault(L, 1, false);
    ++upState(L)->storeFrontend().vehicleNotifyScreenCoveredCount;
    return 0;
}

// store_crib_init_crib_garage / store_gallery_display_character / store_
// gallery_download_show_list (CRASH) / store_gallery_upload_character /
// store_gang_begin_exit / store_gang_bg_covered / store_gang_is_unlocked /
// store_stronghold_upgrade_end_flyby (CRASH) / store_vehicle_allow_garage /
// store_vehicle_do_return_to_crib (Sec49.1/T16): "resolved in full, no
// further crash shapes beyond the unchecked local-player reads already
// common to this whole series" - except the 2 explicitly named CRASH
// findings below, each with its own one-line gloss in Sec49.1's own text
// (quoted per-stub).

// store_crib_init_crib_garage: "a three-candidate garage-node picker keyed
// by crib upgrade level." Reuses cribFrontend().isStronghold-adjacent
// upgrade-level concept only as a call-gated counter - deep per-level node
// selection is OPEN (no real garage-node table in scope).
int stub_store_crib_init_crib_garage(lua_State* L) {
    logCall(L, upLog(L), "store_crib_init_crib_garage", upStateTag(L));
    ++upState(L)->storeFrontend().cribInitCribGarageCount;
    return 0;
}

// store_gallery_display_character: "a bounds-checked list-index apply" -
// CONFIRMED (this one, unlike its siblings, DOES check its own index in
// the original) - reproduced as a real bound, not a new hardening target.
int stub_store_gallery_display_character(lua_State* L) {
    logCall(L, upLog(L), "store_gallery_display_character", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    auto& store = upState(L)->storeFrontend();
    std::string key = std::to_string(index);
    if (index < 0 || !store.galleryCharacterIndexResolves.known(key) || !store.galleryCharacterIndexResolves.get(key)) {
        ++store.galleryDisplayCharacterOobRefusedCount; // CONFIRMED-real bound reproduced (Sec49.1), not a new divergence
        return 0;
    }
    ++store.galleryDisplayCharacterAppliedCount;
    return 0;
}

// store_gallery_download_show_list (Sec49.1 CRASH): "reuses the exact
// null-dereference shape tranche 09 B.12 already found: the dialog-pool
// allocator can return null on exhaustion, and this function's own success
// path reads null+0x12c unchecked." HARDENED: refuses when the (test-
// populated) dialog pool reports exhausted.
int stub_store_gallery_download_show_list(lua_State* L) {
    logCall(L, upLog(L), "store_gallery_download_show_list", upStateTag(L));
    auto& store = upState(L)->storeFrontend();
    if (store.downloadDialogPoolExhausted.known() && store.downloadDialogPoolExhausted.get()) {
        ++store.galleryDownloadShowListPoolExhaustedRefusedCount; // HARDENED
        return 0;
    }
    ++store.galleryDownloadShowListCount;
    return 0;
}

// store_gallery_upload_character: "a deferred-upload scheduler."
int stub_store_gallery_upload_character(lua_State* L) {
    logCall(L, upLog(L), "store_gallery_upload_character", upStateTag(L));
    ++upState(L)->storeFrontend().galleryUploadScheduledCount;
    return 0;
}

// store_gang_begin_exit: part of "screen-stack pop with a confirmed-dead
// secondary branch" / "the store-camera subsystem shared by gang/
// stronghold/shop screens" glosses (Sec49.1's own text does not
// unambiguously attribute each of its 7 one-line glosses to one of these 8
// names 1:1 - flagged here as a CHOSEN, documented mapping ambiguity, not
// silently resolved): modelled as a plain call counter, consistent with
// every other unresolved-detail name in this family.
int stub_store_gang_begin_exit(lua_State* L) {
    logCall(L, upLog(L), "store_gang_begin_exit", upStateTag(L));
    ++upState(L)->storeFrontend().gangBeginExitCount;
    return 0;
}

// store_gang_bg_covered: see store_gang_begin_exit's own doc comment above
// for the same CHOSEN gloss-mapping ambiguity flag.
int stub_store_gang_bg_covered(lua_State* L) {
    logCall(L, upLog(L), "store_gang_bg_covered", upStateTag(L));
    (void)optionalBoolDefault(L, 1, false);
    ++upState(L)->storeFrontend().gangBgCoveredCount;
    return 0;
}

// store_gang_is_unlocked: "a player cash/unlock-bit gate for the gang
// store."
int stub_store_gang_is_unlocked(lua_State* L) {
    logCall(L, upLog(L), "store_gang_is_unlocked", upStateTag(L));
    auto& store = upState(L)->storeFrontend();
    ++store.gangIsUnlockedQueryCount;
    lua_pushboolean(L, store.gangUnlockedGate.get() ? 1 : 0); // OPEN until set
    return 1;
}

// store_stronghold_upgrade_end_flyby (Sec49.1 CRASH): "Ends the stronghold
// camera fly-by and, if a stronghold resolves, collects every 'unlocked at
// this level' entry into a 256-slot stack array with no cap on the match
// count." HARDENED: caps the collection at the CONFIRMED 256-slot bound
// explicitly rather than reproducing the unbounded write.
int stub_store_stronghold_upgrade_end_flyby(lua_State* L) {
    logCall(L, upLog(L), "store_stronghold_upgrade_end_flyby", upStateTag(L));
    EngineState* st = upState(L);
    auto& store = st->storeFrontend();
    if (st->stronghold().stillLocked.knownCount() > static_cast<size_t>(StoreFrontendState::kUnlockedAtLevelCapacity)) {
        ++store.strongholdUpgradeEndFlybyOverflowRefusedCount; // HARDENED (named CRASH: 256-slot stack array, no cap in the original)
        return 0;
    }
    ++store.strongholdUpgradeEndFlybyCount;
    return 0;
}

// store_vehicle_allow_garage: "a 13-named-mission garage-allow denylist."
int stub_store_vehicle_allow_garage(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_allow_garage", upStateTag(L));
    auto& store = upState(L)->storeFrontend();
    ++store.vehicleAllowGarageCount;
    lua_pushboolean(L, 1); // CHOSEN default: an unrecognized/not-yet-populated mission name is not on the (test-populated) denylist
    return 1;
}

// store_vehicle_do_return_to_crib: "screen-stack pop with a confirmed-dead
// secondary branch" (matching store_vehicle_retrieve_car_and_exit's own
// documented shape, Sec38.4).
int stub_store_vehicle_do_return_to_crib(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_do_return_to_crib", upStateTag(L));
    ++upState(L)->storeFrontend().vehicleDoReturnToCribCount;
    return 0;
}

// =========================================================================
// garage_* family (9 names) - Sec23.10, Sec41.7, Sec53.4.
// =========================================================================

// garage_get_repair_cost (Sec23.10, 0x005f8710): no arguments. Return: 1
// number (repair cost, or 0.0 if no vehicle is selected). CONFIRMED:
// "reads a 2-part 'currently selected garage vehicle' identity pair ... if
// both are zero, pushes 0.0 immediately."
int stub_garage_get_repair_cost(lua_State* L) {
    logCall(L, upLog(L), "garage_get_repair_cost", upStateTag(L));
    auto& garage = upState(L)->garageFrontend();
    if (!garage.hasSelectedVehicle || !garage.selectedVehicleRepairCost.known()) {
        lua_pushnumber(L, 0.0); // CONFIRMED default
        return 1;
    }
    lua_pushnumber(L, garage.selectedVehicleRepairCost.get());
    return 1;
}

// garage_repair_vehicle (Sec41.7 CRASH, 0x005f88d0): no arguments, no
// return. CONFIRMED "if that handle is unset, stale, dead, or not actually
// a vehicle, the vehicle pointer is left null - and the cost-calculation
// callee dereferences it unconditionally with no guard of its own ... the
// player is charged BEFORE the repair actually runs, with no visible
// affordability check." HARDENED: refuses (no charge, no repair) when
// hasSelectedVehicle is false - the real sibling handler's own fix
// template (checks resolution first); the "charge before repair, no
// affordability check" quirk is preserved faithfully for a VALID selection
// (a logic quirk, not the named crash this stub hardens).
int stub_garage_repair_vehicle(lua_State* L) {
    logCall(L, upLog(L), "garage_repair_vehicle", upStateTag(L));
    EngineState* st = upState(L);
    auto& garage = st->garageFrontend();
    if (!garage.hasSelectedVehicle || !garage.selectedVehicleRepairCost.known()) {
        ++garage.repairVehicleNoSelectionRefusedCount; // HARDENED (named CRASH: null vehicle into the cost function)
        return 0;
    }
    double cost = garage.selectedVehicleRepairCost.get();
    st->debitPlayerCash(cost); // CONFIRMED: charged BEFORE the repair runs, no affordability check - faithfully preserved
    ++garage.repairVehicleChargedCount;
    garage.repairVehicleTotalCharged += cost;
    return 0;
}

// garage_back_to_customize (Sec53.4): CONFIRMED "bounds the identical
// array correctly (< count)" - the real, already-safe sibling fix template
// garage_clear_new below is hardened to match.
int stub_garage_back_to_customize(lua_State* L) {
    logCall(L, upLog(L), "garage_back_to_customize", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    auto& garage = upState(L)->garageFrontend();
    if (index < 0 || index >= garage.entryCount) {
        ++garage.backToCustomizeOobRefusedCount; // CONFIRMED-real bound reproduced, not a new divergence
        return 0;
    }
    ++garage.backToCustomizeCount;
    return 0;
}

// garage_can_customize (Sec53.4, 0x005f87c0): "gated on exactly the same
// 12-mission denylist tranche 16's sibling store-access check already
// uses, plus a garage-type and entry-flag check of its own."
int stub_garage_can_customize(lua_State* L) {
    logCall(L, upLog(L), "garage_can_customize", upStateTag(L));
    ++upState(L)->garageFrontend().canCustomizeQueryCount;
    lua_pushboolean(L, 1); // CHOSEN default: not on the (test-populated) mission denylist unless a test says otherwise
    return 1;
}

// garage_can_delete (Sec53.4): "resolved in full, no further crash shapes."
int stub_garage_can_delete(lua_State* L) {
    logCall(L, upLog(L), "garage_can_delete", upStateTag(L));
    auto& garage = upState(L)->garageFrontend();
    ++garage.canDeleteQueryCount;
    lua_pushboolean(L, garage.selectedEntryIndex >= 0 ? 1 : 0);
    return 1;
}

// garage_clear_new (Sec53.4 CRASH, 0x005f8xxx approx): 1 list index.
// CONFIRMED "the bound check accepts index == count, not just indices
// strictly below it ... Its own sibling garage_back_to_customize bounds
// the identical array correctly (< count)." HARDENED: strict < bound,
// matching the sibling's own correct form (the spec's own cited fix
// template).
int stub_garage_clear_new(lua_State* L) {
    logCall(L, upLog(L), "garage_clear_new", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    auto& garage = upState(L)->garageFrontend();
    if (index < 0 || index >= garage.entryCount) { // HARDENED: strict <, NOT the real engine's <=
        ++garage.clearNewOobRefusedCount; // HARDENED (named CRASH: off-by-one, reads/writes one slot past the end)
        return 0;
    }
    garage.entries[static_cast<size_t>(index)].present = garage.entries[static_cast<size_t>(index)].present; // "new" flag internals OPEN - entry presence itself is this project's own observable stand-in
    ++garage.clearNewCount;
    return 0;
}

// garage_get_garage_type (Sec53.4): "resolved in full, no further crash
// shapes."
int stub_garage_get_garage_type(lua_State* L) {
    logCall(L, upLog(L), "garage_get_garage_type", upStateTag(L));
    auto& garage = upState(L)->garageFrontend();
    int32_t type = (garage.selectedEntryIndex >= 0 && garage.selectedEntryIndex < GarageFrontendState::kEntryCapacity)
                       ? garage.entries[static_cast<size_t>(garage.selectedEntryIndex)].garageType
                       : 0;
    lua_pushnumber(L, static_cast<lua_Number>(type));
    return 1;
}

// garage_remove_vehicle (Sec53.4 CRASH, 0x005fxxxx approx): no arguments,
// no return. CONFIRMED "a real double-release logic defect ... never
// clears the 'selected entry' global or removes the stale pointer from the
// 168-slot array afterward. A second call before a fresh selection ...
// hands the same record back to its free pool a second time." HARDENED:
// refuses a second call when selectedEntryIndex is already -1 (cleared
// after the first successful removal here, UNLIKE the original - the
// named fix).
int stub_garage_remove_vehicle(lua_State* L) {
    logCall(L, upLog(L), "garage_remove_vehicle", upStateTag(L));
    auto& garage = upState(L)->garageFrontend();
    if (garage.selectedEntryIndex < 0 || garage.selectedEntryIndex >= GarageFrontendState::kEntryCapacity ||
        !garage.entries[static_cast<size_t>(garage.selectedEntryIndex)].present) {
        ++garage.removeVehicleDoubleReleaseRefusedCount; // HARDENED (named CRASH: double-release of a recycled pool record)
        return 0;
    }
    garage.entries[static_cast<size_t>(garage.selectedEntryIndex)].present = false;
    if (garage.entryCount > 0) --garage.entryCount;
    garage.selectedEntryIndex = -1; // HARDENED: cleared here, UNLIKE the real engine (the named fix)
    ++garage.removeVehicleCount;
    return 0;
}

// garage_vehicle_load_pending (Sec53.4): "resolved in full, no further
// crash shapes."
int stub_garage_vehicle_load_pending(lua_State* L) {
    logCall(L, upLog(L), "garage_vehicle_load_pending", upStateTag(L));
    ++upState(L)->garageFrontend().vehicleLoadPendingQueryCount;
    lua_pushboolean(L, 0); // CHOSEN default: no real async load-pending tracker in scope
    return 1;
}

// =========================================================================
// crib_* family (6 names) + building_purchase_is_crib_being_purchased (1
// name, AGG) - Sec23.19, Sec57.3, Sec59.
// =========================================================================

// crib_has_garage_with_vehicles_of_type (Sec23.19, 0x0080e490): 1 number
// (vehicle-type id, only read if the player/garage-list gate passes). No
// return: 1 boolean. CONFIRMED: "if the current player is null, or its
// +0x209c field (the player's current crib record) is null, pushes false
// immediately with no Lua-stack reads at all."
int stub_crib_has_garage_with_vehicles_of_type(lua_State* L) {
    logCall(L, upLog(L), "crib_has_garage_with_vehicles_of_type", upStateTag(L));
    auto& crib = upState(L)->cribFrontend();
    if (!crib.hasCribRecord) {
        ++crib.hasGarageWithVehiclesFalseCount; // CONFIRMED: no crib record -> false, no further reads
        lua_pushboolean(L, 0);
        return 1;
    }
    int64_t vehicleType = truncateEa2596(lua_tonumber(L, 1));
    std::string key = std::to_string(vehicleType);
    bool has = crib.garageVehicleCountByType.known(key) && crib.garageVehicleCountByType.get(key) > 0;
    if (has) ++crib.hasGarageWithVehiclesTrueCount; else ++crib.hasGarageWithVehiclesFalseCount;
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

// crib_get_cash_stash (Sec57.3, naming trap): no arguments. CONFIRMED
// "despite its name, it returns NO VALUE AT ALL; it instead collects a
// stashed-cash global into the player's cash balance and zeroes the
// stash, discarding the money if the award could only be forwarded to a
// network owner rather than applied locally." Reproduced faithfully (0
// Lua values, not the stash amount).
int stub_crib_get_cash_stash(lua_State* L) {
    logCall(L, upLog(L), "crib_get_cash_stash", upStateTag(L));
    EngineState* st = upState(L);
    auto& crib = st->cribFrontend();
    if (st->hasLocalPlayer().known() && st->hasLocalPlayer().get()) {
        st->addPlayerCash(static_cast<double>(crib.cashStash));  // cash cap
        crib.cashStash = 0.0;
        ++crib.cashStashCollectedCount;
    } else {
        ++crib.cashStashNoLocalPlayerForwardCount; // CONFIRMED: forwarded-and-discarded path when not locally applicable
    }
    return 0; // CONFIRMED naming trap: NO return value, ever
}

// crib_is_garage_disabled (Sec57.3): "resolves in full with no crash
// shape."
int stub_crib_is_garage_disabled(lua_State* L) {
    logCall(L, upLog(L), "crib_is_garage_disabled", upStateTag(L));
    lua_pushboolean(L, upState(L)->cribFrontend().garageDisabled.get() ? 1 : 0); // OPEN until set
    return 1;
}

// crib_is_stronghold (Sec57.3): "its reading of the player's crib record
// directly confirms Sec23.19's own corrected field reading" (+0x148/+0x14c/
// +0x150 upgrade-level hash set).
int stub_crib_is_stronghold(lua_State* L) {
    logCall(L, upLog(L), "crib_is_stronghold", upStateTag(L));
    lua_pushboolean(L, upState(L)->cribFrontend().isStronghold.get() ? 1 : 0); // OPEN until set
    return 1;
}

// crib_purchase_purchase_crib (Sec57.3 CRASH, 0x005fxxxx approx): no
// arguments, no return. CONFIRMED "resolves the currently-selected-for-
// purchase crib handle with no fallback; if nothing is validly selected
// ... the crib pointer is left null and the VERY NEXT INSTRUCTION reads
// price through it ... the player's cash is ALWAYS DEDUCTED BEFORE the
// function checks whether the crib is already owned." HARDENED: refuses
// (no charge at all) when nothing is validly selected - the charge-before-
// ownership-check ordering ITSELF is preserved faithfully for a valid
// selection (a logic defect, not the memory-safety one this stub hardens).
int stub_crib_purchase_purchase_crib(lua_State* L) {
    logCall(L, upLog(L), "crib_purchase_purchase_crib", upStateTag(L));
    EngineState* st = upState(L);
    auto& crib = st->cribFrontend();
    if (!crib.hasSelectedPurchaseCrib) {
        ++crib.purchaseCribNoSelectionRefusedCount; // HARDENED (named CRASH: null read with no crib selected)
        return 0;
    }
    std::string key = "selected";
    double price = crib.cribPurchasePrice.known(key) ? crib.cribPurchasePrice.get(key) : 0.0;
    st->debitPlayerCash(price); // CONFIRMED: charged BEFORE the ownership check - faithfully preserved
    ++crib.purchaseCribChargedCount;
    crib.purchaseCribTotalCharged += price;
    if (!crib.cribAlreadyOwned) crib.cribAlreadyOwned = true; // CONFIRMED: ownership flag only checked/set AFTER the charge
    return 0;
}

// crib_purchasing_unlock (Sec57/T23, AGG): no individual per-function body
// given in spec scope. AGG discipline: thin call counter only.
int stub_crib_purchasing_unlock(lua_State* L) {
    logCall(L, upLog(L), "crib_purchasing_unlock", upStateTag(L));
    ++upState(L)->cribFrontend().purchasingUnlockCount;
    return 0;
}

// building_purchase_is_crib_being_purchased (Sec59/T24, AGG): no
// individual per-function body given in spec scope. AGG discipline: a
// pure-query shell, no invented true/false default beyond "not being
// purchased" (the honest no-producer default, consistent with this
// project's established convention elsewhere).
int stub_building_purchase_is_crib_being_purchased(lua_State* L) {
    logCall(L, upLog(L), "building_purchase_is_crib_being_purchased", upStateTag(L));
    ++upState(L)->cribFrontend().isCribBeingPurchasedQueryCount;
    lua_pushboolean(L, 0);
    return 1;
}

// =========================================================================
// gang_customization_* family (6 names) - Sec53.1, Sec56.1. 4 of this
// batch's own 4 NAMED memory-safety findings live here.
// =========================================================================

// gang_customization_select_vehicle (Sec53.1 MEMSAFE - NAMED finding):
// "real arbitrary-address write, script-controlled." 2 numbers: a visible-
// catalogue position and a GANG-VEHICLE SLOT. CONFIRMED "The slot argument
// indexes a 3-slot global array with absolutely no bound check ... slot 3
// overwrites the very catalogue-category pointer the same function is
// still reading from ... any other value writes an arbitrary item pointer
// to array_base + 4xslot, anywhere in the address space." HARDENED (THE
// NAMED FIX): rejects any slot outside 0-2, per the spec's own explicit
// "a binding layer must reject any slot outside 0-2" instruction; the
// catalogue position is resolved via a test-populated registry (no real
// catalogue table in scope), refused when unresolved.
int stub_gang_customization_select_vehicle(lua_State* L) {
    logCall(L, upLog(L), "gang_customization_select_vehicle", upStateTag(L));
    int64_t position = truncateEa2596(lua_tonumber(L, 1));
    int64_t slot = truncateEa2596(lua_tonumber(L, 2));
    auto& gang = upState(L)->gangCustomization();
    if (slot < 0 || slot >= GangCustomizationState::kVehicleSlotCount) {
        ++gang.selectVehicleOobRefusedCount; // HARDENED - THE NAMED FIX (real engine: arbitrary-address write)
        return 0;
    }
    gang.vehicleSlotItemIndex[static_cast<size_t>(slot)] = static_cast<int32_t>(position); // CHOSEN: catalogue-position resolve itself is OPEN (no real catalogue table in scope) - the slot write is what this name's own CONFIRMED finding targets
    ++gang.selectVehicleCount;
    return 0;
}

// gang_customization_confirm_vehicle (Sec53.1/Sec56.1): CONFIRMED
// "reconfirms tranche 20's unbounded 3-slot array read/write" - reads
// through the identical unchecked slot index. HARDENED: SAME 0-2 bound as
// select_vehicle above.
int stub_gang_customization_confirm_vehicle(lua_State* L) {
    logCall(L, upLog(L), "gang_customization_confirm_vehicle", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& gang = upState(L)->gangCustomization();
    if (slot < 0 || slot >= GangCustomizationState::kVehicleSlotCount) {
        ++gang.confirmVehicleOobRefusedCount; // HARDENED
        return 0;
    }
    gang.previewedVehicleBaselineIndex = gang.vehicleSlotItemIndex[static_cast<size_t>(slot)];
    gang.previewedVehicleBaselineSet = true;
    gang.previewedVehicleBaselineStale = false; // CONFIRMED: a fresh confirm makes the baseline live again
    ++gang.confirmVehicleCount;
    return 0;
}

// gang_customization_revert_vehicle (Sec53.1/Sec56.1 CRASH): CONFIRMED
// "also WRITES through [the 3-slot array] unchecked" plus a SECOND, new
// crash (Sec56.1): "if revert_vehicle runs before the vehicle 'baseline'
// slot is ever set (zero-fill by default), it writes NULL into the slot
// and hands that null to the preview-vehicle spawner, which reaches a
// callee that reads the item's count/array fields at offset 0x24 with no
// null check on its first argument." HARDENED: SAME 0-2 bound, PLUS
// refuses the baseline write when the baseline has NEVER been set at all
// (`!previewedVehicleBaselineSet`) - the real, spec-confirmed trigger for
// this 2nd crash. CORRECTED (adversarial review): a prior version of this
// stub treated the never-set case as a faithful success no-op (wrongly
// citing it as "correctly skips the hazard") and instead keyed its ONE
// refusal on `previewedVehicleBaselineStale` - that field models a
// DIFFERENT bug entirely (Sec53.1's OWN select_vehicle preview-release
// null+0x16c0 write, a separate, not-yet-hardened issue of its own this
// function must not borrow). The never-set case is the actual hazard here
// and must refuse; `revertVehicleCount` now increments ONLY on a genuine,
// real revert, never on a refusal.
int stub_gang_customization_revert_vehicle(lua_State* L) {
    logCall(L, upLog(L), "gang_customization_revert_vehicle", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& gang = upState(L)->gangCustomization();
    if (slot < 0 || slot >= GangCustomizationState::kVehicleSlotCount) {
        ++gang.revertVehicleOobRefusedCount; // HARDENED
        return 0;
    }
    if (!gang.previewedVehicleBaselineSet) {
        // HARDENED (THE NAMED FIX, Sec56.1's 2nd crash): baseline never set
        // (zero-fill default) - refuse rather than write null into the slot
        // and hand it to the null-unsafe preview-vehicle spawner.
        ++gang.revertVehicleNullBaselineRefusedCount;
        return 0;
    }
    gang.vehicleSlotItemIndex[static_cast<size_t>(slot)] = gang.previewedVehicleBaselineIndex;
    ++gang.revertVehicleCount;
    return 0;
}

// gang_customization_select_gang_sign (Sec56.1 MEMSAFE - NAMED finding):
// "two crash shapes. Mode 0 previews a sign by list position with no bound
// check at all ... Mode 1 is a deterministic, non-data-gated null-pointer
// dereference ... Every mode-1 call crashes, full stop." HARDENED: mode 0
// refuses an unresolved position (test-populated registry, bound genuinely
// OPEN - not invented, Sec56.1's own residual item); mode 1 is refused
// UNCONDITIONALLY for every input (THE NAMED FIX - never reproduced, since
// the real crash is deterministic and data-independent).
int stub_gang_customization_select_gang_sign(lua_State* L) {
    logCall(L, upLog(L), "gang_customization_select_gang_sign", upStateTag(L));
    int64_t mode = truncateEa2596(lua_tonumber(L, 1));
    int64_t position = truncateEa2596(optionalNumberDefault(L, 2, 0.0));
    auto& gang = upState(L)->gangCustomization();
    if (mode == 1) {
        ++gang.selectGangSignMode1RefusedCount; // HARDENED - THE NAMED FIX (real engine: deterministic null-vtable call, every call)
        return 0;
    }
    if (mode != 0) {
        ++gang.selectGangSignUnhandledModeCount; // CHOSEN: no further mode is documented - treated as a safe no-op
        return 0;
    }
    std::string key = std::to_string(position);
    if (!gang.signPositionResolves.known(key) || !gang.signPositionResolves.get(key)) {
        ++gang.selectGangSignMode0UnresolvedRefusedCount; // HARDENED (bound genuinely OPEN - refuses rather than inventing one)
        return 0;
    }
    ++gang.selectGangSignMode0Count;
    return 0;
}

// gang_customization_confirm_gang_sign (Sec56.1 MEMSAFE - NAMED finding):
// "Same unchecked list position as select_gang_sign mode 0, but here the
// out-of-range RECORD POINTER ITSELF IS STORED, later dereferenced by the
// sign-list rebuild with no check, and WRITTEN INTO THE SAVE FILE as a
// truncated byte index that the loader trusts unconditionally on the next
// load - so a single out-of-range call can corrupt a save slot." HARDENED
// (THE NAMED FIX): refuses (no store, no persist) when the position does
// not resolve.
int stub_gang_customization_confirm_gang_sign(lua_State* L) {
    logCall(L, upLog(L), "gang_customization_confirm_gang_sign", upStateTag(L));
    int64_t position = truncateEa2596(lua_tonumber(L, 1));
    auto& gang = upState(L)->gangCustomization();
    std::string key = std::to_string(position);
    if (!gang.signPositionResolves.known(key) || !gang.signPositionResolves.get(key)) {
        ++gang.confirmGangSignUnresolvedRefusedCount; // HARDENED - THE NAMED FIX (real engine: persists an OOB index to the save file)
        return 0;
    }
    gang.confirmedSignPosition = static_cast<int32_t>(position);
    gang.confirmedSignPositionSet = true;
    ++gang.confirmGangSignCount;
    return 0;
}

// gang_customization_revert_gang_sign (Sec56.1): CONFIRMED "restores the
// sign recorded when the sign list was last built; if that list was never
// built, the restored pointer is null, deferring the same crash/corruption
// shape to the next list build and the next save (reachability
// HYPOTHESIS)." HARDENED: refuses when no prior confirm/build has
// happened, matching confirm_gang_sign's own resolved-position discipline.
int stub_gang_customization_revert_gang_sign(lua_State* L) {
    logCall(L, upLog(L), "gang_customization_revert_gang_sign", upStateTag(L));
    auto& gang = upState(L)->gangCustomization();
    if (!gang.confirmedSignPositionSet) {
        ++gang.revertGangSignNoPriorBuildRefusedCount; // HARDENED (deferred null shape, Sec56.1)
        return 0;
    }
    ++gang.revertGangSignCount;
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB25() {
    static const std::vector<std::string> names = {
        // vcust_*.
        "vcust_revert_color",
        "vcust_preview_component",
        "vcust_is_camera_tool_mode",
        "vcust_preview_color",
        "vcust_preview_palette",
        "vcust_purchase_wheels",
        "vcust_adjust_camera_angle",
        "vcust_change_top_level_menu",
        "vcust_preview_rim_sizing",
        "vcust_purchase_component",
        "vcust_purchase_paint",
        "vcust_purchase_performance_upgrade",
        "vcust_select_paint_slot",
        "vcust_set_underglow",
        "vcust_show_nos_hydraulic_warning",
        // store_*.
        "store_dlc_make_purchase",
        "store_weapon_purchase_weapon",
        "store_vehicle_change_mode",
        "store_vehicle_retrieve_car_and_exit",
        "store_vehicle_selection_should_lock_controls",
        "store_weapon_change_weapon",
        "store_weapon_process_post_bg_covered",
        "store_character_lineup_loaded",
        "store_clothing_get_store_id",
        "store_dlc_is_offer_free",
        "store_dlc_queue_icon",
        "store_vehicle_no_vehicle_exit",
        "store_vehicle_notify_screen_covered",
        "store_crib_init_crib_garage",
        "store_gallery_display_character",
        "store_gallery_download_show_list",
        "store_gallery_upload_character",
        "store_gang_begin_exit",
        "store_gang_bg_covered",
        "store_gang_is_unlocked",
        "store_stronghold_upgrade_end_flyby",
        "store_vehicle_allow_garage",
        "store_vehicle_do_return_to_crib",
        // garage_*.
        "garage_get_repair_cost",
        "garage_repair_vehicle",
        "garage_back_to_customize",
        "garage_can_customize",
        "garage_can_delete",
        "garage_clear_new",
        "garage_get_garage_type",
        "garage_remove_vehicle",
        "garage_vehicle_load_pending",
        // crib_* + building_*.
        "crib_has_garage_with_vehicles_of_type",
        "crib_get_cash_stash",
        "crib_is_garage_disabled",
        "crib_is_stronghold",
        "crib_purchase_purchase_crib",
        "crib_purchasing_unlock",
        "building_purchase_is_crib_being_purchased",
        // gang_customization_*.
        "gang_customization_select_vehicle",
        "gang_customization_confirm_vehicle",
        "gang_customization_revert_vehicle",
        "gang_customization_select_gang_sign",
        "gang_customization_confirm_gang_sign",
        "gang_customization_revert_gang_sign",
    };
    return names;
}

void registerB25(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // vcust_*.
    if (wants("vcust_revert_color")) registerOne(L, state, log, stateTag, "vcust_revert_color", openGuard<stub_vcust_revert_color>);
    if (wants("vcust_preview_component")) registerOne(L, state, log, stateTag, "vcust_preview_component", openGuard<stub_vcust_preview_component>);
    if (wants("vcust_is_camera_tool_mode")) registerOne(L, state, log, stateTag, "vcust_is_camera_tool_mode", openGuard<stub_vcust_is_camera_tool_mode>);
    if (wants("vcust_preview_color")) registerOne(L, state, log, stateTag, "vcust_preview_color", openGuard<stub_vcust_preview_color>);
    if (wants("vcust_preview_palette")) registerOne(L, state, log, stateTag, "vcust_preview_palette", openGuard<stub_vcust_preview_palette>);
    if (wants("vcust_purchase_wheels")) registerOne(L, state, log, stateTag, "vcust_purchase_wheels", openGuard<stub_vcust_purchase_wheels>);
    if (wants("vcust_adjust_camera_angle")) registerOne(L, state, log, stateTag, "vcust_adjust_camera_angle", openGuard<stub_vcust_adjust_camera_angle>);
    if (wants("vcust_change_top_level_menu")) registerOne(L, state, log, stateTag, "vcust_change_top_level_menu", openGuard<stub_vcust_change_top_level_menu>);
    if (wants("vcust_preview_rim_sizing")) registerOne(L, state, log, stateTag, "vcust_preview_rim_sizing", openGuard<stub_vcust_preview_rim_sizing>);
    if (wants("vcust_purchase_component")) registerOne(L, state, log, stateTag, "vcust_purchase_component", openGuard<stub_vcust_purchase_component>);
    if (wants("vcust_purchase_paint")) registerOne(L, state, log, stateTag, "vcust_purchase_paint", openGuard<stub_vcust_purchase_paint>);
    if (wants("vcust_purchase_performance_upgrade")) registerOne(L, state, log, stateTag, "vcust_purchase_performance_upgrade", openGuard<stub_vcust_purchase_performance_upgrade>);
    if (wants("vcust_select_paint_slot")) registerOne(L, state, log, stateTag, "vcust_select_paint_slot", openGuard<stub_vcust_select_paint_slot>);
    if (wants("vcust_set_underglow")) registerOne(L, state, log, stateTag, "vcust_set_underglow", openGuard<stub_vcust_set_underglow>);
    if (wants("vcust_show_nos_hydraulic_warning")) registerOne(L, state, log, stateTag, "vcust_show_nos_hydraulic_warning", openGuard<stub_vcust_show_nos_hydraulic_warning>);
    // store_*.
    if (wants("store_dlc_make_purchase")) registerOne(L, state, log, stateTag, "store_dlc_make_purchase", openGuard<stub_store_dlc_make_purchase>);
    if (wants("store_weapon_purchase_weapon")) registerOne(L, state, log, stateTag, "store_weapon_purchase_weapon", openGuard<stub_store_weapon_purchase_weapon>);
    if (wants("store_vehicle_change_mode")) registerOne(L, state, log, stateTag, "store_vehicle_change_mode", openGuard<stub_store_vehicle_change_mode>);
    if (wants("store_vehicle_retrieve_car_and_exit")) registerOne(L, state, log, stateTag, "store_vehicle_retrieve_car_and_exit", openGuard<stub_store_vehicle_retrieve_car_and_exit>);
    if (wants("store_vehicle_selection_should_lock_controls")) registerOne(L, state, log, stateTag, "store_vehicle_selection_should_lock_controls", openGuard<stub_store_vehicle_selection_should_lock_controls>);
    if (wants("store_weapon_change_weapon")) registerOne(L, state, log, stateTag, "store_weapon_change_weapon", openGuard<stub_store_weapon_change_weapon>);
    if (wants("store_weapon_process_post_bg_covered")) registerOne(L, state, log, stateTag, "store_weapon_process_post_bg_covered", openGuard<stub_store_weapon_process_post_bg_covered>);
    if (wants("store_character_lineup_loaded")) registerOne(L, state, log, stateTag, "store_character_lineup_loaded", openGuard<stub_store_character_lineup_loaded>);
    if (wants("store_clothing_get_store_id")) registerOne(L, state, log, stateTag, "store_clothing_get_store_id", openGuard<stub_store_clothing_get_store_id>);
    if (wants("store_dlc_is_offer_free")) registerOne(L, state, log, stateTag, "store_dlc_is_offer_free", openGuard<stub_store_dlc_is_offer_free>);
    if (wants("store_dlc_queue_icon")) registerOne(L, state, log, stateTag, "store_dlc_queue_icon", openGuard<stub_store_dlc_queue_icon>);
    if (wants("store_vehicle_no_vehicle_exit")) registerOne(L, state, log, stateTag, "store_vehicle_no_vehicle_exit", openGuard<stub_store_vehicle_no_vehicle_exit>);
    if (wants("store_vehicle_notify_screen_covered")) registerOne(L, state, log, stateTag, "store_vehicle_notify_screen_covered", openGuard<stub_store_vehicle_notify_screen_covered>);
    if (wants("store_crib_init_crib_garage")) registerOne(L, state, log, stateTag, "store_crib_init_crib_garage", openGuard<stub_store_crib_init_crib_garage>);
    if (wants("store_gallery_display_character")) registerOne(L, state, log, stateTag, "store_gallery_display_character", openGuard<stub_store_gallery_display_character>);
    if (wants("store_gallery_download_show_list")) registerOne(L, state, log, stateTag, "store_gallery_download_show_list", openGuard<stub_store_gallery_download_show_list>);
    if (wants("store_gallery_upload_character")) registerOne(L, state, log, stateTag, "store_gallery_upload_character", openGuard<stub_store_gallery_upload_character>);
    if (wants("store_gang_begin_exit")) registerOne(L, state, log, stateTag, "store_gang_begin_exit", openGuard<stub_store_gang_begin_exit>);
    if (wants("store_gang_bg_covered")) registerOne(L, state, log, stateTag, "store_gang_bg_covered", openGuard<stub_store_gang_bg_covered>);
    if (wants("store_gang_is_unlocked")) registerOne(L, state, log, stateTag, "store_gang_is_unlocked", openGuard<stub_store_gang_is_unlocked>);
    if (wants("store_stronghold_upgrade_end_flyby")) registerOne(L, state, log, stateTag, "store_stronghold_upgrade_end_flyby", openGuard<stub_store_stronghold_upgrade_end_flyby>);
    if (wants("store_vehicle_allow_garage")) registerOne(L, state, log, stateTag, "store_vehicle_allow_garage", openGuard<stub_store_vehicle_allow_garage>);
    if (wants("store_vehicle_do_return_to_crib")) registerOne(L, state, log, stateTag, "store_vehicle_do_return_to_crib", openGuard<stub_store_vehicle_do_return_to_crib>);
    // garage_*.
    if (wants("garage_get_repair_cost")) registerOne(L, state, log, stateTag, "garage_get_repair_cost", openGuard<stub_garage_get_repair_cost>);
    if (wants("garage_repair_vehicle")) registerOne(L, state, log, stateTag, "garage_repair_vehicle", openGuard<stub_garage_repair_vehicle>);
    if (wants("garage_back_to_customize")) registerOne(L, state, log, stateTag, "garage_back_to_customize", openGuard<stub_garage_back_to_customize>);
    if (wants("garage_can_customize")) registerOne(L, state, log, stateTag, "garage_can_customize", openGuard<stub_garage_can_customize>);
    if (wants("garage_can_delete")) registerOne(L, state, log, stateTag, "garage_can_delete", openGuard<stub_garage_can_delete>);
    if (wants("garage_clear_new")) registerOne(L, state, log, stateTag, "garage_clear_new", openGuard<stub_garage_clear_new>);
    if (wants("garage_get_garage_type")) registerOne(L, state, log, stateTag, "garage_get_garage_type", openGuard<stub_garage_get_garage_type>);
    if (wants("garage_remove_vehicle")) registerOne(L, state, log, stateTag, "garage_remove_vehicle", openGuard<stub_garage_remove_vehicle>);
    if (wants("garage_vehicle_load_pending")) registerOne(L, state, log, stateTag, "garage_vehicle_load_pending", openGuard<stub_garage_vehicle_load_pending>);
    // crib_* + building_*.
    if (wants("crib_has_garage_with_vehicles_of_type")) registerOne(L, state, log, stateTag, "crib_has_garage_with_vehicles_of_type", openGuard<stub_crib_has_garage_with_vehicles_of_type>);
    if (wants("crib_get_cash_stash")) registerOne(L, state, log, stateTag, "crib_get_cash_stash", openGuard<stub_crib_get_cash_stash>);
    if (wants("crib_is_garage_disabled")) registerOne(L, state, log, stateTag, "crib_is_garage_disabled", openGuard<stub_crib_is_garage_disabled>);
    if (wants("crib_is_stronghold")) registerOne(L, state, log, stateTag, "crib_is_stronghold", openGuard<stub_crib_is_stronghold>);
    if (wants("crib_purchase_purchase_crib")) registerOne(L, state, log, stateTag, "crib_purchase_purchase_crib", openGuard<stub_crib_purchase_purchase_crib>);
    if (wants("crib_purchasing_unlock")) registerOne(L, state, log, stateTag, "crib_purchasing_unlock", openGuard<stub_crib_purchasing_unlock>);
    if (wants("building_purchase_is_crib_being_purchased")) registerOne(L, state, log, stateTag, "building_purchase_is_crib_being_purchased", openGuard<stub_building_purchase_is_crib_being_purchased>);
    // gang_customization_*.
    if (wants("gang_customization_select_vehicle")) registerOne(L, state, log, stateTag, "gang_customization_select_vehicle", openGuard<stub_gang_customization_select_vehicle>);
    if (wants("gang_customization_confirm_vehicle")) registerOne(L, state, log, stateTag, "gang_customization_confirm_vehicle", openGuard<stub_gang_customization_confirm_vehicle>);
    if (wants("gang_customization_revert_vehicle")) registerOne(L, state, log, stateTag, "gang_customization_revert_vehicle", openGuard<stub_gang_customization_revert_vehicle>);
    if (wants("gang_customization_select_gang_sign")) registerOne(L, state, log, stateTag, "gang_customization_select_gang_sign", openGuard<stub_gang_customization_select_gang_sign>);
    if (wants("gang_customization_confirm_gang_sign")) registerOne(L, state, log, stateTag, "gang_customization_confirm_gang_sign", openGuard<stub_gang_customization_confirm_gang_sign>);
    if (wants("gang_customization_revert_gang_sign")) registerOne(L, state, log, stateTag, "gang_customization_revert_gang_sign", openGuard<stub_gang_customization_revert_gang_sign>);
}

} // namespace stub_sections
} // namespace sr3luahost
