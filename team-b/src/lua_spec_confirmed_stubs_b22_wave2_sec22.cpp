// lua_spec_confirmed_stubs_b22_wave2_sec22.cpp
//
// Wave 2, Batch W2-4 - "Sec22+Sec24+Sec25 gameplay residue" (78 names total,
// split into TWO files per the orchestrator's own dispatch: this file holds
// spec-lua-api-behaviour.md Sec22 ("ranks 351-450 tranche, part B", 24 of
// its 30 names - the other 6, vehicle_pathfind_navmesh_do/vehicle_enter_
// check_done/teleport_to_object/move_to_check_done/group_create_check_done/
// action_play_do, belong to sibling wave-2 batches), the Sec24+Sec25 half
// lives in lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp. Own stub_*
// functions (anonymous namespace), own namesB22()/registerB22() pair
// declared in lua_spec_confirmed_stubs_sections.h, called from lua_spec_
// confirmed_stubs_registry.cpp - same pattern every b0N/b1N file already
// uses (see lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp for the
// most recently merged example this file follows).
//
// CLEAN-ROOM: implemented ONLY from spec-lua-api-behaviour.md Sec22 (plus
// the cross-referenced sections it cites - Sec3.7/Sec6.6/Sec6.8/Sec7.11/
// Sec7.17/Sec9.10/Sec15.4/Sec18.28/Sec21.21/Sec22.3/Sec25.1-25.28/Sec28.2/
// Sec33.1/Sec45.1, all already-landed spec text or already-modelled
// EngineState fields this project built from that same spec) plus real
// shipped game data; never the executable or any disassembly. See each
// stub's own doc comment for its exact citation and every CHOSEN/HIGH-
// CONFIDENCE/OPEN judgment call. See engine_state.h's own "Wave 2, Batch
// W2-4" doc comments (CharacterState/VehicleState/PlacedObjectState/
// HelicopterFlyTo/PartyState/ContinuousExplosion/DebrisFlowState/
// NotorietySpawnGroupState/EngineState) for the full per-field reasoning
// the new state below rests on.
//
// HARDENING POLICY findings in this file: is_rappelling (Sec22.16) - a
// genuine resolve-failure path the spec itself flags as "a near-null read
// ... not independently verified to fault at runtime"; this host does not
// reproduce an unverified crash shape, so it answers `false` for that case
// instead (CHOSEN, hardened divergence, counted). debris_flow_create
// (Sec25.21, implemented in the sibling file) has its own, separate
// hardening note - see that file.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// vehicle_make_corpse (Sec22.2, 0x00a62980). 1 mandatory string (vehicle) +
// 1 mandatory boolean (explode), no nil-gate on either. Return none.
// CONFIRMED: no-ops if already flagged a corpse (bit 0x1 of +0x16c8) -
// migrates the vehicle into a global "corpse" list otherwise (not modelled,
// no generic linked-list machinery in this project - see VehicleState::
// isCorpse's own doc comment). The explode-argument's own further cascade
// (0x00aa3b90 + 3 extra deadline-setter calls, gated on 0x008837a0) is OPEN
// internals-only - only that the argument was recorded is tracked.
int stub_vehicle_make_corpse(lua_State* L) {
    logCall(L, upLog(L), "vehicle_make_corpse", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool explode = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    if (v.isCorpse) {
        ++v.makeCorpseAlreadyCorpseNoopCount; // CONFIRMED no-op branch
        return 0;
    }
    v.isCorpse = true;
    v.corpseExplodeArg = explode;
    ++v.makeCorpseCount;
    return 0;
}

// teleport_player_vehicles (Sec22.5, 0x00a61170). 2 mandatory strings (ref
// vehicle A, ref vehicle B), both resolved via the shared flat objectResolves()
// stand-in. Return none. CONFIRMED: relocates PLAYER1's own cached vehicle
// (a NEW field pair, +0x2880/+0x2884, distinct from the generic character
// +0x16c0/+0x16c4 pair - PlayerRig::player1VehicleName) to ref A's position;
// if a co-op partner is present (playerRig().coopPlayerPresent, already
// modelled, Sec28.12), does the same for PLAYER2's own cached vehicle
// (player2VehicleName) to ref B. No coordinate system in this project - only
// the CONFIRMED relocation REQUEST (which vehicle moved to which reference)
// is recorded, honestly, per VehicleState::lastTeleportRelocatedToName.
int stub_teleport_player_vehicles(lua_State* L) {
    logCall(L, upLog(L), "teleport_player_vehicles", upStateTag(L));
    std::string refA = argString(L, 1);
    std::string refB = argString(L, 2);
    EngineState* st = upState(L);
    bool refAResolves = st->objectResolves().get(refA); // OPEN until set
    bool refBResolves = st->objectResolves().get(refB); // OPEN until set
    auto& rig = st->playerRig();
    if (refAResolves && !rig.player1VehicleName.empty() && st->objectResolves().get(rig.player1VehicleName)) {
        VehicleState& v1 = st->getOrCreateVehicle(rig.player1VehicleName);
        v1.lastTeleportRelocatedToName = refA;
        ++v1.teleportRelocateCount;
        ++rig.teleportPlayer1Count;
    }
    if (refBResolves && rig.coopPlayerPresent.known() && rig.coopPlayerPresent.get() &&
        !rig.player2VehicleName.empty() && st->objectResolves().get(rig.player2VehicleName)) {
        VehicleState& v2 = st->getOrCreateVehicle(rig.player2VehicleName);
        v2.lastTeleportRelocatedToName = refB;
        ++v2.teleportRelocateCount;
        ++rig.teleportPlayer2Count;
    }
    return 0;
}

// spotlight_heli_laser_enable (Sec22.6, 0x00a5de90). 1 mandatory boolean, no
// nil-gate. Return none. CONFIRMED: writes straight to a single global - no
// resolver, no gate, no record.
int stub_spotlight_heli_laser_enable(lua_State* L) {
    logCall(L, upLog(L), "spotlight_heli_laser_enable", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0;
    upState(L)->spotlightHeliLaserEnabled() = enable;
    return 0;
}

// roadblocks_ambient_traffic_enable (Sec22.7, 0x00a5b680). 1 mandatory
// boolean, no nil-gate. Return none. CONFIRMED: only writes the global
// "when a live session exists AND this machine is host" - reuses
// coopLocalIsHost() directly (CONFIRMED false in single player project-
// wide, per Sec8.27/Sec26.28), so this is a real, confirmed no-op there.
int stub_roadblocks_ambient_traffic_enable(lua_State* L) {
    logCall(L, upLog(L), "roadblocks_ambient_traffic_enable", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    if (st->coopLocalIsHost()) {
        st->roadblocksAmbientTrafficEnabled() = enable;
    } else {
        ++st->roadblocksAmbientTrafficNoSessionSkipCount(); // CONFIRMED no-op in single player
    }
    return 0;
}

// party_set_dismissable (Sec22.8, 0x00a59430). 1 mandatory boolean, no nil-
// gate. Return none. CONFIRMED: writes straight to a single global byte
// (0x0130af61), the sibling of party_set_recruitable's own 0x0130af60
// (Sec18.28) - no resolver, no gate.
int stub_party_set_dismissable(lua_State* L) {
    logCall(L, upLog(L), "party_set_dismissable", upStateTag(L));
    bool dismissable = lua_toboolean(L, 1) != 0;
    upState(L)->party().dismissable = dismissable;
    return 0;
}

// party_allow_max_followers (Sec22.9, 0x00a59380). 1 optional boolean,
// standard nil-gated idiom, default true. Return none. CONFIRMED (Sec3.7's
// own single-player resolution): the double-gate (0x008ae480/0x008837a0)
// always takes the direct-local-apply branch with no session installed, so
// PLAYER1's own flag always applies locally; PLAYER2's own half repeats
// only if a co-op partner is present (playerRig().coopPlayerPresent,
// already modelled, Sec28.12).
int stub_party_allow_max_followers(lua_State* L) {
    logCall(L, upLog(L), "party_allow_max_followers", upStateTag(L));
    bool allow = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    auto& rig = st->playerRig();
    rig.player1AllowMaxFollowers = allow;
    ++rig.allowMaxFollowersReplicateCount;
    if (rig.coopPlayerPresent.known() && rig.coopPlayerPresent.get()) {
        rig.player2AllowMaxFollowers = allow;
        ++rig.allowMaxFollowersReplicateCount;
    }
    return 0;
}

// on_projectile_hit (Sec22.10, 0x00a571e0). 1 optional string (callback
// name, opaque), no nil-gate as such - a null/empty check gates whether the
// name is actually looked up. Return none. CONFIRMED: registers into the
// GLOBAL-anchored named-hook-slot-array family at slot 7 - this project's
// own uniform EngineState::namedHookCallback stand-in (see its own doc
// comment), keyed by this native's own name since the slot is genuinely
// global (not per-object).
int stub_on_projectile_hit(lua_State* L) {
    logCall(L, upLog(L), "on_projectile_hit", upStateTag(L));
    std::string callbackName = optionalStringDefault(L, 1, "");
    upState(L)->setNamedHookCallback("on_projectile_hit", callbackName);
    return 0;
}

// on_near_crash (Sec22.11, 0x00a57100). 2 mandatory strings: callback name
// (opaque) + a target-object reference, resolved via the shared flat
// objectResolves() stand-in. Return none. CONFIRMED: registers a per-OBJECT
// hook at a computed sub-record (+0x1f00), slot 0xf - this project's own
// uniform namedHookCallback stand-in, keyed per resolved object name.
int stub_on_near_crash(lua_State* L) {
    logCall(L, upLog(L), "on_near_crash", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(targetName)) return 0; // OPEN until set
    st->setNamedHookCallback("on_near_crash:" + targetName, callbackName);
    return 0;
}

// npc_dont_drop_weapon_on_death (Sec22.12, 0x00a553c0). 1 mandatory string
// (character/NPC reference) + 1 mandatory boolean, no nil-gate. Return
// none. CONFIRMED double-gate (variant 1, reusing forceFlagGatePasses) -
// tag "npc"+"force_flagsdont_drop_weapon_on_death" - gate-true -> sets/
// clears bit 0x20 of +0x1e00 directly; gate-false -> replicate only.
int stub_npc_dont_drop_weapon_on_death(lua_State* L) {
    logCall(L, upLog(L), "npc_dont_drop_weapon_on_death", upStateTag(L));
    std::string name = argString(L, 1);
    bool dontDrop = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    // Resolved via 0x00a28150, called DIRECTLY (Sec22.12's own text) -
    // wired 2026-10-08 (follower-sentinel-resolver pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.npcForceFlags1e00.setBits(CharacterState::kDontDropWeaponOnDeathBit1e00,
                                    dontDrop ? CharacterState::kDontDropWeaponOnDeathBit1e00 : 0u);
    } else {
        EngineState::replicateStateChange("npc_dont_drop_weapon_on_death", resolvedName);
    }
    return 0;
}

// notoriety_spawn_only_air (Sec22.13, 0x00a54f60). 1 mandatory boolean, no
// nil-gate. Return none. CONFIRMED: writes unconditionally to (in the real
// engine) nine separate fixed global objects' own +0x455 byte via one
// shared setter - this project tracks ONE global bool standing in for all
// nine (no generic entity shape invented, same convention as this project's
// roadblocksEnabled_/similar bare-global fields).
int stub_notoriety_spawn_only_air(lua_State* L) {
    logCall(L, upLog(L), "notoriety_spawn_only_air", upStateTag(L));
    bool airOnly = lua_toboolean(L, 1) != 0;
    upState(L)->notorietySpawnOnlyAir() = airOnly;
    return 0;
}

// notoriety_restricted_zones_enable (Sec22.14, 0x00a55050). 1 optional
// boolean, standard nil-gated idiom, default true. Return none. CONFIRMED:
// if the value genuinely changes AND the new value is false, walks the
// small fixed 4-entry table (0x026ea7f8) clearing every still-"valid"
// entry back to -1.
int stub_notoriety_restricted_zones_enable(lua_State* L) {
    logCall(L, upLog(L), "notoriety_restricted_zones_enable", upStateTag(L));
    bool enable = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    auto& zones = st->notorietyRestrictedZones();
    bool changed = (zones.enabled != enable);
    zones.enabled = enable;
    ++zones.setCount;
    if (changed && !enable) {
        for (auto& slot : zones.slots) {
            if (slot >= 0) {
                slot = -1;
                ++zones.clearedSlotCount;
            }
        }
    }
    return 0;
}

// is_rappelling (Sec22.16, 0x00a50d60). 1 mandatory string (character
// reference), resolved via the shared flat objectResolves() stand-in.
// Return - a genuine CONFIRMED quirk: always pushes exactly 2 Lua values
// (the predicate's own result pushed twice, unconditionally, even on a
// resolve failure). CONFIRMED predicate: true only when the character's own
// +0xcc8 reads 0x12(18) AND the companion byte +0xcce reads '#'. HARDENED
// (hardened divergence, CHOSEN): a genuine resolve failure calls the real
// predicate with a near-null handle (reads below this module's own base) -
// Sec22.16's own text flags this as "very likely to fault... NOT
// independently verified by running the game"; this host does not
// reproduce an unverified crash shape and answers `false` instead, counted.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_is_rappelling(lua_State* L) {
    logCall(L, upLog(L), "is_rappelling", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    bool resolved = resolveFollowerAwareObject(st, name, resolvedName); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    bool rappelling = false;
    if (resolved) {
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        rappelling = (c.rappelStateCodeCc8 == 0x12) && (c.rappelCompanionByteCce == '#');
    } else {
        ++st->isRappellingResolveFailureSafeCount(); // CHOSEN (hardened divergence) - see this stub's own doc comment
    }
    lua_pushboolean(L, rappelling ? 1 : 0);
    lua_pushboolean(L, rappelling ? 1 : 0); // CONFIRMED double-push return-arity quirk, both branches
    return 2;
}

// ingame_effect_remove_trigger (Sec22.17, 0x00a51720). 1 mandatory string
// (trigger/generic-object reference), resolved via the shared flat
// objectResolves() stand-in + 1 optional number, standard nil-gated idiom,
// default the literal 3 (a 2-bit action bitmask). Return none. CONFIRMED:
// bit 0x2 (default set) opens a network record (CHOSEN: unconditional
// replicateStateChange when set, since this project does not model
// 0x0087ba20's own extra-argument call shape here, Sec22.17's own flagged
// item); bit 0x1 (default set), if the object's own cached "current effect
// index" isn't already -1, resets it to -1.
int stub_ingame_effect_remove_trigger(lua_State* L) {
    logCall(L, upLog(L), "ingame_effect_remove_trigger", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    if (mask & 0x2) {
        EngineState::replicateStateChange("ingame_effect_remove_trigger", name);
        ++st->ingameEffectRemoveNotifyCount();
    }
    if (mask & 0x1) {
        if (st->ingameEffectIndex(name) != -1) {
            st->setIngameEffectIndex(name, -1);
            ++st->ingameEffectRemoveClearedCount();
        }
    }
    return 0;
}

// get_vehicle_type (Sec22.19, 0x00a4b050). 1 mandatory string (vehicle
// reference), resolved via the shared flat objectResolves() stand-in.
// Return 1 number. CORRECTED 2026-10-11 (Sec22.19/Sec21.24, spec sync): this
// is the vehicle's static `Vehicle_Type` xtbl class (0=Automobile,
// 1=Motorcycle, 2=Airplane, 3=Helicopter, 4=Vtol, 5=Watercraft, 6=failure
// sentinel), NOT an AI-mode enum as this comment used to claim - the
// field/accessor (0x00ad2f80, (vehicle+0xbf4)+0x2c) is the SAME one
// get_char_vehicle_type (Sec21.24) reads, confirmed by 3 independent
// sources. The implementation below was already correct (it already reused
// VehicleState::flyingType with the same 0-6 clamp) - only this doc comment
// was wrong; no code change needed here. Byte-confirmed fallback 0.0 if
// unresolved or no class context.
int stub_get_vehicle_type(lua_State* L) {
    logCall(L, upLog(L), "get_vehicle_type", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    double result = 0.0; // byte-confirmed fallback, 0x01115250
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.flyingType.known()) {
            int32_t mode = v.flyingType.get();
            if (mode < 0) mode = 6;
            else if (mode > 6) mode = 6;
            result = static_cast<double>(mode);
        }
    }
    lua_pushnumber(L, result);
    return 1;
}

// dlc_gis_film_grain_load (Sec22.20, 0x00a47860). 1 optional number,
// standard nil-gated idiom, default the literal 3 (a 2-bit action
// bitmask). Return none. CONFIRMED: bit 0x1 (default set) sets the shared
// DLC film-grain "active" latch if currently clear; bit 0x2 (default set)
// fires the shared local-only notification record.
int stub_dlc_gis_film_grain_load(lua_State* L) {
    logCall(L, upLog(L), "dlc_gis_film_grain_load", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if ((mask & 0x1) && !st->dlcGisFilmGrainActive()) {
        st->dlcGisFilmGrainActive() = true;
        ++st->dlcGisFilmGrainLoadLatchSetCount();
        // Sec17.29: the per-frame tick then loads the "gis_grain" document (id >= 0); this host has no frame
        // tick for it, so the loaded state is recorded here (a blocking-menu delay is not modelled).
        st->dlcFilmGrainAvailable().set(true);
    }
    if (mask & 0x2) {
        EngineState::replicateStateChange("dlc_gis_film_grain_load", "");
        ++st->dlcGisFilmGrainLoadNotifyCount();
    }
    return 0;
}

// debris_flow_add_object (Sec22.21, 0x00a47ea0). 1 mandatory number
// (rounded, a debris-flow id) + 1 mandatory string (object reference,
// resolved via the shared flat objectResolves() stand-in) + 3 mandatory
// numbers (local offset, not modelled - no coordinate system) + 2 optional
// booleans, standard nil-gated idiom, both default false. Return none.
// CONFIRMED: on the network HOST, additionally fires a local-only
// notification record; regardless, walks the SAME 3-entry fixed table
// debris_flow_create/_destroy use (EngineState::debrisFlow(), see its own
// doc comment) - on a match (an ALLOCATED, non-destroyed slot), applies;
// no match -> no apply (the real, confirmed branch, not a hardening
// divergence).
int stub_debris_flow_add_object(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_add_object", upStateTag(L));
    int64_t flowId = truncateEa2596(lua_tonumber(L, 1));
    std::string objectName = argString(L, 2);
    double offsetX = lua_tonumber(L, 3);
    double offsetY = lua_tonumber(L, 4);
    double offsetZ = lua_tonumber(L, 5);
    (void)offsetX;
    (void)offsetY;
    (void)offsetZ; // no coordinate system modelled in this project
    bool bool1 = optionalBoolDefault(L, 6, false);
    bool bool2 = optionalBoolDefault(L, 7, false);
    (void)bool1;
    (void)bool2; // recorded in the avoid-object sibling's own convention only where read back matters
    EngineState* st = upState(L);
    if (!st->objectResolves().get(objectName)) return 0; // OPEN until set
    if (st->coopLocalIsHost()) {
        EngineState::replicateStateChange("debris_flow_add_object", objectName); // opcode 0x45, local-only commit
    }
    auto& flow = st->debrisFlow();
    if (flowId >= 0 && flowId < static_cast<int64_t>(EngineState::DebrisFlowState::kFlowSlotCount)) {
        auto& inst = flow.flows[static_cast<size_t>(flowId)];
        if (inst.allocated && !inst.destroyed) {
            inst.objects.push_back(objectName);
            ++flow.addObjectCount;
        } else {
            ++flow.addObjectNoMatchCount; // CONFIRMED real "no apply" branch, not hardening
        }
    } else {
        ++flow.addObjectNoMatchCount; // out-of-range id - same real "no match" outcome
    }
    return 0;
}

// cower (Sec22.22, 0x00a437e0). 2 mandatory strings (character; threat/
// target reference), both resolved via the shared flat objectResolves()
// stand-in + 2 optional booleans, standard nil-gated idiom, both default
// false. Return none. CONFIRMED: enters the "cower" AI reaction state
// (state-code 9, flag bit 0x8@+0x317 - not individually modelled, only the
// top-level "cowering" fact) directed at the named threat, packing the 2
// boolean modifiers (real-world meaning HYPOTHESIS per spec).
// Resolver 0x00a281a0 (both args) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_cower(lua_State* L) {
    logCall(L, upLog(L), "cower", upStateTag(L));
    std::string characterName = argString(L, 1);
    std::string threatName = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, false);
    bool arg4 = optionalBoolDefault(L, 4, false);
    EngineState* st = upState(L);
    std::string resolvedCharacter, resolvedThreat;
    bool charResolves = resolveFollowerAwareObject(st, characterName, resolvedCharacter);  // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    bool threatResolves = resolveFollowerAwareObject(st, threatName, resolvedThreat);   // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (charResolves && threatResolves) {
        CharacterState& c = st->getOrCreateCharacter(resolvedCharacter);
        c.cowering = true;
        c.cowerThreatName = resolvedThreat;
        c.cowerArg3 = arg3;
        c.cowerArg4 = arg4;
        ++c.cowerCount;
    }
    return 0;
}

// continuous_spawn_set_orders (Sec22.23, 0x00a463d0). 4 mandatory, no nil-
// gate: group name (resolved via the SAME "guardian angel"/script-group
// table group_destroy_do/Sec25.16 and group_create_check_done already
// share), an order-type name (matched against the CONFIRMED 4-entry literal
// table "attack"/"defend"/"moveflee"/"idle", else sentinel 4), an anchor/
// target reference (resolved via the shared flat objectResolves() stand-in,
// fails harmlessly), a radius number. Return none. CONFIRMED: writes the
// resolved order-type index, anchor position (not modelled - world-origin
// default if unresolved) and radius into the per-group order-slot table.
int stub_continuous_spawn_set_orders(lua_State* L) {
    logCall(L, upLog(L), "continuous_spawn_set_orders", upStateTag(L));
    std::string groupName = argString(L, 1);
    std::string orderTypeName = argString(L, 2);
    std::string anchorName = argString(L, 3);
    double radius = lua_tonumber(L, 4);
    EngineState* st = upState(L);
    if (!(st->guardianAngelGroupKnown(groupName) && st->guardianAngelGroupRegistered(groupName))) return 0; // OPEN until set
    auto& order = st->getOrCreateContinuousSpawnOrder(groupName);
    order.orderTypeIndex = EngineState::continuousSpawnOrderTypeIndex(orderTypeName);
    order.anchorName = (!anchorName.empty() && st->objectResolves().known(anchorName) && st->objectResolves().get(anchorName))
                            ? anchorName
                            : std::string(); // "" = world-origin default
    order.radius = radius;
    ++order.setCount;
    return 0;
}

// continuous_explosion_stop (Sec22.24, 0x00a42b30). No real arguments
// consumed (lua_gettop's own result is unused - CONFIRMED genuinely zero-
// argument-sensitive). Return none. CONFIRMED (reconciled with Sec28.16):
// unconditionally clears the ONE global continuous-explosion running flag
// (EngineState::continuousExplosion().active, already modelled) - does NOT
// reset the definition row or position (the next start overwrites them).
int stub_continuous_explosion_stop(lua_State* L) {
    logCall(L, upLog(L), "continuous_explosion_stop", upStateTag(L));
    (void)lua_gettop(L); // CONFIRMED read but unused, matching the real inert-argument shape
    EngineState* st = upState(L);
    st->continuousExplosion().active.set(false);
    ++st->continuousExplosion().stopCount;
    return 0;
}

// check_animation_state (Sec22.25, 0x00a433e0). 2 mandatory strings:
// character reference (resolved via the shared flat objectResolves() stand-
// in) + an animation-state NAME (never resolved as an object). Return 1
// boolean. CONFIRMED: compares the character's own cached "current
// animation-state id" (+0x1cc4, a NEW field) against the SAME shared name->
// id table EngineState::lookupAnimationStateId already models (Sec7.11/
// Sec7.17/Sec25.24) - `false` if the character doesn't resolve at all.
int stub_check_animation_state(lua_State* L) {
    logCall(L, upLog(L), "check_animation_state", upStateTag(L));
    std::string name = argString(L, 1);
    std::string stateName = argString(L, 2);
    EngineState* st = upState(L);
    bool match = false;
    std::string resolvedName;
    // arg1 resolved via 0x00a28150 (Sec22.25's own text) - wired 2026-10-08
    // (follower-sentinel-resolver pass).
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        match = (c.currentAnimationStateId == st->lookupAnimationStateId(stateName));
    }
    lua_pushboolean(L, match ? 1 : 0);
    return 1;
}

// character_release_human_shield (Sec22.26, 0x00a42010). 1 mandatory string
// (character reference), resolved via the shared flat objectResolves()
// stand-in + 2 optional booleans, standard nil-gated idiom, both default
// false ("forcibly eject" / "delayed release"). Return NONE (a confirmed
// asymmetry against player_release_human_shield, which pushes a boolean -
// out of this batch's scope, not yet implemented). CONFIRMED branch
// structure: forced-eject falls through to ALSO run the normal release;
// delayed-release returns immediately, skipping normal release; otherwise
// runs normal release alone. Reuses CharacterState::humanShieldHostageName
// directly (already modelled, Sec45.1) as the "holding a hostage" state.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_release_human_shield(lua_State* L) {
    logCall(L, upLog(L), "character_release_human_shield", upStateTag(L));
    std::string name = argString(L, 1);
    bool forciblyEject = optionalBoolDefault(L, 2, false);
    bool delayedRelease = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (forciblyEject) {
        c.humanShieldHostageName.clear();
        ++c.humanShieldForcedEjectCount;
        // CONFIRMED: falls through to ALSO run normal release below.
    } else if (delayedRelease) {
        ++c.humanShieldDelayedReleaseCount; // CONFIRMED: returns immediately, no clear yet (deferred)
        return 0;
    }
    if (!c.humanShieldHostageName.empty() || forciblyEject) {
        c.humanShieldHostageName.clear();
        ++c.humanShieldNormalReleaseCount;
    }
    return 0;
}

// boss_battle_kb_enable_crowd_props (Sec22.27, 0x00a409a0). 1 mandatory
// boolean (enable) + 1 optional number, standard nil-gated idiom, default
// the literal 1. Return none. CONFIRMED: enabling resolves 6 named level
// markers into a small global position table (not previously catalogued,
// not modelled - no coordinate system) and sets a density global from the
// numeric argument; disabling tears down. Only the CONFIRMED top-level
// facts (enabled, density) are tracked.
int stub_boss_battle_kb_enable_crowd_props(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_kb_enable_crowd_props", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0;
    double density = optionalNumberDefault(L, 2, 1.0);
    EngineState* st = upState(L);
    auto& props = st->bossBattleKbCrowdProps();
    props.enabled = enable;
    if (enable) {
        props.density = density;
        ++props.enableCount;
    } else {
        ++props.disableCount;
    }
    return 0;
}

// base_jumping_enable (Sec22.28, 0x00a40ba0). 1 optional boolean, standard
// nil-gated idiom, default true. Return none. CONFIRMED: applies locally
// AND unconditionally fires a notification record (the FIRST confirmed
// opcode-0x44 sighting through the local-only commit sibling).
int stub_base_jumping_enable(lua_State* L) {
    logCall(L, upLog(L), "base_jumping_enable", upStateTag(L));
    bool enable = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    st->baseJumpingEnabled() = enable;
    EngineState::replicateStateChange("base_jumping_enable", "");
    ++st->baseJumpingReplicateCount();
    return 0;
}

// wind_override_set (Sec22.30, 0x00a68730). 4 mandatory numbers, no nil-
// gate: direction x/y/z + a magnitude/duration value. Return none.
// CONFIRMED exact normalize algorithm (byte-confirmed constants - see
// EngineState::normalizeWindVector's own doc comment): degenerate (near-
// zero or non-finite) input falls back to the fixed (1,0,0) axis. CONFIRMED:
// if the shared replication-mode flag (0x024d4461, EngineState::
// actionStopSyncedStateFlag) is set, fires a local-only notification record;
// regardless, always applies locally.
int stub_wind_override_set(lua_State* L) {
    logCall(L, upLog(L), "wind_override_set", upStateTag(L));
    double x = lua_tonumber(L, 1);
    double y = lua_tonumber(L, 2);
    double z = lua_tonumber(L, 3);
    double magnitude = lua_tonumber(L, 4);
    EngineState* st = upState(L);
    auto& wind = st->windOverride();
    EngineState::normalizeWindVector(x, y, z, wind.x, wind.y, wind.z);
    wind.magnitude = magnitude;
    ++wind.setCount;
    if (st->actionStopSyncedStateFlag()) {
        EngineState::replicateStateChange("wind_override_set", "");
        ++wind.setNotifyCount;
    }
    return 0;
}

const std::vector<std::string>& b22Names() {
    static const std::vector<std::string> names = {
        "vehicle_make_corpse",
        "teleport_player_vehicles",
        "spotlight_heli_laser_enable",
        "roadblocks_ambient_traffic_enable",
        "party_set_dismissable",
        "party_allow_max_followers",
        "on_projectile_hit",
        "on_near_crash",
        "npc_dont_drop_weapon_on_death",
        "notoriety_spawn_only_air",
        "notoriety_restricted_zones_enable",
        "is_rappelling",
        "ingame_effect_remove_trigger",
        "get_vehicle_type",
        "dlc_gis_film_grain_load",
        "debris_flow_add_object",
        "cower",
        "continuous_spawn_set_orders",
        "continuous_explosion_stop",
        "check_animation_state",
        "character_release_human_shield",
        "boss_battle_kb_enable_crowd_props",
        "base_jumping_enable",
        "wind_override_set",
    };
    return names;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB22() { return b22Names(); }

void registerB22(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("vehicle_make_corpse")) registerOne(L, state, log, stateTag, "vehicle_make_corpse", openGuard<stub_vehicle_make_corpse>);
    if (wants("teleport_player_vehicles")) registerOne(L, state, log, stateTag, "teleport_player_vehicles", openGuard<stub_teleport_player_vehicles>);
    if (wants("spotlight_heli_laser_enable")) registerOne(L, state, log, stateTag, "spotlight_heli_laser_enable", openGuard<stub_spotlight_heli_laser_enable>);
    if (wants("roadblocks_ambient_traffic_enable")) registerOne(L, state, log, stateTag, "roadblocks_ambient_traffic_enable", openGuard<stub_roadblocks_ambient_traffic_enable>);
    if (wants("party_set_dismissable")) registerOne(L, state, log, stateTag, "party_set_dismissable", openGuard<stub_party_set_dismissable>);
    if (wants("party_allow_max_followers")) registerOne(L, state, log, stateTag, "party_allow_max_followers", openGuard<stub_party_allow_max_followers>);
    if (wants("on_projectile_hit")) registerOne(L, state, log, stateTag, "on_projectile_hit", openGuard<stub_on_projectile_hit>);
    if (wants("on_near_crash")) registerOne(L, state, log, stateTag, "on_near_crash", openGuard<stub_on_near_crash>);
    if (wants("npc_dont_drop_weapon_on_death")) registerOne(L, state, log, stateTag, "npc_dont_drop_weapon_on_death", openGuard<stub_npc_dont_drop_weapon_on_death>);
    if (wants("notoriety_spawn_only_air")) registerOne(L, state, log, stateTag, "notoriety_spawn_only_air", openGuard<stub_notoriety_spawn_only_air>);
    if (wants("notoriety_restricted_zones_enable")) registerOne(L, state, log, stateTag, "notoriety_restricted_zones_enable", openGuard<stub_notoriety_restricted_zones_enable>);
    if (wants("is_rappelling")) registerOne(L, state, log, stateTag, "is_rappelling", openGuard<stub_is_rappelling>);
    if (wants("ingame_effect_remove_trigger")) registerOne(L, state, log, stateTag, "ingame_effect_remove_trigger", openGuard<stub_ingame_effect_remove_trigger>);
    if (wants("get_vehicle_type")) registerOne(L, state, log, stateTag, "get_vehicle_type", openGuard<stub_get_vehicle_type>);
    if (wants("dlc_gis_film_grain_load")) registerOne(L, state, log, stateTag, "dlc_gis_film_grain_load", openGuard<stub_dlc_gis_film_grain_load>);
    if (wants("debris_flow_add_object")) registerOne(L, state, log, stateTag, "debris_flow_add_object", openGuard<stub_debris_flow_add_object>);
    if (wants("cower")) registerOne(L, state, log, stateTag, "cower", openGuard<stub_cower>);
    if (wants("continuous_spawn_set_orders")) registerOne(L, state, log, stateTag, "continuous_spawn_set_orders", openGuard<stub_continuous_spawn_set_orders>);
    if (wants("continuous_explosion_stop")) registerOne(L, state, log, stateTag, "continuous_explosion_stop", openGuard<stub_continuous_explosion_stop>);
    if (wants("check_animation_state")) registerOne(L, state, log, stateTag, "check_animation_state", openGuard<stub_check_animation_state>);
    if (wants("character_release_human_shield")) registerOne(L, state, log, stateTag, "character_release_human_shield", openGuard<stub_character_release_human_shield>);
    if (wants("boss_battle_kb_enable_crowd_props")) registerOne(L, state, log, stateTag, "boss_battle_kb_enable_crowd_props", openGuard<stub_boss_battle_kb_enable_crowd_props>);
    if (wants("base_jumping_enable")) registerOne(L, state, log, stateTag, "base_jumping_enable", openGuard<stub_base_jumping_enable>);
    if (wants("wind_override_set")) registerOne(L, state, log, stateTag, "wind_override_set", openGuard<stub_wind_override_set>);
}

} // namespace stub_sections
} // namespace sr3luahost
