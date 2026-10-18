// lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp
//
// Wave 2, Batch W2-2 of the 680-name lua-natives backlog (2026-10-08) -
// spec-lua-api-behaviour.md Sec17 ("ranks 251-350 tranche, part A", 30
// gameplay-cluster names) + Sec18 ("ranks 251-350 tranche, part C", 30
// gameplay-cluster names), 60 names total - the highest real call volume
// in the entire backlog (8-13 calls each). A NEW file, own stub_*
// functions, own namesB21()/registerB21() pair, declared in
// lua_spec_confirmed_stubs_sections.h and wired into
// lua_spec_confirmed_stubs_registry.cpp's append/register chains
// (additively - nothing else touched).
//
// SHARED-STATE REUSE (per the orchestrator's own brief, not re-derived):
//   - hud_display_set_element (Sec17.26) extends HudDisplayStatePool
//     (engine_state.h, wave-1 Batch 5) with a SEPARATE 20-slot
//     active/value table alongside that struct's existing 5-slot alloc-
//     tracking arrays - the SAME real HUD display-state slot family, a
//     genuinely distinct sub-mechanism within it (OPEN whether the two
//     "active" concepts are the same real bit - not asserted).
//   - player_release_human_shield (Sec18.9) reuses the EXACT
//     statusByte28ac/28ad status-byte family (wave-1 Batch 4,
//     CharacterState::humanShieldHostageName/kHumanShieldLockedBit28ad)
//     for "is a shield currently held" - a NEW field,
//     humanShieldStateByte18d4, models the separate +0x18d4 multi-value
//     state byte the spec's own text confirms is distinct from those bits.
//   - helicopter_enter_retreat (Sec17.27) reuses
//     EngineState::HelicopterFlyTo::qualifies (wave-1 Batch 3/4,
//     "0x00ad31a0: heli AI record +0x2c in {3,4}") directly as its own
//     mode-gate - the SAME real address Sec17.27's own text cites
//     ("the SAME mode field 0x00ad2f80 reads in §17.4's loop"). The
//     EngineState::Helicopter struct (keyed by vehicle name) gains new
//     retreat-specific fields alongside its existing aiDriveStateOk.
//   - action_play_synced_is_finished / action_stop_synced_state (Sec18.5/
//     Sec18.4) pair with wave-1 Batch 4's own action_play_synced_state
//     (Sec14.1) - action_stop_synced_state reuses
//     CharacterState::syncedActionPartnerName/syncedActionPaired directly,
//     the SAME request-tracking fields that native's own local-apply path
//     writes.
//
// TRANSPOSITION RISKS (re-verified at implementation time, per the
// scoping brief's own warning):
//   - camera_shake_play_looping (Sec17.15) is kept fully separate from the
//     ALREADY-REGISTERED camera_shake_play (Sec13.15) - CameraShakeState
//     gained its own loopingProfile/loopingMagnitude/loopingActive/
//     loopingPlayCount fields, never touching lastProfile/lastMagnitude.
//   - inv_item_remove (Sec17.25) is this batch's OWN name; the sibling
//     wave-2 batch's own inv_item_remove_in_slot is a DIFFERENT, not-yet-
//     merged native - this file declares its own
//     inventoryItemRemoveAppliedCount/ReplicateCount fields, nothing
//     shared.
//   - tutorial_stop (Sec17.18) is this batch's OWN name; the sibling wave-2
//     batch's own tutorial_stop_current is a DIFFERENT, not-yet-merged
//     native - no field or name collision (tutorial_stop reuses the
//     EXISTING EngineState::tutorialLookup()/tutorialState() machinery
//     from the already-merged main-branch Sec6.19/Sec10.4/Sec26.28 work,
//     not anything from that sibling batch).
//
// HARDENING POLICY: one genuine judgment-call hardened divergence in this
// batch - hud_display_set_element (Sec17.26)'s direct-apply write indexes
// a fixed 12-sub-field array by a caller-supplied subIndex with no stated
// bounds check beyond the documented "== 0xf means all" special case, and
// a fixed 20-slot table by a caller-supplied slotId with no stated LOWER
// bound beyond "< 0x14". Both are bounded/refused rather than reproduced
// as a guessed OOB access, each counted via its own named counter and
// labeled CHOSEN (hardened divergence) at the stub body below. No other
// name in this batch's 60 was found to have a genuine memory-safety gap
// within this pass's clean-room (spec-text-only) scope.
//
// CLEAN-ROOM: every behaviour below is implemented strictly from
// spec-lua-api-behaviour.md Sec17/Sec18 (plus the Sec60.3 correction for
// mission_autosave, and Sec17.31/Sec18.31's own cross-function notes) -
// no executable, disassembly, or forbidden external source was read for
// this batch. Every genuinely OPEN detail no spec text resolves is
// modeled as an OpenValue/OpenValueMap/OpenBits32 (open_state.h) rather
// than guessed, EXCEPT where a name is explicitly RT-tagged (a confirmed
// real-hit in a measurement run) and the OPEN detail sits on that
// specific hot path with no in-scope reader depending on its real value -
// there, a plain, clearly-labeled CHOSEN default is used instead so a
// real run is not needlessly blocked (same standing convention this
// project already applies elsewhere, e.g. player_actions_enable's own
// "always takes the documented DEFAULT-case branch" choice).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include "sr3luahost/save_game_bridge.h"

#include <cmath>

namespace sr3luahost {
namespace {

// =======================================================================
// Section 17 batch (spec-lua-api-behaviour.md Sec17, "ranks 251-350
// tranche, part A", 30 names, wave 2 batch W2-2, 2026-10-08).
// =======================================================================

// vehicle_set_script_hard_goto (Sec17.1, 0x00a631d0). 1 mandatory string +
// 1 un-gated boolean (default false via lua_toboolean's own nil-handling).
// Return none. Resolves via objectResolves() (stand-in for 0x00a281e0).
// Double-gate (variant 1), the SAME +0xc68 sub-record vehicle_suppress_
// npc_exit/vehicle_ignore_repulsors already share - a NEW bit (+5 bit 0x4)
// on the EXISTING vehicleAiForceFlags byte (+5 bit 0x1, vehicle_set_no_
// chase, Sec33.1).
int stub_vehicle_set_script_hard_goto(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_script_hard_goto", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0; // CONFIRMED: un-gated, default false
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& veh = st->getOrCreateVehicle(name);
    if (veh.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        veh.vehicleAiForceFlags.setBits(VehicleState::kScriptHardGotoBit,
                                         v ? VehicleState::kScriptHardGotoBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_set_script_hard_goto", name);
    }
    return 0;
}

// vehicle_set_crazy (Sec17.2, 0x00a62e50). Same argument shape as Sec17.1.
// CONFIRMED a genuinely DIFFERENT, simpler dispatch - no shared +0xc68
// sub-record, no double-gate/record idiom reached within the spec's own
// one-level decompile budget (0x00b23f60's own body OPEN). Only the direct
// write is modeled; no replicateStateChange call is invented, since the
// spec explicitly does not confirm one exists at this call depth.
int stub_vehicle_set_crazy(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_crazy", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).crazyEnabled = v;
    return 0;
}

// set_perfect_aim (Sec17.3, 0x00a5d240). 1 mandatory string + 1 optional
// boolean, default true. Return none. Resolves via the generic/#PLAYER#-
// then-#FOLLOWER# chain 0x00a28150, resolveFollowerAwareObject() - wired
// 2026-10-08 (follower-sentinel-resolver pass). Double-gate (variant 1),
// identity +8/+0xc: an EIGHTH confirmed bit on the shared +0x1c98 flags
// field.
int stub_set_perfect_aim(lua_State* L) {
    logCall(L, upLog(L), "set_perfect_aim", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kPerfectAimBit1c98,
                                  v ? CharacterState::kPerfectAimBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("set_perfect_aim", resolvedName);
    }
    return 0;
}

// set_civilians_flee (Sec17.4, 0x00a5e970) - the largest non-COMPLEX entry
// in Sec17-22. 1 un-gated boolean (flee, default false) + 1 optional
// boolean (default true, forwarded into the OPEN-role two-global-write
// helper 0x009060e0) + 1 optional string (anchor, default absent). Return
// none. No generic world-object registry exists in this project (stated,
// project-wide gap) - the global tracked-object array (0x03171a64) is
// modeled as a never-populated-by-default test registry
// (EngineState::registerCivilianForTesting), so the mass-update loop is a
// safe no-op by default. Per-civilian directive applied via the SAME
// double-gate (forceFlagGatePasses) every other force-flag setter in this
// project shares, CONFIRMED to be the identical 0x008ae480/0x008837a0
// pair (just a different opcode, 0x42, at this call site).
int stub_set_civilians_flee(lua_State* L) {
    logCall(L, upLog(L), "set_civilians_flee", upStateTag(L));
    bool flee = lua_toboolean(L, 1) != 0; // CONFIRMED: un-gated, default false
    bool arg2 = optionalBoolDefault(L, 2, true);
    std::string anchorArg = optionalStringDefault(L, 3, "");
    EngineState* st = upState(L);

    // 0x009060e0's own role is OPEN beyond "writes two globals, gated
    // pointer-block copy" - tracked honestly, not guessed.
    st->civilianFleeHelperGlobalA() = arg2;
    st->civilianFleeHelperGlobalB() = arg2;
    if (arg2) ++st->civilianFleeHelperPointerCopyCount();

    std::string anchorName;
    if (flee && !anchorArg.empty() && st->objectResolves().get(anchorArg)) { // OPEN until set
        anchorName = anchorArg;
    }
    st->lastCivilianFleeAnchorName() = anchorName;

    for (const auto& name : st->civilianNames()) {
        if (!st->objectResolves().get(name)) continue; // liveness gate, OPEN until set
        if (st->civilianFleeAiModeSkip().get(name)) continue; // OPEN until set per name
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            c.fleeActive = flee;
            c.fleeAnchorName = flee ? anchorName : std::string(); // "" = self-position variant when flee+no anchor
            ++c.fleeDirectiveApplyCount;
        } else {
            EngineState::replicateStateChange("set_civilians_flee", name);
            ++c.fleeDirectiveReplicateCount;
        }
    }
    return 0;
}

// set_attack_enemies_flag (Sec17.5, 0x00a5f000). 1 mandatory string + 1
// optional boolean, default true. Return none. Resolves via objectResolves()
// (0x005e4dd0 stand-in). CONFIRMED: bit 0x2 of +0xac is flipped
// UNCONDITIONALLY, before the vtable +0x68 predicate (OPEN) is even
// evaluated; only if that predicate is truthy does the double-gate
// "human_ai_data" sub-object setter (+9 bit 0x2) run. Structurally
// identical to set_attack_player_flag's own "two flag locations, only one
// replicates" split (Sec7.10) - an independent sibling instance, not a
// reuse of those exact fields.
int stub_set_attack_enemies_flag(lua_State* L) {
    logCall(L, upLog(L), "set_attack_enemies_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    c.attackEnemiesBitAc = v; // CONFIRMED unconditional, unreplicated
    if (c.attackEnemiesSecondaryGatePredicate.known() ? c.attackEnemiesSecondaryGatePredicate.get() : true) { // Sec17.5/Sec7.10 CHOSEN rule (2026-10-12): vtable +0x68 true for a resolved live character
        if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            c.aiDataFlags9.setBits(CharacterState::kAttackEnemiesOnSightBit9,
                                    v ? CharacterState::kAttackEnemiesOnSightBit9 : 0u);
        } else {
            EngineState::replicateStateChange("set_attack_enemies_flag", name);
        }
    }
    return 0;
}

// player_force_vehicle_seat (Sec17.6, 0x00a5a8e0). 1 mandatory string + 1
// mandatory number (seat index, round-to-int) + 1 optional string (vehicle
// name), default absent. Return none. Two independent global writes, not
// gated on each other (Sec17.6's own flagged item). The seat-index half is
// per-target, gated by forceFlagGatePasses; the vehicle-identity half is
// tracked globally (EngineState::forcedSeatVehicleName), proceeding
// regardless of whether the target resolved.
int stub_player_force_vehicle_seat(lua_State* L) {
    logCall(L, upLog(L), "player_force_vehicle_seat", upStateTag(L));
    std::string name = argString(L, 1);
    int32_t seat = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 2)));
    std::string vehicleName = optionalStringDefault(L, 3, "");
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            c.forcedVehicleSeatIndex = seat;
        } else {
            EngineState::replicateStateChange("player_force_vehicle_seat", name);
        }
    }
    // Independently of whether the target resolved (CONFIRMED, Sec17.6).
    if (vehicleName.empty()) {
        st->forcedSeatVehicleName() = ""; // the fixed default 8-byte struct
    } else if (st->objectResolves().get(vehicleName)) { // OPEN until set
        st->forcedSeatVehicleName() = vehicleName;
    }
    return 0;
}

// on_human_killed (Sec17.7, 0x00a56f00). 1 optional string, nil-gated
// pointer-null check. Return none. A GLOBAL (not per-object) hook-slot-array
// member, slot 0xb (11). CORRECTED 2026-10-14: the string is the callback
// name (the earlier "human kind filter" reading is wrong), see the body.
int stub_on_human_killed(lua_State* L) {
    logCall(L, upLog(L), "on_human_killed", upStateTag(L));
    // CORRECTED 2026-10-14 (spec sync bfe0542, "Engine callback dispatch: death and destruction" section C, FINAL):
    // the single string is the CALLBACK NAME, interned straight into global slot 0xb - not a human-kind filter, and
    // there is no resolve step. One global registration: the last call wins, "" or nil clears. The engine fires it
    // from the NPC death finalizer (EngineState::tickCharacterDeaths) with (victim_team, killer_or_nil, victim_name).
    std::string callback = optionalStringDefault(L, 1, "");
    EngineState* st = upState(L);
    st->onHumanKilledCallback() = callback;
    st->onHumanKilledFilterName().clear(); // legacy field of the superseded "filter" reading: never set any more
    return 0;
}

// mesh_mover_destroyed (Sec17.8, 0x00a52f00). 1 mandatory string. Return 1
// boolean. Resolves via objectResolves() (0x006434a0 stand-in). Reuses the
// EXISTING +0xc0/+0xdc sub-object presence facts (hasScriptedKillSubObjectC0/
// hasPlainKillSubObjectDc, Sec14.10) directly: +0xc0 non-null -> test its
// own +0x138 pointer; else fall back to +0xdc directly. A non-null result
// means "not destroyed"; null means "destroyed".
int stub_mesh_mover_destroyed(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_destroyed", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    PlacedObjectState& p = st->getOrCreatePlacedObject(name);
    bool notDestroyed = p.hasScriptedKillSubObjectC0 ? p.scriptedSubObjectField138NonNull.get() // OPEN until set
                                                      : p.hasPlainKillSubObjectDc;
    lua_pushboolean(L, notDestroyed ? 0 : 1);
    return 1;
}

// character_set_persona (Sec17.9, 0x00a427c0). 2 mandatory strings
// (character; persona name). Return none. Resolves via objectResolves()
// (0x00a281a0 stand-in). The persona name->id hash (0x0046fd00) collapses
// to this project tracking the NAME directly (no fixed id table). The
// single gate at this call site always evaluates its own literal-0 half as
// true, so "gate-false AND arg-zero" collapses to just "gate-false" -
// reused directly as controlLockGatePasses ("variant 2" family, CONFIRMED
// true in single player). The second, parallel opcode-0x41 record
// (Sec17.9's own flagged item) is OPEN and not modeled - nothing in scope
// observes it.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_persona(lua_State* L) {
    logCall(L, upLog(L), "character_set_persona", upStateTag(L));
    std::string name = argString(L, 1);
    std::string persona = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.controlLockGatePasses.get()) { // CONFIRMED true in single player, Sec7.7/Sec6.22
        c.currentPersonaName = persona;
        ++c.personaApplyCount;
    } else {
        EngineState::replicateStateChange("character_set_persona", resolvedName);
    }
    return 0;
}

// character_set_never_fall (Sec17.10, 0x00a424a0). 1 mandatory string + 1
// optional boolean, default true. Return none. Double-gate (variant 1),
// identity +8/+0xc: a NEW bit (0x10000) on the EXISTING forceFlags1c9c
// dword (0x4, no_explosive_panic, Sec13.5).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_never_fall(lua_State* L) {
    logCall(L, upLog(L), "character_set_never_fall", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kNeverFallBit1c9c,
                                  v ? CharacterState::kNeverFallBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_set_never_fall", resolvedName);
    }
    return 0;
}

// character_set_attack_multiplier (Sec17.11, 0x00a422d0). 1 mandatory
// string + 1 mandatory number (raw float, no rounding). Return none.
// Double-gate (variant 1), identity +8/+0xc: writes the float directly to
// +0x1cc0 (a value field, not a bit). CHOSEN default 1.0 ("no scaling" - no
// stated construction value anywhere in scope).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_attack_multiplier(lua_State* L) {
    logCall(L, upLog(L), "character_set_attack_multiplier", upStateTag(L));
    std::string name = argString(L, 1);
    double mult = lua_tonumber(L, 2); // CONFIRMED: raw float, no rounding
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.attackMultiplier = mult;
    } else {
        EngineState::replicateStateChange("character_set_attack_multiplier", resolvedName);
    }
    return 0;
}

// character_ragdoll (Sec17.12, 0x00a43140). 1 mandatory string + 5 further
// optional arguments (duration ms, default 3600000; impulse magnitude,
// default a fixed constant; source name, default absent; 2 booleans,
// default true/false). Return none. No real physics system in this
// project (stated, project-wide gap) - only the raw request is tracked.
// Duration 0 means "clear ragdoll" (CONFIRMED).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_ragdoll(lua_State* L) {
    logCall(L, upLog(L), "character_ragdoll", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t durationMs = truncateEa2596(optionalNumberDefault(L, 2, 3600000.0));
    double magnitude = optionalNumberDefault(L, 3, 0.0); // CHOSEN placeholder - real constant (0x012a2d54) value unknown here
    std::string source = optionalStringDefault(L, 4, "");
    bool arg5 = optionalBoolDefault(L, 5, true);
    (void)arg5; // world/local-space flag forwarded to the un-modeled impulse apply, Sec17.12 - no physics in scope
    bool arg6 = optionalBoolDefault(L, 6, false);
    (void)arg6;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    c.ragdollDurationMs = durationMs;
    if (durationMs == 0) {
        ++c.ragdollClearCount;
        return 0;
    }
    ++c.ragdollStartCount;
    bool sourceResolves = !source.empty() && st->objectResolves().known(source) && st->objectResolves().get(source);
    c.ragdollImpulseMagnitude = magnitude;
    c.ragdollImpulseWasDirectional = sourceResolves;
    return 0;
}

// character_add_prop (Sec17.13, 0x00a41030). 2 mandatory strings
// (character; prop/customization-item name). Return none. Resolves via
// objectResolves(); an unresolved character is a no-op (CONFIRMED). No
// real worn-prop object graph in this project - only successfully-
// attached prop names are recorded, gated on
// EngineState::propDefinitionResolves() (collapsing the two real lookups).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_add_prop(lua_State* L) {
    logCall(L, upLog(L), "character_add_prop", upStateTag(L));
    std::string name = argString(L, 1);
    std::string propName = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!st->propNameResolves(propName)) return 0; // items_3d catalogue once installed (f752324), else OPEN until set
    st->getOrCreateCharacter(resolvedName).attachedProps.push_back(propName);
    return 0;
}

// camera_shake_stop (Sec17.14, 0x00a44d50). No arguments. Return none.
// Zeroes the global camera-shake parameter block and releases any live
// shake-runtime object; UNCONDITIONALLY opens a network record (opcode
// 0x43, subtype 0xf) - the SAME shared camera-shake record family
// camera_shake_play_looping below corroborates.
int stub_camera_shake_stop(lua_State* L) {
    logCall(L, upLog(L), "camera_shake_stop", upStateTag(L));
    EngineState* st = upState(L);
    auto& shake = st->cameraShake();
    shake.loopingActive = false;
    ++shake.stopCount;
    EngineState::replicateStateChange("camera_shake_stop", "");
    return 0;
}

// camera_shake_play_looping (Sec17.15, 0x00a44ba0). 1 mandatory string
// (shake-effect name) + 1 optional number, default a fixed constant.
// Return none. "No gate at all, always both" idiom: applies the effect AND
// UNCONDITIONALLY opens a network record (opcode 0x43, subtype 0xf, the
// SAME recovered subtype camera_shake_stop uses). Kept fully separate from
// the already-registered camera_shake_play (Sec13.15) per this batch's own
// transposition-risk note.
int stub_camera_shake_play_looping(lua_State* L) {
    logCall(L, upLog(L), "camera_shake_play_looping", upStateTag(L));
    std::string effectName = argString(L, 1);
    double magnitude = optionalNumberDefault(L, 2, 1.0); // CHOSEN placeholder - real constant (0x01117a4c) value unknown here
    EngineState* st = upState(L);
    auto& shake = st->cameraShake();
    shake.loopingProfile = effectName;
    shake.loopingMagnitude = magnitude;
    shake.loopingActive = true;
    ++shake.loopingPlayCount;
    EngineState::replicateStateChange("camera_shake_play_looping", "");
    return 0;
}

// ai_attack_region (Sec17.16, 0x00a3e1c0). 1 mandatory string (character,
// resolved via 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08,
// follower-sentinel-resolver pass) + 1 mandatory string (region name) + 1
// mandatory number (radius, or a negative sentinel meaning "clear").
// Return none. A region-name-aware WRAPPER around the SAME underlying
// leash-clear/leash-set primitives npc_leash_remove (Sec7.1)/npc_leash_to_
// object (Sec15.22) already model - reused directly, not re-derived.
// Negative radius clears via the SAME leash-CLEAR fields npc_leash_remove
// uses; a resolved region feeds the SAME leash-to-explicit-position fields
// npc_leash_to_object uses.
int stub_ai_attack_region(lua_State* L) {
    logCall(L, upLog(L), "ai_attack_region", upStateTag(L));
    std::string name = argString(L, 1);
    std::string region = argString(L, 2);
    double radius = lua_tonumber(L, 3); // CONFIRMED: unconditional read, no nil-gate
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (radius < 0.0) {
        // CONFIRMED: the SAME leash-CLEAR setter npc_leash_remove (Sec7.1) uses.
        if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            c.leashRadius.set(-1.0);
            c.leashPosition.forget();
        } else {
            EngineState::replicateStateChange("ai_attack_region", resolvedName);
        }
        return 0;
    }
    if (st->objectResolves().get(region)) { // OPEN until set - flat stand-in for the two per-kind resolvers
        // The SAME leash-to-explicit-position wrapper npc_leash_to_object
        // (Sec15.22) already models - reused directly (0x004e7db0).
        c.npcLeashAnchorName = region;
        c.npcLeashRadius = radius;
        c.npcLeashArg4 = false;
        ++c.npcLeashSetCount;
    }
    return 0;
}

// vehicle_prevent_transition_to_ambient (Sec17.17, 0x00a62c40). Same
// argument shape as Sec17.1. Resolves via objectResolves(). Double-gate
// (variant 1), identity at the sub-record's own +0x4e0/+0x4e4: a SECOND
// confirmed bit-offset within the +0xc68 sub-record - a NEW byte (+7 bit
// 0x4), distinct from vehicleAiForceFlags (+5) and vehicleAiForceFlagsByte4
// (+4).
int stub_vehicle_prevent_transition_to_ambient(lua_State* L) {
    logCall(L, upLog(L), "vehicle_prevent_transition_to_ambient", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& veh = st->getOrCreateVehicle(name);
    if (veh.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        veh.vehicleAiForceFlagsByte7.setBits(VehicleState::kNeverGoAmbientBit,
                                              v ? VehicleState::kNeverGoAmbientBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_prevent_transition_to_ambient", name);
    }
    return 0;
}

// tutorial_stop (Sec17.18, 0x00a601f0). 1 mandatory string (tutorial
// name). Return none. Case-insensitive lookup against the SAME 210-entry
// table tutorial_start/tutorial_advance already model
// (EngineState::tutorialLookup()) - reused directly, not re-derived.
// Writes state code 4 ("stopped") into the SAME per-tutorial state array
// (EngineState::tutorialState()). The coordinated-co-op-session gate
// (0x0087ba20) reuses coopSession().present directly - CHOSEN false by
// default (single-player-only project stance), so the replicate branch is
// dead unless a test installs a session; the two further predicates
// (0x00bc55a0/0x007157c0) gating that branch are consequently never
// reached in scope and are not separately modeled.
int stub_tutorial_stop(lua_State* L) {
    logCall(L, upLog(L), "tutorial_stop", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    EngineState::TutorialLookup lookup = EngineState::tutorialLookup(name);
    if (lookup.index < 0) {
        if (lookup.couldBeIndex176) {
            // Genuinely ambiguous - index 176's own real string is OPEN
            // (Sec26.28) - refuse honestly rather than guess a match.
            throw OpenStateError("tutorial name table entry 176 (unresolved string)",
                                 "spec-lua-api-behaviour.md Sec17.18/Sec26.28");
        }
        return 0; // CONFIRMED real "no match" outcome - a clean no-op
    }
    st->tutorialState().set(EngineState::tutorialStateKey(lookup.index), 4);
    if (st->coopSession().present.get()) { // CONFIRMED false by default (single player)
        EngineState::replicateStateChange("tutorial_stop", name);
    }
    return 0;
}

// set_stag_active (Sec17.19, 0x00a5d640). 1 un-gated boolean, default
// false. Return none. Writes directly to a single global byte - no
// resolver, no gate, no record of any kind - the simplest function in this
// batch.
int stub_set_stag_active(lua_State* L) {
    logCall(L, upLog(L), "set_stag_active", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0; // CONFIRMED: un-gated, default false
    upState(L)->stagActive() = v;
    return 0;
}

// set_cant_cower_flag (Sec17.20, 0x00a5ce10). 1 mandatory string + 1
// optional boolean, default true. Return none. Resolves via 0x00a28150,
// resolveFollowerAwareObject() - wired 2026-10-08 (follower-sentinel-
// resolver pass). Double-gate (variant 1), identity +0x610/+0x614
// (the SAME "human_ai_data" sub-object family) - a THIRD confirmed bit on
// object +0xa (followerCanAbandon's own 0x2, Sec7.6; cantFleeFlag's own
// 0x20, Sec14.26), kept as its own independent field per this header's
// established "two bits of the same byte, independent fields" convention.
int stub_set_cant_cower_flag(lua_State* L) {
    logCall(L, upLog(L), "set_cant_cower_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.cantCowerFlag = v;
    } else {
        EngineState::replicateStateChange("set_cant_cower_flag", resolvedName);
    }
    return 0;
}

// persona_set_global_situation_override (Sec17.21, 0x00a59d10). 1
// mandatory number (round-to-int, range-checked [1,9], out-of-range
// silently ignored). Return none. Caches into a global; network-
// replicated only inside a coordinated co-op session (coopSession().present,
// CHOSEN false by default).
int stub_persona_set_global_situation_override(lua_State* L) {
    logCall(L, upLog(L), "persona_set_global_situation_override", upStateTag(L));
    int64_t v = truncateEa2596(lua_tonumber(L, 1));
    if (v < 1 || v > 9) return 0; // CONFIRMED: out-of-range silently ignored
    EngineState* st = upState(L);
    st->setPersonaGlobalSituationOverride(static_cast<int32_t>(v));
    if (st->coopSession().present.get()) { // CONFIRMED false by default (single player)
        EngineState::replicateStateChange("persona_set_global_situation_override", "");
    }
    return 0;
}

// persona_set_dlc_bundle (Sec17.22, 0x00a59ce0). 1 mandatory number
// (round-to-int). Return none. Only acts if the value differs from the
// cached global (CONFIRMED change-detection); replicated only inside a
// coordinated co-op session.
int stub_persona_set_dlc_bundle(lua_State* L) {
    logCall(L, upLog(L), "persona_set_dlc_bundle", upStateTag(L));
    int32_t v = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 1)));
    EngineState* st = upState(L);
    if (v == st->personaDlcBundle()) return 0; // CONFIRMED: no-op if unchanged
    st->personaDlcBundle() = v;
    if (st->coopSession().present.get()) { // CONFIRMED false by default (single player)
        EngineState::replicateStateChange("persona_set_dlc_bundle", "");
    }
    return 0;
}

// Defined in lua_spec_confirmed_stubs_b21_wave2_sec17_18_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b21 {
int stub_mission_autosave(lua_State* L);
int stub_light_group_set_intensity(lua_State* L);
int stub_inv_item_remove(lua_State* L);
int stub_hud_display_set_element(lua_State* L);
int stub_helicopter_enter_retreat(lua_State* L);
int stub_door_is_open(lua_State* L);
int stub_dlc_gis_film_grain_unload(lua_State* L);
int stub_difficulty_get_level(lua_State* L);
int stub_cash_add(lua_State* L);
int stub_boss_battle_matt_set_matt_flying(lua_State* L);
int stub_ai_do_scripted_take_cover(lua_State* L);
int stub_action_stop_synced_state(lua_State* L);
int stub_action_play_synced_is_finished(lua_State* L);
int stub_spawn_override_change_hood_alternate_spawning(lua_State* L);
int stub_shop_ignore_clerk(lua_State* L);
int stub_set_animation_set(lua_State* L);
int stub_player_release_human_shield(lua_State* L);
int stub_player_is_rc_driver(lua_State* L);
int stub_on_incapacitated(lua_State* L);
int stub_notoriety_force_no_air_spawn(lua_State* L);
int stub_helicopters_set_jitter_override(lua_State* L);
int stub_group_members_alive(lua_State* L);
int stub_flashpoints_enable(lua_State* L);
int stub_character_set_immune_to_explosion_vomit(lua_State* L);
int stub_character_set_always_combat_ready(lua_State* L);
int stub_character_make_priority_target(lua_State* L);
int stub_character_is_player(lua_State* L);
int stub_vehicle_set_team(lua_State* L);
int stub_vehicle_set_ignore_rail_obstacles(lua_State* L);
int stub_set_suppress_synced_attacks_flag(lua_State* L);
int stub_reticle_override_clear(lua_State* L);
int stub_radio_global_block(lua_State* L);
int stub_player_grenades_enable(lua_State* L);
int stub_player_can_sprint(lua_State* L);
int stub_player_actions_disable(lua_State* L);
int stub_party_set_recruitable(lua_State* L);
int stub_parking_spot_disable_all(lua_State* L);
int stub_on_purchase(lua_State* L);
} // namespace stubs_b21
namespace {
using stubs_b21::stub_mission_autosave;
using stubs_b21::stub_light_group_set_intensity;
using stubs_b21::stub_inv_item_remove;
using stubs_b21::stub_hud_display_set_element;
using stubs_b21::stub_helicopter_enter_retreat;
using stubs_b21::stub_door_is_open;
using stubs_b21::stub_dlc_gis_film_grain_unload;
using stubs_b21::stub_difficulty_get_level;
using stubs_b21::stub_cash_add;
using stubs_b21::stub_boss_battle_matt_set_matt_flying;
using stubs_b21::stub_ai_do_scripted_take_cover;
using stubs_b21::stub_action_stop_synced_state;
using stubs_b21::stub_action_play_synced_is_finished;
using stubs_b21::stub_spawn_override_change_hood_alternate_spawning;
using stubs_b21::stub_shop_ignore_clerk;
using stubs_b21::stub_set_animation_set;
using stubs_b21::stub_player_release_human_shield;
using stubs_b21::stub_player_is_rc_driver;
using stubs_b21::stub_on_incapacitated;
using stubs_b21::stub_notoriety_force_no_air_spawn;
using stubs_b21::stub_helicopters_set_jitter_override;
using stubs_b21::stub_group_members_alive;
using stubs_b21::stub_flashpoints_enable;
using stubs_b21::stub_character_set_immune_to_explosion_vomit;
using stubs_b21::stub_character_set_always_combat_ready;
using stubs_b21::stub_character_make_priority_target;
using stubs_b21::stub_character_is_player;
using stubs_b21::stub_vehicle_set_team;
using stubs_b21::stub_vehicle_set_ignore_rail_obstacles;
using stubs_b21::stub_set_suppress_synced_attacks_flag;
using stubs_b21::stub_reticle_override_clear;
using stubs_b21::stub_radio_global_block;
using stubs_b21::stub_player_grenades_enable;
using stubs_b21::stub_player_can_sprint;
using stubs_b21::stub_player_actions_disable;
using stubs_b21::stub_party_set_recruitable;
using stubs_b21::stub_parking_spot_disable_all;
using stubs_b21::stub_on_purchase;

const std::vector<std::string>& namesList() {
    static const std::vector<std::string> names = {
        // Sec17 (30 names)
        "vehicle_set_script_hard_goto",
        "vehicle_set_crazy",
        "set_perfect_aim",
        "set_civilians_flee",
        "set_attack_enemies_flag",
        "player_force_vehicle_seat",
        "on_human_killed",
        "mesh_mover_destroyed",
        "character_set_persona",
        "character_set_never_fall",
        "character_set_attack_multiplier",
        "character_ragdoll",
        "character_add_prop",
        "camera_shake_stop",
        "camera_shake_play_looping",
        "ai_attack_region",
        "vehicle_prevent_transition_to_ambient",
        "tutorial_stop",
        "set_stag_active",
        "set_cant_cower_flag",
        "persona_set_global_situation_override",
        "persona_set_dlc_bundle",
        "mission_autosave",
        "light_group_set_intensity",
        "inv_item_remove",
        "hud_display_set_element",
        "helicopter_enter_retreat",
        "door_is_open",
        "dlc_gis_film_grain_unload",
        "difficulty_get_level",
        // Sec18 (30 names)
        "cash_add",
        "boss_battle_matt_set_matt_flying",
        "ai_do_scripted_take_cover",
        "action_stop_synced_state",
        "action_play_synced_is_finished",
        "spawn_override_change_hood_alternate_spawning",
        "shop_ignore_clerk",
        "set_animation_set",
        "player_release_human_shield",
        "player_is_rc_driver",
        "on_incapacitated",
        "notoriety_force_no_air_spawn",
        "helicopters_set_jitter_override",
        "group_members_alive",
        "flashpoints_enable",
        "character_set_immune_to_explosion_vomit",
        "character_set_always_combat_ready",
        "character_make_priority_target",
        "character_is_player",
        "vehicle_set_team",
        "vehicle_set_ignore_rail_obstacles",
        "set_suppress_synced_attacks_flag",
        "reticle_override_clear",
        "radio_global_block",
        "player_grenades_enable",
        "player_can_sprint",
        "player_actions_disable",
        "party_set_recruitable",
        "parking_spot_disable_all",
        "on_purchase",
    };
    return names;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB21() { return namesList(); }

void registerB21(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("vehicle_set_script_hard_goto"))
        registerOne(L, state, log, stateTag, "vehicle_set_script_hard_goto", openGuard<stub_vehicle_set_script_hard_goto>);
    if (wants("vehicle_set_crazy"))
        registerOne(L, state, log, stateTag, "vehicle_set_crazy", openGuard<stub_vehicle_set_crazy>);
    if (wants("set_perfect_aim"))
        registerOne(L, state, log, stateTag, "set_perfect_aim", openGuard<stub_set_perfect_aim>);
    if (wants("set_civilians_flee"))
        registerOne(L, state, log, stateTag, "set_civilians_flee", openGuard<stub_set_civilians_flee>);
    if (wants("set_attack_enemies_flag"))
        registerOne(L, state, log, stateTag, "set_attack_enemies_flag", openGuard<stub_set_attack_enemies_flag>);
    if (wants("player_force_vehicle_seat"))
        registerOne(L, state, log, stateTag, "player_force_vehicle_seat", openGuard<stub_player_force_vehicle_seat>);
    if (wants("on_human_killed"))
        registerOne(L, state, log, stateTag, "on_human_killed", openGuard<stub_on_human_killed>);
    if (wants("mesh_mover_destroyed"))
        registerOne(L, state, log, stateTag, "mesh_mover_destroyed", openGuard<stub_mesh_mover_destroyed>);
    if (wants("character_set_persona"))
        registerOne(L, state, log, stateTag, "character_set_persona", openGuard<stub_character_set_persona>);
    if (wants("character_set_never_fall"))
        registerOne(L, state, log, stateTag, "character_set_never_fall", openGuard<stub_character_set_never_fall>);
    if (wants("character_set_attack_multiplier"))
        registerOne(L, state, log, stateTag, "character_set_attack_multiplier",
                    openGuard<stub_character_set_attack_multiplier>);
    if (wants("character_ragdoll"))
        registerOne(L, state, log, stateTag, "character_ragdoll", openGuard<stub_character_ragdoll>);
    if (wants("character_add_prop"))
        registerOne(L, state, log, stateTag, "character_add_prop", openGuard<stub_character_add_prop>);
    if (wants("camera_shake_stop"))
        registerOne(L, state, log, stateTag, "camera_shake_stop", openGuard<stub_camera_shake_stop>);
    if (wants("camera_shake_play_looping"))
        registerOne(L, state, log, stateTag, "camera_shake_play_looping", openGuard<stub_camera_shake_play_looping>);
    if (wants("ai_attack_region"))
        registerOne(L, state, log, stateTag, "ai_attack_region", openGuard<stub_ai_attack_region>);
    if (wants("vehicle_prevent_transition_to_ambient"))
        registerOne(L, state, log, stateTag, "vehicle_prevent_transition_to_ambient",
                    openGuard<stub_vehicle_prevent_transition_to_ambient>);
    if (wants("tutorial_stop"))
        registerOne(L, state, log, stateTag, "tutorial_stop", openGuard<stub_tutorial_stop>);
    if (wants("set_stag_active"))
        registerOne(L, state, log, stateTag, "set_stag_active", openGuard<stub_set_stag_active>);
    if (wants("set_cant_cower_flag"))
        registerOne(L, state, log, stateTag, "set_cant_cower_flag", openGuard<stub_set_cant_cower_flag>);
    if (wants("persona_set_global_situation_override"))
        registerOne(L, state, log, stateTag, "persona_set_global_situation_override",
                    openGuard<stub_persona_set_global_situation_override>);
    if (wants("persona_set_dlc_bundle"))
        registerOne(L, state, log, stateTag, "persona_set_dlc_bundle", openGuard<stub_persona_set_dlc_bundle>);
    if (wants("mission_autosave"))
        registerOne(L, state, log, stateTag, "mission_autosave", openGuard<stub_mission_autosave>);
    if (wants("light_group_set_intensity"))
        registerOne(L, state, log, stateTag, "light_group_set_intensity", openGuard<stub_light_group_set_intensity>);
    if (wants("inv_item_remove"))
        registerOne(L, state, log, stateTag, "inv_item_remove", openGuard<stub_inv_item_remove>);
    if (wants("hud_display_set_element"))
        registerOne(L, state, log, stateTag, "hud_display_set_element", openGuard<stub_hud_display_set_element>);
    if (wants("helicopter_enter_retreat"))
        registerOne(L, state, log, stateTag, "helicopter_enter_retreat", openGuard<stub_helicopter_enter_retreat>);
    if (wants("door_is_open"))
        registerOne(L, state, log, stateTag, "door_is_open", openGuard<stub_door_is_open>);
    if (wants("dlc_gis_film_grain_unload"))
        registerOne(L, state, log, stateTag, "dlc_gis_film_grain_unload", openGuard<stub_dlc_gis_film_grain_unload>);
    if (wants("difficulty_get_level"))
        registerOne(L, state, log, stateTag, "difficulty_get_level", openGuard<stub_difficulty_get_level>);

    if (wants("cash_add"))
        registerOne(L, state, log, stateTag, "cash_add", openGuard<stub_cash_add>);
    if (wants("boss_battle_matt_set_matt_flying"))
        registerOne(L, state, log, stateTag, "boss_battle_matt_set_matt_flying",
                    openGuard<stub_boss_battle_matt_set_matt_flying>);
    if (wants("ai_do_scripted_take_cover"))
        registerOne(L, state, log, stateTag, "ai_do_scripted_take_cover", openGuard<stub_ai_do_scripted_take_cover>);
    if (wants("action_stop_synced_state"))
        registerOne(L, state, log, stateTag, "action_stop_synced_state", openGuard<stub_action_stop_synced_state>);
    if (wants("action_play_synced_is_finished"))
        registerOne(L, state, log, stateTag, "action_play_synced_is_finished",
                    openGuard<stub_action_play_synced_is_finished>);
    if (wants("spawn_override_change_hood_alternate_spawning"))
        registerOne(L, state, log, stateTag, "spawn_override_change_hood_alternate_spawning",
                    openGuard<stub_spawn_override_change_hood_alternate_spawning>);
    if (wants("shop_ignore_clerk"))
        registerOne(L, state, log, stateTag, "shop_ignore_clerk", openGuard<stub_shop_ignore_clerk>);
    if (wants("set_animation_set"))
        registerOne(L, state, log, stateTag, "set_animation_set", openGuard<stub_set_animation_set>);
    if (wants("player_release_human_shield"))
        registerOne(L, state, log, stateTag, "player_release_human_shield", openGuard<stub_player_release_human_shield>);
    if (wants("player_is_rc_driver"))
        registerOne(L, state, log, stateTag, "player_is_rc_driver", openGuard<stub_player_is_rc_driver>);
    if (wants("on_incapacitated"))
        registerOne(L, state, log, stateTag, "on_incapacitated", openGuard<stub_on_incapacitated>);
    if (wants("notoriety_force_no_air_spawn"))
        registerOne(L, state, log, stateTag, "notoriety_force_no_air_spawn", openGuard<stub_notoriety_force_no_air_spawn>);
    if (wants("helicopters_set_jitter_override"))
        registerOne(L, state, log, stateTag, "helicopters_set_jitter_override",
                    openGuard<stub_helicopters_set_jitter_override>);
    if (wants("group_members_alive"))
        registerOne(L, state, log, stateTag, "group_members_alive", openGuard<stub_group_members_alive>);
    if (wants("flashpoints_enable"))
        registerOne(L, state, log, stateTag, "flashpoints_enable", openGuard<stub_flashpoints_enable>);
    if (wants("character_set_immune_to_explosion_vomit"))
        registerOne(L, state, log, stateTag, "character_set_immune_to_explosion_vomit",
                    openGuard<stub_character_set_immune_to_explosion_vomit>);
    if (wants("character_set_always_combat_ready"))
        registerOne(L, state, log, stateTag, "character_set_always_combat_ready",
                    openGuard<stub_character_set_always_combat_ready>);
    if (wants("character_make_priority_target"))
        registerOne(L, state, log, stateTag, "character_make_priority_target",
                    openGuard<stub_character_make_priority_target>);
    if (wants("character_is_player"))
        registerOne(L, state, log, stateTag, "character_is_player", openGuard<stub_character_is_player>);
    if (wants("vehicle_set_team"))
        registerOne(L, state, log, stateTag, "vehicle_set_team", openGuard<stub_vehicle_set_team>);
    if (wants("vehicle_set_ignore_rail_obstacles"))
        registerOne(L, state, log, stateTag, "vehicle_set_ignore_rail_obstacles",
                    openGuard<stub_vehicle_set_ignore_rail_obstacles>);
    if (wants("set_suppress_synced_attacks_flag"))
        registerOne(L, state, log, stateTag, "set_suppress_synced_attacks_flag",
                    openGuard<stub_set_suppress_synced_attacks_flag>);
    if (wants("reticle_override_clear"))
        registerOne(L, state, log, stateTag, "reticle_override_clear", openGuard<stub_reticle_override_clear>);
    if (wants("radio_global_block"))
        registerOne(L, state, log, stateTag, "radio_global_block", openGuard<stub_radio_global_block>);
    if (wants("player_grenades_enable"))
        registerOne(L, state, log, stateTag, "player_grenades_enable", openGuard<stub_player_grenades_enable>);
    if (wants("player_can_sprint"))
        registerOne(L, state, log, stateTag, "player_can_sprint", openGuard<stub_player_can_sprint>);
    if (wants("player_actions_disable"))
        registerOne(L, state, log, stateTag, "player_actions_disable", openGuard<stub_player_actions_disable>);
    if (wants("party_set_recruitable"))
        registerOne(L, state, log, stateTag, "party_set_recruitable", openGuard<stub_party_set_recruitable>);
    if (wants("parking_spot_disable_all"))
        registerOne(L, state, log, stateTag, "parking_spot_disable_all", openGuard<stub_parking_spot_disable_all>);
    if (wants("on_purchase"))
        registerOne(L, state, log, stateTag, "on_purchase", openGuard<stub_on_purchase>);
}

} // namespace stub_sections
} // namespace sr3luahost
