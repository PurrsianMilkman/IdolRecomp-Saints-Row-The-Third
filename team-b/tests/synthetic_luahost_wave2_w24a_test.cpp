// Synthetic tests for wave 2, Batch W2-4 - spec-lua-api-behaviour.md Sec22
// ("ranks 351-450 tranche, part B", 24 of its 30 names - see
// src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp's own top comment for the
// other 6, which belong to sibling wave-2 batches). Same hand-written-
// fixture convention as every other synthetic_luahost_*_test.cpp (see
// tests/synthetic_luahost_b10_batch3_test.cpp for the template this follows).
// All 24 names are real `gameplay`-cluster registrations (verified directly
// against tools/lua_all_registered_1490_tagged.txt).
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
    auto r = host.runChunk(L, code, "wave2_w24a_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w24a_test.lua");
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
        {"vehicle_make_corpse", "gameplay"},
        {"teleport_player_vehicles", "gameplay"},
        {"spotlight_heli_laser_enable", "gameplay"},
        {"roadblocks_ambient_traffic_enable", "gameplay"},
        {"party_set_dismissable", "gameplay"},
        {"party_allow_max_followers", "gameplay"},
        {"on_projectile_hit", "gameplay"},
        {"on_near_crash", "gameplay"},
        {"npc_dont_drop_weapon_on_death", "gameplay"},
        {"notoriety_spawn_only_air", "gameplay"},
        {"notoriety_restricted_zones_enable", "gameplay"},
        {"is_rappelling", "gameplay"},
        {"ingame_effect_remove_trigger", "gameplay"},
        {"get_vehicle_type", "gameplay"},
        {"dlc_gis_film_grain_load", "gameplay"},
        {"debris_flow_add_object", "gameplay"},
        {"cower", "gameplay"},
        {"continuous_spawn_set_orders", "gameplay"},
        {"continuous_explosion_stop", "gameplay"},
        {"check_animation_state", "gameplay"},
        {"character_release_human_shield", "gameplay"},
        {"boss_battle_kb_enable_crowd_props", "gameplay"},
        {"base_jumping_enable", "gameplay"},
        {"wind_override_set", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: all 24 land in gameplayState(), nowhere else.
    {
        Host host(fixtureNames());
        CHECK(!hasGlobal(host.uiState(), "vehicle_make_corpse"));
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
        }
    }

    Host host(fixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // Sec22.2 vehicle_make_corpse: OPEN until resolved; no-ops once already a corpse.
    fails(host, gp, "vehicle_make_corpse('V1', false)", "V1");
    es.objectResolves().set("V1", true);
    ok(host, gp, "vehicle_make_corpse('V1', true)");
    CHECK(es.getOrCreateVehicle("V1").isCorpse);
    CHECK(es.getOrCreateVehicle("V1").corpseExplodeArg == true);
    CHECK(es.getOrCreateVehicle("V1").makeCorpseCount == 1);
    ok(host, gp, "vehicle_make_corpse('V1', true)"); // already a corpse -> no-op
    CHECK(es.getOrCreateVehicle("V1").makeCorpseAlreadyCorpseNoopCount == 1);
    CHECK(es.getOrCreateVehicle("V1").makeCorpseCount == 1);

    // Sec22.5 teleport_player_vehicles: PLAYER1 always relocates when its own
    // vehicle is set and resolves; PLAYER2 only when a co-op partner is present.
    es.objectResolves().set("RefA", true);
    es.objectResolves().set("RefB", true);
    es.objectResolves().set("P1Car", true);
    es.objectResolves().set("P2Car", true);
    es.playerRig().player1VehicleName = "P1Car";
    ok(host, gp, "teleport_player_vehicles('RefA', 'RefB')");
    CHECK(es.getOrCreateVehicle("P1Car").lastTeleportRelocatedToName == "RefA");
    CHECK(es.playerRig().teleportPlayer1Count == 1);
    CHECK(es.playerRig().teleportPlayer2Count == 0); // no co-op partner known yet
    es.playerRig().coopPlayerPresent.set(true);
    es.playerRig().player2VehicleName = "P2Car";
    ok(host, gp, "teleport_player_vehicles('RefA', 'RefB')");
    CHECK(es.getOrCreateVehicle("P2Car").lastTeleportRelocatedToName == "RefB");
    CHECK(es.playerRig().teleportPlayer2Count == 1);

    // Sec22.6 spotlight_heli_laser_enable: bare global, no resolver.
    CHECK(!es.spotlightHeliLaserEnabled());
    ok(host, gp, "spotlight_heli_laser_enable(true)");
    CHECK(es.spotlightHeliLaserEnabled());

    // Sec22.7 roadblocks_ambient_traffic_enable: CONFIRMED no-op in single
    // player (coopLocalIsHost() is false by construction, applySpecInitialState).
    ok(host, gp, "roadblocks_ambient_traffic_enable(true)");
    CHECK(!es.roadblocksAmbientTrafficEnabled());
    CHECK(es.roadblocksAmbientTrafficNoSessionSkipCount() == 1);
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    ok(host, gp, "roadblocks_ambient_traffic_enable(true)");
    CHECK(es.roadblocksAmbientTrafficEnabled()); // structural coverage of the host-write branch

    // Sec22.8 party_set_dismissable: bare global.
    CHECK(!es.party().dismissable);
    ok(host, gp, "party_set_dismissable(true)");
    CHECK(es.party().dismissable);

    // Sec22.9 party_allow_max_followers: default true; PLAYER2 repeats only
    // when a co-op partner is present (already true from above).
    ok(host, gp, "party_allow_max_followers(false)");
    CHECK(es.playerRig().player1AllowMaxFollowers == false);
    CHECK(es.playerRig().player2AllowMaxFollowers == false);
    CHECK(es.playerRig().allowMaxFollowersReplicateCount == 2);
    ok(host, gp, "party_allow_max_followers()"); // nil-gated default true
    CHECK(es.playerRig().player1AllowMaxFollowers == true);

    // Sec22.10 on_projectile_hit: global hook slot.
    ok(host, gp, "on_projectile_hit('OnHit')");
    CHECK(es.namedHookCallback("on_projectile_hit") == "OnHit");
    ok(host, gp, "on_projectile_hit()"); // absent -> clears
    CHECK(es.namedHookCallback("on_projectile_hit").empty());

    // Sec22.11 on_near_crash: per-object hook, OPEN until the target resolves.
    fails(host, gp, "on_near_crash('OnCrash', 'Veh2')", "Veh2");
    es.objectResolves().set("Veh2", true);
    ok(host, gp, "on_near_crash('OnCrash', 'Veh2')");
    CHECK(es.namedHookCallback("on_near_crash:Veh2") == "OnCrash");

    // Sec22.12 npc_dont_drop_weapon_on_death: double-gate, gate passes true
    // in single player by construction.
    es.objectResolves().set("Npc1", true);
    ok(host, gp, "npc_dont_drop_weapon_on_death('Npc1', true)");
    CHECK(es.getOrCreateCharacter("Npc1").npcForceFlags1e00.get(CharacterState::kDontDropWeaponOnDeathBit1e00) != 0);
    ok(host, gp, "npc_dont_drop_weapon_on_death('Npc1', false)");
    CHECK(es.getOrCreateCharacter("Npc1").npcForceFlags1e00.get(CharacterState::kDontDropWeaponOnDeathBit1e00) == 0);

    // Sec22.13 notoriety_spawn_only_air: bare global.
    ok(host, gp, "notoriety_spawn_only_air(true)");
    CHECK(es.notorietySpawnOnlyAir());

    // Sec22.14 notoriety_restricted_zones_enable: default true; disabling
    // genuinely clears any populated slot.
    es.notorietyRestrictedZones().slots[1] = 7;
    ok(host, gp, "notoriety_restricted_zones_enable(false)");
    CHECK(!es.notorietyRestrictedZones().enabled);
    CHECK(es.notorietyRestrictedZones().slots[1] == -1);
    CHECK(es.notorietyRestrictedZones().clearedSlotCount == 1);

    // Sec22.16 is_rappelling: a confirmed double-push return count; an
    // explicit resolve failure goes to the hardened `false` path (counted),
    // not a thrown error.
    es.objectResolves().set("Unknown1", false);
    ok(host, gp, "local a, b = is_rappelling('Unknown1'); assert(a == false and b == false)");
    CHECK(es.isRappellingResolveFailureSafeCount() == 1);
    es.objectResolves().set("Rap1", true);
    ok(host, gp, "local a, b = is_rappelling('Rap1'); assert(a == false and b == false)");
    es.getOrCreateCharacter("Rap1").rappelStateCodeCc8 = 0x12;
    es.getOrCreateCharacter("Rap1").rappelCompanionByteCce = '#';
    ok(host, gp, "local a, b = is_rappelling('Rap1'); assert(a == true and b == true)");

    // Sec22.17 ingame_effect_remove_trigger: default mask 3 clears the
    // cached effect index and notifies.
    es.objectResolves().set("Trig1", true);
    es.setIngameEffectIndex("Trig1", 4);
    ok(host, gp, "ingame_effect_remove_trigger('Trig1')");
    CHECK(es.ingameEffectIndex("Trig1") == -1);
    CHECK(es.ingameEffectRemoveClearedCount() == 1);
    CHECK(es.ingameEffectRemoveNotifyCount() == 1);

    // Sec22.19 get_vehicle_type: reuses VehicleState::flyingType directly,
    // clamped 0-6; unresolved/unknown -> byte-confirmed 0.0.
    es.objectResolves().set("NoSuchVehicle999", false);
    ok(host, gp, "assert(get_vehicle_type('NoSuchVehicle999') == 0.0)");
    es.objectResolves().set("Heli1", true);
    ok(host, gp, "assert(get_vehicle_type('Heli1') == 0.0)"); // flyingType unknown -> fallback
    es.getOrCreateVehicle("Heli1").flyingType.set(9);
    ok(host, gp, "assert(get_vehicle_type('Heli1') == 6.0)"); // clamped

    // Sec22.20 dlc_gis_film_grain_load: default mask 3 sets the latch once
    // and always notifies.
    ok(host, gp, "dlc_gis_film_grain_load()");
    CHECK(es.dlcGisFilmGrainActive());
    CHECK(es.dlcGisFilmGrainLoadLatchSetCount() == 1);
    CHECK(es.dlcGisFilmGrainLoadNotifyCount() == 1);
    ok(host, gp, "dlc_gis_film_grain_load()"); // already set -> latch count unchanged
    CHECK(es.dlcGisFilmGrainLoadLatchSetCount() == 1);
    CHECK(es.dlcGisFilmGrainLoadNotifyCount() == 2);

    // Sec22.21 debris_flow_add_object: only applies to an ALLOCATED flow slot.
    es.objectResolves().set("Debris1", true);
    ok(host, gp, "debris_flow_add_object(0, 'Debris1', 1, 2, 3)");
    CHECK(es.debrisFlow().addObjectNoMatchCount == 1); // slot 0 not yet allocated
    es.debrisFlow().flows[0].allocated = true;
    ok(host, gp, "debris_flow_add_object(0, 'Debris1', 1, 2, 3, true, false)");
    CHECK(es.debrisFlow().flows[0].objects.size() == 1);
    CHECK(es.debrisFlow().addObjectCount == 1);

    // Sec22.22 cower: both character and threat must resolve.
    es.objectResolves().set("Civ1", true);
    es.objectResolves().set("Threat1", true);
    ok(host, gp, "cower('Civ1', 'Threat1', true, false)");
    CHECK(es.getOrCreateCharacter("Civ1").cowering);
    CHECK(es.getOrCreateCharacter("Civ1").cowerThreatName == "Threat1");
    CHECK(es.getOrCreateCharacter("Civ1").cowerArg3 == true);

    // Sec22.23 continuous_spawn_set_orders: resolves via the guardian-angel
    // group table (a plain, never-throwing accessor - an unregistered name
    // is a real, silent no-op, not an OPEN refusal), matches the order-type
    // name against the fixed 4-entry table (else sentinel 4).
    ok(host, gp, "continuous_spawn_set_orders('Grp1', 'attack', '', 10.0)"); // not yet registered -> silent no-op
    CHECK(es.getOrCreateContinuousSpawnOrder("Grp1").setCount == 0);
    es.setGuardianAngelGroupRegistered("Grp1", true);
    ok(host, gp, "continuous_spawn_set_orders('Grp1', 'attack', '', 10.0)");
    CHECK(es.getOrCreateContinuousSpawnOrder("Grp1").orderTypeIndex == 0);
    CHECK(es.getOrCreateContinuousSpawnOrder("Grp1").radius == 10.0);
    ok(host, gp, "continuous_spawn_set_orders('Grp1', 'unknown_order', '', 5.0)");
    CHECK(es.getOrCreateContinuousSpawnOrder("Grp1").orderTypeIndex == 4);

    // Sec22.24 continuous_explosion_stop: genuinely zero-argument-sensitive;
    // always clears the one global running flag.
    es.continuousExplosion().active.set(true);
    ok(host, gp, "continuous_explosion_stop(1, 2, 3)"); // extra args, all ignored
    CHECK(es.continuousExplosion().active.get() == false);
    CHECK(es.continuousExplosion().stopCount == 1);

    // Sec22.25 check_animation_state: reuses the shared name->id table.
    es.objectResolves().set("Anim1", true);
    es.registerAnimationStateIdForTesting("idle", 5);
    ok(host, gp, "assert(check_animation_state('Anim1', 'idle') == false)");
    es.getOrCreateCharacter("Anim1").currentAnimationStateId = 5;
    ok(host, gp, "assert(check_animation_state('Anim1', 'idle') == true)");
    es.objectResolves().set("NoSuchCharacterXYZ", false);
    ok(host, gp, "assert(check_animation_state('NoSuchCharacterXYZ', 'idle') == false)");

    // Sec22.26 character_release_human_shield: forced-eject falls through to
    // also run normal release; delayed-release returns immediately.
    es.objectResolves().set("Taker1", true);
    es.getOrCreateCharacter("Taker1").humanShieldHostageName = "Hostage1";
    ok(host, gp, "character_release_human_shield('Taker1')"); // normal release
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldHostageName.empty());
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldNormalReleaseCount == 1);
    es.getOrCreateCharacter("Taker1").humanShieldHostageName = "Hostage2";
    ok(host, gp, "character_release_human_shield('Taker1', true)"); // forced
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldForcedEjectCount == 1);
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldNormalReleaseCount == 2);
    es.getOrCreateCharacter("Taker1").humanShieldHostageName = "Hostage3";
    ok(host, gp, "character_release_human_shield('Taker1', false, true)"); // delayed
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldDelayedReleaseCount == 1);
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldHostageName == "Hostage3"); // NOT cleared yet

    // Sec22.27 boss_battle_kb_enable_crowd_props.
    ok(host, gp, "boss_battle_kb_enable_crowd_props(true, 3.0)");
    CHECK(es.bossBattleKbCrowdProps().enabled);
    CHECK(es.bossBattleKbCrowdProps().density == 3.0);
    ok(host, gp, "boss_battle_kb_enable_crowd_props(false)");
    CHECK(!es.bossBattleKbCrowdProps().enabled);
    CHECK(es.bossBattleKbCrowdProps().disableCount == 1);

    // Sec22.28 base_jumping_enable: default true, always replicates.
    ok(host, gp, "base_jumping_enable()");
    CHECK(es.baseJumpingEnabled());
    CHECK(es.baseJumpingReplicateCount() == 1);

    // Sec22.30 wind_override_set: CONFIRMED exact normalize math.
    ok(host, gp, "wind_override_set(3.0, 4.0, 0.0, 10.0)");
    CHECK(std::abs(es.windOverride().x - 0.6) < 1e-9);
    CHECK(std::abs(es.windOverride().y - 0.8) < 1e-9);
    CHECK(es.windOverride().magnitude == 10.0);
    ok(host, gp, "wind_override_set(0.0, 0.0, 0.0, 5.0)"); // degenerate -> fixed (1,0,0) fallback
    CHECK(es.windOverride().x == 1.0);
    CHECK(es.windOverride().y == 0.0);
    CHECK(es.windOverride().setNotifyCount == 0); // actionStopSyncedStateFlag defaults false
    es.actionStopSyncedStateFlag() = true;
    ok(host, gp, "wind_override_set(1.0, 0.0, 0.0, 1.0)");
    CHECK(es.windOverride().setNotifyCount == 1);

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_wave2_w24a_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_wave2_w24a_test: " << g_failures << " check(s) failed\n";
    return 1;
}
