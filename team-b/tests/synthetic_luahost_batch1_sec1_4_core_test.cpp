// Synthetic tests for wave 1 batch 1 (2026-10-08): spec-lua-api-behaviour.md
// §1-4 (trigger/mission/teleport/combat-hook core) - objective_text (§1.3),
// on_trigger_exit (§1.7), spawn_region_enable (§1.8), door_open (§1.10),
// audio_conversation_play (§2.10), game_peg_unload (§2.7), pop_screen
// (§2.8), object_is_in_trigger (§1.12), ai_do_scripted_move (§3.11),
// on_attack_performed (§3.12), teleport (§4.3), release_to_world (§4.4),
// vehicle_suppress_npc_exit (§4.5), set_player_can_enter_exit_vehicles
// (§4.6), continuous_spawn_stop (§4.7). See
// src/lua_spec_confirmed_stubs_b08_batch1_core.cpp's own top comment for
// the 6 names deferred from the original 21-name batch assignment (each
// still carrying its own un-superseded "NOT yet cleared for implementation"
// Review-status line) and for each implemented stub's own per-function
// citation. Same fixture convention as
// tests/synthetic_luahost_vehicle_chase_test.cpp; real cluster tags
// verified directly against tools/lua_all_registered_1490_tagged.txt
// (grepped, not assumed) - all 15 are "gameplay" except game_peg_unload and
// pop_screen ("ui").
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
    auto r = host.runChunk(L, code, "batch1_sec1_4_core_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "batch1_sec1_4_core_test.lua");
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
        {"objective_text", "gameplay"},
        {"on_trigger_exit", "gameplay"},
        {"spawn_region_enable", "gameplay"},
        {"door_open", "gameplay"},
        {"audio_conversation_play", "gameplay"},
        {"game_peg_unload", "ui"},
        {"pop_screen", "ui"},
        {"object_is_in_trigger", "gameplay"},
        {"ai_do_scripted_move", "gameplay"},
        {"on_attack_performed", "gameplay"},
        {"teleport", "gameplay"},
        {"release_to_world", "gameplay"},
        {"vehicle_suppress_npc_exit", "gameplay"},
        {"set_player_can_enter_exit_vehicles", "gameplay"},
        {"continuous_spawn_stop", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: 13 gameplay-only, 2 ui-only (game_peg_unload, pop_screen).
    {
        Host host(fixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "objective_text"));
        CHECK(hasGlobal(host.gameplayState(), "teleport"));
        CHECK(hasGlobal(host.uiState(), "game_peg_unload"));
        CHECK(!hasGlobal(host.gameplayState(), "game_peg_unload"));
        CHECK(hasGlobal(host.uiState(), "pop_screen"));
        CHECK(!hasGlobal(host.gameplayState(), "pop_screen"));
        CHECK(!hasGlobal(host.uiState(), "teleport"));
    }

    // --- objective_text (§1.3) -----------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "objective_text(0, 'KEY_A', 'aux1', 'aux2', 1, 2)");
        auto& slot = es.objectiveTextSlots()[0];
        CHECK(slot.rawTextKey == "KEY_A");
        CHECK(slot.combinedAuxText == "aux1/aux2"); // CONFIRMED "%s/%s" when arg4 present+non-empty
        CHECK(slot.flag5 == 1);
        CHECK(slot.flag6 == 2);
        CHECK(slot.writeCount == 1);

        ok(host, gp, "objective_text(1, 'KEY_B', 'aux3')"); // arg4 absent -> arg3 verbatim, defaults 3/0
        auto& slot1 = es.objectiveTextSlots()[1];
        CHECK(slot1.combinedAuxText == "aux3");
        CHECK(slot1.flag5 == 3);
        CHECK(slot1.flag6 == 0);

        ok(host, gp, "objective_text(1, 'KEY_B', 'aux3', '')"); // arg4 present but EMPTY -> still verbatim arg3
        CHECK(es.objectiveTextSlots()[1].combinedAuxText == "aux3");

        // Out-of-range slot (>= 4) is a silent no-op (matches the real
        // engine's own CONFIRMED "< 4" bound check).
        ok(host, gp, "objective_text(4, 'KEY_C')");
        CHECK(es.objectiveTextSlots().count(4) == 0);
        ok(host, gp, "objective_text(-1, 'KEY_D')");
        CHECK(es.objectiveTextSlots().count(-1) == 0);
    }

    // --- on_trigger_exit (§1.7) -----------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "on_trigger_exit('cb', 'NoTrigger')", "named-object resolution");
        es.objectResolves().set("Trig1", true);
        ok(host, gp, "on_trigger_exit('OnExitCb', 'Trig1')");
        CHECK(es.onTriggerExitCallbackKnown("Trig1"));
        CHECK(es.onTriggerExitCallbackValue("Trig1") == "OnExitCb");
        // on_trigger_exit's own slot (+0xfc) is independent of on_trigger's
        // enter slot (+0xf8) - not re-exercised here since on_trigger is
        // registered in b01/b07, not this fixture.
    }

    // --- spawn_region_enable (§1.8) -------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Region1", true);
        ok(host, gp, "spawn_region_enable('Region1', true)");
        CHECK(es.spawnRegionEnabled().at("Region1") == true);
        ok(host, gp, "spawn_region_enable('Region1', false)");
        CHECK(es.spawnRegionEnabled().at("Region1") == false);
        // Unresolved region: a silent no-op, no refusal (CONFIRMED - both
        // args read unconditionally, but the write itself is gated).
        es.objectResolves().set("Region2", false);
        ok(host, gp, "spawn_region_enable('Region2', true)");
        CHECK(es.spawnRegionEnabled().count("Region2") == 0);
    }

    // --- door_open (§1.10) -----------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Unresolved door -> push false, no refusal.
        es.objectResolves().set("NoDoor", false);
        {
            auto r = host.runChunk(gp, "return door_open('NoDoor')", "door_open_test.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // Resolved door, arg4 absent -> simple path, pushes true.
        es.objectResolves().set("Door1", true);
        ok(host, gp, "local r = door_open('Door1'); assert(r == true)");
        CHECK(es.getOrCreatePlacedObject("Door1").simpleOpenCount == 1);

        // Rich path: arg4 present but anchor OPEN -> refuses.
        es.objectResolves().set("Char1", true);
        refuses(host, gp, "door_open('Door1', false, true, 'Char1', 'AnimA')",
                "door +0xc8 interaction-anchor sub-object present");

        // Anchor present -> rich path applies, pushes true.
        es.getOrCreatePlacedObject("Door1").hasInteractionAnchor.set(true);
        ok(host, gp, "local r = door_open('Door1', false, true, 'Char1', 'AnimA'); assert(r == true)");
        PlacedObjectState& door1 = es.getOrCreatePlacedObject("Door1");
        CHECK(door1.lastInteractionCharacterName == "Char1");
        CHECK(door1.lastInteractionAnimStateName == "AnimA");
        CHECK(door1.lastInteractionTargetName.empty());
        CHECK(door1.richPathAppliedCount == 1);

        // Arg4 present but doesn't resolve -> pushes false.
        es.objectResolves().set("GhostChar", false);
        ok(host, gp, "local r = door_open('Door1', false, true, 'GhostChar'); assert(r == false)");
    }

    // --- audio_conversation_play (§2.10) --------------------------------
    // currentResolves is an OpenValue - calling .get() while OPEN throws,
    // so a test must configure it (true or false) before the stub's own
    // unconditional read of it succeeds.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.conversationPlay().currentResolves.set(false);
        ok(host, gp, "audio_conversation_play(5)");
        CHECK(es.conversationPlay().directDispatchCount == 0);
        CHECK(es.conversationPlay().interruptCount == 0);

        es.conversationPlay().currentResolves.set(true);
        ok(host, gp, "audio_conversation_play(7)");
        CHECK(es.conversationPlay().directDispatchCount == 1);
        CHECK(es.conversationPlay().alreadyActiveFlag == true);
        CHECK(es.conversationPlay().interruptCount == 0); // first mark, wasn't already active

        ok(host, gp, "audio_conversation_play(9)");
        CHECK(es.conversationPlay().directDispatchCount == 2);
        CHECK(es.conversationPlay().interruptCount == 1); // already active this time -> interrupt fires
    }

    // --- game_peg_unload (§2.7) ------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        ok(host, ui, "game_peg_unload()"); // no args -> silent no-op
        ok(host, ui, "game_peg_unload('never_loaded.peg_pc')"); // never configured -> silent no-op
        CHECK(es.pegUnloadCount("never_loaded.peg_pc") == 0);

        es.pegInLoadedState().set("loaded.peg_pc", true);
        ok(host, ui, "game_peg_unload('loaded.peg_pc')");
        CHECK(es.pegUnloadCount("loaded.peg_pc") == 1);

        es.pegInLoadedState().set("not_loaded.peg_pc", false);
        ok(host, ui, "game_peg_unload('not_loaded.peg_pc')");
        CHECK(es.pegUnloadCount("not_loaded.peg_pc") == 0);
    }

    // --- pop_screen (§2.8) -------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        CHECK(es.uiScreenStackDepth() == 0); // CHOSEN starting depth
        ok(host, ui, "pop_screen()"); // empty stack -> no-op
        CHECK(es.popScreenCount() == 0);

        es.uiScreenStackDepth() = 2;
        ok(host, ui, "pop_screen()");
        CHECK(es.uiScreenStackDepth() == 1);
        CHECK(es.popScreenCount() == 1);
        ok(host, ui, "pop_screen()");
        CHECK(es.uiScreenStackDepth() == 0);
        CHECK(es.popScreenCount() == 2);
        ok(host, ui, "pop_screen()"); // now empty again
        CHECK(es.popScreenCount() == 2);
    }

    // --- object_is_in_trigger (§1.12) -------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "object_is_in_trigger('NoTrig', 'NoObj')", "named-object resolution");

        es.objectResolves().set("Trig1", true);
        es.objectResolves().set("Obj1", false); // resolved name-space but resolve fails
        ok(host, gp, "local r = object_is_in_trigger('Trig1', 'Obj1'); assert(r == false)");

        es.objectResolves().set("Obj2", true);
        es.triggerContainsObject().set(EngineState::triggerContainsObjectKey("Trig1", "Obj2"), true);
        ok(host, gp, "local r = object_is_in_trigger('Trig1', 'Obj2'); assert(r == true)");

        es.objectResolves().set("Obj3", true);
        refuses(host, gp, "object_is_in_trigger('Trig1', 'Obj3')", "containment test result");
    }

    // --- ai_do_scripted_move (§3.11) --------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Arg 1 fails to resolve -> pushes false (the ONLY false-pushing
        // path, CONFIRMED).
        es.objectResolves().set("NoChar", false);
        ok(host, gp, "local r = ai_do_scripted_move('NoChar', 'Dest1'); assert(r == false)");

        // Arg 1 resolves, dest does not -> STILL pushes true, no dispatch.
        es.objectResolves().set("Char1", true);
        es.objectResolves().set("BadDest", false);
        ok(host, gp, "local r = ai_do_scripted_move('Char1', 'BadDest'); assert(r == true)");
        CharacterState& c1 = es.getOrCreateCharacter("Char1");
        CHECK(c1.scriptedMoveDispatchCount == 0);

        // Both resolve -> dispatch, flags merged (defaults true/true -> 0x0C).
        es.objectResolves().set("Dest1", true);
        ok(host, gp, "local r = ai_do_scripted_move('Char1', 'Dest1'); assert(r == true)");
        CHECK(c1.scriptedMoveDispatchCount == 1);
        CHECK(c1.pendingScriptedMoveDestination == "Dest1");
        CHECK(c1.pendingScriptedMoveFlags.get(0x0Cu) == 0x0Cu);

        ok(host, gp, "ai_do_scripted_move('Char1', 'Dest1', false, true)");
        CHECK(c1.pendingScriptedMoveFlags.get(0x0Cu) == 0x08u); // arg3 false -> bit 0x04 clear, arg4 true -> bit 0x08 set
    }

    // --- on_attack_performed (§3.12) --------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        es.playerClassChainResolves().set("#PLAYER#", true);
        ok(host, gp, "on_attack_performed('CbGeneric', '#PLAYER#')");
        CHECK(es.getOrCreateCharacter("#PLAYER#").onAttackPerformedGenericSlotCallback == "CbGeneric");
        CHECK(es.getOrCreateCharacter("#PLAYER#").onAttackPerformedCharacterSlotCallback.empty());

        es.playerClassChainResolves().set("Npc1", false);
        es.objectResolves().set("Npc1", true);
        ok(host, gp, "on_attack_performed('CbChar', 'Npc1')");
        CHECK(es.getOrCreateCharacter("Npc1").onAttackPerformedCharacterSlotCallback == "CbChar");
        CHECK(es.getOrCreateCharacter("Npc1").onAttackPerformedGenericSlotCallback.empty());
    }

    // --- teleport (§4.3) ---------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Dest fails -> no request allocated at all.
        es.objectResolves().set("BadDest", false);
        es.objectResolves().set("Mover1", true);
        ok(host, gp, "teleport('Mover1', 'BadDest')");
        CHECK(es.teleportRequests().count("Mover1") == 0);
        CHECK(es.teleportScriptedRequestCount() == 0);

        // Both resolve, non-player path -> arg3 NOT forwarded.
        es.objectResolves().set("Dest1", true);
        es.playerClassChainResolves().set("Mover1", false);
        ok(host, gp, "teleport('Mover1', 'Dest1', true, false, 1.0, 2.0, 3.0, false)");
        TeleportRequest& r1 = es.teleportRequests().at("Mover1");
        CHECK(r1.destinationName == "Dest1");
        CHECK(r1.offsetX == 1.0 && r1.offsetY == 2.0 && r1.offsetZ == 3.0);
        CHECK(r1.arg4 == false);
        CHECK(r1.arg8 == false);
        CHECK(r1.tookPlayerPath == false);
        CHECK(r1.exitVehiclesArg3 == false); // CONFIRMED: never forwarded on the non-player path
        CHECK(es.teleportScriptedRequestCount() == 1);

        // Player path -> arg3 forwarded.
        es.objectResolves().set("#PLAYER#", true);
        es.playerClassChainResolves().set("#PLAYER#", true);
        ok(host, gp, "teleport('#PLAYER#', 'Dest1', true)");
        TeleportRequest& r2 = es.teleportRequests().at("#PLAYER#");
        CHECK(r2.tookPlayerPath == true);
        CHECK(r2.exitVehiclesArg3 == true);
    }

    // --- release_to_world (§4.4) --------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        refuses(host, gp, "release_to_world('NoObj')", "named-object resolution");

        // Tier 2/3 (character/vehicle): direct release.
        es.objectResolves().set("Char1", true);
        es.releaseToWorldKind().set("Char1", 2);
        ok(host, gp, "release_to_world('Char1')");
        CHECK(es.releasedCount().at("Char1") == 1);

        // Tier 1 (group): releases every pre-filtered active member.
        es.objectResolves().set("Group1", true);
        es.releaseToWorldKind().set("Group1", 1);
        es.releaseToWorldGroupMembers()["Group1"] = {"MemberA", "MemberB"};
        ok(host, gp, "release_to_world('Group1')");
        CHECK(es.releasedCount().at("MemberA") == 1);
        CHECK(es.releasedCount().at("MemberB") == 1);
        CHECK(es.releasedCount().count("Group1") == 0); // the group itself is never "released", only its members
    }

    // --- vehicle_suppress_npc_exit (§4.5) -----------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car1", true);
        ok(host, gp, "vehicle_suppress_npc_exit('Car1')"); // default true
        VehicleState& v1 = es.getOrCreateVehicle("Car1");
        uint32_t mask = VehicleState::kNoStopEnterWithDriverBit | VehicleState::kForceNoPassengerExitBit;
        CHECK(v1.vehicleAiForceFlagsByte4.get(mask) == mask); // BOTH bits set together

        ok(host, gp, "vehicle_suppress_npc_exit('Car1', false)");
        CHECK(v1.vehicleAiForceFlagsByte4.get(mask) == 0u); // BOTH bits cleared together
    }

    // --- set_player_can_enter_exit_vehicles (§4.6) --------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("#PLAYER#", true);
        ok(host, gp, "set_player_can_enter_exit_vehicles('#PLAYER#', false)"); // "cannot" enter/exit
        CharacterState& p = es.getOrCreateCharacter("#PLAYER#");
        CHECK(p.flags28ac.get(CharacterState::kCantEnterExitVehiclesBit28ac) == CharacterState::kCantEnterExitVehiclesBit28ac);

        ok(host, gp, "set_player_can_enter_exit_vehicles('#PLAYER#')"); // default true ("can") -> bit clear
        CHECK(p.flags28ac.get(CharacterState::kCantEnterExitVehiclesBit28ac) == 0u);
    }

    // --- continuous_spawn_stop (§4.7) ---------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Region1", true);

        // Never marked active by any in-scope native -> unconditional
        // reset happens, but the boolean is never consulted.
        ok(host, gp, "continuous_spawn_stop('Region1', true)");
        CHECK(es.continuousSpawnStopCount().at("Region1") == 1);
        CHECK(es.continuousSpawnLastStopBoolean().count("Region1") == 0);
        CHECK(es.continuousSpawnActive().at("Region1") == false);

        // Test marks it active directly -> boolean now recorded, then reset.
        es.continuousSpawnActive()["Region1"] = true;
        ok(host, gp, "continuous_spawn_stop('Region1', true)");
        CHECK(es.continuousSpawnLastStopBoolean().at("Region1") == true);
        CHECK(es.continuousSpawnActive().at("Region1") == false); // unconditional reset still applies
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_batch1_sec1_4_core_test: all checks passed\n";
    return 0;
}
