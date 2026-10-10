// Synthetic tests for wave 2, Batch W2-4 - spec-lua-api-behaviour.md Sec24
// ("ranks 451-550 tranche, part A", 27 of its 28 names) + Sec25 ("ranks
// 451-550 tranche, part B", 27 of its 28 names), 54 names total - see
// src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own top comment.
// Same hand-written-fixture convention as every other synthetic_luahost_*_
// test.cpp. All 54 names are real `gameplay`-cluster registrations
// (verified directly against tools/lua_all_registered_1490_tagged.txt).
#include <cmath>
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
    auto r = host.runChunk(L, code, "wave2_w24b_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w24b_test.lua");
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

std::vector<RegisteredName> fixtureNames() {
    return {
        // Section 24.
        {"boss_battle_matt_cheats_stop", "gameplay"},
        {"bink_play", "gameplay"},
        {"wind_override_clear", "gameplay"},
        {"wander_start", "gameplay"},
        {"vehicle_spotlight_cancel", "gameplay"},
        {"vehicle_set_explosion_damage_multiplier", "gameplay"},
        {"vehicle_in_water", "gameplay"},
        {"vehicle_get_smoke_and_fire_state", "gameplay"},
        {"vehicle_enable_physics", "gameplay"},
        {"tutorial_unlock", "gameplay"},
        {"tutorial_queued", "gameplay"},
        {"trigger_type_enable", "gameplay"},
        {"trigger_set_hold_to_repeat", "gameplay"},
        {"skydive_set_elev_angles_deg", "gameplay"},
        {"set_suppress_synced_melee_attacks_flag", "gameplay"},
        {"radio_newsbreak", "gameplay"},
        {"player_supress_falling_scream", "gameplay"},
        {"player_super_powered", "gameplay"},
        {"on_wieldable_prop_destroy", "gameplay"},
        {"on_taunt", "gameplay"},
        {"on_random_obj_killed_do", "gameplay"},
        {"on_notoriety_event", "gameplay"},
        {"on_hud_screen_exit", "gameplay"},
        {"on_character_grabbed", "gameplay"},
        {"npc_weapon_spread", "gameplay"},
        {"npc_unholster_best_weapon", "gameplay"},
        {"npc_drop_grenades", "gameplay"},
        // Section 25.
        {"notoriety_spawn_group_set_destination", "gameplay"},
        {"notoriety_spawn_group_clear_destination", "gameplay"},
        {"mission_store_purchased", "gameplay"},
        {"mission_set_next_mission", "gameplay"},
        {"message_remove_all", "gameplay"},
        {"mesh_mover_update_control", "gameplay"},
        {"item_hide", "gameplay"},
        {"inv_item_is_firearm_equipped", "gameplay"},
        {"human_set_force_holster", "gameplay"},
        {"human_set_always_sprint", "gameplay"},
        {"human_riding_trigger_end", "gameplay"},
        {"hud_inventory_disable", "gameplay"},
        {"hud_critical_timer_get_remainder", "gameplay"},
        {"helicopter_go_to_set_target", "gameplay"},
        {"helicopter_fly_to_stop", "gameplay"},
        {"group_destroy_do", "gameplay"},
        {"get_char_in_vehicle", "gameplay"},
        {"game_choice_get_state", "gameplay"},
        {"force_ai_mode_check_done", "gameplay"},
        {"debris_flow_destroy", "gameplay"},
        {"debris_flow_create", "gameplay"},
        {"debris_flow_add_avoid_object", "gameplay"},
        {"continuous_explosion_follow_target_add", "gameplay"},
        {"check_script_animation_state", "gameplay"},
        {"character_set_combat_ready", "gameplay"},
        {"character_set_can_take_human_shield", "gameplay"},
        {"character_is_released", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: all 54 land in gameplayState(), nowhere else.
    {
        Host host(fixtureNames());
        CHECK(!hasGlobal(host.uiState(), "character_is_released"));
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
        }
    }

    Host host(fixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // Section 24.
    // ==================================================================

    // Sec24.1 boss_battle_matt_cheats_stop: always counts; resets `active`.
    es.bossBattleMattCheatsActive() = true;
    ok(host, gp, "boss_battle_matt_cheats_stop()");
    CHECK(!es.bossBattleMattCheatsActive());
    CHECK(es.bossBattleMattCheatsStopCount() == 1);

    // Sec24.2 bink_play: genuine no-op, any arguments.
    ok(host, gp, "bink_play(1, 'x', true)");

    // Sec24.3 wind_override_clear: resets to (1,0,0)/0; notify only when the
    // shared replication-mode flag is set.
    es.windOverride().x = 0.5;
    es.windOverride().magnitude = 9.0;
    ok(host, gp, "wind_override_clear()");
    CHECK(es.windOverride().x == 1.0 && es.windOverride().magnitude == 0.0);
    CHECK(es.windOverride().clearNotifyCount == 0);
    es.actionStopSyncedStateFlag() = true;
    ok(host, gp, "wind_override_clear()");
    CHECK(es.windOverride().clearNotifyCount == 1);

    // Sec24.4 wander_start: gated on a non-negative height.
    es.objectResolves().set("Wanderer1", true);
    ok(host, gp, "wander_start('Wanderer1')"); // default height -1.0 -> gate fails
    CHECK(es.getOrCreateCharacter("Wanderer1").wanderStartCount == 0);
    ok(host, gp, "wander_start('Wanderer1', nil, 3.0)");
    CHECK(es.getOrCreateCharacter("Wanderer1").wanderStartCount == 1);
    CHECK(es.getOrCreateCharacter("Wanderer1").wanderHeight == 3.0);
    es.objectResolves().set("WanderTarget1", true);
    ok(host, gp, "wander_start('Wanderer1', 'WanderTarget1', 2.0)");
    CHECK(es.getOrCreateCharacter("Wanderer1").wanderTargetName == "WanderTarget1");

    // Sec24.5 vehicle_spotlight_cancel: 2nd argument is a confirmed dead read.
    es.objectResolves().set("SpotVeh", true);
    es.getOrCreateVehicle("SpotVeh").spotlightActive = true;
    ok(host, gp, "vehicle_spotlight_cancel('SpotVeh', true)");
    CHECK(!es.getOrCreateVehicle("SpotVeh").spotlightActive);
    CHECK(es.getOrCreateVehicle("SpotVeh").spotlightCancelCount == 1);

    // Sec24.6 vehicle_set_explosion_damage_multiplier: double-gate, passes
    // true by construction in single player.
    es.objectResolves().set("ExplVeh", true);
    ok(host, gp, "vehicle_set_explosion_damage_multiplier('ExplVeh', 2.5)");
    CHECK(es.getOrCreateVehicle("ExplVeh").explosionDamageMultiplier == 2.5);

    // Sec24.7/Sec24.8 vehicle_in_water / vehicle_get_smoke_and_fire_state.
    es.objectResolves().set("WetVeh", true);
    es.getOrCreateVehicle("WetVeh").inWater = true;
    ok(host, gp, "assert(vehicle_in_water('WetVeh') == true)");
    es.objectResolves().set("NoSuchVehicleABC", false);
    ok(host, gp, "assert(vehicle_in_water('NoSuchVehicleABC') == false)");
    es.getOrCreateVehicle("WetVeh").smokeActive = true;
    ok(host, gp, "local s, f = vehicle_get_smoke_and_fire_state('WetVeh'); assert(s == true and f == false)");

    // Sec24.10 vehicle_enable_physics: CONFIRMED absent-capability -> false,
    // not an OPEN refusal.
    es.objectResolves().set("PhysVeh", true);
    ok(host, gp, "assert(vehicle_enable_physics('PhysVeh', true) == false)"); // fullySetUp unknown
    es.getOrCreateVehicle("PhysVeh").fullySetUp.set(true);
    ok(host, gp, "assert(vehicle_enable_physics('PhysVeh', true, false) == true)");
    CHECK(es.getOrCreateVehicle("PhysVeh").physicsEnabled);
    CHECK(es.getOrCreateVehicle("PhysVeh").physicsEnabledSecondaryArg == false);

    // Sec24.11/Sec24.12 tutorial_unlock / tutorial_queued: host-gated unlock
    // (CONFIRMED no-op in single player); queued reads the shared 210-entry
    // table's own per-entry state.
    ok(host, gp, "tutorial_unlock('save', true)");
    CHECK(es.tutorialUnlockCount(0) == 0); // not host -> no-op
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    ok(host, gp, "tutorial_unlock('save', true)");
    CHECK(es.tutorialUnlockCount(0) == 1);
    ok(host, gp, "assert(tutorial_queued('save') == false)"); // seeded state 0
    es.tutorialState().set(EngineState::tutorialStateKey(0), 2);
    ok(host, gp, "assert(tutorial_queued('save') == true)");

    // Sec24.13 trigger_type_enable: default enable=true; absent = enabled.
    ok(host, gp, "trigger_type_enable('MyType')"); // default enable=true, no return value
    CHECK(!es.triggerTypeDisabled()["MyType"]);
    ok(host, gp, "trigger_type_enable('MyType', false)");
    CHECK(es.triggerTypeDisabled()["MyType"]);
    CHECK(es.triggerTypeEnableNotifyCount() == 2);

    // Sec24.14 trigger_set_hold_to_repeat.
    es.objectResolves().set("Trig24", true);
    ok(host, gp, "trigger_set_hold_to_repeat('Trig24')"); // default true
    CHECK(es.triggerHoldToRepeat()["Trig24"]);

    // Sec24.15 skydive_set_elev_angles_deg: CONFIRMED exact conversion.
    ok(host, gp, "skydive_set_elev_angles_deg(90, 180)");
    CHECK(std::abs(es.skydiveElevAngleA() - 90.0 * 0.017453) < 1e-9);
    CHECK(std::abs(es.skydiveElevAngleB() - 180.0 * 0.017453) < 1e-9);

    // Sec24.16 set_suppress_synced_melee_attacks_flag.
    es.objectResolves().set("Melee1", true);
    ok(host, gp, "set_suppress_synced_melee_attacks_flag('Melee1')"); // default true
    CHECK(es.getOrCreateCharacter("Melee1").forceFlags1c98.get(CharacterState::kSuppressSyncedMeleeAttacksBit1c98) != 0);

    // Sec24.17 radio_newsbreak.
    ok(host, gp, "radio_newsbreak('intro_news')");
    CHECK(es.currentRadioNewsbreak() == "intro_news");

    // Sec24.18 player_supress_falling_scream: always-set, never cleared.
    es.objectResolves().set("Scream1", true);
    ok(host, gp, "player_supress_falling_scream('Scream1')");
    CHECK(es.getOrCreateCharacter("Scream1").fallingScreamSuppressed);

    // Sec24.19 player_super_powered.
    es.objectResolves().set("Super1", true);
    ok(host, gp, "player_super_powered('Super1', true)");
    CHECK(es.getOrCreateCharacter("Super1").forceFlags1c9c.get(CharacterState::kSuperPoweredBit1c9c) != 0);

    // Sec24.20 on_wieldable_prop_destroy.
    es.objectResolves().set("Prop1", true);
    ok(host, gp, "on_wieldable_prop_destroy('OnDestroy', 'Prop1')");
    CHECK(es.namedHookCallback("on_wieldable_prop_destroy:Prop1") == "OnDestroy");

    // Sec24.21 on_taunt: generic path wins when the name resolves; the
    // character-fallback path only ever records a non-empty, unresolved name
    // (this project's single flat objectResolves() map cannot independently
    // re-check the real engine's own second resolver).
    es.objectResolves().set("TauntObj", true);
    ok(host, gp, "on_taunt('OnTaunt', 'TauntObj')");
    CHECK(es.namedHookCallback("on_taunt:generic:TauntObj") == "OnTaunt");
    ok(host, gp, "on_taunt('OnTaunt2', 'SomeCharacterName')");
    CHECK(es.namedHookCallback("on_taunt:character:SomeCharacterName") == "OnTaunt2");

    // Sec24.22 on_random_obj_killed_do.
    ok(host, gp, "on_random_obj_killed_do('vehicles', 'OnKilled', 2)");
    CHECK(es.randomObjKilledHooks().size() == 1);
    CHECK(es.randomObjKilledHooks()[0].objectCategoryName == "vehicles");
    CHECK(es.randomObjKilledHooks()[0].callbackName == "OnKilled");
    CHECK(es.randomObjKilledHooks()[0].category == 2);

    // Sec24.23 on_notoriety_event: default "#PLAYER1#".
    es.objectResolves().set("#PLAYER1#", true);
    ok(host, gp, "on_notoriety_event('OnNotoriety')");
    CHECK(es.namedHookCallback("on_notoriety_event:#PLAYER1#") == "OnNotoriety");

    // Sec24.24 on_hud_screen_exit: a never-populated, test-registrable registry.
    fails(host, gp, "on_hud_screen_exit('OnExit', 'pause_screen')", "pause_screen");
    es.hudScreenNameResolves().set("pause_screen", true);
    ok(host, gp, "on_hud_screen_exit('OnExit', 'pause_screen')");
    CHECK(es.namedHookCallback("on_hud_screen_exit:pause_screen") == "OnExit");

    // Sec24.25 on_character_grabbed: same dual-path shape as on_taunt.
    es.objectResolves().set("GrabObj", true);
    ok(host, gp, "on_character_grabbed('OnGrab', 'GrabObj')");
    CHECK(es.namedHookCallback("on_character_grabbed:generic:GrabObj") == "OnGrab");

    // Sec24.26 npc_weapon_spread: default 1.0.
    es.objectResolves().set("Spreader1", true);
    ok(host, gp, "npc_weapon_spread('Spreader1')");
    CHECK(es.getOrCreateCharacter("Spreader1").npcSpreadMultiplier1e04 == 1.0);
    ok(host, gp, "npc_weapon_spread('Spreader1', 2.0)");
    CHECK(es.getOrCreateCharacter("Spreader1").npcSpreadMultiplier1e04 == 2.0);

    // Sec24.27 npc_unholster_best_weapon.
    es.objectResolves().set("Unholster1", true);
    ok(host, gp, "npc_unholster_best_weapon('Unholster1')");
    CHECK(es.getOrCreateCharacter("Unholster1").unholsterBestWeaponCount == 1);

    // Sec24.28 npc_drop_grenades: bare global.
    ok(host, gp, "npc_drop_grenades(true)");
    CHECK(es.npcDropGrenades());

    // ==================================================================
    // Section 25.
    // ==================================================================

    // Sec25.1/Sec25.2 notoriety_spawn_group_set_destination/_clear_destination.
    fails(host, gp, "notoriety_spawn_group_set_destination('Rollerz', 'Dest1')", "Rollerz");
    es.notorietySpawnGroupResolves().set("Rollerz", true);
    es.objectResolves().set("Dest1", true);
    ok(host, gp, "notoriety_spawn_group_set_destination('Rollerz', 'Dest1')");
    CHECK(es.getOrCreateNotorietySpawnGroup("Rollerz").destinationTargetName == "Dest1");
    ok(host, gp, "notoriety_spawn_group_clear_destination('Rollerz')");
    CHECK(es.getOrCreateNotorietySpawnGroup("Rollerz").destinationTargetName.empty());
    CHECK(es.getOrCreateNotorietySpawnGroup("Rollerz").clearDestinationCount == 1);

    // Sec25.3 mission_store_purchased: CHOSEN store-and-echo.
    ok(host, gp, "assert(mission_store_purchased(true) == true)");
    CHECK(es.missionStorePurchased());

    // Sec25.4 mission_set_next_mission - CORRECTED by Sec60.3: genuine no-op.
    ok(host, gp, "mission_set_next_mission('m01')");
    CHECK(es.missionSetNextMissionNoopCount() == 1);

    // Sec25.5 message_remove_all: no real arguments consumed.
    ok(host, gp, "message_remove_all(1, 2, 3)");
    CHECK(es.messageRemoveAllCount() == 1);

    // Sec25.6 mesh_mover_update_control: both gates must be satisfied.
    es.objectResolves().set("Mover1", true);
    fails(host, gp, "mesh_mover_update_control('Mover1', 'speed', true)", "+0xc8");
    es.getOrCreatePlacedObject("Mover1").hasInteractionAnchor.set(false);
    fails(host, gp, "mesh_mover_update_control('Mover1', 'speed', true)", "+0xc4");
    es.getOrCreatePlacedObject("Mover1").hasMoverControlSubRecord.set(true);
    ok(host, gp, "mesh_mover_update_control('Mover1', 'enabled', true)");
    CHECK(es.getOrCreatePlacedObject("Mover1").moverBoolControls["enabled"] == true);
    ok(host, gp, "mesh_mover_update_control('Mover1', 'speed', 3.7, true)");
    CHECK(es.getOrCreatePlacedObject("Mover1").moverNumericControls["speed"] == 3.0); // truncated
    CHECK(es.getOrCreatePlacedObject("Mover1").moverControlUpdateCount == 2);

    // Sec25.7 item_hide: reuses item_show's own backing field.
    es.objectResolves().set("Item1", true);
    ok(host, gp, "item_hide('Item1')");
    CHECK(es.itemHidden()["Item1"]);

    // Sec25.8 inv_item_is_firearm_equipped: default "#PLAYER1#".
    es.objectResolves().set("#PLAYER1#", true);
    es.getOrCreateCharacter("#PLAYER1#").firearmEquipped = true;
    ok(host, gp, "assert(inv_item_is_firearm_equipped() == true)");

    // Sec25.9/Sec25.10 human_set_force_holster / human_set_always_sprint:
    // confirmed asymmetry - different target fields.
    es.objectResolves().set("Human1", true);
    ok(host, gp, "human_set_force_holster('Human1', true)");
    CHECK(es.getOrCreateCharacter("Human1").forceHolster);
    ok(host, gp, "human_set_always_sprint('Human1', true)");
    CHECK(es.getOrCreateCharacter("Human1").alwaysSprintAiData2b0);

    // Sec25.11 human_riding_trigger_end: predicate-gated.
    es.objectResolves().set("Rider1", true);
    ok(host, gp, "human_riding_trigger_end('Rider1')"); // not active -> no-op
    CHECK(es.getOrCreateCharacter("Rider1").humanRidingTriggerEndCount == 0);
    es.getOrCreateCharacter("Rider1").ridingTriggerActive = true;
    ok(host, gp, "human_riding_trigger_end('Rider1')");
    CHECK(es.getOrCreateCharacter("Rider1").humanRidingTriggerEndCount == 1);
    CHECK(!es.getOrCreateCharacter("Rider1").ridingTriggerActive);

    // Sec25.12 hud_inventory_disable: always both.
    ok(host, gp, "hud_inventory_disable(true)");
    CHECK(es.hudInventoryDisabled());
    CHECK(es.hudInventoryDisableReplicateCount() == 1);
    CHECK(es.hudInventoryDisableSecondaryCallCount() == 1);

    // Sec25.13 hud_critical_timer_get_remainder.
    es.hudCriticalTimerRemainder() = 4.5;
    ok(host, gp, "assert(hud_critical_timer_get_remainder() == 4.5)");

    // Sec25.14/Sec25.15 helicopter_go_to_set_target / helicopter_fly_to_stop:
    // extend the SAME HelicopterFlyTo struct wave 1 Batch 3 already uses.
    es.objectResolves().set("Heli25", true);
    es.objectResolves().set("GoTarget1", true);
    ok(host, gp, "helicopter_go_to_set_target('Heli25', 'GoTarget1')");
    CHECK(es.helicopterFlyTo().lastGoToTarget["Heli25"].targetName == "GoTarget1");
    CHECK(es.helicopterFlyTo().lastGoToTarget["Heli25"].applied);
    fails(host, gp, "helicopter_fly_to_stop('Heli25')", "Heli25");
    es.helicopterFlyTo().qualifies.set("Heli25", true);
    ok(host, gp, "helicopter_fly_to_stop('Heli25')");
    CHECK(es.helicopterFlyTo().lastGoToTarget.count("Heli25") == 0);
    CHECK(es.helicopterFlyTo().flyToStopCount == 1);

    // Sec25.16 group_destroy_do: SAME guardian-angel group table Sec22.23 uses.
    es.setGuardianAngelGroupRegistered("Escort1", true);
    ok(host, gp, "group_destroy_do('Escort1')");
    CHECK(!es.guardianAngelGroupRegistered("Escort1"));
    CHECK(es.groupDestroyedCount() == 1);

    // Sec25.17 get_char_in_vehicle: seat 0 reuses VehicleState::driverName;
    // any other seat is OPEN/nil (no per-seat registry beyond seat 0).
    es.objectResolves().set("SeatVeh", true);
    ok(host, gp, "local n = get_char_in_vehicle('SeatVeh', 0); assert(n == nil)");
    es.getOrCreateVehicle("SeatVeh").driverName.set("Driver1");
    ok(host, gp, "assert(get_char_in_vehicle('SeatVeh', 0) == 'Driver1')");
    ok(host, gp, "local n = get_char_in_vehicle('SeatVeh', 1); assert(n == nil)");

    // Sec25.18 game_choice_get_state: reuses the Sec15.26 bitmask directly.
    ok(host, gp, "assert(game_choice_get_state(3) == false)");
    es.setGameChoiceState(3, true);
    ok(host, gp, "assert(game_choice_get_state(3) == true)");

    // Sec25.19 force_ai_mode_check_done.
    es.objectResolves().set("ForceAi1", true);
    ok(host, gp, "assert(force_ai_mode_check_done('ForceAi1') == false)");
    es.getOrCreateCharacter("ForceAi1").forceAiModePendingTagSet = true;
    ok(host, gp, "assert(force_ai_mode_check_done('ForceAi1') == true)");
    CHECK(!es.getOrCreateCharacter("ForceAi1").forceAiModePendingTagSet);

    // Sec25.20/Sec25.21/Sec25.22 debris_flow_destroy/create/add_avoid_object:
    // SAME 3-slot table Sec22.21's debris_flow_add_object uses.
    ok(host, gp, "local id = debris_flow_create(); assert(id == 0)");
    CHECK(es.debrisFlow().flows[0].allocated);
    ok(host, gp, "local id = debris_flow_create(); assert(id == 1)");
    ok(host, gp, "local id = debris_flow_create(); assert(id == 2)");
    ok(host, gp, "local id = debris_flow_create(); assert(id == 0)"); // CHOSEN hardened recycle
    CHECK(es.debrisFlow().createRecycledCount == 1);
    es.objectResolves().set("AvoidObj1", true);
    ok(host, gp, "debris_flow_add_avoid_object(1, 'AvoidObj1')");
    CHECK(es.debrisFlow().flows[1].avoidObjects.size() == 1);
    ok(host, gp, "debris_flow_destroy(1)");
    CHECK(!es.debrisFlow().flows[1].allocated);
    CHECK(es.debrisFlow().destroyCount == 1);
    ok(host, gp, "debris_flow_destroy(99)"); // out of range - HARDENING POLICY, bounded
    CHECK(es.debrisFlow().destroySkippedCount == 1);

    // Sec25.23 continuous_explosion_follow_target_add - CORRECTED liveness
    // (Sec57), capped at 2.
    es.objectResolves().set("Follow1", true);
    es.objectResolves().set("Follow2", true);
    es.objectResolves().set("Follow3", true);
    ok(host, gp, "continuous_explosion_follow_target_add('Follow1')"); // not active -> dropped
    CHECK(es.continuousExplosion().followTargets.empty());
    CHECK(es.continuousExplosion().followTargetAddDroppedCount == 1);
    es.continuousExplosion().active.set(true);
    ok(host, gp, "continuous_explosion_follow_target_add('Follow1')");
    ok(host, gp, "continuous_explosion_follow_target_add('Follow2')");
    ok(host, gp, "continuous_explosion_follow_target_add('Follow3')"); // cap of 2 -> dropped
    CHECK(es.continuousExplosion().followTargets.size() == 2);
    CHECK(es.continuousExplosion().followTargetAddCount == 2);
    CHECK(es.continuousExplosion().followTargetAddDroppedCount == 2);

    // Sec25.24 check_script_animation_state: direct read counterpart of
    // set_script_animation_state (Sec7.17) - reuses its own backing fields.
    es.objectResolves().set("ScriptAnim1", true);
    ok(host, gp, "assert(check_script_animation_state('ScriptAnim1', 'idle') == false)"); // scriptNpcBound false
    es.getOrCreateCharacter("ScriptAnim1").scriptNpcBound = true;
    es.registerAnimationStateIdForTesting("walking", 9);
    es.getOrCreateCharacter("ScriptAnim1").scriptAnimationStateId = 9;
    ok(host, gp, "assert(check_script_animation_state('ScriptAnim1', 'walking') == true)");

    // Sec25.25 character_set_combat_ready.
    es.objectResolves().set("Combat1", true);
    ok(host, gp, "character_set_combat_ready('Combat1', true, 2)");
    CHECK(es.getOrCreateCharacter("Combat1").combatReady);
    CHECK(es.getOrCreateCharacter("Combat1").combatReadyMode == 2);

    // Sec25.26 character_set_can_take_human_shield.
    es.objectResolves().set("Shieldable1", true);
    ok(host, gp, "character_set_can_take_human_shield('Shieldable1', false)");
    CHECK(!es.getOrCreateCharacter("Shieldable1").canTakeHumanShield);

    // Sec25.28 character_is_released: default-safe direction (unbound ->
    // released), explicit resolve-false also takes the safe default.
    es.objectResolves().set("Released1", true);
    ok(host, gp, "assert(character_is_released('Released1') == true)");
    es.getOrCreateCharacter("Released1").boundOrPossessed = true;
    ok(host, gp, "assert(character_is_released('Released1') == false)");
    es.objectResolves().set("NeverResolves1", false);
    ok(host, gp, "assert(character_is_released('NeverResolves1') == true)");

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_wave2_w24b_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_wave2_w24b_test: " << g_failures << " check(s) failed\n";
    return 1;
}
