// Synthetic tests for the Section 6 batch (2026-10-07, full-document spec
// sweep): set_ped_density/set_traffic_density (spec-lua-api-behaviour.md
// Sec6.9/Sec6.14), notoriety_set_can_decay (Sec6.18), waypoint_remove
// (Sec6.17), spawning_pedestrians/spawning_vehicles (Sec6.13/Sec6.15),
// traffic_disable_drawbridge_area (Sec6.4), guardian_angel_register_
// script_group/guardian_angel_unregister_script_group (Sec6.8/Sec6.6).
// Same fixture convention as tests/synthetic_luahost_native_batch1007_test.cpp;
// all 9 names are "gameplay" in tools/lua_all_registered_1490_tagged.txt
// (grepped directly, not assumed).
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
    auto r = host.runChunk(L, code, "sec6_sweep_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code`, expecting a Lua error whose message contains `needle`.
void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sec6_sweep_test.lua");
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
        {"set_ped_density", "gameplay"},
        {"set_traffic_density", "gameplay"},
        {"notoriety_set_can_decay", "gameplay"},
        {"waypoint_remove", "gameplay"},
        {"spawning_pedestrians", "gameplay"},
        {"spawning_vehicles", "gameplay"},
        {"traffic_disable_drawbridge_area", "gameplay"},
        {"guardian_angel_register_script_group", "gameplay"},
        {"guardian_angel_unregister_script_group", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay only, all 9.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    // set_ped_density / set_traffic_density: direct global-float writes,
    // no gate, no return value.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "set_ped_density(0.5)");
        CHECK(es.pedDensity() == 0.5f);
        ok(host, gp, "set_traffic_density(0.25)");
        CHECK(es.trafficDensity() == 0.25f);
    }

    // notoriety_set_can_decay: CONFIRMED inverted polarity - true clears
    // the "suppressed" flag, false sets it.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "notoriety_set_can_decay(false)");
        CHECK(es.notorietyDecaySuppressed() == true);
        ok(host, gp, "notoriety_set_can_decay(true)");
        CHECK(es.notorietyDecaySuppressed() == false);
    }

    // waypoint_remove: no Lua-visible return; records call count + last
    // mode, default mode 3 when the argument is omitted.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.waypointRemoveCount() == 0);
        ok(host, gp, "waypoint_remove()");
        CHECK(es.waypointRemoveCount() == 1);
        CHECK(es.lastWaypointRemoveMode() == 3);
        ok(host, gp, "waypoint_remove(1)");
        CHECK(es.waypointRemoveCount() == 2);
        CHECK(es.lastWaypointRemoveMode() == 1);
    }

    // spawning_pedestrians / spawning_vehicles: master on/off toggle,
    // CHOSEN default true (see EngineState's own doc comment).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.pedestrianSpawningEnabled() == true);
        CHECK(es.vehicleSpawningEnabled() == true);
        ok(host, gp, "spawning_pedestrians(false)");
        CHECK(es.pedestrianSpawningEnabled() == false);
        ok(host, gp, "spawning_pedestrians(true, false)"); // arg2 consumed, not modelled
        CHECK(es.pedestrianSpawningEnabled() == true);
        ok(host, gp, "spawning_vehicles(false)");
        CHECK(es.vehicleSpawningEnabled() == false);
        ok(host, gp, "spawning_vehicles(true)");
        CHECK(es.vehicleSpawningEnabled() == true);
    }

    // traffic_disable_drawbridge_area: resolve-gated (shared objectResolves()),
    // builds the fixed "Spline_Disabler<NNN>" name, toggles bit (area-1).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "traffic_disable_drawbridge_area(1, true)", "named-object resolution");
        es.objectResolves().set("Spline_Disabler<001>", false);
        ok(host, gp, "traffic_disable_drawbridge_area(1, true)");
        CHECK(es.drawbridgeDisabledMask() == 0); // known-not-resolved: no bit set
        es.objectResolves().set("Spline_Disabler<003>", true);
        ok(host, gp, "traffic_disable_drawbridge_area(3, true)");
        CHECK((es.drawbridgeDisabledMask() & (1u << 2)) != 0); // bit (3-1)=2
        ok(host, gp, "traffic_disable_drawbridge_area(3, false)");
        CHECK((es.drawbridgeDisabledMask() & (1u << 2)) == 0);
    }

    // guardian_angel_register_script_group / _unregister_script_group:
    // resolve-gated, records a per-name registered/unregistered boolean.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "guardian_angel_register_script_group('NoGroup')", "named-object resolution");
        es.objectResolves().set("KnownMissingGaGroup", false);
        ok(host, gp, "guardian_angel_register_script_group('KnownMissingGaGroup')");
        CHECK(!es.guardianAngelGroupKnown("KnownMissingGaGroup"));
        es.objectResolves().set("GaGroup1", true);
        ok(host, gp, "guardian_angel_register_script_group('GaGroup1')");
        CHECK(es.guardianAngelGroupKnown("GaGroup1") && es.guardianAngelGroupRegistered("GaGroup1") == true);
        ok(host, gp, "guardian_angel_unregister_script_group('GaGroup1')");
        CHECK(es.guardianAngelGroupRegistered("GaGroup1") == false);
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_sec6_sweep_test: all checks passed\n";
    return 0;
}
