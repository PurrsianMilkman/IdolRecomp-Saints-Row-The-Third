// Synthetic tests for wave 2, Batch W2-7 (2026-10-08) - the vehicle/
// helicopter/airplane/traffic half (39 names) of spec-lua-api-behaviour.md
// Sec38-60 - see src/lua_spec_confirmed_stubs_b26_wave2_vehicles_
// helicopter.cpp's own top comment for the full citation list. Sibling
// test tests/synthetic_luahost_wave2_w27b_test.cpp covers the other 74
// (spawn/world/effects/audio/camera/mission-specific) names, including the
// dlc2_m02_clapboards_set hardening-unification and the audio-family
// return-shape quirks this task's own instructions call out explicitly.
// Same hand-written-fixture convention as every other synthetic_luahost_*_
// test. All 39 names are `gameplay` (every §38-§59 source line this batch
// cites places them in the 1,014/1,016-entry gameplay registrar
// 0x00a20840, never one of each tranche's own UI sub-registrars).
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
    auto r = host.runChunk(L, code, "w27a_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "w27a_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"vehicle_anim_playing", "gameplay"},
        {"vehicle_delete_all_corpses", "gameplay"},
        {"vehicle_disable_weapon_physics", "gameplay"},
        {"vehicle_engine_start", "gameplay"},
        {"vehicle_forced_corpse_removal_enabled", "gameplay"},
        {"vehicle_hidden", "gameplay"},
        {"vehicle_set_kneecappers_damage", "gameplay"},
        {"vehicle_set_npc_engine_audio", "gameplay"},
        {"vehicle_set_obey_traffic_lights", "gameplay"},
        {"vehicle_set_radio_controls_locked", "gameplay"},
        {"vehicle_set_sirens", "gameplay"},
        {"vehicle_set_use_short_cuts", "gameplay"},
        {"vehicle_allow_ownership_transfer", "gameplay"},
        {"vehicle_component_is_shattered", "gameplay"},
        {"vehicle_component_shatter_glass", "gameplay"},
        {"vehicle_detonate_random_parked_car", "gameplay"},
        {"vehicle_evacuate", "gameplay"},
        {"vehicle_flee", "gameplay"},
        {"vehicle_get_max_speed", "gameplay"},
        {"vehicle_in_air", "gameplay"},
        {"vehicle_is_flipped", "gameplay"},
        {"vault_heli_hack_for_m01", "gameplay"},
        {"get_vehicle_speed", "gameplay"},
        {"helicopter_set_dont_use_constraints", "gameplay"},
        {"helicopter_clear_max_bank_angle", "gameplay"},
        {"helicopter_enter_dropoff", "gameplay"},
        {"helicopter_land", "gameplay"},
        {"helicopter_set_dont_explode_in_air", "gameplay"},
        {"helicopter_set_dont_move_in_combat", "gameplay"},
        {"helicopter_set_large_dspiral", "gameplay"},
        {"rc_set_max_signal_range", "gameplay"},
        {"roadblock_create", "gameplay"},
        {"roadblock_destroy", "gameplay"},
        {"spotlight_heli_laser_size", "gameplay"},
        {"vtol_flyby_animation_for_m18", "gameplay"},
        {"is_a_vehicle", "gameplay"},
        {"airplane_land_do", "gameplay"},
        {"airplane_takeoff_do", "gameplay"},
        {"airplane_fly_to_do", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration smoke test.
    // ------------------------------------------------------------------
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(host.gameplayState() != nullptr);
            lua_getglobal(host.gameplayState(), rn.name.c_str());
            CHECK(lua_isfunction(host.gameplayState(), -1));
            lua_pop(host.gameplayState(), 1);
        }
    }

    Host host(fixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();
    es.objectResolves().set("Car1", true);
    es.objectResolves().set("NoSuchObject", false);
    es.objectResolves().set("Heli1", true);
    es.objectResolves().set("Plane1", true);
    es.objectResolves().set("PointA", true);
    es.objectResolves().set("PathA", true);

    // --- Sec38.1 vehicle cluster ------------------------------------------
    ok(host, gp, "assert(vehicle_anim_playing('NoSuchObject') == false)");
    es.getOrCreateVehicle("Car1").bodyActive.set(true);
    es.getOrCreateVehicle("Car1").animSetActiveNotFinishing.set(true);
    ok(host, gp, "assert(vehicle_anim_playing('Car1') == true)");

    es.getOrCreateVehicle("Car1").wrecked = true;
    ok(host, gp, "vehicle_delete_all_corpses()");
    CHECK(es.getOrCreateVehicle("Car1").deletedAsCorpse);

    CHECK(es.weaponPhysicsEnabled()); // CHOSEN true initial
    ok(host, gp, "vehicle_disable_weapon_physics()"); // CONFIRMED: no-arg call disables (writes NOT(true) = false)
    CHECK(!es.weaponPhysicsEnabled()); // CONFIRMED: no-argument call disables the effect

    ok(host, gp, "vehicle_engine_start('Car1')");
    CHECK(es.getOrCreateVehicle("Car1").engineRunning);

    CHECK(es.vehicleForcedCorpseRemovalEnabled()); // CHOSEN true initial (CONFIRMED reset value)
    // CONFIRMED: writes only "when a session exists and this machine is
    // host" - single player has no session (coopLocalIsHost() == false,
    // Sec8.27/Sec26.28), so this is a genuine, confirmed no-op here, the
    // same "host-gated setter is a no-op in single player" shape this
    // whole spec documents repeatedly (e.g. store_stronghold_game_
    // purchase_upgrade, mission_maybe_uncomplete_m22).
    CHECK(!es.coopLocalIsHost());
    ok(host, gp, "vehicle_forced_corpse_removal_enabled(false)");
    CHECK(es.vehicleForcedCorpseRemovalEnabled()); // unchanged - no session/host in single player
    // With a simulated host session, the write DOES apply.
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    CHECK(es.coopLocalIsHost());
    ok(host, gp, "vehicle_forced_corpse_removal_enabled(false)");
    CHECK(!es.vehicleForcedCorpseRemovalEnabled());

    ok(host, gp, "assert(vehicle_hidden('NoSuchObject') == true)"); // CONFIRMED: unresolved -> true
    ok(host, gp, "assert(vehicle_hidden('Car1') == false)");        // not hidden by default

    // --- Sec46.5 direct unreplicated field setters -------------------------
    ok(host, gp, "vehicle_set_kneecappers_damage('Car1', 3.5)");
    CHECK(es.getOrCreateVehicle("Car1").kneecappersDamage == 3.5);
    // Transposition-risk check: the already-registered kneecappersEnabled/
    // kneecappersDeferred fields (vehicle_set_kneecappers, Sec33.1) are
    // UNTOUCHED by this different native.
    CHECK(!es.getOrCreateVehicle("Car1").kneecappersEnabled);

    ok(host, gp, "vehicle_set_npc_engine_audio('Car1', false)");
    CHECK(!es.getOrCreateVehicle("Car1").npcEngineAudioEnabled);

    // vehicle_set_obey_traffic_lights (Sec4.5a) REUSES the SAME vai_force_
    // flags vehicle-AI sub-record byte +0xc68+4 as vehicle_suppress_npc_exit
    // (Sec4.5) - verify it lands there, not a separate CHOSEN field.
    ok(host, gp, "vehicle_set_obey_traffic_lights('Car1', false)");
    CHECK(es.getOrCreateVehicle("Car1").vehicleAiForceFlagsByte4.get(VehicleState::kObeyTrafficLightsBit) == 0);
    ok(host, gp, "vehicle_set_obey_traffic_lights('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").vehicleAiForceFlagsByte4.get(VehicleState::kObeyTrafficLightsBit) != 0);

    // vehicle_set_radio_controls_locked REUSES the Sec30.5 double-gate
    // force-flag family directly (same bit vehicle_clear_all_radio_locks
    // clears elsewhere) - verify it actually lands on VehicleState::
    // forceFlags, not a separate CHOSEN field.
    ok(host, gp, "vehicle_set_radio_controls_locked('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").forceFlags.get(VehicleState::kRadioControlsLockedBit) != 0);
    ok(host, gp, "vehicle_set_radio_controls_locked('Car1', false)");
    CHECK(es.getOrCreateVehicle("Car1").forceFlags.get(VehicleState::kRadioControlsLockedBit) == 0);

    ok(host, gp, "vehicle_set_sirens('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").sirensOn);

    // vehicle_set_use_short_cuts (Sec4.5a) - its own byte, +0xc68+6, not
    // previously catalogued anywhere else in this family.
    ok(host, gp, "vehicle_set_use_short_cuts('Car1', false)");
    CHECK(es.getOrCreateVehicle("Car1").vehicleAiForceFlagsByte6.get(VehicleState::kUseShortCutsBit) == 0);
    ok(host, gp, "vehicle_set_use_short_cuts('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").vehicleAiForceFlagsByte6.get(VehicleState::kUseShortCutsBit) != 0);

    // --- Sec52.2 vehicle cluster --------------------------------------------
    ok(host, gp, "vehicle_allow_ownership_transfer('Car1', false)");
    CHECK(!es.getOrCreateVehicle("Car1").ownershipTransferAllowed);

    ok(host, gp, "assert(vehicle_component_is_shattered('Car1', 'hood') == false)");
    ok(host, gp, "vehicle_component_shatter_glass('Car1', 'hood')"); // "not glass-specific" - any named component
    ok(host, gp, "assert(vehicle_component_is_shattered('Car1', 'hood') == true)");
    // HARDENED: a non-string component name refuses instead of reaching a
    // real engine's CRT _stricmp null-pointer crash.
    CHECK(es.getOrCreateVehicle("Car1").componentNullNameGuardCount == 0);
    ok(host, gp, "vehicle_component_shatter_glass('Car1', nil)");
    CHECK(es.getOrCreateVehicle("Car1").componentNullNameGuardCount == 1);

    // vehicle_detonate_random_parked_car: HARDENED, bounds the blast scope
    // instead of reproducing the real "resolve failure widens to the whole
    // world" defect. Uses its own fresh vehicle name (Car2) so its effect
    // isn't muddied by Car1's earlier vehicle_delete_all_corpses() state.
    es.objectResolves().set("Car2", true);
    CHECK(es.vehicleDetonateRandomNoReferenceGuardCount() == 0);
    ok(host, gp, "vehicle_detonate_random_parked_car()");
    CHECK(es.vehicleDetonateRandomNoReferenceGuardCount() == 1);
    es.objectResolves().set("StrayCar", false); // explicitly a known-false resolve, not merely unregistered
    ok(host, gp, "vehicle_detonate_random_parked_car('StrayCar')");
    CHECK(es.vehicleDetonateRandomFailedReferenceGuardCount() == 1);
    CHECK(!es.getOrCreateVehicle("Car2").deletedAsCorpse); // untouched by either guarded call above
    // SPEC-CONFIRMED (Sec52.2): once a reference name is given AND resolves,
    // the reference object is a LOCATION ANCHOR for an angle/distance
    // window, not the detonation target - the default (all-zero) window
    // admits nothing, so this correctly detonates NOTHING. The resolved
    // reference's own state must be left completely unchanged, and only the
    // dedicated no-op counter advances.
    CHECK(es.vehicleDetonateRandomResolvedReferenceNoOpCount() == 0);
    ok(host, gp, "vehicle_detonate_random_parked_car('Car2')"); // resolves -> spec-confirmed no-op (location anchor, not the target)
    CHECK(!es.getOrCreateVehicle("Car2").deletedAsCorpse); // reference object's state is untouched
    CHECK(es.vehicleDetonateRandomResolvedReferenceNoOpCount() == 1);
    // Transposition-risk check: the already-registered vehicle_detonate
    // (Sec9.7) is a completely different native/registry entry - not
    // exercised or touched here.

    ok(host, gp, "vehicle_evacuate('Car1')");
    CHECK(es.getOrCreateVehicle("Car1").evacuateCount == 1);

    ok(host, gp, "vehicle_flee('Car1')");
    CHECK(es.getOrCreateVehicle("Car1").fleeing);
    CHECK(es.getOrCreateVehicle("Car1").aiMode == 2);
    es.getOrCreateVehicle("Heli1").playerDriven.set(true);
    ok(host, gp, "vehicle_flee('Heli1')");
    CHECK(!es.getOrCreateVehicle("Heli1").fleeing); // CONFIRMED veto: player-driven never flees

    ok(host, gp, "assert(vehicle_get_max_speed('NoSuchObject') == -1)");
    es.getOrCreateVehicle("Car1").maxSpeedMph.set(85.0);
    ok(host, gp, "assert(vehicle_get_max_speed('Car1') == 85.0)");

    es.getOrCreateVehicle("Car1").inAir.set(true);
    ok(host, gp, "assert(vehicle_in_air('Car1') == true)");
    es.getOrCreateVehicle("Car1").flipped.set(false);
    ok(host, gp, "assert(vehicle_is_flipped('Car1') == false)");

    ok(host, gp, "vault_heli_hack_for_m01('Heli1', 3)");
    CHECK(es.getOrCreateVehicle("Heli1").vaultHeliComponentIndexCache == 3);

    // --- Sec41.6/41.5/53.2 helicopter + speed cluster -----------------------
    ok(host, gp, "assert(get_vehicle_speed('NoSuchObject') == 0)");
    es.getOrCreateVehicle("Car1").bodyActive.set(true);
    es.getOrCreateVehicle("Car1").speedMph.set(42);
    ok(host, gp, "assert(get_vehicle_speed('Car1') == 42)");

    ok(host, gp, "helicopter_set_dont_use_constraints('Heli1', true)");
    CHECK(es.getOrCreateVehicle("Heli1").dontUseConstraints);

    es.getOrCreateVehicle("Heli1").maxBankAngleRadians = 1.5;
    es.getOrCreateVehicle("Heli1").maxBankAngleOverrideEnabled = true;
    ok(host, gp, "helicopter_clear_max_bank_angle('Heli1')");
    CHECK(!es.getOrCreateVehicle("Heli1").maxBankAngleOverrideEnabled);
    CHECK(es.getOrCreateVehicle("Heli1").maxBankAngleRadians == 1.5); // CONFIRMED: angle value left untouched

    ok(host, gp, "assert(helicopter_land('Heli1') == true)"); // CONFIRMED optimistic true (resolve-only precondition)
    CHECK(!es.getOrCreateVehicle("Heli1").landed); // distance gate OPEN/unset -> not actually landed
    es.getOrCreateVehicle("Heli1").landDistanceGatePasses.set(true);
    ok(host, gp, "helicopter_land('Heli1')");
    CHECK(es.getOrCreateVehicle("Heli1").landed);

    ok(host, gp, "assert(helicopter_enter_dropoff('Heli1') == true)");
    CHECK(!es.getOrCreateVehicle("Heli1").dropoffRequested);
    ok(host, gp, "helicopter_enter_dropoff('Heli1', nil, true)"); // explicit override
    CHECK(es.getOrCreateVehicle("Heli1").dropoffRequested);

    ok(host, gp, "helicopter_set_dont_explode_in_air('Heli1', true)");
    CHECK(es.getOrCreateVehicle("Heli1").dontExplodeInAir);
    ok(host, gp, "helicopter_set_dont_move_in_combat('Heli1', true)");
    CHECK(es.getOrCreateVehicle("Heli1").dontMoveInCombat);
    ok(host, gp, "helicopter_set_large_dspiral('Heli1', true)");
    CHECK(es.getOrCreateVehicle("Heli1").largeDspiral);

    // --- Sec39.2 rc signal/roadblocks/spotlight -----------------------------
    ok(host, gp, "assert(rc_set_max_signal_range(100) == 200.0)"); // start-up value 200 (tweak RC_vehicle_gun_max_distance, Sec39.2)
    CHECK(es.rcMaxSignalRange().get() == 100.0);
    es.rcMaxSignalRange().set(50.0);
    ok(host, gp, "assert(rc_set_max_signal_range(100) == 50.0)"); // returns the PREVIOUS value
    ok(host, gp, "assert(rc_set_max_signal_range(0) == 100.0)");

    es.roadblockPlacementResolves().set("Spot1", true);
    ok(host, gp, "local h = roadblock_create('tmpl', 'Spot1'); assert(h > 0)");
    CHECK(es.roadblockCount() == 1);
    ok(host, gp, "assert(roadblock_create('tmpl', 'BadSpot') == -1)"); // CONFIRMED: -1 on any failure
    CHECK(es.roadblockCount() == 1); // the failed create did not add a record
    ok(host, gp, "roadblock_destroy(1)");
    CHECK(es.roadblockCount() == 0);

    CHECK(es.spotlightHeliLaserSize() == 1.0); // CONFIRMED default
    ok(host, gp, "spotlight_heli_laser_size(2.5)");
    CHECK(es.spotlightHeliLaserSize() == 2.5);

    // --- Sec46.4 vtol_flyby_animation_for_m18 -------------------------------
    ok(host, gp, "vtol_flyby_animation_for_m18('Plane1')");
    CHECK(es.getOrCreateVehicle("Plane1").vtolFlybyM18Played);
    CHECK(es.vtolFlybyM18BroadcastCount() == 1);

    // --- Sec54.3 is_a_vehicle ------------------------------------------------
    ok(host, gp, "assert(is_a_vehicle('Car1') == true)");
    ok(host, gp, "assert(is_a_vehicle('NotAVehicleName') == false)");

    // --- Sec58/59.7 airplanes ------------------------------------------------
    ok(host, gp, "assert(airplane_takeoff_do('Plane1') == true)");
    CHECK(es.airplanes()["Plane1"].takeoffRequested);
    ok(host, gp, "assert(airplane_land_do('Plane1', 'PointA') == true)"); // CONFIRMED return-value trap: always true once resolved

    // airplane_fly_to_do: navpoint form always false (CONFIRMED faithful,
    // not a bug to fix); HARDENED null-target strlen() guard.
    ok(host, gp, "assert(airplane_fly_to_do('Plane1', 'PointA', 30) == false)");
    CHECK(es.airplaneFlyToLeakedRequestCount() == 1);
    CHECK(es.airplaneFlyToNullTargetGuardCount() == 0);
    ok(host, gp, "airplane_fly_to_do('Plane1', nil, 30)");
    CHECK(es.airplaneFlyToNullTargetGuardCount() == 1);
    es.pathObjectIsPath().set("PathA", true);
    ok(host, gp, "assert(airplane_fly_to_do('Plane1', 'PathA', 30) == true)"); // path form: CHOSEN minimal, resolve-only

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_wave2_w27a_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_wave2_w27a_test: " << g_failures << " check(s) failed\n";
    return 1;
}
