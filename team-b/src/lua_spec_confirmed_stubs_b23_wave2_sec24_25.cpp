// lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp
//
// Wave 2, Batch W2-4 - "Sec22+Sec24+Sec25 gameplay residue" (78 names total,
// split into TWO files per the orchestrator's own dispatch: the Sec22 half
// lives in lua_spec_confirmed_stubs_b22_wave2_sec22.cpp; this file holds
// spec-lua-api-behaviour.md Sec24 ("ranks 451-550 tranche, part A", 27 of
// its 28 names - the 28th, vehicle_exit_check_done/Sec24.9, belongs to a
// sibling wave-2 batch) + Sec25 ("ranks 451-550 tranche, part B", 27 of its
// 28 names - the 28th, character_playing_combat_move/Sec25.27, belongs to a
// sibling wave-2 batch). Own stub_* functions (anonymous namespace), own
// namesB23()/registerB23() pair declared in lua_spec_confirmed_stubs_
// sections.h, called from lua_spec_confirmed_stubs_registry.cpp - same
// pattern every b0N/b1N/b22 file already uses.
//
// CLEAN-ROOM: implemented ONLY from spec-lua-api-behaviour.md Sec24/Sec25
// (plus the cross-referenced sections they cite, and the Sec57/Sec60
// corrections the task brief calls out by name) plus real shipped game
// data; never the executable or any disassembly. See each stub's own doc
// comment for its exact citation and every CHOSEN/HIGH-CONFIDENCE/OPEN
// judgment call. See engine_state.h's own "Wave 2, Batch W2-4" doc comments
// for the full per-field reasoning the new state below rests on.
//
// TWO MUST-READ CORRECTIONS applied here, per the task brief:
//   - mission_set_next_mission (Sec25.4) is implemented from the Sec60.3
//     correction, NOT Sec25.4 alone: in single player this native takes its
//     own no-session path and changes nothing at all - the previously-cited
//     gate constant (0x011239d0) is a true compile-time constant, not a
//     live flag. Implemented as a genuine, confirmed no-op (call count only,
//     same convention as bink_play).
//   - continuous_explosion_follow_target_add (Sec25.23) is implemented from
//     the Sec57 tranche-23 correction in place at Sec25.23 itself (not the
//     original, now-struck text): the liveness check IS made, inside the
//     resolver 0x00a45800 itself, and +8/+0xc is a handle pair, not a
//     position. Its sibling continuous_explosion_follow_target_remove
//     belongs to a DIFFERENT wave-2 batch - see EngineState::
//     ContinuousExplosion::followTargets's own doc comment for the shared
//     record design this batch deliberately keeps simple/extensible for it.
//
// HARDENING POLICY findings in this file: debris_flow_create (Sec25.21) -
// the real allocator's own internal algorithm is OPEN (Sec25.21's own
// text); when all 3 fixed slots are already live, this host recycles slot
// 0 rather than inventing a 4th slot or refusing outright (CHOSEN, hardened
// divergence, counted via DebrisFlowState::createRecycledCount).
// debris_flow_add_avoid_object (Sec25.22) and debris_flow_destroy
// (Sec25.20) both bound their own flow-id argument against the fixed
// 3-slot array before indexing it (this project always bounds its own
// array access, regardless of whether the real engine's own bound is
// independently confirmed) - counted via DebrisFlowState::
// destroySkippedCount/addAvoidObjectSkippedCount.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// =====================================================================
// Section 24.
// =====================================================================

// boss_battle_matt_cheats_stop (Sec24.1, 0x00a40830). No arguments read
// beyond the universal lua_gettop boilerplate. Return none. CONFIRMED:
// unconditionally delegates to a teardown that resets the entire "boss
// battle vs. Matt" cluster to idle IF the sentinel wasn't already idle -
// this project tracks the top-level "active" fact only (the real multi-
// global cluster is OPEN/not independently catalogued further).
int stub_boss_battle_matt_cheats_stop(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_cheats_stop", upStateTag(L));
    EngineState* st = upState(L);
    if (st->bossBattleMattCheatsActive()) {
        st->bossBattleMattCheatsActive() = false; // CONFIRMED teardown to idle (0x005e5bb0/0x007f1e30 internals OPEN)
    }
    ++st->bossBattleMattCheatsStopCount();
    return 0;
}

// bink_play (Sec24.2, 0x007c9f50). CONFIRMED: the ENTIRE real function body
// is exactly "read lua_gettop, return 0" - a genuine, confirmed no-op stub
// in the shipped binary, reproduced exactly (no state of any kind).
int stub_bink_play(lua_State* L) {
    logCall(L, upLog(L), "bink_play", upStateTag(L));
    (void)lua_gettop(L);
    return 0;
}

// wind_override_clear (Sec24.3, 0x00a688f0). No arguments. Return none.
// CONFIRMED: unconditionally calls the zero-argument local-apply "clear"
// helper (reverting to the degenerate-input fallback direction/zero
// magnitude, same convention stub_wind_override_set's own normalize default
// uses); if the SAME replication-mode flag wind_override_set uses is set,
// fires a local-only notification record with the IDENTICAL opcode+sub-tag
// as the `_set` direction (direction carried only by which local-apply ran,
// never by the record payload itself - Sec24.3's own confirmed finding).
int stub_wind_override_clear(lua_State* L) {
    logCall(L, upLog(L), "wind_override_clear", upStateTag(L));
    EngineState* st = upState(L);
    auto& wind = st->windOverride();
    wind.x = 1.0;
    wind.y = 0.0;
    wind.z = 0.0;
    wind.magnitude = 0.0;
    ++wind.clearCount;
    if (st->actionStopSyncedStateFlag()) {
        EngineState::replicateStateChange("wind_override_clear", ""); // SAME opcode 0x43 sub-tag 0x2d as _set
        ++wind.clearNotifyCount;
    }
    return 0;
}

// wander_start (Sec24.4, 0x00a68b50). 1 mandatory string (character), 1
// optional string (position-source object, nil-gated, default absent), 1
// optional number (height/Z-offset, nil-gated, default -1.0, byte-confirmed
// 0x012a2d54). Return none. CONFIRMED: resolves the character via the
// shared flat objectResolves() stand-in; proceeds only if the height value
// is non-negative (this project ignores the character's own +0x2f0
// fallback field - OPEN, not modelled, no in-scope setter populates it).
// If arg2 was given and resolves, it becomes the wander target; otherwise
// the target stays "" (sourced from the character's own default, +0x2b0,
// not modelled - no coordinate system).
int stub_wander_start(lua_State* L) {
    logCall(L, upLog(L), "wander_start", upStateTag(L));
    std::string name = argString(L, 1);
    std::string secondObject = optionalStringDefault(L, 2, "");
    double height = optionalNumberDefault(L, 3, -1.0);
    EngineState* st = upState(L);
    // arg1 resolved via the "#FOLLOWER#"-then-character chain 0x00a28150
    // (Sec24.4's own text) - wired 2026-10-08 (follower-sentinel-resolver
    // pass). arg2 resolves via the DIFFERENT 0x005982e0 (no #FOLLOWER#
    // acceptance) - unchanged.
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (height < 0.0) return 0; // CONFIRMED gate (ignoring the +0x2f0 fallback, OPEN/not modelled)
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    std::string target;
    if (!secondObject.empty() && st->objectResolves().known(secondObject) && st->objectResolves().get(secondObject)) {
        target = secondObject;
    }
    c.wanderTargetName = target;
    c.wanderHeight = height;
    ++c.wanderStartCount;
    return 0;
}

// vehicle_spotlight_cancel (Sec24.5, 0x00a65d70). 1 mandatory string
// (character OR vehicle name) + 1 optional boolean - CONFIRMED a genuine
// dead read, accepted and ignored entirely. Return none. This project does
// not model the real dual-singleton/id-pair resolution chain (0x024433a8
// vs. the generic resolver, Sec24.5's own OPEN complexity) - every resolved
// name is treated as a vehicle spotlight target directly, a stated
// simplification.
//
// The character-first stage of that chain is 0x00a281a0 - RE-CORRECTED
// (2026-10-08, direct disassembly of its own body): a two-stage
// #PLAYER#-then-#FOLLOWER# wrapper, not PLAYER-only as an earlier reading
// had it - wired here via resolveFollowerAwareObject() so a #FOLLOWER...#
// literal resolves to the real character name first, same as every other
// name this (already-flattened) simplification then treats as "a vehicle
// spotlight target directly."
int stub_vehicle_spotlight_cancel(lua_State* L) {
    logCall(L, upLog(L), "vehicle_spotlight_cancel", upStateTag(L));
    std::string name = argString(L, 1);
    (void)optionalBoolDefault(L, 2, false); // CONFIRMED dead read - accepted, never used
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    name = resolvedName;
    VehicleState& v = st->getOrCreateVehicle(name);
    v.spotlightActive = false;
    ++v.spotlightCancelCount;
    return 0;
}

// vehicle_set_explosion_damage_multiplier (Sec24.6, 0x00a62f20). 1
// mandatory string (vehicle), 1 mandatory number, no nil-gate on either.
// Return none. CONFIRMED double-gate (variant 1, reusing VehicleState::
// forceFlagGatePasses) - tag pair "vehicle"+"explosion_multiplier".
int stub_vehicle_set_explosion_damage_multiplier(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_explosion_damage_multiplier", upStateTag(L));
    std::string name = argString(L, 1);
    double multiplier = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        v.explosionDamageMultiplier = multiplier;
    } else {
        EngineState::replicateStateChange("vehicle_set_explosion_damage_multiplier", name);
    }
    return 0;
}

// vehicle_in_water (Sec24.7, 0x00a62710). 1 mandatory string (vehicle).
// Return 1 boolean (always exactly 1 value, both branches push). CONFIRMED
// pure query of a NEW field, +0xc8.
int stub_vehicle_in_water(lua_State* L) {
    logCall(L, upLog(L), "vehicle_in_water", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool inWater = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        inWater = st->getOrCreateVehicle(name).inWater;
    }
    lua_pushboolean(L, inWater ? 1 : 0);
    return 1;
}

// vehicle_get_smoke_and_fire_state (Sec24.8, 0x00a625a0). 1 mandatory
// string (vehicle). Return 2 booleans, always exactly 2 values. CONFIRMED
// pure query of +0x16c0 bits 1/7 (a DIFFERENT field from CharacterState's
// own unrelated +0x16c0, Sec24.8's own "coincidental offset" caution).
int stub_vehicle_get_smoke_and_fire_state(lua_State* L) {
    logCall(L, upLog(L), "vehicle_get_smoke_and_fire_state", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool smoke = false, fire = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        smoke = v.smokeActive;
        fire = v.fireActive;
    }
    lua_pushboolean(L, smoke ? 1 : 0);
    lua_pushboolean(L, fire ? 1 : 0);
    return 2;
}

// vehicle_enable_physics (Sec24.10, 0x00a65140). 1 mandatory string
// (vehicle), 1 mandatory boolean (no nil-gate), 1 optional boolean
// (standard nil-gated idiom, default true). Return 1 boolean. CONFIRMED:
// gated on bit 0x8 of capability byte +0x33 - the EXACT SAME bit
// VehicleState::fullySetUp above already models (Sec33.1's own vehicle_is_
// ready) - reused directly. CONFIRMED "absent (or resolve fails) -> false",
// NOT an OPEN refusal, so `known()`/`get()` is used rather than a bare
// `.get()`. On success: unconditionally opens a LOCAL-ONLY dispatch record
// (not host-gated).
int stub_vehicle_enable_physics(lua_State* L) {
    logCall(L, upLog(L), "vehicle_enable_physics", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0;
    bool secondary = optionalBoolDefault(L, 3, true);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name); // OPEN until set
    bool capable = false;
    if (resolved) {
        VehicleState& v = st->getOrCreateVehicle(name);
        capable = v.fullySetUp.known() && v.fullySetUp.get(); // CONFIRMED: unknown/absent -> false, not a refusal
        if (capable) {
            v.physicsEnabled = enable;
            v.physicsEnabledSecondaryArg = secondary;
            ++v.physicsEnableDispatchCount;
            EngineState::replicateStateChange("vehicle_enable_physics", name); // unconditional, local-only
        }
    }
    lua_pushboolean(L, capable ? 1 : 0);
    return 1;
}

// tutorial_unlock (Sec24.11, 0x00a60520). 1 mandatory string (tutorial
// name), 1 optional boolean (standard nil-gated idiom, default false).
// Return none. CONFIRMED: host-gated (coopLocalIsHost(), CONFIRMED false in
// single player project-wide); 0x007161a0's own internal write is OPEN
// (not independently decompiled) - this project does NOT invent which
// tutorialState() code it writes, only that a resolved call reached the
// setter is counted, per index.
int stub_tutorial_unlock(lua_State* L) {
    logCall(L, upLog(L), "tutorial_unlock", upStateTag(L));
    std::string name = argString(L, 1);
    (void)optionalBoolDefault(L, 2, false); // the real setter's own 2nd argument - forwarded, internal effect OPEN
    EngineState* st = upState(L);
    if (!st->coopLocalIsHost()) return 0; // CONFIRMED no-op in single player
    EngineState::TutorialLookup lookup = EngineState::tutorialLookup(name);
    if (lookup.index != -1) {
        st->recordTutorialUnlock(lookup.index);
    }
    return 0;
}

// tutorial_queued (Sec24.12, 0x00a60190). 1 mandatory string (tutorial
// name). Return 1 boolean. CONFIRMED: resolves via the SAME 210-entry table
// tutorial_active already uses; pushes (state == 2) - a NEW confirmed state
// code ("queued"), joining the already-established state 3 ("active").
int stub_tutorial_queued(lua_State* L) {
    logCall(L, upLog(L), "tutorial_queued", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    EngineState::TutorialLookup lookup = EngineState::tutorialLookup(name);
    bool queued = false;
    if (lookup.index != -1) {
        queued = (st->tutorialState().get(EngineState::tutorialStateKey(lookup.index)) == 2);
    }
    lua_pushboolean(L, queued ? 1 : 0);
    return 1;
}

// trigger_type_enable (Sec24.13, 0x00a5ffe0). 1 mandatory string (trigger-
// type name), 1 optional boolean (standard nil-gated idiom, default true).
// Return none. CHOSEN simplification: the type NAME itself stands in for
// the real hash+64-bit-bitmask-by-type-id mechanism (no generic hash table
// modelled). CONFIRMED: either direction ALSO fires the SAME local-only
// notification record (identical opcode+sub-tag both ways, direction
// carried only by which local bit operation ran).
int stub_trigger_type_enable(lua_State* L) {
    logCall(L, upLog(L), "trigger_type_enable", upStateTag(L));
    std::string typeName = argString(L, 1);
    bool enable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    st->triggerTypeDisabled()[typeName] = !enable;
    EngineState::replicateStateChange("trigger_type_enable", typeName);
    ++st->triggerTypeEnableNotifyCount();
    return 0;
}

// trigger_set_hold_to_repeat (Sec24.14, 0x00a61a90). 1 mandatory string
// (trigger name, resolved via the shared flat objectResolves() stand-in), 1
// optional boolean (standard nil-gated idiom, default true). Return none.
// CONFIRMED: sets/clears a resolved trigger's own +0x7b bit 0x10.
int stub_trigger_set_hold_to_repeat(lua_State* L) {
    logCall(L, upLog(L), "trigger_set_hold_to_repeat", upStateTag(L));
    std::string name = argString(L, 1);
    bool holdToRepeat = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->triggerHoldToRepeat()[name] = holdToRepeat;
    return 0;
}

// skydive_set_elev_angles_deg (Sec24.15, 0x00a5e490). 2 mandatory numbers,
// no nil-gate on either (rounded via the established pair). Return none.
// CONFIRMED exact degrees-to-radians conversion (byte-confirmed 0x012a30d8
// = pi/180).
int stub_skydive_set_elev_angles_deg(lua_State* L) {
    logCall(L, upLog(L), "skydive_set_elev_angles_deg", upStateTag(L));
    int64_t a = truncateEa2596(lua_tonumber(L, 1));
    int64_t b = truncateEa2596(lua_tonumber(L, 2));
    EngineState* st = upState(L);
    st->skydiveElevAngleA() = static_cast<double>(a) * 0.017453;
    st->skydiveElevAngleB() = static_cast<double>(b) * 0.017453;
    return 0;
}

// set_suppress_synced_melee_attacks_flag (Sec24.16, 0x00a5d6a0). 1
// mandatory string (character), 1 optional boolean (standard nil-gated
// idiom, default true). Return none. CONFIRMED double-gate (reusing
// forceFlagGatePasses) - tag pair "human"+"human_force_flagssuppress_
// synced_melee_attacks", bit 0x40000000 of forceFlags1c98.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_suppress_synced_melee_attacks_flag(lua_State* L) {
    logCall(L, upLog(L), "set_suppress_synced_melee_attacks_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool suppress = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kSuppressSyncedMeleeAttacksBit1c98,
                                 suppress ? CharacterState::kSuppressSyncedMeleeAttacksBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("set_suppress_synced_melee_attacks_flag", resolvedName);
    }
    return 0;
}

// radio_newsbreak (Sec24.17, 0x00a5b570). 1 mandatory string (newsbreak
// name). Return none. CONFIRMED: sets the "current radio newsbreak" global
// to a resolved/hashed id (0x00462960's own internals OPEN) - the raw name
// is recorded, honestly, standing in for whatever resolved id results.
int stub_radio_newsbreak(lua_State* L) {
    logCall(L, upLog(L), "radio_newsbreak", upStateTag(L));
    std::string name = argString(L, 1);
    upState(L)->currentRadioNewsbreak() = name;
    return 0;
}

// player_supress_falling_scream (Sec24.18, 0x00a59a60). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in). No
// boolean argument exists. Return none. CONFIRMED: unconditionally ORs bit
// 0x80 into +0x1d99 - an "always-set, never-clear from this one entry
// point" flag (a separate clearer exists elsewhere in the binary, out of
// this batch's scope, per Sec24.18's own correction).
int stub_player_supress_falling_scream(lua_State* L) {
    logCall(L, upLog(L), "player_supress_falling_scream", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateCharacter(name).fallingScreamSuppressed = true;
    return 0;
}

// player_super_powered (Sec24.19, 0x00a59a00). 1 mandatory string
// (character), 1 mandatory boolean, no nil-gate. Return none. CONFIRMED
// double-gate (reusing forceFlagGatePasses) - tag pair "human"+"human_
// force_flagssuper_powered", bit 0x80000 of forceFlags1c9c.
int stub_player_super_powered(lua_State* L) {
    logCall(L, upLog(L), "player_super_powered", upStateTag(L));
    std::string name = argString(L, 1);
    bool powered = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kSuperPoweredBit1c9c,
                                 powered ? CharacterState::kSuperPoweredBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("player_super_powered", name);
    }
    return 0;
}

// on_wieldable_prop_destroy (Sec24.20, 0x00a57520). 2 mandatory strings:
// callback name + target object name, resolved via the shared flat
// objectResolves() stand-in. Return none. CONFIRMED: registers a per-
// object hook at the established +0x1f00 computed sub-record base, slot
// 0xe - this project's own uniform namedHookCallback stand-in.
int stub_on_wieldable_prop_destroy(lua_State* L) {
    logCall(L, upLog(L), "on_wieldable_prop_destroy", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(targetName)) return 0; // OPEN until set
    st->setNamedHookCallback("on_wieldable_prop_destroy:" + targetName, callbackName);
    return 0;
}

// on_taunt (Sec24.21, 0x00a585d0). 2 mandatory strings: callback name +
// target name/special token. Return none. CONFIRMED a genuine two-path
// structure: tries the generic resolver FIRST (base +0x1f00, slot 0xc);
// else, if the raw name is non-empty, falls back to resolving it as a
// character (base +0xf8, slot 0x11). This project's single flat
// objectResolves() map cannot independently distinguish the two real
// resolvers failing/succeeding for the SAME name - a stated simplification:
// only the generic (first) path is reachable when the name resolves at
// all; the character-fallback path is recorded for any non-empty name that
// does NOT resolve (this project cannot re-check the real character
// resolver's own liveness independently of the already-failed generic
// check).
int stub_on_taunt(lua_State* L) {
    logCall(L, upLog(L), "on_taunt", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().known(targetName) && st->objectResolves().get(targetName);
    if (resolved) {
        st->setNamedHookCallback("on_taunt:generic:" + targetName, callbackName);
    } else if (!targetName.empty()) {
        st->setNamedHookCallback("on_taunt:character:" + targetName, callbackName);
    }
    return 0;
}

// on_random_obj_killed_do (Sec24.22, 0x00a58d50). 1 mandatory string
// (object/category name, resolved through the shared name-to-hook-ref
// primitive - a registration pool, not a true existence resolve, per
// Sec24.22's own text), 1 mandatory string (callback name), 1 mandatory
// number (category selector 1-4, no nil-gate). Return none. CONFIRMED:
// dispatches to one of 4 category-specific handlers (own internals OPEN) -
// this project records the resolved registration, honestly.
int stub_on_random_obj_killed_do(lua_State* L) {
    logCall(L, upLog(L), "on_random_obj_killed_do", upStateTag(L));
    std::string objectCategoryName = argString(L, 1);
    std::string callbackName = argString(L, 2);
    int64_t category = truncateEa2596(lua_tonumber(L, 3));
    EngineState* st = upState(L);
    EngineState::RandomObjKilledHook hook;
    hook.objectCategoryName = objectCategoryName;
    hook.callbackName = callbackName;
    hook.category = static_cast<int32_t>(category);
    st->randomObjKilledHooks().push_back(hook);
    return 0;
}

// on_notoriety_event (Sec24.23, 0x00a57160). 1 mandatory string (callback
// name), 1 optional string (character name, standard nil-gated idiom,
// default the established "#PLAYER1#" sentinel). Return none. CONFIRMED:
// registers a per-object hook at the established +0x1f00 base, slot 2.
int stub_on_notoriety_event(lua_State* L) {
    logCall(L, upLog(L), "on_notoriety_event", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string characterName = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    if (!st->objectResolves().get(characterName)) return 0; // OPEN until set
    st->setNamedHookCallback("on_notoriety_event:" + characterName, callbackName);
    return 0;
}

// on_hud_screen_exit (Sec24.24, 0x00a56ea0). 2 mandatory strings: callback
// name + HUD-screen name. Return none. CONFIRMED: resolves the screen name
// through a DEDICATED registry singleton (0x012fced8, distinct from the
// SAME shared 0x02442750 multi-kind registry) - this project's own
// EngineState::hudScreenNameResolves() stands in (a never-populated,
// test-registrable object space, same precedent as notorietySpawnGroupResolves/
// shops elsewhere). On a match, registers a sibling-wrapper hook (slot 0,
// base +0x8).
int stub_on_hud_screen_exit(lua_State* L) {
    logCall(L, upLog(L), "on_hud_screen_exit", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string screenName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->hudScreenNameResolves().get(screenName)) return 0; // OPEN until set
    st->setNamedHookCallback("on_hud_screen_exit:" + screenName, callbackName);
    return 0;
}

// on_character_grabbed (Sec24.25, 0x00a578f0). 2 mandatory strings:
// callback name + target name/special token. Return none. CONFIRMED the
// SAME two-path structure as on_taunt above (checking the generic resolver
// path FIRST) - same stated simplification applies.
int stub_on_character_grabbed(lua_State* L) {
    logCall(L, upLog(L), "on_character_grabbed", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().known(targetName) && st->objectResolves().get(targetName);
    if (resolved) {
        st->setNamedHookCallback("on_character_grabbed:generic:" + targetName, callbackName);
    } else if (!targetName.empty()) {
        st->setNamedHookCallback("on_character_grabbed:character:" + targetName, callbackName);
    }
    return 0;
}

// npc_weapon_spread (Sec24.26, 0x00a55870). 1 mandatory string (character),
// 1 optional number (standard nil-gated idiom, default the established
// constant 1.0, byte-confirmed 0x01117a4c). Return none. CONFIRMED double-
// gate (reusing forceFlagGatePasses) - tag pair "npc"+"spread_multiplier",
// a float-payload member of the "npc_force_flags" family, +0x1e04.
int stub_npc_weapon_spread(lua_State* L) {
    logCall(L, upLog(L), "npc_weapon_spread", upStateTag(L));
    std::string name = argString(L, 1);
    double spread = optionalNumberDefault(L, 2, 1.0);
    EngineState* st = upState(L);
    // Resolved via the "#FOLLOWER#"-then-character chain 0x00a28150
    // (Sec24.26's own text) - wired 2026-10-08 (follower-sentinel-resolver
    // pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.npcSpreadMultiplier1e04 = spread;
    } else {
        EngineState::replicateStateChange("npc_weapon_spread", resolvedName);
    }
    return 0;
}

// npc_unholster_best_weapon (Sec24.27, 0x00a55790). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in).
// Return none. CONFIRMED: dispatches through the already-catalogued
// +0x1b78 "equipment" sub-object (0x00600720's own ~2,900-instruction body
// is OPEN) - only the resolved dispatch is counted.
int stub_npc_unholster_best_weapon(lua_State* L) {
    logCall(L, upLog(L), "npc_unholster_best_weapon", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    // Resolved via 0x00a28150 (Sec24.27's own text) - wired 2026-10-08
    // (follower-sentinel-resolver pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    ++st->getOrCreateCharacter(resolvedName).unholsterBestWeaponCount;
    return 0;
}

// npc_drop_grenades (Sec24.28, 0x00a55420). 1 mandatory boolean, no nil-
// gate. No string argument at all. Return none. CONFIRMED: a bare global,
// no resolver, no record.
int stub_npc_drop_grenades(lua_State* L) {
    logCall(L, upLog(L), "npc_drop_grenades", upStateTag(L));
    bool drop = lua_toboolean(L, 1) != 0;
    upState(L)->npcDropGrenades() = drop;
    return 0;
}

// =====================================================================
// Section 25.
// =====================================================================

// notoriety_spawn_group_set_destination (Sec25.1, 0x00a561f0). 2 mandatory
// strings: a notoriety spawn-GROUP name (resolved via the SAME 24-entry
// notoriety-group table Sec6.2/Sec6.10/Sec13.2/Sec21.3 already establish,
// EngineState::notorietySpawnGroupResolves()) + a destination reference
// name (resolved via the shared flat objectResolves() stand-in). Return
// none. CONFIRMED: writes the destination's own position/orientation block
// onto the group record via a newly-identified setter (0x008edfb0) - this
// project records the resolved destination NAME, honestly (no coordinate
// system).
int stub_notoriety_spawn_group_set_destination(lua_State* L) {
    logCall(L, upLog(L), "notoriety_spawn_group_set_destination", upStateTag(L));
    std::string groupName = argString(L, 1);
    std::string destName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->notorietySpawnGroupResolves().get(groupName)) return 0; // OPEN until set
    if (!st->objectResolves().get(destName)) return 0;                // OPEN until set
    auto& group = st->getOrCreateNotorietySpawnGroup(groupName);
    group.destinationTargetName = destName;
    ++group.setDestinationCount;
    return 0;
}

// notoriety_spawn_group_clear_destination (Sec25.2, 0x00a55e10). 1
// mandatory string (group name, SAME table as Sec25.1 above). Return none.
// CONFIRMED: calls the SAME setter (0x008edfb0) with a zeroed buffer and
// enable=0 - this project's own getOrCreateNotorietySpawnGroup record
// clears directly.
int stub_notoriety_spawn_group_clear_destination(lua_State* L) {
    logCall(L, upLog(L), "notoriety_spawn_group_clear_destination", upStateTag(L));
    std::string groupName = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->notorietySpawnGroupResolves().get(groupName)) return 0; // OPEN until set
    auto& group = st->getOrCreateNotorietySpawnGroup(groupName);
    group.destinationTargetName.clear();
    ++group.clearDestinationCount;
    return 0;
}

// mission_store_purchased (Sec25.3, 0x00a52720). 1 mandatory boolean.
// Arg = KIND selector (false store / true property). CONFIRMED pure query
// (spec sync f752324): sticky purchased flag of the first mission-tagged
// record of that kind, or true when no such record is loaded.
int stub_mission_store_purchased(lua_State* L) {
    logCall(L, upLog(L), "mission_store_purchased", upStateTag(L));
    bool isProperty = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    const std::optional<bool>& rec = st->missionStoreRecordPurchased(isProperty);
    lua_pushboolean(L, rec.has_value() ? (*rec ? 1 : 0) : 1);
    return 1;
}

// mission_set_next_mission (Sec25.4, 0x00a53b40) - CORRECTED by Sec60.3 (see
// this file's own top comment): in single player this native takes its own
// no-session path and changes nothing at all; the gate constant 0x011239d0
// is a true compile-time constant, not a live flag. Implemented as a
// genuine, confirmed no-op (same "genuine no-op, call count only"
// convention as bink_play above). 1 mandatory string (activity/mission
// name), read but never consumed further. Return none.
int stub_mission_set_next_mission(lua_State* L) {
    logCall(L, upLog(L), "mission_set_next_mission", upStateTag(L));
    (void)argString(L, 1); // read, matching the real call shape - CONFIRMED (Sec60.3) no further effect
    ++upState(L)->missionSetNextMissionNoopCount();
    return 0;
}

// message_remove_all (Sec25.5, 0x00a52340). No real arguments consumed
// (the lua_gettop result is never used). Return none. CONFIRMED a genuine
// "takes no real Lua arguments" case - no real message queue is modelled
// anywhere in this project, so only a call count stands in.
int stub_message_remove_all(lua_State* L) {
    logCall(L, upLog(L), "message_remove_all", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->messageRemoveAllCount();
    return 0;
}

// mesh_mover_update_control (Sec25.6, 0x00a53100). 1 mandatory string
// (mover reference, resolved via the shared flat objectResolves() stand-
// in), 1 mandatory string (control-parameter name), 1 mandatory value
// (boolean OR number, selecting the write path) + 1 optional boolean
// (truncate-to-integer mode, only meaningful when arg 3 is a number).
// Return none. CONFIRMED gates: the resolved placed-object's own +0xc8
// (PlacedObjectState::hasInteractionAnchor, already modelled for door_open,
// Sec1.10) must be CLEAR (this name is acting as a mover, not a door); its
// own +0xc4 (hasMoverControlSubRecord, a NEW field) must be present.
int stub_mesh_mover_update_control(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_update_control", upStateTag(L));
    std::string moverName = argString(L, 1);
    std::string controlName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(moverName)) return 0; // OPEN until set
    PlacedObjectState& obj = st->getOrCreatePlacedObject(moverName);
    if (obj.hasInteractionAnchor.get()) return 0;  // OPEN until set; acting as a door here, not a mover
    if (!(obj.hasMoverControlSubRecord.known() ? obj.hasMoverControlSubRecord.get() : true)) return 0; // e6691f1 Sec25.6: link modelled present unless a test says otherwise
    int argType = lua_type(L, 3);
    if (argType == LUA_TBOOLEAN) {
        obj.moverBoolControls[controlName] = lua_toboolean(L, 3) != 0;
    } else if (argType == LUA_TNUMBER) {
        double v = lua_tonumber(L, 3);
        bool truncateMode = optionalBoolDefault(L, 4, false);
        if (truncateMode) v = std::trunc(v);
        obj.moverNumericControls[controlName] = v;
    } else {
        return 0; // neither type - no real call shape in scope
    }
    ++obj.moverControlUpdateCount;
    return 0;
}

// item_hide (Sec25.7, 0x00a51ce0). 1 mandatory string (item/pickup
// reference, resolved via the shared flat objectResolves() stand-in).
// Return none. CONFIRMED: hides a named item/pickup, gated by a per-item
// capability predicate (vtable +0x68, OPEN internals) - CHOSEN: treated as
// always capable (no evidence otherwise). Reuses EngineState::itemHidden()
// directly - the SAME field item_show (Sec32.6, already implemented) uses,
// explicitly left for this native by that earlier batch's own doc comment.
int stub_item_hide(lua_State* L) {
    logCall(L, upLog(L), "item_hide", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->itemHidden()[name] = true;
    return 0;
}

// inv_item_is_firearm_equipped (Sec25.8, 0x00a50af0). 1 optional string
// (standard nil/absent-gated idiom, default the already-established
// "#PLAYER1#" sentinel). Return 1 boolean. CONFIRMED pure query; `false` on
// resolve failure.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_is_firearm_equipped(lua_State* L) {
    logCall(L, upLog(L), "inv_item_is_firearm_equipped", upStateTag(L));
    std::string name = optionalStringDefault(L, 1, "#PLAYER1#");
    EngineState* st = upState(L);
    bool equipped = false;
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        equipped = st->getOrCreateCharacter(resolvedName).firearmEquipped;
    }
    lua_pushboolean(L, equipped ? 1 : 0);
    return 1;
}

// human_set_force_holster (Sec25.9, 0x00a4d3b0). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in), 1
// mandatory boolean (no nil-gate). Return none. CONFIRMED hidden-`this`:
// writes DIRECTLY on the resolved character handle itself - a confirmed
// asymmetry against human_set_always_sprint below, which targets the
// +0x2b0 AI-data sub-object instead.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_human_set_force_holster(lua_State* L) {
    logCall(L, upLog(L), "human_set_force_holster", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    st->getOrCreateCharacter(resolvedName).forceHolster = flag;
    return 0;
}

// human_set_always_sprint (Sec25.10, 0x00a4d410). Identical argument shape
// to Sec25.9 above. Return none. CONFIRMED hidden-`this`: targets the
// per-character AI-data sub-object at +0x2b0 (a third confirmed consumer,
// joining alwaysCowerFlag/Sec15.4 and canTakeHumanShield/Sec25.26 below).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_human_set_always_sprint(lua_State* L) {
    logCall(L, upLog(L), "human_set_always_sprint", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    st->getOrCreateCharacter(resolvedName).alwaysSprintAiData2b0 = flag;
    return 0;
}

// human_riding_trigger_end (Sec25.11, 0x00a4d330). 1 mandatory string,
// resolved via the shared flat objectResolves() stand-in. Return none.
// CONFIRMED: ends a "riding" state/trigger interaction, gated by a
// predicate (this project's own test-settable CharacterState::
// ridingTriggerActive stands in for the real predicate, 0x009a8430's own
// internals OPEN).
int stub_human_riding_trigger_end(lua_State* L) {
    logCall(L, upLog(L), "human_riding_trigger_end", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.ridingTriggerActive) {
        c.ridingTriggerActive = false;
        ++c.humanRidingTriggerEndCount;
    }
    return 0;
}

// hud_inventory_disable (Sec25.12, 0x00a4f3e0). 1 mandatory boolean
// (despite a decompiler-rendering artifact on the lua_gettop call itself,
// raw disassembly confirms exactly 1 real argument). Return none.
// CONFIRMED "always both" category - unconditionally opens a record AND
// applies locally, with the host-check result forwarded as data (not a
// skip gate); a separate always-executed call (0x007f01b0, role OPEN)
// fires alongside.
int stub_hud_inventory_disable(lua_State* L) {
    logCall(L, upLog(L), "hud_inventory_disable", upStateTag(L));
    bool disable = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    st->hudInventoryDisabled() = disable;
    EngineState::replicateStateChange("hud_inventory_disable", "");
    ++st->hudInventoryDisableReplicateCount();
    ++st->hudInventoryDisableSecondaryCallCount(); // 0x007f01b0, role OPEN
    return 0;
}

// hud_critical_timer_get_remainder (Sec25.13, 0x00a4dcd0). No real
// arguments consumed. Return 1 number. CONFIRMED pure query of a DIFFERENT
// global timer (0x006cf080) from HudCriticalTimerState's own hud_critical_
// timer_set record - 0x006cf080's own internal state source is OPEN.
int stub_hud_critical_timer_get_remainder(lua_State* L) {
    logCall(L, upLog(L), "hud_critical_timer_get_remainder", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushnumber(L, upState(L)->hudCriticalTimerRemainder());
    return 1;
}

// helicopter_go_to_set_target (Sec25.14, 0x00a4fc00). 2 mandatory strings:
// helicopter/vehicle reference + a target reference (tried through a real
// 4-tier fallback chain this project collapses to the one shared flat
// objectResolves() map, per Sec29's "one shared global name map"
// convention). Return none. CONFIRMED: helicopter resolve failure is an
// immediate no-op. EXTENDS the EXISTING EngineState::HelicopterFlyTo struct
// (wave 1 Batch 3's own helicopter_fly_to_do/helicopter_fly_to_set_goal_
// direction, Sec12.11/Sec28.2) via its own new GoToTarget member, rather
// than a parallel struct.
int stub_helicopter_go_to_set_target(lua_State* L) {
    logCall(L, upLog(L), "helicopter_go_to_set_target", upStateTag(L));
    std::string heliName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(heliName)) return 0; // OPEN until set; CONFIRMED immediate no-op on failure
    bool targetResolves = st->objectResolves().get(targetName); // OPEN until set
    auto& helo = st->helicopterFlyTo();
    auto& rec = helo.lastGoToTarget[heliName];
    rec.targetName = targetResolves ? targetName : std::string();
    rec.applied = targetResolves;
    if (targetResolves) ++helo.goToSetTargetCount;
    return 0;
}

// helicopter_fly_to_stop (Sec25.15, 0x00a4cb80). 1 mandatory string
// (helicopter reference). Return none. CONFIRMED: gated by the SAME
// helicopter-qualification check helicopter_fly_to_do/_set_goal_direction
// already share (EngineState::HelicopterFlyTo::qualifies, Sec12.11/
// Sec28.2) - reused directly rather than a second gate; clears whichever of
// lastRequest/lastGoToTarget is present (0x00b37050's own internals OPEN).
int stub_helicopter_fly_to_stop(lua_State* L) {
    logCall(L, upLog(L), "helicopter_fly_to_stop", upStateTag(L));
    std::string heliName = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(heliName)) return 0; // OPEN until set
    auto& helo = st->helicopterFlyTo();
    if (!helo.qualifies.get(heliName)) return 0; // OPEN until set
    helo.lastRequest.erase(heliName);
    helo.lastGoToTarget.erase(heliName);
    ++helo.flyToStopCount;
    return 0;
}

// group_destroy_do (Sec25.16, 0x00a4c940). 1 mandatory string (script-group
// name, resolved via the SAME "guardian angel"/script-group table Sec6.6/
// Sec6.8 already establish - EngineState::guardianAngelGroupKnown/
// guardianAngelGroupRegistered, also shared by continuous_spawn_set_orders/
// Sec22.23). Return none. CONFIRMED: destroys the named group - this
// project marks it un-registered (CHOSEN: a destroyed group should not go
// on resolving as alive).
int stub_group_destroy_do(lua_State* L) {
    logCall(L, upLog(L), "group_destroy_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!(st->guardianAngelGroupKnown(name) && st->guardianAngelGroupRegistered(name))) return 0; // OPEN until set
    st->setGuardianAngelGroupRegistered(name, false);
    ++st->groupDestroyedCount();
    return 0;
}

// get_char_in_vehicle (Sec25.17, 0x00a4ad20). 1 mandatory string (vehicle
// reference, resolved via the shared flat objectResolves() stand-in), 1
// mandatory number (rounded, seat/slot index). Return 1 Lua value (string
// or nil). CONFIRMED pure query. This project has no real per-seat
// occupant registry beyond seat 0 (VehicleState::driverName, already
// modelled) - seat indices other than 0 are OPEN and answer nil.
int stub_get_char_in_vehicle(lua_State* L) {
    logCall(L, upLog(L), "get_char_in_vehicle", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    int64_t seat = truncateEa2596(lua_tonumber(L, 2));
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(vehicleName); // OPEN until set
    if (resolved && seat == 0) {
        VehicleState& v = st->getOrCreateVehicle(vehicleName);
        if (v.driverName.known() && !v.driverName.get().empty()) {
            lua_pushstring(L, v.driverName.get().c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// game_choice_get_state (Sec25.18, 0x00a4ace0). 1 mandatory number
// (rounded, choice id). Return 1 Lua value (boolean-shaped). CONFIRMED pure
// query - reuses EngineState::gameChoiceBitValue() directly, the SAME
// bitmask game_choice_set_state (Sec15.26, already implemented) writes.
int stub_game_choice_get_state(lua_State* L) {
    logCall(L, upLog(L), "game_choice_get_state", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    bool value = (id >= 0 && id < 256) ? st->gameChoiceBitValue(static_cast<uint8_t>(id)) : false;
    lua_pushboolean(L, value ? 1 : 0);
    return 1;
}

// force_ai_mode_check_done (Sec25.19, 0x00a4a660). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in).
// Return 1 boolean. CONFIRMED: clears the per-character pending-request tag
// (+0x4aa, CharacterState::forceAiModePendingTagSet stands in for the real
// 16-bit tag/queue-lookup pair) and pushes true on completion; false
// otherwise, tag untouched.
int stub_force_ai_mode_check_done(lua_State* L) {
    logCall(L, upLog(L), "force_ai_mode_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    // Resolved via the "#PLAYER#"/"#FOLLOWER#" chain (0x00a28150, Sec25.19's
    // own text) - wired 2026-10-08 (follower-sentinel-resolver pass).
    std::string resolvedName;
    bool resolved = resolveFollowerAwareObject(st, name, resolvedName); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    bool done = false;
    if (resolved) {
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        if (c.forceAiModePendingTagSet) {
            c.forceAiModePendingTagSet = false;
            done = true;
        }
    }
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// debris_flow_destroy (Sec25.20, 0x00a472a0). 1 mandatory number (rounded,
// debris-flow id). Return none. CONFIRMED: destroys the instance by id -
// EngineState::DebrisFlowState's own shared 3-slot table (SAME table
// debris_flow_create/debris_flow_add_object use). HARDENING POLICY: the id
// is bounded against the fixed 3-slot array before indexing it.
int stub_debris_flow_destroy(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_destroy", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& flow = st->debrisFlow();
    if (id >= 0 && id < static_cast<int64_t>(EngineState::DebrisFlowState::kFlowSlotCount)) {
        auto& inst = flow.flows[static_cast<size_t>(id)];
        if (inst.allocated) {
            inst.destroyed = true;
            inst.allocated = false; // freed for reuse by a later debris_flow_create
            ++flow.destroyCount;
        } else {
            ++flow.destroySkippedCount;
        }
    } else {
        ++flow.destroySkippedCount; // HARDENING POLICY - bounded array access
    }
    return 0;
}

// debris_flow_create (Sec25.21, 0x00a47270). No real arguments consumed.
// Return 1 number (the new flow's id). CONFIRMED: allocates a new debris-
// flow id - this project reuses the SAME 3-slot fixed table debris_flow_
// set_active/_add_object/_destroy already share. HARDENING POLICY
// (CHOSEN, hardened divergence): the real allocator's own internal
// algorithm is OPEN; when all 3 slots are already live, this host recycles
// slot 0 rather than inventing a 4th slot, counted via createRecycledCount.
int stub_debris_flow_create(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_create", upStateTag(L));
    (void)lua_gettop(L); // CONFIRMED read but unused
    EngineState* st = upState(L);
    auto& flow = st->debrisFlow();
    int64_t chosen = -1;
    for (size_t i = 0; i < EngineState::DebrisFlowState::kFlowSlotCount; ++i) {
        if (!flow.flows[i].allocated) {
            chosen = static_cast<int64_t>(i);
            break;
        }
    }
    if (chosen < 0) {
        chosen = 0; // CHOSEN (hardened divergence): all 3 slots already live -> reuse slot 0
        ++flow.createRecycledCount;
    }
    auto& inst = flow.flows[static_cast<size_t>(chosen)];
    inst.allocated = true;
    inst.destroyed = false;
    inst.objects.clear();
    inst.avoidObjects.clear();
    ++flow.createCount;
    lua_pushnumber(L, static_cast<double>(chosen));
    return 1;
}

// debris_flow_add_avoid_object (Sec25.22, 0x00a47e30). 1 mandatory number
// (rounded, debris-flow id), 1 mandatory string (object name to avoid,
// resolved via the shared flat objectResolves() stand-in). Return none.
// CONFIRMED: adds the resolved object's position as an "avoid" point for
// the given flow (no coordinate system - only the resolved NAME is
// recorded). HARDENING POLICY: the id is bounded against the fixed 3-slot
// array before indexing it (0x006fc100's own internal bound, if any, is
// OPEN - this project always bounds its own array access regardless).
int stub_debris_flow_add_avoid_object(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_add_avoid_object", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    std::string objectName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(objectName)) return 0; // OPEN until set
    auto& flow = st->debrisFlow();
    if (id >= 0 && id < static_cast<int64_t>(EngineState::DebrisFlowState::kFlowSlotCount)) {
        flow.flows[static_cast<size_t>(id)].avoidObjects.push_back(objectName);
        ++flow.addAvoidObjectCount;
    } else {
        ++flow.addAvoidObjectSkippedCount; // HARDENING POLICY - bounded array access
    }
    return 0;
}

// continuous_explosion_follow_target_add (Sec25.23, 0x00a462c0) - CORRECTED
// by Sec57's own tranche-23 pass (see this file's own top comment): the
// liveness check IS made, inside the resolver (0x00a45800) itself, so a
// plain objectResolves() check models it faithfully; +8/+0xc is a handle
// pair, not a position. 1 mandatory string (object reference). Return
// none. CONFIRMED: appends the resolved handle to the follow-target list
// ONLY while the ONE global continuous-explosion is active (reusing
// EngineState::continuousExplosion().active directly, Sec28.16/Sec22.24),
// capped at 2 entries (no duplicate check, a 3rd add is silently dropped).
int stub_continuous_explosion_follow_target_add(lua_State* L) {
    logCall(L, upLog(L), "continuous_explosion_follow_target_add", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set (liveness is inside this resolver, Sec57)
    auto& ce = st->continuousExplosion();
    bool active = ce.active.known() && ce.active.get();
    if (active && ce.followTargets.size() < EngineState::ContinuousExplosion::kFollowTargetCap) {
        ce.followTargets.push_back(name);
        ++ce.followTargetAddCount;
    } else {
        ++ce.followTargetAddDroppedCount;
    }
    return 0;
}

// check_script_animation_state (Sec25.24, 0x00a429c0). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in), 1
// optional string (standard nil-gated idiom, default absent). Return 1
// boolean. CONFIRMED the direct READ counterpart of the already-published
// set_script_animation_state (Sec7.17) - reuses CharacterState::
// scriptAnimationStateId/scriptNpcBound directly (already modelled) and the
// SAME shared name->id table (EngineState::lookupAnimationStateId).
int stub_check_script_animation_state(lua_State* L) {
    logCall(L, upLog(L), "check_script_animation_state", upStateTag(L));
    std::string name = argString(L, 1);
    std::string stateName = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    bool match = false;
    // arg1 resolves via the "#PLAYER#"/"#FOLLOWER#" chain (0x00a28150,
    // Sec25.24's own text) - wired 2026-10-08 (follower-sentinel-resolver
    // pass).
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        if (c.scriptNpcBound) {
            match = (c.scriptAnimationStateId == st->lookupAnimationStateId(stateName));
        }
    }
    lua_pushboolean(L, match ? 1 : 0);
    return 1;
}

// character_set_combat_ready (Sec25.25, 0x00a45100). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in), 1
// mandatory boolean (no nil-gate), 1 optional number (rounded, default 0).
// Return none. CONFIRMED single-gate (0x008addb0, reusing
// forceFlagGatePasses as this project's one shared double-gate/single-gate
// stand-in) - gate-true -> direct local apply; gate-false -> record.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_combat_ready(lua_State* L) {
    logCall(L, upLog(L), "character_set_combat_ready", upStateTag(L));
    std::string name = argString(L, 1);
    bool ready = lua_toboolean(L, 2) != 0;
    int64_t mode = truncateEa2596(optionalNumberDefault(L, 3, 0.0));
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.combatReady = ready;
        c.combatReadyMode = mode;
    } else {
        EngineState::replicateStateChange("character_set_combat_ready", resolvedName);
    }
    ++c.combatReadySetCount;
    return 0;
}

// character_set_can_take_human_shield (Sec25.26, 0x00a46590). 1 mandatory
// string (character, resolved via the shared flat objectResolves() stand-
// in), 1 mandatory boolean (no nil-gate). Return none. CONFIRMED hidden-
// `this`: targets the per-character AI-data sub-object at +0x2b0 (a fourth
// confirmed consumer, joining alwaysCowerFlag/Sec15.4, human_set_always_
// sprint/Sec25.10 above); the real engine stores the boolean NEGATED - this
// project keeps CharacterState::canTakeHumanShield in its natural, non-
// negated sense (see that field's own doc comment).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_can_take_human_shield(lua_State* L) {
    logCall(L, upLog(L), "character_set_can_take_human_shield", upStateTag(L));
    std::string name = argString(L, 1);
    bool canTake = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    st->getOrCreateCharacter(resolvedName).canTakeHumanShield = canTake;
    return 0;
}

// character_is_released (Sec25.28, 0x00a45e90). 1 mandatory string
// (character, resolved via the shared flat objectResolves() stand-in).
// Return 1 boolean. CONFIRMED: queries the vtable+0x70 "is bound/possessed"
// predicate (CharacterState::boundOrPossessed stands in) - `false`
// (bound) only if truthy; `true` (released) otherwise, INCLUDING on any
// resolve/liveness failure (a real, confirmed EXPLICIT resolve failure -
// i.e. a test setting objectResolves() false - takes this safe default; an
// unconfigured name still throws OPEN, same project-wide discipline as
// every other native in this file).
int stub_character_is_released(lua_State* L) {
    logCall(L, upLog(L), "character_is_released", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name); // OPEN until set
    bool released = true; // CONFIRMED default direction (unbound/resolve-failure -> released)
    if (resolved) {
        released = !st->getOrCreateCharacter(name).boundOrPossessed;
    }
    lua_pushboolean(L, released ? 1 : 0);
    return 1;
}

const std::vector<std::string>& b23Names() {
    static const std::vector<std::string> names = {
        // Section 24.
        "boss_battle_matt_cheats_stop",
        "bink_play",
        "wind_override_clear",
        "wander_start",
        "vehicle_spotlight_cancel",
        "vehicle_set_explosion_damage_multiplier",
        "vehicle_in_water",
        "vehicle_get_smoke_and_fire_state",
        "vehicle_enable_physics",
        "tutorial_unlock",
        "tutorial_queued",
        "trigger_type_enable",
        "trigger_set_hold_to_repeat",
        "skydive_set_elev_angles_deg",
        "set_suppress_synced_melee_attacks_flag",
        "radio_newsbreak",
        "player_supress_falling_scream",
        "player_super_powered",
        "on_wieldable_prop_destroy",
        "on_taunt",
        "on_random_obj_killed_do",
        "on_notoriety_event",
        "on_hud_screen_exit",
        "on_character_grabbed",
        "npc_weapon_spread",
        "npc_unholster_best_weapon",
        "npc_drop_grenades",
        // Section 25.
        "notoriety_spawn_group_set_destination",
        "notoriety_spawn_group_clear_destination",
        "mission_store_purchased",
        "mission_set_next_mission",
        "message_remove_all",
        "mesh_mover_update_control",
        "item_hide",
        "inv_item_is_firearm_equipped",
        "human_set_force_holster",
        "human_set_always_sprint",
        "human_riding_trigger_end",
        "hud_inventory_disable",
        "hud_critical_timer_get_remainder",
        "helicopter_go_to_set_target",
        "helicopter_fly_to_stop",
        "group_destroy_do",
        "get_char_in_vehicle",
        "game_choice_get_state",
        "force_ai_mode_check_done",
        "debris_flow_destroy",
        "debris_flow_create",
        "debris_flow_add_avoid_object",
        "continuous_explosion_follow_target_add",
        "check_script_animation_state",
        "character_set_combat_ready",
        "character_set_can_take_human_shield",
        "character_is_released",
    };
    return names;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB23() { return b23Names(); }

void registerB23(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // Section 24.
    if (wants("boss_battle_matt_cheats_stop")) registerOne(L, state, log, stateTag, "boss_battle_matt_cheats_stop", openGuard<stub_boss_battle_matt_cheats_stop>);
    if (wants("bink_play")) registerOne(L, state, log, stateTag, "bink_play", openGuard<stub_bink_play>);
    if (wants("wind_override_clear")) registerOne(L, state, log, stateTag, "wind_override_clear", openGuard<stub_wind_override_clear>);
    if (wants("wander_start")) registerOne(L, state, log, stateTag, "wander_start", openGuard<stub_wander_start>);
    if (wants("vehicle_spotlight_cancel")) registerOne(L, state, log, stateTag, "vehicle_spotlight_cancel", openGuard<stub_vehicle_spotlight_cancel>);
    if (wants("vehicle_set_explosion_damage_multiplier")) registerOne(L, state, log, stateTag, "vehicle_set_explosion_damage_multiplier", openGuard<stub_vehicle_set_explosion_damage_multiplier>);
    if (wants("vehicle_in_water")) registerOne(L, state, log, stateTag, "vehicle_in_water", openGuard<stub_vehicle_in_water>);
    if (wants("vehicle_get_smoke_and_fire_state")) registerOne(L, state, log, stateTag, "vehicle_get_smoke_and_fire_state", openGuard<stub_vehicle_get_smoke_and_fire_state>);
    if (wants("vehicle_enable_physics")) registerOne(L, state, log, stateTag, "vehicle_enable_physics", openGuard<stub_vehicle_enable_physics>);
    if (wants("tutorial_unlock")) registerOne(L, state, log, stateTag, "tutorial_unlock", openGuard<stub_tutorial_unlock>);
    if (wants("tutorial_queued")) registerOne(L, state, log, stateTag, "tutorial_queued", openGuard<stub_tutorial_queued>);
    if (wants("trigger_type_enable")) registerOne(L, state, log, stateTag, "trigger_type_enable", openGuard<stub_trigger_type_enable>);
    if (wants("trigger_set_hold_to_repeat")) registerOne(L, state, log, stateTag, "trigger_set_hold_to_repeat", openGuard<stub_trigger_set_hold_to_repeat>);
    if (wants("skydive_set_elev_angles_deg")) registerOne(L, state, log, stateTag, "skydive_set_elev_angles_deg", openGuard<stub_skydive_set_elev_angles_deg>);
    if (wants("set_suppress_synced_melee_attacks_flag")) registerOne(L, state, log, stateTag, "set_suppress_synced_melee_attacks_flag", openGuard<stub_set_suppress_synced_melee_attacks_flag>);
    if (wants("radio_newsbreak")) registerOne(L, state, log, stateTag, "radio_newsbreak", openGuard<stub_radio_newsbreak>);
    if (wants("player_supress_falling_scream")) registerOne(L, state, log, stateTag, "player_supress_falling_scream", openGuard<stub_player_supress_falling_scream>);
    if (wants("player_super_powered")) registerOne(L, state, log, stateTag, "player_super_powered", openGuard<stub_player_super_powered>);
    if (wants("on_wieldable_prop_destroy")) registerOne(L, state, log, stateTag, "on_wieldable_prop_destroy", openGuard<stub_on_wieldable_prop_destroy>);
    if (wants("on_taunt")) registerOne(L, state, log, stateTag, "on_taunt", openGuard<stub_on_taunt>);
    if (wants("on_random_obj_killed_do")) registerOne(L, state, log, stateTag, "on_random_obj_killed_do", openGuard<stub_on_random_obj_killed_do>);
    if (wants("on_notoriety_event")) registerOne(L, state, log, stateTag, "on_notoriety_event", openGuard<stub_on_notoriety_event>);
    if (wants("on_hud_screen_exit")) registerOne(L, state, log, stateTag, "on_hud_screen_exit", openGuard<stub_on_hud_screen_exit>);
    if (wants("on_character_grabbed")) registerOne(L, state, log, stateTag, "on_character_grabbed", openGuard<stub_on_character_grabbed>);
    if (wants("npc_weapon_spread")) registerOne(L, state, log, stateTag, "npc_weapon_spread", openGuard<stub_npc_weapon_spread>);
    if (wants("npc_unholster_best_weapon")) registerOne(L, state, log, stateTag, "npc_unholster_best_weapon", openGuard<stub_npc_unholster_best_weapon>);
    if (wants("npc_drop_grenades")) registerOne(L, state, log, stateTag, "npc_drop_grenades", openGuard<stub_npc_drop_grenades>);
    // Section 25.
    if (wants("notoriety_spawn_group_set_destination")) registerOne(L, state, log, stateTag, "notoriety_spawn_group_set_destination", openGuard<stub_notoriety_spawn_group_set_destination>);
    if (wants("notoriety_spawn_group_clear_destination")) registerOne(L, state, log, stateTag, "notoriety_spawn_group_clear_destination", openGuard<stub_notoriety_spawn_group_clear_destination>);
    if (wants("mission_store_purchased")) registerOne(L, state, log, stateTag, "mission_store_purchased", openGuard<stub_mission_store_purchased>);
    if (wants("mission_set_next_mission")) registerOne(L, state, log, stateTag, "mission_set_next_mission", openGuard<stub_mission_set_next_mission>);
    if (wants("message_remove_all")) registerOne(L, state, log, stateTag, "message_remove_all", openGuard<stub_message_remove_all>);
    if (wants("mesh_mover_update_control")) registerOne(L, state, log, stateTag, "mesh_mover_update_control", openGuard<stub_mesh_mover_update_control>);
    if (wants("item_hide")) registerOne(L, state, log, stateTag, "item_hide", openGuard<stub_item_hide>);
    if (wants("inv_item_is_firearm_equipped")) registerOne(L, state, log, stateTag, "inv_item_is_firearm_equipped", openGuard<stub_inv_item_is_firearm_equipped>);
    if (wants("human_set_force_holster")) registerOne(L, state, log, stateTag, "human_set_force_holster", openGuard<stub_human_set_force_holster>);
    if (wants("human_set_always_sprint")) registerOne(L, state, log, stateTag, "human_set_always_sprint", openGuard<stub_human_set_always_sprint>);
    if (wants("human_riding_trigger_end")) registerOne(L, state, log, stateTag, "human_riding_trigger_end", openGuard<stub_human_riding_trigger_end>);
    if (wants("hud_inventory_disable")) registerOne(L, state, log, stateTag, "hud_inventory_disable", openGuard<stub_hud_inventory_disable>);
    if (wants("hud_critical_timer_get_remainder")) registerOne(L, state, log, stateTag, "hud_critical_timer_get_remainder", openGuard<stub_hud_critical_timer_get_remainder>);
    if (wants("helicopter_go_to_set_target")) registerOne(L, state, log, stateTag, "helicopter_go_to_set_target", openGuard<stub_helicopter_go_to_set_target>);
    if (wants("helicopter_fly_to_stop")) registerOne(L, state, log, stateTag, "helicopter_fly_to_stop", openGuard<stub_helicopter_fly_to_stop>);
    if (wants("group_destroy_do")) registerOne(L, state, log, stateTag, "group_destroy_do", openGuard<stub_group_destroy_do>);
    if (wants("get_char_in_vehicle")) registerOne(L, state, log, stateTag, "get_char_in_vehicle", openGuard<stub_get_char_in_vehicle>);
    if (wants("game_choice_get_state")) registerOne(L, state, log, stateTag, "game_choice_get_state", openGuard<stub_game_choice_get_state>);
    if (wants("force_ai_mode_check_done")) registerOne(L, state, log, stateTag, "force_ai_mode_check_done", openGuard<stub_force_ai_mode_check_done>);
    if (wants("debris_flow_destroy")) registerOne(L, state, log, stateTag, "debris_flow_destroy", openGuard<stub_debris_flow_destroy>);
    if (wants("debris_flow_create")) registerOne(L, state, log, stateTag, "debris_flow_create", openGuard<stub_debris_flow_create>);
    if (wants("debris_flow_add_avoid_object")) registerOne(L, state, log, stateTag, "debris_flow_add_avoid_object", openGuard<stub_debris_flow_add_avoid_object>);
    if (wants("continuous_explosion_follow_target_add")) registerOne(L, state, log, stateTag, "continuous_explosion_follow_target_add", openGuard<stub_continuous_explosion_follow_target_add>);
    if (wants("check_script_animation_state")) registerOne(L, state, log, stateTag, "check_script_animation_state", openGuard<stub_check_script_animation_state>);
    if (wants("character_set_combat_ready")) registerOne(L, state, log, stateTag, "character_set_combat_ready", openGuard<stub_character_set_combat_ready>);
    if (wants("character_set_can_take_human_shield")) registerOne(L, state, log, stateTag, "character_set_can_take_human_shield", openGuard<stub_character_set_can_take_human_shield>);
    if (wants("character_is_released")) registerOne(L, state, log, stateTag, "character_is_released", openGuard<stub_character_is_released>);
}

} // namespace stub_sections
} // namespace sr3luahost
