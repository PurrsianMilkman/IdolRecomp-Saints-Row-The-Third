// Synthetic tests for the batch added resuming a session interrupted by a
// rate limit (2026-10-02): the long-standing spec-lua-api-behaviour.md
// Sec3.7/Sec20.1 vehicle-invulnerability bit-conflict resolution (settled by
// ranking tranche 14's Sec46.1: bit 0x01 of vehicle +0x1d7a, Sec3.7 right
// all along) is implemented here for the first time (previously only
// corrected in the spec text), plus a curated subset of ranking tranches
// 12-14 (Sec44/Sec45/Sec46, 75 previously-unspecced names total) - see
// src/lua_spec_confirmed_stubs.cpp's own batch header comment (right before
// stub_turn_invulnerable) for the full selection reasoning and the list of
// tranche-12/13/14 names deliberately left as ordinary generic logging
// stubs this pass. Same convention as tests/synthetic_luahost_tranche03_
// test.cpp: a small, hand-written fixture. Every name below is registered
// with its own real cluster tag, grepped directly from tools/
// lua_all_registered_1490_tagged.txt.
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
    auto r = host.runChunk(L, code, "sec44_46_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sec44_46_test.lua");
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

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"turn_invulnerable", "gameplay"},
        {"turn_vulnerable", "gameplay"},
        {"vehicle_set_vulnerable", "gameplay"},
        {"vehicle_is_invulnerable", "gameplay"},
        {"character_fake_revival_start", "gameplay"},
        {"character_fake_revival_end", "gameplay"},
        {"character_take_human_shield_check_done", "gameplay"},
        {"vehicle_lights_on", "gameplay"},
        {"vehicle_tire_indicators_alive", "gameplay"},
        {"vehicle_turret_base_to_do", "gameplay"},
        {"game_is_pc_dx11", "ui"},
        {"game_get_ps3_button_swap", "ui"},
        {"game_record_mode_is_supported", "ui"},
        {"game_record_mode_is_active", "ui"},
        {"game_show_party_ui", "ui"},
        {"game_show_community_sessions_ui", "ui"},
        {"flee_to_navpoint", "gameplay"},
        {"character_hidden", "gameplay"},
        {"ambient_gang_spawn_enable", "gameplay"},
        {"cellphone_animate_stop_do", "gameplay"},
        {"cell_camera_enable", "ui"},
        {"cell_camera_is_enabled", "ui"},
        {"ambient_cop_spawn_enable", "gameplay"},
        {"action_nodes_shouldnt_flee", "gameplay"},
        {"action_nodes_restrict_spawning", "gameplay"},
        {"whored_countdown_finished", "ui"},
        {"vehicle_set_tire_durability", "gameplay"},
        {"vehicle_set_tire_damage_multiplier", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: every name lands in its own real cluster's state only.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            lua_State* own = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            lua_State* other = rn.cluster == "ui" ? host.gameplayState() : host.uiState();
            CHECK(hasGlobal(own, rn.name.c_str()));
            CHECK(!hasGlobal(other, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // turn_invulnerable / turn_vulnerable / vehicle_set_vulnerable /
    // vehicle_is_invulnerable (Sec3.7/Sec3.8/Sec20.1/Sec46.1/Sec46.5): the
    // settled bit 0x01 ("invulnerable") / 0x02 ("always apply player
    // damage") of vehicle +0x1d7a, and the character-side mirror at
    // +0x1c98 (bits 0x20/0x200).
    //
    // Unresolved name: real no-op, no VehicleState/CharacterState entry
    // created.
    es.objectResolves().set("nobody", false);
    ok(host, gp, "turn_invulnerable('nobody', true)");
    CHECK(!es.hasCharacter("nobody") && !es.hasVehicle("nobody"));

    // A name known ONLY as a vehicle -> the vehicle branch (this project's
    // own documented character-vs-vehicle disambiguation).
    es.objectResolves().set("veh1", true);
    (void)es.getOrCreateVehicle("veh1"); // exists as a vehicle, not a character
    fails(host, gp, "turn_invulnerable('veh1', true)", "0x008ae480"); // gate OPEN -> refuses
    es.getOrCreateVehicle("veh1").forceFlagGatePasses.set(false);
    ok(host, gp, "turn_invulnerable('veh1', true)"); // gate false: replicate only
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.knownMask() == 0);
    es.vehicles().at("veh1").forceFlagGatePasses.set(true);
    ok(host, gp, "turn_invulnerable('veh1', true)");
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) ==
          VehicleState::kInvulnerableBit1d7a);
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.get(VehicleState::kAlwaysApplyPlayerDamageBit1d7a) ==
          VehicleState::kAlwaysApplyPlayerDamageBit1d7a);
    ok(host, gp, "assert(vehicle_is_invulnerable('veh1') == true)");

    // vehicle_set_vulnerable: CONFIRMED only touches bit 0x01 (the
    // companion "always apply player damage" bit is left untouched, unlike
    // turn_vulnerable below).
    ok(host, gp, "vehicle_set_vulnerable('veh1')");
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) == 0);
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.get(VehicleState::kAlwaysApplyPlayerDamageBit1d7a) ==
          VehicleState::kAlwaysApplyPlayerDamageBit1d7a); // untouched - still set from above
    ok(host, gp, "assert(vehicle_is_invulnerable('veh1') == false)");

    // turn_vulnerable: clears BOTH bits, hardcoded, no second argument read.
    es.vehicles().at("veh1").forceFlags1d7a.setBits(VehicleState::kInvulnerableBit1d7a,
                                                    VehicleState::kInvulnerableBit1d7a);
    ok(host, gp, "turn_vulnerable('veh1')");
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) == 0);
    CHECK(es.vehicles().at("veh1").forceFlags1d7a.get(VehicleState::kAlwaysApplyPlayerDamageBit1d7a) == 0);

    // A name known as a character (created via getOrCreateCharacter) ->
    // the character branch, even though turn_invulnerable's own real
    // resolution order tries character paths first.
    es.objectResolves().set("npc1", true);
    es.getOrCreateCharacter("npc1").forceFlagGatePasses.set(true);
    ok(host, gp, "turn_invulnerable('npc1', false)");
    CHECK(!es.hasVehicle("npc1"));
    CHECK(es.getOrCreateCharacter("npc1").forceFlags1c98.get(CharacterState::kInvulnerableBit1c98) ==
          CharacterState::kInvulnerableBit1c98);
    CHECK(es.getOrCreateCharacter("npc1").forceFlags1c98.get(CharacterState::kAlwaysApplyPlayerDamageBit1c98) == 0);
    ok(host, gp, "turn_vulnerable('npc1')");
    CHECK(es.getOrCreateCharacter("npc1").forceFlags1c98.get(CharacterState::kInvulnerableBit1c98) == 0);

    // ------------------------------------------------------------------
    // character_fake_revival_start/_end (Sec45.1): +0xe4 bit 0x10000.
    // Unresolved: real no-op (flagged null crash, not reproduced).
    es.objectResolves().set("ghost", false);
    ok(host, gp, "character_fake_revival_start('ghost')");
    CHECK(!es.hasCharacter("ghost"));
    ok(host, gp, "character_fake_revival_end('ghost')");
    CHECK(!es.hasCharacter("ghost"));

    es.objectResolves().set("npc2", true);
    ok(host, gp, "character_fake_revival_start('npc2')");
    CHECK(es.getOrCreateCharacter("npc2").flagsE4.get(CharacterState::kFakeRevivalInProgressBit) ==
          CharacterState::kFakeRevivalInProgressBit);
    ok(host, gp, "character_fake_revival_end('npc2')");
    CHECK(es.getOrCreateCharacter("npc2").flagsE4.get(CharacterState::kFakeRevivalInProgressBit) == 0);

    // ------------------------------------------------------------------
    // character_take_human_shield_check_done (Sec45.1): only the first
    // (boolean) return value is modeled.
    es.objectResolves().set("taker", false);
    ok(host, gp, "assert(character_take_human_shield_check_done('taker', 'hostage') == false)");
    es.objectResolves().set("taker2", true);
    es.getOrCreateCharacter("taker2").humanShieldHostageName = "hostage";
    ok(host, gp, "assert(character_take_human_shield_check_done('taker2', 'hostage') == true)");
    ok(host, gp, "assert(character_take_human_shield_check_done('taker2', 'someone_else') == false)");
    ok(host, gp, "assert(character_take_human_shield_check_done('taker2', '') == false)"); // vacuous "" == "" excluded

    // ------------------------------------------------------------------
    // vehicle_lights_on (Sec46.5): the naming trap - false clears BOTH
    // flags (automatic), not "off."
    es.objectResolves().set("veh2", true);
    ok(host, gp, "vehicle_lights_on('veh2', true)");
    CHECK(es.vehicles().at("veh2").lightsForceFlags.get(VehicleState::kLightsForceOnBit) ==
          VehicleState::kLightsForceOnBit);
    CHECK(es.vehicles().at("veh2").lightsForceFlags.get(VehicleState::kLightsForceOffBit) == 0);
    ok(host, gp, "vehicle_lights_on('veh2', false)");
    CHECK(es.vehicles().at("veh2").lightsForceFlags.get(VehicleState::kLightsForceOnBit) == 0);
    CHECK(es.vehicles().at("veh2").lightsForceFlags.get(VehicleState::kLightsForceOffBit) == 0);

    // ------------------------------------------------------------------
    // vehicle_tire_indicators_alive (Sec46.5): disabled -> 8 ("all
    // alive," the naming trap); not disabled -> refused (no per-wheel
    // physics pool); unresolved -> this project's own 0 default.
    es.objectResolves().set("veh3", true);
    es.getOrCreateVehicle("veh3").tireIndicatorObjectDisabled.set(true);
    ok(host, gp, "assert(vehicle_tire_indicators_alive('veh3') == 8)");
    es.vehicles().at("veh3").tireIndicatorObjectDisabled.set(false);
    fails(host, gp, "vehicle_tire_indicators_alive('veh3')", "Sec46.5");
    es.objectResolves().set("veh4", false);
    ok(host, gp, "assert(vehicle_tire_indicators_alive('veh4') == 0)");

    // ------------------------------------------------------------------
    // vehicle_turret_base_to_do (Sec46.4): boolean false on every failure
    // shape this project models; the real "attempt" progress code (no
    // path/node graph here) is refused.
    es.objectResolves().set("veh5", false);
    ok(host, gp, "assert(vehicle_turret_base_to_do('veh5', 'target', true) == false)");
    es.objectResolves().set("veh6", true);
    es.getOrCreateVehicle("veh6").seat0Occupied.set(false);
    ok(host, gp, "assert(vehicle_turret_base_to_do('veh6', 'target', true) == false)");
    es.vehicles().at("veh6").seat0Occupied.set(true);
    ok(host, gp, "assert(vehicle_turret_base_to_do('veh6', nil, true) == false)"); // nil target: flagged crash, not reproduced
    fails(host, gp, "vehicle_turret_base_to_do('veh6', 'a_path', true)", "Sec46.4");

    // ------------------------------------------------------------------
    // The 6 trivial Sec44.1 names: 4 constant-false, 2 confirmed no-ops.
    ok(host, ui, "assert(game_is_pc_dx11() == false)");
    ok(host, ui, "assert(game_get_ps3_button_swap() == false)");
    ok(host, ui, "assert(game_record_mode_is_supported() == false)");
    ok(host, ui, "assert(game_record_mode_is_active() == false)");
    ok(host, ui, "game_show_party_ui()");
    ok(host, ui, "game_show_community_sessions_ui()");

    // ------------------------------------------------------------------
    // flee_to_navpoint (Sec44.5/Sec44.6): gated on resolved, not
    // "#PLAYER#", not dead.
    CHECK(es.getOrCreateCharacter("fleeing1").fleeToNavpointRequestCount == 0);
    es.objectResolves().set("fleeing1", true);
    ok(host, gp, "flee_to_navpoint('fleeing1')");
    CHECK(es.getOrCreateCharacter("fleeing1").fleeToNavpointRequestCount == 1);
    es.getOrCreateCharacter("fleeing1").isDeadHighConfidence = true;
    ok(host, gp, "flee_to_navpoint('fleeing1')");
    CHECK(es.getOrCreateCharacter("fleeing1").fleeToNavpointRequestCount == 1); // dead: no increment
    ok(host, gp, "flee_to_navpoint('#PLAYER#')"); // CONFIRMED: never accepted for the fleeing character
    CHECK(!es.hasCharacter("#PLAYER#"));

    // ------------------------------------------------------------------
    // character_hidden (Sec45.1): the one inverted-polarity unresolved
    // default in this file (unresolved -> true, not false).
    es.objectResolves().set("hideme", false);
    ok(host, gp, "assert(character_hidden('hideme') == true)");
    es.objectResolves().set("hideme2", true);
    es.getOrCreateCharacter("hideme2").hiddenFlag.set(false);
    ok(host, gp, "assert(character_hidden('hideme2') == false)");
    es.getOrCreateCharacter("hideme2").hiddenFlag.set(true);
    ok(host, gp, "assert(character_hidden('hideme2') == true)");

    // ------------------------------------------------------------------
    // Simple global-toggle round trips (Sec45.4/Sec45.6/Sec46.3).
    ok(host, gp, "ambient_gang_spawn_enable(true)");
    CHECK(es.ambientGangSpawnEnabled().get() == true);
    ok(host, gp, "cellphone_animate_stop_do()"); // confirmed no-op
    ok(host, ui, "cell_camera_enable(true)");
    CHECK(es.cellCameraEnabled().get() == true);
    ok(host, ui, "assert(cell_camera_is_enabled() == true)");
    ok(host, gp, "ambient_cop_spawn_enable(true)");
    CHECK(es.ambientCopSpawnEnabled().get() == true);
    ok(host, gp, "action_nodes_shouldnt_flee(true)");
    CHECK(es.actionNodesShouldntFlee().get() == true);
    ok(host, gp, "action_nodes_restrict_spawning(false)");
    CHECK(es.actionNodesRestrictSpawning().get() == false);

    // whored_countdown_finished: confirmed no local effect (no reader
    // anywhere) - just must not crash.
    ok(host, ui, "whored_countdown_finished()");

    // ------------------------------------------------------------------
    // vehicle_set_tire_durability / vehicle_set_tire_damage_multiplier
    // (Sec46.5): <= 0 silently ignored; unresolved name -> no local
    // VehicleState entry created at all (replicate-only, no local
    // fallback).
    es.objectResolves().set("veh7", true);
    ok(host, gp, "vehicle_set_tire_durability('veh7', -5)");
    CHECK(!es.hasVehicle("veh7")); // ignored value: no VehicleState entry created at all
    ok(host, gp, "vehicle_set_tire_durability('veh7', 50)");
    CHECK(es.vehicles().at("veh7").tireDurability.get() == 50.0);
    ok(host, gp, "vehicle_set_tire_damage_multiplier('veh7', 2.5)");
    CHECK(es.vehicles().at("veh7").tireDamageMultiplier.get() == 2.5);

    es.objectResolves().set("veh8", false);
    ok(host, gp, "vehicle_set_tire_durability('veh8', 50)");
    CHECK(!es.hasVehicle("veh8"));

    if (g_failures == 0)
        std::cout << "batch 2026-10-02 (resumed session, Sec3.7/Sec20.1/Sec46.1 correction + "
                     "Sec44/Sec45/Sec46 subset, 28 functions): all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
