// Synthetic tests for wave 2, Batch W2-3 (2026-10-08): spec-lua-api-
// behaviour.md Sec20 ("ranks 351-450 tranche, part C", 29 of its 32 names)
// + Sec21 ("ranks 351-450 tranche, part A", 28 of its 30 names), 57 names
// total. Same fixture convention as tests/synthetic_luahost_sec13_14_
// batch4_test.cpp; all 57 are "gameplay" in
// tools/lua_all_registered_1490_tagged.txt (grepped directly, not assumed).
//
// Not every one of the 57 gets its own dedicated check below - this file
// focuses on: registration of all 57; the TRANSPOSITION-HAZARD cross-check
// between character_set_counter_on_grabbed_by_player (this batch) and the
// ALREADY-REGISTERED character_set_counter_on_grabbed (Sec28.19); the
// team_make_allies hardening parity with team_make_hostile/unfriendly; the
// mission_is_complete/cell_is_mission_complete shared-state reuse; the two
// HARDENED memory-safety divergences (dlc_achievement_counter_get negative
// index, hud_timer_get_remainder OOB index); and a representative sample
// of the simpler flag-setter/query natives.
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
    auto r = host.runChunk(L, code, "wave2_w23_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w23_test.lua");
    if (!r.loadOk || r.pcallOk || r.pcallError.find(needle) == std::string::npos) {
        std::cerr << "expected refusal containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("success") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<std::string> kNames = {
    "vehicle_repair", "vehicle_car_alarm_enable", "tutorial_vehicle_controls_enabled", "tutorial_active",
    "team_make_allies", "skydive_set_catching_shaundi", "reticle_override_set", "player_warp_to_shore_enable",
    "persona_suppress_ambient_lines", "on_pickup", "mission_set_enemy_team", "mission_is_complete",
    "light_group_use_intensity", "inv_item_is_equipped", "inv_item_get_equipped_item", "inv_has_item",
    "interior_disable_exterior", "human_skydive_create_npc_backpack", "human_enter_downed",
    "hud_timer_get_remainder", "get_team", "get_object_pos", "dlc_achievement_counter_get",
    "character_set_only_scripted_grabs", "character_set_counter_on_grabbed_by_player", "character_is_jumping",
    "character_ignite", "character_has_human_shield", "character_disable_human_downed_interactions",
    "on_collision", "npc_use_closest_action_node_of_type", "notoriety_set_override_timer",
    "inv_item_remove_in_slot", "ingame_effect_add_trigger", "hud_push_screen",
    "continuous_explosion_avoid_object_add", "character_set_only_player_grabs", "character_disable_revival",
    "vehicle_set_allow_ram_ped", "vehicle_anim_finished", "trigger_get_radius",
    "spawning_allow_cop_ambient_spawns", "set_script_animation_group", "set_force_combat_ready_team",
    "player_grenades_disable", "on_weapon_equip", "on_door_opened", "npc_refire_rate_override",
    "npc_hearing_enable", "inv_item_equip_slot", "get_char_vehicle_type", "drug_effect_clear_override_values",
    "character_is_in_a_driver_seat", "change_mission_enemy_team", "action_stop_custom",
    "vehicle_skydiving_move_to_do", "vehicle_prevent_vehicle_collision_damage",
};

// The already-registered sibling this batch must NOT collide with
// (character_set_counter_on_grabbed, Sec28.19) - registered here too so
// the cross-check test below can call BOTH names in the same Host.
std::vector<std::string> kSiblingNames = {
    "character_set_counter_on_grabbed",
};

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> out;
    out.reserve(kNames.size() + kSiblingNames.size());
    for (const auto& n : kNames) out.push_back({n, "gameplay"});
    for (const auto& n : kSiblingNames) out.push_back({n, "gameplay"});
    return out;
}

} // namespace

int main() {
    // Registration: all 57, gameplay only.
    {
        Host host(fixtureNames());
        CHECK(kNames.size() == 57);
        for (const auto& n : kNames) {
            CHECK(hasGlobal(host.gameplayState(), n.c_str()));
            CHECK(!hasGlobal(host.uiState(), n.c_str()));
        }
    }

    // TRANSPOSITION HAZARD cross-check (the orchestrator's own highest-
    // flagged risk): character_set_counter_on_grabbed_by_player (Sec20.27,
    // THIS batch, forceFlags1c98 bit 0x4000000) must NEVER touch
    // counterOnGrabbed().value[name], the SAME field the ALREADY-
    // REGISTERED character_set_counter_on_grabbed (Sec28.19) writes - and
    // vice versa.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Grab1", true);
        CharacterState& c = es.getOrCreateCharacter("Grab1");

        ok(host, gp, "character_set_counter_on_grabbed_by_player('Grab1', true)");
        CHECK((c.forceFlags1c98.get(CharacterState::kCounterOnGrabbedByPlayerBit1c98) != 0));
        CHECK(!es.counterOnGrabbed().value.count("Grab1")); // the OTHER native's own storage, untouched

        ok(host, gp, "character_set_counter_on_grabbed('Grab1', true)");
        CHECK(es.counterOnGrabbed().value.at("Grab1") == true);
        // The bit this batch's own native writes must be unaffected by the sibling call.
        CHECK((c.forceFlags1c98.get(CharacterState::kCounterOnGrabbedByPlayerBit1c98) != 0));

        ok(host, gp, "character_set_counter_on_grabbed_by_player('Grab1', false)");
        CHECK((c.forceFlags1c98.get(CharacterState::kCounterOnGrabbedByPlayerBit1c98) == 0));
        // The sibling's own value must be untouched by clearing THIS bit.
        CHECK(es.counterOnGrabbed().value.at("Grab1") == true);
    }

    // character_set_only_scripted_grabs / character_set_only_player_grabs:
    // sibling bits on the SAME +0x1c98 word, independently settable.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Grab2", true);
        CharacterState& c = es.getOrCreateCharacter("Grab2");
        ok(host, gp, "character_set_only_scripted_grabs('Grab2')"); // default true
        CHECK((c.forceFlags1c98.get(CharacterState::kOnlyScriptedGrabsBit1c98) != 0));
        ok(host, gp, "character_set_only_player_grabs('Grab2')"); // default true
        CHECK((c.forceFlags1c98.get(CharacterState::kOnlyPlayerGrabsBit1c98) != 0));
        CHECK((c.forceFlags1c98.get(CharacterState::kOnlyScriptedGrabsBit1c98) != 0)); // still set
    }

    // character_disable_revival / character_disable_human_downed_interactions:
    // sibling bits on the SAME +0x1c9c word.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Grab3", true);
        CharacterState& c = es.getOrCreateCharacter("Grab3");
        ok(host, gp, "character_disable_revival('Grab3')"); // default true
        CHECK((c.forceFlags1c9c.get(CharacterState::kRevivalDisabledBit1c9c) != 0));
        ok(host, gp, "character_disable_human_downed_interactions('Grab3')"); // default true
        CHECK((c.forceFlags1c9c.get(CharacterState::kHumanDownedInteractDisabledBit1c9c) != 0));
        CHECK((c.forceFlags1c9c.get(CharacterState::kRevivalDisabledBit1c9c) != 0)); // still set
    }

    // team_make_allies (Sec20.6): value 4, symmetric, team-5-mirrors-6
    // (reuses the SAME setTeamRelationIfInBounds() infra), plus the SAME
    // OOB hardening pattern team_make_hostile/team_make_unfriendly use.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.registerTeamIdForTesting("teamA", 1);
        es.registerTeamIdForTesting("teamB", 2);
        ok(host, gp, "team_make_allies('teamA', 'teamB')");
        CHECK(es.teamRelationForTesting(1, 2) == 4);
        CHECK(es.teamRelationForTesting(2, 1) == 4);

        uint64_t before = es.teamRelationOobGuardCount();
        es.registerTeamIdForTesting("teamUnknown", 99); // out of [0,8) bounds
        ok(host, gp, "team_make_allies('teamUnknown', 'teamA')");
        CHECK(es.teamRelationOobGuardCount() == before + 1); // HARDENED, no OOB write

        // team 5 mirrors team 6 (CONFIRMED refinement already established by team_make_unfriendly/Sec30.6).
        es.registerTeamIdForTesting("team5", 5);
        es.registerTeamIdForTesting("teamX", 3);
        ok(host, gp, "team_make_allies('team5', 'teamX')");
        CHECK(es.teamRelationForTesting(6, 3) == 4);
        CHECK(es.teamRelationForTesting(3, 6) == 4);
    }

    // mission_is_complete (Sec20.14): SAME shared state cell_is_mission_
    // complete (Sec28.19, already registered elsewhere) reads - this test
    // only wires THIS name (cell_is_mission_complete is not in this
    // fixture), but the underlying map is the same production EngineState
    // accessor either stub would read.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "mission_is_complete('NoMission')", "named-object resolution");
        es.objectResolves().set("M1", true);
        es.missionComplete().set("M1", true);
        lua_getglobal(gp, "mission_is_complete");
        lua_pushstring(gp, "M1");
        lua_call(gp, 1, 1);
        CHECK(lua_toboolean(gp, -1) != 0);
        lua_pop(gp, 1);
    }

    // dlc_achievement_counter_get (Sec20.25): in-range reads the SAME array
    // dlc_achievement_counter_set writes; index >= 3 pushes nothing
    // (CONFIRMED, not a divergence); negative index is HARDENED (refused).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.setDlcAchievementCounter(1, 42);
        lua_getglobal(gp, "dlc_achievement_counter_get");
        lua_pushnumber(gp, 1);
        lua_call(gp, 1, LUA_MULTRET);
        CHECK(lua_gettop(gp) == 1);
        CHECK(lua_tonumber(gp, -1) == 42.0);
        lua_settop(gp, 0);

        lua_getglobal(gp, "dlc_achievement_counter_get");
        lua_pushnumber(gp, 3);
        lua_call(gp, 1, LUA_MULTRET);
        CHECK(lua_gettop(gp) == 0); // CONFIRMED: pushes nothing for index >= 3
        lua_settop(gp, 0);

        uint64_t before = es.dlcAchievementCounterGetNegativeIndexGuardCount();
        lua_getglobal(gp, "dlc_achievement_counter_get");
        lua_pushnumber(gp, -5);
        lua_call(gp, 1, LUA_MULTRET);
        CHECK(lua_gettop(gp) == 0); // HARDENED: refused, no OOB read, no value pushed
        lua_settop(gp, 0);
        CHECK(es.dlcAchievementCounterGetNegativeIndexGuardCount() == before + 1);
    }

    // hud_timer_get_remainder (Sec20.22): in-range reads remainingTicks
    // (with the CONFIRMED unsigned-wraparound correction for a negative
    // value); any OOB index (negative OR >= 4) is HARDENED (refused, 0.0).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.hudTimerSlot(1)->remainingTicks = 5000;
        lua_getglobal(gp, "hud_timer_get_remainder");
        lua_pushnumber(gp, 1);
        lua_call(gp, 1, 1);
        CHECK(lua_tonumber(gp, -1) == 5000.0);
        lua_pop(gp, 1);

        es.hudTimerSlot(2)->remainingTicks = -1;
        lua_getglobal(gp, "hud_timer_get_remainder");
        lua_pushnumber(gp, 2);
        lua_call(gp, 1, 1);
        CHECK(lua_tonumber(gp, -1) == 4294967295.0); // -1 -> wraparound correction
        lua_pop(gp, 1);

        uint64_t before = es.hudHardeningCounters().timerGetRemainderOobCount;
        lua_getglobal(gp, "hud_timer_get_remainder");
        lua_pushnumber(gp, 99);
        lua_call(gp, 1, 1);
        CHECK(lua_tonumber(gp, -1) == 0.0); // HARDENED
        lua_pop(gp, 1);
        CHECK(es.hudHardeningCounters().timerGetRemainderOobCount == before + 1);
    }

    // inv_item_is_equipped / inv_item_get_equipped_item (Sec20.16/20.17):
    // shared CharacterState::equippedItemName, CONFIRMED inverted polarity
    // when no filter is given.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("#PLAYER1#", true);
        CharacterState& c = es.getOrCreateCharacter("#PLAYER1#");

        lua_getglobal(gp, "inv_item_is_equipped");
        lua_call(gp, 0, 1);
        CHECK(lua_toboolean(gp, -1) != 0); // nothing equipped -> true (inverted polarity)
        lua_pop(gp, 1);

        c.equippedItemName = "Pistol";
        lua_getglobal(gp, "inv_item_is_equipped");
        lua_call(gp, 0, 1);
        CHECK(lua_toboolean(gp, -1) == 0); // something equipped -> false
        lua_pop(gp, 1);

        lua_getglobal(gp, "inv_item_is_equipped");
        lua_pushstring(gp, "Pistol");
        lua_call(gp, 1, 1);
        CHECK(lua_toboolean(gp, -1) != 0); // matches filter -> true
        lua_pop(gp, 1);

        lua_getglobal(gp, "inv_item_get_equipped_item");
        lua_pushstring(gp, "#PLAYER1#");
        lua_call(gp, 1, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "Pistol");
        lua_pop(gp, 1);
    }

    // vehicle_car_alarm_enable (Sec20.3): capability gate.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car1", true);
        VehicleState& v = es.getOrCreateVehicle("Car1");
        v.alarmDisabledByDesign.set(false);
        ok(host, gp, "vehicle_car_alarm_enable('Car1', true)");
        CHECK((v.statusByte163d.get(VehicleState::kCarAlarmEnabledBit163d) != 0));
        v.alarmDisabledByDesign.set(true);
        ok(host, gp, "vehicle_car_alarm_enable('Car1', false)");
        CHECK((v.statusByte163d.get(VehicleState::kCarAlarmEnabledBit163d) != 0)); // unchanged, gated off

        // vehicle_prevent_vehicle_collision_damage (Sec21.30): SAME +0x1d7a
        // byte turn_invulnerable/vehicle_set_vulnerable already model, a
        // CONFIRMED distinct bit (0x8).
        ok(host, gp, "vehicle_prevent_vehicle_collision_damage('Car1', true)");
        CHECK((v.forceFlags1d7a.get(VehicleState::kImmuneToVehicleCollisionDamageBit1d7a) != 0));
        CHECK((v.forceFlags1d7a.known(VehicleState::kInvulnerableBit1d7a)
                   ? v.forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) == 0
                   : true)); // untouched sibling bit

        // vehicle_set_allow_ram_ped (Sec21.10): a SECOND bit on the SAME
        // +0xc68+5 byte vehicle_set_no_chase already models.
        ok(host, gp, "vehicle_set_allow_ram_ped('Car1', true)");
        CHECK((v.vehicleAiForceFlags.get(VehicleState::kAllowRamPedBit) != 0));
    }

    // vehicle_repair (Sec20.2): positive-fraction gate, HP repair math.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car2", true);
        VehicleState& v = es.getOrCreateVehicle("Car2");
        v.maxHitPointsV = 100;
        v.currentHitPointsV = 20;
        ok(host, gp, "vehicle_repair('Car2', 0.5)"); // +50
        CHECK(v.currentHitPointsV == 70);
        ok(host, gp, "vehicle_repair('Car2', 1.0)"); // +100, clamped to max
        CHECK(v.currentHitPointsV == 100);
        int32_t before = v.currentHitPointsV;
        ok(host, gp, "vehicle_repair('Car2', 0)"); // CONFIRMED: only proceeds if positive
        CHECK(v.currentHitPointsV == before);
    }

    // inv_item_remove_in_slot / inv_item_equip_slot (Sec21.4/Sec21.23):
    // slot-keyed inventory, distinct from the shared equippedItemName
    // accessor and from any OTHER batch's own unslotted `inv_item_remove`.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("#PLAYER1#", true);
        CharacterState& c = es.getOrCreateCharacter("#PLAYER1#");
        es.registerInventorySlotIdForTesting("slot1", 1);
        c.inventorySlotContentsBySlotId[1] = "Shotgun";
        ok(host, gp, "inv_item_equip_slot('slot1')");
        CHECK(c.equippedItemName == "Shotgun");
        CHECK(c.equippedCount == 1);
        ok(host, gp, "inv_item_equip_slot('slot1')"); // already equipped -> skip, not re-equip
        CHECK(c.equippedCount == 1);
        CHECK(c.reequipSkippedAlreadyEquippedCount == 1);

        ok(host, gp, "inv_item_remove_in_slot('slot1')");
        CHECK(c.inventorySlotContentsBySlotId[1].empty());
        CHECK(c.equippedItemName.empty()); // removing the equipped item's own slot clears it too
    }

    // get_object_pos (Sec20.24): OPEN trailing constant blocks even the
    // failure path until a test sets it; once set, both success/failure
    // paths read the SAME value.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "return get_object_pos('Anything')", "0x0112a1e0");
        es.objectPosTrailingConstant.set(7.0);
        es.objectResolves().set("NoObj", false); // explicitly known-unresolved, not OPEN
        lua_getglobal(gp, "get_object_pos");
        lua_pushstring(gp, "NoObj");
        lua_call(gp, 1, LUA_MULTRET);
        CHECK(lua_gettop(gp) == 3); // failure -> 3 values
        CHECK(lua_tonumber(gp, -1) == 7.0);
        lua_settop(gp, 0);

        es.objectResolves().set("ObjP", true);
        es.getOrCreateCharacter("ObjP").worldPosition.set(WorldPosition{1.0, 2.0, 3.0});
        lua_getglobal(gp, "get_object_pos");
        lua_pushstring(gp, "ObjP");
        lua_call(gp, 1, LUA_MULTRET);
        CHECK(lua_gettop(gp) == 6); // success -> 6 values
        CHECK(lua_tonumber(gp, 1) == 1.0);
        CHECK(lua_tonumber(gp, 2) == 2.0);
        CHECK(lua_tonumber(gp, 3) == 3.0);
        CHECK(lua_tonumber(gp, 4) == 7.0);
        lua_settop(gp, 0);
    }

    // get_team (Sec20.23): pure query, OPEN per-name team.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Unresolved", false); // explicitly known-unresolved, not OPEN
        lua_getglobal(gp, "get_team");
        lua_pushstring(gp, "Unresolved");
        lua_call(gp, 1, 1);
        CHECK(lua_isnil(gp, -1));
        lua_pop(gp, 1);

        es.objectResolves().set("Teamed", true);
        es.teamNameByObject().set("Teamed", "Saints");
        lua_getglobal(gp, "get_team");
        lua_pushstring(gp, "Teamed");
        lua_call(gp, 1, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "Saints");
        lua_pop(gp, 1);
    }

    // on_collision / on_weapon_equip / on_door_opened / on_pickup (Sec21.1/
    // Sec21.18/Sec21.19/Sec20.12): hook-slot registration.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Hooked", true);
        ok(host, gp, "on_collision('cbCollide', 'Hooked')");
        CHECK(es.getOrCreateCharacter("Hooked").onCollisionCallback == "cbCollide");
        ok(host, gp, "on_weapon_equip('cbEquip', 'Hooked')");
        CHECK(es.getOrCreateCharacter("Hooked").onWeaponEquipCallback == "cbEquip");
        ok(host, gp, "on_door_opened('cbDoor', 'Hooked')");
        CHECK(es.getOrCreatePlacedObject("Hooked").onDoorOpenedCallback == "cbDoor");
        ok(host, gp, "on_pickup('cbPickup', 'Hooked')");
        CHECK(es.getOrCreatePlacedObject("Hooked").onPickupCallback == "cbPickup");
    }

    // notoriety_set_override_timer (Sec21.3) / set_force_combat_ready_team
    // (Sec21.16) / change_mission_enemy_team (Sec21.27): resolver/gate
    // reuse spot-checks.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.registerNotorietyTrackCodeForTesting("police", 0);
        ok(host, gp, "notoriety_set_override_timer('police', true, 1, 10)");
        CHECK(es.notorietyOverrideTimers().at("police").enabled);
        CHECK(es.notorietyOverrideTimers().at("police").maxValue == 10);

        es.registerTeamIdForTesting("cops", 2);
        ok(host, gp, "set_force_combat_ready_team('cops')");
        CHECK(es.forcedCombatReadyTeamId == 2);

        es.registerTeamIdForTesting("gang3", 3);
        es.missionContext().activeAndRunning.set(true);
        es.missionOneShotGateA0.set(false); // CONFIRMED: write proceeds only when this reads 0/false
        ok(host, gp, "change_mission_enemy_team('gang3')");
        CHECK(es.missionEnemyTeamCodeA5 == 3);
        CHECK(es.changeMissionEnemyTeamNotifyCount == 1);
    }

    // get_char_vehicle_type (Sec21.24): RE-WIRED 2026-10-11 - reads
    // VehicleState::flyingType ((+0xbf4)+0x2c), the SAME real xtbl
    // Vehicle_Type class field EngineState::loadZoneObjectsSection()
    // already populates for every real zone-placed vehicle, NOT the
    // separate never-wired vehicleTypeCode placeholder this native used to
    // read (removed). Covers: unresolved occupant -> sentinel 6; resolved
    // occupant with no cached vehicle association -> sentinel 6; resolved
    // occupant in a vehicle whose real xtbl class is known (0-5) -> that
    // exact code; and an out-of-range flyingType value clamping to 6.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Unresolved occupant -> sentinel 6.
        es.objectResolves().set("NoSuchOccupant999", false);
        ok(host, gp, "assert(get_char_vehicle_type('NoSuchOccupant999') == 6)");

        // Resolved occupant, but no cached vehicle association -> sentinel 6.
        es.objectResolves().set("Driver1", true);
        ok(host, gp, "assert(get_char_vehicle_type('Driver1') == 6)");

        // Occupant's cached vehicle has a real xtbl class (e.g. 3 =
        // Helicopter, the SAME code m07's own heli-adjacent flow resolves
        // via the identical flyingType field) -> returned directly.
        es.getOrCreateCharacter("Driver1").currentVehicleName.set("HeliA");
        es.getOrCreateVehicle("HeliA").flyingType.set(3);
        ok(host, gp, "assert(get_char_vehicle_type('Driver1') == 3)");

        // Automobile (class 0) round-trips too, not just the non-zero
        // classes - guards against an accidental "0 looks like OPEN/unset"
        // regression.
        es.getOrCreateCharacter("Driver1").currentVehicleName.set("Car1");
        es.getOrCreateVehicle("Car1").flyingType.set(0);
        ok(host, gp, "assert(get_char_vehicle_type('Driver1') == 0)");

        // Out-of-range class value clamps to the sentinel.
        es.getOrCreateCharacter("Driver1").currentVehicleName.set("Weird1");
        es.getOrCreateVehicle("Weird1").flyingType.set(42);
        ok(host, gp, "assert(get_char_vehicle_type('Driver1') == 6)");
    }

    // drug_effect_clear_override_values (Sec21.25): reuses the SAME
    // channel-0 fields drug_effect_set_override_values (Sec14.19) writes.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.drugEffectOverrideValueA() = 5.0;
        es.drugEffectOverrideValueB() = 9.0;
        ok(host, gp, "drug_effect_clear_override_values()");
        CHECK(es.drugEffectOverrideValueA() == 0.0);
        CHECK(es.drugEffectOverrideValueB() == 0.0);
    }

    if (g_failures == 0) {
        std::cout << "All wave2_w23 checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
