// Synthetic tests for character_is_in_vehicle (spec-lua-api-behaviour.md
// Sec3.10, 0x00a45db0), cleared for implementation 2026-10-07 once Team A
// resolved the one remaining OPEN item (the vtable +0x70 call's argument
// shape). Same fixture convention as
// tests/synthetic_luahost_native_batch1007_test.cpp; "gameplay" per
// tools/lua_all_registered_1490_tagged.txt (grepped directly, not assumed).
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
    auto r = host.runChunk(L, code, "character_is_in_vehicle_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code`, expecting a Lua error whose message contains `needle`.
void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "character_is_in_vehicle_test.lua");
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
        {"character_is_in_vehicle", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay only (CONFIRMED tag).
    {
        Host host(fixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "character_is_in_vehicle"));
        CHECK(!hasGlobal(host.uiState(), "character_is_in_vehicle"));
    }

    // Arg 1 unresolved (never .set() at all - the SAME shared named-object
    // resolution gate every other character-name native in this file uses)
    // -> refuses, regardless of arg 2.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        refuses(host, gp, "character_is_in_vehicle('Nobody')", "named-object resolution");
        refuses(host, gp, "character_is_in_vehicle('Nobody', 'AnyCar')", "named-object resolution");
    }

    // Arg 1 KNOWN not to resolve (explicit .set(name,false)) -> false, one
    // return value, no OPEN throw (same convention as every other
    // boolean-returning query in this file).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("KnownMissingCharacter", false);
        ok(host, gp, "local a,b = character_is_in_vehicle('KnownMissingCharacter')\n"
                     "assert(a == false and b == nil)");
    }

    // Only arg 1 given, character resolves, stateEnum != 3 -> false, one
    // return value. No vehicle-pair field is touched (currentVehicleName
    // stays OPEN/unset the whole time - proven by never calling .set() on
    // it here and the call still succeeding).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Alice", true);
        es.getOrCreateCharacter("Alice").stateEnum.set(0); // "not in a vehicle"
        ok(host, gp, "local a,b = character_is_in_vehicle('Alice')\n"
                     "assert(a == false and b == nil)");
    }

    // Only arg 1 given, character resolves, stateEnum == 3 -> true, one
    // return value (+0x16d4 == 3 alone decides it; no vehicle-pair check
    // since no vehicle name was supplied).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Bob", true);
        es.getOrCreateCharacter("Bob").stateEnum.set(3); // "in a vehicle"
        ok(host, gp, "local a,b = character_is_in_vehicle('Bob')\n"
                     "assert(a == true and b == nil)");
    }

    // Both args given, stateEnum == 3, arg 2 resolves and MATCHES the
    // character's own stored vehicle link (currentVehicleName) -> true,
    // one return value.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Carl", true);
        es.getOrCreateCharacter("Carl").stateEnum.set(3);
        es.getOrCreateCharacter("Carl").currentVehicleName.set("Bootlegger1");
        es.objectResolves().set("Bootlegger1", true);
        ok(host, gp, "local a,b = character_is_in_vehicle('Carl', 'Bootlegger1')\n"
                     "assert(a == true and b == nil)");
    }

    // Both args given, stateEnum == 3, arg 2 resolves but does NOT match
    // the character's own stored vehicle link -> false, one return value
    // (CONFIRMED: this is NOT the double-push path - arg 2 resolved fine,
    // it simply didn't match).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Dana", true);
        es.getOrCreateCharacter("Dana").stateEnum.set(3);
        es.getOrCreateCharacter("Dana").currentVehicleName.set("Bootlegger1");
        es.objectResolves().set("Baron1", true); // a DIFFERENT, also-resolving vehicle
        ok(host, gp, "local a,b = character_is_in_vehicle('Dana', 'Baron1')\n"
                     "assert(a == false and b == nil)");
    }

    // Both args given, stateEnum == 3, arg 2 FAILS to resolve to a valid
    // vehicle handle -> the CONFIRMED double-push: false for the resolve
    // failure, THEN the real predicate result (true here, since stateEnum
    // alone decides it once no valid handle exists) - TWO return values.
    // This is the one real behavioral subtlety Sec3.10 calls out.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Erin", true);
        es.getOrCreateCharacter("Erin").stateEnum.set(3);
        es.objectResolves().set("GhostCar", false); // KNOWN not to resolve
        ok(host, gp, "local a,b = character_is_in_vehicle('Erin', 'GhostCar')\n"
                     "assert(a == false and b == true)");
    }

    // Same double-push path, but stateEnum != 3 this time -> [false,
    // false] - both pushed values are false, but for DIFFERENT reasons
    // (the resolve failure, then the real predicate's own "not in a
    // vehicle at all" answer), confirming the second push is the real
    // predicate result and not just a hardcoded duplicate of the first.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Frank", true);
        es.getOrCreateCharacter("Frank").stateEnum.set(0);
        es.objectResolves().set("GhostCar2", false);
        ok(host, gp, "local a,b = character_is_in_vehicle('Frank', 'GhostCar2')\n"
                     "assert(a == false and b == false)");
    }

    // Both args given, stateEnum == 3, but arg 2's own resolve state was
    // NEVER set at all (true OPEN, not "known false") -> refuses at the
    // same shared gate, same as arg 1's own OPEN case above.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Gina", true);
        es.getOrCreateCharacter("Gina").stateEnum.set(3);
        refuses(host, gp, "character_is_in_vehicle('Gina', 'NeverConsideredVehicle')", "named-object resolution");
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_character_is_in_vehicle_test: all checks passed\n";
    return 0;
}
