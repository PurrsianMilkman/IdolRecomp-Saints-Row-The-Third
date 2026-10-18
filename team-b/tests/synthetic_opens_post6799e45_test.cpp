// Tests for the OPEN stops the post-6799e45 mission sweep surfaced (thread_new now really runs mission worker
// threads). Each block cites the spec text that answers the value. Real Host, real Lua natives.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

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
    auto r = host.runChunk(L, code, "opens_post6799e45_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "opens_post6799e45_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> v;
    for (const char* n : {"character_exists", "get_team", "move_to_do", "vehicle_is_tank"})
        v.push_back({n, "gameplay"});
    return v;
}

}  // namespace

int main() {
    // ---- m21 '#player1#', m24 'Saint_NPC 007': the by-name registry lookup (0x004588f0) hashes the name
    // lower-cased and compares case-insensitively (spec-lua-api-behaviour.md, gat_captive_nav and
    // 'Vfx Transformer DTH<004>' notes), so a differently-cased spelling is the same object.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "assert(character_exists('#PLAYER1#') == true)");  // pre-seeded literal
        ok(host, gp, "assert(character_exists('#player1#') == true)");  // m21 inv_weapon_remove_temporary spelling
        es.objectResolves().set("Saint_NPC 007", true);
        ok(host, gp, "assert(character_exists('saint_npc 007') == true)");
        ok(host, gp, "assert(character_exists('SAINT_NPC 007') == true)");
        es.objectResolves().set("ghost_Npc", false);
        ok(host, gp, "assert(character_exists('GHOST_NPC') == false)");
        // A name nobody registered never resolves: false, never an error (Sec7.9 / 4.13.0, spec sync 78fb40a).
        ok(host, gp, "assert(character_exists('never_registered') == false)");
    }

    // ---- m22 'NPC_sind_two 001': get_team(script_npc) with an explicit zone script_npc_team returns the name from
    // the id table 0x02623a80 (spec-lua-api-behaviour.md 20.23, 2026-10-15 'where the team id comes from').
    // The zone string is mapped name -> id case-insensitively (0x0094cc60) and id -> canonical name; an unknown
    // name (the one 'Brotherhood' record) leaves id 9 = "None".
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        for (const char* n : {"NPC_sind_two 001", "NPC_sind_two 002", "NPC_sind_two 003", "NPC_sind_two 004"})
            es.objectResolves().set(n, true);
        es.getOrCreateCharacter("NPC_sind_two 001").teamName = "Luchadores";
        es.getOrCreateCharacter("NPC_sind_two 002").teamName = "morningstar";
        es.getOrCreateCharacter("NPC_sind_two 003").teamName = "Brotherhood";
        ok(host, gp, "assert(get_team('NPC_sind_two 001') == 'Luchadores')");
        ok(host, gp, "assert(get_team('NPC_sind_two 002') == 'Morningstar')");
        ok(host, gp, "assert(get_team('NPC_sind_two 003') == 'None')");
        // No authored team and no character-row data: still OPEN.
        fails(host, gp, "get_team('NPC_sind_two 004')", "team-name reverse lookup");
    }

    // ---- m08: move_to_do's 0x00a3bae0(character, false) is character_is_ready's own predicate (spec 7.27 and
    // 'Body: via 0x00a3bae0(object, 0)'). An ordinary standing NPC reads true; a dead one reads false.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("mt_c", true);
        es.objectResolves().set("mt_dest", true);
        CharacterState& mt = es.getOrCreateCharacter("mt_c");
        mt.isAlive.set(true);
        auto r = host.runChunk(gp, "move_to_do('mt_c', 'mt_dest', 1, false, false, false, 0)", "t.lua");
        // Whatever the NEXT open gate is, it is no longer the predicate itself.
        CHECK(r.pcallOk || r.pcallError.find("0x00a3bae0(character, false)") == std::string::npos);
        mt.isAlive.set(false);
        ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 1, false, false, false, 0) == 0)");  // predicate false: fail
    }

    // ---- m13: vehicle_is_tank is '(vehicle+0xbf4)+0x4c4 == 14', the SAME Special_Vehicle_Type field that
    // VehicleState::vehicleClass holds (spec-player-runtime.md 13.4 step 5, spec-vehicle-data.md +0x4C4 table).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("tank1", true);
        es.objectResolves().set("van1", true);
        es.objectResolves().set("raw1", true);
        es.getOrCreateVehicle("tank1").vehicleClass.set(14);
        es.getOrCreateVehicle("van1").vehicleClass.set(5);
        ok(host, gp, "assert(vehicle_is_tank('tank1') == true)");
        ok(host, gp, "assert(vehicle_is_tank('van1') == false)");
        fails(host, gp, "vehicle_is_tank('raw1')", "Sec15.16");  // no class data at all: still OPEN
    }

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "synthetic_opens_post6799e45_test: all checks passed\n";
    return 0;
}
