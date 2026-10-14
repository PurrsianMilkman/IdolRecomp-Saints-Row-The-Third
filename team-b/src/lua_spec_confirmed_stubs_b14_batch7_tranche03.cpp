// lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp
//
// Batch 2026-10-08 (wave 1, Batch 7 of a 7-agent parallel fan-out - see
// results/lua_natives_sec10_28_fanout_scoping_20261008.md Sec3/Sec5 for the
// full scoping rationale). spec-lua-api-behaviour.md Sec30 ("ranking
// tranche 03"), the 16 of its 25 names NOT already wired by
// lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp's own 9
// (vehicle_set_invulnerable_to_player_explosives, vehicle_disable_
// explosion_and_damage_vfx, vehicle_set_special_override_never_ghost,
// vehicle_clear_all_radio_locks, vehicle_is_vtol, auto_pickup_enable,
// vehicle_exit_group_do, vehicle_exit_group_check_done, team_make_
// unfriendly - sanity-checked against directly, see below):
//   character_dont_regenerate, character_clear_combat_move,
//   boss_battle_matt_set_player_as_avatar, boss_battle_matt_hide_wings,
//   audio_ambient_emitter_pause, audio_ambient_emitter_resume,
//   ai_suggest_action, ai_do_scripted_advance, vehicle_set_2d_tank_controls,
//   vehicle_rc_get_signal_strength, vehicle_stop_do, vehicle_pathfind_to_do,
//   vehicle_set_malfunctioning, vehicle_component_detach, turn_to_do,
//   teleport_vehicle_to_object.
//
// This is a NEW file per the refactor's own reviewer guidance (do not
// retrofit into b01-b07) - see lua_spec_confirmed_stubs_sections.h/
// lua_spec_confirmed_stubs_registry.cpp's own top comments for the shared-
// registration mechanism this file plugs into exactly the same way b01-b07
// already do.
//
// SHARED INFRASTRUCTURE REUSED FROM b03's OWN 9 SIBLINGS (read b03's file
// directly before writing this one, per this task's own instruction):
//   - objectResolves() (Sec29's "one shared global name map") for every
//     name resolution below - the SAME idiom b03 uses throughout
//     (`st->objectResolves().get(name)`), including the same "a name never
//     registered at all throws OpenStateError, a name explicitly
//     registered false is a real, honest resolve failure" discipline.
//   - getOrCreateVehicle()/getOrCreateCharacter(), VehicleState::
//     forceFlagGatePasses / CharacterState::controlLockGatePasses (both
//     CONFIRMED-true-in-single-player per Sec3.7/Sec6.22's resolution,
//     seeded at construction) - reused directly wherever Sec30's own text
//     describes "authority checked"/"single-gate" without citing a
//     DIFFERENT address, exactly the same "this whole gate FAMILY resolves
//     true in single player" reading b03's own vehicle force-flag setters
//     already rely on.
//   - VehicleState::bodyActive (+0xbd0==1, reused from Sec33.1's
//     vehicle_set_sirenlights, already wired in b03) for vehicle_component_
//     detach's own identical gate.
//   - VehicleState::flyingType ((+0xbf4)+0x2c==4, reused from b03's
//     vehicle_is_vtol/vehicle_is_helicopter/etc.) for vehicle_set_
//     malfunctioning's own identical VTOL-class gate.
//   - VehicleState::seat0Occupied (reused from Sec46.4's vehicle_turret_
//     base_to_do) for vehicle_pathfind_to_do's own identical "has a driver"
//     (seat 0) gate.
//   - CharacterState::currentVehicleName (reused from b03's vehicle_exit_
//     group_do/_check_done) for vehicle_stop_do/teleport_vehicle_to_
//     object's own "via character, uses its current vehicle" path.
//   - CharacterState::worldPosition/VehicleState::worldPosition (Sec4.2's
//     get_dist mechanism) for turn_to_do's position-equality check and
//     vehicle_rc_get_signal_strength's horizontal-distance formula.
//   - EngineState::hasLocalPlayer()/playerRig().coopPlayerPresent (Sec10.3/
//     Sec28.12) for boss_battle_matt_set_player_as_avatar's own worker
//     gates.
//   - caseInsensitiveEquals (lua_spec_confirmed_stubs_common.h) for
//     vehicle_component_detach's own case-insensitive component-name scan.
//
// NEW SHARED INFRASTRUCTURE THIS BATCH BUILDS (engine_state.h/
// lua_engine_state.cpp - see each one's own doc comment there for the full
// citation):
//   - The 70-entry CAE combat-action table (0x012e1c60, Sec30.4) +
//     EngineState::lookupCaeActionIndex() - consumed by ai_suggest_action/
//     ai_do_scripted_advance below.
//   - The per-character staged/committed CAE action record (character
//     +0x438..+0x4ff, Sec30.4) - CharacterState::caeStagedActionIndex/
//     caeStagedTargetName/caeCommittedActionIndex/caeCommittedTargetName/
//     caeAdvanceDestinationComputes.
//   - The shared script-request-id allocator (0x006f62b0, Sec30.5's own
//     preamble) - EngineState::allocateScriptRequestId()/
//     releaseScriptRequestId() - consumed by ai_suggest_action (channel 3),
//     turn_to_do (channel 0), vehicle_pathfind_to_do (channel 2).
//   - EngineState::stopVehicle() (0x00abe510, shared by vehicle_stop_do and
//     teleport_vehicle_to_object per Sec30.5/Sec30.6's own explicit cross-
//     reference).
//
// HARDENING POLICY (orchestrator's standing directive): vehicle_pathfind_
// to_do's own CONFIRMED crash-shaped edge (Sec30.5/Sec30.7: "Takes strlen
// of arg 2 before the NULL test - a non-string arg 2 is dereferenced") is
// the one genuine memory-safety bug found in this batch's 16 named
// natives. See stub_vehicle_pathfind_to_do's own doc comment below for the
// CHOSEN (hardened divergence), counted via EngineState::
// vehiclePathfindNonStringArg2GuardCount().
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cmath>

namespace sr3luahost {
namespace {

// =======================================================================
// Sec30.1 character cluster
// =======================================================================

// ---------------------------------------------------------------------
// character_dont_regenerate (Sec30.1, 0x00a41370). Arguments: 1 mandatory
// string (character name) + 1 boolean (lua_toboolean, no nil-gate, absent
// -> false - CONFIRMED). No return. Body: resolve the character (liveness
// guard - a null/unresolved character is a no-op); authority checked via
// the SAME single-gate 0x008addb0 (variant 2) CharacterState::
// controlLockGatePasses above already models (Sec7.7/Sec7.22/Sec7.25,
// CONFIRMED same address) - reused directly, not re-derived. With
// authority: writes the boolean into bit 0x2 of the SAME force-flag byte
// +0x1c98 forceFlags1c98 already models (Sec3.7); true -> -1 into +0x19c0
// (CharacterState::regenRestartDeadlineStamp, HYPOTHESIS meaning); false
// -> restamped to "now" (EngineState::nextMonotonicStamp(), this project's
// own CHOSEN clock stand-in). Without authority (CONFIRMED dead in single
// player, same as every other variant-2-gated setter in this project):
// replicate only, local flag NOT written.
// ---------------------------------------------------------------------
int stub_character_dont_regenerate(lua_State* L) {
    logCall(L, upLog(L), "character_dont_regenerate", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate, absent -> false
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set; unresolved/false -> liveness guard fails, no-op
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.controlLockGatePasses.get()) { // SAME single gate 0x008addb0, reused from Sec7.7/Sec7.22/Sec7.25
            c.forceFlags1c98.setBits(CharacterState::kDontRegenHealthBit1c98, flag ? CharacterState::kDontRegenHealthBit1c98 : 0u);
            c.regenRestartDeadlineStamp = flag ? -1 : st->nextMonotonicStamp();
        } else {
            EngineState::replicateStateChange("character_dont_regenerate", name); // opcode-0x46 record
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_clear_combat_move (Sec30.1, 0x00a410e0). Arguments: 1
// mandatory string (character name). No return. Body: resolves; does
// nothing for a null character or one whose word at +0x1358 is 0xffff
// (CharacterState::combatMoveSlot1358, CONFIRMED sentinel, HYPOTHESIS
// meaning "no combat move/state assigned"). Otherwise the real predicate
// 0x0095d720 and its two branches' own callees (0x0095f250/0x00967f60 +
// three 0x00957ee0 action-request calls with ids 0x1d53/0x1d54/0x1d8c) are
// each individually OPEN (Sec30.1's own text) and have no Lua-visible
// return value either way (this native returns none) - so only that the
// call proceeded past the +0x1358 gate is counted
// (CharacterState::combatMoveClearedCount), not which branch ran.
// ---------------------------------------------------------------------
int stub_character_clear_combat_move(lua_State* L) {
    logCall(L, upLog(L), "character_clear_combat_move", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set; unresolved -> null character, no-op
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.combatMoveSlot1358.get() != 0xffff) { // OPEN until set; CONFIRMED sentinel
            ++c.combatMoveClearedCount; // test-observable: gate passed; downstream predicate/effects OPEN, not modelled
        }
    }
    return 0;
}

// =======================================================================
// Sec30.2 boss_battle_matt_* cluster - both resolve the boss through a
// hardcoded literal, neither takes a character name from Lua.
// =======================================================================

// ---------------------------------------------------------------------
// boss_battle_matt_set_player_as_avatar (Sec30.2, 0x00a40930). Arguments:
// 1 boolean (lua_toboolean, absent -> false). Return: exactly 1 boolean on
// every path - the WORKER's own result, not the input. Body: global
// 0x0268d2d0 bit 0 (BossBattleMatt::avatarModeBit) is written
// UNCONDITIONALLY per the input flag, regardless of the worker's outcome
// (CONFIRMED). Worker: no local player (EngineState::hasLocalPlayer(),
// reused Sec10.3) -> false, no-op. flag true (become avatar): "npc_bahamut"
// lookup (BossBattleMatt::npcBahamutResolves) then the avatar model's own
// SEPARATE resolve (BossBattleMatt::avatarModelResolves, Sec30.2's own 2
// distinct failure sentences) - either failing -> false; both resolving:
// binds, repeats for the co-op player if present (playerRig().
// coopPlayerPresent, reused Sec28.12 - CONFIRMED no separate failure gate
// for that leg, and this project has no distinct co-op-player
// customization target to bind, so an unknown presence does NOT block
// success here - peeked via known()+get(), never forced through a plain
// get() the way this field's OTHER, branch-selecting consumers use it),
// host-side (coopLocalIsHost()) writes BossBattleMatt::
// hostAvatarActiveByte = true (global byte 0x012ec740), returns true. flag
// false (restore): "StyleTest_PC" lookup (BossBattleMatt::
// styleTestPcResolves) - not found -> CONFIRMED returns true WITHOUT
// restoring; found -> restores (the female/male rig-swap choice, CharacterState-
// style +0xa41 gender, is HYPOTHESIS and has no further Lua-visible
// consequence in scope, so it is not separately modelled), host writes
// hostAvatarActiveByte = false, returns CONFIRMED false (NOT a plain
// success flag).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_set_player_as_avatar(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_set_player_as_avatar", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0; // CONFIRMED: no nil-gate, absent -> false
    EngineState* st = upState(L);
    auto& matt = st->bossBattleMatt();
    matt.avatarModeBit = flag; // CONFIRMED: written unconditionally, independent of the worker's own outcome

    bool result;
    if (!st->hasLocalPlayer().get()) { // OPEN until set
        result = false; // CONFIRMED: no local player -> false, no-op
    } else if (flag) {
        if (matt.npcBahamutResolves.get() && matt.avatarModelResolves.get()) { // OPEN until set, both
            ++matt.becomeAvatarSuccessCount;
            // "repeats for the co-op player if present" - Sec30.2's own text states NO separate
            // failure gate for this leg, and this project has no distinct co-op-player
            // customization target to bind anyway (same single-player-focused scope every other
            // co-op-gated field in this project already has) - so whether a co-op player is
            // present has NO Lua-visible effect this host can model either way. CHOSEN: peek
            // without forcing a refusal (known-and-true, not .get()) rather than letting a
            // genuinely unrelated OPEN read block an otherwise fully-determined success path -
            // a deliberate narrower choice than playerRig().coopPlayerPresent's other, gating
            // consumers (e.g. Sec28.12/Sec9.5), where the read DOES select a real branch.
            (void)(st->playerRig().coopPlayerPresent.known() && st->playerRig().coopPlayerPresent.get());
            if (st->coopLocalIsHost()) matt.hostAvatarActiveByte = true; // CONFIRMED host-side write
            result = true;
        } else {
            result = false; // CONFIRMED: either "not found" (npc_bahamut) or model-resolve failure -> false
        }
    } else {
        if (!matt.styleTestPcResolves.get()) { // OPEN until set
            result = true; // CONFIRMED: "StyleTest_PC" not found -> true WITHOUT restoring
        } else {
            ++matt.restoreSuccessCount;
            if (st->coopLocalIsHost()) matt.hostAvatarActiveByte = false; // CONFIRMED host-side write
            result = false; // CONFIRMED: false after a successful restore - NOT a plain success flag
        }
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// boss_battle_matt_hide_wings (Sec30.2, 0x00a40850). Arguments: 1 optional
// boolean, nil-gated, default true. No return. Body: calls the already-
// catalogued inert always-1 stub 0x00d221a0 (Sec30.7, discarded - not
// modelled); resolves the LITERAL name "Matt" (0x005e5ae0 =
// 0x00a281a0("Matt", NULL), not an argument). If found: walks Matt's
// circular attached-object list for every "Avatar_wings"-hashed entry and
// hides/shows it (this project has no real attached-object list - only the
// last flag value is tracked, BossBattleMatt::wingsHidden). If hiding
// (flag true) AND this machine is host: stores Matt's object id into word
// global 0x012ec742 (CONFIRMED structure: hide + host only, never cleared
// on show) - this project has no real object-id scheme, so only the "was
// it stored" fact is tracked (BossBattleMatt::wingsHideHostMarkerSet).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_hide_wings(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_hide_wings", upStateTag(L));
    bool flag = optionalBoolDefault(L, 1, true); // CONFIRMED: nil-gated, default true
    EngineState* st = upState(L);
    if (st->objectResolves().get("Matt")) { // OPEN until set; resolves the hardcoded literal "Matt", not an argument
        auto& matt = st->bossBattleMatt();
        matt.wingsHidden = flag; // per-entry hide/show of every "Avatar_wings" attached object - not individually modelled
        if (flag && st->coopLocalIsHost()) {
            matt.wingsHideHostMarkerSet = true; // CONFIRMED: word global 0x012ec742 written on hide+host only, never cleared on show
        }
    }
    return 0;
}

// =======================================================================
// Sec30.3 global toggles and audio emitters
// =======================================================================

// ---------------------------------------------------------------------
// audio_ambient_emitter_pause / audio_ambient_emitter_resume (Sec30.3,
// 0x00a3c800 / 0x00a3c830). Identical shape. Arguments: 1 mandatory string
// (emitter name). No return. Body: name -> Wwise id (0x00462960, not
// modelled, no Wwise hash table anywhere in this project - same gap as
// game_audio_get_audio_id's own Wwise-hash resolver elsewhere), then scans
// the world object manager's ambient-emitter list (global 0x03171a64) for
// a match; match's handle is paused/resumed. This project has no real
// world-object-manager emitter list (same "no world objects in this host"
// precedent already established elsewhere) - EngineState::
// ambientEmitterKnown()/registerAmbientEmitterForTesting() stand in for
// it, same never-populated-registry convention as VdoObject: a name this
// host has never had registered honestly takes the real, CONFIRMED "no
// match -> nothing" path. OPEN: the 3 callee bodies (Sec30.3's own text);
// only the pause/resume state transition itself is modelled.
// ---------------------------------------------------------------------
int stub_audio_ambient_emitter_pause(lua_State* L) {
    logCall(L, upLog(L), "audio_ambient_emitter_pause", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->ambientEmitterKnown(name)) st->ambientEmitterPaused()[name] = true; // CONFIRMED: no match -> nothing
    return 0;
}

int stub_audio_ambient_emitter_resume(lua_State* L) {
    logCall(L, upLog(L), "audio_ambient_emitter_resume", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->ambientEmitterKnown(name)) st->ambientEmitterPaused()[name] = false; // CONFIRMED: no match -> nothing
    return 0;
}

// =======================================================================
// Sec30.4 AI cluster - the 70-entry CAE combat-action table
// (EngineState::lookupCaeActionIndex(), engine_state.h/
// lua_engine_state.cpp) and the per-character staged/committed action
// record (CharacterState::caeStagedActionIndex/caeStagedTargetName/
// caeCommittedActionIndex/caeCommittedTargetName/
// caeAdvanceDestinationComputes).
// =======================================================================

// ---------------------------------------------------------------------
// ai_suggest_action (Sec30.4, 0x00a3eb00). Arguments: 3 - arg 1 mandatory
// character name, arg 2 mandatory action name (one of the 70, or "NONE"),
// arg 3 optional target name (nil-gated, default none). No return. Body:
// unknown name (including "NONE") -> EngineState::lookupCaeActionIndex()
// returns the CONFIRMED sentinel 71 -> silent no-op (the real engine's own
// console print for a genuinely-unknown name has no visible effect in
// retail, per spec's own text); known name (0-69) -> arg 3, if resolvable,
// becomes the staged target name (this project tracks the target by NAME,
// not the real 12-byte handle/position pair - no coordinate system
// anywhere in this project), else empty ("an all-zero pair"); allocates a
// request id (EngineState::allocateScriptRequestId(), channel 3); stages
// (CONFIRMED: force=1 bypasses the busy test, so this ALWAYS stages,
// unconditionally) into CharacterState::caeStagedActionIndex/
// caeStagedTargetName. OPEN (Sec30.4's own text): how/whether the AI later
// commits or discards a staged suggestion - not modelled beyond the one
// staging write itself.
// ---------------------------------------------------------------------
int stub_ai_suggest_action(lua_State* L) {
    logCall(L, upLog(L), "ai_suggest_action", upStateTag(L));
    std::string character = argString(L, 1);
    std::string actionName = argString(L, 2);
    bool hasTarget = lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL;
    std::string target = hasTarget ? argString(L, 3) : "";
    EngineState* st = upState(L);
    if (st->objectResolves().get(character)) { // OPEN until set
        int32_t index = st->lookupCaeActionIndex(actionName); // CONFIRMED 71 sentinel for "NONE" or any unknown name
        if (index < 70) { // CONFIRMED: only a real 0-69 action index stages anything
            CharacterState& c = st->getOrCreateCharacter(character);
            std::string resolvedTarget;
            if (hasTarget && st->objectResolves().get(target)) resolvedTarget = target; // else "an all-zero pair" -> empty stand-in
            c.caeStagedActionIndex = index;
            c.caeStagedTargetName = resolvedTarget;
            (void)st->allocateScriptRequestId(character, /*channel=*/3); // 0x006f62b0, Sec30.5's own cross-function note
            ++c.caeSuggestCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// ai_do_scripted_advance (Sec30.4, 0x00a3e370). Arguments: 1 mandatory
// character name + 1 optional target name (nil-gated, default none).
// Return: exactly 1 boolean. Body: unresolved character -> false (this
// project's own consistent "unresolved subject -> false" default, same as
// every comparable query elsewhere in this document - the character's own
// resolve failure is not separately discussed by Sec30.4's text, which
// focuses on the target-given/no-target split). Target given: resolves via
// the established chain (this project's one flat objectResolves() gate
// stands in for the real two-resolver chain, 0x00a3df50 then 0x005982e0,
// Sec29's "one shared name map" convention) - resolved -> copies its
// position (tracked by NAME, no coordinate system) and COMMITS action 18
// ("move advance") immediately (CONFIRMED: 0x004f4d00 commits on the
// authority machine, CONFIRMED-true-in-single-player per the established
// gate-family resolution, so the "without authority" forward path is dead
// code here, same as every other such setter in this project), pushes
// true; unresolved -> false (CONFIRMED: "neither resolves -> false"). No
// target: whether 0x0053a520's own advance-destination computation
// succeeds is OPEN (CharacterState::caeAdvanceDestinationComputes) - true
// -> same commit, pushes true; false -> pushes false.
// ---------------------------------------------------------------------
int stub_ai_do_scripted_advance(lua_State* L) {
    logCall(L, upLog(L), "ai_do_scripted_advance", upStateTag(L));
    std::string character = argString(L, 1);
    bool hasTarget = lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    bool result;
    if (hasTarget) {
        std::string target = argString(L, 2);
        if (st->objectResolves().get(target)) { // OPEN until set
            c.caeCommittedActionIndex = 18; // "move advance"
            c.caeCommittedTargetName = target; // copies its position - stand-in: name only, no coordinate system
            ++c.caeAdvanceCommitCount;
            result = true;
        } else {
            result = false; // CONFIRMED: "neither resolves -> false"
        }
    } else if (c.caeAdvanceDestinationComputes.get()) { // OPEN until set - 0x0053a520's own success condition
        c.caeCommittedActionIndex = 18;
        ++c.caeAdvanceCommitCount;
        result = true;
    } else {
        result = false;
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec30.5 vehicle cluster
// =======================================================================

// ---------------------------------------------------------------------
// vehicle_set_2d_tank_controls (Sec30.5, 0x00a62d00). 1 boolean, no nil-
// gate. No return. Body: host sends an opcode-0x42 record (sub-header
// 0x1f, EngineState::replicateStateChange's own no-op call-site marker -
// this is a genuinely global write, not per-object, so the marker's own
// `objectName` parameter is empty) BEFORE storing the boolean into global
// byte 0x0278cad8 (EngineState::twoDTankControlsEnabled()). HYPOTHESIS:
// belongs to the RC-vehicle controls (adjacent global 0x0278cad4 is the RC
// signal range, see vehicle_rc_get_signal_strength below).
// ---------------------------------------------------------------------
int stub_vehicle_set_2d_tank_controls(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_2d_tank_controls", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0; // CONFIRMED: no nil-gate
    EngineState::replicateStateChange("vehicle_set_2d_tank_controls", ""); // CONFIRMED: sent before the store, every call, global state (no object)
    upState(L)->twoDTankControlsEnabled() = flag;
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_rc_get_signal_strength (Sec30.5, 0x00a64100). Arguments: 2
// mandatory strings (vehicle, character). Return: exactly 1 number, 0.0 on
// resolve failure (CONFIRMED). Body: 0x00a92430 returns `1 - d/R` where d
// is the HORIZONTAL-ONLY distance (X/Z, Y dropped - CONFIRMED, same Y-up
// convention get_dist's own Sec4.2 establishes) and R is float global
// 0x0278cad4 (EngineState::rcSignalRange(), OPEN - "OPEN: R's own source
// (only writer 0x00a906b0)"). CONFIRMED NOT clamped - negative beyond
// range. Reuses the SAME worldPosition/worldPositionOr mechanism get_dist
// (Sec4.2) already establishes.
// ---------------------------------------------------------------------
int stub_vehicle_rc_get_signal_strength(lua_State* L) {
    logCall(L, upLog(L), "vehicle_rc_get_signal_strength", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    std::string characterName = argString(L, 2);
    EngineState* st = upState(L);
    double result = 0.0; // CONFIRMED: 0.0 on resolve failure
    if (st->objectResolves().get(vehicleName) && st->objectResolves().get(characterName)) { // OPEN until set, each
        // REAL fallback (2026-10-10, m07 live-playtest follow-up) - see
        // EngineState::tryResolveWorldPositionFromZoneData()'s own doc
        // comment, same shared worldPosition field stub_get_dist uses.
        VehicleState& v = st->getOrCreateVehicle(vehicleName);
        CharacterState& c = st->getOrCreateCharacter(characterName);
        st->tryResolveWorldPositionFromZoneData(vehicleName, v.worldPosition);
        st->tryResolveWorldPositionFromZoneData(characterName, c.worldPosition);
        const WorldPosition kProbeOrigin{}; // same diagnostic-only convention as stub_get_dist
        WorldPosition pv = st->worldPositionOr(v.worldPosition, kProbeOrigin);
        WorldPosition pc = st->worldPositionOr(c.worldPosition, kProbeOrigin);
        double dx = pv.x - pc.x;
        double dz = pv.z - pc.z; // horizontal only, Y dropped (CONFIRMED)
        double d = std::sqrt(dx * dx + dz * dz);
        double R = st->rcSignalRange().get(); // OPEN until set
        result = 1.0 - d / R; // CONFIRMED: NOT clamped - negative beyond range
    }
    lua_pushnumber(L, static_cast<lua_Number>(result));
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_stop_do (Sec30.5, 0x00a67c80). 1 mandatory string: a character
// name (uses its current vehicle) or a vehicle name. No return. Body:
// character path requires a live vehicle via CharacterState::
// currentVehicleName (reused from b03's vehicle_exit_group_do), else does
// nothing (CONFIRMED: "vehicle-name path not tried as fallback"); vehicle
// path direct otherwise. CHOSEN resolution-order choice (this project has
// no name-to-type registry, Sec29's own "one shared name map" - same
// simplification get_dist's own doc comment states): a name this host
// already knows as a CHARACTER (hasCharacter()) takes the character path;
// every other resolved name takes the vehicle path directly. Authority
// checked on the character (controlLockGatePasses) or the vehicle
// (forceFlagGatePasses) per Sec30.5's own text - both CONFIRMED-true-in-
// single-player, so the "without authority" replicate-only branch
// (EngineState::stopVehicle()'s own false path) is dead code here, same as
// every other such setter in this project; still modelled for structural
// coverage.
// ---------------------------------------------------------------------
int stub_vehicle_stop_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_stop_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->hasCharacter(name)) { // CHOSEN resolution-order choice, Sec29
        if (!st->objectResolves().get(name)) return 0; // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        std::string vehicleName = c.currentVehicleName.get(); // OPEN until set
        if (vehicleName.empty()) return 0; // CONFIRMED: no live vehicle -> does nothing
        st->stopVehicle(vehicleName, c.controlLockGatePasses.get()); // "authority checked on the character"
    } else {
        if (!st->objectResolves().get(name)) return 0; // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        st->stopVehicle(name, v.forceFlagGatePasses.get()); // "authority checked ... on the vehicle (if direct)"
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_pathfind_to_do (Sec30.5, 0x00a680c0). Arguments: 4, positional,
// no nil-gates - arg 1 vehicle, arg 2 target name, arg 3/4 booleans
// (absent -> false). Return: exactly 1 value - boolean false on every
// failure, but on the main path a NUMBER (0 is truthy in Lua). Body: needs
// a vehicle with a driver - VehicleState::seat0Occupied (reused from
// Sec46.4's vehicle_turret_base_to_do, "seat 0 = driver"). HARDENING
// (CHOSEN, hardened divergence - orchestrator's standing policy, citing
// Sec30.5/Sec30.7): the real engine takes strlen() of arg 2 BEFORE testing
// its Lua type (CONFIRMED crash-shaped edge - "a non-string arg 2 is
// dereferenced"). This host never dereferences a non-string Lua value as a
// C string: a non-string/absent arg 2 takes the SAME path the real
// engine's own empty-string check would take (false) instead of crashing,
// counted via EngineState::vehiclePathfindNonStringArg2GuardCount(). Empty
// string or unresolvable target -> false. CHOSEN simplification (no name-
// to-type registry, Sec29): every resolved target is treated as the
// simpler "point-kind" case (2 points: vehicle, target) - the path-object
// multi-point case (up to 25 nodes) is NOT modelled (no path-node data in
// this project). With >=2 points: allocates a request id (channel 2,
// 0x006f62b0) and pushes 0x00b2b140's own return value
// (EngineState::pathfindResult(), OPEN - "OPEN: 0x00b2b140's own
// semantics/return meaning"); a 0 result releases the id (CONFIRMED).
// ---------------------------------------------------------------------
int stub_vehicle_pathfind_to_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_pathfind_to_do", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool arg3 = lua_toboolean(L, 3) != 0; // CONFIRMED: no nil-gate; forwarded to 0x00b2b140, not otherwise modelled (OPEN callee)
    bool arg4 = lua_toboolean(L, 4) != 0;
    (void)arg3;
    (void)arg4;
    EngineState* st = upState(L);

    if (!st->objectResolves().get(vehicleName)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    if (!v.seat0Occupied.get()) { // OPEN until set; CONFIRMED gate "needs a vehicle with a driver"
        lua_pushboolean(L, 0);
        return 1;
    }

    if (lua_gettop(L) < 2 || lua_type(L, 2) != LUA_TSTRING) {
        // CHOSEN (hardened divergence, Sec30.5/Sec30.7): never dereference a
        // non-string arg 2 as a C string - take the safe "empty string"
        // outcome instead of the real engine's own pre-NULL-check strlen().
        st->recordVehiclePathfindNonStringArg2Guard();
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string targetName = argString(L, 2);
    if (targetName.empty() || !st->objectResolves().get(targetName)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }

    int32_t requestId = st->allocateScriptRequestId(vehicleName, /*channel=*/2); // 0x006f62b0
    double result = st->pathfindResult().get(); // OPEN until set - 0x00b2b140's own return semantics
    if (result == 0.0) st->releaseScriptRequestId(vehicleName, 2); // CONFIRMED: "a 0 result releases the id"
    else st->registerVehiclePathfindRequest(vehicleName, targetName); // Sec9.10a: pending request, completes on arrival/abort
    (void)requestId;
    lua_pushnumber(L, result);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_set_malfunctioning (Sec30.5, 0x00a65540). Arguments: 1 mandatory
// vehicle name + 1 optional boolean, nil-gated, default true. No return.
// Body: does nothing unless the vehicle is class 4 (VTOL) - the ONLY gate
// (VehicleState::flyingType, reused from b03's vehicle_is_vtol/Sec33.1's
// vehicle_is_helicopter family, SAME real field). true: adds 100.0 to the
// local player's own screen-distortion float(s) +0x1650 (clamped [0,
// runtime max] - the upper bound is OPEN/not given a number by spec, so
// only the lower bound 0 is enforced here, a stated gap with no Lua-
// visible getter in scope either way), sets global byte 0x02625852,
// activates the "cyber_m16_glitch" effect slot, sets vehicle +0xf8 bits
// 0x1/0x2. false: zeroes the distortion float(s), clears the global byte
// and effect slot, clears vehicle +0xf8 bit 0x2 ONLY (CONFIRMED asymmetry
// - bit 0x1 is left alone on the false path). Both paths send an opcode-
// 0x43 record with NO authority/host gate - every machine sends it
// (EngineState::replicateStateChange() called unconditionally).
// ---------------------------------------------------------------------
int stub_vehicle_set_malfunctioning(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_malfunctioning", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true); // CONFIRMED: nil-gated, default true
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.flyingType.get() != 4) return 0; // OPEN until set; CONFIRMED: "the only gate" is class 4 (VTOL)

    if (flag) {
        double d = st->localPlayerScreenDistortion1650() + 100.0;
        st->localPlayerScreenDistortion1650() = (d < 0.0) ? 0.0 : d; // lower-bound clamp only; upper "runtime max" bound OPEN, not enforced
        st->malfunctionGlobalByte02625852() = true;
        v.forceFlagsF8.setBits(VehicleState::kMalfunctionBit1F8 | VehicleState::kMalfunctionBit2F8,
                                VehicleState::kMalfunctionBit1F8 | VehicleState::kMalfunctionBit2F8);
        st->cyberM16GlitchEffectActive() = true;
    } else {
        st->localPlayerScreenDistortion1650() = 0.0; // CONFIRMED: zeroes all 4 real floats; one scalar stand-in here
        st->malfunctionGlobalByte02625852() = false;
        st->cyberM16GlitchEffectActive() = false;
        v.forceFlagsF8.setBits(VehicleState::kMalfunctionBit2F8, 0u); // CONFIRMED: clears bit 0x2 ONLY, not bit 0x1
    }
    EngineState::replicateStateChange("vehicle_set_malfunctioning", name); // CONFIRMED: sent unconditionally, no authority/host gate
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_component_detach (Sec30.5, 0x00a63870). Arguments: 2 mandatory
// strings (vehicle, component name) + 4 optional, all nil-gated - arg 3/4
// booleans default true, arg 5 number default 0, arg 6 boolean default
// false. No return. Body: requires vehicle +0xbd0==1 (VehicleState::
// bodyActive, reused from Sec33.1's vehicle_set_sirenlights, already wired
// in b03); finds the component by case-insensitive name scan (miss -> -1,
// CONFIRMED nothing). This project has no real per-vehicle component tree
// (VehicleState::componentNames, test-populated only) - the recursive
// detach/spawn routine (0x00a8eab0) and its arg3-6-driven sub-behavior are
// NOT modelled (no component tree to recurse into; args consumed for shape
// only). Marks the component detached (bit 0x200000 of +0xf0,
// VehicleState::detachedComponents) on every path once found, regardless
// of outcome (CONFIRMED).
// ---------------------------------------------------------------------
int stub_vehicle_component_detach(lua_State* L) {
    logCall(L, upLog(L), "vehicle_component_detach", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    std::string componentName = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, true);
    bool arg4 = optionalBoolDefault(L, 4, true);
    double arg5 = optionalNumberDefault(L, 5, 0.0);
    bool arg6 = optionalBoolDefault(L, 6, false);
    (void)arg3; (void)arg4; (void)arg5; (void)arg6; // forwarded into the recursive detach/spawn routine (0x00a8eab0) - not modelled, no component tree in this project

    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    if (!v.bodyActiveOrDefault()) return 0; // ce4556f: +0xbd0 default 1 for a live vehicle

    std::string matched;
    for (const auto& c : v.componentNames) {
        if (caseInsensitiveEquals(c, componentName)) { matched = c; break; }
    }
    if (matched.empty()) return 0; // CONFIRMED: case-insensitive scan miss (-1) -> nothing
    v.detachedComponents.insert(matched); // CONFIRMED: marked detached on every path once found, regardless of outcome
    ++v.componentDetachAttempts;
    return 0;
}

// =======================================================================
// Sec30.6 character turn, vehicle teleport, team relation
// =======================================================================

// ---------------------------------------------------------------------
// turn_to_do (Sec30.6, 0x00a61e10). Arguments: 2 mandatory strings
// (character, target) + 1 optional boolean, nil-gated, default false.
// Return: exactly 1 boolean on every path. Body: character or target
// missing -> CONFIRMED true. Character already exactly on target position
// (arg 3 false, using the SAME established position resolver 0x00a455a0
// get_dist's own Sec4.2 already models - CharacterState::worldPosition) ->
// CONFIRMED true. Otherwise: allocates a request id (channel 0,
// 0x006f62b0); 0x00987410's own 3 OPEN branch outcomes, in order -
// CharacterState::turnRefusalPredicateFires (true -> releases the id,
// returns true); the SAME single-gate family reused for "local authority"
// (controlLockGatePasses, CONFIRMED-true-in-single-player, so this branch
// is dead code here but modelled for structural coverage - false ->
// replicate opcode-0x41, returns false); CharacterState::turnRequestBusy
// (true -> false); else CharacterState::turnStartRoutineSucceedsImmediately
// (the actual turn-start routine's own OPEN result). "The return reads as
// 'already done'" per Sec30.6's own closing note.
// ---------------------------------------------------------------------
int stub_turn_to_do(lua_State* L) {
    logCall(L, upLog(L), "turn_to_do", upStateTag(L));
    std::string character = argString(L, 1);
    std::string target = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, false); // CONFIRMED: nil-gated, default false
    EngineState* st = upState(L);

    if (!st->objectResolves().get(character) || !st->objectResolves().get(target)) { // OPEN until set, each
        lua_pushboolean(L, 1); // CONFIRMED: character or target missing -> true
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    CharacterState& t = st->getOrCreateCharacter(target); // SAME established position resolver as get_dist, Sec4.2/Sec30.6

    if (!arg3) {
        // REAL fallback (2026-10-10, m07 live-playtest follow-up) - see
        // EngineState::tryResolveWorldPositionFromZoneData()'s own doc comment.
        st->tryResolveWorldPositionFromZoneData(character, c.worldPosition);
        st->tryResolveWorldPositionFromZoneData(target, t.worldPosition);
        WorldPosition pc = c.worldPosition.get(); // OPEN until set
        WorldPosition pt = t.worldPosition.get();
        if (pc.x == pt.x && pc.y == pt.y && pc.z == pt.z) {
            lua_pushboolean(L, 1); // CONFIRMED: character already exactly on target position (arg3 false) -> true
            return 1;
        }
    }

    (void)st->allocateScriptRequestId(character, /*channel=*/0); // 0x006f62b0, Sec30.5's own preamble
    if (c.turnRefusalPredicateFires.get()) { // OPEN until set
        st->releaseScriptRequestId(character, 0);
        lua_pushboolean(L, 1); // CONFIRMED: refusal predicate fires -> true, releases the id
        return 1;
    }
    if (!c.controlLockGatePasses.get()) { // "false without local authority" - same single-gate family reused
        EngineState::replicateStateChange("turn_to_do", character); // opcode-0x41 turn request
        lua_pushboolean(L, 0);
        return 1;
    }
    if (c.turnRequestBusy.get()) { // OPEN until set
        lua_pushboolean(L, 0); // CONFIRMED: busy flag -> false
        return 1;
    }
    lua_pushboolean(L, c.turnStartRoutineSucceedsImmediately.get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// teleport_vehicle_to_object (Sec30.6, 0x00a60ae0). Arguments: 2 mandatory
// strings (arg 1 vehicle OR character - its current vehicle; arg 2 target
// object, via the general object chain) + 5 optional, all nil-gated - args
// 3/4/5 numbers default 0.0 (offsets), arg 6 number default 0.0 (heading
// degrees, *0.017453 -> radians), arg 7 boolean default true. No return.
// Body: target unresolved -> nothing (CONFIRMED). Vehicle via character
// path (CHOSEN resolution-order choice, same as vehicle_stop_do above) has
// NO extra gate; vehicle-name path is refused when VehicleState::
// teleportNameGateField0xbf8a8 (OPEN) is >= 0 (CONFIRMED: "applies only to
// the vehicle-name path"). Stops the vehicle (EngineState::stopVehicle(),
// SHARED with vehicle_stop_do per Sec30.5/Sec30.6's own explicit cross-
// reference), detaches all seats (counter only, no seat/occupant array
// beyond seat0Occupied). This project has no orientation/rotation data
// anywhere (WorldPosition is "plain xyz, no rotation") - the real 4x4 pose
// build/offset/rotate (0x00a874f0) is NOT simulated; only the raw request
// is recorded (VehicleState::lastTeleportRequest, same "record the
// request, not real physics" convention as MinimapIconRecord).
// ---------------------------------------------------------------------
int stub_teleport_vehicle_to_object(lua_State* L) {
    logCall(L, upLog(L), "teleport_vehicle_to_object", upStateTag(L));
    std::string name = argString(L, 1);
    std::string targetName = argString(L, 2);
    double ox = optionalNumberDefault(L, 3, 0.0);
    double oy = optionalNumberDefault(L, 4, 0.0);
    double oz = optionalNumberDefault(L, 5, 0.0);
    double headingDeg = optionalNumberDefault(L, 6, 0.0);
    double headingRad = headingDeg * 0.017453; // CONFIRMED conversion literal, Sec30.6
    bool arg7 = optionalBoolDefault(L, 7, true);

    EngineState* st = upState(L);
    if (!st->objectResolves().get(targetName)) return 0; // CONFIRMED: target unresolved -> nothing

    std::string vehicleName;
    if (st->hasCharacter(name)) { // CHOSEN resolution-order choice, Sec29 (same as vehicle_stop_do)
        if (!st->objectResolves().get(name)) return 0; // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        vehicleName = c.currentVehicleName.get(); // OPEN until set
        if (vehicleName.empty()) return 0; // no live vehicle
        // CONFIRMED: "vehicle via character path has no extra gate"
    } else {
        if (!st->objectResolves().get(name)) return 0; // OPEN until set
        VehicleState& v0 = st->getOrCreateVehicle(name);
        if (v0.teleportNameGateField0xbf8a8.get() >= 0) return 0; // OPEN until set; CONFIRMED refusal shape, vehicle-name path only
        vehicleName = name;
    }

    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    st->stopVehicle(vehicleName, v.forceFlagGatePasses.get()); // SHARED 0x00abe510, Sec30.5/Sec30.6
    ++v.teleportDetachAllSeatsCount; // "detaches all seats" - counter only, no seat/occupant array modelled beyond seat0Occupied

    VehicleState::TeleportRequest req;
    req.targetName = targetName;
    req.offsetX = ox; req.offsetY = oy; req.offsetZ = oz;
    req.headingRadians = headingRad;
    req.arg7 = arg7;
    v.lastTeleportRequest = req;
    ++v.teleportRequestCount;
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB14() {
    static const std::vector<std::string> names = {
        // Batch 2026-10-08 (Batch 7, dedicated agent): spec-lua-api-
        // behaviour.md Sec30 ("ranking tranche 03"), the 16 of its 25
        // names NOT already wired by lua_spec_confirmed_stubs_b03_vintui_
        // r03_r05.cpp's own 9. All 25 names in this tranche are `gameplay`
        // in tools/lua_all_registered_1490_tagged.txt (grepped directly by
        // the scoping report that dispatched this batch; re-verified for
        // these 16 specifically).
        "character_dont_regenerate",
        "character_clear_combat_move",
        "boss_battle_matt_set_player_as_avatar",
        "boss_battle_matt_hide_wings",
        "audio_ambient_emitter_pause",
        "audio_ambient_emitter_resume",
        "ai_suggest_action",
        "ai_do_scripted_advance",
        "vehicle_set_2d_tank_controls",
        "vehicle_rc_get_signal_strength",
        "vehicle_stop_do",
        "vehicle_pathfind_to_do",
        "vehicle_set_malfunctioning",
        "vehicle_component_detach",
        "turn_to_do",
        "teleport_vehicle_to_object",
    };
    return names;
}

void registerB14(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("character_dont_regenerate")) registerOne(L, state, log, stateTag, "character_dont_regenerate", openGuard<stub_character_dont_regenerate>);
    if (wants("character_clear_combat_move")) registerOne(L, state, log, stateTag, "character_clear_combat_move", openGuard<stub_character_clear_combat_move>);
    if (wants("boss_battle_matt_set_player_as_avatar")) registerOne(L, state, log, stateTag, "boss_battle_matt_set_player_as_avatar", openGuard<stub_boss_battle_matt_set_player_as_avatar>);
    if (wants("boss_battle_matt_hide_wings")) registerOne(L, state, log, stateTag, "boss_battle_matt_hide_wings", openGuard<stub_boss_battle_matt_hide_wings>);
    if (wants("audio_ambient_emitter_pause")) registerOne(L, state, log, stateTag, "audio_ambient_emitter_pause", openGuard<stub_audio_ambient_emitter_pause>);
    if (wants("audio_ambient_emitter_resume")) registerOne(L, state, log, stateTag, "audio_ambient_emitter_resume", openGuard<stub_audio_ambient_emitter_resume>);
    if (wants("ai_suggest_action")) registerOne(L, state, log, stateTag, "ai_suggest_action", openGuard<stub_ai_suggest_action>);
    if (wants("ai_do_scripted_advance")) registerOne(L, state, log, stateTag, "ai_do_scripted_advance", openGuard<stub_ai_do_scripted_advance>);
    if (wants("vehicle_set_2d_tank_controls")) registerOne(L, state, log, stateTag, "vehicle_set_2d_tank_controls", openGuard<stub_vehicle_set_2d_tank_controls>);
    if (wants("vehicle_rc_get_signal_strength")) registerOne(L, state, log, stateTag, "vehicle_rc_get_signal_strength", openGuard<stub_vehicle_rc_get_signal_strength>);
    if (wants("vehicle_stop_do")) registerOne(L, state, log, stateTag, "vehicle_stop_do", openGuard<stub_vehicle_stop_do>);
    if (wants("vehicle_pathfind_to_do")) registerOne(L, state, log, stateTag, "vehicle_pathfind_to_do", openGuard<stub_vehicle_pathfind_to_do>);
    if (wants("vehicle_set_malfunctioning")) registerOne(L, state, log, stateTag, "vehicle_set_malfunctioning", openGuard<stub_vehicle_set_malfunctioning>);
    if (wants("vehicle_component_detach")) registerOne(L, state, log, stateTag, "vehicle_component_detach", openGuard<stub_vehicle_component_detach>);
    if (wants("turn_to_do")) registerOne(L, state, log, stateTag, "turn_to_do", openGuard<stub_turn_to_do>);
    if (wants("teleport_vehicle_to_object")) registerOne(L, state, log, stateTag, "teleport_vehicle_to_object", openGuard<stub_teleport_vehicle_to_object>);
}

} // namespace stub_sections
} // namespace sr3luahost
