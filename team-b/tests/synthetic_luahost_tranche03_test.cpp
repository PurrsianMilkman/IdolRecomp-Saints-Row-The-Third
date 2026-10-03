// Synthetic tests for spec-lua-api-behaviour.md Sec30 ("ranking tranche
// 03", 2026-10-02), the 9 of its 25 CONFIRMED names this pass implements -
// see include/sr3luahost/engine_state.h and src/lua_spec_confirmed_stubs.cpp
// (that file's own Sec30 batch header comment) for the full per-function
// reasoning and for why the other 16 names stay generic logging stubs this
// pass. Same convention as tests/synthetic_luahost_batch2728_test.cpp: a
// small, hand-written fixture (no real tagged-registration-list file or real
// archive read here). Every name below is registered with its own real
// cluster tag, grepped directly from tools/lua_all_registered_1490_
// tagged.txt (all 9 are `gameplay`).
//
// Specific focus (per this task's own explicit instruction): the two
// CONFIRMED crash-shaped edges this tranche flags (Sec30.7) - vehicle_
// exit_group_do's possible NULL-`this` call and team_make_unfriendly's
// out-of-bounds relation-matrix index - are each exercised to confirm this
// host guards them gracefully (via EngineState::vehicleExitGroupNullThis
// GuardCount()/teamRelationOobGuardCount(), test-observable counters, NOT
// real engine fields) rather than crashing or throwing uncaught. The exact
// guarded behavior (silently skip the equivalent write/call) is this host's
// own HOST-SAFETY choice, not a spec fact - the real engine's own true
// consequence for either input is at best "OPEN, not confirmed" per Sec30.7
// itself.
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

// Runs `code` and expects it to succeed (asserts inside the chunk do the work).
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "tranche03_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code` and expects a Lua error whose message contains `needle`.
void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "tranche03_test.lua");
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

// The 9 (name, cluster) rows this batch registers - real tags grepped
// directly from tools/lua_all_registered_1490_tagged.txt.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"vehicle_set_invulnerable_to_player_explosives", "gameplay"},
        {"vehicle_disable_explosion_and_damage_vfx", "gameplay"},
        {"vehicle_set_special_override_never_ghost", "gameplay"},
        {"vehicle_clear_all_radio_locks", "gameplay"},
        {"vehicle_is_vtol", "gameplay"},
        {"auto_pickup_enable", "gameplay"},
        {"vehicle_exit_group_do", "gameplay"},
        {"vehicle_exit_group_check_done", "gameplay"},
        {"team_make_unfriendly", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 9 names land as real functions in gameplay, and
    // nowhere else.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "team_make_unfriendly"));
        CHECK(!hasGlobal(host.uiState(), "team_make_unfriendly"));
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec30.3 auto_pickup_enable: CONFIRMED file-backed default "enabled";
    // the call always re-sets it true.
    CHECK(es.autoPickupEnabled() == true);
    es.autoPickupEnabled() = false;
    ok(host, gp, "auto_pickup_enable()");
    CHECK(es.autoPickupEnabled() == true);

    // ------------------------------------------------------------------
    // Sec30.5 vehicle force-flag setters: unresolved vehicle -> real no-op
    // (never even creates a VehicleState entry); resolved + gate OPEN ->
    // refuses; gate false -> replicate-only (no local bit write); gate true
    // -> the bit is actually written, both ways.
    es.objectResolves().set("veh01", false);
    ok(host, gp, "vehicle_set_invulnerable_to_player_explosives('veh01', true)");
    CHECK(!es.hasVehicle("veh01"));

    es.objectResolves().set("veh02", true);
    fails(host, gp, "vehicle_set_invulnerable_to_player_explosives('veh02', true)", "0x008ae480");
    es.getOrCreateVehicle("veh02").forceFlagGatePasses.set(false);
    ok(host, gp, "vehicle_set_invulnerable_to_player_explosives('veh02', true)"); // gate false: replicate only
    CHECK(es.vehicles().at("veh02").forceFlags.knownMask() == 0);
    es.vehicles().at("veh02").forceFlagGatePasses.set(true);
    ok(host, gp, "vehicle_set_invulnerable_to_player_explosives('veh02', true)");
    CHECK(es.vehicles().at("veh02").forceFlags.get(VehicleState::kInvulnerableToPlayerExplosivesBit) ==
          VehicleState::kInvulnerableToPlayerExplosivesBit);
    ok(host, gp, "vehicle_set_invulnerable_to_player_explosives('veh02', false)");
    CHECK(es.vehicles().at("veh02").forceFlags.get(VehicleState::kInvulnerableToPlayerExplosivesBit) == 0);

    // Sec30.5 vehicle_disable_explosion_and_damage_vfx: this tranche's own
    // highest real call count (28, see this file's own top comment and
    // lua_spec_confirmed_stubs.cpp's batch header).
    es.objectResolves().set("veh03", true);
    es.getOrCreateVehicle("veh03").forceFlagGatePasses.set(true);
    ok(host, gp, "vehicle_disable_explosion_and_damage_vfx('veh03', true)");
    CHECK(es.vehicles().at("veh03").forceFlags.get(VehicleState::kDisableExpAndDamageVfxBit) ==
          VehicleState::kDisableExpAndDamageVfxBit);
    ok(host, gp, "vehicle_disable_explosion_and_damage_vfx('veh03', false)");
    CHECK(es.vehicles().at("veh03").forceFlags.get(VehicleState::kDisableExpAndDamageVfxBit) == 0);

    // Sec30.5 vehicle_set_special_override_never_ghost: same shape, the 4th bit.
    es.objectResolves().set("veh04", true);
    es.getOrCreateVehicle("veh04").forceFlagGatePasses.set(true);
    ok(host, gp, "vehicle_set_special_override_never_ghost('veh04', true)");
    CHECK(es.vehicles().at("veh04").forceFlags.get(VehicleState::kSpecialOverrideNeverGhostBit) ==
          VehicleState::kSpecialOverrideNeverGhostBit);

    // Sec30.5 vehicle_clear_all_radio_locks: walks every vehicle this host
    // currently knows about (veh02/veh03/veh04 from above); clears the
    // radio-locked bit only where the per-vehicle gate passes, hardcoded
    // false either way - a gate failure replicates instead of skipping the
    // vehicle (so veh04's gate is left false, unlike the others, to prove
    // the walk still visits it).
    es.vehicles().at("veh02").forceFlags.setBits(VehicleState::kRadioControlsLockedBit,
                                                  VehicleState::kRadioControlsLockedBit);
    es.vehicles().at("veh03").forceFlags.setBits(VehicleState::kRadioControlsLockedBit,
                                                  VehicleState::kRadioControlsLockedBit);
    es.vehicles().at("veh04").forceFlagGatePasses.set(false);
    es.vehicles().at("veh04").forceFlags.setBits(VehicleState::kRadioControlsLockedBit,
                                                  VehicleState::kRadioControlsLockedBit);
    ok(host, gp, "vehicle_clear_all_radio_locks()");
    CHECK(es.vehicles().at("veh02").forceFlags.get(VehicleState::kRadioControlsLockedBit) == 0);       // gate true: cleared
    CHECK(es.vehicles().at("veh03").forceFlags.get(VehicleState::kRadioControlsLockedBit) == 0);       // gate true: cleared
    CHECK(es.vehicles().at("veh04").forceFlags.get(VehicleState::kRadioControlsLockedBit) ==
          VehicleState::kRadioControlsLockedBit); // gate false: replicate-only, NOT cleared

    // Sec30.5 vehicle_is_vtol: unresolved -> false; resolved -> true only
    // when vehicleClass == 4.
    es.objectResolves().set("veh05", false);
    ok(host, gp, "assert(vehicle_is_vtol('veh05') == false)");
    es.objectResolves().set("veh06", true);
    fails(host, gp, "vehicle_is_vtol('veh06')", "0xbf4");
    es.getOrCreateVehicle("veh06").vehicleClass.set(2);
    ok(host, gp, "assert(vehicle_is_vtol('veh06') == false)");
    es.vehicles().at("veh06").vehicleClass.set(4);
    ok(host, gp, "assert(vehicle_is_vtol('veh06') == true)");

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_exit_group_do - including the CONFIRMED crash-shaped
    // edge (Sec30.7): a NULL-`this` call when no vehicle was found. This
    // host guards it (HOST-SAFETY, not a spec fact) and counts the averted
    // path via vehicleExitGroupNullThisGuardCount() instead of crashing.
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 0);

    // Fewer than 4 args: CONFIRMED no table is seen, same as an empty one -
    // no character resolves -> false, AND no vehicle was ever found -> the
    // guard fires.
    ok(host, gp, "assert(vehicle_exit_group_do(true) == false)");
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 1);

    ok(host, gp, "assert(vehicle_exit_group_do(true, false, false, {}) == false)");
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 2);

    es.objectResolves().set("npcA", false); // does not resolve
    ok(host, gp, "assert(vehicle_exit_group_do(true, false, false, {'npcA'}) == false)");
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 3);

    es.objectResolves().set("npcB", true);
    es.getOrCreateCharacter("npcB").stateEnum.set(0); // resolves, not state 3
    ok(host, gp, "assert(vehicle_exit_group_do(true, false, false, {'npcB'}) == false)");
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 4);

    // OPEN-state discipline still applies: state enum, then the vehicle
    // link, each refuse independently until set.
    es.objectResolves().set("npcE", true);
    fails(host, gp, "vehicle_exit_group_do(true, false, false, {'npcE'})", "+0x16d4");
    es.getOrCreateCharacter("npcE").stateEnum.set(3);
    fails(host, gp, "vehicle_exit_group_do(true, false, false, {'npcE'})", "+0x16c0");

    // Resolves, state 3, AND a real vehicle link -> true, guard does NOT fire.
    es.getOrCreateCharacter("npcE").currentVehicleName.set("veh07");
    ok(host, gp, "assert(vehicle_exit_group_do(true, false, false, {'npcE'}) == true)");
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 4); // unchanged - a vehicle was found

    // THE core crash-guard case: resolves, state 3 (so an exit request IS
    // built and the call returns true), but the character's own vehicle
    // link is explicitly "none" - exactly the real engine's own "no
    // vehicle found across the whole table" shape, which calls 0x00a7bdc0
    // with a NULL `this` (Sec30.7). This host must NOT crash here; it
    // guards and counts instead.
    es.objectResolves().set("npcD", true);
    es.getOrCreateCharacter("npcD").stateEnum.set(3);
    es.getOrCreateCharacter("npcD").currentVehicleName.set(""); // explicitly "no vehicle"
    ok(host, gp, "assert(vehicle_exit_group_do(true, false, false, {'npcD'}) == true)");
    CHECK(es.vehicleExitGroupNullThisGuardCount() == 5); // guard fired despite a TRUE return

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_exit_group_check_done - the "polling partner". True
    // for an empty/missing table (CONFIRMED); false iff some listed
    // character resolves, is alive, and is in state 2 or 3.
    ok(host, gp, "assert(vehicle_exit_group_check_done({}) == true)");
    ok(host, gp, "assert(vehicle_exit_group_check_done() == true)");

    es.objectResolves().set("npcF", true);
    fails(host, gp, "vehicle_exit_group_check_done({'npcF'})", "0x0096f4f0");
    es.getOrCreateCharacter("npcF").isAlive.set(false);
    ok(host, gp, "assert(vehicle_exit_group_check_done({'npcF'}) == true)"); // not alive: skipped
    es.getOrCreateCharacter("npcF").isAlive.set(true);
    fails(host, gp, "vehicle_exit_group_check_done({'npcF'})", "+0x16d4");
    es.getOrCreateCharacter("npcF").stateEnum.set(1);
    ok(host, gp, "assert(vehicle_exit_group_check_done({'npcF'}) == true)"); // state 1: not 2 or 3
    es.getOrCreateCharacter("npcF").stateEnum.set(2);
    ok(host, gp, "assert(vehicle_exit_group_check_done({'npcF'}) == false)"); // state 2: matches
    es.getOrCreateCharacter("npcF").stateEnum.set(3);
    ok(host, gp, "assert(vehicle_exit_group_check_done({'npcF'}) == false)"); // state 3: matches

    // ------------------------------------------------------------------
    // Sec30.6 team_make_unfriendly - including the CONFIRMED crash-shaped
    // edge (Sec30.7): an unrecognized team name resolves to the real
    // sentinel id 9, which indexes past the CONFIRMED 8x8 relation matrix.
    // This host bounds-checks and skips the write (HOST-SAFETY, not a spec
    // fact) rather than performing the equivalent out-of-bounds C++ array
    // write, counting the averted path via teamRelationOobGuardCount().
    CHECK(es.teamRelationOobGuardCount() == 0);
    ok(host, gp, "team_make_unfriendly('UnknownTeamA', 'UnknownTeamB')"); // both sentinel 9: guarded
    CHECK(es.teamRelationOobGuardCount() == 1);
    CHECK(es.teamRelationForTesting(9, 9) == -2); // out of bounds: never written

    es.registerTeamIdForTesting("Saints", 0);
    es.registerTeamIdForTesting("Police", 1);
    ok(host, gp, "team_make_unfriendly('Saints', 'Police')"); // both in-bounds: real write
    CHECK(es.teamRelationOobGuardCount() == 1); // unchanged
    CHECK(es.teamRelationForTesting(0, 1) == 1);
    CHECK(es.teamRelationForTesting(1, 0) == 1); // symmetric write

    ok(host, gp, "team_make_unfriendly('Saints', 'UnknownTeamC')"); // one side still unresolved: guarded
    CHECK(es.teamRelationOobGuardCount() == 2);

    // Sec30.6's own refinement of Sec20.6: team 5 mirrors onto team 6.
    es.registerTeamIdForTesting("Neutral5", 5);
    es.registerTeamIdForTesting("Other", 2);
    ok(host, gp, "team_make_unfriendly('Neutral5', 'Other')");
    CHECK(es.teamRelationForTesting(5, 2) == 1);
    CHECK(es.teamRelationForTesting(2, 5) == 1);
    CHECK(es.teamRelationForTesting(6, 2) == 1); // mirrored onto team 6
    CHECK(es.teamRelationForTesting(2, 6) == 1); // mirrored onto team 6

    if (g_failures == 0)
        std::cout << "batch 2026-10-02 (Sec30 ranking tranche 03, 9 functions): all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
