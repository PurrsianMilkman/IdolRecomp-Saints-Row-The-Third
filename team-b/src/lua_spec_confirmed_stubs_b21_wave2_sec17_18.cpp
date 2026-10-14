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

// on_human_killed (Sec17.7, 0x00a56f00). 1 optional string (human "kind"
// filter), nil-gated pointer-null check. Return none. A GLOBAL (not per-
// object) hook-slot-array member, slot 0xb (11) - unconditionally cleared,
// then (re-)registered if the filter resolves. The 350-entry name-
// interning table (0x00a1fcc0) collapses to a flat resolves-or-not test,
// test-registerable (same "no fixed id table" class of gap as several
// other name-lookup natives in this project).
int stub_on_human_killed(lua_State* L) {
    logCall(L, upLog(L), "on_human_killed", upStateTag(L));
    std::string filter = optionalStringDefault(L, 1, "");
    EngineState* st = upState(L);
    st->onHumanKilledCallback().clear(); // CONFIRMED unconditional clear of slot 0xb
    st->onHumanKilledFilterName().clear();
    if (!filter.empty() && st->humanKindResolves(filter)) {
        st->onHumanKilledFilterName() = filter;
        st->onHumanKilledCallback() = filter; // (re-)registers the slot; this project tracks the filter as the live registration
    }
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

// mission_autosave (Sec17.23, 0x00a52b10, implemented from the Sec60.3
// CORRECTION, not Sec17.23 alone). No arguments. Return none. CORRECTED
// per Sec60.3: a silent-return-on-Steam-check-failure path gates the WHOLE
// function (nothing happens at all on failure - no record, no save
// attempt); the busy predicate's own polarity is inverted from Sec17.23's
// original text (the real save kickoff proceeds only when busy is FALSE);
// the native explicitly skips game_autosave's own extra mission gates (so
// it can save mid-mission - this project has no such gates to skip in the
// first place). The record-open and the no-op stub call are otherwise
// unconditional once past the Steam check.
//
// WIRED 2026-10-08 (spec-save-format.md Sec15.2): a kickoff is this
// native's own real save-entry call (`mission_autosave` 0x00a52b10 ->
// 0x00b94ff0, CONFIRMED by Sec15.2's exhaustive call-site scan) - same
// real file-write branch game_autosave's own kickoff reaches, so a
// successful kickoff now also performs a real write of the live
// EngineState::saveGame()/playerCash() state (save_game_bridge.h's
// writeAutosaveSnapshot(), never throws, failure recorded on
// EngineState::lastAutosaveError() rather than as a Lua error).
int stub_mission_autosave(lua_State* L) {
    logCall(L, upLog(L), "mission_autosave", upStateTag(L));
    EngineState* st = upState(L);
    if (!st->missionAutosaveSteamCheckOk()) return 0; // Sec60.3 correction: silent return, nothing else happens
    EngineState::replicateStateChange("mission_autosave", ""); // CONFIRMED unconditional, opcode 0x43 subtype 0x1c
    ++st->missionAutosaveStubCallCount(); // the always-1, result-unused stub 0x00d21500
    if (!st->missionAutosaveBusy()) { // Sec60.3: proceeds only when busy is FALSE
        if (st->missionAutosaveChainReady()) {
            ++st->missionAutosaveKickoffCount();
            std::string path, err;
            writeAutosaveSnapshot(*st, path, err); // Sec15.2/Sec6.3: real file write
        } else {
            st->missionAutosaveDeferredPending() = true;
        }
    }
    return 0;
}

// light_group_set_intensity (Sec17.24, 0x00a51f50). 1 mandatory string
// (light-group name) + 1 mandatory number (intensity, raw float) + 1
// optional boolean - CONFIRMED read but never used (a genuinely dead/
// vestigial optional argument). Return none. Resolves via a dedicated
// per-kind resolver, collapsed to EngineState::lightGroupResolves() (no
// fixed id table in this project). UNCONDITIONALLY sets bit 0x2 of the
// flags byte at +0x40 and writes the intensity to +0x50, then re-applies
// to every owned member light; a SEPARATE conditional record fires only
// inside a coordinated co-op session.
int stub_light_group_set_intensity(lua_State* L) {
    logCall(L, upLog(L), "light_group_set_intensity", upStateTag(L));
    std::string name = argString(L, 1);
    double intensity = lua_tonumber(L, 2); // CONFIRMED: mandatory raw float
    (void)optionalBoolDefault(L, 3, false); // CONFIRMED dead argument - read, never used
    EngineState* st = upState(L);
    if (!st->lightGroupResolve(name)) return 0; // Sec4.13.0: unresolved group is a no-op
    auto& group = st->lightGroup(name);
    group.flagBit2Set = true;
    group.intensity = intensity;
    ++group.applyCount;
    if (st->coopSession().present.get()) { // CONFIRMED false by default (single player)
        EngineState::replicateStateChange("light_group_set_intensity", name);
        ++group.replicateCount;
    }
    return 0;
}

// inv_item_remove (Sec17.25, 0x00a50b70). 1 mandatory string (item/weapon
// name) + 1 optional string, default "#PLAYER1#" (the SECOND, distinct
// player sentinel - not "#PLAYER#"). Return none. Resolves via
// objectResolves() (0x00a281a0 stand-in). Gate is single
// (forceFlagGatePasses, variant 3, single-gate-0x008ae480-only - the SAME
// shared predicate npc_leash_remove/npc_leash_to_nav already reuse,
// CONFIRMED true in single player). The item definition lookup
// (0x008dcb10/0x005feaa0) collapses to EngineState::
// inventoryItemDefinitionResolves() (no fixed id table in this project).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_remove(lua_State* L) {
    logCall(L, upLog(L), "inv_item_remove", upStateTag(L));
    std::string itemName = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    target = resolvedTarget;
    CharacterState& c = st->getOrCreateCharacter(target);
    if (!c.forceFlagGatePasses.get()) { // CONFIRMED true in single player (variant 3, Sec7's own front matter)
        EngineState::replicateStateChange("inv_item_remove", target);
        ++c.inventoryItemRemoveReplicateCount;
        return 0;
    }
    if (st->inventoryItemDefinitionResolve(itemName)) { // Sec17.25: "none" (not a row) resolves null; other unregistered names OPEN
        ++c.inventoryItemRemoveAppliedCount;
    }
    return 0;
}

// hud_display_set_element (Sec17.26, 0x00a4ed30). 3 mandatory numbers
// (slot id; sub-field index, 0xf meaning "all"; value). Return none. "No
// gate at all, always both" idiom: UNCONDITIONALLY opens a network record,
// AND UNCONDITIONALLY calls the direct-apply dispatcher - which itself
// only writes when slotId < 20 and that slot's own "active" flag is set.
// Extends HudDisplayStatePool (wave-1 Batch 5) with a SEPARATE 20-slot
// active/value table. CHOSEN (hardened divergence, Sec17.26): a negative
// slotId (never explicitly excluded by the spec's own "< 0x14" test) and a
// subIndex outside {0..11, 0xf} (never explicitly bounds-checked beyond
// the 0xf special case) are both refused rather than reproduced as a
// guessed OOB array access - each counted separately.
int stub_hud_display_set_element(lua_State* L) {
    logCall(L, upLog(L), "hud_display_set_element", upStateTag(L));
    int64_t slotIdRaw = truncateEa2596(lua_tonumber(L, 1));
    int64_t subIndexRaw = truncateEa2596(lua_tonumber(L, 2));
    double value = lua_tonumber(L, 3);
    EngineState* st = upState(L);
    auto& pool = st->hudDisplayStatePool();
    EngineState::replicateStateChange("hud_display_set_element", ""); // CONFIRMED unconditional
    ++pool.setElementCount;

    if (slotIdRaw < 0 || slotIdRaw >= HudDisplayStatePool::kElementSlotCount) {
        ++pool.setElementHardenedSlotIdCount; // CHOSEN (hardened divergence, Sec17.26)
        return 0;
    }
    int slotId = static_cast<int>(slotIdRaw);
    if (!pool.elementActive[static_cast<size_t>(slotId)]) return 0; // CONFIRMED: inactive slot, no-op

    if (subIndexRaw == HudDisplayStatePool::kElementSetAllSentinel) {
        for (int i = 0; i < HudDisplayStatePool::kElementSubFieldCount; ++i)
            pool.elementValues[static_cast<size_t>(slotId)][static_cast<size_t>(i)] = value;
        ++pool.setElementAppliedCount;
        return 0;
    }
    if (subIndexRaw < 0 || subIndexRaw >= HudDisplayStatePool::kElementSubFieldCount) {
        ++pool.setElementHardenedSubIndexCount; // CHOSEN (hardened divergence, Sec17.26)
        return 0;
    }
    pool.elementValues[static_cast<size_t>(slotId)][static_cast<size_t>(subIndexRaw)] = value;
    ++pool.setElementAppliedCount;
    return 0;
}

// helicopter_enter_retreat (Sec17.27, 0x00a4cb10). 1 mandatory string
// (vehicle name). Return 1 boolean. Resolves via objectResolves() (0x00a281e0
// stand-in). Reuses EngineState::HelicopterFlyTo::qualifies directly (the
// SAME 0x00ad31a0 "+0x2c in {3,4}" test, CONFIRMED the identical address
// Sec17.4's own loop reads) rather than re-deriving a new gate. Since this
// call site always passes the delegate a literal `true`, the "boolean OR
// fallback-field-clear" short-circuit is structurally always satisfied -
// not separately modeled (it never changes the observable branch taken).
int stub_helicopter_enter_retreat(lua_State* L) {
    logCall(L, upLog(L), "helicopter_enter_retreat", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!st->helicopterFlyTo().qualifies.get(name)) { // OPEN until set - reused directly, Sec28.2
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& veh = st->getOrCreateVehicle(name);
    auto& h = st->helicopter(name);
    if (veh.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        if (h.subObjectModeEnum.get() != 6) { // OPEN until set
            h.field158Cleared = true;
            ++h.retreatTransitionCount; // 0x00b350a0(vehicle, 5, 0)
            if (h.heightCheckTrips.get()) { // OPEN until set
                h.heightTripFlag268 = true;
                h.retreatDeadlineStamp = st->nextMonotonicStamp() + 1500;
            }
        }
    } else {
        EngineState::replicateStateChange("helicopter_enter_retreat", name);
        ++h.retreatReplicateCount;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// door_is_open (Sec17.28, 0x00a48760). 1 mandatory string. Return 1
// boolean. Resolves via objectResolves() (0x006434a0 stand-in). Reuses the
// EXISTING hasInteractionAnchor field (door +0xc8, Sec1.10) directly for
// the outer null-test; the inner state byte (+0x7a bit 0x40) is a new,
// independently OPEN fact. Null pointer -> pushes false.
int stub_door_is_open(lua_State* L) {
    logCall(L, upLog(L), "door_is_open", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    PlacedObjectState& p = st->getOrCreatePlacedObject(name);
    bool isOpen = p.hasInteractionAnchor.get() && p.doorOpenBit40At7a.get(); // OPEN until set
    lua_pushboolean(L, isOpen ? 1 : 0);
    return 1;
}

// dlc_gis_film_grain_unload (Sec17.29, 0x00a479d0). 1 optional boolean,
// default true + 1 optional number (2-bit action mask), default 3. Return
// none. Bit 0 (default set): local DLC film-grain fade toggle, gated on
// the DLC-availability sentinel (CHOSEN false - "not loaded," this project
// models no real DLC content elsewhere either). Bit 1 (default set): opens
// a network record (opcode 0x43, subtype 0x2e). With the default mask,
// BOTH actions always happen.
int stub_dlc_gis_film_grain_unload(lua_State* L) {
    logCall(L, upLog(L), "dlc_gis_film_grain_unload", upStateTag(L));
    bool v = optionalBoolDefault(L, 1, true);
    int mask = static_cast<int>(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    if (mask & 0x1) {
        if (st->dlcFilmGrainAvailable().get()) { // OPEN until set
            st->filmGrainFadeState() = v;
        } else {
            st->filmGrainFadeState() = false; // CONFIRMED: "DLC not loaded" short-circuits to a reset
        }
        ++st->filmGrainLocalApplyCount();
    }
    if (mask & 0x2) {
        EngineState::replicateStateChange("dlc_gis_film_grain_unload", "");
        ++st->filmGrainReplicateCount();
    }
    return 0;
}

// difficulty_get_level (Sec17.30, 0x00a47300). No arguments. Return 1
// number. Pure global-state query, no resolver/gate/side-effect of any
// kind. CONFIRMED (per lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a.cpp's
// own doc comment) to be the SAME real function get_current_difficulty
// (Sec41.2) under its own alias name - reuses the SAME field directly
// (file-default 1, CONFIRMED there), not a second, weaker-default copy.
int stub_difficulty_get_level(lua_State* L) {
    logCall(L, upLog(L), "difficulty_get_level", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->difficultyLevel()));
    return 1;
}

// =======================================================================
// Section 18 batch (spec-lua-api-behaviour.md Sec18, "ranks 251-350
// tranche, part C", 30 names, wave 2 batch W2-2, 2026-10-08).
// =======================================================================

// cash_add (Sec18.1, 0x00a42ef0). 1 mandatory number (cash amount,
// unconditional) + 1 optional string, default "#PLAYER1#" (the SAME second
// player sentinel inv_item_remove above uses). Return none (0 values).
// Reuses EngineState::playerCash() directly (this project's single
// economy value) - scaled x100 and saturated to +/-2,000,000,000.
int stub_cash_add(lua_State* L) {
    logCall(L, upLog(L), "cash_add", upStateTag(L));
    double amount = lua_tonumber(L, 1); // CONFIRMED: unconditional read
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    if (!st->objectResolves().get(target)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(target);
    double scaled = amount * 100.0;
    scaled = std::max<double>(-2000000000.0, std::min<double>(2000000000.0, scaled)); // CONFIRMED saturating clamp
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        st->playerCash() += scaled;
    } else {
        EngineState::replicateStateChange("cash_add", target);
    }
    return 0;
}

// boss_battle_matt_set_matt_flying (Sec18.2, 0x00a408c0). 1 optional
// boolean, default true. Return none. Unconditionally writes to a single
// global - no resolver, no gate, no record.
int stub_boss_battle_matt_set_matt_flying(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_set_matt_flying", upStateTag(L));
    bool v = optionalBoolDefault(L, 1, true);
    upState(L)->bossBattleMattFlying() = v;
    return 0;
}

// ai_do_scripted_take_cover (Sec18.3, 0x00a3e9d0). 1 mandatory string
// (character) + 1 optional string, default absent (anchor). Return 1
// boolean. Resolves arg 1 via the #FOLLOWER#-then-character chain
// 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08 (follower-
// sentinel-resolver pass). If arg 2 absent: checks the "already covering"
// state test (alreadyCoveringTowardAnchor, OPEN); if true, dispatches the
// SAME scripted-action-set call and returns that boolean. If arg 2 given:
// resolves it, and on success writes the resolved handle into the SAME
// pendingScriptedMoveDestination field ai_do_scripted_move (Sec3.11)
// already models (CONFIRMED the identical physical +0x4b0/+0x4b4 field),
// dispatches, returns true; otherwise false.
int stub_ai_do_scripted_take_cover(lua_State* L) {
    logCall(L, upLog(L), "ai_do_scripted_take_cover", upStateTag(L));
    std::string name = argString(L, 1);
    std::string anchor = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (anchor.empty()) {
        bool already = c.alreadyCoveringTowardAnchor.get(); // OPEN until set
        if (already) {
            ++c.scriptedTakeCoverDispatchCount; // 0x004f4d00(char, 8, 0)
        }
        lua_pushboolean(L, already ? 1 : 0);
        return 1;
    }
    if (st->objectResolves().get(anchor)) { // OPEN until set
        c.pendingScriptedMoveDestination = anchor; // CONFIRMED the SAME +0x4b0/+0x4b4 field, Sec3.11
        ++c.scriptedTakeCoverDispatchCount; // 0x004f4d00(char, 8, 0)
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// action_stop_synced_state (Sec18.4, 0x00a3d5a0). 1 mandatory string
// (character) + 1 optional boolean, default false. Return none. Resolves
// via objectResolves() (0x00a281a0 stand-in). The global replication-mode
// flag (0x024d4461) is CHOSEN false by default (single-player-only project
// stance), so this always takes the documented "normal local path" branch,
// clearing the SAME pairing action_play_synced_state (Sec14.1) writes.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_action_stop_synced_state(lua_State* L) {
    logCall(L, upLog(L), "action_stop_synced_state", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    name = resolvedName;
    CharacterState& c = st->getOrCreateCharacter(name);
    c.lastSyncedStopArg = arg2;
    if (st->globalReplicationModeFlag() && !c.forceFlagGatePasses.get()) { // CHOSEN false by default
        EngineState::replicateStateChange("action_stop_synced_state", name);
        return 0;
    }
    // CONFIRMED normal local path: clears the synced pairing this native shares with action_play_synced_state.
    c.syncedActionPartnerName.clear();
    c.syncedActionPaired = false;
    ++c.syncedStopLocalApplyCount;
    return 0;
}

// action_play_synced_is_finished (Sec18.5, 0x00a3c0c0). 2 mandatory
// strings (a synced-action pair) + 1 optional number, default a fixed
// constant. Return 1 boolean. Resolves both via objectResolves() (0x00a281a0
// stand-in); either unresolved -> false immediately. For each resolved
// character independently: no active controller state (syncedAnimControllerActive
// false) -> "finished"; else compares elapsed time against the threshold.
// Overall result is the logical AND of both.
// Resolver 0x00a281a0 (both actors) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_action_play_synced_is_finished(lua_State* L) {
    logCall(L, upLog(L), "action_play_synced_is_finished", upStateTag(L));
    std::string actor1 = argString(L, 1);
    std::string actor2 = argString(L, 2);
    double threshold = optionalNumberDefault(L, 3, 1.0); // CHOSEN placeholder - real constant (0x01117a4c) value unknown here
    EngineState* st = upState(L);
    std::string resolvedActor1, resolvedActor2;
    if (!resolveFollowerAwareObject(st, actor1, resolvedActor1) ||
        !resolveFollowerAwareObject(st, actor2, resolvedActor2)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    actor1 = resolvedActor1;
    actor2 = resolvedActor2;
    auto finished = [&](const std::string& actor) {
        CharacterState& c = st->getOrCreateCharacter(actor);
        if (!c.syncedAnimControllerActive.get()) return true; // OPEN until set; -1 "no active state" => finished
        return c.syncedAnimElapsedTime.get() >= threshold; // OPEN until set
    };
    bool result = finished(actor1) && finished(actor2);
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// spawn_override_change_hood_alternate_spawning (Sec18.6, 0x00a5fab0). 1
// mandatory string (hood/zone name) + 1 optional boolean, default true.
// Return none. Resolves via the shared objectResolves() map directly (no
// separate predicate needed - Sec29's standing convention). UNCONDITIONALLY
// toggles the per-hood bit; a SEPARATE host-only record fires via the
// EXISTING coopLocalIsHost() singleton.
int stub_spawn_override_change_hood_alternate_spawning(lua_State* L) {
    logCall(L, upLog(L), "spawn_override_change_hood_alternate_spawning", upStateTag(L));
    std::string hood = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(hood)) return 0; // OPEN until set
    st->hoodAlternateSpawning()[hood] = v; // CONFIRMED unconditional local toggle
    if (st->coopLocalIsHost()) { // reuses the EXISTING 0x0087ba20 host-check singleton, Sec8.27/Sec6.22/Sec10.9
        EngineState::replicateStateChange("spawn_override_change_hood_alternate_spawning", hood);
    }
    return 0;
}

// shop_ignore_clerk (Sec18.7, 0x00a5dab0). 1 mandatory boolean, no nil-
// gate. Return none. CONFIRMED a dead no-op stub in the shipped binary
// (0x00754410, already documented elsewhere in this project as a stripped
// debug/profiling stub) - never touches any real state.
int stub_shop_ignore_clerk(lua_State* L) {
    logCall(L, upLog(L), "shop_ignore_clerk", upStateTag(L));
    (void)lua_toboolean(L, 1);
    return 0;
}

// set_animation_set (Sec18.8, 0x00a5e150). 2 mandatory strings (character;
// animation-set name). Return none. Resolves via objectResolves() (0x00a281a0
// stand-in). The set-name->id lookup (0x004b16c0) collapses to tracking the
// name directly (no fixed id table). If the controller already has an
// active instance (CHOSEN false default - no stated construction value),
// the in-progress blend is immediately re-applied.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_animation_set(lua_State* L) {
    logCall(L, upLog(L), "set_animation_set", upStateTag(L));
    std::string name = argString(L, 1);
    std::string setName = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    c.animationSetName = setName;
    if (c.hasActiveAnimationInstance) {
        ++c.animationSetReapplyCount;
    }
    return 0;
}

// player_release_human_shield (Sec18.9, 0x00a598d0). 1 mandatory string
// (character) + 2 optional booleans, default false/false. Return 1
// boolean (the actual release-attempt result). Resolves via objectResolves()
// (0x00a280c0 stand-in); unresolved -> pushes false. A genuinely rich state
// machine, modeled at the observable-branch level only (no real position/
// velocity/timer data in this project). isLocalAuthority CHOSEN true
// (single-player-only project stance).
int stub_player_release_human_shield(lua_State* L) {
    logCall(L, upLog(L), "player_release_human_shield", upStateTag(L));
    std::string name = argString(L, 1);
    bool boolA = optionalBoolDefault(L, 2, false);
    bool boolB = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(name);
    if (!c.humanShieldReleaseIsLocalAuthority) {
        EngineState::replicateStateChange("player_release_human_shield", name);
        ++c.humanShieldReplicateCount;
        lua_pushboolean(L, 0);
        return 1;
    }
    bool released = false;
    if (boolA) {
        ++c.humanShieldForcibleEjectCount;
        released = true;
    } else if (boolB) {
        ++c.humanShieldDelayedReleaseCount; // 0x009ad200, own body OPEN (resolved elsewhere, Sec22.26)
        released = true;
    } else if (c.humanShieldNormalReleaseGatesPass.get() && // OPEN until set
               !c.humanShieldHostageName.empty() && c.humanShieldStateByte18d4 == 1) {
        c.humanShieldStateByte18d4 = 4; // CONFIRMED: both parties' state byte -> 4 ("cooldown/just-released")
        ++c.humanShieldNormalReleaseCount;
        released = true;
    }
    lua_pushboolean(L, released ? 1 : 0);
    return 1;
}

// player_is_rc_driver (Sec18.10, 0x00a5a9e0). 1 mandatory string
// (character) + 1 optional string, default absent (vehicle). Return 1
// boolean. Resolves arg 1 via objectResolves() (0x00a280c0 stand-in);
// unresolved -> false. Collapses the full resolve/driver-seat-position-
// compare chain into one OPEN predicate per character name (a pure query).
int stub_player_is_rc_driver(lua_State* L) {
    logCall(L, upLog(L), "player_is_rc_driver", upStateTag(L));
    std::string name = argString(L, 1);
    (void)optionalStringDefault(L, 2, ""); // arg 2 selects which of two resolve paths feeds the SAME collapsed predicate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, st->isRcDriverPredicate().get(name) ? 1 : 0); // OPEN until set
    return 1;
}

// on_incapacitated (Sec18.11, 0x00a58070). 1 mandatory string (callback
// name, opaque) + 1 mandatory string (character name). Return none.
// Resolves arg 2 via objectResolves() (0x005e4dd0 stand-in). Registers into
// the character-kind hook base +0x68/+0x6c, slot 1 - same single-
// dedicated-field-per-hook-kind convention as onDeathCallback above.
int stub_on_incapacitated(lua_State* L) {
    logCall(L, upLog(L), "on_incapacitated", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string name = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateCharacter(name).onIncapacitatedCallback = callbackName;
    return 0;
}

// notoriety_force_no_air_spawn (Sec18.12, 0x00a54d40). 1 mandatory
// boolean, no nil-gate. Return none. Writes unconditionally into SIX
// separate global objects' own +0x455 byte via the SAME one-line setter -
// collapsed to one flag since nothing distinguishes the six from each
// other.
int stub_notoriety_force_no_air_spawn(lua_State* L) {
    logCall(L, upLog(L), "notoriety_force_no_air_spawn", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->notorietyForceNoAirSpawn() = v;
    return 0;
}

// helicopters_set_jitter_override (Sec18.13, 0x00a4ce90). 2 optional
// numbers, both defaulting to the SAME fixed float constant. Return none.
// Both values written directly to two adjacent globals via one-line
// setters - no resolver, no gate, no record.
int stub_helicopters_set_jitter_override(lua_State* L) {
    logCall(L, upLog(L), "helicopters_set_jitter_override", upStateTag(L));
    EngineState* st = upState(L);
    st->helicopterJitterMin() = optionalNumberDefault(L, 1, 0.0); // CHOSEN placeholder - real constant (0x01117a4c) value unknown here
    st->helicopterJitterMax() = optionalNumberDefault(L, 2, 0.0);
    return 0;
}

// group_members_alive (Sec18.14, 0x00a4c6d0). 1 mandatory string (group
// name). Return 1 number (live-member count). Resolves via a dedicated
// group-name resolver, collapsed to EngineState::groupResolves() (no fixed
// id table in this project). Walks a test-registerable member list,
// counting non-dead members via the SAME already-established
// CharacterState::isDeadHighConfidence predicate, reused directly.
int stub_group_members_alive(lua_State* L) {
    logCall(L, upLog(L), "group_members_alive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupResolves().get(name)) { // OPEN until set
        lua_pushnumber(L, 0.0);
        return 1;
    }
    int32_t count = 0;
    if (st->groupHasMembersBit().get(name)) { // OPEN until set
        count = st->groupMembersAliveCount(name);
    }
    lua_pushnumber(L, static_cast<lua_Number>(count));
    return 1;
}

// flashpoints_enable (Sec18.15, 0x00a49800). 1 optional boolean, default
// true. Return none. Writes directly to a single global - no resolver, no
// gate, no record.
int stub_flashpoints_enable(lua_State* L) {
    logCall(L, upLog(L), "flashpoints_enable", upStateTag(L));
    bool v = optionalBoolDefault(L, 1, true);
    upState(L)->flashpointsEnabled() = v;
    return 0;
}

// character_set_immune_to_explosion_vomit (Sec18.16, 0x00a42370). 1
// mandatory string + 1 mandatory boolean, no nil-gate. Return none.
// Double-gate (variant 1), identity +8/+0xc: a further bit (0x1000) on the
// EXISTING forceFlags1c9c dword.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_immune_to_explosion_vomit(lua_State* L) {
    logCall(L, upLog(L), "character_set_immune_to_explosion_vomit", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kImmuneToExplosionVomitBit1c9c,
                                  v ? CharacterState::kImmuneToExplosionVomitBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_set_immune_to_explosion_vomit", resolvedName);
    }
    return 0;
}

// character_set_always_combat_ready (Sec18.17, 0x00a420e0). Same shape as
// Sec18.16. Double-gate (variant 1), identity +8/+0xc: bit 0x1 of the
// +0x1c98 flags field.
int stub_character_set_always_combat_ready(lua_State* L) {
    logCall(L, upLog(L), "character_set_always_combat_ready", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kAlwaysCombatReadyBit1c98,
                                  v ? CharacterState::kAlwaysCombatReadyBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_always_combat_ready", name);
    }
    return 0;
}

// character_make_priority_target (Sec18.18, 0x00a41bd0). 1 mandatory
// string + 1 mandatory boolean, no nil-gate + 1 optional number, default
// 0.0 (radius). Return none. Local-authority branch: sets bit 0x400 of
// +0x1c98 to the boolean AND unconditionally overwrites +0x1ca4 with the
// radius SQUARED (CONFIRMED, regardless of the boolean's value) - a genuine
// asymmetry against the network branch, which carries the radius
// un-squared (Sec18.18's own flagged, unresolved receiving-side question,
// not modeled further).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_make_priority_target(lua_State* L) {
    logCall(L, upLog(L), "character_make_priority_target", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    double radius = optionalNumberDefault(L, 3, 0.0);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kPriorityTargetBit1c98,
                                  v ? CharacterState::kPriorityTargetBit1c98 : 0u);
        c.priorityTargetRadiusSquared = radius * radius; // CONFIRMED: always overwritten, squared
    } else {
        EngineState::replicateStateChange("character_make_priority_target", resolvedName);
    }
    return 0;
}

// character_is_player (Sec18.19, 0x00a41880). 1 mandatory string. Return 1
// boolean. Resolved through the GENERIC resolver only - this project's flat
// objectResolves() already collapses every resolver variant into one
// shared map (Sec29), so no new state is needed; "does this name resolve
// at all" IS the real test.
int stub_character_is_player(lua_State* L) {
    logCall(L, upLog(L), "character_is_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool resolves = upState(L)->objectResolves().get(name); // OPEN until set
    lua_pushboolean(L, resolves ? 1 : 0);
    return 1;
}

// vehicle_set_team (Sec18.20, 0x00a632b0). 2 mandatory strings (vehicle;
// team name). Return none. Resolves via objectResolves() (0x00a281e0
// stand-in). The team-name hash/lookup (0x0094cc60 -> 0x005961a0)
// collapses to EngineState::lookupVehicleTeamId() - a test-registerable
// map with the one CONFIRMED default fallback (9, "no match"), not an
// invented real-world name mapping. A bare, unconditional local store - no
// gate, no record at all (a real asymmetry against most of this batch's
// setters).
int stub_vehicle_set_team(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_team", upStateTag(L));
    std::string name = argString(L, 1);
    std::string teamName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).teamId = st->lookupVehicleTeamId(teamName);
    return 0;
}

// vehicle_set_ignore_rail_obstacles (Sec18.21, 0x00a661f0). 1 mandatory
// string + 1 mandatory boolean, no nil-gate. Return none. Resolves via
// objectResolves() (0x0062a190 stand-in). Raw-disassembly-confirmed
// computed-sub-record hidden-`this` (the SAME +0xc68 pattern
// vehicle_set_script_hard_goto already confirms) - a further bit (0x4) on
// the EXISTING vehicleAiForceFlagsByte4 byte.
int stub_vehicle_set_ignore_rail_obstacles(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_ignore_rail_obstacles", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& veh = st->getOrCreateVehicle(name);
    if (veh.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        veh.vehicleAiForceFlagsByte4.setBits(VehicleState::kIgnoreRailObstaclesBit,
                                              v ? VehicleState::kIgnoreRailObstaclesBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_set_ignore_rail_obstacles", name);
    }
    return 0;
}

// set_suppress_synced_attacks_flag (Sec18.22, 0x00a5d710). 1 mandatory
// string + 1 optional boolean, default true. Return none. Double-gate
// (variant 1), identity +8/+0xc: a further bit (0x20000000) on the
// EXISTING +0x1c98 flags field.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_suppress_synced_attacks_flag(lua_State* L) {
    logCall(L, upLog(L), "set_suppress_synced_attacks_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kSuppressSyncedAttacksBit1c98,
                                  v ? CharacterState::kSuppressSyncedAttacksBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("set_suppress_synced_attacks_flag", resolvedName);
    }
    return 0;
}

// reticle_override_clear (Sec18.23, 0x00a5b880). 1 mandatory number
// (flags byte, no nil-gate - not optional). Return none. Bit 0x1 clears
// two reticle-override globals locally; bit 0x2 opens a local-only network
// record.
int stub_reticle_override_clear(lua_State* L) {
    logCall(L, upLog(L), "reticle_override_clear", upStateTag(L));
    int64_t flags = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    if (flags & 0x1) {
        st->reticleOverrideGlobalsCleared() = true;
        ++st->reticleOverrideClearLocalCount();
    }
    if (flags & 0x2) {
        EngineState::replicateStateChange("reticle_override_clear", "");
        ++st->reticleOverrideClearReplicateCount();
    }
    return 0;
}

// radio_global_block (Sec18.24, 0x00a5b540). 1 mandatory boolean, no nil-
// gate. Return none. Writes directly to a single global - distinct from
// radio_block's own per-vehicle scoping (Sec12.20).
int stub_radio_global_block(lua_State* L) {
    logCall(L, upLog(L), "radio_global_block", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->radioGlobalBlocked() = v;
    return 0;
}

// player_grenades_enable (Sec18.25, 0x00a59700). No arguments consumed
// (lua_gettop called but unused - the same "inert argument" shape several
// other natives in this project already document). Return none.
// Unconditionally clears two globals to 0 every call, regardless of any
// script-side argument - genuinely zero-argument-sensitive.
int stub_player_grenades_enable(lua_State* L) {
    logCall(L, upLog(L), "player_grenades_enable", upStateTag(L));
    (void)lua_gettop(L);
    EngineState* st = upState(L);
    st->playerGrenadeGlobalA() = 0.0;
    st->playerGrenadeGlobalB() = 0.0;
    ++st->playerGrenadesEnableCallCount();
    return 0;
}

// player_can_sprint (Sec18.26, 0x00a59600). 1 mandatory boolean, no nil-
// gate. Return none. Always writes the global sprint-capability byte; if
// disabling, additionally halts any currently-active sprint state. No per-
// character "current player" handle in this project (same project-wide
// "singular local player" simplification several other natives apply).
int stub_player_can_sprint(lua_State* L) {
    logCall(L, upLog(L), "player_can_sprint", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    st->sprintEnabled() = v;
    if (!v) {
        st->sprintBit2Cleared() = true;
        if (st->sprintModeIsFive()) {
            ++st->sprintDisableCount(); // the extra call under mode/difficulty code 5 (HYPOTHESIS meaning)
        } else {
            ++st->sprintDisableCount();
        }
    }
    return 0;
}

// player_actions_disable (Sec18.27, 0x00a595a0). 1 mandatory string
// (character). Return none. Resolves via objectResolves() (0x00a280c0
// stand-in). Single gate (controlLockGatePasses, the SAME 0x008addb0
// "variant 2" family, CONFIRMED true in single player) - reuses the
// EXISTING, previously write-only actionsEnableBit20_1d99 field (Sec15.21/
// Sec27.x) directly for the local-apply branch.
//
// BIT-SENSE POLARITY (resolved 2026-10-08, prior review flag closed):
// despite the field's own "Enable" name, bit 0x20 at +0x1d99 is a
// *disabled*-sense bit, the SAME family as the sibling bits at this byte
// (CharacterState::controlsDisabled/movementDisabled, 0x08/0x10).
// spec-player-runtime.md Sec2.10 "Question B" names all three bits
// explicitly: "the player's `+0x1d99` bits (`0x08`/`0x10`/`0x20` -
// controls/movement/actions disabled) ... HIGH CONFIDENCE ... all clear
// (0) - enabled" and "treat all three bits ... as clear at spawn -
// controls, movement and actions all enabled" - i.e. clear(0)=enabled,
// set(1)=disabled for bit 0x20 too, exactly like 0x08/0x10. Sec18.27's own
// body independently matches this: the hard-coded literal `1` passed to
// `0x009e30d0(handle, 1)` (never script-controlled - this native has no
// boolean argument, unconditionally "disable") mirrors the sibling direct-
// write setters 0x009e2dd0/0x009e2fa0 (Sec7.22/Sec7.25: "writes the bit
// directly" from their own disable-boolean argument, where a `true`
// "disable" argument becomes bit=1). So "disable" must SET the bit, not
// clear it - the previous `.set(false)` had the polarity backwards.
int stub_player_actions_disable(lua_State* L) {
    logCall(L, upLog(L), "player_actions_disable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) { // CONFIRMED true in single player, Sec7.7/Sec6.22
        c.actionsEnableBit20_1d99.set(true); // "disable" SETS the disabled-sense bit (spec-player-runtime.md Sec2.10)
    } else {
        EngineState::replicateStateChange("player_actions_disable", name);
        ++c.playerActionsDisableReplicateCount;
    }
    return 0;
}

// party_set_recruitable (Sec18.28, 0x00a59460). 1 mandatory boolean, no
// nil-gate. Return none. Writes directly to a single global - no resolver,
// no gate, no record.
int stub_party_set_recruitable(lua_State* L) {
    logCall(L, upLog(L), "party_set_recruitable", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->partyRecruitable() = v;
    return 0;
}

// parking_spot_disable_all (Sec18.29, 0x00a59320). 1 mandatory boolean, no
// nil-gate. Return none. CONFIRMED (raw-disassembly-verified hidden-
// argument finding): despite its own name, only ONE fixed slot (index 1)
// is ever touched, never "all" - the Lua-facing sense is "enable" (true ->
// clear/enable slot 1) vs. "disable" (false -> set/disable slot 1),
// INVERTED from what the name suggests as a command.
int stub_parking_spot_disable_all(lua_State* L) {
    logCall(L, upLog(L), "parking_spot_disable_all", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->parkingSpotSlot1Enabled() = v; // CONFIRMED: true -> enable (clear), false -> disable (set)
    return 0;
}

// on_purchase (Sec18.30, 0x00a57260). 1 mandatory string (callback name,
// opaque). Return none. Registers into the named-hook-slot-array family's
// first confirmed GLOBAL-anchored instance (base 0x026e9528, slot 4) -
// same empty-string-clears-slot convention as every other hook field in
// this header.
int stub_on_purchase(lua_State* L) {
    logCall(L, upLog(L), "on_purchase", upStateTag(L));
    std::string callbackName = argString(L, 1);
    upState(L)->onPurchaseCallback() = callbackName;
    return 0;
}

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
