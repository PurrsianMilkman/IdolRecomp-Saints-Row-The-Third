// lua_spec_confirmed_stubs_b05_invuln_r15_23.cpp
//
// The Sec3.7/Sec20.1/Sec46.1 vehicle-invulnerability bit-conflict resolution + a curated subset of ranking tranches 12-14 (Sec44/45/46) + ranking tranches 15/17/20/22/23 real-hit batch + the action_play_do/action_play_is_finished/get_dist/player_names_get_all real-hit probe-mode additions.
//
// One of 7 files split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp (a pure structural refactor, zero behaviour
// change - every stub body, comment and citation below is reproduced
// verbatim from that file). See lua_spec_confirmed_stubs_registry.cpp's own
// top comment for the full split rationale and the shared-registration
// mechanism (lua_spec_confirmed_stubs_sections.h/_common.h).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Batch 2026-10-02 (resumed session): spec-lua-api-behaviour.md Sec3.7/
// Sec20.1's vehicle-invulnerability bit conflict, now RESOLVED by ranking
// tranche 14 (Sec46.1: bit 0x01 of vehicle +0x1d7a, Sec3.7 right all along,
// Sec20.1's own prior "bit 0x8" was the error) - turn_invulnerable,
// turn_vulnerable, vehicle_set_vulnerable, vehicle_is_invulnerable - plus a
// curated subset of ranking tranches 12-14 (Sec44/Sec45/Sec46) themselves:
// the 2 siblings this correction directly touches' own supporting state
// (character_fake_revival_start/_end, character_take_human_shield_check_
// done, vehicle_turret_base_to_do, vehicle_lights_on, vehicle_tire_
// indicators_alive - all already scaffolded in engine_state.h before this
// session was interrupted), plus a further batch chosen the same way
// Sec33's own batch header (above) explains: a real call-count check
// (results/stub_ranking_with_specced_20261002.tsv, 70 rows, every row with
// >= 1 real measured call) finds ZERO of tranches 12/13/14's 75 names in
// it at all - no real-call-volume signal exists to prioritize BY within
// these tranches (matching their own source line: "Team B call-count
// order," ranks 226-300 of 554 - simply low in the overall static-call
// ranking, not specifically exercised by this trace's own missions either)
// - so, as Sec30/Sec33 both already precedent, breadth is applied instead:
// every remaining name in this batch is one Sec44/Sec45/Sec46 names
// EXPLICITLY enough to implement without inventing a new subsystem this
// project has nowhere else built (no networking, no Steam, no path/node
// graph, no cellphone activity-record state machine, no key-binding
// table). Named tranche-12/13/14 functions NOT in this batch (fov_check_
// xz_plane - ambiguous geometry-argument shape beyond "arg 3 is a half-
// angle"; the Steam/co-op/lobby/machinima/dialog/cinema-editor/cellphone_
// dial/boss-battle/action-sequence/world-despawn/ai_force_team_idle/
// customization_swap_player_rig/debris_flow_set_inactive/camera_script_*/
// cutscene_*/flashpoint_mission_status/autil_hud_mayhem_init/vehicle_set_
// npc_engine_audio/vehicle_set_kneecappers_damage/vint_options_remap_
// reset_bindings families) are deliberately left as ordinary generic
// logging stubs this pass - each would need either real networking/Steam/
// UI-pool/path-graph modeling this project does not build, or (the last
// two vehicle setters) a Lua argument TYPE the spec prose does not state -
// documented here rather than guessed, per this project's own no-invented-
// fixes/skip-and-revisit convention, for a future pass to pick up.
// =======================================================================

// Shared body for turn_invulnerable/turn_vulnerable (Sec3.7/Sec3.8):
// resolve via objectResolves() (this project's one shared name-resolution
// map, Sec29 - see engine_state.h's own top note on why there is no
// separate per-kind resolver), then write BOTH the "invulnerable" and
// "always apply player damage" bits via the SAME double-gate idiom
// (CharacterState::forceFlagGatePasses / VehicleState::forceFlagGatePasses),
// on whichever kind the name resolves to. CONFIRMED (Sec3.7's own
// single-player resolution, 2026-10-03): the gate always passes with no
// session installed, so every character/vehicle takes the direct-flip
// branch from construction. CONFIRMED (Sec3.7): the real
// function's own resolution chain tries several DIFFERENT mechanisms in
// sequence (the generic/"#PLAYER#"/"#FOLLOWER#" chain, then a plain
// character resolver, then a vehicle resolver) that this project's single
// flat objectResolves() map cannot distinguish by itself - this project's
// own CHOSEN, explicitly-stated disambiguation (NOT a spec fact): if a
// VehicleState entry already exists for `targetName` and no CharacterState
// entry does, treat it as a vehicle; otherwise default to character,
// matching the real chain's own order (the character-oriented paths are
// tried FIRST, so character wins when a test has set up neither or both).
void applyInvulnerabilityFlags(lua_State* L, const char* name, const std::string& targetName, bool invulnerable,
                                bool alwaysApplyPlayerDamage) {
    EngineState* st = upState(L);
    if (!st->objectResolves().get(targetName)) return; // OPEN until set
    bool treatAsVehicle = st->hasVehicle(targetName) && !st->hasCharacter(targetName);
    if (treatAsVehicle) {
        VehicleState& vehicle = st->getOrCreateVehicle(targetName);
        if (vehicle.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            vehicle.forceFlags1d7a.setBits(VehicleState::kInvulnerableBit1d7a,
                                           invulnerable ? VehicleState::kInvulnerableBit1d7a : 0u);
            vehicle.forceFlags1d7a.setBits(VehicleState::kAlwaysApplyPlayerDamageBit1d7a,
                                           alwaysApplyPlayerDamage ? VehicleState::kAlwaysApplyPlayerDamageBit1d7a : 0u);
        } else {
            EngineState::replicateStateChange(name, targetName);
        }
    } else {
        CharacterState& character = st->getOrCreateCharacter(targetName);
        if (character.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            character.forceFlags1c98.setBits(CharacterState::kInvulnerableBit1c98,
                                             invulnerable ? CharacterState::kInvulnerableBit1c98 : 0u);
            character.forceFlags1c98.setBits(CharacterState::kAlwaysApplyPlayerDamageBit1c98,
                                             alwaysApplyPlayerDamage ? CharacterState::kAlwaysApplyPlayerDamageBit1c98
                                                                     : 0u);
        } else {
            EngineState::replicateStateChange(name, targetName);
        }
    }
}

// ---------------------------------------------------------------------
// turn_invulnerable (Sec3.7, 0x00a61b80) - 222 calls / 33 scripts (real
// static-call ranking, distinct from this batch's own zero-mission-drive-
// hits finding above). Arg 1 mandatory string; arg 2 optional boolean,
// nil-gated, default false. No return. CONFIRMED: sets the invulnerable
// bit to TRUE (hardcoded, not tied to any argument) and the companion
// "always apply player damage" bit to arg 2, via the double-gate idiom
// above, on whichever kind resolves.
// ---------------------------------------------------------------------
int stub_turn_invulnerable(lua_State* L) {
    logCall(L, upLog(L), "turn_invulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    bool alwaysApplyPlayerDamage = optionalBoolDefault(L, 2, false);
    applyInvulnerabilityFlags(L, "turn_invulnerable", name, /*invulnerable=*/true, alwaysApplyPlayerDamage);
    return 0;
}

// ---------------------------------------------------------------------
// turn_vulnerable (Sec3.8, 0x00a61cb0) - 149 calls / 30 scripts. Arg 1
// mandatory string only - "no second argument is read at all" (CONFIRMED).
// No return. CONFIRMED: the mirror image of turn_invulnerable - clears
// BOTH the invulnerable flag and its companion flag (both hardcoded false)
// on whichever kind resolves, via the same double-gate idiom.
// ---------------------------------------------------------------------
int stub_turn_vulnerable(lua_State* L) {
    logCall(L, upLog(L), "turn_vulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    applyInvulnerabilityFlags(L, "turn_vulnerable", name, /*invulnerable=*/false, /*alwaysApplyPlayerDamage=*/false);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_vulnerable (Sec20.1, 0x00a634f0). 1 mandatory string
// (vehicle name). "No boolean argument exists at all" (CONFIRMED). No
// return. CONFIRMED: delegates to 0x00a7a830(vehicle, false - hardcoded
// literal, never a Lua argument) ONLY - unlike turn_vulnerable above, the
// companion "always apply player damage" setter (0x00a7a9a0) is never
// called from here, so that bit is left untouched entirely. Reuses the
// existing vehicle-only double-gate helper (Sec30.5) directly - this is a
// vehicle-only Lua entry point (resolves via "the already-established
// dedicated vehicle resolver," Sec20.1 - this project's objectResolves()
// stands in for that too, same as every other vehicle function here).
// ---------------------------------------------------------------------
int stub_vehicle_set_vulnerable(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_vulnerable", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    applyVehicleForceFlagSetter(L, "vehicle_set_vulnerable", vehicleName, /*flag=*/false,
                                VehicleState::kInvulnerableBit1d7a, vehicleForceFlags1d7aField);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_is_invulnerable (Sec46.5/Sec46.1). 1 mandatory string (vehicle
// name). Return: 1 boolean. CONFIRMED: "reads the now-settled bit 0x01 of
// +0x1d7a" - the exact bit turn_invulnerable/vehicle_set_vulnerable write
// above. Unresolved vehicle -> false (this project's own consistent
// default for every unresolved-vehicle boolean getter in this file, same
// as vehicle_is_helicopter/vehicle_is_ready).
// ---------------------------------------------------------------------
int stub_vehicle_is_invulnerable(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_invulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        result = st->getOrCreateVehicle(name).forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) != 0; // OPEN until set
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// character_fake_revival_start (Sec45.1, 0x00a41f80). 1 mandatory string
// (character name). No return (not stated). CONFIRMED: reads through null
// first (inside a helper, +0xcd2) when `name` does not resolve - a real
// null-read crash, flagged, not reproduced (same "flagged, not reproduced"
// convention as minimap_icon_remove_do's Sec32.8 table-index crash): this
// project simply skips the write. On a resolved name, sets dword +0xe4 bit
// 0x10000 ("fake revival in progress," established together with its
// sibling _end below).
// ---------------------------------------------------------------------
int stub_character_fake_revival_start(lua_State* L) {
    logCall(L, upLog(L), "character_fake_revival_start", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateCharacter(name).flagsE4.setBits(CharacterState::kFakeRevivalInProgressBit,
                                                       CharacterState::kFakeRevivalInProgressBit);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_fake_revival_end (Sec45.1, 0x00a41fc0). 1 mandatory string
// (character name). No return (not stated). CONFIRMED: "writes through
// null (clearing bit 16 of +0xe4) when the name doesn't resolve" - a real
// null-write crash, flagged, not reproduced. On a resolved name, clears
// the same bit _start above sets.
// ---------------------------------------------------------------------
int stub_character_fake_revival_end(lua_State* L) {
    logCall(L, upLog(L), "character_fake_revival_end", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateCharacter(name).flagsE4.setBits(CharacterState::kFakeRevivalInProgressBit, 0u);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_take_human_shield_check_done (Sec45.1, 0x00a46600). 2
// mandatory strings (taker, expected hostage name). CONFIRMED: "returns 1,
// 2, or 3 Lua values depending on what resolves (a script reading only the
// first value gets the intended 'taker holds the expected hostage'
// boolean)." This project models only that first, documented boolean - the
// real content of a 2nd/3rd return value is not stated by the spec and is
// refused rather than fabricated. When `taker` does not resolve, "the root
// pushes false and keeps going into the hostage-lookup helper anyway" - a
// real null-read crash (reads null +0x1890), flagged, not reproduced: this
// project stops at the same `false`. This project models only the single,
// locally-owned hostage field (CharacterState::humanShieldHostageName's own
// doc comment - no real networking layer).
// ---------------------------------------------------------------------
int stub_character_take_human_shield_check_done(lua_State* L) {
    logCall(L, upLog(L), "character_take_human_shield_check_done", upStateTag(L));
    std::string taker = argString(L, 1);
    std::string expectedHostage = argString(L, 2);
    EngineState* st = upState(L);
    bool takerHoldsExpectedHostage = false;
    if (st->objectResolves().get(taker)) { // OPEN until set
        // The vacuous "" == "" case (no expected name given at all) is
        // deliberately excluded - "holds nothing" should never read as
        // "holds the (unnamed) expected hostage," this project's own minor
        // defensive choice, not a spec fact.
        takerHoldsExpectedHostage =
            !expectedHostage.empty() && st->getOrCreateCharacter(taker).humanShieldHostageName == expectedHostage;
    }
    lua_pushboolean(L, takerHoldsExpectedHostage ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_lights_on (Sec46.5). 1 mandatory string (vehicle name) + 1
// boolean, no stated nil-gate (absent -> false via real lua_toboolean
// semantics, same convention as vehicle_car_alarm_enable, Sec20.3). No
// return. CONFIRMED: `vehicle_lights_on(v, false)` does NOT turn the
// lights off - it clears BOTH the force-on and force-off flags, returning
// the lights to automatic control (a genuine naming trap, not reproduced
// as "off"). The true-path's own exact flag combination beyond "force on"
// is this project's own stated choice (see VehicleState::lightsForceFlags's
// own doc comment).
// ---------------------------------------------------------------------
int stub_vehicle_lights_on(lua_State* L) {
    logCall(L, upLog(L), "vehicle_lights_on", upStateTag(L));
    std::string name = argString(L, 1);
    bool forceOn = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        uint32_t bothBits = VehicleState::kLightsForceOnBit | VehicleState::kLightsForceOffBit;
        v.lightsForceFlags.setBits(bothBits, forceOn ? VehicleState::kLightsForceOnBit : 0u);
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_tire_indicators_alive (Sec46.5). 1 mandatory string (vehicle
// name). Return: 1 number. CONFIRMED naming-trap short-circuit: reports 8
// ("all alive") whenever the vehicle's tire-indicator object is simply
// DISABLED, not actually alive. The real per-tire alive bitmask (the non-
// disabled case) needs a per-wheel physics pool this project does not
// build (same gap as kneecappersEnabled's own precedent, Sec33.1) -
// refused (OpenStateError) rather than fabricated. Unresolved vehicle: not
// addressed by Sec46.5's own text - this project's own choice (not a spec
// fact), the same "false"-equivalent default every other unresolved-
// vehicle getter in this file uses.
// ---------------------------------------------------------------------
int stub_vehicle_tire_indicators_alive(lua_State* L) {
    logCall(L, upLog(L), "vehicle_tire_indicators_alive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.tireIndicatorObjectDisabled.get()) { // OPEN until set
            lua_pushnumber(L, 8);
            return 1;
        }
        throw OpenStateError("vehicle_tire_indicators_alive per-tire alive bitmask (indicator object not disabled)",
                             "spec-lua-api-behaviour.md Sec46.5");
    }
    lua_pushnumber(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_turret_base_to_do (Sec46.4). 1 vehicle name + 1 target name + 1
// boolean. CONFIRMED: "boolean false on failure, a plain number (0/1/2) on
// an attempt." The target-string length loop runs before its own null
// test, so a nil/non-string target name dereferences null WHEN the vehicle
// resolves and has a seat-0 occupant - a real null-read crash, flagged,
// not reproduced (this project takes the same "boolean false" path rather
// than a "no occupant" early-false and a null-target early-false sharing
// one outcome, since nothing in Sec46.4's text distinguishes their return
// shapes). Beyond that: by design an OBJECT target can never actually
// drive the vehicle (reduces to a single point, below the shared drive
// routine's 2-point minimum) - only a PATH target works, truncated at 24
// nodes - and this project has no real path/node object anywhere (no in-
// scope producer creates one, same "no generic entity shape invented"
// boundary this header's own top note states), so it cannot tell a path
// target from an object target; the real "plain number (0/1/2) on an
// attempt" progress code is refused (OpenStateError) rather than
// fabricated.
// ---------------------------------------------------------------------
int stub_vehicle_turret_base_to_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_turret_base_to_do", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    std::string targetName = argString(L, 2);
    (void)lua_toboolean(L, 3); // arg 3's own role is not stated by Sec46.4 - read, not modeled further
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) { // OPEN until set
        lua_pushboolean(L, 0); // CONFIRMED: boolean false on failure
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    if (!v.seat0Occupied.get()) { // OPEN until set - gates the real null-read crash below
        lua_pushboolean(L, 0);
        return 1;
    }
    if (targetName.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    throw OpenStateError("vehicle_turret_base_to_do attempt-progress code (path-target validation not modeled)",
                         "spec-lua-api-behaviour.md Sec46.4");
}

// ---------------------------------------------------------------------
// game_is_pc_dx11 / game_get_ps3_button_swap / game_record_mode_is_
// supported / game_record_mode_is_active (Sec44.1, all 4 resolve to the
// SAME native handler 0x00a3c670). CONFIRMED: no arguments read,
// unconditionally returns false - "this executable is the DX9 build;
// whether a separate DX11 executable binds a different handler is OPEN"
// (not a question this single-build project can answer). Console-overlay/
// recording features with no PC implementation. Each gets its own tiny
// wrapper (not one shared body) - same "each registered name is a real C
// function" convention every other entry in this file follows.
// ---------------------------------------------------------------------
int stub_game_is_pc_dx11(lua_State* L) {
    logCall(L, upLog(L), "game_is_pc_dx11", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}
int stub_game_get_ps3_button_swap(lua_State* L) {
    logCall(L, upLog(L), "game_get_ps3_button_swap", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}
int stub_game_record_mode_is_supported(lua_State* L) {
    logCall(L, upLog(L), "game_record_mode_is_supported", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}
int stub_game_record_mode_is_active(lua_State* L) {
    logCall(L, upLog(L), "game_record_mode_is_active", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// game_show_party_ui / game_show_community_sessions_ui (Sec44.1). CONFIRMED:
// both resolve to the existing shared no-op stub 0x007c9f50 - the SAME
// native handler already backing set_mission_author (Sec6.1) - so these
// are genuinely, confirmedly inert, not merely unmodeled.
// ---------------------------------------------------------------------
int stub_game_show_party_ui(lua_State* L) {
    logCall(L, upLog(L), "game_show_party_ui", upStateTag(L));
    return 0;
}
int stub_game_show_community_sessions_ui(lua_State* L) {
    logCall(L, upLog(L), "game_show_community_sessions_ui", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// flee_to_navpoint (Sec44.5/Sec44.6). CONFIRMED: queues a per-character AI
// event (type 0x30) with the navpoint position and a threat handle;
// "silently dropped if the event pool is empty, the character is dead, or
// it is a player-class object; '#PLAYER#' is not accepted for the fleeing
// character itself." This project has no real AI-event free-list to
// exhaust (stated gap) and no coordinate system to read a real navpoint
// position/threat handle from (project-wide simplification) - only what
// Sec44.5's own prose actually states is modeled: the fleeing character
// (arg 1) must resolve, must not be "#PLAYER#," and must not be dead;
// CharacterState::fleeToNavpointRequestCount counts a qualifying request.
// No return (not stated).
// ---------------------------------------------------------------------
int stub_flee_to_navpoint(lua_State* L) {
    logCall(L, upLog(L), "flee_to_navpoint", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (name != "#PLAYER#" && st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (!c.isDeadHighConfidence) {
            ++c.fleeToNavpointRequestCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_hidden (Sec45.1, 0x00a41540). 1 mandatory string. Return: 1
// boolean. CONFIRMED: reads bit 0 of +0x3b; "an unresolved name reads as
// 'hidden' (true), not an error" - the one inverted-polarity unresolved
// default in this whole file (every other unresolved-name boolean getter
// here defaults to false).
// ---------------------------------------------------------------------
int stub_character_hidden(lua_State* L) {
    logCall(L, upLog(L), "character_hidden", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool hidden = true; // CONFIRMED unresolved default
    if (st->objectResolves().get(name)) { // OPEN until set
        hidden = st->getOrCreateCharacter(name).hiddenFlag.get(); // OPEN until set
    }
    lua_pushboolean(L, hidden ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// ambient_gang_spawn_enable (Sec45.6). 1 boolean, no stated nil-gate.
// CONFIRMED: "a single boolean fans out to all four ambient-gang-enable
// bytes at once - no per-gang Lua control exists at this entry point" -
// modeled as one global value (EngineState::ambientGangSpawnEnabled's own
// doc comment explains why 4 redundant copies would add nothing).
// ---------------------------------------------------------------------
int stub_ambient_gang_spawn_enable(lua_State* L) {
    logCall(L, upLog(L), "ambient_gang_spawn_enable", upStateTag(L));
    upState(L)->ambientGangSpawnEnabled().set(lua_toboolean(L, 1) != 0);
    return 0;
}

// ---------------------------------------------------------------------
// cellphone_animate_stop_do (Sec45.4). CONFIRMED no-op: "calls the
// project's already-known no-op stub."
// ---------------------------------------------------------------------
int stub_cellphone_animate_stop_do(lua_State* L) {
    logCall(L, upLog(L), "cellphone_animate_stop_do", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// cell_camera_enable / cell_camera_is_enabled (Sec45.4). CONFIRMED: "writes
// a global that has exactly one other reference anywhere in the binary -
// its own getter ... confirmed Lua-side-only state with no engine
// consumer." A single bare global, not per-name.
// ---------------------------------------------------------------------
int stub_cell_camera_enable(lua_State* L) {
    logCall(L, upLog(L), "cell_camera_enable", upStateTag(L));
    upState(L)->cellCameraEnabled().set(lua_toboolean(L, 1) != 0);
    return 0;
}
int stub_cell_camera_is_enabled(lua_State* L) {
    logCall(L, upLog(L), "cell_camera_is_enabled", upStateTag(L));
    lua_pushboolean(L, upState(L)->cellCameraEnabled().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// ambient_cop_spawn_enable / action_nodes_shouldnt_flee / action_nodes_
// restrict_spawning (Sec46.3). CONFIRMED: "simple global-byte toggles
// gating AI spawn/flee behavior, none replicated - a co-op client calling
// any of them only changes its own copy." Each write-only in this
// project's scope (no in-scope Lua getter reads any of them back).
// ---------------------------------------------------------------------
int stub_ambient_cop_spawn_enable(lua_State* L) {
    logCall(L, upLog(L), "ambient_cop_spawn_enable", upStateTag(L));
    upState(L)->ambientCopSpawnEnabled().set(lua_toboolean(L, 1) != 0);
    return 0;
}
int stub_action_nodes_shouldnt_flee(lua_State* L) {
    logCall(L, upLog(L), "action_nodes_shouldnt_flee", upStateTag(L));
    upState(L)->actionNodesShouldntFlee().set(lua_toboolean(L, 1) != 0);
    return 0;
}
int stub_action_nodes_restrict_spawning(lua_State* L) {
    logCall(L, upLog(L), "action_nodes_restrict_spawning", upStateTag(L));
    upState(L)->actionNodesRestrictSpawning().set(lua_toboolean(L, 1) != 0);
    return 0;
}

// ---------------------------------------------------------------------
// whored_countdown_finished (Sec46.6, UI sub-registrar 0x006147c0).
// CONFIRMED: "its own guard byte is never written anywhere in the binary
// (so the guard always passes) and the flag it sets has no direct reader
// found - in practice this call only ever sends a network broadcast with
// no host gate." No local state to model (the flag has no reader anywhere
// in the binary) - only the replicate call site is recorded, honestly,
// same "mark the call site, no further effect" convention as every other
// confirmed-inert broadcast in this file.
// ---------------------------------------------------------------------
int stub_whored_countdown_finished(lua_State* L) {
    logCall(L, upLog(L), "whored_countdown_finished", upStateTag(L));
    EngineState::replicateStateChange("whored_countdown_finished", "");
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_tire_durability / vehicle_set_tire_damage_multiplier
// (Sec46.5). 1 vehicle name + 1 number, no stated nil-gate. No return.
// CONFIRMED: "silently ignore any value <= 0 (the field can never be reset
// to 0 from Lua this way) and, for an unresolved vehicle name inside a
// session, broadcast a network record for vehicle id 0 rather than doing
// nothing" - the notable quirk §46.7 flags as "a single-gate broadcast
// variant with no local fallback for an unresolved vehicle name": unlike
// this file's double-gate force-flag setters, an unresolved name here
// still replicates (tagged with the literal "vehicle id 0" this project's
// own stand-in for that real fallback id, not a guessed numeric scheme)
// instead of doing nothing at all.
// ---------------------------------------------------------------------
int stub_vehicle_set_tire_durability(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_tire_durability", upStateTag(L));
    std::string name = argString(L, 1);
    double value = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        if (value > 0.0) st->getOrCreateVehicle(name).tireDurability.set(value);
        EngineState::replicateStateChange("vehicle_set_tire_durability", name);
    } else {
        EngineState::replicateStateChange("vehicle_set_tire_durability", "<vehicle-id-0>");
    }
    return 0;
}
int stub_vehicle_set_tire_damage_multiplier(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_tire_damage_multiplier", upStateTag(L));
    std::string name = argString(L, 1);
    double value = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        if (value > 0.0) st->getOrCreateVehicle(name).tireDamageMultiplier.set(value);
        EngineState::replicateStateChange("vehicle_set_tire_damage_multiplier", name);
    } else {
        EngineState::replicateStateChange("vehicle_set_tire_damage_multiplier", "<vehicle-id-0>");
    }
    return 0;
}

// Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking tranche
// 16") - 25 previously-unspecced Lua-bound names. This batch's own fresh
// mission-drive run (build_verify_tranche16, against the real game
// archives, same convention as Sec38/Sec39/Sec40's own batch) found
// exactly ONE real hit among the 25 names - `spawning_boats` (1
// incremental call from a real mission script, grepped directly from
// that run's own verdict_stub_hits_with_missions.tsv) - every other name
// was zero real hits, the same outcome that combined 75-name batch
// already found for all 75 of ITS names. Per the orchestrator's own
// explicit scope instruction, this batch implements the 8 names
// EXPLICITLY MANDATED regardless of call count (4 crash guards, 4
// faithful quirks - see each stub's own doc comment below and
// engine_state.h's own Sec49 accessor comments for the full citation/
// reasoning): shop_purchase_purchase_shop, spawn_override_set_override_
// category_for_hood, skydive_move_to_check_done, set_char_in_string
// (guards); store_stronghold_game_purchase_upgrade, squad_enable, sfx_
// use_load_images, screen_capture_preview_should_upload (quirks); PLUS
// the 1 name with a real measured hit, `spawning_boats` (9 total). The
// other 16 named this tranche (store_stronghold_upgrade_end_flyby/
// store_gallery_download_show_list/store_vehicle_do_return_to_crib/
// _allow_garage/store_gang_is_unlocked/_bg_covered/_begin_exit/
// store_gallery_upload_character/_display_character/store_crib_init_
// crib_garage/spawn_global_override_set_category/_clear_category/set_
// attack_peds_flag, plus 3 further spawn_*/spawning_* names §49.2's own
// condensed text counts toward its "7 functions" header without naming
// individually) are zero real hits with no mandated guard/quirk - each
// would need its own substantial new engine-state modeling (a store-
// screen-stack/camera-fly-by model, a garage-node/mission-denylist
// table, a gang-store cash/unlock-bit gate, a deferred-upload
// scheduler, a road/world object resolver) this project does not have
// in scope - left as generic stubs this pass, same judgment call Sec38/
// Sec39/Sec40's own batch made for its own 59 unimplemented names.
// -----------------------------------------------------------------------

// shop_purchase_purchase_shop (Sec49.1, 0x00a03600 one-name registrar,
// function-pointer-before-name - the SAME registration quirk tranche 13
// first found) - MANDATED crash guard #1 PLUS the MANDATED faithful
// double-charge quirk, both on the same function. No arguments. See
// EngineState::ShopPurchase's own doc comment (engine_state.h) for the
// full citation. HOST-SAFETY DEVIATION (not a spec fact): guards the
// null-shop dereference via `triggerResolves` (CHOSEN default false)
// instead of performing the equivalent unguarded read, counting the
// averted path via nullShopGuardCount - "this project does not simulate
// crashes" (cf. group_get_next_npc, Sec28.5). Once resolved, EVERY call
// repeats the payment/trigger-disable/broadcast/fly-by side effects
// (the four *RepeatCount fields) - reproduced faithfully, NOT "fixed"
// into a once-only purchase - while `owned` itself is set only once,
// matching the real, CONFIRMED guarded bit exactly.
int stub_shop_purchase_purchase_shop(lua_State* L) {
    logCall(L, upLog(L), "shop_purchase_purchase_shop", upStateTag(L));
    EngineState* st = upState(L);
    auto& sp = st->shopPurchase();
    if (!sp.triggerResolves) {
        ++sp.nullShopGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "shop_purchase_purchase_shop:NULL_SHOP_GUARD", upStateTag(L));
        return 0;
    }
    // CONFIRMED real double-charge defect (Sec49.1): every side effect
    // below repeats on every resolved call; only `owned` just below is
    // guarded to a single call.
    st->playerCash() -= sp.price; // "routed to owner if not locally owned" - not modeled further, no economy-routing layer in scope
    ++sp.paymentRepeatCount;
    ++sp.triggerDisableRepeatCount;
    ++sp.ownedBroadcastRepeatCount;
    ++sp.flyByRepeatCount;
    if (!sp.owned) {
        sp.owned = true; // CONFIRMED: the ONE guarded bit
    }
    return 0;
}

// spawn_override_set_override_category_for_hood (Sec49.2, 0x00a20840
// gameplay registrar) - MANDATED crash guard #2. 2 mandatory strings
// (neighbourhood, category name). See EngineState::SpawnOverride's own
// doc comment (engine_state.h) for the full citation. HOST-SAFETY
// DEVIATION (not a spec fact): guards only the immediate-dispatch host
// branch (the one shape that crashes INSIDE this function, per Sec49.2's
// own text); the "null merely stored, dereferenced later elsewhere"
// branch is not a crash in THIS function, so it is reproduced plainly
// (the null is honestly recorded as stored, not guarded) rather than
// invented into a guard that would not match the real defect's own
// shape. Deliberately does NOT copy the sibling `spawn_global_override_
// set_category`'s own check (that sibling is out of this batch's scope,
// and "fixing" this one would misrepresent a real, confirmed asymmetry
// between the two).
int stub_spawn_override_set_override_category_for_hood(lua_State* L) {
    logCall(L, upLog(L), "spawn_override_set_override_category_for_hood", upStateTag(L));
    std::string hood = argString(L, 1);
    std::string category = argString(L, 2);
    EngineState* st = upState(L);
    auto& so = st->spawnOverride();
    bool resolved = so.categoryNameResolves.known(category) && so.categoryNameResolves.get(category); // OPEN until set
    if (!resolved) {
        bool present = st->coopSession().present.get();                      // OPEN until set
        bool hostImmediate = present && st->coopSession().localIsHost.get(); // OPEN until set, only read when present
        if (hostImmediate) {
            ++so.hoodCategoryImmediateNullDerefGuardCount; // HOST-SAFETY, see comment above
            logCall(L, upLog(L), "spawn_override_set_override_category_for_hood:NULL_CATEGORY_GUARD", upStateTag(L));
            return 0;
        }
        // CONFIRMED real defect, reproduced plainly: the null is stored
        // unguarded here (not a crash in THIS function) - the deferred
        // read happens elsewhere (the per-hood reader or the co-op
        // join-snapshot serializer), out of this batch's own scope.
        so.perHoodCategoryNullStored.insert(hood);
        so.perHoodCategory.erase(hood);
        return 0;
    }
    so.perHoodCategoryNullStored.erase(hood);
    so.perHoodCategory[hood] = category;
    return 0;
}

// spawning_boats (Sec49.2, 0x00a20840 gameplay registrar) - added to this
// batch after its own fresh mission-drive run (build_verify_tranche16)
// found a REAL measured hit: 1 incremental call from a real mission
// script (grepped directly from that run's own verdict_stub_hits_with_
// missions.tsv: call_count_all_inclusive=1, incremental_from_missions=1,
// last_argc=1, last_arg_types=boolean). 1 mandatory boolean. CONFIRMED:
// writes a host-authoritative, session-replicated byte (registered name
// `"os_boat"`) with NO host gate on the write itself - unlike tranche
// 13's sibling `audio_suppress_ambient_player_lines` (Sec45, itself
// unimplemented anywhere in this project), which DOES gate. Reuses
// EngineState::replicateStateChange() (this project's own established
// no-op stand-in for the record-and-replicate idiom, Sec3.4/Sec3.7/
// Sec4.13/Sec7.33 - this project builds no real networking layer).
int stub_spawning_boats(lua_State* L) {
    logCall(L, upLog(L), "spawning_boats", upStateTag(L));
    bool value = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED)
    EngineState* st = upState(L);
    auto& sb = st->spawningBoats();
    sb.value = value; // CONFIRMED: no host gate on the write itself
    ++sb.writeCount;
    EngineState::replicateStateChange("os_boat", value ? "true" : "false"); // CONFIRMED: session-replicated, unconditionally
    return 0;
}

// skydive_move_to_check_done (Sec49.3, 0x00a20840 gameplay registrar) -
// MANDATED crash guard #3, a NEW crash shape for this series (pointer-
// arithmetic-survives-null-test, not a plain unguarded dereference). See
// EngineState::SkydiveMoveToCheckDone's own doc comment (engine_state.h)
// for the full citation, including the CHOSEN/labelled argument shape
// (not independently re-derived this pass).
int stub_skydive_move_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "skydive_move_to_check_done", upStateTag(L));
    std::string target = argString(L, 1);
    bool listMode = lua_toboolean(L, 2) != 0; // CHOSEN arg shape, see engine_state.h's own doc comment
    EngineState* st = upState(L);
    bool targetKnownResolved = st->objectResolves().known(target) && st->objectResolves().get(target);
    if (!targetKnownResolved) {
        st->skydiveMoveToCheckDone().nullTargetGuardCount++; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "skydive_move_to_check_done:NULL_TARGET_GUARD", upStateTag(L));
        // HOST-SAFETY DEVIATION: gives the SAME answer the already-correct
        // list-mode branch gives for the identical failure ("done", true),
        // regardless of which mode this call actually requested - per
        // Sec49.3's own text, that is the correct answer for BOTH modes on
        // this exact failure; only single-object mode's own code path
        // mishandles getting there.
        lua_pushboolean(L, 1);
        return 1;
    }
    (void)listMode; // resolved-target done/pending determination OPEN in either mode, see comment above - refuses rather than invents
    throw OpenStateError("skydive_move_to_check_done resolved-target done/pending determination",
                         "spec-lua-api-behaviour.md Sec49.3");
}

// set_char_in_string (Sec49.4, `ui`) - MANDATED bounds check (a HOST-
// SAFETY stand-in for the real unbounded stack allocation - this project
// has no literal stack guard to overrun). 1 string + 1 0-based index +
// 1 single-character string. See EngineState::SetCharInString's own doc
// comment (engine_state.h) for the full citation, including the CHOSEN
// 1024 cap and the CHOSEN space-fill construction. The "either string
// argument non-a-string reads through null" defect is already avoided
// by construction via this file's own NULL-safe argString() (same
// precedent as team_make_hostile, Sec38.2) - nothing further to guard.
int stub_set_char_in_string(lua_State* L) {
    logCall(L, upLog(L), "set_char_in_string", upStateTag(L));
    std::string base = argString(L, 1);
    int64_t idx = truncateEa2596(lua_tonumber(L, 2)); // 0-based (CONFIRMED); refuses NaN/out-of-range per existing convention
    std::string ch = argString(L, 3);
    EngineState* st = upState(L);
    auto& scs = st->setCharInString();
    if (idx < 0 || idx > EngineState::SetCharInString::kMaxIndex) {
        ++scs.indexOutOfBoundsGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "set_char_in_string:INDEX_OUT_OF_BOUNDS_GUARD", upStateTag(L));
        lua_pushstring(L, base.c_str()); // CHOSEN: return the original string unmodified rather than build an unbounded result
        return 1;
    }
    // CHOSEN, labelled construction (not a spec fact beyond the crash
    // shape itself): pad with spaces to reach `idx`, then write the given
    // character there.
    if (base.size() <= static_cast<size_t>(idx)) {
        base.resize(static_cast<size_t>(idx) + 1, ' ');
    }
    base[static_cast<size_t>(idx)] = ch.empty() ? '\0' : ch[0]; // CHOSEN fallback for a non-single-char/absent 3rd argument
    lua_pushstring(L, base.c_str());
    return 1;
}

// store_stronghold_game_purchase_upgrade (Sec49.1, `ui`) - MANDATED
// faithful quirk (charge-then-no-op) PLUS a MANDATED, separate crash
// guard (no local-player null check). No arguments. See
// EngineState::StrongholdPurchaseUpgrade's own doc comment
// (engine_state.h) for the full citation, including the reconciliation
// against Sec8.27/Sec26.28's own CONFIRMED-false `game_get_is_host`
// verdict - NOT "fixed" into an actual upgrade; reproduced exactly.
int stub_store_stronghold_game_purchase_upgrade(lua_State* L) {
    logCall(L, upLog(L), "store_stronghold_game_purchase_upgrade", upStateTag(L));
    EngineState* st = upState(L);
    auto& su = st->strongholdPurchaseUpgrade();
    if (!su.strongholdResolves) return 0; // CHOSEN default: no ambient stronghold context resolved - nothing to charge or upgrade
    if (!st->hasLocalPlayer().get()) {    // OPEN until set - separate CONFIRMED crash shape (no local-player null check)
        ++su.localPlayerNullGuardCount;   // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "store_stronghold_game_purchase_upgrade:NULL_LOCAL_PLAYER_GUARD", upStateTag(L));
        return 0;
    }
    st->playerCash() -= su.price; // CONFIRMED: charged unconditionally once a stronghold resolves, no affordability check
    ++su.chargeCount;
    // CONFIRMED (Sec49.1, reconciled against Sec8.27/Sec26.28): the
    // level-up setter's own gate - a session exists AND this machine is
    // host - is CONFIRMED false in single player (no session is ever
    // installed, EngineState::coopLocalIsHost()'s own doc comment) - so it
    // silently no-ops here, every time. Net effect: cash is always
    // deducted, the stronghold level never changes, in this host as in
    // single-player retail play.
    bool present = st->coopSession().present.get();                 // OPEN until set
    bool hostBranch = present && st->coopSession().localIsHost.get(); // OPEN until set, only read when present
    if (!hostBranch) {
        ++su.levelUpGateFailedNoopCount;
    }
    // hostBranch == true is unreachable in single player (Sec26.28) but
    // left as a structural branch rather than collapsed, in case a future
    // co-op host integration sets coopSession() present+host - the real
    // stronghold-level-up mechanics themselves are OPEN beyond the gate
    // (not given in the condensed spec text), so nothing further is
    // modeled on that branch either.
    return 0;
}

// squad_enable (Sec49.3, `gameplay`) - MANDATED faithful quirk
// (asymmetric enable/disable). 1 character name + 1 boolean (CONFIRMED
// default false). See EngineState::SquadMember's own doc comment
// (engine_state.h) for the full citation - NOT "fixed" into real
// inverses; reproduced exactly.
int stub_squad_enable(lua_State* L) {
    logCall(L, upLog(L), "squad_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_gettop(L) >= 2 ? (lua_toboolean(L, 2) != 0) : false; // CONFIRMED default false
    EngineState* st = upState(L);
    auto& member = st->squadMember(name);
    if (!enable) {
        // CONFIRMED: actually dismisses the character - restores stats,
        // removes it from the roster, releases resources. Stat-restore/
        // resource-release internals are OPEN beyond the fact they
        // happen - only the roster-membership bit is modeled.
        member.inRoster = false;
        ++member.dismissCount;
    } else {
        // CONFIRMED real asymmetric-effect defect (Sec49.3): only flips
        // two unrelated flag bits - does NOT re-add the character to the
        // crew. `inRoster` is deliberately left untouched here.
        member.flagBitA = !member.flagBitA; // CHOSEN toggle semantics ("flips", per spec's own word) - real bit meanings OPEN
        member.flagBitB = !member.flagBitB;
        ++member.enableTrueCount;
    }
    return 0;
}

// =======================================================================
// Ranking tranches 15/17/20/22/23 (spec-lua-api-behaviour.md Sec52/Sec51/
// Sec53/Sec55/Sec57), real-hit names only (2026-10-03, orchestrator-
// directed pass): across the full Sec50-Sec59 backlog (10 sections, the
// entire remaining 554-name ranking backlog except tranche 16/Sec49, done
// separately), a fresh mission drive against the real cache found exactly
// 8 names with a real, non-zero call count. Every other name in those 10
// sections stays an ordinary generic logging stub this pass - see the
// implementing commit's own message for the full per-section tally.
// =======================================================================

// tutorial_lock (Sec52.3, tranche 15, 0x00a60??? not individually dumped
// this pass - see engine_state.h's own doc comment on tutorialLockCount()/
// recordTutorialLock() for the full citation and the HOST-SAFETY-adjacent
// "no invented state code" reasoning). 1 mandatory string (tutorial name) -
// real mission-drive call site: argc=1, type=string. No return stated;
// HIGH CONFIDENCE "none", matching this family's own setter/query split
// (tutorial_unlock, Sec24.11, is also "none").
int stub_tutorial_lock(lua_State* L) {
    logCall(L, upLog(L), "tutorial_lock", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    EngineState::TutorialLookup lookup = EngineState::tutorialLookup(name);
    // CONFIRMED (Sec52.3): only indices up to 188 take effect; 189-209 (and
    // an unresolved name, index -1) are a silent no-op.
    if (lookup.index >= 0 && lookup.index <= 188) {
        st->recordTutorialLock(lookup.index);
    }
    return 0;
}

// sfx_use_load_images (Sec49.4, `ui`) - MANDATED faithful quirk (trivial
// hardcoded constant). No modeled state needed - see engine_state.h's
// own note next to EngineState::ScreenCapturePreview.
int stub_sfx_use_load_images(lua_State* L) {
    logCall(L, upLog(L), "sfx_use_load_images", upStateTag(L));
    lua_pushboolean(L, 1); // CONFIRMED hardcoded true (0x0149365c, Sec49.4/Sec26.24)
    return 1;
}

// screen_capture_preview_should_upload (Sec49.4, `ui`) - MANDATED
// faithful quirk PLUS a MANDATED, explicitly labelled CHOSEN stand-in for
// the unmodeled platform-privilege fork. 1 boolean, 0 Lua return values
// either way (CONFIRMED). See EngineState::ScreenCapturePreview's own
// doc comment (engine_state.h) for the full citation.
int stub_screen_capture_preview_should_upload(lua_State* L) {
    logCall(L, upLog(L), "screen_capture_preview_should_upload", upStateTag(L));
    bool wantUpload = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED arg)
    EngineState* st = upState(L);
    auto& sc = st->screenCapturePreview();
    if (!wantUpload) {
        ++sc.explicitFalseCancelCount; // CONFIRMED: explicit false -> the "cancel" path
        logCall(L, upLog(L), "screen_capture_preview_should_upload:CANCEL_EXPLICIT_FALSE", upStateTag(L));
        return 0; // CONFIRMED: 0 Lua return values either way
    }
    // CHOSEN (not a spec fact, explicitly labelled): this project has no
    // real platform-privilege layer anywhere in scope to query, so the
    // "platform denied" fork the spec describes (which would silently
    // take the SAME cancel path above, indistinguishable from the
    // caller's own point of view - 0 return values either way) is modeled
    // as ALWAYS "not denied" here - a stated simplification, never a
    // confirmed value.
    ++sc.uploadProceedCount;
    return 0; // CONFIRMED: 0 Lua return values either way
}

// radio_newsbreak_clear (Sec51.3, tranche 17). No arguments - real
// mission-drive call site: argc=0. No return stated. CONFIRMED only as "a
// straightforward local-only state change" - see engine_state.h's own
// radioNewsbreakActive() doc comment for the minimal-model reasoning.
int stub_radio_newsbreak_clear(lua_State* L) {
    logCall(L, upLog(L), "radio_newsbreak_clear", upStateTag(L));
    upState(L)->radioNewsbreakActive().set(false);
    return 0;
}

// group_create_do / group_create_hidden_do (Sec53.3, tranche 20). 1
// mandatory string (group name) - real mission-drive call sites: argc=1,
// type=string, both. No return stated. CONFIRMED: "the same underlying
// creation routine differing only in one passed literal, and creation
// happens exactly once" - see engine_state.h's own groupAlreadyCreated()/
// markGroupCreated() doc comment.
int stub_group_create_do(lua_State* L) {
    logCall(L, upLog(L), "group_create_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(name)) st->markGroupCreated(name);
    return 0;
}

int stub_group_create_hidden_do(lua_State* L) {
    logCall(L, upLog(L), "group_create_hidden_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(name)) st->markGroupCreated(name);
    return 0;
}

// dlc2_m02_clapboards_get / dlc2_m02_clapboards_reset (Sec55.5, tranche
// 22). `_get`: 1 number (1-based clapboard index) - real mission-drive
// call site: argc=1, type=number. `_reset`: 1 number (count) - real
// mission-drive call site: argc=1, type=number. Neither return is stated
// beyond `_get`'s own documented "no value at all when out of range";
// `_get` in range pushes the boolean flag byte (see engine_state.h's own
// dlc2ClapboardsGet() doc comment for the full CONFIRMED shape and the
// HOST-SAFETY lower-bound deviation; `_set` is a zero-hit sibling and
// stays a generic stub this pass, per this task's own scope rule).
int stub_dlc2_m02_clapboards_reset(lua_State* L) {
    logCall(L, upLog(L), "dlc2_m02_clapboards_reset", upStateTag(L));
    int32_t rawCount = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    upState(L)->dlc2ClapboardsReset(rawCount);
    return 0;
}

int stub_dlc2_m02_clapboards_get(lua_State* L) {
    logCall(L, upLog(L), "dlc2_m02_clapboards_get", upStateTag(L));
    int32_t clapboardNumber = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    int result = upState(L)->dlc2ClapboardsGet(clapboardNumber);
    if (result < 0) return 0; // CONFIRMED/HOST-SAFETY: no value at all (nil), not false
    lua_pushboolean(L, result);
    return 1;
}

// cutscene_play_do / cutscene_play_check_done (Sec57.2, tranche 23) - see
// EngineState::cutscenePlayDo()'s own doc comment in lua_cutscene.cpp for
// the full design. `_do`: real mission-drive call site argc=5, types
// string,nil,table,nil,nil (name, the read-and-discarded 2nd argument, the
// destination table, 2 further arguments this spec gives no behaviour for
// and the one real call site never populates). `_check_done`: real
// mission-drive call site argc=0 (the single highest hit count of this
// whole 8-name batch, 12121 calls - this pair is where almost every
// mission in this project's own mission-drive baseline parks, per
// "waiting_at=...cutscene_play" in the fresh verdict TSV this pass ran).
int stub_cutscene_play_do(lua_State* L) {
    logCall(L, upLog(L), "cutscene_play_do", upStateTag(L));
    std::string name = argString(L, 1);
    // Arg 2 (optional table-or-boolean): CONFIRMED "read and discarded" -
    // never used for any decision, so not read at all here (functionally
    // identical to reading then ignoring it).
    std::vector<std::string> destinations = readCharacterNameArray(L, 3); // CONFIRMED-shape "a Lua table" reader, Sec2.9's own idiom
    // Args 4/5: no behaviour given by Sec57.2 beyond this point; the one
    // real call site passes nil for both, so nothing further to read.
    upState(L)->cutscenePlayDo(name, std::move(destinations));
    return 0;
}

int stub_cutscene_play_check_done(lua_State* L) {
    logCall(L, upLog(L), "cutscene_play_check_done", upStateTag(L));
    lua_pushboolean(L, upState(L)->cutscenePlayCheckDone() ? 1 : 0);
    return 1;
}

// action_play_do (Sec22.29, 0x00a3fae0) / action_play_is_finished (Sec7.21,
// 0x00a3dcc0) - real-hit probe-mode batch (2026-10-03). Investigation
// trigger: results/probe_blockers_ranked_20261003_post_realhits.md's own
// #1 shared blocker - action_play_do has 49,655 real calls in a fresh run
// of this project's own 49-mission drive (the single highest call count
// of any name in the whole run), is NOT in specConfirmedStubNames(), and 7
// missions (dlc3_m03/m06/m10/m11/m13/m14/m15) all park at the identical
// [string "game_lib.lua"]:563 (in action_play_non_blocking), no
// first_error, inside a thread_yield poll loop - the SAME architectural
// shape cutscene_play_do/cutscene_play_check_done had before Sec57.2
// closed it (src/lua_cutscene.cpp).
//
// UNLIKE the cutscene case, the natural same-shaped guess (a separate
// "_check_done"-style completion-check native action_play_non_blocking
// polls) does not hold up against this project's own real mission-drive
// evidence: action_play_is_finished (Sec7.21, a real, registered
// `gameplay` native - grepped directly in
// tools/lua_all_registered_1490_tagged.txt) has ZERO measured calls in a
// fresh run of these same 49 missions (grepped directly against
// verdict_stub_hits_with_missions.tsv - absent from the whole file, not
// merely zero), while thread_new/thread_check_done/thread_yield (this
// project's own already-implemented thread-scheduler primitives,
// thread_scheduler.cpp - deliberately NOT touched here, per
// spec_confirmed_stubs.h's own standing "Deliberately NOT touched" note)
// show large but structurally inconsistent call counts for this role: a
// thread_new()-created handle is never lua_resume()'d by anything in this
// project's scope (thread_scheduler.cpp's own stub_thread_yield doc
// comment), so ThreadScheduler::isDone() would report "dead" on the FIRST
// check after creation - the opposite of a multi-tick stall.
// action_play_non_blocking is itself a plain game_lib.lua FUNCTION, not a
// registered native (absent from tools/lua_all_registered_1490_tagged.txt,
// grepped directly) - its traceback frame is a Lua call frame, not a C one.
//
// A temporary, reverted-before-commit diagnostic (dumping real argument
// VALUES, not just types, for action_play_do specifically - never
// committed, and never reading game_lib.lua's own source text, which
// stays off-limits per this project's clean-room boundary) found the SAME
// exact call repeating every tick for a stuck mission:
// action_play_do("MS_Jimmy", "saint fan react b", "", false, true, true,
// nil, nil). genericStubTrampoline (lua_stub_registry.cpp) always pushes
// exactly 1 value - nil - for an unimplemented name; in Lua, nil is one of
// only two falsy values (with false), so a wrapper of the shape `while
// not action_play_do(...) do thread_yield() end` (or an equivalent "keep
// retrying until this stops being nil/false" idiom - inferred from this
// real call/return evidence, never from reading the off-limits script
// text) can never terminate against a stub that returns nil on every
// call, regardless of whether any SEPARATE completion-check native exists
// at all. Sec22.29 itself describes action_play_do as a one-shot
// dispatcher with NO completion-signaling mechanism of its own (no
// scripted-request-id pool like Sec35.6's, no thread handle, no flag it
// sets for a later poll to read) - its own CONFIRMED contract is simply
// "always pushes exactly 1 number" (1 on full success; two explicitly
// named sentinels on two named failure paths). Implementing THAT contract
// faithfully - a real number on every path, never nil - is therefore both
// the full extent of what Sec22.29 actually specifies and, on this
// project's own measured evidence, the real candidate fix for the
// observed stall: no separate completion-tracking mechanism is invented
// here because Sec22.29 does not describe one to invent.
//
// Body per Sec22.29 (full citation in this file's own §22.29 reading):
// arg 1 resolves via the generic/#PLAYER# chain (EngineState::
// objectResolves(), the SAME shared Sec29 map the Killbane-class blocker
// already uses) - resolve failure pushes the fixed sentinel -1.0 (the
// SAME named constant effect_play/effect_play_on_script_object, Sec9.1/
// Sec12.10, would use for their own failure case - NEITHER is implemented
// anywhere in this codebase yet, so EngineState::
// kActionPlaySentinelResolveFail is this constant's first home here, not
// a reused one). If resolved but dead and not forced (arg 4): a bare -1 (a
// genuinely distinct sentinel per Sec22.29's own text, tracked via its own
// named constant/counter rather than conflated with the resolve-failure
// path, even though both are -1 as a Lua number). The dead-check reuses
// CharacterState::notDeadForActionPlay(), a SEPARATELY SCOPED predicate
// from the pre-existing isAlive (Sec30.5) - see that method's own doc
// comment (engine_state.h) for why reusing isAlive here would assume a
// cross-call-site sharing fact neither section states. UPDATE 2026-10-03
// (Sec34.5): this predicate's own real body is now CONFIRMED (lifeState ==
// 5, or the +0xe3 bit 0x01 flag) and computed live from those two fields,
// both seeded to their CONFIRMED spawn defaults at construction - no
// longer an independently-OPEN flag a test or the probe diagnostic has to
// set. Otherwise: the action-id
// lookup (0x004bf810) has zero confirmed entries anywhere in this
// project's spec (the same "never-populated registry" boundary this
// project already established for team_make_unfriendly's team-name
// registry, Sec30.6) - every lookup therefore honestly takes the "not
// found" path, which alone already forces Sec22.29's own AND-gated final
// return below 1, regardless of what the still-OPEN dispatch (0x00a275e0)
// would otherwise have decided - so this project never needs to guess
// 0x00a275e0's own internal result to answer honestly. The EXACT non-1
// numeric encoding is not itself given by Sec22.29 (only the two sentinels
// above are spelled out) - EngineState::kActionPlaySentinelLookupMiss is
// CHOSEN as 0.0, this document's own common boolean-as-number "false"
// idiom used throughout, NOT a claimed real value.
int stub_action_play_do(lua_State* L) {
    logCall(L, upLog(L), "action_play_do", upStateTag(L));
    std::string name = argString(L, 1);
    std::string actionName = argString(L, 2);
    // Arg 3: standard nil-gated idiom, but CONFIRMED default = arg 2's OWN
    // value (Sec22.29: "a genuinely distinct default shape"), not a
    // literal constant.
    std::string variant = optionalStringDefault(L, 3, actionName);
    bool forced = optionalBoolDefault(L, 4, false);
    // Args 5/6: forwarded into the still-OPEN dispatch (0x00a275e0) only;
    // no further effect modeled anywhere in this project's scope.
    (void)optionalBoolDefault(L, 5, false);
    (void)optionalBoolDefault(L, 6, false);
    // Args 7/8: two secondary named references, each resolved via the 5th
    // per-kind resolver (liveness-gated, default 0 on failure) and
    // forwarded into the still-OPEN dispatch - no Lua-visible effect
    // anywhere in this project's scope, so read (matching the real call
    // shape) and not modeled further, same as cutscene_play_do's own
    // "read and discarded" 2nd argument above.
    (void)optionalStringDefault(L, 7, "");
    (void)optionalStringDefault(L, 8, "");

    EngineState* st = upState(L);
    // OPEN until set (Sec29) - throws for an unconfirmed real name (the
    // same Killbane-class blocker every other in-scope CharacterState-
    // based function already produces); true via --probe-unresolved-names.
    // Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly
    // of its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
    // PLAYER-only as an earlier reading had it - wired here, follower-
    // sentinel-resolver pass, via resolveFollowerAwareObject().
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) {
        lua_pushnumber(L, EngineState::kActionPlaySentinelResolveFail);
        return 1;
    }
    CharacterState& character = st->getOrCreateCharacter(resolvedName);
    // CONFIRMED (Sec34.5, 2026-10-03): the predicate's own two real fields
    // (lifeState, killFinalizedFlagE3) are seeded to their CONFIRMED spawn
    // defaults at construction (EngineState::getOrCreateCharacter) for
    // every character - real or probed alike - so no separate probe
    // fallback is needed here any more (see notDeadForActionPlay()'s own
    // doc comment).
    bool notDead = forced || character.notDeadForActionPlay();
    if (!forced && !notDead) {
        st->recordActionPlayDeadGuard();
        lua_pushnumber(L, EngineState::kActionPlaySentinelDeadNotForced);
        return 1;
    }
    // Action-id lookup (0x004bf810): see this function's own doc comment
    // above - this project has zero confirmed animation-name table
    // entries, so every lookup honestly takes the "not found" path, which
    // alone already forces a non-1 final return per Sec22.29's own AND
    // logic.
    (void)variant; // consumed only by the still-OPEN sub-state blend step (0x0095be60) - no further effect modeled
    st->recordActionPlayLookupMiss();
    lua_pushnumber(L, EngineState::kActionPlaySentinelLookupMiss);
    return 1;
}

// action_play_is_finished (Sec7.21, 0x00a3dcc0) - see EngineState::
// actionPlayIsFinished()'s own doc comment (engine_state.h) for the full
// reasoning: this project never models anything that could make its
// CONFIRMED "has an active sub-state below threshold" branch true, so it
// honestly always reports "finished." Zero measured real calls across
// this project's own full 49-mission drive as of this batch (see this
// file's own doc comment above stub_action_play_do) - implemented
// regardless, per Sec7.21's own real, registered-native status and this
// task's own explicit "closes part of the picture, real value regardless"
// instruction, not because it was the actual observed blocker for the 7
// missions named above.
int stub_action_play_is_finished(lua_State* L) {
    logCall(L, upLog(L), "action_play_is_finished", upStateTag(L));
    (void)argString(L, 1); // resolved via the generic/#PLAYER# chain per Sec7.21 - consumed for real call shape only; see doc comment above for why resolution doesn't change the answer in this project's scope
    (void)optionalNumberDefault(L, 2, 0.0); // threshold argument - consumed for shape only, same reasoning
    lua_pushboolean(L, upState(L)->actionPlayIsFinished() ? 1 : 0);
    return 1;
}

// get_dist (Sec4.2, 0x00a4bdb0) - 217 calls/35 scripts. Investigation
// trigger: results/trace_host_calls_20261003.md - the new
// --trace-host-calls-before-error diagnostic's first real use found
// get_dist (a generic stub, always nil) feeding directly into m08.lua:910's
// "attempt to compare nil with number". Real mission-drive call shape
// confirmed directly (verdict_stub_hits_with_missions.tsv): argc=2,
// types=string,string - matches Sec4.2's own 2-mandatory-string shape.
//
// CONFIRMED contract: 2 mandatory strings (object names) + 2 optional
// booleans (vertical-axis handling), each individually nil-gated, default
// false. Each name resolves through the SAME shared objectResolves() map
// every other generic/#PLAYER# resolver in this project's scope already
// uses (Sec29's "one shared global name map" finding - Sec4.2's own
// 0x00a280c0 first-try resolver). If EITHER name fails to resolve, pushes
// the CONFIRMED IEEE-754 single-precision max value as a "could not
// compute" sentinel - Sec4.2's own text is explicit this is not an error
// path. If both resolve, computes the Euclidean distance between their
// world positions, with the middle (vertical) axis of the difference
// optionally multiplied by 10.0 (arg 3) or zeroed entirely (arg 4, checked
// after arg 3 so it wins if both are true) - "vertical" = Y, CONFIRMED
// engine convention (spec-physics-runtime.md's own gravity constant
// "(0, -9.8, 0), Y-up").
//
// Every resolved name is treated as character-shaped: the SAME
// simplification this file already establishes for other dual/triple-typed
// resolvers (see stub_on_take_damage's own doc comment) - this project has
// no name-to-type registry to tell a character name from a vehicle name
// from a bare string alone, and inventing one would be exactly the kind of
// unconfirmed state this project's discipline refuses. worldPosition
// itself is OPEN by default (CharacterState::worldPosition's own doc
// comment) - no spec gives a CONFIRMED spawn position for any object, and
// the save format doesn't carry one either (spec-save-format.md Sec6.6,
// confirmed 2026-10-03), so a read while unset throws OpenStateError. This
// is the correct, honest outcome for "resolved object, unknown position" -
// a GENUINELY different case from "name failed to resolve", which never
// reaches this far (handled above via the FLT_MAX sentinel instead). So
// get_dist does not yet unblock m08's own forward progress (it still
// errors - just with a clean, labelled OPEN message instead of a raw
// nil-compare) until something writes a real position; implementing the
// full CONFIRMED contract faithfully, rather than fabricating 0,0,0 to
// force a number through, is what Sec4.2 actually specifies.
int stub_get_dist(lua_State* L) {
    logCall(L, upLog(L), "get_dist", upStateTag(L));
    std::string a = argString(L, 1);
    std::string b = argString(L, 2);
    bool vertTimes10 = optionalBoolDefault(L, 3, false);
    bool vertZero = optionalBoolDefault(L, 4, false); // checked after arg 3 - wins if both true, per Sec4.2
    EngineState* st = upState(L);
    if (!st->objectResolves().get(a) || !st->objectResolves().get(b)) {
        lua_pushnumber(L, static_cast<lua_Number>((std::numeric_limits<float>::max)()));
        return 1;
    }
    // REAL fallback (2026-10-10, m07 live-playtest follow-up): try the
    // real on-disk zone-object position for each name before falling back
    // to the diagnostic-only probe path below - see
    // EngineState::tryResolveWorldPositionFromZoneData()'s own doc comment.
    CharacterState& ca = st->getOrCreateCharacter(a);
    CharacterState& cb = st->getOrCreateCharacter(b);
    st->tryResolveWorldPositionFromZoneData(a, ca.worldPosition);
    st->tryResolveWorldPositionFromZoneData(b, cb.worldPosition);
    // DIAGNOSTIC ONLY (--probe-unresolved-names' own worldPositionOr()
    // extension, 2026-10-03): off by default, throws exactly as .get()
    // would until explicitly enabled; see that method's own doc comment.
    // Only still reached for a name with NEITHER a live-bridged position
    // NOR a real zone-object position (e.g. the one demo playtest vehicle).
    const WorldPosition kProbeOrigin{}; // CHOSEN (0,0,0), logged as probe - never a real baseline value
    WorldPosition pa = st->worldPositionOr(ca.worldPosition, kProbeOrigin);
    WorldPosition pb = st->worldPositionOr(cb.worldPosition, kProbeOrigin);
    double dx = pa.x - pb.x;
    double dy = pa.y - pb.y; // Y = vertical (spec-physics-runtime.md: gravity (0,-9.8,0), Y-up)
    double dz = pa.z - pb.z;
    if (vertZero) dy = 0.0;
    else if (vertTimes10) dy *= 10.0;
    lua_pushnumber(L, static_cast<lua_Number>(std::sqrt(dx * dx + dy * dy + dz * dz)));
    return 1;
}

// player_names_get_all (Sec51.4, ranking tranche 17): "resolved in full...
// a dense 1-based name table" of local + remote co-op players. Real
// mission-drive call shape confirmed directly
// (verdict_stub_hits_with_missions.tsv): argc=0 - no arguments observed,
// matching a plain "give me every player's name" query. This host models
// single player only (no remote co-op session ever exists here), so the
// table always holds exactly 1 entry: the local player's own name
// sentinel, reusing the literal "#PLAYER1#" Sec29.4 already establishes as
// a CONFIRMED pre-resolved name in this project's shared objectResolves()
// map (CHOSEN - the tranche summary doesn't spell out the exact string
// each table entry holds, only that it's "a dense 1-based name table", so
// reusing this project's own already-established player-name sentinel
// rather than inventing a different display-name format is the
// non-fabricating reading). Fixes the sh03/dlc3_m02 player_list nil-index
// blocker found via --trace-host-calls-before-error (results/
// trace_host_calls_20261003.md): player_names_get_all was a generic stub,
// always nil, feeding directly into game_lib.lua:1488's "attempt to index
// local 'player_list' (a nil value)".
int stub_player_names_get_all(lua_State* L) {
    logCall(L, upLog(L), "player_names_get_all", upStateTag(L));
    lua_newtable(L);
    lua_pushstring(L, "#PLAYER1#");
    lua_rawseti(L, -2, 1);
    return 1;
}


} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB05() {
    static const std::vector<std::string> names = {
        // Batch 2026-10-02 (resumed session): spec-lua-api-behaviour.md
        // Sec3.7/Sec20.1 vehicle-invulnerability bit-conflict resolution
        // (Sec46.1) plus a curated subset of ranking tranches 12-14
        // (Sec44/Sec45/Sec46) - see this file's own batch header comment
        // above (right before stub_turn_invulnerable) for the full
        // selection reasoning and the list of tranche-12/13/14 names
        // deliberately NOT included this pass. All 30 verified `gameplay`/
        // `ui` directly against tools/lua_all_registered_1490_tagged.txt
        // (grepped, not assumed): turn_invulnerable/turn_vulnerable/
        // vehicle_set_vulnerable/vehicle_is_invulnerable/character_fake_
        // revival_start/character_fake_revival_end/character_take_human_
        // shield_check_done/vehicle_turret_base_to_do/vehicle_lights_on/
        // vehicle_tire_indicators_alive/flee_to_navpoint/character_hidden/
        // ambient_gang_spawn_enable/cellphone_animate_stop_do/ambient_cop_
        // spawn_enable/action_nodes_shouldnt_flee/action_nodes_restrict_
        // spawning/vehicle_set_tire_durability/vehicle_set_tire_damage_
        // multiplier are `gameplay`; game_is_pc_dx11/game_get_ps3_button_
        // swap/game_record_mode_is_supported/game_record_mode_is_active/
        // game_show_party_ui/game_show_community_sessions_ui/cell_camera_
        // enable/cell_camera_is_enabled/whored_countdown_finished are `ui`.
        "turn_invulnerable",
        "turn_vulnerable",
        "vehicle_set_vulnerable",
        "vehicle_is_invulnerable",
        "character_fake_revival_start",
        "character_fake_revival_end",
        "character_take_human_shield_check_done",
        "vehicle_lights_on",
        "vehicle_tire_indicators_alive",
        "vehicle_turret_base_to_do",
        "game_is_pc_dx11",
        "game_get_ps3_button_swap",
        "game_record_mode_is_supported",
        "game_record_mode_is_active",
        "game_show_party_ui",
        "game_show_community_sessions_ui",
        "flee_to_navpoint",
        "character_hidden",
        "ambient_gang_spawn_enable",
        "cellphone_animate_stop_do",
        "cell_camera_enable",
        "cell_camera_is_enabled",
        "ambient_cop_spawn_enable",
        "action_nodes_shouldnt_flee",
        "action_nodes_restrict_spawning",
        "whored_countdown_finished",
        "vehicle_set_tire_durability",
        "vehicle_set_tire_damage_multiplier",
        // Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking
        // tranche 16"), 9 of its 25 names (the 8 explicitly mandated
        // crash guards/faithful quirks, PLUS spawning_boats - the one
        // name this batch's own fresh mission-drive run found a real
        // measured hit for - see this file's own Sec49 batch header
        // comment above for which and why). Real cluster tags verified
        // directly against tools/lua_all_registered_1490_tagged.txt
        // (grepped, not assumed): spawn_override_set_override_category_
        // for_hood/spawning_boats/skydive_move_to_check_done/squad_enable
        // are `gameplay`; every other name below is `ui`.
        "shop_purchase_purchase_shop",
        "spawn_override_set_override_category_for_hood",
        "spawning_boats",
        "skydive_move_to_check_done",
        "set_char_in_string",
        "store_stronghold_game_purchase_upgrade",
        "squad_enable",
        "sfx_use_load_images",
        "screen_capture_preview_should_upload",
        // Ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03) - the
        // 8 names with a real, non-zero mission-drive call count out of
        // the whole Sec50-Sec59 backlog (see this file's own doc comment
        // above each stub for the per-name citation).
        "tutorial_lock",
        "radio_newsbreak_clear",
        "group_create_do",
        "group_create_hidden_do",
        "dlc2_m02_clapboards_get",
        "dlc2_m02_clapboards_reset",
        "cutscene_play_do",
        "cutscene_play_check_done",
        // Real-hit probe-mode batch (2026-10-03): spec-lua-api-behaviour.md
        // Sec22.29/Sec7.21 - see this file's own doc comment above
        // stub_action_play_do for the full citation/reasoning. Both
        // `gameplay` in tools/lua_all_registered_1490_tagged.txt (grepped
        // directly).
        "action_play_do",
        "action_play_is_finished",
        // --trace-host-calls-before-error's first real use (2026-10-03,
        // results/trace_host_calls_20261003.md): both generic stubs were
        // feeding nil directly into a script-level nil-compare/nil-index.
        "get_dist",
        "player_names_get_all",
    };
    return names;
}

void registerB05(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {

    // Batch 2026-10-02 (resumed session): Sec3.7/Sec20.1/Sec46.1 vehicle-
    // invulnerability bit correction + curated Sec44/Sec45/Sec46 subset.
    if (wants("turn_invulnerable")) registerOne(L, state, log, stateTag, "turn_invulnerable", openGuard<stub_turn_invulnerable>);
    if (wants("turn_vulnerable")) registerOne(L, state, log, stateTag, "turn_vulnerable", openGuard<stub_turn_vulnerable>);
    if (wants("vehicle_set_vulnerable")) registerOne(L, state, log, stateTag, "vehicle_set_vulnerable", openGuard<stub_vehicle_set_vulnerable>);
    if (wants("vehicle_is_invulnerable")) registerOne(L, state, log, stateTag, "vehicle_is_invulnerable", openGuard<stub_vehicle_is_invulnerable>);
    if (wants("character_fake_revival_start")) registerOne(L, state, log, stateTag, "character_fake_revival_start", openGuard<stub_character_fake_revival_start>);
    if (wants("character_fake_revival_end")) registerOne(L, state, log, stateTag, "character_fake_revival_end", openGuard<stub_character_fake_revival_end>);
    if (wants("character_take_human_shield_check_done")) registerOne(L, state, log, stateTag, "character_take_human_shield_check_done", openGuard<stub_character_take_human_shield_check_done>);
    if (wants("vehicle_lights_on")) registerOne(L, state, log, stateTag, "vehicle_lights_on", openGuard<stub_vehicle_lights_on>);
    if (wants("vehicle_tire_indicators_alive")) registerOne(L, state, log, stateTag, "vehicle_tire_indicators_alive", openGuard<stub_vehicle_tire_indicators_alive>);
    if (wants("vehicle_turret_base_to_do")) registerOne(L, state, log, stateTag, "vehicle_turret_base_to_do", openGuard<stub_vehicle_turret_base_to_do>);
    if (wants("game_is_pc_dx11")) registerOne(L, state, log, stateTag, "game_is_pc_dx11", openGuard<stub_game_is_pc_dx11>);
    if (wants("game_get_ps3_button_swap")) registerOne(L, state, log, stateTag, "game_get_ps3_button_swap", openGuard<stub_game_get_ps3_button_swap>);
    if (wants("game_record_mode_is_supported")) registerOne(L, state, log, stateTag, "game_record_mode_is_supported", openGuard<stub_game_record_mode_is_supported>);
    if (wants("game_record_mode_is_active")) registerOne(L, state, log, stateTag, "game_record_mode_is_active", openGuard<stub_game_record_mode_is_active>);
    if (wants("game_show_party_ui")) registerOne(L, state, log, stateTag, "game_show_party_ui", openGuard<stub_game_show_party_ui>);
    if (wants("game_show_community_sessions_ui")) registerOne(L, state, log, stateTag, "game_show_community_sessions_ui", openGuard<stub_game_show_community_sessions_ui>);
    if (wants("flee_to_navpoint")) registerOne(L, state, log, stateTag, "flee_to_navpoint", openGuard<stub_flee_to_navpoint>);
    if (wants("character_hidden")) registerOne(L, state, log, stateTag, "character_hidden", openGuard<stub_character_hidden>);
    if (wants("ambient_gang_spawn_enable")) registerOne(L, state, log, stateTag, "ambient_gang_spawn_enable", openGuard<stub_ambient_gang_spawn_enable>);
    if (wants("cellphone_animate_stop_do")) registerOne(L, state, log, stateTag, "cellphone_animate_stop_do", openGuard<stub_cellphone_animate_stop_do>);
    if (wants("cell_camera_enable")) registerOne(L, state, log, stateTag, "cell_camera_enable", openGuard<stub_cell_camera_enable>);
    if (wants("cell_camera_is_enabled")) registerOne(L, state, log, stateTag, "cell_camera_is_enabled", openGuard<stub_cell_camera_is_enabled>);
    if (wants("ambient_cop_spawn_enable")) registerOne(L, state, log, stateTag, "ambient_cop_spawn_enable", openGuard<stub_ambient_cop_spawn_enable>);
    if (wants("action_nodes_shouldnt_flee")) registerOne(L, state, log, stateTag, "action_nodes_shouldnt_flee", openGuard<stub_action_nodes_shouldnt_flee>);
    if (wants("action_nodes_restrict_spawning")) registerOne(L, state, log, stateTag, "action_nodes_restrict_spawning", openGuard<stub_action_nodes_restrict_spawning>);
    if (wants("whored_countdown_finished")) registerOne(L, state, log, stateTag, "whored_countdown_finished", openGuard<stub_whored_countdown_finished>);
    if (wants("vehicle_set_tire_durability")) registerOne(L, state, log, stateTag, "vehicle_set_tire_durability", openGuard<stub_vehicle_set_tire_durability>);
    if (wants("vehicle_set_tire_damage_multiplier")) registerOne(L, state, log, stateTag, "vehicle_set_tire_damage_multiplier", openGuard<stub_vehicle_set_tire_damage_multiplier>);

    // Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking tranche 16"), 9 names.
    if (wants("shop_purchase_purchase_shop")) registerOne(L, state, log, stateTag, "shop_purchase_purchase_shop", openGuard<stub_shop_purchase_purchase_shop>);
    if (wants("spawn_override_set_override_category_for_hood")) registerOne(L, state, log, stateTag, "spawn_override_set_override_category_for_hood", openGuard<stub_spawn_override_set_override_category_for_hood>);
    if (wants("spawning_boats")) registerOne(L, state, log, stateTag, "spawning_boats", openGuard<stub_spawning_boats>);
    if (wants("skydive_move_to_check_done")) registerOne(L, state, log, stateTag, "skydive_move_to_check_done", openGuard<stub_skydive_move_to_check_done>);
    if (wants("set_char_in_string")) registerOne(L, state, log, stateTag, "set_char_in_string", openGuard<stub_set_char_in_string>);
    if (wants("store_stronghold_game_purchase_upgrade")) registerOne(L, state, log, stateTag, "store_stronghold_game_purchase_upgrade", openGuard<stub_store_stronghold_game_purchase_upgrade>);
    if (wants("squad_enable")) registerOne(L, state, log, stateTag, "squad_enable", openGuard<stub_squad_enable>);
    if (wants("sfx_use_load_images")) registerOne(L, state, log, stateTag, "sfx_use_load_images", openGuard<stub_sfx_use_load_images>);
    if (wants("screen_capture_preview_should_upload")) registerOne(L, state, log, stateTag, "screen_capture_preview_should_upload", openGuard<stub_screen_capture_preview_should_upload>);

    // Ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03).
    if (wants("tutorial_lock")) registerOne(L, state, log, stateTag, "tutorial_lock", openGuard<stub_tutorial_lock>);
    if (wants("radio_newsbreak_clear")) registerOne(L, state, log, stateTag, "radio_newsbreak_clear", openGuard<stub_radio_newsbreak_clear>);
    if (wants("group_create_do")) registerOne(L, state, log, stateTag, "group_create_do", openGuard<stub_group_create_do>);
    if (wants("group_create_hidden_do")) registerOne(L, state, log, stateTag, "group_create_hidden_do", openGuard<stub_group_create_hidden_do>);
    if (wants("dlc2_m02_clapboards_get")) registerOne(L, state, log, stateTag, "dlc2_m02_clapboards_get", openGuard<stub_dlc2_m02_clapboards_get>);
    if (wants("dlc2_m02_clapboards_reset")) registerOne(L, state, log, stateTag, "dlc2_m02_clapboards_reset", openGuard<stub_dlc2_m02_clapboards_reset>);
    if (wants("cutscene_play_do")) registerOne(L, state, log, stateTag, "cutscene_play_do", openGuard<stub_cutscene_play_do>);
    if (wants("cutscene_play_check_done")) registerOne(L, state, log, stateTag, "cutscene_play_check_done", openGuard<stub_cutscene_play_check_done>);
    // Real-hit probe-mode batch (2026-10-03): Sec22.29/Sec7.21.
    if (wants("action_play_do")) registerOne(L, state, log, stateTag, "action_play_do", openGuard<stub_action_play_do>);
    if (wants("action_play_is_finished")) registerOne(L, state, log, stateTag, "action_play_is_finished", openGuard<stub_action_play_is_finished>);
    // --trace-host-calls-before-error's first real use (2026-10-03).
    if (wants("get_dist")) registerOne(L, state, log, stateTag, "get_dist", openGuard<stub_get_dist>);
    if (wants("player_names_get_all")) registerOne(L, state, log, stateTag, "player_names_get_all", openGuard<stub_player_names_get_all>);
}

} // namespace stub_sections
} // namespace sr3luahost
