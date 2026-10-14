// Synthetic tests for wave 2, Batch W2-5 (2026-10-08), half 1 of 2 -
// "Customization frontends: PCU/PCR/customization_*"
// (src/lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp). 63 names. Same
// hand-written-fixture convention as every other synthetic_luahost_*_test.
// Cluster tags grepped directly from tools/lua_all_registered_1490_tagged.
// txt: all 4 customization_* names are `gameplay`; all 59 pcr_*/pcu_* names
// are `ui`.
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
    auto r = host.runChunk(L, code, "w25a_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "w25a_test.lua");
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
        {"customization_swap_player_rig", "gameplay"},
        {"customization_create_character", "gameplay"},
        {"customization_creation_is_open", "gameplay"},
        {"customization_screen_is_ready", "gameplay"},
        {"pcr_report_composite_items", "ui"},
        {"pcr_set_random_composite", "ui"},
        {"pcr_set_triangle_cursor_pos", "ui"},
        {"pcr_change_skin_color", "ui"},
        {"pcr_set_anim", "ui"},
        {"pcr_change_eye_color", "ui"},
        {"pcr_apply_preset", "ui"},
        {"pcr_get_composite_info", "ui"},
        {"pcr_get_hair_info", "ui"},
        {"pcr_preview_anim", "ui"},
        {"pcr_report_avail_morphs", "ui"},
        {"pcr_report_composite_colors", "ui"},
        {"pcr_restore_clothing", "ui"},
        {"pcr_set_voice", "ui"},
        {"pcr_strip_clothing", "ui"},
        {"pcu_clear_preview_slot", "ui"},
        {"pcu_set_active_category", "ui"},
        {"pcu_get_slot_info", "ui"},
        {"pcu_set_slot_info", "ui"},
        {"pcu_discard_slot", "ui"},
        {"pcu_get_areas", "ui"},
        {"pcu_get_categories_in_area", "ui"},
        {"pcu_report_item_colors", "ui"},
        {"pcu_wear_current_clothing", "ui"},
        {"pcr_add_default_clothing", "ui"},
        {"pcr_apply_regional_preset", "ui"},
        {"pcr_cleanup_player_voice", "ui"},
        {"pcr_creation_complete", "ui"},
        {"pcr_get_triangle_cursor_pos", "ui"},
        {"pcr_get_voice", "ui"},
        {"pcr_hair_randomize_all", "ui"},
        {"pcr_lips_randomize", "ui"},
        {"pcr_purchase_tattoo", "ui"},
        {"pcr_randomize_eye_colors", "ui"},
        {"pcr_report_anims", "ui"},
        {"pcr_report_eye_colors", "ui"},
        {"pcr_report_hair_colors", "ui"},
        {"pcr_report_hair_items", "ui"},
        {"pcr_report_skin_colors", "ui"},
        {"pcu_get_num_items_owned", "ui"},
        {"pcu_get_owned_item_info", "ui"},
        {"pcu_is_current_item_worn", "ui"},
        {"pcu_get_variants", "ui"},
        {"pcu_get_wear_options", "ui"},
        {"pcu_report_outfits", "ui"},
        {"pcu_is_item_needed_for_outfit", "ui"},
        {"pcu_remove_clothing", "ui"},
        {"pcu_set_slots_empty", "ui"},
        {"pcu_wear_outfit", "ui"},
        {"pcu_bra_required", "ui"},
        {"pcu_can_create_outfit", "ui"},
        {"pcu_category_nav_clear_slot", "ui"},
        {"pcu_clear_item_new", "ui"},
        {"pcu_clear_obscured_slots", "ui"},
        {"pcu_create_outfit", "ui"},
        {"pcu_delete_outfit", "ui"},
        {"pcu_get_item_defaults", "ui"},
        {"pcu_get_items_in_category", "ui"},
        {"pcu_get_num_items_category", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration smoke test: all 63 names land as real functions in
    // their own real cluster's state, and nowhere else.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "customization_swap_player_rig"));
        CHECK(!hasGlobal(host.uiState(), "customization_swap_player_rig"));
        CHECK(hasGlobal(host.uiState(), "pcr_set_voice"));
        CHECK(!hasGlobal(host.gameplayState(), "pcr_set_voice"));
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
    // customization_* (Sec60.1).
    // ==================================================================

    // customization_swap_player_rig: always broadcasts regardless of
    // resolution (no CHECK on the replicate side - no Lua-visible effect
    // to assert beyond "did not crash").
    ok(host, gp, "customization_swap_player_rig('rig1', 'rig2')");
    ok(host, gp, "customization_swap_player_rig('rig1', 'rig2', 1)");

    // customization_create_character: HARDENED no-session refusal (default,
    // CONFIRMED false) vs. session-present attempted path.
    CHECK(es.customizationCreation().noSessionRefusedCount == 0);
    ok(host, gp, "customization_create_character()");
    CHECK(es.customizationCreation().noSessionRefusedCount == 1); // HARDENED path: no session -> refused, not crashed
    es.coopSession().present.set(true);
    ok(host, gp, "customization_create_character()");
    CHECK(es.customizationCreation().sessionPresentAttemptedCount == 1);
    es.coopSession().present.set(false); // restore default for later assertions

    // customization_creation_is_open: CONFIRMED-faithful always-true.
    ok(host, gp, "assert(customization_creation_is_open() == true)");
    ok(host, gp, "assert(customization_creation_is_open() == true)"); // a polling loop never sees false - faithful, not a bug

    // customization_screen_is_ready: OPEN until set.
    ok(host, gp, "assert(customization_screen_is_ready() == false)"); // false until store_character sets Store_lineup_is_loaded (Team A sync 9c70c61)
    es.customizationCreation().screenIsReady.set(true);
    ok(host, gp, "assert(customization_screen_is_ready() == true)");

    // ==================================================================
    // pcr_* Sec19/Sec23/Sec26.
    // ==================================================================

    // pcr_set_triangle_cursor_pos/pcr_get_triangle_cursor_pos: truncates to int.
    ok(host, ui, "pcr_set_triangle_cursor_pos(3.9, -2.1)");
    ok(host, ui, "local x, y = pcr_get_triangle_cursor_pos(); assert(x == 3 and y == -2)");

    // pcr_change_skin_color (Sec19.15): CORRECTED test (adversarial
    // review) - the real function "bounds-checks the index against a live
    // count global (0x02300850)" and, on success, writes into its OWN
    // dedicated slot, NOT the shared category-0 ("lips") slot
    // pcr_set_random_composite/pcr_get_composite_info also use. A prior
    // version of this stub silently accepted ANY non-negative index
    // (gated on the WRONG, hair-family field, with no upper-bound compare
    // at all) and aliased its write into items[0] ("lips") - both are
    // fixed; this test proves the refusal, the real bound, and the
    // no-aliasing property.
    ok(host, ui, "pcr_change_skin_color(5)"); // HARDENED-refusal: table size never set
    CHECK(es.pcrSkinColorChange().oobRefusedCount == 1);
    CHECK(!es.pcrCustomization().skinColorSet);
    es.pcrSkinColorChange().tableSize.set(10);
    ok(host, ui, "pcr_change_skin_color(-1)"); // HARDENED-refusal: negative index
    CHECK(es.pcrSkinColorChange().oobRefusedCount == 2);
    ok(host, ui, "pcr_change_skin_color(10)"); // HARDENED-refusal: index == count (out of range)
    CHECK(es.pcrSkinColorChange().oobRefusedCount == 3);
    ok(host, ui, "local a = pcr_get_composite_info('lips'); assert(a == -1)"); // CONFIRMED: 'lips' ("items[0]") untouched by any of the 3 refused calls above
    ok(host, ui, "pcr_change_skin_color(5)"); // faithful path: in-range index
    CHECK(es.pcrSkinColorChange().appliedCount == 1);
    CHECK(es.pcrCustomization().skinColorSet);
    CHECK(es.pcrCustomization().skinColorItem.styleId == 5);
    ok(host, ui, "local a = pcr_get_composite_info('lips'); assert(a == -1)"); // CONFIRMED: still untouched - the real write landed in the dedicated slot, not items[0]

    // pcr_change_eye_color / pcr_set_anim: write into the shared 63-slot
    // PCR list without crashing.
    ok(host, ui, "pcr_change_eye_color(5)");
    CHECK(es.pcrCustomization().items[1].styleId == 5);
    ok(host, ui, "pcr_set_anim('taunt_style', 7)");
    ok(host, ui, "pcr_set_anim('melee_combat_style', 99)"); // CONFIRMED no-op category

    // pcr_set_random_composite: unresolved category -> no-op; resolved
    // category with 0 eligible rows -> no-op; resolved with eligible rows
    // -> applies.
    ok(host, ui, "pcr_set_random_composite('not_a_category')");
    CHECK(es.pcrRandomComposite().categoryUnresolvedCount == 1);
    es.pcrRandomComposite().eligibleRowCount.set("lips", 0);
    ok(host, ui, "pcr_set_random_composite('lips')");
    CHECK(es.pcrRandomComposite().noEligibleRowsCount == 1);
    es.pcrRandomComposite().eligibleRowCount.set("lips", 3);
    ok(host, ui, "pcr_set_random_composite('lips')");
    CHECK(es.pcrRandomComposite().appliedCount == 1);

    // pcr_report_composite_items / pcr_report_composite_colors / pcr_
    // report_avail_morphs / pcr_apply_preset: resolve-or-skip shape.
    ok(host, ui, "pcr_report_composite_items('eyes', 'target1')");
    CHECK(es.pcrReportCounters().compositeItemsReportCount == 1);
    ok(host, ui, "pcr_report_composite_colors('eyes', true, 'target1')");
    CHECK(es.pcrReportCounters().compositeColorsReportCount == 1);
    es.pcrMorph().sessionResolves.set("slot1", true);
    ok(host, ui, "pcr_report_avail_morphs('slot1', 'target1')");
    CHECK(es.pcrReportCounters().availMorphsReportCount == 1);
    ok(host, ui, "pcr_apply_preset('unknown_preset')");
    CHECK(es.pcrPreset().appliedCount == 0);
    es.pcrPreset().presetResolves.set("preset1", true);
    ok(host, ui, "pcr_apply_preset('preset1')");
    CHECK(es.pcrPreset().appliedCount == 1);

    // pcr_get_hair_info / pcr_get_composite_info: resolve-and-read shape.
    ok(host, ui, "local a,b,c,d,e = pcr_get_hair_info(0); assert(a ~= nil)");
    ok(host, ui, "local a = pcr_get_hair_info(5); assert(a == -1)"); // out-of-range category: CONFIRMED real "no match" path, not an error
    ok(host, ui, "local a,b,c = pcr_get_composite_info('eyes'); assert(a ~= nil)");
    ok(host, ui, "local n = pcr_get_composite_info('not_a_category'); assert(n == nil)");

    // pcr_set_voice / pcr_get_voice / pcr_cleanup_player_voice.
    ok(host, ui, "local v = pcr_get_voice(); assert(v == nil)"); // nothing cached yet
    es.pcrVoice().voiceTagResolves.set("player_wm", true);
    ok(host, ui, "pcr_set_voice('player_wm', false)"); // arg2 false -> writes cache
    ok(host, ui, "local v = pcr_get_voice(); assert(v ~= nil)");
    ok(host, ui, "pcr_cleanup_player_voice()");
    ok(host, ui, "local v = pcr_get_voice(); assert(v == nil)"); // cleared

    // pcr_restore_clothing / pcr_strip_clothing: call counters only.
    ok(host, ui, "pcr_restore_clothing()");
    CHECK(es.pcrRestoreClothingCount() == 1);
    ok(host, ui, "pcr_strip_clothing()");
    CHECK(es.pcrStripClothingCount() == 1);

    // ==================================================================
    // pcu_* Sec19/Sec26.
    // ==================================================================
    ok(host, ui, "pcu_set_active_category(0, 1)");
    CHECK(es.pcuClothing().setActiveCategoryCount == 1);
    ok(host, ui, "pcu_clear_preview_slot()");
    CHECK(es.pcuClothing().previewedSlot == -1);
    ok(host, ui, "pcu_set_slot_info(5, 10, 0, 1, true)");
    CHECK(es.pcuClothing().setSlotInfoAppliedCount == 1);
    ok(host, ui, "pcu_set_slot_info(0x18, 10, 0, 1)"); // slotId == 0x18 -> HARDENED refusal (out of range)
    CHECK(es.pcuClothing().setSlotInfoOobRefusedCount == 1);
    ok(host, ui, "local a, b = pcu_get_slot_info(5); assert(a ~= nil)");
    ok(host, ui, "local a = pcu_get_slot_info(999); assert(a ~= nil)"); // not-found -> fixed sentinel, still 1 value
    ok(host, ui, "pcu_discard_slot(5, 0)");
    CHECK(es.pcuClothing().discardSlotCount == 1);
    ok(host, ui, "pcu_get_areas('wardrobe', 'cb')");
    CHECK(es.pcuClothing().wardrobeMode == true);
    ok(host, ui, "pcu_get_categories_in_area(0, 'cb')");
    ok(host, ui, "pcu_report_item_colors('cb', 5)");
    ok(host, ui, "pcu_wear_current_clothing()");
    CHECK(es.pcuClothing().wearCurrentClothingCount == 1);

    // ==================================================================
    // pcr_* Sec43.3 - including CRASH-flagged HARDENING coverage.
    // ==================================================================

    // pcr_report_hair_items (CRASH): out-of-range category refused;
    // in-range category reported.
    ok(host, ui, "pcr_report_hair_items(5, 'cb')");
    CHECK(es.pcrReportCounters().hairItemsOobRefusedCount == 1); // HARDENED path
    ok(host, ui, "pcr_report_hair_items(1, 'cb')");
    CHECK(es.pcrReportCounters().hairItemsReportCount == 1); // faithful path

    ok(host, ui, "pcr_report_hair_colors('cb')");
    CHECK(es.pcrReportCounters().hairColorsReportCount == 1);
    ok(host, ui, "pcr_report_eye_colors('cb')");
    CHECK(es.pcrReportCounters().eyeColorsReportCount == 1);
    ok(host, ui, "pcr_report_skin_colors('cb')");
    CHECK(es.pcrReportCounters().skinColorsReportCount == 1);
    ok(host, ui, "pcr_report_anims('melee_combat_style', 'cb')"); // CONFIRMED no-op category
    ok(host, ui, "pcr_report_anims('taunt_style', 'cb')");
    CHECK(es.pcrReportCounters().animsReportCount >= 1);

    // pcr_randomize_eye_colors: counted only (data-gated, not a divide-by-
    // zero shape per spec).
    ok(host, ui, "pcr_randomize_eye_colors()");
    CHECK(es.pcrRandomizePalettes().eyeColorsAppliedCount == 1);

    // pcr_purchase_tattoo (CRASH - negative price pays the player in the
    // original): HARDENED refusal on negative price; faithful charge on a
    // valid price.
    es.hasLocalPlayer().set(true);
    double cashBefore = es.playerCash();
    ok(host, ui, "pcr_purchase_tattoo(-50)");
    CHECK(es.pcrTattooPurchase().negativePriceRefusedCount == 1); // HARDENED path
    CHECK(es.playerCash() == cashBefore); // not paid - refused, not reproduced
    ok(host, ui, "pcr_purchase_tattoo(50)");
    CHECK(es.pcrTattooPurchase().chargedCount == 1); // faithful path
    CHECK(es.playerCash() == cashBefore - 50.0);

    // pcr_lips_randomize / pcr_hair_randomize_all (CRASH - divide-by-zero
    // on an empty palette): HARDENED refusal when unset/empty; faithful
    // apply once a palette size is registered.
    ok(host, ui, "pcr_lips_randomize()");
    CHECK(es.pcrRandomizePalettes().lipsEmptyPaletteRefusedCount == 1); // HARDENED path
    es.pcrRandomizePalettes().lipsColorPaletteSize.set(12);
    ok(host, ui, "pcr_lips_randomize()");
    CHECK(es.pcrRandomizePalettes().lipsAppliedCount == 1); // faithful path
    ok(host, ui, "pcr_hair_randomize_all()");
    CHECK(es.pcrRandomizePalettes().hairEmptyPaletteRefusedCount == 1); // HARDENED path
    es.pcrRandomizePalettes().hairColorPaletteSize.set(8);
    ok(host, ui, "pcr_hair_randomize_all()");
    CHECK(es.pcrRandomizePalettes().hairAppliedCount == 1); // faithful path

    // pcr_creation_complete (CRASH - null deref on an exhausted dialog
    // pool): single player is always ready; co-op + exhausted pool is
    // HARDENED-refused rather than crashing.
    ok(host, ui, "pcr_creation_complete()");
    CHECK(es.pcrCreationComplete().readyCount == 1); // single player: always ready (Sec43.3)
    es.coopSession().present.set(true);
    es.pcrCreationComplete().dialogPoolExhausted.set(true);
    ok(host, ui, "pcr_creation_complete()");
    CHECK(es.pcrCreationComplete().poolExhaustedRefusedCount == 1); // HARDENED path
    es.coopSession().present.set(false);

    // pcr_apply_regional_preset (CRASH - no local-player null check):
    // unresolved region/preset -> no-op; resolved but no local player ->
    // HARDENED refusal; resolved + local player -> faithful apply.
    ok(host, ui, "pcr_apply_regional_preset('region1', 'preset1')");
    CHECK(es.pcrPreset().regionalAppliedCount == 0);
    es.pcrPreset().regionResolves.set("region1|preset1", true);
    es.hasLocalPlayer().forget(); // reset to OPEN (unknown) so the "no local player" HARDENED path below is reachable
    ok(host, ui, "pcr_apply_regional_preset('region1', 'preset1')");
    CHECK(es.pcrPreset().regionalNoLocalPlayerRefusedCount == 1); // HARDENED path (no local player known)
    es.hasLocalPlayer().set(true);
    ok(host, ui, "pcr_apply_regional_preset('region1', 'preset1')");
    CHECK(es.pcrPreset().regionalAppliedCount == 1); // faithful path

    // pcr_add_default_clothing: CONFIRMED-destructive wipe, preserved
    // faithfully (not hardened away). CORRECTED test (adversarial review):
    // the real wipe target is PcuInventory::ownedItemCount (the owned-
    // clothing inventory count the spec names: "empties the player's
    // entire owned-clothing inventory, sets the owned count to 0"), NOT
    // PcuClothingState::slots (the WORN slots - a DIFFERENT storage,
    // Sec19.16's own "do not conflate the two" caution) - a prior version
    // of this test asserted the wrong object was wiped, matching a prior
    // version of the stub that wiped the wrong object.
    es.pcuInventory().ownedItemCount = 9;
    es.pcuClothing().slots[0].itemId = 7;
    ok(host, ui, "pcr_add_default_clothing()");
    CHECK(es.pcrAddDefaultClothingWipeCount() == 1);
    CHECK(es.pcuClothing().addDefaultClothingSlot10RefreshCount == 1); // CONFIRMED real, separate "refreshes worn slot 10" effect (content OPEN, counted only - not invented)
    CHECK(es.pcuInventory().ownedItemCount == 0); // CONFIRMED: faithfully wiped - the REAL destructive effect, now landing on the correct field
    CHECK(es.pcuClothing().slots[0].itemId == 7); // CONFIRMED: worn slots are a DIFFERENT storage from the owned inventory, untouched by this wipe

    // ==================================================================
    // pcu_* Sec40.5/40.6 (3 spelled out + 7 AGG) and Sec43.2.
    // ==================================================================
    es.pcuCatalogOutfits().flagsByIndex.set("0", 0x1u);
    es.pcuCatalogOutfits().flagsByIndex.set("1", 0x2u);
    ok(host, ui, "assert(pcu_get_num_items_owned() == 1)"); // bit 0 only - 1 of 2 qualifies

    es.pcuOwnedItemQueries().ownedItemResolves.set("0", true);
    ok(host, ui, "local a = pcu_get_owned_item_info(0); assert(a ~= nil)");
    ok(host, ui, "local a = pcu_get_owned_item_info(999); assert(a == nil)");
    CHECK(es.pcuOwnedItemQueries().ownedInfoOobRefusedCount == 1); // HARDENED (named CRASH)

    ok(host, ui, "local v = select('#', pcu_is_current_item_worn(999)); assert(v == 0)"); // CONFIRMED return-arity quirk
    es.pcuClothing().slots[3].itemId = 5;
    ok(host, ui, "local v = pcu_is_current_item_worn(3); assert(v == true)");

    ok(host, ui, "pcu_get_variants(1, 'cb')");
    CHECK(es.pcuAggregate().getVariantsCallbackDispatchCount == 1);
    ok(host, ui, "pcu_get_wear_options(1, 'cb')");
    CHECK(es.pcuAggregate().getWearOptionsCallbackDispatchCount == 1);
    ok(host, ui, "pcu_report_outfits('cb')");
    CHECK(es.pcuAggregate().reportOutfitsCallbackDispatchCount == 1);
    ok(host, ui, "local v = pcu_is_item_needed_for_outfit(1)");
    ok(host, ui, "pcu_remove_clothing(3)");
    CHECK(es.pcuClothing().slots[3].itemId == -1);
    ok(host, ui, "pcu_set_slots_empty()");
    ok(host, ui, "pcu_wear_outfit(0)");
    CHECK(es.pcuAggregate().wearOutfitCount == 1);

    es.hasLocalPlayer().set(true);
    es.pcrCustomization().genderByte = 1;
    ok(host, ui, "assert(pcu_bra_required() == true)");
    es.pcrCustomization().genderByte = 0;
    ok(host, ui, "assert(pcu_bra_required() == false)");

    ok(host, ui, "local ok1 = pcu_can_create_outfit(); assert(ok1 == true)");
    es.pcuCreatorMisc().blockPoolEmpty.set(true);
    ok(host, ui, "local ok1, title = pcu_can_create_outfit(); assert(ok1 == false)");
    es.pcuCreatorMisc().blockPoolEmpty.set(false);

    // pcu_category_nav_clear_slot (CRASH): out-of-range index refused;
    // in-range "Piercings" area stashes+restores slot 5.
    ok(host, ui, "pcu_category_nav_clear_slot(999)");
    CHECK(es.pcuClothing().categoryNavOobRefusedCount == 1); // HARDENED path
    es.pcuClothing().areas[2].name = "Piercings";
    es.pcuClothing().areas[2].known = true;
    es.pcuClothing().slots[5].itemId = 42;
    ok(host, ui, "pcu_category_nav_clear_slot(2)");
    CHECK(es.pcuClothing().slots[5].itemId == -1); // stashed
    CHECK(es.pcuClothing().categoryNavBackupSlot5Item == 42);
    es.pcuClothing().areas[3].name = "Other";
    es.pcuClothing().areas[3].known = true;
    ok(host, ui, "pcu_category_nav_clear_slot(3)");
    CHECK(es.pcuClothing().slots[5].itemId == 42); // restored

    ok(host, ui, "pcu_clear_item_new(0, -1)");
    CHECK(es.pcuCreatorMisc().clearItemNewCount == 1);
    ok(host, ui, "pcu_clear_obscured_slots()");
    CHECK(es.pcuCreatorMisc().clearObscuredSlotsCount == 1);

    // pcu_create_outfit (CRASH): exhausted pool refused; cap refused; else
    // succeeds and increments the saved-outfit count.
    es.pcuCreatorMisc().blockPoolEmpty.set(true);
    ok(host, ui, "local n = pcu_create_outfit('key1'); assert(n == -1)");
    CHECK(es.pcuClothing().createOutfitPoolExhaustedRefusedCount == 1); // HARDENED path
    es.pcuCreatorMisc().blockPoolEmpty.set(false);
    ok(host, ui, "local n = pcu_create_outfit('key1'); assert(n == 0)");
    CHECK(es.pcuInventory().savedOutfitCount == 1); // faithful path

    ok(host, ui, "pcu_delete_outfit(0)");
    CHECK(es.pcuInventory().savedOutfitCount == 0);

    ok(host, ui, "local n = pcu_get_item_defaults(999); assert(n == -1)"); // HARDENED (unresolved)
    es.pcuOwnedItemQueries().ownedItemResolves.set("0", true);
    ok(host, ui, "local n = pcu_get_item_defaults(0); assert(n ~= nil)");

    ok(host, ui, "pcu_get_items_in_category(1, 'cb')");
    CHECK(es.pcuCreatorMisc().itemsInCategoryMode0Count == 1);

    ok(host, ui, "local n = pcu_get_num_items_category(0, -1); assert(n ~= nil)");
    ok(host, ui, "local n = pcu_get_num_items_category(0, 5); assert(n ~= nil)");

    if (g_failures == 0) {
        std::cout << "All wave-2 W2-5 (b24: PCU/PCR/customization_*) synthetic tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
