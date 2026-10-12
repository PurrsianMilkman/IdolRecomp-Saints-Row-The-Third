// Synthetic coverage for weapon pickup (feature/weapon-pickup-dropped-and-
// heavy, 2026-10-12), combining two confirmed spec-weapons-runtime.md Sec5.1
// sources into one shared give/equip writer:
//
//   1. Walk-over auto-pickup for ordinary enemy-dropped weapons -
//      EngineState::registerWeaponDrop()/updateWeaponDropPickup(). The
//      write side (0x00B84330 -> 0x00601380) is REAL confirmed; the exact
//      proximity-trigger condition is CHOSEN, static search exhausted (see
//      kWeaponDropPickupRadius's own doc comment in engine_state.h) - the
//      real per-frame dispatcher that would pin this down was never found
//      by any static pass (Sec5.1's own 2026-10-11 changelog entry).
//
//   2. Press-E pickup for heavy/"pickup-only" weapons (minigun,
//      flamethrower) - EngineState::registerHeavyWeaponPickupTrigger()/
//      updateHeavyWeaponPickupPrompt()/pressHeavyWeaponPickupInteract(),
//      reusing the exact trigger-volume-overlap + hud_prompt machinery the
//      shop/crib unit established (tests/synthetic_shop_crib_press_e_
//      entry_test.cpp), wired to interaction-array entry 0 (Sec5.1's
//      `0x0111f2d0[0]` -> ... -> `0x00972a70` chain) instead of entry 8.
//
// Both paths call EngineState::giveEquipWeapon() - the ONE shared writer -
// never a duplicated/divergent write path per caller.
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

constexpr uint32_t kPistolWeaponId = 0x1001;
constexpr int kPistolWeaponClass = 0; // "pistol" (kWeaponClassNames[0]) - not heavy

constexpr uint32_t kMinigunWeaponId = 0x1002;
constexpr int kMinigunWeaponClass = 17; // "minigun" (kWeaponClassNames[17]) - heavy/pickup-only

} // namespace

int main() {
    // --- (a) Walking within the CHOSEN proximity radius of a non-heavy
    // dropped weapon auto-picks it up -------------------------------------
    {
        EngineState es;
        es.registerWeaponDrop("enemy_drop_1", 50, 0, 0, kPistolWeaponId, kPistolWeaponClass,
                               /*primaryAmmo=*/40, /*roundsInClip=*/12);

        // Far away: not yet consumed, no grant.
        es.updateWeaponDropPickup(0, 0, 0);
        CHECK(!es.isWeaponDropConsumed("enemy_drop_1"));
        CHECK(!es.hasWeapon(kPistolWeaponId));
        CHECK(es.weaponGiveEquipCount() == 0);

        // Within kWeaponDropPickupRadius (1.5m): auto-picked-up.
        es.updateWeaponDropPickup(50.5, 0, 0);
        CHECK(es.isWeaponDropConsumed("enemy_drop_1"));
        CHECK(es.hasWeapon(kPistolWeaponId));
        CHECK(es.weaponGiveEquipCount() == 1);
        CHECK(es.lastWeaponGiveEquipId() == kPistolWeaponId);
        const auto* slot = es.weaponSlot(kPistolWeaponId);
        CHECK(slot != nullptr);
        CHECK(slot->ammo[0] == 40);
        CHECK(slot->ammo[1] == 12);

        // One-shot: re-polling the same position does not grant again.
        es.updateWeaponDropPickup(50.5, 0, 0);
        CHECK(es.weaponGiveEquipCount() == 1);
    }

    // --- (b) The SAME proximity trigger does NOT auto-pick-up a heavy/
    // pickup-only weapon type ----------------------------------------------
    {
        EngineState es;
        es.registerWeaponDrop("enemy_drop_minigun", 50, 0, 0, kMinigunWeaponId, kMinigunWeaponClass,
                               /*primaryAmmo=*/200, /*roundsInClip=*/50);

        // Walk right on top of it - still no auto-grant, heavy class is
        // excluded from the walk-over path entirely.
        es.updateWeaponDropPickup(50, 0, 0);
        CHECK(!es.isWeaponDropConsumed("enemy_drop_minigun"));
        CHECK(!es.hasWeapon(kMinigunWeaponId));
        CHECK(es.weaponGiveEquipCount() == 0);

        // Flamethrower (the spec's other named example) is excluded too.
        CHECK(EngineState::IsHeavyPickupOnlyWeaponClass(14));  // flamethrower
        CHECK(EngineState::IsHeavyPickupOnlyWeaponClass(17));  // minigun
        CHECK(!EngineState::IsHeavyPickupOnlyWeaponClass(kPistolWeaponClass));
    }

    // --- (c) A press-E prompt appears near a heavy-weapon pickup trigger,
    // and pressing interact grants the weapon through the same writer ------
    {
        EngineState es;
        EngineState::TriggerVolumeGeometry g;
        g.px = 200; g.py = 0; g.pz = 0;
        g.shapeCode = 1; // sphere
        g.radius = 2.0;
        es.registerTriggerGeometry("heavy_weapon_cache_trigger", g);
        es.registerHeavyWeaponPickupTrigger("heavy_weapon_cache_trigger", kMinigunWeaponId,
                                             /*primaryAmmo=*/200, /*roundsInClip=*/50);

        // Far away: no prompt, press does nothing.
        CHECK(es.updateHeavyWeaponPickupPrompt(0, 0, 0).empty());
        CHECK(!es.heavyWeaponPickupPromptShowing());
        CHECK(es.hudPromptRequests().empty());
        CHECK(!es.pressHeavyWeaponPickupInteract());
        CHECK(es.weaponGiveEquipCount() == 0);

        // Walk into the trigger -> prompt appears.
        const std::string& active = es.updateHeavyWeaponPickupPrompt(200, 0, 0);
        CHECK(active == "heavy_weapon_cache_trigger");
        CHECK(es.heavyWeaponPickupPromptShowing());
        CHECK(es.activeHeavyWeaponPickupTrigger() == "heavy_weapon_cache_trigger");
        CHECK(es.hudPromptRequests().size() == 1);
        CHECK(es.hudPromptRequests().back().target == "heavy_weapon_cache_trigger");

        // Re-polling the same position (still inside) does not re-fire the
        // prompt - same transition-only convention as the shop/crib unit.
        es.updateHeavyWeaponPickupPrompt(200, 0, 0);
        CHECK(es.hudPromptRequests().size() == 1);

        // Standing inside without pressing interact does NOT grant the
        // weapon - this path requires the explicit press, unlike path 1.
        CHECK(!es.hasWeapon(kMinigunWeaponId));

        // Pressing interact grants it via the shared writer.
        CHECK(es.pressHeavyWeaponPickupInteract());
        CHECK(es.heavyWeaponPickupInteractCount() == 1);
        CHECK(es.hasWeapon(kMinigunWeaponId));
        CHECK(es.weaponGiveEquipCount() == 1);
        CHECK(es.lastWeaponGiveEquipId() == kMinigunWeaponId);

        // Walking back out clears the prompt; pressing interact again is a
        // no-op (no live prompt to act on).
        CHECK(es.updateHeavyWeaponPickupPrompt(0, 0, 0).empty());
        CHECK(!es.heavyWeaponPickupPromptShowing());
        CHECK(!es.pressHeavyWeaponPickupInteract());
        CHECK(es.weaponGiveEquipCount() == 1); // unchanged
    }

    // --- (d) Both paths produce identical resulting slot state for the
    // same weapon id - proving they share one writer, not two divergent
    // ones -------------------------------------------------------------
    {
        EngineState esA; // via walk-over proximity path
        esA.registerWeaponDrop("drop_rifle", 0, 0, 0, 0x2001, /*weaponClass=*/2 /* "rifle" */,
                                /*primaryAmmo=*/90, /*roundsInClip=*/30);
        esA.updateWeaponDropPickup(0, 0, 0);
        const auto* slotA = esA.weaponSlot(0x2001);
        CHECK(slotA != nullptr);

        EngineState esB; // via press-E path (heavy classification irrelevant here -
                         // this checks the WRITER's output is identical, not the gate)
        EngineState::TriggerVolumeGeometry g;
        g.px = 0; g.py = 0; g.pz = 0; g.shapeCode = 1; g.radius = 5.0;
        esB.registerTriggerGeometry("rifle_cache", g);
        esB.registerHeavyWeaponPickupTrigger("rifle_cache", 0x2001, /*primaryAmmo=*/90, /*roundsInClip=*/30);
        esB.updateHeavyWeaponPickupPrompt(0, 0, 0);
        CHECK(esB.pressHeavyWeaponPickupInteract());
        const auto* slotB = esB.weaponSlot(0x2001);
        CHECK(slotB != nullptr);

        CHECK(slotA->weaponDefId == slotB->weaponDefId);
        CHECK(slotA->ammo[0] == slotB->ammo[0]); // primary ammo
        CHECK(slotA->ammo[1] == slotB->ammo[1]); // rounds in clip
        CHECK(slotA->ammo[2] == slotB->ammo[2]); // secondary ammo
        CHECK(slotA->flag0 == slotB->flag0);
        CHECK(slotA->flag1 == slotB->flag1);

        // Both counted through the SAME EngineState::giveEquipWeapon() -
        // checked indirectly: each EngineState's own counter reads exactly
        // 1 (one grant occurred, through the one shared writer, in each).
        CHECK(esA.weaponGiveEquipCount() == 1);
        CHECK(esB.weaponGiveEquipCount() == 1);
    }

    if (g_failures == 0) {
        std::cout << "All weapon pickup (dropped + heavy) checks passed.\n";
    } else {
        std::cerr << g_failures << " check(s) failed.\n";
    }
    return g_failures == 0 ? 0 : 1;
}
