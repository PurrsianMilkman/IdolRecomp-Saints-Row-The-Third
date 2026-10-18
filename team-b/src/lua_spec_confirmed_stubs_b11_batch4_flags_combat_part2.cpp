// lua_spec_confirmed_stubs_b11_batch4_flags_combat_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include "sr3save/save_crc.h"
#include <cmath>

namespace sr3luahost {
namespace stubs_b11 {

// helicopter_set_max_bank_angle (Sec13.23, 0x00a4ce20). 1 mandatory string
// (vehicle) + 1 mandatory number, no nil-gate (degrees). Return none.
// CONFIRMED-by-byte-dump degrees->radians constant (~0.017453, pi/180).
// Double-gate (reuses VehicleState::forceFlagGatePasses) writing
// VehicleState::maxBankAngleRadians.
int stub_helicopter_set_max_bank_angle(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_max_bank_angle", upStateTag(L));
    std::string name = argString(L, 1);
    double angleDeg = lua_tonumber(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    double angleRad = angleDeg * 0.017453; // CONFIRMED constant, byte-dump-verified
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.maxBankAngleRadians = angleRad;
    } else {
        EngineState::replicateStateChange("helicopter_set_max_bank_angle", name);
    }
    return 0;
}

// crib_disable_interface (Sec13.24, 0x00a42c60). 1 mandatory boolean, no
// nil-gate, no name/target. Return none. A pure global on/off switch
// (CONFIRMED full call chain, 2026-10-01 jobs - the "0x0101b4f0 stub" is
// inert, only the global write observable).
int stub_crib_disable_interface(lua_State* L) {
    logCall(L, upLog(L), "crib_disable_interface", upStateTag(L));
    bool disable = lua_toboolean(L, 1) != 0; // mandatory, no nil-gate
    upState(L)->cribInterfaceDisabled() = disable;
    return 0;
}

// character_ragdoll_set_last_valid_position (Sec13.25, 0x00a45f50). 1
// mandatory string (character) + 1 mandatory string (destination,
// resolved via the SAME flat objectResolves() map this project uses for
// every third-tier resolver) + 1 optional boolean, default false. Return
// none. CONFIRMED: ALWAYS opens a record (opcode 0x43, tag 0x29) with NO
// double-gate skip, but ONLY when both names resolve (the call to
// 0x00a26ec0 itself is gated on that). Maintains a per-character 10-entry
// rolling ring buffer (CharacterState::ragdollValidPositionHistory) - the
// destination's own NAME stands in for "the position fields" (no
// coordinate system in this project). false (default): append one entry;
// true: overwrite all 10 with the same value. DIFFERENT from character_
// ragdoll_clear_last_resort_position (Sec13.6) above - see this file's own
// top comment.
// Resolver 0x00a281a0 (arg 1, the character, only - arg 2's destination
// resolver 0x005982e0 is unaffected) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_ragdoll_set_last_valid_position(lua_State* L) {
    logCall(L, upLog(L), "character_ragdoll_set_last_valid_position", upStateTag(L));
    std::string name = argString(L, 1);
    std::string dest = argString(L, 2);
    bool resetAll = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    bool resolved = resolveFollowerAwareObject(st, name, resolvedName) && st->objectResolves().get(dest); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!resolved) return 0;
    EngineState::replicateStateChange("character_ragdoll_set_last_valid_position", resolvedName); // CONFIRMED always-both shape
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (resetAll) {
        c.ragdollValidPositionHistory.fill(dest); // CONFIRMED: overwrites ALL 10 slots
    } else {
        c.ragdollValidPositionHistoryIndex = (c.ragdollValidPositionHistoryIndex + 1) % 10; // CONFIRMED: 10-slot wrap
        c.ragdollValidPositionHistory[static_cast<size_t>(c.ragdollValidPositionHistoryIndex)] = dest;
    }
    return 0;
}

// character_collision_enable (Sec13.26, 0x00a41120). 1 mandatory string +
// 1 mandatory boolean, no nil-gate. Return none. CONFIRMED: this is really
// ONE fixed caller of a large REASON-CODE dispatcher (0x0094f450), not a
// simple bit flag - the individual reason codes/predicates are OPEN (not
// independently decompiled). Same "record that a call happened, not the
// real internal dispatch" convention as EngineState::callbacksClearedCount_
// (Sec28.17) - per-character count + last-requested value only.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_collision_enable(lua_State* L) {
    logCall(L, upLog(L), "character_collision_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    ++c.collisionEnableCallCount;
    c.lastCollisionEnableValue = enable;
    return 0;
}

// ai_set_personality (Sec13.27, 0x00a3c5b0). 1 mandatory string
// (character) + 1 mandatory string (personality profile), no nil-gate.
// Return none. Arg 2 resolves via a hash+table-scan helper (0x00507e60)
// whose own selection semantics are OPEN - EngineState::
// lookupAiPersonalityProfileCode is the test-registration hook (same
// "never-populated object space" precedent as lookupNotorietyTrackCode),
// CONFIRMED real fallback sentinel 0x14 (20) on no match. The further
// 0x011177e5 gating nuance (Sec13.27's own OPEN item) is not separately
// modelled - this project always proceeds to the double-gate write once a
// profile code is resolved (the common, CONFIRMED top-level path). Raw-
// disassembly-confirmed hidden-`this` delegate (Sec13.29 item 1).
int stub_ai_set_personality(lua_State* L) {
    logCall(L, upLog(L), "ai_set_personality", upStateTag(L));
    std::string name = argString(L, 1);
    std::string profile = argString(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    // arg1 resolved via 0x00a28150 (Sec13.27) - wired 2026-10-08 (follower-
    // sentinel-resolver pass). arg2 (profile) resolves through the SEPARATE
    // hash-table lookup 0x00507e60 (no #FOLLOWER# acceptance) - unchanged.
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    int32_t code = st->lookupAiPersonalityProfileCode(profile);
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.aiPersonalityProfileCode = code;
    } else {
        EngineState::replicateStateChange("ai_set_personality", resolvedName);
    }
    return 0;
}

// vehicle_suppress_flipping (Sec13.28, 0x00a636a0). 1 mandatory string +
// 1 optional boolean, default true. Return none. Raw-disassembly-confirmed
// hidden-`this` delegate - double-gate writing bit 0x8 of +0x1d7d, packed
// into the SAME combined forceFlags word (VehicleState::
// kSuppressFlippingBit1d7d, this project's own established +0x1d7c/
// +0x1d7d packing convention).
int stub_vehicle_suppress_flipping(lua_State* L) {
    logCall(L, upLog(L), "vehicle_suppress_flipping", upStateTag(L));
    std::string name = argString(L, 1);
    bool suppress = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.forceFlags.setBits(VehicleState::kSuppressFlippingBit1d7d,
                              suppress ? VehicleState::kSuppressFlippingBit1d7d : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_suppress_flipping", name);
    }
    return 0;
}

// =======================================================================
// Section 14 batch (spec-lua-api-behaviour.md Sec14, "ranks 151-250
// tranche, part B" - a 27-name subset; mesh_mover_set_invulnerable (14.9),
// party_dismiss_all (14.17) and zscene_is_loaded (14.23) are NOT this
// batch's own assignment, 2026-10-08).
// =======================================================================

// action_play_synced_state (Sec14.1, 0x00a3d3e0). 3 mandatory strings, no
// nil-gates. Return 1 number: -1 if either name fails to resolve, else 1.
// Reuses lookupSyncedActionIndex/kSyncedActionNotFound (the SAME, already-
// corrected 0x0095da50 mechanism action_play_synced_do, Sec33.3, already
// models - a plain name->index lookup, no further table cross-reference
// modelled). Reuses CharacterState::isDeadHighConfidence for actor 1's "is
// dead" check (the SAME field action_play_synced_do's own analogous call
// site already uses, for consistency with that established sibling). The
// per-actor local-authority gate (0x008addb0) is explicitly "not
// previously attributed to a specific subsystem" (Sec14.1's own text) -
// kept SEPARATELY SCOPED (EngineState::syncedActionAppliesLocally()),
// never reusing controlLockGatePasses. Local apply cross-writes a partner
// NAME (no coordinate system in this project, same convention as
// VehicleState::chaseTargetName).
// Resolver 0x00a281a0 (both args 1 and 2) - RE-CORRECTED (2026-10-08,
// direct disassembly of its own body): a two-stage #PLAYER#-then-
// #FOLLOWER# wrapper, not PLAYER-only as an earlier reading had it. This
// was one of the sites explicitly deferred pending that correction -
// wired here, follower-sentinel-resolver pass, via
// resolveFollowerAwareObject().
int stub_action_play_synced_state(lua_State* L) {
    logCall(L, upLog(L), "action_play_synced_state", upStateTag(L));
    std::string actor1 = argString(L, 1);
    std::string actor2 = argString(L, 2);
    std::string actionName = argString(L, 3);
    EngineState* st = upState(L);
    std::string resolvedActor1, resolvedActor2;
    bool resolved = resolveFollowerAwareObject(st, actor1, resolvedActor1) &&
                    resolveFollowerAwareObject(st, actor2, resolvedActor2); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!resolved) {
        lua_pushnumber(L, -1.0);
        return 1;
    }
    if (!st->getOrCreateCharacter(resolvedActor1).isDeadHighConfidence) {
        int32_t idx = st->lookupSyncedActionIndex(actionName);
        (void)idx; // no further synced-action table cross-reference modelled (same as action_play_synced_do)
        // 0x008addb0 (the per-actor ownership gate) is true for every object in single player (spec sync bfe0542,
        // Sec13.14 note / V.1 "network-authority test ... always passes in single player"); a test-set value wins.
        const bool appliesLocally = st->syncedActionAppliesLocally().known(resolvedActor1)
                                        ? st->syncedActionAppliesLocally().get(resolvedActor1)
                                        : true;
        if (appliesLocally) {
            CharacterState& c1 = st->getOrCreateCharacter(resolvedActor1);
            CharacterState& c2 = st->getOrCreateCharacter(resolvedActor2);
            c1.syncedActionPartnerName = resolvedActor2;
            c1.syncedActionPaired = true;
            c2.syncedActionPartnerName = resolvedActor1;
            c2.syncedActionPaired = true;
        } else {
            EngineState::replicateStateChange("action_play_synced_state", resolvedActor1);
        }
    }
    lua_pushnumber(L, 1.0); // CONFIRMED: 1 whenever both names resolved, per the Return line
    return 1;
}

// vehicle_disable_chase (Sec14.2, 0x00a62170). 1 mandatory string + 1
// optional boolean, default true. Return none. Direct sibling of
// vehicle_chase (Sec9.4) on the SAME per-vehicle chase/behavior sub-record
// (+0xc68); the real setter receives arg 2 directly after a double
// negation (CONFIRMED). The exact sub-record bit offset is OPEN -
// VehicleState::chaseDisabled is this project's own CHOSEN project-tracked
// stand-in (same convention as kneecappersEnabled).
int stub_vehicle_disable_chase(lua_State* L) {
    logCall(L, upLog(L), "vehicle_disable_chase", upStateTag(L));
    std::string name = argString(L, 1);
    bool disable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).chaseDisabled = disable;
    return 0;
}

// set_players_can_be_downed (Sec14.3, 0x00a5d4d0). 1 mandatory string
// (gate-only resolve via the generic/"#PLAYER#" chain) + 1 mandatory
// boolean, no nil-gate. Return none. CONFIRMED literal debug tags
// "player"/"sync_flagscant_be_downed" - a NEGATED-sense polarity (the Lua
// argument means "can be downed"; the stored flag is the opposite,
// matching the established negated-boolean convention). Bit 0 of status
// byte +0x28ad (CharacterState::statusByte28ac/28ad, Sec14.31's own
// player-category census).
int stub_set_players_can_be_downed(lua_State* L) {
    logCall(L, upLog(L), "set_players_can_be_downed", upStateTag(L));
    std::string name = argString(L, 1);
    bool canBeDowned = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ad.setBits(CharacterState::kCantBeDownedBit28ad,
                                  canBeDowned ? 0u : CharacterState::kCantBeDownedBit28ad); // CONFIRMED negated
    } else {
        EngineState::replicateStateChange("set_players_can_be_downed", name);
    }
    return 0;
}

// set_player_can_take_human_shields (Sec14.4, 0x00a5d400). Same gate-only
// resolve as above + 1 OPTIONAL boolean, default true (a real, confirmed
// asymmetry against set_players_can_be_downed's unconditional arg 2).
// Return none. Debug tags "player"/"sync_flagscant_take_human_shields" -
// bit 7 of status byte +0x28ac (a DIFFERENT byte from set_players_can_be_
// downed's own +0x28ad). HIGH CONFIDENCE negated polarity, by direct
// analogy with Sec14.3's own "identical shape" text (not independently
// re-confirmed in the spec's own prose for this specific entry).
int stub_set_player_can_take_human_shields(lua_State* L) {
    logCall(L, upLog(L), "set_player_can_take_human_shields", upStateTag(L));
    std::string name = argString(L, 1);
    bool canTake = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ac.setBits(CharacterState::kCantTakeHumanShieldsBit28ac,
                                  canTake ? 0u : CharacterState::kCantTakeHumanShieldsBit28ac); // HIGH CONFIDENCE negated, by analogy
    } else {
        EngineState::replicateStateChange("set_player_can_take_human_shields", name);
    }
    return 0;
}

// player_human_shield_lock (Sec14.5, 0x00a59720). Same gate-only resolve +
// 1 mandatory boolean, unconditional (matches set_players_can_be_downed's
// shape, not its sibling's optional one). Return none. Debug tags
// "player"/"sync_flagshuman_shield_state_locked" - NO negating prefix, and
// matching that, NO negation applied (a confirmed, checked polarity
// difference from both siblings above). Bit 3 of the SAME status byte
// +0x28ad set_players_can_be_downed also uses.
int stub_player_human_shield_lock(lua_State* L) {
    logCall(L, upLog(L), "player_human_shield_lock", upStateTag(L));
    std::string name = argString(L, 1);
    bool locked = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ad.setBits(CharacterState::kHumanShieldLockedBit28ad,
                                  locked ? CharacterState::kHumanShieldLockedBit28ad : 0u); // CONFIRMED: no negation
    } else {
        EngineState::replicateStateChange("player_human_shield_lock", name);
    }
    return 0;
}

// on_mover_enter_water (Sec14.6, 0x00a58200). 1 mandatory string
// (callback, opaque) + 1 mandatory string (object, resolved via the
// generic named-placed-object resolver - this project's flat
// objectResolves() stand-in). Return none. Registers into mover hook slot
// 3 (+0x20/+0x24 sub-record) - this RESOLVES the document's own
// previously-flagged OPEN "3rd unidentified kind" hook-slot array
// (PlacedObjectState::onMoverEnterWaterCallback). Empty callback clears
// (same convention as onMoverDestroyedCallback).
int stub_on_mover_enter_water(lua_State* L) {
    logCall(L, upLog(L), "on_mover_enter_water", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string object = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(object)) return 0; // OPEN until set
    st->getOrCreatePlacedObject(object).onMoverEnterWaterCallback = callback;
    return 0;
}

// on_ai_do_action_complete (Sec14.7, 0x00a577c0). 1 mandatory string
// (callback, opaque) + 1 mandatory string (character reference). Return
// none. CONFIRMED, checked asymmetry against on_death: arg 2 is tested
// against the generic/"#PLAYER#" resolver FIRST, and a successful match
// there means DO NOTHING (reuses EngineState::playerClassChainResolves(),
// the SAME mechanism on_vehicle_destroyed/character_is_dead already
// establish for this exact resolver-order shape). Only the plain-character
// fallback path registers character hook slot 0x14 (20).
int stub_on_ai_do_action_complete(lua_State* L) {
    logCall(L, upLog(L), "on_ai_do_action_complete", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string character = argString(L, 2);
    EngineState* st = upState(L);
    if (st->playerClassChainResolves(character)) { // CHOSEN default now applies when no test .set() it - CONFIRMED: generic/#PLAYER# match => do nothing
        return 0;
    }
    if (st->objectResolves().get(character)) { // OPEN until set - plain-character fallback resolve + liveness
        st->getOrCreateCharacter(character).onAiDoActionCompleteCallback = callback;
    }
    return 0;
}

// mission_set_excluded_reward (Sec14.8, 0x00a526f0). 1 mandatory string
// (reward id). Return none. Gated on the mission being active AND not in
// one specific excluded game-mode state (0x014c8460/0x014c7a14 != 8) -
// SEPARATELY SCOPED from EngineState::MissionContext::activeAndRunning
// (this project's own established "do not merge across call sites unless
// a spec explicitly confirms shared scope" convention, same reasoning as
// that field's own doc comment), plus a per-object-type descriptor bit.
// The reward name is hashed via sr3save::nameHash (CONFIRMED match to the
// real 0x00d9e8b0 hash, reused per se3.4/Sec13.29) and de-duplicated
// against a growing per-mission list; a duplicate is silently ignored, a
// new id is appended and replicated (further site of opcode 0x44, first
// seen Sec13.4).
int stub_mission_set_excluded_reward(lua_State* L) {
    logCall(L, upLog(L), "mission_set_excluded_reward", upStateTag(L));
    std::string reward = argString(L, 1);
    EngineState* st = upState(L);
    auto& m = st->missionExcludedRewards();
    if (m.activeAndRunning.get() && m.descriptorBit8At0xc.get()) { // OPEN until set
        uint32_t hashed = sr3save::nameHash(reward);
        bool dup = false;
        for (uint32_t h : m.excludedRewardHashes) {
            if (h == hashed) {
                dup = true;
                break;
            }
        }
        if (dup) {
            ++m.dedupSkipCount; // CONFIRMED: a duplicate is silently ignored
        } else {
            m.excludedRewardHashes.push_back(hashed);
            EngineState::replicateStateChange("mission_set_excluded_reward", reward); // opcode 0x44
        }
    }
    return 0;
}

// mesh_mover_kill (Sec14.10, 0x00a53000). 1 mandatory string (mover,
// resolved via 0x006434a0 + liveness - this project's flat
// objectResolves() stand-in) + 1 optional string, default absent
// (instigator/owner, resolved the same way when given). Return none. Two
// internal paths depending on which "extended" sub-object the mover has
// (PlacedObjectState::hasScriptedKillSubObjectC0/hasPlainKillSubObjectDc,
// test-settable, CHOSEN false - no generic sub-object graph in this
// project). The +0xc0 scripted path's vtable+0x80 internals are OPEN
// (counted only). The +0xdc path shares the THIRD distinct "should this
// replicate" gate (0x027038c0 == 0, Sec14.9/Sec14.31) with mesh_mover_set_
// invulnerable (NOT this batch's scope) - PlacedObjectState::
// replicateGate027038c0Zero models that comparison's own result directly.
// CONFIRMED, non-obvious finding: with the real call's damage amount
// always 0, this path only FINALIZES an already-at-or-below-zero health
// value into a destroy, never deals damage itself.
// Owner (arg 2) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here via
// resolveFollowerAwareObject(). The mover's own resolver (arg 1,
// 0x006434a0) is unaffected - distinct resolver, not re-corrected.
int stub_mesh_mover_kill(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_kill", upStateTag(L));
    std::string name = argString(L, 1);
    std::string owner = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    if (!owner.empty()) {
        std::string resolvedOwner;
        (void)resolveFollowerAwareObject(st, owner, resolvedOwner); // OPEN until set; the resolved handle (or 0) is not separately observable in this project's model; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    }
    auto& mover = st->getOrCreatePlacedObject(name);
    if (mover.hasScriptedKillSubObjectC0) {
        ++mover.scriptedKillCount; // vtable+0x80 internals OPEN, count only
    } else if (mover.hasPlainKillSubObjectDc) {
        if (mover.replicateGate027038c0Zero.get()) { // OPEN until set
            EngineState::replicateStateChange("mesh_mover_kill", name);
        } else if (mover.killSubObjectHealth <= 0) {
            mover.killSubObjectHealth = 0;
            ++mover.plainKillFinalizeCount; // CONFIRMED: amount=0 only finalizes an already-<=0 value
        }
    }
    return 0;
}

// helicopter_shoot_navpoint (Sec14.11, 0x00a4fd80). 1 mandatory string
// (helicopter) + 1 mandatory string (navpoint, resolved via the flat
// objectResolves() map) + 1 optional boolean, default true. Return 1
// boolean. Fires only if the helicopter resolves AND is in the SAME AI
// drive-mode state vehicle_chase (Sec9.4) establishes
// (VehicleState::aiDriveModeIsChasing, reused directly, not re-derived -
// Sec14.11's own text explicitly cross-references this exact predicate).
// The shared helicopter-fire dispatcher's own targeting/hit-test logic (no
// coordinate system in this project) is a test-settable outcome
// (VehicleState::helicopterFireSucceeds), shared with helicopter_shoot_
// human below per Sec14.31's own explicit sharing.
int stub_helicopter_shoot_navpoint(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_navpoint", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string navpoint = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, true);
    (void)arg3; // passed through to the shared dispatcher; not separately modelled (see doc comment)
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(heli) && st->objectResolves().get(navpoint); // OPEN until set
    if (!resolved) {
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(heli);
    ++v.helicopterShootNavpointCount;
    if (v.aiDriveModeIsChasing) { // CONFIRMED gate, reused from Sec9.4
        lua_pushboolean(L, v.helicopterFireSucceeds.get() ? 1 : 0); // OPEN until set
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

// customization_item_wear (Sec14.12, 0x00a43970). 3 mandatory strings
// (item/variant/color) + 1 optional boolean default true (immediate wear)
// + 1 optional number/flags default 3 + 1 optional boolean default false
// (refresh thumbnails). Return none. Bit 0x1: applies to the primary
// local-player object (reuses EngineState::hasLocalPlayer(), the SAME real
// 0x009da4e0 global). Bit 0x2: additionally applies to the co-op/network-
// roster representation, gated on playerRig().coopPlayerPresent (the SAME
// 0x009df3d0 global, reused directly - the exact precedent this project's
// own b04 batch already established for an identical bit-gated dual
// apply). Shares the SAME dual local-player customization state as
// customization_outfit_wear above (Sec14.31's own cross-batch note for the
// PCU/PCR family). Always runs a fixed UI-refresh tail (counted only -
// internals OPEN).
int stub_customization_item_wear(lua_State* L) {
    logCall(L, upLog(L), "customization_item_wear", upStateTag(L));
    std::string item = argString(L, 1);
    std::string variant = argString(L, 2);
    std::string color = argString(L, 3);
    bool immediateWear = optionalBoolDefault(L, 4, true);
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 5, 3.0));
    bool refreshThumbnails = optionalBoolDefault(L, 6, false);
    EngineState* st = upState(L);
    EngineState::CustomizationItemWearRequest req;
    req.item = item;
    req.variant = variant;
    req.color = color;
    req.immediateWear = immediateWear;
    req.flags = flags;
    req.refreshThumbnails = refreshThumbnails;
    if (flags & 0x1) {
        if (st->hasLocalPlayer().get()) { // OPEN until set
            req.appliedPrimary = true;
        }
    }
    if (flags & 0x2) {
        if (st->playerRig().coopPlayerPresent.get()) { // CONFIRMED false by default (applySpecInitialState item 13) - no-op in single player
            req.appliedSecondRepresentation = true;
        }
    }
    EngineState::replicateStateChange("customization_item_wear", item); // CHOSEN: top-level call, same reasoning as customization_outfit_wear above
    ++st->customizationUiRefreshTailCount();                            // unconditional 4-bit UI refresh + 2 further notify calls, count only
    st->customizationItemWearRequests().push_back(req);
    return 0;
}

// customization_item_revert (Sec14.13, 0x00a438f0). 1 optional number/
// flags, default 3 - the ONLY argument. Return none. Genuinely simpler and
// non-targeted (a real, checked asymmetry against its near-identically-
// named sibling above): bit 0x1 sets a single global "revert pending" flag
// (EngineState::customizationRevertPending()); bit 0x2 replicates (further
// opcode-0x44 site). Shares the same always-run UI-refresh tail counter.
int stub_customization_item_revert(lua_State* L) {
    logCall(L, upLog(L), "customization_item_revert", upStateTag(L));
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if (flags & 0x1) {
        st->customizationRevertPending() = true;
    }
    if (flags & 0x2) {
        EngineState::replicateStateChange("customization_item_revert", "");
    }
    ++st->customizationItemRevertCount();
    ++st->customizationUiRefreshTailCount();
    return 0;
}

// vehicle_set_keyframed_physics (Sec14.14, 0x00a65310). 1 mandatory string
// + 1 mandatory boolean, unconditional. Return none. Decompilation
// degraded by stack-protection instrumentation (flagged, not glossed
// over). A single hardcoded mission vehicle ("m05_heli_pierce", case-
// insensitive) gets an extra orientation-capture block first (internals
// OPEN, counted only via VehicleState::keyframedPhysicsSpecialCaseCount).
// Regardless, the boolean is always written and ALWAYS replicated
// (CONFIRMED - no double-gate skip at this call site; the record's own
// opcode argument was not visible in the decompile, OPEN, not guessed).
int stub_vehicle_set_keyframed_physics(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_keyframed_physics", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (caseInsensitiveEquals(name, "m05_heli_pierce")) {
        ++v.keyframedPhysicsSpecialCaseCount; // extra orientation-capture block, internals OPEN
    }
    v.keyframedPhysics = flag;
    EngineState::replicateStateChange("vehicle_set_keyframed_physics", name); // CONFIRMED: always replicated
    return 0;
}

// vehicle_hide (Sec14.15, 0x00a64250). 1 mandatory string + 1 optional
// boolean, default FALSE (a real, confirmed asymmetry against most other
// optional booleans in this batch, which default true). Return none.
// ALWAYS hides the main body (a literal "hide" parameter is fixed at this
// call site, CONFIRMED); arg 2 ADDITIONALLY hides attached sub-objects/
// occupants (no attachment array modelled - only the two observable
// booleans, VehicleState::hidden/attachmentsHidden). DIFFERENT function
// from the already-registered vehicle_show - see this file's own top
// comment.
int stub_vehicle_hide(lua_State* L) {
    logCall(L, upLog(L), "vehicle_hide", upStateTag(L));
    std::string name = argString(L, 1);
    bool hideAttachments = optionalBoolDefault(L, 2, false); // CONFIRMED default false, asymmetric vs. most of this batch
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.hidden = true; // CONFIRMED: always hides the main body
    if (hideAttachments) {
        v.attachmentsHidden = true;
    }
    return 0;
}

// persona_override_persona_start (Sec14.16, 0x00a5ad20). 1 mandatory
// string (character) + 1 mandatory string (persona, resolved via the
// already-independently-catalogued resolver tail 0x00462960->0x0046fd00) -
// this project extends its Sec29 "one shared name map" simplification to
// this resolver too (the tail's own real table contents are not given
// anywhere, same gap class as every other hashed/data-table lookup in this
// batch). Return none. On both resolving, sets CharacterState::
// personaOverrideName (the context getter's own "silently fails if null"
// case, Sec14.16's own OPEN item, is not separately modelled - this
// project always proceeds once both names resolve).
// Character (arg 1) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08,
// direct disassembly of its own body): a two-stage #PLAYER#-then-
// #FOLLOWER# wrapper, not PLAYER-only as an earlier reading had it -
// wired here via resolveFollowerAwareObject(). Arg 2's persona resolver
// (0x00462960->0x0046fd00) is unaffected - distinct resolver.
int stub_persona_override_persona_start(lua_State* L) {
    logCall(L, upLog(L), "persona_override_persona_start", upStateTag(L));
    std::string character = argString(L, 1);
    std::string persona = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedCharacter;
    // ce4556f Sec17.9 FINAL: arg 2 is an audio persona NAME (Wwise FNV-1 id via
    // 0x0046fd00), never a named object - it must not go through the object
    // resolver. Only arg 1 resolves.
    bool resolved = resolveFollowerAwareObject(st, character, resolvedCharacter); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (resolved) {
        st->getOrCreateCharacter(resolvedCharacter).personaOverrideName = persona;
    }
    return 0;
}

// helicopter_shoot_human (Sec14.18, 0x00a4d550). 1 mandatory string
// (helicopter) + 1 mandatory string (target character) + 1 optional
// boolean default true (random spread) + 1 optional number default 1.0
// (CONFIRMED .rdata spread-radius constant, shared with debris_flow_set_
// active's own default) + 1 optional boolean default true (immediate).
// Return 1 boolean. CONFIRMED, checked asymmetry against helicopter_shoot_
// navpoint: this native does NOT itself check the AI drive-mode gate - the
// shared dispatcher's own internal gating is what actually governs
// whether the shot lands (VehicleState::helicopterFireSucceeds, shared per
// Sec14.31). Spread/geometry not modelled (no coordinate system).
// Target (arg 2) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here via
// resolveFollowerAwareObject(). The helicopter's own resolver (arg 1,
// 0x00a281e0) is unaffected - distinct resolver.
int stub_helicopter_shoot_human(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_human", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    bool spread = optionalBoolDefault(L, 3, true);
    double radius = optionalNumberDefault(L, 4, 1.0); // CONFIRMED .rdata constant (same default as debris_flow_set_active)
    bool immediate = optionalBoolDefault(L, 5, true);
    (void)spread;
    (void)radius;
    (void)immediate; // geometry/spread not modelled - no coordinate system in this project
    EngineState* st = upState(L);
    std::string resolvedTarget;
    bool resolved = st->objectResolves().get(heli) && resolveFollowerAwareObject(st, target, resolvedTarget); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!resolved) {
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(heli);
    ++v.helicopterShootHumanCount;
    lua_pushboolean(L, v.helicopterFireSucceeds.get() ? 1 : 0); // OPEN until set; no drive-mode gate here, see doc comment
    return 1;
}

// drug_effect_set_override_values (Sec14.19, 0x00a474e0). 2 mandatory
// numbers, unconditional + 1 optional number/flags, default 3. Return
// none. Always targets channel 0 of a structurally multi-channel pair of
// parallel arrays (HIGH CONFIDENCE other channels exist and are reachable
// elsewhere, per Sec14.19's own text). Bit 0x2 (default on): replicate
// (THIRD confirmed site of opcode 0x40). Bit 0x1 (default on): writes both
// values directly (EngineState::drugEffectOverrideValueA/B).
int stub_drug_effect_set_override_values(lua_State* L) {
    logCall(L, upLog(L), "drug_effect_set_override_values", upStateTag(L));
    double a = lua_tonumber(L, 1); // mandatory, unconditional
    double b = lua_tonumber(L, 2); // mandatory, unconditional
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    if (flags & 0x2) {
        EngineState::replicateStateChange("drug_effect_set_override_values", ""); // THIRD confirmed opcode-0x40 site
    }
    if (flags & 0x1) {
        st->drugEffectOverrideValueA() = a;
        st->drugEffectOverrideValueB() = b;
    }
    return 0;
}

// debris_flow_set_active (Sec14.20, 0x00a48040). 1 mandatory number/flag
// (debris-flow index) + 1 mandatory string (primary location, resolved via
// the flat objectResolves() map) + 3 mandatory numbers (direction vector) +
// 1 optional string, default absent (relative-to location) + 1 optional
// number, default 1.0 (CONFIRMED .rdata radius constant, shared with
// helicopter_shoot_human's own default) + 2 optional booleans, defaults
// true/false. Return none. This project has no coordinate system (stated
// gap) - the direction vector and the +0x64/+0x68/+0x6c field reads are
// NOT modelled, only the CONFIRMED-structure parts: whether the relative-
// to location resolved, the 3-slot fixed global table (size CONFIRMED,
// contents not, EngineState::debrisFlow()), and the host-check gate
// (reuses EngineState::coopLocalIsHost() directly, the SAME real
// singleton Sec8.27/Sec6.22/Sec10.9 already establishes).
int stub_debris_flow_set_active(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_set_active", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1)); // mandatory, unconditional
    std::string primary = argString(L, 2);
    double dx = lua_tonumber(L, 3);
    double dy = lua_tonumber(L, 4);
    double dz = lua_tonumber(L, 5);
    (void)dx;
    (void)dy;
    (void)dz; // direction vector not modelled further - no coordinate math in this project
    std::string relativeTo = optionalStringDefault(L, 6, "");
    double radius = optionalNumberDefault(L, 7, 1.0); // CONFIRMED .rdata constant
    bool enabled = optionalBoolDefault(L, 8, true);
    bool arg9 = optionalBoolDefault(L, 9, false);
    (void)radius;
    (void)enabled;
    (void)arg9;
    EngineState* st = upState(L);
    (void)st->objectResolves().get(primary); // OPEN until set; the +0x64/+0x68/+0x6c field reads off it are OPEN, not modelled
    auto& flow = st->debrisFlow();
    if (!relativeTo.empty() && st->objectResolves().get(relativeTo)) { // OPEN until set
        ++flow.relativeLocationAppliedCount;
    }
    if (st->coopLocalIsHost()) { // reuses the SAME host-check singleton
        EngineState::replicateStateChange("debris_flow_set_active", primary); // opcode 0x45
    }
    if (index >= 0 && index < 3) {
        flow.slotActive[static_cast<size_t>(index)] = true; // the 3-slot fixed global table, size CONFIRMED
    }
    ++flow.activateCount;
    return 0;
}

// camera_restrict_set_limits (Sec14.21, 0x00a442e0). 4 mandatory numbers
// (min/max yaw, min/max pitch) + 1 optional string, default absent (target
// player) + 1 optional boolean, default false. Return none. The SAME
// degrees-to-radians constant Sec13.23 uses. A genuine 3-way dispatch keyed
// on WHO the limits are for: no arg 5 -> local write only; arg 5 resolves
// to something OTHER than the local player -> replicate only; arg 5 IS the
// local player -> both. The "fixed camera-restriction record" this
// function targets has no other identified owner in this project
// (EngineState::cameraRestrictLimits(), a singleton stand-in). The local-
// player NAME comparison (EngineState::localPlayerNameForTesting()) is a
// SEPARATELY SCOPED new stand-in - no prior field carried the local
// player's own name as a string, only presence/gender booleans.
int stub_camera_restrict_set_limits(lua_State* L) {
    logCall(L, upLog(L), "camera_restrict_set_limits", upStateTag(L));
    double minYaw = lua_tonumber(L, 1);
    double maxYaw = lua_tonumber(L, 2);
    double minPitch = lua_tonumber(L, 3);
    double maxPitch = lua_tonumber(L, 4);
    std::string target = optionalStringDefault(L, 5, "");
    bool arg6 = optionalBoolDefault(L, 6, false);
    EngineState* st = upState(L);
    const double kDegToRad = 0.017453; // CONFIRMED constant, same as Sec13.23
    bool hasTarget = !target.empty();
    bool targetIsLocalPlayer = false;
    if (hasTarget) {
        bool resolved = st->objectResolves().get(target); // OPEN until set (via 0x00a280c0)
        if (resolved && st->localPlayerNameForTesting().known() && st->localPlayerNameForTesting().get() == target) {
            targetIsLocalPlayer = true;
        }
    }
    bool doLocalWrite = !hasTarget || targetIsLocalPlayer;
    bool doReplicate = hasTarget;
    if (doLocalWrite) {
        auto& rec = st->cameraRestrictLimits();
        rec.minYaw = minYaw * kDegToRad;
        rec.maxYaw = maxYaw * kDegToRad;
        rec.minPitch = minPitch * kDegToRad;
        rec.maxPitch = maxPitch * kDegToRad;
        rec.arg6 = arg6;
        rec.written = true;
    }
    if (doReplicate) {
        EngineState::replicateStateChange("camera_restrict_set_limits", target); // opcode 0x43, tag 0xd
    }
    return 0;
}

// audio_conversation_playing (Sec14.22, 0x00a3ca00). 1 mandatory
// number/flag (slot index, -1 = "any"). Return 1 boolean, pure query. A
// GENUINELY DIFFERENT mechanism from audio_any_conversation_playing/
// EngineState::conversationChannelActive (Sec33.4's own per-session-
// channel flag, kept separate per this header's own near-duplicate
// caution) - this one walks a SLOT-INDEXED linked list (0x006de990) or
// queries one slot directly (0x00877dc0). -1: scans every test-registered
// slot (bounded, honest - an absent registration never throws here, since
// an empty real list IS a legitimate, non-OPEN state); a specific slot:
// OPEN until that slot is set.
int stub_audio_conversation_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_conversation_playing", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1)); // mandatory, unconditional
    EngineState* st = upState(L);
    bool result = false;
    if (slot == -1) {
        for (int i = 0; i < 64; ++i) { // a bounded scan of plausible test-registered slot ids, not a real list walk
            std::string key = std::to_string(i);
            if (st->conversationSlotPlaying().known(key) && st->conversationSlotPlaying().get(key)) {
                result = true;
                break;
            }
        }
    } else {
        result = st->conversationSlotActive(std::to_string(slot)); // Sec14.22 port rule: no voice playback modelled -> false unless a host marked it playing
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// vehicle_speed_cancel (Sec14.24, 0x00a635f0). 1 mandatory string. Return
// none. Direct undo-counterpart to vehicle_speed_override (Sec9.5) -
// clears the SAME fields that function writes (VehicleState::hasSpeedCap/
// speedCapRaw), no network replication (matches that function's own
// already-confirmed local-only behavior).
int stub_vehicle_speed_cancel(lua_State* L) {
    logCall(L, upLog(L), "vehicle_speed_cancel", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.hasSpeedCap = false; // CONFIRMED: undoes vehicle_speed_override's own writes
    v.speedCapRaw = 0.0;
    return 0;
}

// vehicle_disable_flee (Sec14.25, 0x00a622b0). Structurally identical to
// vehicle_disable_chase (Sec14.2) through a distinct address pair, on the
// SAME per-vehicle sub-record (+0xc68). VehicleState::fleeDisabled, same
// CHOSEN stand-in convention.
int stub_vehicle_disable_flee(lua_State* L) {
    logCall(L, upLog(L), "vehicle_disable_flee", upStateTag(L));
    std::string name = argString(L, 1);
    bool disable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).fleeDisabled = disable;
    return 0;
}

// set_cant_flee_flag (Sec14.26, 0x00a5ce90). 1 mandatory string (dual-mode
// resolver - this project's flat objectResolves() stand-in covers both the
// group and plain-character branches uniformly) + 1 optional boolean,
// default true. Return none. Debug tags "human_ai_data"/
// "ai_force_flagscant_flee" confirm NO negation is applied (unlike the
// "cant_be_downed"/"cant_take_human_shields" pair). Bit 0x20 of byte +0xa -
// a DIFFERENT bit of the SAME real byte CharacterState::followerCanAbandon
// already models bit 0x02 of (CharacterState::cantFleeFlag, kept
// independent per this header's own "two bits, two fields" convention).
int stub_set_cant_flee_flag(lua_State* L) {
    logCall(L, upLog(L), "set_cant_flee_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    // Resolved via 0x00a28150, the "#PLAYER#"-then-"#FOLLOWER#" chain
    // (Sec14.26) - wired 2026-10-08 (follower-sentinel-resolver pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.cantFleeFlag.set(flag);      // CONFIRMED: no negation
    } else {
        EngineState::replicateStateChange("set_cant_flee_flag", resolvedName);
    }
    return 0;
}

// set_behavior_set (Sec14.27, 0x00a5f170). 1 mandatory string (character,
// resolved exclusively via the plain-character path - this project's flat
// objectResolves() stand-in, same exclusive-plain-resolver shape as
// on_ai_do_action_complete above) + 1 mandatory string (behavior-set
// name). Return none. Arg 2 resolves through a dedicated, case-insensitive
// fixed-table lookup (0x004f27f0) this project has no real contents for -
// EngineState::lookupBehaviorSetCode is the test-registration hook (same
// "never-populated object space" precedent), CONFIRMED real sentinel 0x2c
// (44) on no match. The vtable+0x70 gate decides which of two apply paths
// (validated vs. raw-write) ran - both write the SAME observable field in
// this project's model (CharacterState::behaviorSetIndex), so this native
// does NOT force that field to be known before proceeding (it would be an
// invented obstacle to gate a call on a fact that provably cannot change
// its own outcome) - CharacterState::behaviorSetVtableGatePasses stays
// available for a test to .set() directly if it wants to record which
// path a given call is standing in for, but is never read here.
int stub_set_behavior_set(lua_State* L) {
    logCall(L, upLog(L), "set_behavior_set", upStateTag(L));
    std::string name = argString(L, 1);
    std::string behaviorSetName = argString(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    int32_t code = st->lookupBehaviorSetCode(behaviorSetName);
    CharacterState& c = st->getOrCreateCharacter(name);
    c.behaviorSetIndex = code;
    return 0;
}

// roadblocks_enable (Sec14.28, 0x00a5b6b0). 1 optional boolean, default
// true (applies both when absent and whenever the presence/type test
// itself fails - matches optionalBoolDefault's own default=true reading).
// Return none. A single global byte; false additionally fires a cleanup
// (HIGH CONFIDENCE "clear/despawn all active roadblocks", not confirmed).
int stub_roadblocks_enable(lua_State* L) {
    logCall(L, upLog(L), "roadblocks_enable", upStateTag(L));
    bool enable = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    st->roadblocksEnabled() = enable;
    if (!enable) {
        ++st->roadblocksDisableCleanupCount(); // HIGH CONFIDENCE cleanup, Sec14.28
    }
    return 0;
}

// player_script_controlled_clear (Sec14.29, 0x00a59890). 1 mandatory
// string (gate-only resolve, matches the player-flags-trio idiom). Return
// none. CONFIRMED: the SAME 0x008addb0 mechanism controlLockGatePasses
// already models (Sec7.7/Sec7.22/Sec7.25), "tested for exact value 1 here
// rather than the 'not equal to zero' shape used elsewhere - an immaterial
// difference in comparison form, same underlying test" per Sec14.29's own
// cross-reference - so, unlike ai_scripted_action_is_processing/
// action_play_synced_state above, THIS call site IS attributed to the
// established field and reused directly. Clears CharacterState::
// scriptControlledMode (+0x2888) and bit 0x2 of status byte +0x28ac - the
// SAME byte set_player_can_take_human_shields targets its own bit 0x80 on,
// now shown to carry a second, independently-named flag.
int stub_player_script_controlled_clear(lua_State* L) {
    logCall(L, upLog(L), "player_script_controlled_clear", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) { // SAME 0x008addb0 mechanism as Sec7.7, reused directly per Sec14.29's own cross-ref
        c.scriptControlledMode = 0;
        c.statusByte28ac.setBits(CharacterState::kScriptControlledBit28ac, 0);
    } else {
        EngineState::replicateStateChange("player_script_controlled_clear", name);
    }
    return 0;
}

// on_vehicle_enter_water (Sec14.30, 0x00a589c0). 1 mandatory string
// (callback, opaque) + 1 mandatory string (vehicle, resolved via the flat
// objectResolves() map + liveness). Return none. Registers into VEHICLE
// hook slot 8 (+0x30/+0x34 sub-record, already established) via a THIRD
// distinct registration-helper address (0x00a56d50) sharing the SAME
// clear/convert/notify shape as the character and mover hook-slot helpers
// (Sec14.31's own census). Same empty-string-clears-slot convention as
// onVehicleDestroyedCallback.
int stub_on_vehicle_enter_water(lua_State* L) {
    logCall(L, upLog(L), "on_vehicle_enter_water", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string vehicle = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicle)) return 0; // OPEN until set
    st->getOrCreateVehicle(vehicle).onVehicleEnterWaterCallback = callback;
    return 0;
}

} // namespace stubs_b11
} // namespace sr3luahost
