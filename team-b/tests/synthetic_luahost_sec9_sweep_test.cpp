// Synthetic tests for the Section 9 batch (2026-10-07, full-document spec
// sweep): vehicle_speed_override (Sec9.5), vehicle_infinite_mass (Sec9.8),
// set_unjackable_flag (Sec9.13), vehicle_exists (Sec9.9), teleport_vehicle
// (Sec9.11), effect_play (Sec9.1), effect_stop (Sec9.2),
// inv_weapon_disable_all_slots (Sec9.20). Same fixture convention as
// tests/synthetic_luahost_native_batch1007_test.cpp; all 8 names are
// "gameplay" in tools/lua_all_registered_1490_tagged.txt (grepped directly,
// not assumed).
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
        }                                                                                     \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "sec9_sweep_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sec9_sweep_test.lua");
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

std::vector<RegisteredName> fixtureNames() {
    return {
        {"vehicle_speed_override", "gameplay"},
        {"vehicle_infinite_mass", "gameplay"},
        {"set_unjackable_flag", "gameplay"},
        {"vehicle_exists", "gameplay"},
        {"teleport_vehicle", "gameplay"},
        {"effect_play", "gameplay"},
        {"effect_stop", "gameplay"},
        {"inv_weapon_disable_all_slots", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay only, all 8.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    // vehicle_speed_override: mph->m/s conversion, clamp to 1000.0.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "vehicle_speed_override('NoCar', 60)", "named-object resolution");
        es.objectResolves().set("Car1", true);
        ok(host, gp, "vehicle_speed_override('Car1', 60)");
        VehicleState& v = es.getOrCreateVehicle("Car1");
        CHECK(v.hasSpeedCap == true);
        CHECK(std::fabs(v.speedCapRaw - (60.0 * 0.44703999161720276)) < 1e-9);
        // Clamp: a huge mph value clamps to 1000.0 m/s.
        ok(host, gp, "vehicle_speed_override('Car1', 100000)");
        CHECK(v.speedCapRaw == 1000.0);
    }

    // vehicle_infinite_mass / set_unjackable_flag: 2 bits sharing one byte,
    // direct-apply (forceFlagGatePasses CONFIRMED true in single player).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "vehicle_infinite_mass('NoCar')", "named-object resolution");
        es.objectResolves().set("Car2", true);
        ok(host, gp, "vehicle_infinite_mass('Car2')"); // default true
        VehicleState& v = es.getOrCreateVehicle("Car2");
        CHECK((v.forceFlags1d7b.get(VehicleState::kInfiniteMassBit1d7b) != 0));
        ok(host, gp, "vehicle_infinite_mass('Car2', false)");
        CHECK((v.forceFlags1d7b.get(VehicleState::kInfiniteMassBit1d7b) == 0));

        ok(host, gp, "set_unjackable_flag('Car2')"); // default true
        CHECK((v.forceFlags1d7b.get(VehicleState::kUnjackableBit1d7b) != 0));
        CHECK(v.unjackableSecondarySubObjectFlag == true);
        ok(host, gp, "set_unjackable_flag('Car2', false)");
        CHECK((v.forceFlags1d7b.get(VehicleState::kUnjackableBit1d7b) == 0));
        CHECK(v.unjackableSecondarySubObjectFlag == false);
    }

    // vehicle_exists: pure resolve-only query.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "vehicle_exists('NoCar')", "named-object resolution");
        es.objectResolves().set("KnownMissingCar", false);
        ok(host, gp, "assert(vehicle_exists('KnownMissingCar') == false)");
        es.objectResolves().set("Car3", true);
        ok(host, gp, "assert(vehicle_exists('Car3') == true)");
    }

    // teleport_vehicle: copies destination's worldPosition onto the source,
    // via the SAME CharacterState::worldPosition field get_dist uses.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "teleport_vehicle('Car4', 'Dest1')", "named-object resolution");
        es.objectResolves().set("Car4", true);
        es.objectResolves().set("Dest1", true);
        es.getOrCreateCharacter("Dest1").worldPosition.set(WorldPosition{1.0, 2.0, 3.0});
        ok(host, gp, "teleport_vehicle('Car4', 'Dest1')");
        WorldPosition p = es.getOrCreateCharacter("Car4").worldPosition.get();
        CHECK(p.x == 1.0 && p.y == 2.0 && p.z == 3.0);
    }

    // effect_play: -1.0 sentinel on unresolved; a fresh increasing handle
    // on success.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "effect_play('NoEffect')", "named-object resolution");
        es.objectResolves().set("KnownMissingEffect", false);
        ok(host, gp, "assert(effect_play('KnownMissingEffect') == -1.0)");
        es.objectResolves().set("Fx1", true);
        ok(host, gp, "local h1 = effect_play('Fx1')\n"
                     "local h2 = effect_play('Fx1')\n"
                     "assert(h1 > 0 and h2 == h1 + 1)");
    }

    // effect_stop / inv_weapon_disable_all_slots: argument parsing only,
    // no crash, no return value asserted beyond "the call succeeds".
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "effect_stop(1)");
        ok(host, gp, "effect_stop(1, 2)");
        ok(host, gp, "inv_weapon_disable_all_slots(true)");
        ok(host, gp, "inv_weapon_disable_all_slots(false, 1)");
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_sec9_sweep_test: all checks passed\n";
    return 0;
}
