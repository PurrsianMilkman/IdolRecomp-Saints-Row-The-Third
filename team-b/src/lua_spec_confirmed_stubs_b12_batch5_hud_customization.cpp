// lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp
//
// Wave 1, Batch 5 of the 7-agent parallel fan-out implementing cleared-but-
// unregistered Lua natives (see results/lua_natives_sec10_28_fanout_scoping_
// 20261008.md Sec3 "Batch 5" for the full scoping/priority rationale this
// batch was dispatched from). NEW file per the refactor's own reviewer
// guidance (do NOT retrofit into b01-b07) - follows the exact same
// namesB0N()/registerB0N() pair pattern as the existing 7 files (see
// lua_spec_confirmed_stubs_registry.cpp's own top comment for the full
// split rationale and shared-registration mechanism).
//
// Scope: HUD-slot/customization/pause-menu cluster, spec-lua-api-
// behaviour.md Sec15 (ranks 251-350, 25 names) + Sec16 (ranks 251-350
// UI-cluster, 16 names) - 41 natives total. Every behavior claim below
// cites its exact Sec15.N/Sec16.N subsection; a genuinely OPEN detail uses
// OpenStateError (via an EngineState OpenValue/OpenValueMap read) rather
// than inventing a value, per this project's standing clean-room
// discipline. See each new engine_state.h field/method's own doc comment
// (added alongside this file, in CharacterState/VehicleState/the new
// HudDisplayStatePool/QteSlotEntry/PcrCustomizationState structs, and
// EngineState's own public section) for the full per-field reasoning this
// file's stub bodies rest on - not re-derived here.
//
// CROSS-BATCH COORDINATION (informational, per the scoping report's own
// §4): the HUD display-state slot family (hud_display_create_state/
// remove_state/commit_state, qte_cleanup) is fully co-located in this
// batch - the shared infra (HudDisplayStatePool) is built once here. The
// customization family (pcr_change_hair/pcr_change_composite/
// pcr_get_player_preset/pcr_set_identity) shares its underlying mechanism
// with Batch 4's customization_outfit_wear/customization_item_wear/
// customization_item_revert - see PcrCustomizationState's own doc comment
// (engine_state.h) for this batch's own, independently-written model and
// the explicit note that reconciling it with Batch 4's own model is left
// for a future PCU/PCR-family pass, not attempted here.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cmath>

namespace sr3luahost {
namespace {

// =======================================================================
// Section 15 batch (spec-lua-api-behaviour.md Sec15, "ranks 251-350
// tranche, part B", 25 of its 28 named natives - sidewalk_disable_nodes/
// teleport_check_done/mission_end_silently are NOT in this batch, per the
// scoping report: the first is already registered in b06, the other two
// were not assigned to Batch 5). See each function's own doc comment for
// its exact Sec15.N citation.
// =======================================================================

// character_set_damage_multiplier (Sec15.1, 0x00a42320) - 12 calls.
// CONFIRMED: arg 1 mandatory string (character/player name), resolved via
// the generic/#PLAYER# chain 0x00a281a0 (this project's flat
// objectResolves() stand-in, Sec29). Arg 2 mandatory number, read
// UNCONDITIONALLY (no nil guard at all) as a real float (not the
// int-truncate pair this cluster otherwise uses). Body: a hidden-this
// thiscall helper (0x0094a960, raw-disassembly-CONFIRMED hidden-this
// argument) sets a per-character damage-multiplier field; 0x0094a960's
// own internal write was not independently decompiled (Sec15.1's own OPEN
// item) - this project's own field stands in directly. CHOSEN default 1.0
// (no in-scope reader). Return: none.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_damage_multiplier(lua_State* L) {
    logCall(L, upLog(L), "character_set_damage_multiplier", upStateTag(L));
    std::string name = argString(L, 1);
    double mult = lua_tonumber(L, 2); // CONFIRMED: unconditional read, no nil-gate
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        st->getOrCreateCharacter(resolvedName).damageMultiplier = mult;
    }
    return 0;
}

// vehicle_anim_start (Sec15.2, 0x00a64850) - 11 calls. CONFIRMED: arg 1
// mandatory string (vehicle name, resolved via the vehicle resolver
// 0x00a281e0 - flat objectResolves() stand-in). Args 2/3 mandatory
// strings (track names, each independently converted to a numeric id via
// a distinct name->id lookup, 0x004bf810, NOT part of the object-handle-
// resolution family - no fixed id table in this project's scope). Arg 4
// optional number, CHOSEN placeholder default (Sec15.2's own undecoded
// global float literal, 0x01117a4c - no in-scope reader of the resulting
// blend value). Body: records the raw request only; the 3 tail
// dispatchers' own +0xbd0/+0xa78 anim-controller gate has no in-scope
// reader either way, so is not modeled - the function unconditionally
// opens a record-and-replicate block regardless of that gate's outcome
// (CONFIRMED, Sec15.2's own text). Return: none.
int stub_vehicle_anim_start(lua_State* L) {
    logCall(L, upLog(L), "vehicle_anim_start", upStateTag(L));
    std::string name = argString(L, 1);
    std::string trackArg2 = argString(L, 2);
    std::string trackArg3 = argString(L, 3);
    double blend = optionalNumberDefault(L, 4, 0.0); // CHOSEN placeholder - OPEN undecoded default literal, Sec15.2
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        v.animStartTrackArg2 = trackArg2;
        v.animStartTrackArg3 = trackArg3;
        v.animStartBlend = blend;
        ++v.animStartCount;
        EngineState::replicateStateChange("vehicle_anim_start", name); // CONFIRMED unconditional, Sec15.2
    }
    return 0;
}

// set_always_cower_flag (Sec15.4, 0x00a5e070) - 11 calls. CONFIRMED: arg 1
// mandatory string, resolved via the generic/#PLAYER#-then-#FOLLOWER#
// chain 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08
// (follower-sentinel-resolver pass). Arg 2 optional bool,
// default true. Body: sets/clears the per-character +0x2b0 sub-object's
// cower flag (CHOSEN plain field, no in-scope reader); when turning it ON
// specifically, queues AI event 0x26 on the character (CORRECTED per
// Sec44.6, not a VFX call as an earlier pass believed). Return: none.
int stub_set_always_cower_flag(lua_State* L) {
    logCall(L, upLog(L), "set_always_cower_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool cower = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        c.alwaysCowerFlag = cower;
        if (cower) ++c.aiEvent26QueuedCount; // CORRECTED per Sec44.6
    }
    return 0;
}

// refraction_situation_clear (Sec15.5, 0x00a5bd20) - 11 calls. CONFIRMED:
// 1 optional numeric/flag value, default 3 (both bits set), read via the
// same lua_tonumber+truncateEa2596 pair this cluster's siblings use
// (Sec4.1). Body: bit 0x1 clears the global refraction-override slot 0
// locally; bit 0x2 opens an UNCONDITIONAL (not host-gated - Sec15.5's own
// text) record-and-replicate block. Return: none.
int stub_refraction_situation_clear(lua_State* L) {
    logCall(L, upLog(L), "refraction_situation_clear", upStateTag(L));
    int64_t flags = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    upState(L)->recordRefractionSituationClear(flags);
    return 0;
}

// qte_cleanup (Sec15.6, 0x00a5b1d0) - 11 calls. CONFIRMED: no arguments
// used (lua_gettop called, result discarded). Body: iterates a fixed
// global QTE-slot list this project does not independently size (OPEN,
// Sec15.6's own "many parallel 0x014b01xx QTE-context arrays... not
// mapped field-by-field"); for each currently-tracked entry, releases any
// linked HUD-display-state slot via the SAME shared slot-release
// mechanism hud_display_remove_state's own dispatch reaches (CONFIRMED
// cross-link, Sec15.6's own text) - see QteSlotEntry's own doc comment
// (engine_state.h) for the "mechanism confirmed, no in-scope producer"
// reasoning. Return: none.
int stub_qte_cleanup(lua_State* L) {
    logCall(L, upLog(L), "qte_cleanup", upStateTag(L));
    (void)lua_gettop(L); // CONFIRMED: called, result never read
    upState(L)->qteCleanup();
    return 0;
}

// persona_clear_global_situation_override (Sec15.7, 0x00a59580) - 11
// calls. CONFIRMED: no arguments used. Body: unconditionally resets the
// global persona-situation-override value to 0; a record-and-replicate
// block is opened only when hosting a coop session (CONFIRMED false in
// single player, same precedent as audio_conversation_end Sec8.2) -
// structurally reproduced via coopLocalIsHost(), never fires in this
// project's scope. Return: none.
int stub_persona_clear_global_situation_override(lua_State* L) {
    logCall(L, upLog(L), "persona_clear_global_situation_override", upStateTag(L));
    (void)lua_gettop(L);
    upState(L)->personaClearGlobalSituationOverride();
    return 0;
}

// on_weapon_pickup (Sec15.8, 0x00a574c0) - 11 calls. CONFIRMED: 2
// mandatory strings - arg 1 an opaque callback name (never resolved), arg
// 2 a character/player name resolved via the generic/#PLAYER# resolver
// 0x00a280c0 (flat objectResolves() stand-in). Body: registers the
// callback into the resolved character's hook slot 6 (the generic
// +0x1f00-array path - CONFIRMED does NOT collide with on_take_damage's
// DIFFERENT-base slot 6, Sec15.8's own cross-reference), same single-
// dedicated-field-per-hook-kind convention as onTakeDamageCallback.
// Empty-string clears the slot (same shared setter convention). Return: none.
int stub_on_weapon_pickup(lua_State* L) {
    logCall(L, upLog(L), "on_weapon_pickup", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(targetName)) { // OPEN until set
        st->getOrCreateCharacter(targetName).onWeaponPickupCallback = callbackName;
    }
    return 0;
}

// mission_unlock (Sec15.9, 0x00a52760) - 11 calls. CONFIRMED: 1 mandatory
// string (mission name), resolved via the newly-identified mission
// resolver 0x005f4c30 (structurally identical to the vehicle resolver -
// this project's flat objectResolves() stand-in, Sec29). Body: a generic
// mission-state setter is called with a fixed literal 1 ("unlocked", HIGH
// CONFIDENCE by name-match). Return: none.
int stub_mission_unlock(lua_State* L) {
    logCall(L, upLog(L), "mission_unlock", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->recordMissionUnlock(name);
    }
    return 0;
}

// mission_set_completed (Sec15.10, 0x00a539e0) - 11 calls. CONFIRMED: 1
// mandatory string (mission name), resolved via the SAME mission resolver
// as mission_unlock above (Sec15.9's cross-reference). Body: always fires
// a "mission list changed" notify (no in-scope observable effect) and,
// idempotently, marks the mission completed (skips re-marking an already-
// completed mission); unconditionally (resolved or not) opens a record-
// and-replicate block afterward. Return: none.
int stub_mission_set_completed(lua_State* L) {
    logCall(L, upLog(L), "mission_set_completed", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name); // OPEN until set
    st->recordMissionSetCompleted(name, resolved);
    return 0;
}

// message (Sec15.11, 0x00a52860) - 11 calls. CONFIRMED: 4 args. Arg 1
// mandatory string. Arg 2 optional number, CHOSEN placeholder default
// (Sec15.11's own 2 chained undecoded global float literals - no in-scope
// reader of the duration value). Arg 3 optional bool, default true
// (selects which of 2 dispatch paths runs - neither has any further in-
// scope observable effect). Arg 4 optional number/flag, default 1 (read,
// not modeled further). Return: 1 number - a message/popup-instance id,
// ALWAYS pushed (raw-disassembly-CONFIRMED single return value despite
// the decompiler's void-looking signature). No real id scheme is given -
// reuses this project's own established monotonically-increasing-handle
// convention (nextAudioVoiceHandle()/nextVisualEffectHandle()'s own
// precedent), CONFIRMED a SEPARATE mechanism from the hud_display_* slot-
// queue (Sec15.11's own text).
int stub_message(lua_State* L) {
    logCall(L, upLog(L), "message", upStateTag(L));
    (void)argString(L, 1);
    (void)optionalNumberDefault(L, 2, 0.0); // CHOSEN placeholder - OPEN chained default literals, Sec15.11
    (void)optionalBoolDefault(L, 3, true);
    (void)truncateEa2596(optionalNumberDefault(L, 4, 1.0));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->nextMessageHandle()));
    return 1;
}

// hud_display_remove_state (Sec15.12, 0x00a4f230) - 11 calls. CONFIRMED:
// arg 1 mandatory number (slot id, read via truncateEa2596). Arg 2
// optional bool, default false. Body: see HudDisplayStatePool's own doc
// comment (engine_state.h) for the full shared-infra reasoning. Return: none.
int stub_hud_display_remove_state(lua_State* L) {
    logCall(L, upLog(L), "hud_display_remove_state", upStateTag(L));
    int slotId = static_cast<int>(truncateEa2596(lua_tonumber(L, 1)));
    bool arg2 = optionalBoolDefault(L, 2, false);
    upState(L)->hudDisplayRemoveState(slotId, arg2);
    return 0;
}

// follower_remain_in_car (Sec15.13, 0x00a49920) - 11 calls. CONFIRMED: arg
// 1 mandatory string, resolved via the generic/#PLAYER#-then-#FOLLOWER#
// chain 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08
// (follower-sentinel-resolver pass). Arg 2 mandatory bool, read
// UNCONDITIONALLY (no nil-gate). Body: sets/clears bit 0x100 of byte
// +0x1c9c ("cannot_exit_vehicles" - CONFIRMED by the setter's own literal
// debug-sync strings "human"/"m_force_flagscannot_exit_vehicles"), gated
// by the SAME forceFlagGatePasses double-gate independentFollower/
// dontAttackMeOnSight etc. already reuse (CONFIRMED true in single player,
// Sec3.7/Sec6.22's cross-reference covers this gate pair generically).
// Return: none.
int stub_follower_remain_in_car(lua_State* L) {
    logCall(L, upLog(L), "follower_remain_in_car", upStateTag(L));
    std::string name = argString(L, 1);
    bool stay = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate at all
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec6.22
        c.flags1c9c.setBits(CharacterState::kCannotExitVehiclesBit1c9c,
                             stay ? CharacterState::kCannotExitVehiclesBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("follower_remain_in_car", resolvedName);
    }
    return 0;
}

// character_is_ragdolled (Sec15.14, 0x00a418c0) - 11 calls. CONFIRMED: 1
// mandatory string, resolved via 0x00a281a0. Return: 1 boolean ALWAYS
// (both paths explicitly push). On success, pushes the exclusion test on
// the +0xcd2 state-enum byte (!=0 && !=6) - OPEN which exact value(s)
// mean "ragdolled" beyond this exclusion test (Sec15.14's own flagged item).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_is_ragdolled(lua_State* L) {
    logCall(L, upLog(L), "character_is_ragdolled", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    int32_t s = st->getOrCreateCharacter(resolvedName).ragdollStateEnumCd2.get(); // OPEN until set
    lua_pushboolean(L, (s != 0 && s != 6) ? 1 : 0);
    return 1;
}

// vehicle_skydiving_start (Sec15.15, 0x00a66510) - 10 calls. CONFIRMED:
// arg 1 mandatory string (vehicle, resolved via 0x00a281e0). Arg 2
// optional string (secondary anchor-object name, resolved via 0x005982e0
// - same flat objectResolves() stand-in). Body (literal branch structure,
// flagged as a confirmed oddity, not resolved further - same house
// convention as vehicle_chase's own JUDGMENT CALL note): "already in the
// target state" (the local bit already set) takes the LOCAL-apply
// branch; otherwise (the common first-call case) takes the network-
// replicate branch. Neither branch has an in-scope reader beyond the bit
// itself, which only a test can pre-set. Return: none.
int stub_vehicle_skydiving_start(lua_State* L) {
    logCall(L, upLog(L), "vehicle_skydiving_start", upStateTag(L));
    std::string name = argString(L, 1);
    bool hasArg2 = lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL;
    std::string anchorArg = hasArg2 ? argString(L, 2) : std::string();
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    std::string anchorSource;
    if (hasArg2 && st->objectResolves().get(anchorArg)) { // OPEN until set
        anchorSource = anchorArg;
    }
    if (v.skydiveActiveFlag1_16c4) {
        // "already in target state" branch (CONFIRMED literal structure, Sec15.15) - local apply.
        v.skydiveAnchorSourceName = anchorSource;
        v.skydiveActiveFlag1_16c4 = true; // re-set, no-op (already true)
        ++v.skydiveStartLocalApplyCount;
    } else {
        EngineState::replicateStateChange("vehicle_skydiving_start", name);
        ++v.skydiveStartReplicateCount;
    }
    return 0;
}

// vehicle_is_tank (Sec15.16, 0x00a660f0) - 10 calls. CONFIRMED: 1
// mandatory string, resolved via 0x0062a190. Return: 1 boolean, ALWAYS.
// On success, the vtable+0x70 secondary handle's own nested field == 14
// predicate (HIGH CONFIDENCE "Tank" by name-match) - OpenValue stands in
// for the whole resolved predicate (this project models no nested-handle
// chain, same top-of-file "no real handle-resolution mechanism" note).
int stub_vehicle_is_tank(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_tank", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, st->getOrCreateVehicle(name).isTankClassPredicate.get() ? 1 : 0); // OPEN until set
    return 1;
}

// skydive_set_player_dive_fall_speed (Sec15.18, 0x00a5db40) - 10 calls.
// CONFIRMED: 1 optional number, CHOSEN placeholder default (Sec15.18's
// own flagged address-ambiguity between 2 possible default-literal
// addresses - genuinely OPEN which is right - no in-scope reader of the
// value either way). NOTE a bare stack-depth check, not the fuller nil-
// test idiom (Sec15.18's own flagged minor variant) - reproduced here via
// a plain lua_gettop check rather than optionalNumberDefault's type-gated
// idiom. Body: writes the value into the global player-skydive-fall-
// speed; the record-and-replicate block is CONFIRMED host-only (false in
// single player, same precedent as persona_clear_global_situation_
// override). Return: none.
int stub_skydive_set_player_dive_fall_speed(lua_State* L) {
    logCall(L, upLog(L), "skydive_set_player_dive_fall_speed", upStateTag(L));
    double value = (lua_gettop(L) >= 1) ? lua_tonumber(L, 1) : 0.0; // CHOSEN placeholder default, OPEN per Sec15.18; bare stack-depth check
    upState(L)->setPlayerSkydiveFallSpeed(value);
    return 0;
}

// set_unenterable_for_player (Sec15.19, 0x00a5d9a0) - 10 calls. CONFIRMED:
// arg 1 mandatory string (vehicle, resolved via 0x00a281e0). Arg 2
// optional bool, default true. Body: primary bit 0x80 at +0x1d7b
// (CONFIRMED by the setter's own literal debug-label strings "vehicle"/
// "m_force_flagsunenterable_for_player"), gated by the SAME
// forceFlagGatePasses double-gate (CONFIRMED true in single player);
// unconditionally (regardless of gate) mirrors the boolean onto a
// secondary sub-object's own bit 0x4 (this project's flat field stand-in,
// same convention as unjackableSecondarySubObjectFlag, Sec9.13).
// Return: none.
int stub_set_unenterable_for_player(lua_State* L) {
    logCall(L, upLog(L), "set_unenterable_for_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool unenterable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.unenterableForPlayerSecondarySubObjectFlag = unenterable; // unconditional, every call (CONFIRMED)
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.forceFlags1d7b.setBits(VehicleState::kUnenterableForPlayerBit1d7b,
                                  unenterable ? VehicleState::kUnenterableForPlayerBit1d7b : 0u);
    } else {
        EngineState::replicateStateChange("set_unenterable_for_player", name);
    }
    return 0;
}

// set_player_undercover_team (Sec15.20, 0x00a5d470) - 10 calls. CONFIRMED:
// 2 mandatory strings - arg 1 player/character name (resolved via
// 0x00a280c0), arg 2 a team name. GENUINE CROSS-FUNCTION FINDING
// (Sec15.20's own text): the team-name->code hash/lookup (0x0094cc60,
// hashed via 0x00dab330, table 0x02623b48, fallback 9) is the EXACT SAME
// helper notoriety_force_no_spawn (Sec3.6) already models as
// lookupNotorietyTrackCode() - reused directly here, not re-derived.
// Body: writes the resolved byte into +0x27f0, gated by the SAME
// forceFlagGatePasses double-gate (reused by direct analogy, same
// precedent as follower_remain_in_car above). Return: none.
int stub_set_player_undercover_team(lua_State* L) {
    logCall(L, upLog(L), "set_player_undercover_team", upStateTag(L));
    std::string name = argString(L, 1);
    std::string teamName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    int32_t code = st->lookupNotorietyTrackCode(teamName); // SAME helper as notoriety_force_no_spawn, Sec3.6/Sec15.20
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec6.22
        c.undercoverTeamCode = static_cast<uint8_t>(code);
    } else {
        EngineState::replicateStateChange("set_player_undercover_team", name);
    }
    return 0;
}

// player_actions_enable (Sec15.21, 0x00a595d0) - 10 calls. CONFIRMED: 1
// mandatory string, resolved via 0x00a280c0. No toggle argument - this
// function is unconditionally "enable" (matching its own name exactly).
// CHOSEN simplification (see CharacterState::playerActionsEnableCount's
// own doc comment, engine_state.h): always takes the documented not-yet-
// enabled/replicate branch (the common real-world case), since neither
// real branch has an in-scope reader. Return: none.
int stub_player_actions_enable(lua_State* L) {
    logCall(L, upLog(L), "player_actions_enable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    EngineState::replicateStateChange("player_actions_enable", name); // CHOSEN default branch, Sec15.21
    ++c.playerActionsEnableCount;
    return 0;
}

// npc_leash_to_object (Sec15.22, 0x00a56460) - 10 calls. CONFIRMED: arg 1
// mandatory string (NPC, resolved via 0x00a28150, resolveFollowerAwareObject()
// - wired 2026-10-08, follower-sentinel-resolver pass). Arg 2 mandatory string
// (leash-anchor/target name, tried against up to 3 real resolvers in the
// original - this project's flat objectResolves() stand-in covers all 3,
// Sec29). Arg 3 mandatory number (radius, unconditional read). Arg 4
// optional bool, default false. Early-return path: a negative radius
// dispatches to a "clear leash" call instead (CONFIRMED, no further in-
// scope observable effect beyond the fact that it ran). No real position
// data (same project-wide gap attentionSourceObjectName already notes) -
// only the resolved anchor name + raw radius/arg4 are recorded. Return: none.
int stub_npc_leash_to_object(lua_State* L) {
    logCall(L, upLog(L), "npc_leash_to_object", upStateTag(L));
    std::string npcName = argString(L, 1);
    std::string anchorName = argString(L, 2);
    double radius = lua_tonumber(L, 3); // CONFIRMED: unconditional read, no nil-gate
    bool arg4 = optionalBoolDefault(L, 4, false);
    EngineState* st = upState(L);
    std::string resolvedNpcName;
    if (!resolveFollowerAwareObject(st, npcName, resolvedNpcName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedNpcName);
    if (radius < 0.0) {
        c.npcLeashAnchorName.clear();
        ++c.npcLeashClearCount;
        return 0;
    }
    if (st->objectResolves().get(anchorName)) { // OPEN until set - flat stand-in for all 3 real resolvers
        c.npcLeashAnchorName = anchorName;
        c.npcLeashRadius = radius;
        c.npcLeashArg4 = arg4;
        ++c.npcLeashSetCount;
    }
    return 0;
}

// hud_display_create_state (Sec15.24, 0x00a4f0c0) - 10 calls. CONFIRMED:
// no arguments. Return: 1 number - the allocated slot id, or -1 on
// exhaustion (raw-disassembly-confirmed single return value). See
// HudDisplayStatePool's own doc comment for the shared-infra reasoning.
int stub_hud_display_create_state(lua_State* L) {
    logCall(L, upLog(L), "hud_display_create_state", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->hudDisplayCreateState()));
    return 1;
}

// hud_display_commit_state (Sec15.25, 0x00a4ef10) - 10 calls. CONFIRMED:
// arg 1 mandatory number (slot id). Arg 2 optional bool, default false.
// Return: none.
int stub_hud_display_commit_state(lua_State* L) {
    logCall(L, upLog(L), "hud_display_commit_state", upStateTag(L));
    int slotId = static_cast<int>(truncateEa2596(lua_tonumber(L, 1)));
    bool arg2 = optionalBoolDefault(L, 2, false);
    upState(L)->hudDisplayCommitState(slotId, arg2);
    return 0;
}

// game_choice_set_state (Sec15.26, 0x00a4ac60) - 10 calls. CONFIRMED: 2
// mandatory args, both read unconditionally. Arg 1 number (choice index,
// kept as a byte via the flag/number-extractor pair). Arg 2 boolean.
// Return: none.
int stub_game_choice_set_state(lua_State* L) {
    logCall(L, upLog(L), "game_choice_set_state", upStateTag(L));
    uint8_t index = static_cast<uint8_t>(truncateEa2596(lua_tonumber(L, 1)) & 0xFF);
    bool state = lua_toboolean(L, 2) != 0;
    upState(L)->setGameChoiceState(index, state);
    return 0;
}

// dlc_achievement_counter_set (Sec15.27, 0x00a475c0) - 10 calls.
// CONFIRMED: 2 mandatory numbers, both read unconditionally via the
// flag/number-extractor pair. Arg 1 counter index, arg 2 counter value.
// HARDENING POLICY: CHOSEN (hardened divergence) - see EngineState::
// setDlcAchievementCounter's own doc comment (engine_state.h) for the
// full memory-safety reasoning (the real `< 3` check does not exclude a
// negative index, a genuine OOB-write bug in the original binary - this
// project refuses the resulting out-of-bounds write rather than
// replicating it). Return: none.
int stub_dlc_achievement_counter_set(lua_State* L) {
    logCall(L, upLog(L), "dlc_achievement_counter_set", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    int32_t value = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 2)));
    upState(L)->setDlcAchievementCounter(index, value);
    return 0;
}

// character_parachute_detach (Sec15.28, 0x00a41cc0) - 10 calls.
// CONFIRMED: 1 mandatory string, resolved via 0x00a281a0. CHOSEN
// simplification (see CharacterState::parachuteDetachCount's own doc
// comment, engine_state.h): always takes the documented replicate-path
// branch (the common real-world case - a character whose parachute is
// actually deployed), since neither real branch has an in-scope reader.
// A trivial inert stub call (0x00d21f80, same shared pattern as Sec10.9)
// always runs afterward with no effect - not modeled. Return: none.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_parachute_detach(lua_State* L) {
    logCall(L, upLog(L), "character_parachute_detach", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    EngineState::replicateStateChange("character_parachute_detach", resolvedName); // CHOSEN default branch, Sec15.28
    ++st->getOrCreateCharacter(resolvedName).parachuteDetachCount;
    return 0;
}

// =======================================================================
// Section 16 batch (spec-lua-api-behaviour.md Sec16, "ranks 251-350
// tranche, part D", all 16 of its named UI-cluster natives). See each
// function's own doc comment for its exact Sec16.N citation.
// =======================================================================

// game_get_language (Sec16.1, 0x00841c20) - 113-name loop registrar, 12
// calls/11 scripts. CONFIRMED: no arguments. Return: 1 string. See
// EngineState::languageString()'s own doc comment for the floor-not-full-
// list table and the OPEN-past-entry-12 refusal.
int stub_game_get_language(lua_State* L) {
    logCall(L, upLog(L), "game_get_language", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushstring(L, upState(L)->languageString().c_str());
    return 1;
}

// game_award_respect (Sec16.2, 0x00843bf0) - 113-name loop registrar, 13
// calls/8 scripts. CONFIRMED: 1 mandatory number (count) + 3 optional
// trailing (2 numbers default -1, 1 bool default false). See
// EngineState::respectMultiplier()'s own doc comment for the OPEN-
// multiplier-gated return value reasoning (the real applied-amount
// formula depends on an unspecified global AND 2 further disassembly-
// confirmed anomalies, Sec16.2's own flagged OPEN items - collapsed to
// identity past the multiplier, a stated simplification). Return: 1 number.
int stub_game_award_respect(lua_State* L) {
    logCall(L, upLog(L), "game_award_respect", upStateTag(L));
    double count = lua_tonumber(L, 1); // CONFIRMED mandatory, no nil-gate
    bool hasTarget = lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL;
    (void)optionalNumberDefault(L, 2, -1.0);
    (void)optionalNumberDefault(L, 3, -1.0); // selector - read, consumed for real call shape only (Sec16.2's own flagged anomaly)
    bool notify = optionalBoolDefault(L, 4, false);
    EngineState* st = upState(L);
    double multiplier = st->respectMultiplier().get(); // OPEN until set, Sec16.2
    // 0x00dad900's own round-to-int shape, CONFIRMED; 0x009da4e0/0x009e2440's
    // own further OPEN effect collapsed to identity (CHOSEN simplification,
    // Sec16.2's own 2 flagged anomalies - neither independently re-derived).
    double amount = std::round(count * multiplier);
    st->recordRespectGrant(hasTarget, notify);
    lua_pushnumber(L, amount);
    return 1;
}

// tutorial_set_hud_help_closing (Sec16.3, 0x008429f0) - 113-name loop
// registrar, 8 calls/2 scripts. CONFIRMED: 1 mandatory boolean, direct
// unconditional write to global 0x0151d5a4. Return: none.
int stub_tutorial_set_hud_help_closing(lua_State* L) {
    logCall(L, upLog(L), "tutorial_set_hud_help_closing", upStateTag(L));
    bool closing = lua_toboolean(L, 1) != 0;
    upState(L)->tutorialHudHelpClosing() = closing;
    return 0;
}

// completion_load_lut (Sec16.4, 0x007bdbd0) - 10 calls/5 scripts.
// CONFIRMED: 1 mandatory string (a LUT name, or the literal "none" -
// matched as a case-insensitive 4-char PREFIX, not an exact match). See
// EngineState::completionLoadLut()'s own doc comment for the fresh-handle
// stand-in reasoning (audio_conversation_load_direct's own precedent,
// Sec8.1) on the resolve-a-named-LUT path. Return: none.
int stub_completion_load_lut(lua_State* L) {
    logCall(L, upLog(L), "completion_load_lut", upStateTag(L));
    std::string name = argString(L, 1);
    upState(L)->completionLoadLut(name);
    return 0;
}

// completion_cutscene_was_active (Sec16.5, 0x007bdce0) - 12 calls/4
// scripts. CONFIRMED: no arguments. Return: 1 boolean (pure read of
// global 0x02282283 - this project's own real writer is out of scope,
// Sec16.5's own text; a test sets it directly).
int stub_completion_cutscene_was_active(lua_State* L) {
    logCall(L, upLog(L), "completion_cutscene_was_active", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, upState(L)->completionCutsceneWasActive() ? 1 : 0);
    return 1;
}

// pcr_change_hair (Sec16.6, 0x008375c0, force-disassembled) - 12
// calls/4 scripts. CONFIRMED: 4 mandatory numbers, no nil/type gate on
// any of them. Body: arg1 bounds-checked against 3, mapped through the
// literal {1,2,3} table (0x01300cf4) to a category id; arg2 negative ->
// clear that category's style; arg2 >= 0 -> resolve+apply a named style
// via arg3/arg4 (CAUTION, Sec16.6: the style-count global reads 0 in this
// static image, "almost certainly runtime-populated... not evidence the
// apply path is unreachable" - this project always treats the apply path
// as succeeding since nothing in scope reads the stored styleId back
// either way, a CHOSEN simplification, not a claim about the real
// static-zero table in isolation). CHOSEN: the category id is used
// directly as the shared 63-slot list's own slot index (no real per-
// category slot assignment is given beyond the id itself - see
// PcrCustomizationState's own doc comment, engine_state.h). Return: none.
int stub_pcr_change_hair(lua_State* L) {
    logCall(L, upLog(L), "pcr_change_hair", upStateTag(L));
    int64_t arg1 = truncateEa2596(lua_tonumber(L, 1));
    int64_t arg2 = truncateEa2596(lua_tonumber(L, 2));
    int64_t arg3 = truncateEa2596(lua_tonumber(L, 3));
    int64_t arg4 = truncateEa2596(lua_tonumber(L, 4));
    if (arg1 < 0 || arg1 >= 3) return 0; // CONFIRMED bounds-check (Sec16.6), real no-op
    static const int32_t kHairCategoryMap[3] = {1, 2, 3}; // CONFIRMED literal table, 0x01300cf4
    int32_t categoryId = kHairCategoryMap[arg1];
    PcrCustomizationState& pcr = upState(L)->pcrCustomization();
    PcrCustomizationItem item;
    item.categoryId = categoryId;
    if (arg2 < 0) {
        item.styleId = -1;
    } else {
        item.styleId = static_cast<int32_t>(arg2);
        item.rawArg2 = static_cast<double>(arg3);
        item.rawArg3 = static_cast<double>(arg4);
    }
    pcr.items[static_cast<size_t>(categoryId)] = item; // CHOSEN: categoryId as slot index
    pcr.dirty = true;
    return 0;
}

// pcr_change_composite (Sec16.7, 0x00835760) - 12 calls/4 scripts.
// CONFIRMED: 3 mandatory (string, number, number) + 2 optional trailing
// numbers (defaults 1.0 and 0). Body: arg1 looked up case-insensitively
// against a 34-entry category-name table (0x0130b7f0) via _stricmp.
// JUDGMENT CALL: the spec's own prose only gives the 34 names in
// ABBREVIATED grouped form ("tattoo upper/lower/entire arm left/right",
// etc.), not as 34 literal flat strings with a confirmed separator - this
// project expands that grouping into 34 distinct, underscore_separated
// names (matching this engine's own typical naming convention elsewhere,
// e.g. "cannot_exit_vehicles") as its OWN CHOSEN transcription; the COUNT
// (34) and GROUPING are CONFIRMED (independently cross-checked: this
// expansion places "fingernails" at index 0x16/22, matching Sec16.7's own
// independently-stated special-cased index exactly), the exact
// separator/spelling is NOT independently confirmed - flagged here and in
// the batch report, not silently assumed correct. On a successful lookup,
// this project records the raw Lua-given numbers directly into the
// shared 63-slot list (categoryId as the slot index, same CHOSEN
// simplification as pcr_change_hair above) rather than the real per-item-
// color-list-resolved, gamma-corrected RGB (0x00832230/0x00ea4300's own
// further transform is OPEN per Sec16.7) - see PcrCustomizationItem's own
// doc comment. Category 0x16 ("fingernails", HIGH CONFIDENCE id-only
// inference) additionally mirrors the same values onto a second,
// literal-named item "fingernails_r" (CONFIRMED special case). A failed
// lookup (-1) only increments a reset/notify counter (no item write -
// CONFIRMED the real fixed notify call, category id 4 literal, has no
// further in-scope observable effect). Return: none.
int stub_pcr_change_composite(lua_State* L) {
    logCall(L, upLog(L), "pcr_change_composite", upStateTag(L));
    std::string categoryName = argString(L, 1);
    double arg2 = lua_tonumber(L, 2);
    double arg3 = lua_tonumber(L, 3);
    double arg4 = optionalNumberDefault(L, 4, 1.0);
    (void)optionalNumberDefault(L, 5, 0.0); // 5th arg - read for real call shape, no further in-scope effect modeled
    static const char* kCategoryNames[34] = {
        "lips", "eyes",
        "tattoo_upper_arm_left", "tattoo_lower_arm_left", "tattoo_entire_arm_left",
        "tattoo_upper_arm_right", "tattoo_lower_arm_right", "tattoo_entire_arm_right",
        "tattoo_upper_chest", "tattoo_lower_chest", "tattoo_entire_chest",
        "tattoo_upper_back", "tattoo_lower_back", "tattoo_entire_back",
        "tattoo_head", "tattoo_neck", "tattoo_leg_left", "tattoo_leg_right",
        "tattoo_hand_left", "tattoo_hand_right",
        "body_mask", "shaderball", "fingernails", "fingernails_alt", "body_diffuse",
        "makeup_eyeliner", "makeup_eyeshadow", "makeup_cheeks", "makeup_entire_face",
        "eyebrows", "facial_hair", "body_hair", "face_features", "face_items",
    };
    int32_t categoryId = -1;
    for (int32_t i = 0; i < 34; ++i) {
        if (caseInsensitiveEquals(categoryName, kCategoryNames[i])) { categoryId = i; break; }
    }
    PcrCustomizationState& pcr = upState(L)->pcrCustomization();
    if (categoryId < 0) {
        ++pcr.compositeLookupFailedCount; // CONFIRMED fixed notify/reset call, category id 4 literal - no further effect
        return 0;
    }
    PcrCustomizationItem item;
    item.categoryId = categoryId;
    item.styleId = -1;
    item.rawArg2 = arg2;
    item.rawArg3 = arg3;
    item.rawArg4 = arg4;
    pcr.items[static_cast<size_t>(categoryId)] = item; // CHOSEN: categoryId as slot index
    if (categoryId == 0x16) { // "fingernails" HIGH CONFIDENCE special case, Sec16.7
        pcr.fingernailsRMirror = item;
        pcr.fingernailsRMirrorSet = true;
    }
    pcr.dirty = true;
    return 0;
}

// pcr_get_player_preset (Sec16.8, 0x00830790) - 11 calls/2 scripts.
// CONFIRMED: no arguments. Return: 3 strings (gender, ethnicity, body-
// type) - body-type is ALWAYS the hardcoded literal "average". See
// PcrCustomizationState's own doc comment (engine_state.h) for the
// sentinel->Default-preset CHOSEN fallback reasoning (genderByte==3/
// ethnicityByte==5, CONFIRMED never reaching a real script per Sec16.8's
// own resolved finding).
int stub_pcr_get_player_preset(lua_State* L) {
    logCall(L, upLog(L), "pcr_get_player_preset", upStateTag(L));
    PcrCustomizationState& pcr = upState(L)->pcrCustomization();
    static const char* kGenderTable[2] = {"male", "female"};
    static const char* kEthnicityTable[4] = {"asian", "black", "hispanic", "white"};
    uint8_t gender = (pcr.genderByte == 3) ? 0 : pcr.genderByte;       // CHOSEN: sentinel -> shipped Default preset, Sec16.8
    uint8_t ethnicity = (pcr.ethnicityByte == 5) ? 3 : pcr.ethnicityByte; // CHOSEN: sentinel -> shipped Default preset, Sec16.8
    if (gender >= 2 || ethnicity >= 4) {
        throw OpenStateError("pcr_get_player_preset gender/ethnicity byte out of table range",
                             "spec-lua-api-behaviour.md Sec16.8");
    }
    lua_pushstring(L, kGenderTable[gender]);
    lua_pushstring(L, kEthnicityTable[ethnicity]);
    lua_pushstring(L, "average"); // CONFIRMED hardcoded literal, not data-driven
    return 3;
}

// pcr_set_identity (Sec16.8a, 0x00830fa0) - not previously catalogued,
// found 2026-10-02. CONFIRMED: 1 mandatory string, compared case-
// insensitively against "male". Result: 0 for "male", 1 for ANY other
// string (no validated enum, no "else 3" sentinel path). If it differs
// from the current genderByte, writes it and refreshes body/re-syncs
// clothing categories (no in-scope observable effect beyond a counter).
// Does NOT touch ethnicityByte. Return: none.
int stub_pcr_set_identity(lua_State* L) {
    logCall(L, upLog(L), "pcr_set_identity", upStateTag(L));
    std::string arg = argString(L, 1);
    uint8_t newGender = caseInsensitiveEquals(arg, "male") ? 0 : 1;
    PcrCustomizationState& pcr = upState(L)->pcrCustomization();
    if (pcr.genderByte != newGender) {
        pcr.genderByte = newGender;
        ++pcr.setIdentityBodyRefreshCount;
    }
    return 0;
}

// dialog_box_disconnect (Sec16.9, 0x007c2f80, force-disassembled) - 9
// calls/9 scripts. CONFIRMED: no arguments. Return: none. See
// EngineState::dialogBoxDisconnectCount()'s own doc comment for why only
// a call counter is modeled (every real internal consequence is session/
// pause-UI teardown this project does not model anywhere).
int stub_dialog_box_disconnect(lua_State* L) {
    logCall(L, upLog(L), "dialog_box_disconnect", upStateTag(L));
    (void)lua_gettop(L);
    upState(L)->recordDialogBoxDisconnect();
    return 0;
}

// main_menu_supress_profile_change (Sec16.10, 0x007c9e60, force-
// disassembled) - 12 calls/3 scripts. CONFIRMED: 1 mandatory boolean,
// direct unconditional write to global 0x022425f6. Return: none.
int stub_main_menu_supress_profile_change(lua_State* L) {
    logCall(L, upLog(L), "main_menu_supress_profile_change", upStateTag(L));
    bool suppress = lua_toboolean(L, 1) != 0;
    upState(L)->mainMenuProfileChangeSuppressed() = suppress;
    return 0;
}

// pause_menu_control_scheme_init (Sec16.11, 0x007e1110) - 11 calls/3
// scripts. CONFIRMED: arg1 mandatory bool, arg2 optional number default
// -1. Return: 0, 2 or 3 numbers (genuinely variable, matched to the real
// pushes). False arg1 -> 0 pushes. True -> stores arg2, pushes 0.0; if
// scheme is exactly 0 or 1/2, pushes a stored preset id (2nd push);
// always (when arg1 true) pushes the decoded literal 25.0 last.
int stub_pause_menu_control_scheme_init(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_control_scheme_init", upStateTag(L));
    bool arg1 = lua_toboolean(L, 1) != 0;
    if (!arg1) return 0; // CONFIRMED zero pushes
    int64_t scheme = truncateEa2596(optionalNumberDefault(L, 2, -1.0));
    EngineState* st = upState(L);
    st->pauseMenuControlScheme() = static_cast<int32_t>(scheme);
    lua_pushnumber(L, 0.0);
    int pushCount = 1;
    if (scheme == 0) {
        lua_pushnumber(L, static_cast<lua_Number>(st->pauseMenuPresetScheme0()));
        ++pushCount;
    } else if (scheme == 1 || scheme == 2) {
        lua_pushnumber(L, static_cast<lua_Number>(st->pauseMenuPresetScheme1Or2()));
        ++pushCount;
    }
    lua_pushnumber(L, 25.0); // CONFIRMED decoded literal, 0x4039000000000000
    ++pushCount;
    return pushCount;
}

// pause_menu_update_option (Sec16.12, 0x007e13e0) - 9 calls/9 scripts.
// CONFIRMED: 2 mandatory numbers (option id, category id) + 1 mandatory
// bool + 1 optional number default 0.0. See EngineState::
// recordPauseMenuUpdateOption()'s own doc comment for the "record the raw
// request, not the unenumerated ~25-setter dispatch" reasoning (Sec16.12's
// own text gives only 4 representative examples, not the full mapping -
// inventing the other ~20 would be fabricating data). Return: none.
int stub_pause_menu_update_option(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_update_option", upStateTag(L));
    int64_t optionId = truncateEa2596(lua_tonumber(L, 1));
    int64_t categoryId = truncateEa2596(lua_tonumber(L, 2));
    bool flag = lua_toboolean(L, 3) != 0;
    double value = optionalNumberDefault(L, 4, 0.0);
    upState(L)->recordPauseMenuUpdateOption(static_cast<int32_t>(categoryId), static_cast<int32_t>(optionId), flag, value);
    return 0;
}

// pause_menu_restore_defaults (Sec16.13, 0x007e11f0) - 10 calls/10
// scripts. CONFIRMED: 1 mandatory number (category id, SAME numbering as
// pause_menu_update_option). Return: none. Sibling simplification to
// pause_menu_update_option above - see that stub's own doc comment.
int stub_pause_menu_restore_defaults(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_restore_defaults", upStateTag(L));
    int64_t categoryId = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->recordPauseMenuRestoreDefaults(static_cast<int32_t>(categoryId));
    return 0;
}

// store_common_bg_anim_complete (Sec16.14, 0x0080dbf0, force-
// disassembled) - 9 calls/9 scripts. CONFIRMED: no arguments, sets global
// 0x022ccee8 to 1 unconditionally. Return: none.
int stub_store_common_bg_anim_complete(lua_State* L) {
    logCall(L, upLog(L), "store_common_bg_anim_complete", upStateTag(L));
    (void)lua_gettop(L);
    upState(L)->storeCommonBgAnimComplete() = true;
    return 0;
}

// store_weapon_hide_weapon (Sec16.15, 0x00815ee0, force-disassembled) - 8
// calls/2 scripts. CONFIRMED: no arguments. Return: none. See
// EngineState::storeWeaponHideWeapon()'s own doc comment for the
// "mechanism confirmed, no in-scope producer" reasoning on the detach
// branch.
int stub_store_weapon_hide_weapon(lua_State* L) {
    logCall(L, upLog(L), "store_weapon_hide_weapon", upStateTag(L));
    (void)lua_gettop(L);
    upState(L)->storeWeaponHideWeapon();
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB12() {
    static const std::vector<std::string> names = {
        // Section 15 batch (spec-lua-api-behaviour.md Sec15) - 25 of its 28
        // named natives (sidewalk_disable_nodes already in b06;
        // teleport_check_done/mission_end_silently not in this batch) - see
        // each stub's own doc comment above for the full citation.
        "character_set_damage_multiplier",
        "vehicle_anim_start",
        "set_always_cower_flag",
        "refraction_situation_clear",
        "qte_cleanup",
        "persona_clear_global_situation_override",
        "on_weapon_pickup",
        "mission_unlock",
        "mission_set_completed",
        "message",
        "hud_display_remove_state",
        "follower_remain_in_car",
        "character_is_ragdolled",
        "vehicle_skydiving_start",
        "vehicle_is_tank",
        "skydive_set_player_dive_fall_speed",
        "set_unenterable_for_player",
        "set_player_undercover_team",
        "player_actions_enable",
        "npc_leash_to_object",
        "hud_display_create_state",
        "hud_display_commit_state",
        "game_choice_set_state",
        "dlc_achievement_counter_set",
        "character_parachute_detach",
        // Section 16 batch (spec-lua-api-behaviour.md Sec16) - all 16 named
        // UI-cluster natives - see each stub's own doc comment above.
        "game_get_language",
        "game_award_respect",
        "tutorial_set_hud_help_closing",
        "completion_load_lut",
        "completion_cutscene_was_active",
        "pcr_change_hair",
        "pcr_change_composite",
        "pcr_get_player_preset",
        "pcr_set_identity",
        "dialog_box_disconnect",
        "main_menu_supress_profile_change",
        "pause_menu_control_scheme_init",
        "pause_menu_update_option",
        "pause_menu_restore_defaults",
        "store_common_bg_anim_complete",
        "store_weapon_hide_weapon",
    };
    return names;
}

void registerB12(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // Section 15 batch.
    if (wants("character_set_damage_multiplier")) registerOne(L, state, log, stateTag, "character_set_damage_multiplier", openGuard<stub_character_set_damage_multiplier>);
    if (wants("vehicle_anim_start")) registerOne(L, state, log, stateTag, "vehicle_anim_start", openGuard<stub_vehicle_anim_start>);
    if (wants("set_always_cower_flag")) registerOne(L, state, log, stateTag, "set_always_cower_flag", openGuard<stub_set_always_cower_flag>);
    if (wants("refraction_situation_clear")) registerOne(L, state, log, stateTag, "refraction_situation_clear", openGuard<stub_refraction_situation_clear>);
    if (wants("qte_cleanup")) registerOne(L, state, log, stateTag, "qte_cleanup", openGuard<stub_qte_cleanup>);
    if (wants("persona_clear_global_situation_override")) registerOne(L, state, log, stateTag, "persona_clear_global_situation_override", openGuard<stub_persona_clear_global_situation_override>);
    if (wants("on_weapon_pickup")) registerOne(L, state, log, stateTag, "on_weapon_pickup", openGuard<stub_on_weapon_pickup>);
    if (wants("mission_unlock")) registerOne(L, state, log, stateTag, "mission_unlock", openGuard<stub_mission_unlock>);
    if (wants("mission_set_completed")) registerOne(L, state, log, stateTag, "mission_set_completed", openGuard<stub_mission_set_completed>);
    if (wants("message")) registerOne(L, state, log, stateTag, "message", openGuard<stub_message>);
    if (wants("hud_display_remove_state")) registerOne(L, state, log, stateTag, "hud_display_remove_state", openGuard<stub_hud_display_remove_state>);
    if (wants("follower_remain_in_car")) registerOne(L, state, log, stateTag, "follower_remain_in_car", openGuard<stub_follower_remain_in_car>);
    if (wants("character_is_ragdolled")) registerOne(L, state, log, stateTag, "character_is_ragdolled", openGuard<stub_character_is_ragdolled>);
    if (wants("vehicle_skydiving_start")) registerOne(L, state, log, stateTag, "vehicle_skydiving_start", openGuard<stub_vehicle_skydiving_start>);
    if (wants("vehicle_is_tank")) registerOne(L, state, log, stateTag, "vehicle_is_tank", openGuard<stub_vehicle_is_tank>);
    if (wants("skydive_set_player_dive_fall_speed")) registerOne(L, state, log, stateTag, "skydive_set_player_dive_fall_speed", openGuard<stub_skydive_set_player_dive_fall_speed>);
    if (wants("set_unenterable_for_player")) registerOne(L, state, log, stateTag, "set_unenterable_for_player", openGuard<stub_set_unenterable_for_player>);
    if (wants("set_player_undercover_team")) registerOne(L, state, log, stateTag, "set_player_undercover_team", openGuard<stub_set_player_undercover_team>);
    if (wants("player_actions_enable")) registerOne(L, state, log, stateTag, "player_actions_enable", openGuard<stub_player_actions_enable>);
    if (wants("npc_leash_to_object")) registerOne(L, state, log, stateTag, "npc_leash_to_object", openGuard<stub_npc_leash_to_object>);
    if (wants("hud_display_create_state")) registerOne(L, state, log, stateTag, "hud_display_create_state", openGuard<stub_hud_display_create_state>);
    if (wants("hud_display_commit_state")) registerOne(L, state, log, stateTag, "hud_display_commit_state", openGuard<stub_hud_display_commit_state>);
    if (wants("game_choice_set_state")) registerOne(L, state, log, stateTag, "game_choice_set_state", openGuard<stub_game_choice_set_state>);
    if (wants("dlc_achievement_counter_set")) registerOne(L, state, log, stateTag, "dlc_achievement_counter_set", openGuard<stub_dlc_achievement_counter_set>);
    if (wants("character_parachute_detach")) registerOne(L, state, log, stateTag, "character_parachute_detach", openGuard<stub_character_parachute_detach>);

    // Section 16 batch.
    if (wants("game_get_language")) registerOne(L, state, log, stateTag, "game_get_language", openGuard<stub_game_get_language>);
    if (wants("game_award_respect")) registerOne(L, state, log, stateTag, "game_award_respect", openGuard<stub_game_award_respect>);
    if (wants("tutorial_set_hud_help_closing")) registerOne(L, state, log, stateTag, "tutorial_set_hud_help_closing", openGuard<stub_tutorial_set_hud_help_closing>);
    if (wants("completion_load_lut")) registerOne(L, state, log, stateTag, "completion_load_lut", openGuard<stub_completion_load_lut>);
    if (wants("completion_cutscene_was_active")) registerOne(L, state, log, stateTag, "completion_cutscene_was_active", openGuard<stub_completion_cutscene_was_active>);
    if (wants("pcr_change_hair")) registerOne(L, state, log, stateTag, "pcr_change_hair", openGuard<stub_pcr_change_hair>);
    if (wants("pcr_change_composite")) registerOne(L, state, log, stateTag, "pcr_change_composite", openGuard<stub_pcr_change_composite>);
    if (wants("pcr_get_player_preset")) registerOne(L, state, log, stateTag, "pcr_get_player_preset", openGuard<stub_pcr_get_player_preset>);
    if (wants("pcr_set_identity")) registerOne(L, state, log, stateTag, "pcr_set_identity", openGuard<stub_pcr_set_identity>);
    if (wants("dialog_box_disconnect")) registerOne(L, state, log, stateTag, "dialog_box_disconnect", openGuard<stub_dialog_box_disconnect>);
    if (wants("main_menu_supress_profile_change")) registerOne(L, state, log, stateTag, "main_menu_supress_profile_change", openGuard<stub_main_menu_supress_profile_change>);
    if (wants("pause_menu_control_scheme_init")) registerOne(L, state, log, stateTag, "pause_menu_control_scheme_init", openGuard<stub_pause_menu_control_scheme_init>);
    if (wants("pause_menu_update_option")) registerOne(L, state, log, stateTag, "pause_menu_update_option", openGuard<stub_pause_menu_update_option>);
    if (wants("pause_menu_restore_defaults")) registerOne(L, state, log, stateTag, "pause_menu_restore_defaults", openGuard<stub_pause_menu_restore_defaults>);
    if (wants("store_common_bg_anim_complete")) registerOne(L, state, log, stateTag, "store_common_bg_anim_complete", openGuard<stub_store_common_bg_anim_complete>);
    if (wants("store_weapon_hide_weapon")) registerOne(L, state, log, stateTag, "store_weapon_hide_weapon", openGuard<stub_store_weapon_hide_weapon>);
}

} // namespace stub_sections
} // namespace sr3luahost
