// lua_spec_confirmed_stubs_b16_wave2_sec20_21_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include <algorithm>
#include <cmath>

namespace sr3luahost {
namespace stubs_b16 {

// character_is_jumping (Sec20.28, 0x00a41710). 1 mandatory string
// (character name). 1 optional boolean, standard nil-gated idiom, default
// TRUE. Return: on success, exactly 2 booleans (true, false - CONFIRMED
// double-push quirk, the success path falls through into the same shared
// tail every failure path also reaches); on failure, exactly 1 (false).
// CONFIRMED: requires movementStateByteCcd == 4 ALWAYS; if the boolean
// argument is true (default), ADDITIONALLY requires movementStateByteCcc
// == 3 AND NOT (stateEnum == 3, "seated in a vehicle"). POST-MERGE
// CORRECTION: this originally read its own dedicated
// vehicleOccupancyPhase16d4 field, independently added for the exact same
// real +0x16d4 byte CharacterState::stateEnum already models (Sec3.4/
// Sec3.10, "3 = seated in a vehicle") - the two were a latent duplicate-
// field hazard (flagged by a reviewer, not silently resolved at the time)
// until the vehicle enter/exit sync-writes fix made stateEnum the one
// field the real engine's own enter/exit path actually keeps current;
// reading the separate, permanently-unwritten duplicate here would have
// silently never seen that update. Collapsed onto stateEnum directly.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_is_jumping(lua_State* L) {
    logCall(L, upLog(L), "character_is_jumping", upStateTag(L));
    std::string name = argString(L, 1);
    bool strictCheck = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    bool required = c.movementStateByteCcd.get() == 4; // OPEN until set, ALWAYS required
    if (required && strictCheck) {
        bool stateOk = c.movementStateByteCcc.get() == 3; // OPEN until set
        bool seated = c.stateEnum.get() == 3; // OPEN until set (shared field, see this stub's own doc comment)
        required = stateOk && !seated;
    }
    if (required) {
        lua_pushboolean(L, 1);
        lua_pushboolean(L, 0); // CONFIRMED double-push quirk
        return 2;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// character_ignite (Sec20.29, 0x00a41a20). 1 mandatory string (character
// name). Return: none. CONFIRMED: resolves via objectResolves(); delegates
// to 0x0097cf80 with all-hardcoded flag arguments. Fixed 12-simultaneous-
// fires cap (EngineState::charactersOnFire()/kCharactersOnFireCap). Sets
// +0xe8 bit 0x100 ("on fire"). Unless already downed (reuses
// CharacterState::movementStateByteCcd == 4, the SAME shared field Sec20.28
// also tests - see that field's own doc comment), triggers the "catch
// fire" reaction, or notifies a hook partner instead if one is present
// (not modeled - no hook-partner data in scope, so this project always
// takes the "catch fire" branch when not downed). CHOSEN read-guard
// asymmetry vs. Sec20.28 (deliberate, not an oversight): character_is_
// jumping's own RETURN VALUE depends directly on this field, so it
// honestly refuses (OpenStateError) when unknown; here the SAME field only
// gates a SECONDARY internal counter (igniteCatchFireReactionCount) with
// no effect on this native's own `Return: none` contract or its primary
// on-fire-bit effect, so a `.known()` guard (default "not downed" when
// unestablished) is used instead - this native's own main effect never
// blocks on a test having populated this field.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_ignite(lua_State* L) {
    logCall(L, upLog(L), "character_ignite", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    name = resolvedName;
    CharacterState& c = st->getOrCreateCharacter(name);
    auto& fires = st->charactersOnFire();
    if (fires.count(name) == 0) {
        if (fires.size() >= EngineState::kCharactersOnFireCap) {
            return 0; // CONFIRMED real cap - not a divergence, just the fixed array being full
        }
        fires.insert(name);
    }
    c.flagsE8.setBits(CharacterState::kOnFireBit, CharacterState::kOnFireBit);
    bool downed = c.movementStateByteCcd.known() && c.movementStateByteCcd.get() == 4;
    if (!downed) {
        ++c.igniteCatchFireReactionCount;
    }
    return 0;
}

// character_has_human_shield (Sec20.30, 0x00a41450). 1 mandatory string
// (character name). Return: 1 boolean. CHOSEN: the real intricate multi-
// field chain is abstracted to ONE flag, CharacterState::
// hasActiveHumanShieldRelationship (see that field's own doc comment).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_has_human_shield(lua_State* L) {
    logCall(L, upLog(L), "character_has_human_shield", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        result = st->getOrCreateCharacter(resolvedName).hasActiveHumanShieldRelationship.get(); // OPEN until set
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// character_disable_human_downed_interactions (Sec20.31, 0x00a41290). 1
// mandatory string. 1 optional boolean, standard nil-gated idiom, default
// TRUE. Return: none. CONFIRMED: resolves via objectResolves(); double-gate
// (variant 1): gate passes -> bit 0x40000 of the SAME +0x1c9c word
// character_disable_revival (Sec21.9) also uses; gate fails -> replicate
// only.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_disable_human_downed_interactions(lua_State* L) {
    logCall(L, upLog(L), "character_disable_human_downed_interactions", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kHumanDownedInteractDisabledBit1c9c,
                                  flag ? CharacterState::kHumanDownedInteractDisabledBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_disable_human_downed_interactions", resolvedName);
    }
    return 0;
}

// =======================================================================
// Sec21 group (28 names)
// =======================================================================

// on_collision (Sec21.1, 0x00a57a10). 2 mandatory strings: callback name,
// target name/"#PLAYER#". Return: none. CONFIRMED PRIMARY path only
// (CHOSEN simplification - see CharacterState::onCollisionCallback's own
// doc comment for why the vehicle-fallback branch is not separately
// modeled): resolves via objectResolves(), registers hook code 0x13 at the
// +0x1f00 sub-record.
int stub_on_collision(lua_State* L) {
    logCall(L, upLog(L), "on_collision", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(targetName)) { // OPEN until set
        st->getOrCreateCharacter(targetName).onCollisionCallback = callbackName;
    }
    return 0;
}

// npc_use_closest_action_node_of_type (Sec21.2, 0x00a566d0). Arg1 (string)
// character; arg2 (string) action-node type name; arg3 (optional number,
// standard nil-gated idiom, default 10.0) search radius. Return: 1
// boolean. CONFIRMED: character resolves via objectResolves(); type name
// validated via EngineState::actionNodeTypeNameValid() (OPEN per name - no
// CONFIRMED concrete table entries anywhere in scope); on EITHER failure,
// pushes false with no further effect. Otherwise writes radius/type fields
// and ALWAYS opens an opcode-0x43 network record (tracked via
// replicateStateChange); the pushed boolean is a hardcoded `true` -
// CONFIRMED NOT the same as the (unmodeled, write-only) internal +0x16a
// success byte.
int stub_npc_use_closest_action_node_of_type(lua_State* L) {
    logCall(L, upLog(L), "npc_use_closest_action_node_of_type", upStateTag(L));
    std::string character = argString(L, 1);
    std::string typeName = argString(L, 2);
    double radius = optionalNumberDefault(L, 3, 10.0);
    EngineState* st = upState(L);
    bool characterOk = st->objectResolves().get(character); // OPEN until set
    bool typeOk = characterOk && st->actionNodeTypeNameValid().known(typeName) && st->actionNodeTypeNameValid().get(typeName);
    if (!characterOk || !typeOk) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    c.lastActionNodeRadius = radius;
    c.lastActionNodeTypeId = 1; // CHOSEN: a validated type name maps to "some valid id", exact value not in-scope-observable
    ++c.actionNodeNetworkRecordCount; // unconditional once resolved+valid (CONFIRMED)
    EngineState::replicateStateChange("npc_use_closest_action_node_of_type", character);
    lua_pushboolean(L, 1); // CONFIRMED hardcoded true, not the internal success byte
    return 1;
}

// notoriety_set_override_timer (Sec21.3, 0x00a55bf0). Arg1 (string) track
// name; arg2 (boolean, unconditional) enable/disable; arg3/arg4 (numbers,
// unconditional, rounded) min/max. Return: none. CONFIRMED: resolves via
// the SAME 24-entry track table lookupNotorietyTrackCode() already models
// (Sec3.6). Writes the boolean directly; writes min/max ONLY if the
// boolean is true. If coopLocalIsHost(), additionally re-derives the
// track's own index and broadcasts (host-only, tracked via
// replicateStateChange) - a fourth, distinct replication shape (host-only
// conditional broadcast keyed by table index).
int stub_notoriety_set_override_timer(lua_State* L) {
    logCall(L, upLog(L), "notoriety_set_override_timer", upStateTag(L));
    std::string trackName = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0;
    double minVal = truncateEa2596(lua_tonumber(L, 3));
    double maxVal = truncateEa2596(lua_tonumber(L, 4));
    EngineState* st = upState(L);
    if (st->lookupNotorietyTrackCode(trackName) == EngineState::kNotorietyTrackCodeNotFound) return 0;
    auto& timer = st->notorietyOverrideTimers()[trackName];
    timer.enabled = enable;
    if (enable) {
        timer.minValue = minVal;
        timer.maxValue = maxVal;
    }
    if (st->coopLocalIsHost()) {
        EngineState::replicateStateChange("notoriety_set_override_timer", trackName);
    }
    return 0;
}

// inv_item_remove_in_slot (Sec21.4, 0x00a50c40). Arg1 (mandatory string)
// inventory SLOT name (not item name); arg2 (optional string, standard
// nil-gated idiom via lua_type, default "#PLAYER1#") target character.
// Return: none. CONFIRMED: target resolves via objectResolves(); the gate/
// literal at this call site is CONFIRMED "effectively always-true" (the
// caller's own literal argument is always 0) - so this project always
// takes the direct-apply branch (look up the slot's occupant, remove it),
// never the opcode-0x41 record branch (genuinely unreachable at this call
// site, per the spec's own text). TRANSPOSITION NOTE: `inventorySlotContentsBySlotId`
// is keyed by resolved numeric slot id, intentionally distinct from any
// OTHER wave-2 batch's own (unslotted, different spec entry) `inv_item_
// remove` native - see CharacterState's own doc comment. CHOSEN (not
// independently spec-stated, but the natural consequence of this host's
// own simplified single-equip-slot model, equippedItemName): if the
// removed slot's own item is the SAME one currently equipped, this host
// also clears equippedItemName, so inv_item_is_equipped/inv_item_get_
// equipped_item stay consistent with the removal - the real engine's own
// 0x00602020 item-definition-based removal may or may not do this exact
// thing (OPEN, not independently decompiled), but leaving a dangling
// "equipped" reference to a just-removed slot would be a worse, silent
// inconsistency this project's own discipline avoids.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_remove_in_slot(lua_State* L) {
    logCall(L, upLog(L), "inv_item_remove_in_slot", upStateTag(L));
    std::string slotName = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    target = resolvedTarget;
    int32_t slotId = st->resolveInventorySlotId(slotName);
    if (slotId == 0xff) return 0; // CONFIRMED "not found" sentinel - no effect
    CharacterState& c = st->getOrCreateCharacter(target);
    auto it = c.inventorySlotContentsBySlotId.find(slotId);
    if (it != c.inventorySlotContentsBySlotId.end() && !it->second.empty()) {
        if (caseInsensitiveEquals(it->second, c.equippedItemName)) c.equippedItemName.clear();
        it->second.clear();
    }
    return 0;
}

// ingame_effect_add_trigger (Sec21.5, 0x00a51670). Arg1 (mandatory string)
// trigger/world-object name; arg2 (mandatory string) effect name; arg3
// (optional number, standard nil-gated idiom, default 3) a 2-bit action
// mask. Return: none. CONFIRMED: arg1 resolves via objectResolves(); arg2
// resolves to a numeric effect id via a dedicated, test-settable lookup
// (no CONFIRMED concrete table given). If BOTH resolve: mask bit 0 writes
// the id (PlacedObjectState::effectId) only if it actually changed; mask
// bit 1 opens a record (3-deep gate, CHOSEN always-passes per this
// project's single-player convention) - tracked via replicateStateChange.
int stub_ingame_effect_add_trigger(lua_State* L) {
    logCall(L, upLog(L), "ingame_effect_add_trigger", upStateTag(L));
    std::string objectName = argString(L, 1);
    std::string effectName = argString(L, 2);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    if (!st->objectResolves().get(objectName)) return 0; // OPEN until set
    auto& ids = st->effectIdByName();
    auto found = ids.find(effectName);
    int32_t effectId = found != ids.end() ? found->second : -1;
    if (effectId == -1) return 0; // CONFIRMED: effect id lookup failed, no further effect
    PlacedObjectState& p = st->getOrCreatePlacedObject(objectName);
    bool changed = p.effectId != effectId;
    if (mask & 0x2) {
        EngineState::replicateStateChange("ingame_effect_add_trigger", objectName);
        ++p.effectReplicateCount;
    }
    if ((mask & 0x1) && changed) {
        p.effectId = effectId;
        ++p.effectApplyLocalCount;
    }
    return 0;
}

// hud_push_screen (Sec21.6, 0x00a4e670). Arg1 (mandatory string) HUD
// screen name; arg2 (optional number, standard nil-gated idiom, default 1)
// a 2-bit action mask. Return: none. CONFIRMED: resolved via a DEDICATED
// registry (EngineState::hudScreenResolves(), NOT objectResolves() - a
// genuinely different real singleton, per this native's own text). Mask
// bit 0: if the screen's own +0x30 field (hudScreenTypeField(), OPEN)
// equals 0x29, counts the "Planet_Saints" side effect; then pushes onto a
// dedup'd stack. Mask bit 1: an unconditional broadcast (no gate at all) -
// tracked via replicateStateChange.
int stub_hud_push_screen(lua_State* L) {
    logCall(L, upLog(L), "hud_push_screen", upStateTag(L));
    std::string screenName = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 1.0));
    EngineState* st = upState(L);
    if (!st->hudScreenResolves().known(screenName) || !st->hudScreenResolves().get(screenName)) return 0; // OPEN until set
    if (mask & 0x1) {
        if (st->hudScreenTypeField().known(screenName) && st->hudScreenTypeField().get(screenName) == 0x29) {
            ++st->hudPushScreenPlanetSaintsCount;
        }
        auto& stack = st->hudScreenStack();
        if (std::find(stack.begin(), stack.end(), screenName) == stack.end()) {
            stack.push_back(screenName); // CONFIRMED dedup - already-present screen is a no-op push
        }
    }
    if (mask & 0x2) {
        EngineState::replicateStateChange("hud_push_screen", screenName);
        ++st->hudPushScreenBroadcastCount;
    }
    return 0;
}

// continuous_explosion_avoid_object_add (Sec21.7, 0x00a46240). 1 mandatory
// string (object name). Return: none. CONFIRMED: resolves via
// objectResolves(); the real position-pair gate/hard-8-slot-cap is modeled
// as a fixed-capacity name list (no position data for the resolved
// record's own linked-object chain, see PlacedObjectState's own doc
// comment).
int stub_continuous_explosion_avoid_object_add(lua_State* L) {
    logCall(L, upLog(L), "continuous_explosion_avoid_object_add", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    auto& list = st->continuousExplosionAvoidObjects();
    if (list.size() < EngineState::kContinuousExplosionAvoidObjectCap) {
        list.push_back(name);
    }
    return 0;
}

// character_set_only_player_grabs (Sec21.8, 0x00a425f0). 1 mandatory
// string; 1 optional boolean, standard nil-gated idiom, default TRUE.
// Return: none. CONFIRMED: resolves via the #FOLLOWER#-family resolver
// 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08 (follower-
// sentinel-resolver pass); double-gate (variant 1): bit 0x8000000 of the
// SAME +0x1c98 word Sec20.26/Sec20.27 also use.
int stub_character_set_only_player_grabs(lua_State* L) {
    logCall(L, upLog(L), "character_set_only_player_grabs", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kOnlyPlayerGrabsBit1c98, flag ? CharacterState::kOnlyPlayerGrabsBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_only_player_grabs", resolvedName);
    }
    return 0;
}

// character_disable_revival (Sec21.9, 0x00a41300). 1 mandatory string; 1
// optional boolean, standard nil-gated idiom, default TRUE. Return: none.
// CONFIRMED: resolves via the generic/#PLAYER# chain ~~(NOT #FOLLOWER#, a
// real confirmed difference from Sec21.8 above)~~ **STALE per the
// 2026-10-08 0x00a281a0 re-disassembly (see Sec3.14/Sec7.33's own
// cross-function note): Sec21.9's own "NOT #FOLLOWER#" framing predates
// that correction and describes the OLD, now-overturned belief that
// 0x00a281a0 itself was PLAYER-only - the resolver FUNCTION (same address,
// same calling convention at every call site) is now confirmed a two-stage
// #PLAYER#-then-#FOLLOWER# wrapper everywhere it's called, this site
// included** - double-gate (variant 1): bit 0x20000 of the SAME +0x1c9c
// word Sec20.31 also uses. Wired here, follower-sentinel-resolver pass,
// via resolveFollowerAwareObject() (flagged for Team A to update Sec21.9's
// own stale prose).
int stub_character_disable_revival(lua_State* L) {
    logCall(L, upLog(L), "character_disable_revival", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kRevivalDisabledBit1c9c, flag ? CharacterState::kRevivalDisabledBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_disable_revival", resolvedName);
    }
    return 0;
}

// vehicle_set_allow_ram_ped (Sec21.10, 0x00a62d30). 1 mandatory string
// (vehicle name); 1 boolean, no nil-gate, default false. Return: none.
// CONFIRMED two-hop hidden-`this` promotion to the vehicle-AI sub-record
// (+0xc68); double-gate (variant 1): bit 0x80 of the SAME +0xc68+5 byte
// (VehicleState::vehicleAiForceFlags) Sec33.1's own vehicle_set_no_chase
// already models.
int stub_vehicle_set_allow_ram_ped(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_allow_ram_ped", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        v.vehicleAiForceFlags.setBits(VehicleState::kAllowRamPedBit, flag ? VehicleState::kAllowRamPedBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_set_allow_ram_ped", name);
    }
    return 0;
}

// vehicle_anim_finished (Sec21.12, 0x00a63790). 1 mandatory string
// (vehicle name); 1 optional number (raw, NOT rounded), standard nil-gated
// idiom, default 1.0. Return: 1 boolean. CONFIRMED: resolves via
// objectResolves(); requires customAnimActive AND hasAnimStateSubObject;
// pushes true only if threshold <= animProgressFraction. CHOSEN: false on
// any gate failure (no explicit "else" path given by the spec's own text).
int stub_vehicle_anim_finished(lua_State* L) {
    logCall(L, upLog(L), "vehicle_anim_finished", upStateTag(L));
    std::string name = argString(L, 1);
    double threshold = optionalNumberDefault(L, 2, 1.0); // raw, not rounded (CONFIRMED)
    EngineState* st = upState(L);
    bool finished = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.customAnimActive.known() && v.customAnimActive.get() &&
            v.hasAnimStateSubObject.known() && v.hasAnimStateSubObject.get()) {
            finished = threshold <= v.animProgressFraction.get(); // OPEN until set
        }
    }
    lua_pushboolean(L, finished ? 1 : 0);
    return 1;
}

// trigger_get_radius (Sec21.13, 0x00a618b0). 1 mandatory string (trigger
// name). Return: 1 number. CHOSEN: the 3-way shape-kind branch is
// abstracted to ONE final radius value (PlacedObjectState::triggerRadius);
// an unresolved name returns 0.0 (CHOSEN, see that field's own doc comment
// - no real pointer to dereference, matching the spec's own "any other
// kind defaults to 0.0" fallback value).
int stub_trigger_get_radius(lua_State* L) {
    logCall(L, upLog(L), "trigger_get_radius", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    double radius = 0.0; // CHOSEN fallback for an unresolved name, see doc comment above
    if (st->objectResolves().get(name)) { // OPEN until set
        PlacedObjectState& po = st->getOrCreatePlacedObject(name);
        if (po.triggerRadius.known()) {
            radius = po.triggerRadius.get(); // explicit override (tests)
        } else {
            // Spec sync e6691f1 (Sec21.13 FINAL): sphere -> +0x90 radius; cylinder -> +0x94; box -> 0.5 *
            // corner-to-corner diagonal. A trigger whose real shape cannot be decoded stays OPEN.
            if (!st->triggerGeometry(name)) st->registerTriggerGeometryFromZoneData(name);
            const EngineState::TriggerVolumeGeometry* g = st->triggerGeometry(name);
            if (!g || !g->shapeKnown) throw OpenStateError(po.triggerRadius.global(), po.triggerRadius.spec());
            radius = EngineState::triggerRadiusForGeometry(*g);
        }
    }
    lua_pushnumber(L, radius);
    return 1;
}

// spawning_allow_cop_ambient_spawns (Sec21.14, 0x00a5dc90). 1 optional
// boolean, standard nil-gated idiom, default TRUE. Return: none. CONFIRMED:
// bare global write, no resolver, no gate, no record.
int stub_spawning_allow_cop_ambient_spawns(lua_State* L) {
    logCall(L, upLog(L), "spawning_allow_cop_ambient_spawns", upStateTag(L));
    bool flag = optionalBoolDefault(L, 1, true);
    upState(L)->copAmbientSpawnsAllowed() = flag;
    return 0;
}

// set_script_animation_group (Sec21.15, 0x00a5e610). Arg1 (mandatory
// string) character name; arg2 (mandatory string) animation group name.
// Return: none. CONFIRMED: resolves via the #FOLLOWER#-family resolver
// 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08 (follower-
// sentinel-resolver pass); group name converted via a dedicated, test-settable
// lookup (EngineState::animationGroupIdByName, OPEN table per Sec21.15's
// own text). De-duplicating write (only if the value actually changed) to
// CharacterState::scriptAnimationGroupId, propagated to the SAME shared
// CharacterState::equippedItemName's own listener if one is held (tracked
// as a counter). UNCONDITIONALLY (no gate of any kind, CONFIRMED "no gate,
// always" instance): opens and commits an opcode-0x43 record, regardless
// of whether the de-dup check actually changed anything.
int stub_set_script_animation_group(lua_State* L) {
    logCall(L, upLog(L), "set_script_animation_group", upStateTag(L));
    std::string character = argString(L, 1);
    std::string groupName = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedCharacter;
    if (!resolveFollowerAwareObject(st, character, resolvedCharacter)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    auto& table = st->animationGroupIdByName;
    auto it = table.find(groupName);
    int32_t groupId = it != table.end() ? it->second : -1;
    CharacterState& c = st->getOrCreateCharacter(resolvedCharacter);
    if (c.scriptAnimationGroupId != groupId) {
        c.scriptAnimationGroupId = groupId;
        if (!c.equippedItemName.empty()) {
            ++c.animationGroupPropagatedToWeaponCount;
        }
    }
    EngineState::replicateStateChange("set_script_animation_group", resolvedCharacter); // CONFIRMED unconditional
    ++c.scriptAnimationGroupReplicateCount;
    return 0;
}

// set_force_combat_ready_team (Sec21.16, 0x00a5d020). 1 mandatory string,
// NO nil-gate - a team/gang name. Return: none. CONFIRMED: converts via
// resolveTeamId() (0x0094cc60, the SAME shared lookup); writes
// unconditionally to a global, no character/team resolve of any kind
// beyond the id lookup itself.
int stub_set_force_combat_ready_team(lua_State* L) {
    logCall(L, upLog(L), "set_force_combat_ready_team", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    st->forcedCombatReadyTeamId = st->resolveTeamId(name);
    return 0;
}

// player_grenades_disable (Sec21.17, 0x00a596b0). 1 optional boolean,
// standard nil-gated idiom, default false. Return: none. CONFIRMED: the
// exact enable/disable pair for the (out-of-scope) `player_grenades_
// enable` - unconditionally sets grenadesDisableFlagA(); only if the
// argument is true, ALSO sets grenadesDisableFlagB().
int stub_player_grenades_disable(lua_State* L) {
    logCall(L, upLog(L), "player_grenades_disable", upStateTag(L));
    bool extra = optionalBoolDefault(L, 1, false);
    EngineState* st = upState(L);
    st->grenadesDisableFlagA() = true; // CONFIRMED unconditional
    if (extra) st->grenadesDisableFlagB() = true;
    return 0;
}

// on_weapon_equip (Sec21.18, 0x00a57460). 2 mandatory strings: callback
// name, target name/"#PLAYER#". Return: none. CONFIRMED: resolves via
// objectResolves() (the generic/#PLAYER# resolver); registers hook code 7
// at the SAME +0x1f00 sub-record Sec21.1 (on_collision) also uses. NO
// vehicle-fallback path (CONFIRMED, unlike Sec21.1).
int stub_on_weapon_equip(lua_State* L) {
    logCall(L, upLog(L), "on_weapon_equip", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(targetName)) { // OPEN until set
        st->getOrCreateCharacter(targetName).onWeaponEquipCallback = callbackName;
    }
    return 0;
}

// on_door_opened (Sec21.19, 0x00a57ed0). 2 mandatory strings: callback
// name, door/target name. Return: none. CONFIRMED: resolves via
// objectResolves() (the named-placed-object/door resolver); registers hook
// code 4 via the third registration-helper family at the door's own +0x90
// sub-record.
int stub_on_door_opened(lua_State* L) {
    logCall(L, upLog(L), "on_door_opened", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string doorName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(doorName)) { // OPEN until set
        st->getOrCreatePlacedObject(doorName).onDoorOpenedCallback = callbackName;
    }
    return 0;
}

// npc_refire_rate_override (Sec21.20, 0x00a55d20). Arg1 (mandatory
// string) character name; arg2 (optional number, standard nil-gated idiom,
// rounded to 16 bits, default 0xffff). Return: none. CONFIRMED: resolves
// via the #FOLLOWER#-family resolver 0x00a28150, resolveFollowerAwareObject()
// - wired 2026-10-08 (follower-sentinel-resolver pass); bare, ungated,
// non-replicated direct write to +0x1512.
int stub_npc_refire_rate_override(lua_State* L) {
    logCall(L, upLog(L), "npc_refire_rate_override", upStateTag(L));
    std::string name = argString(L, 1);
    uint16_t value = static_cast<uint16_t>(truncateEa2596(optionalNumberDefault(L, 2, 65535.0)) & 0xffffLL);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        st->getOrCreateCharacter(resolvedName).npcRefireRateOverride = value;
    }
    return 0;
}

// npc_hearing_enable (Sec21.21, 0x00a55510). Arg1 (mandatory string)
// character name; arg2 (boolean, no nil-gate, default false). Return:
// none. CONFIRMED: resolves via the #FOLLOWER#-family resolver 0x00a28150,
// resolveFollowerAwareObject() - wired 2026-10-08 (follower-sentinel-
// resolver pass); the LOGICAL NEGATION of the boolean is stored (a
// further confirmed instance of the "negated enable_*" convention) via the
// SAME double-gate (variant 1) template, bit 0x10 ("deaf") of the
// character's own human_ai_data sub-object +9 byte.
int stub_npc_hearing_enable(lua_State* L) {
    logCall(L, upLog(L), "npc_hearing_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0;
    bool deaf = !enable; // CONFIRMED negated storage
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.humanAiDataForceFlagsByte9.setBits(CharacterState::kDeafBit, deaf ? CharacterState::kDeafBit : 0u);
    } else {
        EngineState::replicateStateChange("npc_hearing_enable", resolvedName);
    }
    return 0;
}

// inv_item_equip_slot (Sec21.23, 0x00a507d0). Arg1 (mandatory string) slot
// name OR one of 5 literal "best-weapon" selectors; arg2 (optional string,
// standard nil-gated idiom, default "#PLAYER1#") target character. Return:
// none. CONFIRMED: target resolves via objectResolves(); arg1 converts via
// the SAME slot-name-or-5-literals cascade (resolveInventorySlotId())
// Sec21.4 also uses. The gate/literal at this call site is CONFIRMED
// "effectively always-true" (same shape as Sec21.4) - always direct-
// applies, never the opcode-0x41 record branch. An ordinary slot id reads
// CharacterState::inventorySlotContentsBySlotId; a "best-weapon" selector
// reads a dedicated, test-settable map (no real weapon catalog in scope).
// Compares against equippedItemName (the SAME shared accessor Sec20.16/
// Sec20.17/Sec21.15 all read) - if unchanged, counts a re-equip-skipped
// event instead of re-equipping.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_equip_slot(lua_State* L) {
    logCall(L, upLog(L), "inv_item_equip_slot", upStateTag(L));
    std::string slotArg = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    target = resolvedTarget;
    int32_t code = st->resolveInventorySlotId(slotArg);
    if (code == 0xff) return 0; // CONFIRMED "not found" sentinel
    CharacterState& c = st->getOrCreateCharacter(target);
    std::string newItem;
    if (code >= 0xd && code <= 0x11) { // the 5 CONFIRMED literal "best-*" selectors (0xd..0x11)
        auto& best = st->bestWeaponSelectionByCode();
        newItem = best.known(std::to_string(code)) ? best.get(std::to_string(code)) : ""; // OPEN until set
    } else {
        auto it = c.inventorySlotContentsBySlotId.find(code);
        newItem = it != c.inventorySlotContentsBySlotId.end() ? it->second : ""; // CHOSEN zero-default, no real catalog
    }
    if (caseInsensitiveEquals(newItem, c.equippedItemName)) {
        ++c.reequipSkippedAlreadyEquippedCount;
    } else {
        c.equippedItemName = newItem;
        ++c.equippedCount;
    }
    return 0;
}

// get_char_vehicle_type (Sec21.24, 0x00a4b380). 1 mandatory string
// (occupant name). Return: 1 number (0-5, or sentinel 6 on any failure).
// CONFIRMED: resolves via objectResolves(); reuses CharacterState::
// currentVehicleName (the cached vehicle-association, "trust a known
// association" convention, Sec12.9); reads VehicleState::flyingType
// ((+0xbf4)+0x2c), clamping deterministically.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
//
// RE-WIRED 2026-10-11 (spec-lua-api-behaviour.md Sec21.24 RESOLVED, closing
// the routed m07 mission-blocker): this native's real field is the vehicle's
// static `Vehicle_Type` xtbl class enum at (vehicle+0xbf4)+0x2c - the SAME
// physical field VehicleState::flyingType already models and already
// populates from `_veh.xtbl` data (EngineState::loadZoneObjectsSection(),
// via installVehicleTypeClassTable()/sr3vehicleinfo::ParseAllVehicles -
// see that field's own doc comment, "vehicle flying-type enum
// ((+0xbf4)+0x2c)"). The separate VehicleState::vehicleTypeCode field this
// stub used to read was a distinct, NEVER-wired OPEN placeholder dating
// from when this project had no mechanism joining a zone-placed vehicle
// instance back to its real xtbl row - that mechanism now exists and
// already feeds flyingType for every real zone-placed vehicle (including
// m07's own), so reading flyingType here (not vehicleTypeCode) is what
// actually resolves for real data instead of staying permanently OPEN.
// vehicleTypeCode itself is now dead (no remaining reader) and left
// removed from vehicle_state.h.
int stub_get_char_vehicle_type(lua_State* L) {
    logCall(L, upLog(L), "get_char_vehicle_type", upStateTag(L));
    std::string occupant = argString(L, 1);
    EngineState* st = upState(L);
    int32_t result = 6; // CONFIRMED sentinel
    std::string resolvedOccupant;
    if (resolveFollowerAwareObject(st, occupant, resolvedOccupant)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedOccupant);
        if (c.currentVehicleName.known() && !c.currentVehicleName.get().empty()) {
            VehicleState& v = st->getOrCreateVehicle(c.currentVehicleName.get());
            int32_t code = v.flyingType.get(); // OPEN until set; (+0xbf4)+0x2c, the real xtbl Vehicle_Type class
            result = (code >= 0 && code <= 5) ? code : 6; // CONFIRMED deterministic clamp
        }
    }
    lua_pushnumber(L, static_cast<lua_Number>(result));
    return 1;
}

// drug_effect_clear_override_values (Sec21.25, 0x00a47480). 1 optional
// number, standard nil-gated idiom, rounded, default 3 (a 2-bit action
// mask). Return: none. CONFIRMED: always clears override slot 0's min/max
// to 0.0/0.0 (mask bit 0) and/or opens a broadcast (mask bit 1, opcode 0x40
// subtype 0x16). Reuses EngineState::drugEffectOverrideValueA/B() directly
// - the SAME channel-0 fields drug_effect_set_override_values (Sec14.19)
// already writes.
int stub_drug_effect_clear_override_values(lua_State* L) {
    logCall(L, upLog(L), "drug_effect_clear_override_values", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if (mask & 0x2) {
        EngineState::replicateStateChange("drug_effect_clear_override_values", "");
    }
    if (mask & 0x1) {
        st->drugEffectOverrideValueA() = 0.0;
        st->drugEffectOverrideValueB() = 0.0;
    }
    return 0;
}

// character_is_in_a_driver_seat (Sec21.26, 0x00a454b0). 1 mandatory
// string (occupant name). Return: 1 boolean. ANSWERED 2026-10-15 (spec 21.26
// blocker dlc2_m01, FINAL): in its vehicle (known non-empty currentVehicleName,
// "trust a known cached association") AND seat 0's claim / pending / seated
// handle equals the character.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_is_in_a_driver_seat(lua_State* L) {
    logCall(L, upLog(L), "character_is_in_a_driver_seat", upStateTag(L));
    std::string occupant = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    std::string resolvedOccupant;
    if (resolveFollowerAwareObject(st, occupant, resolvedOccupant)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedOccupant);
        // Spec 21.26 blocker dlc2_m01 (FINAL): the character is attached to this vehicle (its cached vehicle
        // association) AND seat 0's claim, pending or seated handle equals it. The three accessors are three
        // fields of the seat-0 record, not three seat roles; the third argument 0 is the seat index.
        if (c.currentVehicleName.known() && !c.currentVehicleName.get().empty() &&
            st->hasVehicle(c.currentVehicleName.get())) {
            result = st->getOrCreateVehicle(c.currentVehicleName.get()).seat0HoldsCharacter(resolvedOccupant);
        }
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// change_mission_enemy_team (Sec21.27, 0x00a40fd0). 1 mandatory string
// (team/gang name). Return: none. CONFIRMED: converts via resolveTeamId()
// (the SAME shared lookup Sec21.16 also uses); only if the resulting code
// is 1, 2, or 3: checks missionContext().activeAndRunning (the SAME shared
// gate Sec20.13 also reads) AND a SEPARATE one-shot gate (missionOneShotGateA0,
// OPEN); writes a DIFFERENT field from mission_set_enemy_team's own
// missionEnemyTeamId (CONFIRMED distinct, per Sec21.27's own text) and
// notifies (counted).
int stub_change_mission_enemy_team(lua_State* L) {
    logCall(L, upLog(L), "change_mission_enemy_team", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    int code = st->resolveTeamId(name);
    if (code != 1 && code != 2 && code != 3) return 0; // CONFIRMED gate
    if (!st->missionContext().activeAndRunning.get()) return 0; // OPEN until set
    if (st->missionOneShotGateA0.get()) return 0; // OPEN until set - CONFIRMED only proceeds if this reads 0/false (gate TRUE = blocked)
    st->missionEnemyTeamCodeA5 = code;
    ++st->changeMissionEnemyTeamNotifyCount;
    return 0;
}

// action_stop_custom (Sec21.28, 0x00a3dda0). 1 mandatory string (target
// name). Return: none. CONFIRMED: resolves via objectResolves(); calls an
// OPEN internal helper twice (counted only); if the class-descriptor
// capability bit (+0xa bit 0x2) is set, counts an additional internal
// dispatch (0x009e3200).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_action_stop_custom(lua_State* L) {
    logCall(L, upLog(L), "action_stop_custom", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    ++c.actionStopCustomCallCount; // the two 0x00487e80 calls, internals OPEN
    if (c.actionStopCustomCapabilityBit2.known() && c.actionStopCustomCapabilityBit2.get()) {
        ++c.actionStopCustomInternalDispatchCount; // the capability-gated 0x009e3200 dispatch
    }
    return 0;
}

// vehicle_skydiving_move_to_do (Sec21.29, 0x00a66410). Arg1 (mandatory
// string) vehicle name; arg2 (mandatory string) destination/reference
// name; arg3/arg4 (optional numbers, standard nil-gated idiom, default
// -1.0 each - the byte-confirmed value of 0x012a2d54). Return: none.
// CONFIRMED: both names resolve via objectResolves(); the dispatch target
// (0x00ac0f60) is OPEN, not independently decompiled - records the raw
// CONFIRMED request only.
int stub_vehicle_skydiving_move_to_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_skydiving_move_to_do", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    std::string destName = argString(L, 2);
    double arg3 = optionalNumberDefault(L, 3, -1.0);
    double arg4 = optionalNumberDefault(L, 4, -1.0);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName) || !st->objectResolves().get(destName)) return 0; // OPEN until set, each
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    v.lastSkydivingMoveToDestination = destName;
    v.lastSkydivingMoveToArg3 = arg3;
    v.lastSkydivingMoveToArg4 = arg4;
    ++v.skydivingMoveToDispatchCount;
    return 0;
}

// vehicle_prevent_vehicle_collision_damage (Sec21.30, 0x00a62ca0). 1
// mandatory string (vehicle name); 1 boolean, no nil-gate, default false.
// Return: none. CONFIRMED: resolves via objectResolves(); dispatched on the
// RAW vehicle handle (not the +0xc68 sub-record Sec21.10 uses - a genuine,
// CONFIRMED difference); double-gate (variant 1): bit 0x8 of the SAME
// +0x1d7a byte Sec3.7/Sec20.1/Sec46.1 already model, tag "vehicle" (not
// "vehicle_ai").
int stub_vehicle_prevent_vehicle_collision_damage(lua_State* L) {
    logCall(L, upLog(L), "vehicle_prevent_vehicle_collision_damage", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        v.forceFlags1d7a.setBits(VehicleState::kImmuneToVehicleCollisionDamageBit1d7a,
                                  flag ? VehicleState::kImmuneToVehicleCollisionDamageBit1d7a : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_prevent_vehicle_collision_damage", name);
    }
    return 0;
}

} // namespace stubs_b16
} // namespace sr3luahost
