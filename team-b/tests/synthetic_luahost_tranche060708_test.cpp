// Synthetic tests for spec-lua-api-behaviour.md Sec38 ("ranking tranche
// 06"), Sec39 ("ranking tranche 07") and Sec40 ("ranking tranche 08") -
// 2026-10-02 - the 16 of their combined 75 CONFIRMED names this pass
// implements. See include/sr3luahost/engine_state.h and
// src/lua_spec_confirmed_stubs.cpp (that file's own Sec38/Sec39/Sec40
// batch header comments) for the full per-function reasoning and for why
// the other 59 names stay generic logging stubs this pass. Same
// convention as tests/synthetic_luahost_tranche03_test.cpp: a small,
// hand-written fixture (no real tagged-registration-list file or real
// archive read here). Every name below is registered with its own real
// cluster tag, grepped directly from tools/lua_all_registered_1490_
// tagged.txt.
//
// Specific focus (per this task's own explicit instruction): every
// MANDATED crash guard (vcust_preview_wheel_sizing, team_make_hostile,
// skydive_move_to_do, store_gang_show_question_marks, store_gallery_
// download_hide_list) is exercised to confirm this host guards it
// gracefully (test-observable counters, NOT real engine fields) rather
// than crashing or throwing uncaught; every MANDATED faithful quirk
// (vehicle_engine_check_running's zero-return-on-success, store_weapon_
// purchase_ammo's charge-on-failed-lookup, store_common_rotate_mouse_
// drag's CHOSEN-zero stand-in, save_system_save_game/_load_game's
// off-by-one, pcu_purchase_slot/_outfit's charge-while-full, and pcu_
// wear_store_outfit vs. pcu_purchase_outfit's DIVERGENT catalog-counting
// rules) is exercised to confirm it reproduces EXACTLY, not "fixed".
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
        }                                                                                     \
    } while (0)

// Runs `code` and expects it to succeed (asserts inside the chunk do the work).
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "tranche060708_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code` and expects a Lua error whose message contains `needle`.
void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "tranche060708_test.lua");
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

// The 16 (name, cluster) rows this batch registers - real tags grepped
// directly from tools/lua_all_registered_1490_tagged.txt.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"vcust_preview_wheel_sizing", "ui"},
        {"team_make_hostile", "gameplay"},
        {"vehicle_engine_check_running", "gameplay"},
        {"store_weapon_purchase_ammo", "ui"},
        {"skydive_move_to_do", "gameplay"},
        {"store_gang_show_question_marks", "ui"},
        {"store_gallery_download_hide_list", "ui"},
        {"store_common_rotate_mouse_drag", "ui"},
        {"save_system_save_game", "ui"},
        {"save_system_load_game", "ui"},
        {"save_system_cancel_coop_load", "ui"},
        {"pcu_is_bra_category", "ui"},
        {"pcu_is_underwear_category", "ui"},
        {"pcu_purchase_slot", "ui"},
        {"pcu_purchase_outfit", "ui"},
        {"pcu_wear_store_outfit", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: each of the 16 names lands in its own real cluster
    // tag's state, and nowhere else.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            lua_State* home = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            lua_State* away = rn.cluster == "ui" ? host.gameplayState() : host.uiState();
            CHECK(hasGlobal(home, rn.name.c_str()));
            CHECK(!hasGlobal(away, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec38.5 vcust_preview_wheel_sizing - MANDATED crash guard #1. CHOSEN
    // default (no live target) -> guarded, no crash; a test-set live
    // target -> not guarded.
    CHECK(es.vcustPreview().wheelSizingNullTargetGuardCount == 0);
    ok(host, ui, "vcust_preview_wheel_sizing(3, 7)");
    CHECK(es.vcustPreview().wheelSizingNullTargetGuardCount == 1);
    ok(host, ui, "vcust_preview_wheel_sizing(3, 7)"); // every call guarded by this host's own CHOSEN default
    CHECK(es.vcustPreview().wheelSizingNullTargetGuardCount == 2);
    es.vcustPreview().targetLive = true;
    ok(host, ui, "vcust_preview_wheel_sizing(3, 7)");
    CHECK(es.vcustPreview().wheelSizingNullTargetGuardCount == 2); // unchanged - not guarded

    // ------------------------------------------------------------------
    // Sec38.2 team_make_hostile - MANDATED crash guard #2, reusing the
    // SAME Sec30.6 team-relation infrastructure team_make_unfriendly
    // already established (per this task's own explicit instruction not
    // to duplicate it). Writes relation value 0 (hostile), symmetric,
    // with the team-5-mirrors-team-6 refinement; an unrecognized name's
    // sentinel id 9 is guarded (both the write AND the replication).
    CHECK(es.teamRelationOobGuardCount() == 0);
    ok(host, gp, "team_make_hostile('UnknownTeamX', 'UnknownTeamY')"); // both sentinel 9: guarded
    CHECK(es.teamRelationOobGuardCount() == 1);
    CHECK(es.teamRelationForTesting(9, 9) == -2); // out of bounds: never written

    es.registerTeamIdForTesting("Saints", 0);
    es.registerTeamIdForTesting("Police", 1);
    ok(host, gp, "team_make_hostile('Saints', 'Police')"); // both in-bounds: real write
    CHECK(es.teamRelationOobGuardCount() == 1); // unchanged
    CHECK(es.teamRelationForTesting(0, 1) == 0); // value 0 = hostile (not 1, team_make_unfriendly's own value)
    CHECK(es.teamRelationForTesting(1, 0) == 0); // symmetric write

    es.registerTeamIdForTesting("Neutral5b", 5);
    es.registerTeamIdForTesting("OtherB", 2);
    ok(host, gp, "team_make_hostile('Neutral5b', 'OtherB')");
    CHECK(es.teamRelationForTesting(5, 2) == 0);
    CHECK(es.teamRelationForTesting(6, 2) == 0); // mirrored onto team 6 (Sec30.6's own refinement, reused)

    // Defect 1 (non-string name -> would-be null-pointer deref in the real
    // engine's own name-hash helper): already avoided by construction via
    // argString()'s own NULL-safe lua_tolstring wrapper - a nil argument
    // must not crash this host either.
    ok(host, gp, "team_make_hostile(nil, 'Police')");

    // ------------------------------------------------------------------
    // Sec38.1 vehicle_engine_check_running - MANDATED faithful quirk:
    // zero Lua values on success (never "fixed" into a boolean). Failure
    // (unresolved, or gate clear) -> exactly 1 boolean, false.
    es.objectResolves().set("veh10", false);
    ok(host, gp, "assert(vehicle_engine_check_running('veh10') == false)");
    es.objectResolves().set("veh11", true);
    es.getOrCreateVehicle("veh11").engineCheckGatePasses.set(false);
    ok(host, gp, "assert(vehicle_engine_check_running('veh11') == false)");
    es.getOrCreateVehicle("veh11").engineCheckGatePasses.set(true);
    ok(host, gp, "assert(select('#', vehicle_engine_check_running('veh11')) == 0)"); // CONFIRMED: zero values, Sec38.1

    // ------------------------------------------------------------------
    // Sec38.3 store_weapon_purchase_ammo - MANDATED faithful quirk: the
    // charge/purchase-record side effect fires even though this host's
    // own never-populated store-list table means the item lookup always
    // takes the real, CONFIRMED failure branch.
    es.playerCash() = 100.0;
    CHECK(es.purchaseLedger().empty());
    ok(host, ui, "store_weapon_purchase_ammo(1, 2, 3, 25)");
    CHECK(es.playerCash() == 75.0); // charged regardless of the (always-failing) item lookup
    CHECK(es.purchaseLedger().size() == 1);
    CHECK(es.purchaseLedger()[0].tag == "weapon-ammo");
    CHECK(es.purchaseLedger()[0].amountCharged == 25.0);
    CHECK(es.purchaseLedger()[0].itemResolved == false); // CONFIRMED-shaped quirk: never resolved in this host

    // ------------------------------------------------------------------
    // Sec39.1 skydive_move_to_do - MANDATED guard: the use-path-false,
    // unresolved-target null-shaped read at 0x40. use-path true never
    // takes the guarded branch regardless of the target.
    CHECK(es.skydiveMoveToNullTargetGuardCount() == 0);
    ok(host, gp, "skydive_move_to_do('charA', 'never_registered_target', false)"); // never known: guarded
    CHECK(es.skydiveMoveToNullTargetGuardCount() == 1);
    es.objectResolves().set("skyTargetBad", false); // explicitly known NOT to resolve - the real defect's own shape
    ok(host, gp, "skydive_move_to_do('charA', 'skyTargetBad', false)");
    CHECK(es.skydiveMoveToNullTargetGuardCount() == 2);
    es.objectResolves().set("skyTargetGood", true);
    ok(host, gp, "skydive_move_to_do('charA', 'skyTargetGood', false)"); // resolved: not guarded
    CHECK(es.skydiveMoveToNullTargetGuardCount() == 2);
    ok(host, gp, "skydive_move_to_do('charA', 'never_registered_target', true)"); // use-path true: never guarded
    CHECK(es.skydiveMoveToNullTargetGuardCount() == 2);

    // ------------------------------------------------------------------
    // Sec39.5 store_gang_show_question_marks - MANDATED guard: CHOSEN
    // default (neither asset resolved) -> guarded; test-set resolved ->
    // not guarded, and the toggle is actually applied.
    CHECK(es.storePreviewGuards().gangQuestionMarksNullGuardCount == 0);
    ok(host, ui, "store_gang_show_question_marks(true)");
    CHECK(es.storePreviewGuards().gangQuestionMarksNullGuardCount == 1);
    CHECK(es.storePreviewGuards().gangPreviewShowingQuestionMarks == false); // guarded: never applied
    es.storePreviewGuards().gangPreviewAssetOrFallbackResolved = true;
    ok(host, ui, "store_gang_show_question_marks(true)");
    CHECK(es.storePreviewGuards().gangQuestionMarksNullGuardCount == 1); // unchanged
    CHECK(es.storePreviewGuards().gangPreviewShowingQuestionMarks == true); // applied this time

    // ------------------------------------------------------------------
    // Sec39.5 store_gallery_download_hide_list - MANDATED guard: the
    // hazard (and all modeled effect) is specific to the "true" (hide)
    // path; CHOSEN default (gallery object unresolved) -> guarded without
    // ever touching hasLocalPlayer() (OPEN); both resolved -> succeeds.
    ok(host, ui, "store_gallery_download_hide_list(false)"); // "false": no hazard, no guard either way
    CHECK(es.storePreviewGuards().galleryHideListNullGuardCount == 0);
    ok(host, ui, "store_gallery_download_hide_list(true)"); // CHOSEN default: gallery object unresolved -> guarded
    CHECK(es.storePreviewGuards().galleryHideListNullGuardCount == 1);
    es.storePreviewGuards().galleryListObjectResolved = true;
    fails(host, ui, "store_gallery_download_hide_list(true)", "0x009da4e0"); // gallery OK, local player still OPEN
    es.hasLocalPlayer().set(false);
    ok(host, ui, "store_gallery_download_hide_list(true)"); // gallery OK, local player explicitly absent -> guarded
    CHECK(es.storePreviewGuards().galleryHideListNullGuardCount == 2);
    es.hasLocalPlayer().set(true);
    ok(host, ui, "store_gallery_download_hide_list(true)"); // both resolved: succeeds, not guarded
    CHECK(es.storePreviewGuards().galleryHideListNullGuardCount == 2);

    // ------------------------------------------------------------------
    // Sec39.5 store_common_rotate_mouse_drag - MANDATED CHOSEN stand-in:
    // always exactly 0, never raw/undefined memory.
    CHECK(es.storeCommonRotateMouseDrag().callCount == 0);
    ok(host, ui, "store_common_rotate_mouse_drag()");
    CHECK(es.storeCommonRotateMouseDrag().callCount == 1);
    CHECK(es.storeCommonRotateMouseDrag().lastDeltaX == 0.0);
    CHECK(es.storeCommonRotateMouseDrag().lastDeltaY == 0.0);
    CHECK(es.storeCommonRotateMouseDrag().lastDeltaZ == 0.0);

    // ------------------------------------------------------------------
    // Sec39.4 save_system_save_game/_load_game - MANDATED faithful quirk:
    // slot == count is accepted (the real off-by-one), not rejected.
    es.saveSystemUi().slotCount.set(10);
    ok(host, ui, "save_system_save_game(5)"); // slot < count: ordinary accept
    CHECK(es.saveSystemUi().saveOffByOneCount == 0);
    ok(host, ui, "save_system_save_game(10)"); // slot == count: the off-by-one, ALLOWED not rejected
    CHECK(es.saveSystemUi().saveOffByOneCount == 1);
    ok(host, ui, "save_system_save_game(11)"); // slot > count: ordinary reject (no further effect either way)
    CHECK(es.saveSystemUi().saveOffByOneCount == 1); // unchanged
    ok(host, ui, "save_system_save_game(99)"); // the "new save" sentinel - never off-by-one
    CHECK(es.saveSystemUi().saveOffByOneCount == 1);

    ok(host, ui, "save_system_load_game(10)"); // slot == count: the off-by-one, ALLOWED not rejected
    CHECK(es.saveSystemUi().loadOffByOneCount == 1);
    ok(host, ui, "save_system_load_game(5)");
    CHECK(es.saveSystemUi().loadOffByOneCount == 1); // unchanged

    CHECK(es.saveSystemUi().coopLoadState == 0);
    ok(host, ui, "save_system_cancel_coop_load()");
    CHECK(es.saveSystemUi().coopLoadState == 5); // CONFIRMED value, Sec39.4

    // ------------------------------------------------------------------
    // Sec40.5 pcu_is_bra_category/pcu_is_underwear_category - MANDATED
    // guard: an index a test never populated takes the same guarded path
    // a real out-of-range index would (no crash).
    CHECK(es.pcuCategoryTable().categoryIndexOobGuardCount == 0);
    ok(host, ui, "assert(pcu_is_bra_category(0) == false)"); // never populated: guarded, false
    CHECK(es.pcuCategoryTable().categoryIndexOobGuardCount == 1);
    es.pcuCategoryTable().kindByIndex.set("0", EngineState::PcuCategoryTable::kBra);
    es.pcuCategoryTable().kindByIndex.set("1", EngineState::PcuCategoryTable::kUnderwear);
    es.pcuCategoryTable().kindByIndex.set("2", EngineState::PcuCategoryTable::kOther);
    ok(host, ui, "assert(pcu_is_bra_category(0) == true)");
    CHECK(es.pcuCategoryTable().categoryIndexOobGuardCount == 1); // unchanged - populated now
    ok(host, ui, "assert(pcu_is_bra_category(1) == false)");
    ok(host, ui, "assert(pcu_is_underwear_category(1) == true)");
    ok(host, ui, "assert(pcu_is_underwear_category(0) == false)");
    ok(host, ui, "assert(pcu_is_bra_category(2) == false)");
    ok(host, ui, "assert(pcu_is_underwear_category(2) == false)");
    ok(host, ui, "assert(pcu_is_bra_category(-1) == false)"); // negative: guarded too
    CHECK(es.pcuCategoryTable().categoryIndexOobGuardCount == 2);

    // ------------------------------------------------------------------
    // Sec40.5/Sec40.6 pcu_wear_store_outfit vs. pcu_purchase_outfit - the
    // MANDATED divergent-rule distinction: the SAME raw list, numbered by
    // TWO independent filters (wear: "any bit excluding bit 0"; purchase:
    // "bit 0 only"), must resolve the SAME Lua index to DIFFERENT list
    // positions - directly proven, not just asserted, by comparing both
    // functions' own resolution of index 0 against the SAME 3-entry
    // fixture. Catalog list (by position): 0 -> other-bit only (0x2),
    // 1 -> bit0 only (0x1), 2 -> both (0x3).
    es.pcuCatalogOutfits().flagsByIndex.set("0", 0x2u);
    es.pcuCatalogOutfits().flagsByIndex.set("1", 0x1u);
    es.pcuCatalogOutfits().flagsByIndex.set("2", 0x3u);
    // wear's own rule ("any bit excluding bit 0") qualifies positions 0
    // and 2 (0-based: index 0 -> position 0, index 1 -> position 2).
    ok(host, ui, "pcu_wear_store_outfit(0)");
    CHECK(es.pcuInventory().lastWornCatalogPosition == 0);

    // purchase's own rule ("bit 0 only") qualifies positions 1 and 2
    // (0-based: index 0 -> position 1, index 1 -> position 2). Calling it
    // with the SAME Lua index (0) immediately after the wear call above,
    // without touching lastWornCatalogPosition again, proves the
    // confirmed indexing inconsistency directly: one shared raw list,
    // one shared Lua index, two DIFFERENT resolved positions.
    es.pcuInventory().savedOutfitCount = 0;
    es.pcuInventory().cashBalance = 50.0;
    ok(host, ui, "pcu_purchase_outfit(0, 10)"); // purchase's own rule: index 0 -> position 1
    CHECK(es.pcuInventory().lastPurchasedOutfitCatalogPosition == 1);
    CHECK(es.pcuInventory().lastPurchasedOutfitCatalogPosition !=
          es.pcuInventory().lastWornCatalogPosition); // SAME Lua index 0, DIFFERENT resolved position - the mandate itself
    CHECK(es.pcuInventory().cashBalance == 40.0);
    CHECK(es.pcuInventory().savedOutfitCount == 1);
    CHECK(es.pcuInventory().purchaseOutfitChargedWhileFullCount == 0);

    // The remaining wear-rule positions, checked last so they don't
    // disturb the divergence comparison above.
    ok(host, ui, "pcu_wear_store_outfit(1)");
    CHECK(es.pcuInventory().lastWornCatalogPosition == 2);
    ok(host, ui, "pcu_wear_store_outfit(2)"); // only 2 qualifying nodes exist under this rule
    CHECK(es.pcuInventory().lastWornCatalogPosition == -1);

    // ------------------------------------------------------------------
    // Sec40.5 pcu_purchase_outfit/pcu_purchase_slot - MANDATED faithful
    // quirk: charged even when the append is refused (capacity already
    // reached) - nothing is actually added, but the charge still happens.
    es.pcuInventory().savedOutfitCount = EngineState::PcuInventory::kSavedOutfitCapacity; // already full
    double cashBefore = es.pcuInventory().cashBalance;
    ok(host, ui, "pcu_purchase_outfit(0, 15)");
    CHECK(es.pcuInventory().cashBalance == cashBefore - 15.0); // charged regardless
    CHECK(es.pcuInventory().savedOutfitCount == EngineState::PcuInventory::kSavedOutfitCapacity); // nothing added
    CHECK(es.pcuInventory().purchaseOutfitChargedWhileFullCount == 1);

    CHECK(es.pcuInventory().ownedItemCount == 0);
    es.pcuInventory().cashBalance = 100.0;
    ok(host, ui, "pcu_purchase_slot(5, 20)"); // slot 5, price 20: ordinary accept
    CHECK(es.pcuInventory().cashBalance == 80.0);
    CHECK(es.pcuInventory().ownedItemCount == 1);
    CHECK(es.pcuInventory().purchaseSlotChargedWhileFullCount == 0);
    es.pcuInventory().ownedItemCount = EngineState::PcuInventory::kOwnedItemCapacity; // already full
    ok(host, ui, "pcu_purchase_slot(6, 20)");
    CHECK(es.pcuInventory().cashBalance == 60.0); // charged regardless
    CHECK(es.pcuInventory().ownedItemCount == EngineState::PcuInventory::kOwnedItemCapacity); // nothing added
    CHECK(es.pcuInventory().purchaseSlotChargedWhileFullCount == 1);

    if (g_failures == 0)
        std::cout << "batch 2026-10-02 (Sec38/Sec39/Sec40 ranking tranches 06/07/08, 16 functions): all checks "
                     "passed\n";
    return g_failures == 0 ? 0 : 1;
}
