// Synthetic tests for the tranche-F + camera close-out batch
// (src/lua_spec_confirmed_stubs_b30_tranchef_camera.cpp): character_kill
// (Sec3.15), vehicle_is_destroyed (Sec4.9), camera_restrict_set_active
// (Sec8.12). Same hand-written-fixture convention as every other
// synthetic_luahost_*_test.
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
    auto r = host.runChunk(L, code, "b30_tranchef_camera_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "b30_tranchef_camera_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"character_kill", "gameplay"},
        {"vehicle_is_destroyed", "gameplay"},
        {"camera_restrict_set_active", "gameplay"},
    };
}

} // namespace

int main() {
    // ----------------------------------------------------------------
    // character_kill (Sec3.15, resolved by tranche F 2026-10-08): neither
    // resolve chain resolves -> no-op, no throw (CONFIRMED a resolve is
    // required to reach the damage call); primary (player-class) chain
    // resolves -> deterministic lethal hit (lifeState -> 5); arg3 looks up
    // the SAME animation-state table set_animation_state uses; arg4
    // default true SKIPS the finalizer, explicit false with lifeState==6
    // ("downed") runs it.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Neither chain resolves - CONFIRMED silent no-op (OPEN reads
        // still throw when truly unregistered, same as every other
        // resolve-gated native; registering both false proves the no-op).
        es.playerClassChainResolves().set("Ghost", false);
        es.objectResolves().set("Ghost", false);
        ok(host, gp, "character_kill('Ghost')");
        CHECK(es.getOrCreateCharacter("Ghost").characterKillCallCount == 0);

        // Fallback-chain resolve: lethal hit always lands.
        es.objectResolves().set("Bob", true);
        es.playerClassChainResolves().set("Bob", false);
        ok(host, gp, "character_kill('Bob')");
        CharacterState& bob = es.getOrCreateCharacter("Bob");
        CHECK(bob.characterKillCallCount == 1);
        CHECK(bob.lastCharacterKillArg2 == false); // default
        CHECK(bob.lifeState.get() == 5);
        CHECK(bob.deathReactionDispatchCount == 0); // arg3 absent

        // arg2 true recorded (its real-world meaning stays OPEN); arg3
        // death-reaction name looked up via the shared animation-state
        // table.
        es.registerAnimationStateIdForTesting("death_fire", 7);
        ok(host, gp, "character_kill('Bob', true, 'death_fire')");
        CHECK(bob.lastCharacterKillArg2 == true);
        CHECK(bob.deathReactionDispatchCount == 1);
        CHECK(bob.lastDeathReactionAnimationId == 7);

        // arg4 default true -> finalizer SKIPPED even though lifeState==5
        // (not 6) - no finalizer call recorded either way by default.
        CHECK(bob.characterKillFinalizerCount == 0);

        // arg4 explicitly false, lifeState currently 6 ("downed") -> the
        // finalizer's own dead-state writer fires (literal 5 again).
        bob.lifeState.set(6);
        ok(host, gp, "character_kill('Bob', false, nil, false)");
        CHECK(bob.characterKillFinalizerCount == 1);
        CHECK(bob.lifeState.get() == 5);
        CHECK(bob.killFinalizedFlagE3.get() == true);

        // Primary (player-class) chain resolving is equally sufficient.
        es.playerClassChainResolves().set("#PLAYER#", true);
        ok(host, gp, "character_kill('#PLAYER#')");
        CHECK(es.getOrCreateCharacter("#PLAYER#").lifeState.get() == 5);
    }

    // ----------------------------------------------------------------
    // vehicle_is_destroyed (Sec4.9, resolved by tranche F 2026-10-08): arg1
    // is a VEHICLE name (not an occupant); unresolved -> true (fail-safe
    // default); resolved -> direct, UNNEGATED read of destroyedFlag16c8
    // (the SAME field vehicle_detonate/Sec9.7 models).
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Never-registered name: OPEN, refuses (same convention as every
        // other objectResolves()-gated native).
        fails(host, gp, "vehicle_is_destroyed('NoSuchVehicle')", "named-object resolution");
        // Registered but resolve fails (liveness/name-not-found path) ->
        // CONFIRMED fail-safe default true.
        es.objectResolves().set("GhostCar", false);
        ok(host, gp, "assert(vehicle_is_destroyed('GhostCar') == true)");
        // Resolved -> direct, UNNEGATED read of destroyedFlag16c8.
        es.objectResolves().set("Hollywood", true);
        es.getOrCreateVehicle("Hollywood").destroyedFlag16c8.set(false);
        ok(host, gp, "assert(vehicle_is_destroyed('Hollywood') == false)");
        es.getOrCreateVehicle("Hollywood").destroyedFlag16c8.set(true);
        ok(host, gp, "assert(vehicle_is_destroyed('Hollywood') == true)");
    }

    // ----------------------------------------------------------------
    // camera_restrict_set_active (Sec8.12): absent target -> local setter
    // AND replicate BOTH fire (observable here only via the local flag,
    // since replicate is a no-op stand-in); target resolves to the local
    // player -> local setter only; target resolves to anything else (or
    // fails to resolve) -> local setter is SKIPPED entirely.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        ok(host, gp, "camera_restrict_set_active(true)"); // absent arg2: local setter runs
        CHECK(es.cameraRestrictionActive() == true);
        ok(host, gp, "camera_restrict_set_active(false)");
        CHECK(es.cameraRestrictionActive() == false);

        // Resolves to the local player -> local setter runs.
        es.localPlayerNameForTesting().set("Hero");
        es.objectResolves().set("Hero", true);
        ok(host, gp, "camera_restrict_set_active(true, 'Hero')");
        CHECK(es.cameraRestrictionActive() == true);

        // Resolves to someone else -> local setter SKIPPED entirely (flag
        // stays at whatever it last was - unchanged by this call).
        es.objectResolves().set("OtherPlayer", true);
        ok(host, gp, "camera_restrict_set_active(false, 'OtherPlayer')");
        CHECK(es.cameraRestrictionActive() == true); // unchanged - local setter never reached

        // Fails to resolve -> same "skip local setter" branch.
        es.objectResolves().set("Nobody", false);
        ok(host, gp, "camera_restrict_set_active(false, 'Nobody')");
        CHECK(es.cameraRestrictionActive() == true); // still unchanged
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_b30_tranchef_camera_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_b30_tranchef_camera_test: " << g_failures << " check(s) failed\n";
    return 1;
}
