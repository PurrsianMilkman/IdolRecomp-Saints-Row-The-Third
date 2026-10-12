// Synthetic coverage for the shop/crib walk-in "press E" entry (feature/
// shop-crib-press-e-entry, 2026-10-11): EngineState::
// registerShopCribTrigger()/updateShopCribTriggerPrompt()/
// pressShopCribInteract(), added alongside the pre-existing trigger-volume
// overlap machinery (tests/synthetic_trigger_volume_overlap_test.cpp) and
// the hud_prompt native's own recordHudPrompt() request-recording shape
// (src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp's
// stub_hud_prompt).
//
// Per spec-store-runtime.md Sec2: SHOP entry is still a CHOSEN labeled
// stand-in (the real native trigger condition/array-index for shop was
// never resolved, including a same-day spec-sync pass that closed crib's
// own chain but explicitly did not find shop's). CRIB entry is NOT a
// stand-in as of commit b8c17ec: the full chain (press 0x52 -> 0x005b6870
// bit-walk -> 0x0111f2d0[8] -> 0x005eee70 -> 0x0080cc90(this=0x022ccef8
// "store_crib") -> 0x007b5fe0(this)) is confirmed by disassembly, every
// hop. This project still does not implement the interaction-prompt
// bit-walk table itself (not Lua-bound, no C++ model of it exists) - the
// trigger-volume-overlap-based "is the player inside" check below stands in
// for that specific piece EQUALLY for shop and crib (unchanged by b8c17ec);
// what b8c17ec changed is which document name the press opens and whether
// that attribution is REAL (crib -> "store_crib") or CHOSEN (shop ->
// "store_common", itself a real confirmed target per 0x0083ECC0, but reached
// by an unidentified trigger index for the shop case specifically).
#include <iostream>
#include <string>

#include "sr3luahost/engine_state.h"

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

} // namespace

int main() {
    // --- A generic walk-in SHOP trigger (kShop, CHOSEN stand-in end to
    // end) -----------------------------------------------------------------
    {
        EngineState es;
        EngineState::TriggerVolumeGeometry g;
        g.px = 100; g.py = 0; g.pz = 0;
        g.shapeCode = 1; // sphere
        g.radius = 3.0;
        es.registerTriggerGeometry("store_weapon_entry_trigger", g);
        es.registerShopCribTrigger("store_weapon_entry_trigger", EngineState::ShopCribTriggerKind::kShop);

        CHECK(es.isShopCribTrigger("store_weapon_entry_trigger"));
        CHECK(!es.isShopCribTrigger("some_unrelated_trigger"));
        CHECK(es.shopCribTriggerKind("store_weapon_entry_trigger") == EngineState::ShopCribTriggerKind::kShop);

        // Player far away: no prompt, press does nothing.
        CHECK(es.updateShopCribTriggerPrompt(0, 0, 0).empty());
        CHECK(!es.shopCribPromptShowing());
        CHECK(es.hudPromptRequests().empty());
        CHECK(!es.pressShopCribInteract());
        CHECK(es.documentOpenCount() == 0);

        // Player walks into the trigger volume -> prompt appears, reusing
        // the existing hud_prompt request-recording shape, targeting
        // "store_common" (shop's CHOSEN open target).
        const std::string& active = es.updateShopCribTriggerPrompt(100, 0, 0);
        CHECK(active == "store_weapon_entry_trigger");
        CHECK(es.shopCribPromptShowing());
        CHECK(es.activeShopCribTrigger() == "store_weapon_entry_trigger");
        CHECK(es.hudPromptRequests().size() == 1);
        CHECK(es.hudPromptRequests().back().target == "store_weapon_entry_trigger");
        CHECK(es.hudPromptRequests().back().arg2 == "store_common");

        // Re-polling the SAME position (still inside) must NOT re-fire the
        // prompt - matches this project's own pre-existing "only on a
        // transition" convention for trigger enter/exit (Sec1.14).
        es.updateShopCribTriggerPrompt(100, 0, 0);
        CHECK(es.hudPromptRequests().size() == 1);

        // Pressing interact while the prompt is showing opens store_common.
        int32_t depthBefore = es.uiScreenStackDepth();
        CHECK(es.pressShopCribInteract());
        CHECK(es.documentOpenCount() == 1);
        CHECK(es.lastDocumentOpened() == "store_common");
        CHECK(es.lastDocumentOpenTrigger() == "store_weapon_entry_trigger");
        CHECK(es.uiScreenStackDepth() == depthBefore + 1);

        // Walking back out clears the prompt; pressing interact again is a
        // no-op (no live prompt to act on).
        CHECK(es.updateShopCribTriggerPrompt(0, 0, 0).empty());
        CHECK(!es.shopCribPromptShowing());
        CHECK(!es.pressShopCribInteract());
        CHECK(es.documentOpenCount() == 1); // unchanged
    }

    // --- A CRIB trigger (kCrib, REAL confirmed chain per b8c17ec): same
    // prompt/press mechanism, but opens "store_crib" instead -------------
    {
        EngineState es;
        EngineState::TriggerVolumeGeometry g;
        g.px = 0; g.py = 0; g.pz = 0;
        g.shapeCode = 0; // box
        g.boxMinX = -2; g.boxMinY = -2; g.boxMinZ = -2;
        g.boxMaxX = 2; g.boxMaxY = 2; g.boxMaxZ = 2;
        es.registerTriggerGeometry("player_crib_door_trigger", g);
        es.registerShopCribTrigger("player_crib_door_trigger", EngineState::ShopCribTriggerKind::kCrib);

        CHECK(es.shopCribTriggerKind("player_crib_door_trigger") == EngineState::ShopCribTriggerKind::kCrib);
        CHECK(es.updateShopCribTriggerPrompt(0, 0, 0) == "player_crib_door_trigger");
        CHECK(es.shopCribPromptShowing());
        CHECK(es.hudPromptRequests().back().arg2 == "store_crib");
        CHECK(es.pressShopCribInteract());
        CHECK(es.documentOpenCount() == 1);
        CHECK(es.lastDocumentOpened() == "store_crib");
        CHECK(es.lastDocumentOpenTrigger() == "player_crib_door_trigger");
    }

    // --- Shop and crib triggers coexisting: each opens its OWN target,
    // never the other's -----------------------------------------------
    {
        EngineState es;
        EngineState::TriggerVolumeGeometry shopG;
        shopG.px = 10; shopG.py = 0; shopG.pz = 0; shopG.shapeCode = 1; shopG.radius = 1.0;
        es.registerTriggerGeometry("shop_a", shopG);
        es.registerShopCribTrigger("shop_a", EngineState::ShopCribTriggerKind::kShop);

        EngineState::TriggerVolumeGeometry cribG;
        cribG.px = 500; cribG.py = 0; cribG.pz = 0; cribG.shapeCode = 1; cribG.radius = 1.0;
        es.registerTriggerGeometry("crib_a", cribG);
        es.registerShopCribTrigger("crib_a", EngineState::ShopCribTriggerKind::kCrib);

        es.updateShopCribTriggerPrompt(10, 0, 0);
        es.pressShopCribInteract();
        CHECK(es.lastDocumentOpened() == "store_common");

        es.updateShopCribTriggerPrompt(500, 0, 0);
        es.pressShopCribInteract();
        CHECK(es.lastDocumentOpened() == "store_crib");
        CHECK(es.documentOpenCount() == 2);
    }

    // --- Two registered triggers, player never near either -> genuinely
    // nothing fires (no prompt leaks from an unrelated registered trigger)
    {
        EngineState es;
        EngineState::TriggerVolumeGeometry g1;
        g1.px = 5000; g1.py = 0; g1.pz = 0; g1.shapeCode = 1; g1.radius = 1.0;
        es.registerTriggerGeometry("far_away_shop", g1);
        es.registerShopCribTrigger("far_away_shop", EngineState::ShopCribTriggerKind::kShop);
        CHECK(es.updateShopCribTriggerPrompt(0, 0, 0).empty());
        CHECK(!es.shopCribPromptShowing());
        CHECK(es.hudPromptRequests().empty());
    }

    if (g_failures == 0) {
        std::cout << "All shop/crib press-E entry checks passed.\n";
    } else {
        std::cerr << g_failures << " check(s) failed.\n";
    }
    return g_failures == 0 ? 0 : 1;
}
