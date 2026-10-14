// Synthetic tests for the mission-blocker rules synced from Team A (spec-lua-api-behaviour.md,
// 2026-10-12 FINAL blocks): each previously-OPEN engine state now follows the spec's rule.
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3luahost/zone_object_resolver.h"

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
    auto r = host.runChunk(L, code, "sweep_blockers_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool evalBool(Host& host, lua_State* L, const std::string& expr) {
    ok(host, L, "__r = " + expr);
    lua_getglobal(L, "__r");
    bool v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}

double evalNum(Host& host, lua_State* L, const std::string& expr) {
    ok(host, L, "__r = " + expr);
    lua_getglobal(L, "__r");
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sweep_blockers_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"tutorial_start", "gameplay"},          {"party_add_do", "gameplay"},
        {"release_to_world", "gameplay"},        {"set_attack_player_flag", "gameplay"},
        {"set_attack_enemies_flag", "gameplay"}, {"ai_scripted_action_is_processing", "gameplay"},
        {"door_open", "gameplay"},               {"light_group_set_intensity", "gameplay"},
        {"inv_item_remove", "gameplay"},         {"rc_set_max_signal_range", "gameplay"},
        {"get_current_hit_points", "gameplay"},  {"mission_is_complete", "gameplay"},
        {"cell_is_mission_complete", "gameplay"}, {"continuous_explosion_start", "gameplay"},
        {"audio_conversation_playing", "gameplay"}, {"force_throw_do", "gameplay"},
        {"object_is_in_trigger", "gameplay"},    {"inv_weapon_add_temporary", "gameplay"},
    };
}

} // namespace

int main() {
    // 1. Tutorial dispatcher bytes 0x0151d5a6 / 0x014f3d34 start 0 (Sec6.19 steps 1 and 6).
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        CHECK(es.tutorialStart().suppressionByte0151d5a6.known() && !es.tutorialStart().suppressionByte0151d5a6.get());
        CHECK(es.tutorialStart().mode014f3d34.known() && !es.tutorialStart().mode014f3d34.get());
    }

    // 2. '#CLOSEST_PLAYER#' is not a registered name: unresolved, never an OPEN error. party_add_do falls
    //    back to the closest-player redirect ('#PLAYER1#') exactly as Sec32.4 says.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.objectResolves().known("#CLOSEST_PLAYER#") && !es.objectResolves().get("#CLOSEST_PLAYER#"));
        es.objectResolves().set("Pal", true);
        ok(host, gp, "party_add_do('#CLOSEST_PLAYER#', {'Pal'}, false)");
        CHECK(es.party().membersByLeader["#PLAYER1#"].size() == 1);
    }

    // 3. release_to_world: the kind comes from the registry (group -> character -> vehicle); an unknown
    //    kind does nothing and returns normally.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Starting_Vehicles", true);
        es.objectResolves().set("Script_Vehicle 001", true);
        es.objectResolves().set("Npc1", true);
        es.objectResolves().set("Unknown1", true);
        es.releaseToWorldGroupMembers()["Starting_Vehicles"] = {"v1", "v2"};
        es.getOrCreateVehicle("Script_Vehicle 001");
        es.getOrCreateCharacter("Npc1");
        ok(host, gp, "release_to_world('Starting_Vehicles')");
        CHECK(es.releasedCount()["v1"] == 1 && es.releasedCount()["v2"] == 1);
        ok(host, gp, "release_to_world('Script_Vehicle 001')");
        CHECK(es.releasedCount()["Script_Vehicle 001"] == 1);
        ok(host, gp, "release_to_world('Npc1')");
        CHECK(es.releasedCount()["Npc1"] == 1);
        ok(host, gp, "release_to_world('Unknown1')");
        CHECK(es.releaseToWorldUnclassifiedCount() == 1);
    }

    // 4. vtable +0x68 predicate: true for a resolved live character -> both flag locations written.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Npc1", true);
        ok(host, gp, "set_attack_player_flag('Npc1', true)");
        CharacterState& c = es.getOrCreateCharacter("Npc1");
        CHECK(c.attackPlayerBitAc.get() == true);
        CHECK(c.attackPlayerOnSightBit9.known() && c.attackPlayerOnSightBit9.get() == true);
        ok(host, gp, "set_attack_enemies_flag('Npc1', true)");
        CHECK(c.attackEnemiesBitAc == true);
        CHECK(c.aiDataFlags9.known(CharacterState::kAttackEnemiesOnSightBit9));
        // explicit set(false) still selects "predicate false" (local bit only)
        es.objectResolves().set("Npc2", true);
        es.getOrCreateCharacter("Npc2").attackPlayerSecondaryGatePredicate.set(false);
        ok(host, gp, "set_attack_player_flag('Npc2', true)");
        CHECK(!es.getOrCreateCharacter("Npc2").attackPlayerOnSightBit9.known());
    }

    // 5. dlc3_m03: gate 0x008addb0 is true in single player -> ai_scripted_action_is_processing returns false.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Npc3", true);
        CHECK(!evalBool(host, gp, "ai_scripted_action_is_processing('Npc3')"));
    }

    // 6. m01: a door with no (known) +0xc8 anchor skips only the animation step, no error.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Door1", true);
        es.objectResolves().set("Npc1", true);
        CHECK(!evalBool(host, gp, "door_open('Door1', false, true, 'Npc1', 'anim')"));
        CHECK(es.getOrCreatePlacedObject("Door1").richPathAppliedCount == 0);
    }

    // 7. m08 light group / m16 inventory item 'none'.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "light_group_set_intensity('Script_Light_Group_m08_ALL', 0.5)");
        CHECK(es.lightGroup("Script_Light_Group_m08_ALL").applyCount == 0);
        es.objectResolves().set("Npc1", true);
        ok(host, gp, "inv_item_remove('none', 'Npc1')");
        CHECK(es.getOrCreateCharacter("Npc1").inventoryItemRemoveAppliedCount == 0);
        fails(host, gp, "inv_item_remove('some_unregistered_item', 'Npc1')", "inventory item definition");
    }

    // 8. m20: RC max signal range starts at 200 (tweak RC_vehicle_gun_max_distance); setter returns the old value.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        CHECK(std::fabs(evalNum(host, gp, "rc_set_max_signal_range(50)") - 200.0) < 1e-9);
        CHECK(std::fabs(evalNum(host, gp, "rc_set_max_signal_range(200)") - 50.0) < 1e-9);
    }

    // 9. m21: current hit points start at the max (Sec34.2 step 2) instead of OPEN.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Npc1", true);
        es.getOrCreateCharacter("Npc1").maxHitPoints.set(150);
        CHECK(std::fabs(evalNum(host, gp, "get_current_hit_points('Npc1')") - 150.0) < 1e-9);
        CHECK(es.getOrCreateCharacter("Npc1").currentHitPoints.known());
    }

    // 10. m22: no mission is complete unless host state says so.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("m23", true);
        CHECK(!evalBool(host, gp, "mission_is_complete('m23')"));
        es.missionComplete().set("m23", true);
        CHECK(evalBool(host, gp, "mission_is_complete('m23')"));
    }

    // 11. m23: once continuous_explosions.xtbl names are installed, an unknown name is a silent no-op.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Target", true);
        es.continuousExplosion().installNames({"Other"});
        ok(host, gp, "continuous_explosion_start('Daedalus', 'Target')");
        CHECK(es.continuousExplosion().startCount == 0);
        es.continuousExplosion().active.set(false);
        ok(host, gp, "continuous_explosion_start('Other', 'Target')");
        CHECK(es.continuousExplosion().startCount == 1);
    }

    // 12. mm_m16_5: per-slot conversation query is false with no voice playback; marked slots are true.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(!evalBool(host, gp, "audio_conversation_playing(2)"));
        es.conversationSlotPlaying().set("2", true);
        CHECK(evalBool(host, gp, "audio_conversation_playing(2)"));
    }

    // 13. sh04: force_throw_do needs a thrown-class (Weapon_Class 5) weapon with ammo > 0; false stages nothing.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.installWeaponDefinitionNames({"grenade", "pistol"});
        es.setWeaponDefinitionClass("grenade", EngineState::kWeaponClassThrown);
        es.setWeaponDefinitionClass("pistol", 0);
        es.objectResolves().set("Npc1", true);
        ok(host, gp, "force_throw_do('Npc1')");
        CHECK(es.getOrCreateCharacter("Npc1").caeStagedActionIndex != 4);
        ok(host, gp, "inv_weapon_add_temporary('Npc1', 'pistol', 5)");
        ok(host, gp, "force_throw_do('Npc1')");
        CHECK(es.getOrCreateCharacter("Npc1").caeStagedActionIndex != 4);
        ok(host, gp, "inv_weapon_add_temporary('Npc1', 'grenade', 2)");
        ok(host, gp, "force_throw_do('Npc1')");
        CHECK(es.getOrCreateCharacter("Npc1").caeStagedActionIndex == 4);
    }

    // 14. dlc2_m02: object_is_in_trigger is the geometric test of the object's position against the trigger shape.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("trg_loadout_1", true);
        EngineState::TriggerVolumeGeometry g;
        g.px = 10; g.py = 0; g.pz = 10;
        g.shapeCode = 1;
        g.radius = 5;
        es.registerTriggerGeometry("trg_loadout_1", g);
        es.bridgeLiveCharacterPosition("#PLAYER1#", WorldPosition{11, 0, 11});
        CHECK(evalBool(host, gp, "object_is_in_trigger('trg_loadout_1', '#PLAYER1#')"));
        es.bridgeLiveCharacterPosition("#PLAYER1#", WorldPosition{100, 0, 100});
        CHECK(!evalBool(host, gp, "object_is_in_trigger('trg_loadout_1', '#PLAYER1#')"));
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_sweep_blockers_897a7d5_test: all checks passed\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
}
