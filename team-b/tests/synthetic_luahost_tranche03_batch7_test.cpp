// Synthetic tests for spec-lua-api-behaviour.md Sec30 ("ranking tranche
// 03", 2026-10-02), the 16 of its 25 names NOT already wired by
// tests/synthetic_luahost_tranche03_test.cpp's own 9 - see
// src/lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp (that file's own
// top comment) for the full per-function reasoning and the shared
// infrastructure (vehicle force-flag/resolver reuse from b03's own 9
// siblings, the new CAE combat-action table, the new script-request-id
// allocator, the new shared EngineState::stopVehicle()). Same convention
// as tests/synthetic_luahost_tranche03_test.cpp: a small, hand-written
// fixture (no real tagged-registration-list file or real archive read
// here).
//
// Specific focus (per this batch's own HARDENING POLICY instruction):
// vehicle_pathfind_to_do's own CONFIRMED crash-shaped edge (Sec30.5/
// Sec30.7 - "takes strlen of arg 2 before the NULL test") is exercised to
// confirm this host never dereferences a non-string Lua value as a C
// string, counted via EngineState::vehiclePathfindNonStringArg2GuardCount()
// (test-observable only, not a real engine field) rather than crashing.
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
    auto r = host.runChunk(L, code, "tranche03_batch7_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "tranche03_batch7_test.lua");
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

// The 16 (name, cluster) rows this batch registers.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"character_dont_regenerate", "gameplay"},
        {"character_clear_combat_move", "gameplay"},
        {"boss_battle_matt_set_player_as_avatar", "gameplay"},
        {"boss_battle_matt_hide_wings", "gameplay"},
        {"audio_ambient_emitter_pause", "gameplay"},
        {"audio_ambient_emitter_resume", "gameplay"},
        {"ai_suggest_action", "gameplay"},
        {"ai_do_scripted_advance", "gameplay"},
        {"vehicle_set_2d_tank_controls", "gameplay"},
        {"vehicle_rc_get_signal_strength", "gameplay"},
        {"vehicle_stop_do", "gameplay"},
        {"vehicle_pathfind_to_do", "gameplay"},
        {"vehicle_set_malfunctioning", "gameplay"},
        {"vehicle_component_detach", "gameplay"},
        {"turn_to_do", "gameplay"},
        {"teleport_vehicle_to_object", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 16 names land as real functions in gameplay, and
    // nowhere else.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec30.1 character_dont_regenerate: unresolved -> real no-op (never
    // creates a CharacterState entry); the SAME single-gate 0x008addb0
    // (variant 2) CharacterState::controlLockGatePasses already models
    // (Sec7.7/Sec7.22/Sec7.25) is reused, seeded true at construction ->
    // direct bit write; forced false -> replicate-only, no local write.
    es.objectResolves().set("dr01", false);
    ok(host, gp, "character_dont_regenerate('dr01', true)");
    CHECK(!es.hasCharacter("dr01"));

    es.objectResolves().set("dr02", true);
    CHECK(es.getOrCreateCharacter("dr02").controlLockGatePasses.get() == true);
    ok(host, gp, "character_dont_regenerate('dr02', true)");
    CHECK(es.getOrCreateCharacter("dr02").forceFlags1c98.get(CharacterState::kDontRegenHealthBit1c98) ==
          CharacterState::kDontRegenHealthBit1c98);
    CHECK(es.getOrCreateCharacter("dr02").regenRestartDeadlineStamp == -1);

    ok(host, gp, "character_dont_regenerate('dr02', false)");
    CHECK(es.getOrCreateCharacter("dr02").forceFlags1c98.get(CharacterState::kDontRegenHealthBit1c98) == 0);
    CHECK(es.getOrCreateCharacter("dr02").regenRestartDeadlineStamp > 0); // restamped to "now" (nextMonotonicStamp())

    es.getOrCreateCharacter("dr02").controlLockGatePasses.set(false);
    ok(host, gp, "character_dont_regenerate('dr02', true)"); // gate false: replicate only
    CHECK(es.getOrCreateCharacter("dr02").forceFlags1c98.get(CharacterState::kDontRegenHealthBit1c98) == 0); // unchanged

    // ------------------------------------------------------------------
    // Sec30.1 character_clear_combat_move: unresolved -> no-op; +0x1358
    // OPEN until set; CONFIRMED sentinel 0xffff -> no-op; otherwise counts.
    es.objectResolves().set("cm01", false);
    ok(host, gp, "character_clear_combat_move('cm01')");
    CHECK(!es.hasCharacter("cm01"));

    es.objectResolves().set("cm02", true);
    fails(host, gp, "character_clear_combat_move('cm02')", "+0x1358");
    es.getOrCreateCharacter("cm02").combatMoveSlot1358.set(0xffff);
    ok(host, gp, "character_clear_combat_move('cm02')");
    CHECK(es.getOrCreateCharacter("cm02").combatMoveClearedCount == 0); // sentinel -> no-op

    es.getOrCreateCharacter("cm02").combatMoveSlot1358.set(5);
    ok(host, gp, "character_clear_combat_move('cm02')");
    CHECK(es.getOrCreateCharacter("cm02").combatMoveClearedCount == 1);
    ok(host, gp, "character_clear_combat_move('cm02')");
    CHECK(es.getOrCreateCharacter("cm02").combatMoveClearedCount == 2);

    // ------------------------------------------------------------------
    // Sec30.2 boss_battle_matt_set_player_as_avatar: global bit 0268d2d0
    // written unconditionally per flag; worker gates each individually
    // OPEN; CONFIRMED false after a successful restore (not a plain
    // success flag).
    es.hasLocalPlayer().forget(); // CLOSED 2026-10-09: now CONFIRMED-true by default (Sec10.3) - force OPEN to exercise this refusal
    fails(host, gp, "boss_battle_matt_set_player_as_avatar(true)", "0x009da4e0"); // hasLocalPlayer() OPEN
    CHECK(es.bossBattleMatt().avatarModeBit == true); // written unconditionally even on the throwing path

    es.hasLocalPlayer().set(false);
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(true) == false)");
    CHECK(es.bossBattleMatt().avatarModeBit == true);

    es.hasLocalPlayer().set(true);
    fails(host, gp, "boss_battle_matt_set_player_as_avatar(true)", "npc_bahamut");
    es.bossBattleMatt().npcBahamutResolves.set(false);
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(true) == false)");
    CHECK(es.bossBattleMatt().becomeAvatarSuccessCount == 0);

    es.bossBattleMatt().npcBahamutResolves.set(true);
    fails(host, gp, "boss_battle_matt_set_player_as_avatar(true)", "avatar model resolve");
    es.bossBattleMatt().avatarModelResolves.set(false);
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(true) == false)");
    CHECK(es.bossBattleMatt().becomeAvatarSuccessCount == 0);

    es.bossBattleMatt().avatarModelResolves.set(true);
    // coopPlayerPresent (Sec28.12, reused) is now known false by default here
    // (applySpecInitialState item 13, 2026-10-09) rather than OPEN - and
    // succeeds either way: this leg's own presence question has no
    // Lua-visible effect this host can model either way (no separate failure
    // gate per spec, no distinct co-op-player target in scope), so it is
    // peeked via known()&&get(), never forced through a plain get() that
    // would otherwise block an otherwise fully-determined success path.
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(true) == true)");
    CHECK(es.bossBattleMatt().becomeAvatarSuccessCount == 1);
    CHECK(es.bossBattleMatt().hostAvatarActiveByte == false); // no co-op session installed -> coopLocalIsHost() == false

    // Known-true co-op presence also succeeds, same outcome either way.
    es.playerRig().coopPlayerPresent.set(true);
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(true) == true)");
    CHECK(es.bossBattleMatt().becomeAvatarSuccessCount == 2);

    fails(host, gp, "boss_battle_matt_set_player_as_avatar(false)", "StyleTest_PC");
    es.bossBattleMatt().styleTestPcResolves.set(false);
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(false) == true)"); // not found -> true WITHOUT restoring
    CHECK(es.bossBattleMatt().restoreSuccessCount == 0);
    CHECK(es.bossBattleMatt().avatarModeBit == false);

    es.bossBattleMatt().styleTestPcResolves.set(true);
    ok(host, gp, "assert(boss_battle_matt_set_player_as_avatar(false) == false)"); // CONFIRMED false after a successful restore
    CHECK(es.bossBattleMatt().restoreSuccessCount == 1);

    // ------------------------------------------------------------------
    // Sec30.2 boss_battle_matt_hide_wings: resolves the LITERAL "Matt",
    // not an argument; word global 0x012ec742 written hide+host only,
    // NEVER cleared on show.
    es.objectResolves().set("Matt", false);
    ok(host, gp, "boss_battle_matt_hide_wings()"); // unresolved -> no-op (default arg true)
    CHECK(es.bossBattleMatt().wingsHidden == false);

    es.objectResolves().set("Matt", true);
    ok(host, gp, "boss_battle_matt_hide_wings()"); // default true, no session -> not host
    CHECK(es.bossBattleMatt().wingsHidden == true);
    CHECK(es.bossBattleMatt().wingsHideHostMarkerSet == false);

    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    ok(host, gp, "boss_battle_matt_hide_wings(true)");
    CHECK(es.bossBattleMatt().wingsHideHostMarkerSet == true);
    ok(host, gp, "boss_battle_matt_hide_wings(false)");
    CHECK(es.bossBattleMatt().wingsHidden == false);
    CHECK(es.bossBattleMatt().wingsHideHostMarkerSet == true); // CONFIRMED: never cleared on show

    // ------------------------------------------------------------------
    // Sec30.3 audio_ambient_emitter_pause/_resume: never-populated-
    // registry convention - a never-registered name is a real no-op.
    ok(host, gp, "audio_ambient_emitter_pause('siren1')");
    CHECK(es.ambientEmitterPaused().count("siren1") == 0);

    es.registerAmbientEmitterForTesting("siren1");
    ok(host, gp, "audio_ambient_emitter_pause('siren1')");
    CHECK(es.ambientEmitterPaused().at("siren1") == true);
    ok(host, gp, "audio_ambient_emitter_resume('siren1')");
    CHECK(es.ambientEmitterPaused().at("siren1") == false);

    // ------------------------------------------------------------------
    // Sec30.4 ai_suggest_action: the 70-entry CAE table, case-insensitive;
    // "NONE"/unknown -> sentinel 71, silent no-op.
    es.objectResolves().set("ai01", false);
    ok(host, gp, "ai_suggest_action('ai01', 'move advance')");
    CHECK(!es.hasCharacter("ai01"));

    es.objectResolves().set("ai02", true);
    ok(host, gp, "ai_suggest_action('ai02', 'NONE')");
    CHECK(es.getOrCreateCharacter("ai02").caeStagedActionIndex == -1);
    CHECK(es.getOrCreateCharacter("ai02").caeSuggestCount == 0);

    ok(host, gp, "ai_suggest_action('ai02', 'bogus action name')");
    CHECK(es.getOrCreateCharacter("ai02").caeStagedActionIndex == -1);

    ok(host, gp, "ai_suggest_action('ai02', 'move advance')"); // index 18
    CHECK(es.getOrCreateCharacter("ai02").caeStagedActionIndex == 18);
    CHECK(es.getOrCreateCharacter("ai02").caeStagedTargetName.empty());
    CHECK(es.getOrCreateCharacter("ai02").caeSuggestCount == 1);

    ok(host, gp, "ai_suggest_action('ai02', 'MOVE ADVANCE')"); // case-insensitive
    CHECK(es.getOrCreateCharacter("ai02").caeSuggestCount == 2);

    es.objectResolves().set("tgt01", true);
    ok(host, gp, "ai_suggest_action('ai02', 'melee', 'tgt01')"); // index 6
    CHECK(es.getOrCreateCharacter("ai02").caeStagedActionIndex == 6);
    CHECK(es.getOrCreateCharacter("ai02").caeStagedTargetName == "tgt01");

    es.objectResolves().set("tgt02", false);
    ok(host, gp, "ai_suggest_action('ai02', 'reload', 'tgt02')"); // index 44, target unresolved -> empty stand-in
    CHECK(es.getOrCreateCharacter("ai02").caeStagedActionIndex == 44);
    CHECK(es.getOrCreateCharacter("ai02").caeStagedTargetName.empty());

    // ------------------------------------------------------------------
    // Sec30.4 ai_do_scripted_advance: no target -> OPEN 0x0053a520 gate;
    // target given -> resolves or false; always commits action 18.
    es.objectResolves().set("adv01", false);
    ok(host, gp, "assert(ai_do_scripted_advance('adv01') == false)");

    es.objectResolves().set("adv02", true);
    fails(host, gp, "ai_do_scripted_advance('adv02')", "0x0053a520");
    es.getOrCreateCharacter("adv02").caeAdvanceDestinationComputes.set(false);
    ok(host, gp, "assert(ai_do_scripted_advance('adv02') == false)");
    CHECK(es.getOrCreateCharacter("adv02").caeAdvanceCommitCount == 0);

    es.getOrCreateCharacter("adv02").caeAdvanceDestinationComputes.set(true);
    ok(host, gp, "assert(ai_do_scripted_advance('adv02') == true)");
    CHECK(es.getOrCreateCharacter("adv02").caeCommittedActionIndex == 18);
    CHECK(es.getOrCreateCharacter("adv02").caeAdvanceCommitCount == 1);

    es.objectResolves().set("advTgtBad", false);
    ok(host, gp, "assert(ai_do_scripted_advance('adv02', 'advTgtBad') == false)"); // neither resolves -> false

    es.objectResolves().set("advTgtGood", true);
    ok(host, gp, "assert(ai_do_scripted_advance('adv02', 'advTgtGood') == true)");
    CHECK(es.getOrCreateCharacter("adv02").caeCommittedTargetName == "advTgtGood");
    CHECK(es.getOrCreateCharacter("adv02").caeAdvanceCommitCount == 2);

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_set_2d_tank_controls: plain global byte 0x0278cad8.
    CHECK(es.twoDTankControlsEnabled() == false);
    ok(host, gp, "vehicle_set_2d_tank_controls(true)");
    CHECK(es.twoDTankControlsEnabled() == true);
    ok(host, gp, "vehicle_set_2d_tank_controls(false)");
    CHECK(es.twoDTankControlsEnabled() == false);

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_rc_get_signal_strength: 0.0 on resolve failure;
    // horizontal-only distance (Y dropped); `1 - d/R`, NOT clamped.
    es.objectResolves().set("rc01", false);
    es.objectResolves().set("rcc01", true);
    ok(host, gp, "assert(vehicle_rc_get_signal_strength('rc01', 'rcc01') == 0.0)");

    es.objectResolves().set("rc02", true);
    es.objectResolves().set("rcc02", false);
    ok(host, gp, "assert(vehicle_rc_get_signal_strength('rc02', 'rcc02') == 0.0)");

    es.objectResolves().set("rc03", true);
    es.objectResolves().set("rcc03", true);
    fails(host, gp, "vehicle_rc_get_signal_strength('rc03', 'rcc03')", "0x00a455a0"); // worldPosition OPEN
    es.getOrCreateVehicle("rc03").worldPosition.set(WorldPosition{0, 0, 0});
    es.getOrCreateCharacter("rcc03").worldPosition.set(WorldPosition{10, 999, 0}); // Y dropped - must not affect result
    fails(host, gp, "vehicle_rc_get_signal_strength('rc03', 'rcc03')", "0x0278cad4"); // rcSignalRange() OPEN
    es.rcSignalRange().set(20.0);
    ok(host, gp, "assert(vehicle_rc_get_signal_strength('rc03', 'rcc03') == 0.5)"); // 1 - 10/20

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_stop_do: vehicle-name path direct; via-character
    // path requires a live vehicle link; shared EngineState::stopVehicle()
    // gate (CONFIRMED-true-in-single-player; forced-false replicates only).
    es.objectResolves().set("stopveh01", true);
    ok(host, gp, "vehicle_stop_do('stopveh01')");
    CHECK(es.getOrCreateVehicle("stopveh01").stopRequestCount == 1);

    es.objectResolves().set("stopchar01", true);
    es.getOrCreateCharacter("stopchar01"); // pre-create so hasCharacter() selects the via-character path
    fails(host, gp, "vehicle_stop_do('stopchar01')", "0x16c0");
    es.getOrCreateCharacter("stopchar01").currentVehicleName.set("");
    ok(host, gp, "vehicle_stop_do('stopchar01')"); // no live vehicle -> does nothing

    es.getOrCreateCharacter("stopchar01").currentVehicleName.set("stopveh02");
    ok(host, gp, "vehicle_stop_do('stopchar01')");
    CHECK(es.getOrCreateVehicle("stopveh02").stopRequestCount == 1);

    // "authority checked on the character (if via character)" - the CHARACTER's own gate, not
    // the vehicle's; forcing the vehicle's own forceFlagGatePasses false would have no effect on
    // this path (proven implicitly: only controlLockGatePasses below gates it).
    es.getOrCreateCharacter("stopchar01").controlLockGatePasses.set(false);
    ok(host, gp, "vehicle_stop_do('stopchar01')"); // gate false: replicate only
    CHECK(es.getOrCreateVehicle("stopveh02").stopRequestCount == 1); // unchanged

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_pathfind_to_do: needs a driver (seat0Occupied,
    // reused from Sec46.4); HARDENING - a non-string/absent arg 2 is
    // guarded instead of dereferenced; OPEN 0x00b2b140 result on success.
    es.objectResolves().set("pf01", false);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf01', 'pftgt01', false, false) == false)");

    es.objectResolves().set("pf02", true);
    fails(host, gp, "vehicle_pathfind_to_do('pf02', 'pftgt01', false, false)", "seat-0");
    es.getOrCreateVehicle("pf02").seat0Occupied.set(false);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', 'pftgt01', false, false) == false)");

    es.getOrCreateVehicle("pf02").seat0Occupied.set(true);
    CHECK(es.vehiclePathfindNonStringArg2GuardCount() == 0);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', 123, false, false) == false)"); // HARDENED: non-string arg 2
    CHECK(es.vehiclePathfindNonStringArg2GuardCount() == 1);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', nil, false, false) == false)"); // HARDENED: absent/nil arg 2
    CHECK(es.vehiclePathfindNonStringArg2GuardCount() == 2);

    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', '', false, false) == false)"); // empty string: real type, no guard
    CHECK(es.vehiclePathfindNonStringArg2GuardCount() == 2);

    es.objectResolves().set("pftgtbad", false);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', 'pftgtbad', false, false) == false)");

    es.objectResolves().set("pftgtgood", true);
    fails(host, gp, "vehicle_pathfind_to_do('pf02', 'pftgtgood', false, false)", "0x00b2b140");
    es.pathfindResult().set(42.0);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', 'pftgtgood', false, false) == 42)");
    es.pathfindResult().set(0.0);
    ok(host, gp, "assert(vehicle_pathfind_to_do('pf02', 'pftgtgood', false, false) == 0)");

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_set_malfunctioning: VTOL-class-only gate (reused
    // VehicleState::flyingType); CONFIRMED asymmetric false-path (clears
    // +0xf8 bit 0x2 only).
    es.objectResolves().set("malf01", false);
    ok(host, gp, "vehicle_set_malfunctioning('malf01')");
    CHECK(!es.hasVehicle("malf01"));

    es.objectResolves().set("malf02", true);
    fails(host, gp, "vehicle_set_malfunctioning('malf02')", "0xbf4");
    es.getOrCreateVehicle("malf02").flyingType.set(2);
    ok(host, gp, "vehicle_set_malfunctioning('malf02')"); // not VTOL -> no-op
    CHECK(es.cyberM16GlitchEffectActive() == false);

    es.getOrCreateVehicle("malf02").flyingType.set(4);
    ok(host, gp, "vehicle_set_malfunctioning('malf02', true)");
    CHECK(es.localPlayerScreenDistortion1650() == 100.0);
    CHECK(es.malfunctionGlobalByte02625852() == true);
    CHECK(es.cyberM16GlitchEffectActive() == true);
    CHECK(es.getOrCreateVehicle("malf02").forceFlagsF8.get(VehicleState::kMalfunctionBit1F8 | VehicleState::kMalfunctionBit2F8) ==
          (VehicleState::kMalfunctionBit1F8 | VehicleState::kMalfunctionBit2F8));

    ok(host, gp, "vehicle_set_malfunctioning('malf02')"); // default true (arg2 omitted)
    CHECK(es.localPlayerScreenDistortion1650() == 200.0);

    ok(host, gp, "vehicle_set_malfunctioning('malf02', false)");
    CHECK(es.localPlayerScreenDistortion1650() == 0.0);
    CHECK(es.malfunctionGlobalByte02625852() == false);
    CHECK(es.cyberM16GlitchEffectActive() == false);
    CHECK(es.getOrCreateVehicle("malf02").forceFlagsF8.get(VehicleState::kMalfunctionBit2F8) == 0);
    CHECK(es.getOrCreateVehicle("malf02").forceFlagsF8.get(VehicleState::kMalfunctionBit1F8) ==
          VehicleState::kMalfunctionBit1F8); // CONFIRMED: bit 0x1 untouched by the false path

    // ------------------------------------------------------------------
    // Sec30.5 vehicle_component_detach: requires body-active (reused
    // Sec33.1's bodyActive); case-insensitive component scan; marks
    // detached only once found.
    es.objectResolves().set("comp01", false);
    ok(host, gp, "vehicle_component_detach('comp01', 'Engine')");
    CHECK(!es.hasVehicle("comp01"));

    es.objectResolves().set("comp02", true);
    fails(host, gp, "vehicle_component_detach('comp02', 'Engine')", "0xbd0");
    es.getOrCreateVehicle("comp02").bodyActive.set(false);
    ok(host, gp, "vehicle_component_detach('comp02', 'Engine')"); // not active -> no-op
    CHECK(es.getOrCreateVehicle("comp02").componentDetachAttempts == 0);

    es.getOrCreateVehicle("comp02").bodyActive.set(true);
    ok(host, gp, "vehicle_component_detach('comp02', 'Engine')"); // not in componentNames -> miss
    CHECK(es.getOrCreateVehicle("comp02").componentDetachAttempts == 0);
    CHECK(es.getOrCreateVehicle("comp02").detachedComponents.empty());

    es.getOrCreateVehicle("comp02").componentNames.insert("Engine");
    ok(host, gp, "vehicle_component_detach('comp02', 'engine')"); // case-insensitive match
    CHECK(es.getOrCreateVehicle("comp02").componentDetachAttempts == 1);
    CHECK(es.getOrCreateVehicle("comp02").detachedComponents.count("Engine") == 1);

    // ------------------------------------------------------------------
    // Sec30.6 turn_to_do: missing-either -> true; already-exactly-on-
    // target (arg3 false only) -> true; otherwise the 3 OPEN 0x00987410
    // branches in order.
    es.objectResolves().set("turnchar01", false);
    ok(host, gp, "assert(turn_to_do('turnchar01', 'turntgt01') == true)");

    es.objectResolves().set("turnchar02", true);
    es.objectResolves().set("turntgt02", true);
    fails(host, gp, "turn_to_do('turnchar02', 'turntgt02')", "0x00a455a0");

    es.getOrCreateCharacter("turnchar02").worldPosition.set(WorldPosition{1, 2, 3});
    es.getOrCreateCharacter("turntgt02").worldPosition.set(WorldPosition{1, 2, 3});
    ok(host, gp, "assert(turn_to_do('turnchar02', 'turntgt02') == true)"); // already exactly on target

    es.getOrCreateCharacter("turntgt02").worldPosition.set(WorldPosition{5, 5, 5});
    fails(host, gp, "turn_to_do('turnchar02', 'turntgt02')", "0x00987410");
    es.getOrCreateCharacter("turnchar02").turnRefusalPredicateFires.set(true);
    ok(host, gp, "assert(turn_to_do('turnchar02', 'turntgt02') == true)"); // refusal predicate fires -> true

    es.getOrCreateCharacter("turnchar02").turnRefusalPredicateFires.set(false);
    es.getOrCreateCharacter("turnchar02").turnRequestBusy.set(true); // controlLockGatePasses already seeded true
    ok(host, gp, "assert(turn_to_do('turnchar02', 'turntgt02') == false)"); // busy -> false

    es.getOrCreateCharacter("turnchar02").turnRequestBusy.set(false);
    fails(host, gp, "turn_to_do('turnchar02', 'turntgt02')", "turn-start routine");
    es.getOrCreateCharacter("turnchar02").turnStartRoutineSucceedsImmediately.set(true);
    ok(host, gp, "assert(turn_to_do('turnchar02', 'turntgt02') == true)");
    es.getOrCreateCharacter("turnchar02").turnStartRoutineSucceedsImmediately.set(false);
    ok(host, gp, "assert(turn_to_do('turnchar02', 'turntgt02') == false)");

    // arg3 true: the position-equality shortcut must NOT apply even when
    // the positions genuinely are equal.
    es.getOrCreateCharacter("turnchar02").worldPosition.set(WorldPosition{1, 2, 3});
    es.getOrCreateCharacter("turntgt02").worldPosition.set(WorldPosition{1, 2, 3});
    ok(host, gp, "assert(turn_to_do('turnchar02', 'turntgt02', true) == false)");

    // ------------------------------------------------------------------
    // Sec30.6 teleport_vehicle_to_object: target unresolved -> nothing;
    // vehicle-name path's own OPEN refusal gate; via-character path has
    // no extra gate; shares EngineState::stopVehicle() with vehicle_stop_do.
    es.objectResolves().set("tvttgt01", false);
    ok(host, gp, "teleport_vehicle_to_object('tvtveh01', 'tvttgt01')");
    CHECK(!es.hasVehicle("tvtveh01"));

    es.objectResolves().set("tvttgt02", true);
    es.objectResolves().set("tvtveh02", true);
    fails(host, gp, "teleport_vehicle_to_object('tvtveh02', 'tvttgt02')", "0xbf8");
    es.getOrCreateVehicle("tvtveh02").teleportNameGateField0xbf8a8.set(0); // >= 0 -> refused
    ok(host, gp, "teleport_vehicle_to_object('tvtveh02', 'tvttgt02')");
    CHECK(es.getOrCreateVehicle("tvtveh02").teleportRequestCount == 0);

    es.getOrCreateVehicle("tvtveh02").teleportNameGateField0xbf8a8.set(-1); // negative -> allowed
    ok(host, gp, "teleport_vehicle_to_object('tvtveh02', 'tvttgt02', 1.0, 2.0, 3.0, 90.0, false)");
    CHECK(es.getOrCreateVehicle("tvtveh02").teleportRequestCount == 1);
    CHECK(es.getOrCreateVehicle("tvtveh02").stopRequestCount == 1);
    CHECK(es.getOrCreateVehicle("tvtveh02").teleportDetachAllSeatsCount == 1);
    {
        auto& req = es.getOrCreateVehicle("tvtveh02").lastTeleportRequest;
        CHECK(req.has_value());
        CHECK(req->targetName == "tvttgt02");
        CHECK(req->offsetX == 1.0 && req->offsetY == 2.0 && req->offsetZ == 3.0);
        CHECK(req->arg7 == false);
        CHECK(req->headingRadians > 1.5707 && req->headingRadians < 1.5708); // 90deg * 0.017453
    }

    es.objectResolves().set("tvtchar01", true);
    es.getOrCreateCharacter("tvtchar01"); // pre-create so hasCharacter() selects the via-character path
    fails(host, gp, "teleport_vehicle_to_object('tvtchar01', 'tvttgt02')", "0x16c0");
    es.getOrCreateCharacter("tvtchar01").currentVehicleName.set("");
    ok(host, gp, "teleport_vehicle_to_object('tvtchar01', 'tvttgt02')"); // no live vehicle -> no-op

    es.getOrCreateCharacter("tvtchar01").currentVehicleName.set("tvtveh03");
    ok(host, gp, "teleport_vehicle_to_object('tvtchar01', 'tvttgt02')"); // via character -> no extra 0xbf8a8 gate
    CHECK(es.getOrCreateVehicle("tvtveh03").teleportRequestCount == 1);

    if (g_failures == 0)
        std::cout << "batch 2026-10-08 (Sec30 ranking tranche 03, Batch 7, 16 functions): all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
