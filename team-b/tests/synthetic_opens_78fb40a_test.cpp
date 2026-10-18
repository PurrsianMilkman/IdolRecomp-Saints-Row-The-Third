// Tests for the 7 combat-sweep OPENs answered by spec sync 78fb40a (spec-lua-api-behaviour.md, notes dated
// 2026-10-17 "sweep e2eb5b8"). Real Host, real Lua natives.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3zone/zone_objects.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "opens_78fb40a_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> v;
    for (const char* n : {"character_exists", "move_to_do", "dlc_gis_film_grain_unload", "dlc_gis_film_grain_load",
                          "get_char_vehicle_is_in_air", "vehicle_get_driver", "get_char_in_vehicle", "turn_to_do",
                          "vehicle_is_invulnerable"})
        v.push_back({n, "gameplay"});
    return v;
}

}  // namespace

int main() {
    // ---- dlc2_m01 (Sec17.29): "gis_grain" UI doc id is -1 (not loaded) at boot; unload only clears two bytes.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "dlc_gis_film_grain_unload(false)");  // dlc2_m01_cleanup: no load pending, must not be an error
        CHECK(es.filmGrainFadeState() == false);
        CHECK(es.filmGrainLocalApplyCount() == 1);
        CHECK(es.filmGrainReplicateCount() == 1);  // the bit-1 broadcast is independent of the id
        ok(host, gp, "dlc_gis_film_grain_load()");
        ok(host, gp, "dlc_gis_film_grain_unload(true, 1)");  // loaded now: the fade-out is started
        CHECK(es.filmGrainFadeState() == true);
    }

    // ---- m08 (Sec21.22): 0x00993630 returns true for an on-foot human not already within 0.3 m with a route.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("p_c", true);
        es.objectResolves().set("p_dest", true);
        CharacterState& c = es.getOrCreateCharacter("p_c");
        c.isAlive.set(true);
        es.moveToActionIdCounter().set(0);  // not part of this answer; stays OPEN in the live port
        ok(host, gp, "assert(move_to_do('p_c', 'p_dest', 1, false, false, false, 0) > 0)");
        // In a vehicle: false -> move_to_do returns 0.0.
        c.currentVehicleName.set("some_car");
        ok(host, gp, "assert(move_to_do('p_c', 'p_dest', 1, false, false, false, 0) == 0)");
        c.currentVehicleName.set("");
        // Already within 0.3 m (horizontal) of the destination: false.
        c.worldPosition.set(WorldPosition{10.0, 0.0, 10.0});
        es.getOrCreateCharacter("p_dest").worldPosition.set(WorldPosition{10.1, 50.0, 10.1});
        ok(host, gp, "assert(move_to_do('p_c', 'p_dest', 1, false, false, false, 0) == 0)");
        es.getOrCreateCharacter("p_dest").worldPosition.set(WorldPosition{14.0, 0.0, 10.0});
        ok(host, gp, "assert(move_to_do('p_c', 'p_dest', 1, false, false, false, 0) > 0)");
    }

    // ---- m09 (Sec28.8): a flying helicopter is airborne; resting on world ground / water is not.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("pilot", true);
        es.getOrCreateCharacter("pilot").currentVehicleName.set("heli");
        VehicleState& h = es.getOrCreateVehicle("heli");
        h.flyingType.set(3);
        ok(host, gp, "assert(get_char_vehicle_is_in_air('pilot') == true)");
        h.landed = true;
        ok(host, gp, "assert(get_char_vehicle_is_in_air('pilot') == false)");
        h.landed = false;
        h.inWater = true;
        ok(host, gp, "assert(get_char_vehicle_is_in_air('pilot') == false)");
        es.objectResolves().set("walker", true);
        ok(host, gp, "assert(get_char_vehicle_is_in_air('walker') == false)");  // no vehicle -> false
    }

    // ---- m10 (Sec46.1): +0x1d7a is 0 at spawn; bit 0x01 iff the zone's script_vehicle_flags has 'invulnerable'.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        auto addVeh = [&](const std::string& name, const std::string& flags) {
            sr3zone::ObjectRecord rec;
            rec.hasName = true;
            rec.nameText = name;
            rec.classHash = sr3save::nameHash("Script_Vehicle");
            if (!flags.empty()) {
                sr3zone::ObjectPropertyEntry p;
                p.id = sr3zone::ScriptVehicleFlagsPropertyId();
                p.kind = 7;
                p.size = static_cast<uint16_t>(flags.size());
                p.data.assign(flags.begin(), flags.end());
                rec.tailEntries.push_back(p);
            }
            sr3zone::ObjectsSection section;
            section.records.push_back(rec);
            es.loadZoneObjectsSection(section);
        };
        addVeh("veh_oleg_vtol 001", "invulnerable critical hostile");
        addVeh("veh_stag_transport_01", "critical");
        addVeh("veh_plain 001", "");
        es.objectResolves().set("veh_oleg_vtol 001", true);
        es.objectResolves().set("veh_stag_transport_01", true);
        es.objectResolves().set("veh_plain 001", true);
        ok(host, gp, "assert(vehicle_is_invulnerable('veh_oleg_vtol 001') == true)");
        ok(host, gp, "assert(vehicle_is_invulnerable('veh_stag_transport_01') == false)");
        ok(host, gp, "assert(vehicle_is_invulnerable('veh_plain 001') == false)");
        CHECK(es.getOrCreateVehicle("veh_oleg_vtol 001").forceFlags1d7a.get(VehicleState::kAlwaysApplyPlayerDamageBit1d7a) == 0);
        // A script-created vehicle with no zone record: spawn default 0 as well.
        es.objectResolves().set("spawned_car", true);
        ok(host, gp, "assert(vehicle_is_invulnerable('spawned_car') == false)");
    }

    // ---- m13 (Sec12.14): the seat-0 occupant's script name, nil when the seat is empty.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("wave_veh", true);
        VehicleState& v = es.getOrCreateVehicle("wave_veh");
        ok(host, gp, "assert(vehicle_get_driver('wave_veh') == nil)");
        ok(host, gp, "assert(vehicle_get_driver('wave_veh', false) == nil)");
        v.seatOccupants[0] = "Saint_NPC 002";
        ok(host, gp, "assert(vehicle_get_driver('wave_veh') == 'Saint_NPC 002')");
        ok(host, gp, "assert(get_char_in_vehicle('wave_veh', 0) == 'Saint_NPC 002')");
        v.seatOccupants.erase(0);
        ok(host, gp, "assert(vehicle_get_driver('wave_veh') == nil)");
    }

    // ---- m21 (Sec30.6): 0x009433d0 refuses only for a vehicle occupant (modelled); a standing on-foot NPC is not
    // refused, so turn_to_do proceeds past the refusal predicate.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("t_c", true);
        es.objectResolves().set("t_tgt", true);
        CharacterState& c = es.getOrCreateCharacter("t_c");
        c.worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("t_tgt").worldPosition.set(WorldPosition{5, 0, 0});
        auto r = host.runChunk(gp, "return turn_to_do('t_c', 't_tgt', false)", "t.lua");
        CHECK(r.pcallOk || r.pcallError.find("refusal predicate") == std::string::npos);
        c.currentVehicleName.set("some_car");  // occupies a vehicle -> refused -> true
        ok(host, gp, "assert(turn_to_do('t_c', 't_tgt', false) == true)");
    }

    // ---- m24 (Sec7.9): a name no zone places never resolves; character_exists is false, never an error.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "assert(character_exists('Saint_NPC 007') == false)");
    }

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "synthetic_opens_78fb40a_test: all checks passed\n";
    return 0;
}
