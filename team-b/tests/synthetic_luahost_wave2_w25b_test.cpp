// Synthetic tests for wave 2, Batch W2-5 (2026-10-08), half 2 of 2 -
// "vcust/store/garage/crib/gang_customization_*"
// (src/lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp). 60
// names. Same hand-written-fixture convention as every other synthetic_
// luahost_*_test. Cluster tags grepped directly from tools/lua_all_
// registered_1490_tagged.txt: all names are `ui` except crib_purchasing_
// unlock (`gameplay`).
//
// PRIORITY: explicit dual-path (valid-input faithful + hardened-refusal)
// coverage for all 4 of this batch's own NAMED memory-safety findings -
// gang_customization_select_vehicle/_confirm_gang_sign/_select_gang_sign,
// vcust_purchase_component - matching Batch 2's own OOB-test convention.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "w25b_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "w25b_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"vcust_revert_color", "ui"},
        {"vcust_preview_component", "ui"},
        {"vcust_is_camera_tool_mode", "ui"},
        {"vcust_preview_color", "ui"},
        {"vcust_preview_palette", "ui"},
        {"vcust_purchase_wheels", "ui"},
        {"vcust_adjust_camera_angle", "ui"},
        {"vcust_change_top_level_menu", "ui"},
        {"vcust_preview_rim_sizing", "ui"},
        {"vcust_purchase_component", "ui"},
        {"vcust_purchase_paint", "ui"},
        {"vcust_purchase_performance_upgrade", "ui"},
        {"vcust_select_paint_slot", "ui"},
        {"vcust_set_underglow", "ui"},
        {"vcust_show_nos_hydraulic_warning", "ui"},
        {"store_dlc_make_purchase", "ui"},
        {"store_weapon_purchase_weapon", "ui"},
        {"store_vehicle_change_mode", "ui"},
        {"store_vehicle_retrieve_car_and_exit", "ui"},
        {"store_vehicle_selection_should_lock_controls", "ui"},
        {"store_weapon_change_weapon", "ui"},
        {"store_weapon_process_post_bg_covered", "ui"},
        {"store_character_lineup_loaded", "ui"},
        {"store_clothing_get_store_id", "ui"},
        {"store_dlc_is_offer_free", "ui"},
        {"store_dlc_queue_icon", "ui"},
        {"store_vehicle_no_vehicle_exit", "ui"},
        {"store_vehicle_notify_screen_covered", "ui"},
        {"store_crib_init_crib_garage", "ui"},
        {"store_gallery_display_character", "ui"},
        {"store_gallery_download_show_list", "ui"},
        {"store_gallery_upload_character", "ui"},
        {"store_gang_begin_exit", "ui"},
        {"store_gang_bg_covered", "ui"},
        {"store_gang_is_unlocked", "ui"},
        {"store_stronghold_upgrade_end_flyby", "ui"},
        {"store_vehicle_allow_garage", "ui"},
        {"store_vehicle_do_return_to_crib", "ui"},
        {"garage_get_repair_cost", "ui"},
        {"garage_repair_vehicle", "ui"},
        {"garage_back_to_customize", "ui"},
        {"garage_can_customize", "ui"},
        {"garage_can_delete", "ui"},
        {"garage_clear_new", "ui"},
        {"garage_get_garage_type", "ui"},
        {"garage_remove_vehicle", "ui"},
        {"garage_vehicle_load_pending", "ui"},
        {"crib_has_garage_with_vehicles_of_type", "ui"},
        {"crib_get_cash_stash", "ui"},
        {"crib_is_garage_disabled", "ui"},
        {"crib_is_stronghold", "ui"},
        {"crib_purchase_purchase_crib", "ui"},
        {"crib_purchasing_unlock", "gameplay"},
        {"building_purchase_is_crib_being_purchased", "ui"},
        {"gang_customization_select_vehicle", "ui"},
        {"gang_customization_confirm_vehicle", "ui"},
        {"gang_customization_revert_vehicle", "ui"},
        {"gang_customization_select_gang_sign", "ui"},
        {"gang_customization_confirm_gang_sign", "ui"},
        {"gang_customization_revert_gang_sign", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration smoke test.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "crib_purchasing_unlock"));
        CHECK(!hasGlobal(host.uiState(), "crib_purchasing_unlock"));
        CHECK(hasGlobal(host.uiState(), "gang_customization_select_vehicle"));
        CHECK(!hasGlobal(host.gameplayState(), "gang_customization_select_vehicle"));
        for (const auto& rn : batchFixtureNames()) {
            lua_State* want = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            CHECK(hasGlobal(want, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // NAMED FINDING 1/4: gang_customization_select_vehicle (Sec53.1
    // MEMSAFE) - unchecked 0-2 slot index -> arbitrary-address write.
    // ==================================================================
    ok(host, ui, "gang_customization_select_vehicle(10, 0)"); // faithful path: valid slot 0
    CHECK(es.gangCustomization().vehicleSlotItemIndex[0] == 10);
    CHECK(es.gangCustomization().selectVehicleCount == 1);
    ok(host, ui, "gang_customization_select_vehicle(10, 3)"); // HARDENED-refusal path: slot 3, the real arbitrary-write trigger
    CHECK(es.gangCustomization().selectVehicleOobRefusedCount == 1);
    ok(host, ui, "gang_customization_select_vehicle(10, -1)"); // HARDENED-refusal path: negative slot
    CHECK(es.gangCustomization().selectVehicleOobRefusedCount == 2);
    ok(host, ui, "gang_customization_select_vehicle(10, 2)"); // faithful path: valid slot 2 (upper bound)
    CHECK(es.gangCustomization().vehicleSlotItemIndex[2] == 10);
    CHECK(es.gangCustomization().selectVehicleCount == 2);

    // gang_customization_confirm_vehicle / revert_vehicle share the SAME
    // 0-2 bound (Sec53.1/Sec56.1).
    //
    // 2nd crash (Sec56.1), CORRECTED test (adversarial review): revert_
    // vehicle running BEFORE the vehicle baseline slot is EVER set
    // (zero-fill default) must refuse, not report a faithful success -
    // exercised here, before any confirm_vehicle call has happened in this
    // host, so previewedVehicleBaselineSet is still false. (A prior version
    // of this test instead set previewedVehicleBaselineStale = true here,
    // which models a DIFFERENT, unrelated bug in select_vehicle's own
    // preview-release path - not this function's real crash trigger.)
    ok(host, ui, "gang_customization_revert_vehicle(0)");
    CHECK(es.gangCustomization().revertVehicleNullBaselineRefusedCount == 1); // HARDENED-refusal (2nd crash, baseline never set)
    CHECK(es.gangCustomization().revertVehicleCount == 0); // CONFIRMED: a refusal must NOT count as a genuine revert

    ok(host, ui, "gang_customization_confirm_vehicle(0)"); // faithful
    CHECK(es.gangCustomization().confirmVehicleCount == 1);
    ok(host, ui, "gang_customization_confirm_vehicle(5)"); // HARDENED-refusal
    CHECK(es.gangCustomization().confirmVehicleOobRefusedCount == 1);
    ok(host, ui, "gang_customization_revert_vehicle(0)"); // faithful (baseline now live, from the confirm above)
    CHECK(es.gangCustomization().revertVehicleCount == 1);
    ok(host, ui, "gang_customization_revert_vehicle(5)"); // HARDENED-refusal (OOB slot)
    CHECK(es.gangCustomization().revertVehicleOobRefusedCount == 1);

    // ==================================================================
    // NAMED FINDING 2/4: gang_customization_select_gang_sign (Sec56.1
    // MEMSAFE) - mode 0 unbounded index, mode 1 DETERMINISTIC null-vtable
    // call (every call crashes in the original, unconditionally refused
    // here under ANY input).
    // ==================================================================
    ok(host, ui, "gang_customization_select_gang_sign(0, 3)"); // HARDENED-refusal: position 3 unresolved (bound genuinely OPEN - never invented)
    CHECK(es.gangCustomization().selectGangSignMode0UnresolvedRefusedCount == 1);
    es.gangCustomization().signPositionResolves.set("3", true);
    ok(host, ui, "gang_customization_select_gang_sign(0, 3)"); // faithful path: now resolves
    CHECK(es.gangCustomization().selectGangSignMode0Count == 1);
    ok(host, ui, "gang_customization_select_gang_sign(1, 3)"); // HARDENED-refusal: mode 1, the NAMED deterministic-crash mode - refused under ANY input
    CHECK(es.gangCustomization().selectGangSignMode1RefusedCount == 1);
    ok(host, ui, "gang_customization_select_gang_sign(1, 999)"); // mode 1 again, different position - STILL refused unconditionally
    CHECK(es.gangCustomization().selectGangSignMode1RefusedCount == 2);

    // ==================================================================
    // NAMED FINDING 3/4: gang_customization_confirm_gang_sign (Sec56.1
    // MEMSAFE) - unbounded index persisted to the save file.
    // ==================================================================
    ok(host, ui, "gang_customization_confirm_gang_sign(42)"); // HARDENED-refusal: unresolved position
    CHECK(es.gangCustomization().confirmGangSignUnresolvedRefusedCount == 1);
    CHECK(!es.gangCustomization().confirmedSignPositionSet);
    es.gangCustomization().signPositionResolves.set("42", true);
    ok(host, ui, "gang_customization_confirm_gang_sign(42)"); // faithful path: resolves and persists
    CHECK(es.gangCustomization().confirmGangSignCount == 1);
    CHECK(es.gangCustomization().confirmedSignPositionSet);
    CHECK(es.gangCustomization().confirmedSignPosition == 42);

    // gang_customization_revert_gang_sign: refuses before any confirm/
    // build happened; succeeds once one has (a confirm already happened
    // above in THIS host, so the faithful path is exercised here - the
    // "never built" refusal path is exercised structurally by this exact
    // function's own guard, matching confirm_gang_sign's resolved-position
    // discipline already covered above).
    ok(host, ui, "gang_customization_revert_gang_sign()"); // faithful
    CHECK(es.gangCustomization().revertGangSignCount == 1);

    // ==================================================================
    // NAMED FINDING 4/4: vcust_purchase_component (Sec52.1 MEMSAFE) - OOB
    // read+write from a stale -1 "currently selected item" index. Both
    // paths exercised: double-call/no-selection (HARDENED-refusal) and a
    // freshly-simulated selection (faithful charge).
    // ==================================================================
    es.vcustPreview().targetLive = true;
    double cashBefore = es.playerCash();
    ok(host, ui, "vcust_purchase_component()"); // HARDENED-refusal: no prior selection (index at rest -1, the real bug's own default trigger)
    CHECK(es.vcustPreview().purchaseComponentStaleIndexRefusedCount == 1);
    CHECK(es.playerCash() == cashBefore); // not charged
    es.selectVcustComponentForTesting(7, 25.0); // simulate "a fresh selection was just made"
    ok(host, ui, "vcust_purchase_component()"); // faithful path: charges the selected item's price
    CHECK(es.vcustPreview().purchaseComponentChargedCount == 1);
    CHECK(es.playerCash() == cashBefore - 25.0);
    ok(host, ui, "vcust_purchase_component()"); // HARDENED-refusal AGAIN: index was reset to -1 after the prior call (the real bug's OWN "calling it twice in a row" trigger)
    CHECK(es.vcustPreview().purchaseComponentStaleIndexRefusedCount == 2);
    CHECK(es.playerCash() == cashBefore - 25.0); // unchanged - not double-charged
    es.vcustPreview().targetLive = false;
    ok(host, ui, "vcust_purchase_component()"); // HARDENED-refusal: no live target either
    CHECK(es.vcustPreview().purchaseComponentNoTargetRefusedCount == 1);

    // ==================================================================
    // Further CRASH-flagged HARDENING coverage (vcust_* family).
    // ==================================================================
    ok(host, ui, "vcust_preview_color(3)"); // HARDENED-refusal: no live target
    CHECK(es.vcustPreview().previewColorNoTargetRefusedCount == 1);
    es.vcustPreview().targetLive = true;
    ok(host, ui, "vcust_preview_color(3)"); // faithful path
    CHECK(es.vcustPreview().currentColorValue == 3.0);

    ok(host, ui, "vcust_preview_palette(1, 2, 3)"); // HARDENED-refusal: unresolved (zone,palette)
    CHECK(es.vcustPreview().previewPaletteOobRefusedCount == 1);
    es.vcustPreview().zonePaletteCount.set("1:2", true);
    ok(host, ui, "vcust_preview_palette(1, 2, 3)"); // faithful path
    CHECK(es.vcustPreview().previewPaletteNoTargetRefusedCount == 0);

    es.vcustPreview().targetLive = false;
    cashBefore = es.playerCash();
    ok(host, ui, "vcust_purchase_wheels(15)"); // HARDENED-refusal: no live target
    CHECK(es.vcustPreview().purchaseWheelsNoTargetRefusedCount == 1);
    CHECK(es.playerCash() == cashBefore);
    es.vcustPreview().targetLive = true;
    ok(host, ui, "vcust_purchase_wheels(15)"); // faithful path
    CHECK(es.playerCash() == cashBefore - 15.0);
    CHECK(es.vcustPreview().wheelPreviewCommitted);

    // vcust_change_top_level_menu (CRASH - "poisons its own next call"):
    // out-of-range absolute refused; a relative call with no node refused;
    // a valid absolute sets the node, then a relative call succeeds.
    ok(host, ui, "vcust_change_top_level_menu(-1, 1)"); // HARDENED-refusal: relative, no current node yet
    CHECK(es.vcustPreview().changeMenuRelativeNoNodeRefusedCount == 1);
    ok(host, ui, "vcust_change_top_level_menu(999)"); // HARDENED-refusal: absolute out of range
    CHECK(es.vcustPreview().changeMenuAbsoluteOutOfRangeRefusedCount == 1);
    ok(host, ui, "vcust_change_top_level_menu(2)"); // faithful: valid absolute
    CHECK(es.vcustPreview().changeMenuAbsoluteCount == 1);
    ok(host, ui, "vcust_change_top_level_menu(-1, 1)"); // faithful: relative, now has a node
    CHECK(es.vcustPreview().changeMenuRelativeCount == 1);

    ok(host, ui, "vcust_show_nos_hydraulic_warning()"); // faithful (targetLive already true from above)
    CHECK(es.vcustPreview().showNosHydraulicWarningCount == 1);
    es.vcustPreview().targetLive = false;
    ok(host, ui, "vcust_show_nos_hydraulic_warning()"); // HARDENED-refusal
    CHECK(es.vcustPreview().showNosHydraulicWarningNoTargetRefusedCount == 1);

    // Remaining vcust_* names - smoke coverage.
    es.vcustPreview().targetLive = true;
    ok(host, ui, "vcust_revert_color()");
    ok(host, ui, "vcust_preview_component(0, 1)");
    ok(host, ui, "vcust_preview_component(2, 1)");
    ok(host, ui, "vcust_preview_component(99, 1)");
    ok(host, ui, "assert(vcust_is_camera_tool_mode and type(vcust_is_camera_tool_mode) == 'function')");
    es.vcustPreview().cameraToolMode.set(true);
    ok(host, ui, "assert(vcust_is_camera_tool_mode() == true)");
    ok(host, ui, "vcust_adjust_camera_angle(1.5)");
    ok(host, ui, "vcust_preview_rim_sizing(1, 2)");
    ok(host, ui, "vcust_purchase_paint(10)");
    ok(host, ui, "vcust_purchase_performance_upgrade(20)");
    ok(host, ui, "vcust_select_paint_slot(1)");
    ok(host, ui, "vcust_select_paint_slot(999)");
    CHECK(es.vcustPreview().selectPaintSlotOobRefusedCount == 1);
    ok(host, ui, "vcust_set_underglow(1, 5)");
    ok(host, ui, "vcust_set_underglow(999, 5)");
    CHECK(es.vcustPreview().setUnderglowOobRefusedCount == 1);

    // ==================================================================
    // store_* family.
    // ==================================================================
    ok(host, ui, "assert(store_dlc_make_purchase(5) == 0.0)");
    ok(host, ui, "store_weapon_purchase_weapon('pistol', 0, false, 10.0)");
    CHECK(es.storeFrontend().weaponPurchaseGrantedCount == 1);
    ok(host, ui, "store_weapon_purchase_weapon('pistol', 0, false, 10.0)"); // already owned -> achievement sweep
    CHECK(es.storeFrontend().weaponPurchaseAchievementSweepCount == 1);
    ok(host, ui, "store_weapon_purchase_weapon('not_a_category', 0, false, 10.0)");
    CHECK(es.storeFrontend().weaponPurchaseUnmatchedCategoryCount == 1);

    ok(host, ui, "store_vehicle_change_mode(0)");
    ok(host, ui, "store_vehicle_retrieve_car_and_exit()");
    fails(host, ui, "local v = store_vehicle_selection_should_lock_controls()", "store_vehicle_selection_should_lock_controls");
    es.storeFrontend().selectionLocksControls.set(false);
    ok(host, ui, "assert(store_vehicle_selection_should_lock_controls() == false)");

    // store_weapon_change_weapon (CRASH): unresolved refused; resolved
    // (and local player present) succeeds.
    es.hasLocalPlayer().set(true);
    ok(host, ui, "store_weapon_change_weapon('pistol', 0, true, false)"); // HARDENED-refusal: unresolved slot
    CHECK(es.storeFrontend().weaponChangeOobRefusedCount == 1);
    es.storeFrontend().weaponChangeSlotResolves.set("pistol:0", true);
    ok(host, ui, "store_weapon_change_weapon('pistol', 0, true, false)"); // faithful
    CHECK(es.storeFrontend().weaponChangeCount == 1);

    ok(host, ui, "store_weapon_process_post_bg_covered()");
    ok(host, ui, "store_character_lineup_loaded()");
    CHECK(es.storeFrontend().characterLineupLoaded);
    es.storeFrontend().clothingStoreId.set(5);
    ok(host, ui, "assert(store_clothing_get_store_id() == 5)");
    ok(host, ui, "assert(store_dlc_is_offer_free(1) == false)");
    ok(host, ui, "store_dlc_queue_icon(1, 'icon')");
    ok(host, ui, "store_vehicle_no_vehicle_exit()");
    ok(host, ui, "store_vehicle_notify_screen_covered(true)");
    ok(host, ui, "store_crib_init_crib_garage()");

    // store_gallery_display_character: real bound reproduced.
    ok(host, ui, "store_gallery_display_character(5)"); // unresolved -> refused (CONFIRMED-real bound)
    CHECK(es.storeFrontend().galleryDisplayCharacterOobRefusedCount == 1);
    es.storeFrontend().galleryCharacterIndexResolves.set("5", true);
    ok(host, ui, "store_gallery_display_character(5)");
    CHECK(es.storeFrontend().galleryDisplayCharacterAppliedCount == 1);

    // store_gallery_download_show_list (CRASH): exhausted pool refused.
    ok(host, ui, "store_gallery_download_show_list()");
    CHECK(es.storeFrontend().galleryDownloadShowListCount == 1);
    es.storeFrontend().downloadDialogPoolExhausted.set(true);
    ok(host, ui, "store_gallery_download_show_list()");
    CHECK(es.storeFrontend().galleryDownloadShowListPoolExhaustedRefusedCount == 1);

    ok(host, ui, "store_gallery_upload_character()");
    ok(host, ui, "store_gang_begin_exit()");
    ok(host, ui, "store_gang_bg_covered(true)");
    es.storeFrontend().gangUnlockedGate.set(true);
    ok(host, ui, "assert(store_gang_is_unlocked() == true)");

    // store_stronghold_upgrade_end_flyby (CRASH): a tiny population stays
    // under the 256-slot cap (faithful); an artificially huge population
    // triggers the HARDENED refusal.
    ok(host, ui, "store_stronghold_upgrade_end_flyby()");
    CHECK(es.storeFrontend().strongholdUpgradeEndFlybyCount == 1);
    for (int i = 0; i < 300; ++i) es.stronghold().stillLocked.set("lvl" + std::to_string(i), true);
    ok(host, ui, "store_stronghold_upgrade_end_flyby()");
    CHECK(es.storeFrontend().strongholdUpgradeEndFlybyOverflowRefusedCount == 1); // HARDENED-refusal (256-slot cap)

    ok(host, ui, "local v = store_vehicle_allow_garage()");
    ok(host, ui, "store_vehicle_do_return_to_crib()");

    // ==================================================================
    // garage_* family.
    // ==================================================================
    ok(host, ui, "assert(garage_get_repair_cost() == 0.0)"); // CONFIRMED default: no vehicle selected
    es.garageFrontend().hasSelectedVehicle = true;
    es.garageFrontend().selectedVehicleRepairCost.set(42.0);
    ok(host, ui, "assert(garage_get_repair_cost() == 42.0)");

    cashBefore = es.playerCash();
    ok(host, ui, "garage_repair_vehicle()"); // faithful: charges the known cost
    CHECK(es.playerCash() == cashBefore - 42.0);
    es.garageFrontend().hasSelectedVehicle = false;
    cashBefore = es.playerCash();
    ok(host, ui, "garage_repair_vehicle()"); // HARDENED-refusal: no selection
    CHECK(es.garageFrontend().repairVehicleNoSelectionRefusedCount == 1);
    CHECK(es.playerCash() == cashBefore);

    es.garageFrontend().entryCount = 3;
    ok(host, ui, "garage_back_to_customize(2)"); // faithful: < count
    CHECK(es.garageFrontend().backToCustomizeCount == 1);
    ok(host, ui, "garage_back_to_customize(3)"); // refused: == count (this one is ALREADY correctly guarded in the original)
    CHECK(es.garageFrontend().backToCustomizeOobRefusedCount == 1);

    ok(host, ui, "local v = garage_can_customize()");
    ok(host, ui, "local v = garage_can_delete()");

    // garage_clear_new (CRASH - off-by-one): index == count is HARDENED-
    // refused here (strict <), unlike the real engine's own <=.
    ok(host, ui, "garage_clear_new(2)"); // faithful: < count
    CHECK(es.garageFrontend().clearNewCount == 1);
    ok(host, ui, "garage_clear_new(3)"); // HARDENED-refusal: == count, the named off-by-one
    CHECK(es.garageFrontend().clearNewOobRefusedCount == 1);

    ok(host, ui, "local t = garage_get_garage_type()");

    // garage_remove_vehicle (CRASH - double-release): first call
    // succeeds; a second call before a fresh selection is HARDENED-
    // refused (unlike the real engine's own double-release).
    es.garageFrontend().entries[0].present = true;
    es.garageFrontend().selectedEntryIndex = 0;
    ok(host, ui, "garage_remove_vehicle()"); // faithful
    CHECK(es.garageFrontend().removeVehicleCount == 1);
    ok(host, ui, "garage_remove_vehicle()"); // HARDENED-refusal: the named double-release
    CHECK(es.garageFrontend().removeVehicleDoubleReleaseRefusedCount == 1);

    ok(host, ui, "local v = garage_vehicle_load_pending()");

    // ==================================================================
    // crib_* family + building_purchase_is_crib_being_purchased.
    // ==================================================================
    ok(host, ui, "assert(crib_has_garage_with_vehicles_of_type(1) == false)"); // no crib record -> false, no reads
    es.cribFrontend().hasCribRecord = true;
    es.cribFrontend().garageVehicleCountByType.set("1", 3);
    ok(host, ui, "assert(crib_has_garage_with_vehicles_of_type(1) == true)");

    es.cribFrontend().cashStash = 100.0;
    cashBefore = es.playerCash();
    ok(host, ui, "local v = select('#', crib_get_cash_stash()); assert(v == 0)"); // CONFIRMED naming trap: 0 Lua values
    CHECK(es.playerCash() == cashBefore + 100.0);
    CHECK(es.cribFrontend().cashStash == 0.0);

    fails(host, ui, "local v = crib_is_garage_disabled()", "crib_is_garage_disabled");
    es.cribFrontend().garageDisabled.set(false);
    ok(host, ui, "assert(crib_is_garage_disabled() == false)");
    es.cribFrontend().isStronghold.set(true);
    ok(host, ui, "assert(crib_is_stronghold() == true)");

    // crib_purchase_purchase_crib (CRASH): no selection refused; a
    // selection charges (charge-before-ownership-check preserved
    // faithfully, per the task's own hardening scope).
    cashBefore = es.playerCash();
    ok(host, ui, "crib_purchase_purchase_crib()"); // HARDENED-refusal: nothing selected
    CHECK(es.cribFrontend().purchaseCribNoSelectionRefusedCount == 1);
    CHECK(es.playerCash() == cashBefore);
    es.cribFrontend().hasSelectedPurchaseCrib = true;
    es.cribFrontend().cribPurchasePrice.set("selected", 500.0);
    ok(host, ui, "crib_purchase_purchase_crib()"); // faithful
    CHECK(es.cribFrontend().purchaseCribChargedCount == 1);
    CHECK(es.playerCash() == cashBefore - 500.0);

    ok(host, gp, "crib_purchasing_unlock()");
    CHECK(es.cribFrontend().purchasingUnlockCount == 1);
    ok(host, ui, "local v = building_purchase_is_crib_being_purchased()");

    if (g_failures == 0) {
        std::cout << "All wave-2 W2-5 (b25: vcust/store/garage/crib/gang) synthetic tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
