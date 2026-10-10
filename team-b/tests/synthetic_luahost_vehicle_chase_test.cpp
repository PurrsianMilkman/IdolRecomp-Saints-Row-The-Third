// Synthetic tests for vehicle_chase (spec-lua-api-behaviour.md Sec9.4,
// 0x00a65ac0), dedicated pass 2026-10-07: this document's own single
// highest real call-volume CONFIRMED-but-unimplemented native (72 static
// call sites) at the time this was written, deliberately left out of the
// Section 9 full-document-sweep batch (tests/synthetic_luahost_sec9_sweep_
// test.cpp) as "MEDIUM complexity, not a data gap". Same fixture
// convention as that file / tests/synthetic_luahost_native_batch1007_test.cpp;
// "gameplay" per tools/lua_all_registered_1490_tagged.txt (grepped
// directly, not assumed).
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
    auto r = host.runChunk(L, code, "vehicle_chase_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "vehicle_chase_test.lua");
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
        {"vehicle_chase", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay only (CONFIRMED tag).
    {
        Host host(fixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "vehicle_chase"));
        CHECK(!hasGlobal(host.uiState(), "vehicle_chase"));
    }

    // Arg 1 unresolved (never .set() at all) -> refuses at the shared
    // named-object resolution gate, same convention as every other
    // vehicle-name native in this project.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        refuses(host, gp, "vehicle_chase('NoCar')", "named-object resolution");
    }

    // First call on a resolved, never-chased vehicle: takes the "not yet
    // chasing" init path (CONFIRMED) - no return value, and the resolved
    // chase-target name (default "#PLAYER1#") plus the 4 boolean defaults
    // (true/false/true/false, CONFIRMED) are stored. aiDriveModeIsChasing
    // flips true (this project's own judgment call - see VehicleState's own
    // doc comment) so a SECOND call can reach the "already chasing" path.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car1", true);
        CHECK(es.getOrCreateVehicle("Car1").aiDriveModeIsChasing == false); // CHOSEN default, pre-call
        ok(host, gp, "vehicle_chase('Car1')");
        VehicleState& v = es.getOrCreateVehicle("Car1");
        CHECK(v.aiDriveModeIsChasing == true);
        CHECK(v.chaseTargetName == "#PLAYER1#"); // CONFIRMED literal default
        CHECK(v.chaseFlagArg3 == true);
        CHECK(v.chaseFlagArg4 == false);
        CHECK(v.chaseFlagArg5 == true);
        CHECK(v.chaseFlagArg6 == false);
    }

    // Arg 2 (chase-target name) explicit, non-default - stored verbatim,
    // no gating on whether it independently resolves (CONFIRMED: the spec
    // states no failure branch for this specific resolve at this call
    // depth).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car2", true);
        ok(host, gp, "vehicle_chase('Car2', 'Target1')");
        VehicleState& v = es.getOrCreateVehicle("Car2");
        CHECK(v.chaseTargetName == "Target1");
    }

    // All 4 optional booleans given explicitly (non-default values) -
    // stored BY ARGUMENT POSITION (the packed flag byte's own bit-to-
    // argument mapping is OPEN per spec, so this project never claims a
    // bit position - only the raw per-argument values are tracked).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car3", true);
        ok(host, gp, "vehicle_chase('Car3', 'Target2', false, true, false, true)");
        VehicleState& v = es.getOrCreateVehicle("Car3");
        CHECK(v.chaseFlagArg3 == false);
        CHECK(v.chaseFlagArg4 == true);
        CHECK(v.chaseFlagArg5 == false);
        CHECK(v.chaseFlagArg6 == true);
    }

    // Second call on an ALREADY-chasing vehicle: takes the "already
    // chasing" record-and-replicate path (CONFIRMED, opcode 0x42) - still
    // no return value, the target name/flags are still (re)stored (both
    // paths write them, CONFIRMED), and aiDriveModeIsChasing stays true.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car4", true);
        ok(host, gp, "vehicle_chase('Car4')"); // path 1: init, flips chasing true
        VehicleState& v = es.getOrCreateVehicle("Car4");
        CHECK(v.aiDriveModeIsChasing == true);
        ok(host, gp, "vehicle_chase('Car4', 'Target3')"); // path 2: already chasing, update
        CHECK(v.aiDriveModeIsChasing == true); // stays true
        CHECK(v.chaseTargetName == "Target3"); // re-stored on the update path too
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_vehicle_chase_test: all checks passed\n";
    return 0;
}
