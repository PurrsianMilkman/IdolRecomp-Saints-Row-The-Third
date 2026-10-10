// lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp
//
// Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60,
// "gameplay A: vehicles/world/spawn/effects/audio/camera" (113 names total
// across this file and its sibling lua_spec_confirmed_stubs_b27_wave2_
// spawn_world_audio.cpp). This file holds the vehicle/helicopter/airplane/
// traffic half (39 names); the other 74 (spawn/world/effects/audio/camera/
// mission-specific) are in b27. See each stub's own doc comment below for
// its exact spec citation; see engine_state.h's own "Wave 2, Batch W2-7"
// doc comments (VehicleState's new fields, plus the EngineState methods
// this file calls - createRoadblock/destroyRoadblock, setRcMaxSignalRange
// ReturningPrevious, etc.) for the shared-state design.
//
// CLEAN-ROOM: every stub below is written ONLY from spec-lua-api-
// behaviour.md's own text (cited per-function) plus this project's
// existing, already-merged EngineState/VehicleState infrastructure - never
// from the game executable or any disassembly directly.
//
// HARDENING POLICY (orchestrator's standing directive) applied in this
// file: vehicle_component_shatter_glass/_is_shattered (null/non-string
// component name reaching the real CRT `_stricmp`, Sec52.2),
// vehicle_detonate_random_parked_car (a failed/absent reference name
// widening the blast to the whole game world in the original - bounded to
// "detonate nothing" here instead), and airplane_fly_to_do (null-target
// `strlen()` before its own null test, Sec58). Each is labelled CHOSEN
// (hardened divergence) at its own stub below with a named per-site
// counter.
//
// TRANSPOSITION RISKS resolved explicitly (per this batch's own dispatch
// note): vehicle_set_kneecappers_damage is kept as its OWN
// VehicleState::kneecappersDamage field, never conflated with the
// already-registered vehicle_set_kneecappers's own kneecappersEnabled/
// kneecappersDeferred (Sec33.1, b03). vehicle_detonate_random_parked_car is
// kept entirely separate from the already-registered vehicle_detonate
// (Sec9.7, b09) - different native, different registry entry, no shared
// state. vehicle_set_radio_controls_locked REUSES the already-established
// Sec30.5 double-gate force-flag family (VehicleState::forceFlags,
// kRadioControlsLockedBit) via the EXISTING applyVehicleForceFlagSetter
// helper (lua_spec_confirmed_stubs_common.h) - the same bit the already-
// registered vehicle_clear_all_radio_locks clears (b03) - NOT a new field.
// vehicle_set_sirens is deliberately kept SEPARATE from the already-
// registered vehicle_set_sirenlights's own siren-LIGHTS bit
// (VehicleState::forceFlags1d7c, Sec33.1) - no individual spec text names
// vehicle_set_sirens, so conflating it with a different, already-specified
// native's bit would be a guess this project's clean-room discipline does
// not allow; it gets its own new VehicleState::sirensOn field instead.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Sec38.1 - vehicle cluster (6 names)
// =======================================================================

// vehicle_anim_playing (Sec38.1, 0x00a62050). 1 string. Returns 1 boolean,
// false if unresolved. CONFIRMED: "true only if the vehicle's body is
// active (+0xbd0==1) and any entry of its animation-set object has
// 'active' set without 'finishing/paused'" (HYPOTHESIS on the exact bit
// meanings) - modelled via the existing bodyActive gate plus the new
// test-settable animSetActiveNotFinishing override.
int stub_vehicle_anim_playing(lua_State* L) {
    logCall(L, upLog(L), "vehicle_anim_playing", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(name)) {
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.bodyActive.known() && v.bodyActive.get() && v.animSetActiveNotFinishing.known()) {
            result = v.animSetActiveNotFinishing.get();
        }
    }
    lua_pushboolean(L, result);
    return 1;
}

// vehicle_delete_all_corpses (Sec38.1, 0x00a63d30). No arguments, no
// return. CONFIRMED: walks the wrecked-vehicle list; a protected class is
// hidden (and its effect handles released) instead of deleted; an
// unprotected one is deleted outright. This project has no real world-
// object-manager walk, so every known vehicle's own wrecked bit is swept.
int stub_vehicle_delete_all_corpses(lua_State* L) {
    logCall(L, upLog(L), "vehicle_delete_all_corpses", upStateTag(L));
    EngineState* st = upState(L);
    for (auto& entry : st->vehicles()) {
        VehicleState& v = entry.second;
        if (!v.wrecked) continue;
        if (v.corpseClassProtected) v.hiddenAsCorpse = true;
        else v.deletedAsCorpse = true;
    }
    return 0;
}

// vehicle_disable_weapon_physics (Sec38.1, 0x00a62330). 1 optional
// boolean, default true (writes the LOGICAL NOT of the argument) - a
// GLOBAL byte, no vehicle name argument at all. CONFIRMED: "once a script
// disables it, it stays disabled for the rest of the process" - a plain
// unconditional overwrite models that (nothing re-enables it on its own).
int stub_vehicle_disable_weapon_physics(lua_State* L) {
    logCall(L, upLog(L), "vehicle_disable_weapon_physics", upStateTag(L));
    bool arg = optionalBoolDefault(L, 1, true);
    upState(L)->weaponPhysicsEnabled() = !arg; // CONFIRMED: stores the logical NOT of the argument
    return 0;
}

// vehicle_engine_start (Sec38.1, 0x00a62380). 1 string + 1 optional
// boolean, default true. No return. CONFIRMED: "an already-running engine
// is stopped first, then restarted - never left alone." Per-class audio
// event on class 4 (VTOL) with an active sub-object (bodyActive) - counted.
int stub_vehicle_engine_start(lua_State* L) {
    logCall(L, upLog(L), "vehicle_engine_start", upStateTag(L));
    std::string name = argString(L, 1);
    bool instantOrSpinUp = optionalBoolDefault(L, 2, true); // HYPOTHESIS meaning, Sec38.1
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0;
    VehicleState& v = st->getOrCreateVehicle(name);
    v.engineRunning = false; // CONFIRMED: stopped first
    v.engineRunning = true;  // CONFIRMED: then restarted, unconditionally
    (void)instantOrSpinUp;   // HYPOTHESIS "start instantly vs spin-up" - no per-frame engine-audio simulation here
    if (v.vehicleClass.known() && v.vehicleClass.get() == 4 && v.bodyActive.known() && v.bodyActive.get()) {
        ++v.engineStartVtolAudioEventCount; // CONFIRMED: class 4 + active sub-object fires an audio event
    }
    return 0;
}

// vehicle_forced_corpse_removal_enabled (Sec38.1, 0x00a624d0). 1 bare
// boolean. No return. CONFIRMED: "writes a global only when a session
// exists and this machine is host" - this host's own single-player
// resolution is "no session" (Sec8.27/Sec26.28 convention), so a real call
// here is the honest no-op; a test can force coopLocalIsHost()-equivalent
// behaviour is NOT modelled as a bypass - the gate is applied exactly as
// every other host-gated setter in this project already does.
int stub_vehicle_forced_corpse_removal_enabled(lua_State* L) {
    logCall(L, upLog(L), "vehicle_forced_corpse_removal_enabled", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0; // CONFIRMED: bare boolean, no nil-gate
    EngineState* st = upState(L);
    if (st->coopLocalIsHost()) { // CONFIRMED: "only when a session exists and this machine is host"
        st->vehicleForcedCorpseRemovalEnabled() = flag;
    }
    return 0;
}

// vehicle_hidden (Sec38.1, 0x00a62610). 1 string. 1 boolean. CONFIRMED:
// "an unresolved name answers true." Reuses the EXISTING VehicleState::
// hidden bit vehicle_show/vehicle_hide already model (SAME bit per
// Sec38.1's own cross-reference) - no new field.
int stub_vehicle_hidden(lua_State* L) {
    logCall(L, upLog(L), "vehicle_hidden", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = true; // CONFIRMED: unresolved -> true
    if (st->objectResolves().get(name)) result = st->getOrCreateVehicle(name).hidden;
    lua_pushboolean(L, result);
    return 1;
}

// =======================================================================
// Sec46.5 - direct, unreplicated vehicle field setters (6 names: kneecappers
// _damage/npc_engine_audio/obey_traffic_lights[AGG]/radio_controls_locked
// [AGG]/sirens/use_short_cuts[AGG])
// =======================================================================

// vehicle_set_kneecappers_damage (Sec46.5). 1 vehicle name + 1 number
// (CHOSEN shape; thin spec beyond "direct, unreplicated field write with
// no ownership gate"). Deliberately a DIFFERENT field from the already-
// registered vehicle_set_kneecappers's own kneecappersEnabled/
// kneecappersDeferred (Sec33.1) - see this file's own top comment.
int stub_vehicle_set_kneecappers_damage(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_kneecappers_damage", upStateTag(L));
    std::string name = argString(L, 1);
    double damage = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).kneecappersDamage = damage;
    return 0;
}

// vehicle_set_npc_engine_audio (Sec46.5). 1 vehicle name + 1 boolean
// (CHOSEN shape; thin spec). Direct, unreplicated field write.
int stub_vehicle_set_npc_engine_audio(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_npc_engine_audio", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).npcEngineAudioEnabled = flag;
    return 0;
}

// vehicle_set_obey_traffic_lights (Sec4.5a, closed 2026-10-09 wave 10,
// 0x00a63110). 1 vehicle name + optional boolean, default true (both real
// call sites, dlc2_m01.lua only, pass an explicit boolean - the omitted-
// argument default is OPEN but moot in practice per spec). CONFIRMED
// member of the SAME vai_force_flags vehicle-AI bit cluster (+0xc68) as
// Sec4.5's vehicle_suppress_npc_exit: bit 0x01 of sub-record byte +0x4
// (VehicleState::vehicleAiForceFlagsByte4, joining kNoStopEnterWithDriverBit/
// kForceNoPassengerExitBit/kIgnoreRepulsorsBit already on that byte) - the
// SAME double-gate record-and-replicate idiom, not a thin/CHOSEN field.
int stub_vehicle_set_obey_traffic_lights(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_obey_traffic_lights", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.vehicleAiForceFlagsByte4.setBits(VehicleState::kObeyTrafficLightsBit,
                                            flag ? VehicleState::kObeyTrafficLightsBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_set_obey_traffic_lights", name);
    }
    return 0;
}

// vehicle_set_radio_controls_locked (Sec46.5, AGG - but see this file's
// own top comment: this bit is already FULLY specified at Sec30.5, the
// SAME +0x1d7c bit 0x4 the already-registered vehicle_clear_all_radio_
// locks (b03) clears). 1 vehicle name + 1 boolean. Reuses the EXISTING
// double-gate force-flag setter shape directly (applyVehicleForceFlagSetter,
// VehicleState::kRadioControlsLockedBit) - NOT a thin/CHOSEN field.
int stub_vehicle_set_radio_controls_locked(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_radio_controls_locked", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    applyVehicleForceFlagSetter(L, "vehicle_set_radio_controls_locked", name, flag, VehicleState::kRadioControlsLockedBit);
    return 0;
}

// vehicle_set_sirens (Sec46.5, cluster-only text, no individual paragraph).
// 1 vehicle name + optional boolean, default true (CHOSEN shape). Its OWN
// new field (sirensOn) - see this file's own top comment for why this is
// NOT the same bit as the already-registered vehicle_set_sirenlights.
int stub_vehicle_set_sirens(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_sirens", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).sirensOn = flag;
    return 0;
}

// vehicle_set_use_short_cuts (Sec4.5a, closed 2026-10-09 wave 10,
// 0x00a63490). Same 2-slot argument shape as vehicle_set_obey_traffic_
// lights above (both real call sites, dlc3_m02.lua only, pass an explicit
// boolean). CONFIRMED member of the SAME vai_force_flags cluster, but a
// byte not previously catalogued anywhere in this family: bit 0x20 of
// sub-record byte +0x6 (absolute vehicle +0xc6e, VehicleState::
// vehicleAiForceFlagsByte6/kUseShortCutsBit) - same double-gate record-
// and-replicate idiom, not a thin/CHOSEN field.
int stub_vehicle_set_use_short_cuts(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_use_short_cuts", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.vehicleAiForceFlagsByte6.setBits(VehicleState::kUseShortCutsBit,
                                            flag ? VehicleState::kUseShortCutsBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_set_use_short_cuts", name);
    }
    return 0;
}

// =======================================================================
// Sec52.2 - vehicle cluster (9 names)
// =======================================================================

// vehicle_allow_ownership_transfer (Sec52.2). "Resolved in full, no
// further crash shapes" - 1 vehicle name + optional boolean default true
// (CHOSEN shape).
int stub_vehicle_allow_ownership_transfer(lua_State* L) {
    logCall(L, upLog(L), "vehicle_allow_ownership_transfer", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).ownershipTransferAllowed = flag;
    return 0;
}

// Shared HARDENED component-name guard for vehicle_component_is_shattered/
// _shatter_glass (Sec52.2): CONFIRMED "a nil or non-string component name
// passes a null pointer straight into the C runtime's _stricmp... fatal
// under the stock invalid-parameter handler." CHOSEN (hardened
// divergence): this host refuses (treats as "not found"/false) instead of
// reading a non-string Lua value as a C string, counted via
// VehicleState::componentNullNameGuardCount.
bool vehicleComponentNameGuarded(lua_State* L, int idx, EngineState* st, VehicleState& v, std::string* outName) {
    if (!lua_isstring(L, idx)) { // covers nil/boolean/table/none; lua_isstring also accepts numbers (coercible), matching
                                 // the real CRT _stricmp's own tolerance for a non-null C string either way
        ++v.componentNullNameGuardCount;
        (void)st;
        return false;
    }
    *outName = argString(L, idx);
    return true;
}

// vehicle_component_is_shattered (Sec52.2). 1 vehicle + 1 component name.
// Returns 1 boolean.
int stub_vehicle_component_is_shattered(lua_State* L) {
    logCall(L, upLog(L), "vehicle_component_is_shattered", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(vehicleName)) {
        VehicleState& v = st->getOrCreateVehicle(vehicleName);
        std::string component;
        if (vehicleComponentNameGuarded(L, 2, st, v, &component)) {
            auto it = v.componentShattered.find(component);
            result = it != v.componentShattered.end() && it->second;
        }
    }
    lua_pushboolean(L, result);
    return 1;
}

// vehicle_component_shatter_glass (Sec52.2). 1 vehicle + 1 component name.
// No return. CONFIRMED: "not glass-specific at all - it applies the
// 'shattered' bit and effect to any named component on the vehicle."
int stub_vehicle_component_shatter_glass(lua_State* L) {
    logCall(L, upLog(L), "vehicle_component_shatter_glass", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(vehicleName)) {
        VehicleState& v = st->getOrCreateVehicle(vehicleName);
        std::string component;
        if (vehicleComponentNameGuarded(L, 2, st, v, &component)) {
            v.componentShattered[component] = true; // CONFIRMED: any named component, not glass-specific
        }
    }
    return 0;
}

// vehicle_detonate_random_parked_car (Sec52.2). Optional reference-object
// name + angle/distance window (not modelled - no coordinate system).
// CONFIRMED: "not random... deterministically detonates the first
// qualifying parked vehicle in world-list order," and "a reference-object
// name that fails to resolve takes exactly the same code path as passing
// no reference at all... blows up the first parked car ANYWHERE in the
// game world." CHOSEN (hardened divergence, per this batch's own explicit
// instruction): this host has no real world-vehicle-list to pick a "first
// qualifying" target from in EITHER case, and bounds the blast scope by
// refusing to detonate anything at all unless a reference name is BOTH
// given and resolves to a real vehicle this host already knows about -
// never widening to "the whole world." Counted via a named guard distinct
// from the ordinary "no reference given" case (which this host also
// refuses, for the identical no-world-list reason, NOT reproduced as a
// deterministic pick).
//
// Resolved-reference case (reference name given AND resolves): the spec's
// own Sec52.2 text is explicit that the reference object is a LOCATION
// ANCHOR for an angle/distance window, not the detonation target itself -
// "calling it with only a reference name and the default (all-zero)
// angle/distance window effectively detonates nothing, since the default
// window only admits a vehicle at exactly zero distance and zero angle."
// SPEC-CONFIRMED: this host therefore correctly detonates NOTHING in this
// case - it must NOT mutate the reference object itself (it is not the
// target). Counted via its own distinct no-op counter.
int stub_vehicle_detonate_random_parked_car(lua_State* L) {
    logCall(L, upLog(L), "vehicle_detonate_random_parked_car", upStateTag(L));
    EngineState* st = upState(L);
    int argc = lua_gettop(L);
    if (argc < 1 || lua_type(L, 1) != LUA_TSTRING) {
        ++st->vehicleDetonateRandomNoReferenceGuardCount(); // CHOSEN hardened divergence: refuse rather than pick "first in world"
        return 0;
    }
    std::string referenceName = argString(L, 1);
    if (!st->objectResolves().known(referenceName) || !st->objectResolves().get(referenceName)) {
        ++st->vehicleDetonateRandomFailedReferenceGuardCount(); // CHOSEN (hardened divergence): bounds the blast
                                                                 // scope instead of reproducing the real
                                                                 // world-wide-blast defect (Sec52.2)
        return 0;
    }
    // SPEC-CONFIRMED (Sec52.2): the reference object is a location anchor,
    // not the detonation target - the default all-zero window detonates
    // nothing, so this leaves the resolved reference untouched.
    ++st->vehicleDetonateRandomResolvedReferenceNoOpCount();
    return 0;
}

// vehicle_evacuate (Sec52.2). "Resolved in full, no further crash shapes
// beyond discarding its own occupant-liveness check result." No occupant
// model in this project - only the gated call itself is counted.
int stub_vehicle_evacuate(lua_State* L) {
    logCall(L, upLog(L), "vehicle_evacuate", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) ++st->getOrCreateVehicle(name).evacuateCount;
    return 0;
}

// vehicle_flee (Sec52.2). CONFIRMED: "drives AI mode 2 (flee)... a
// player-driven vehicle never flees" (modelled veto); the "AI mode/
// distance veto" is CONFIRMED permanently dead (not modelled, per spec's
// own exhaustive reference check).
int stub_vehicle_flee(lua_State* L) {
    logCall(L, upLog(L), "vehicle_flee", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0;
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.playerDriven.known() && v.playerDriven.get()) return 0; // CONFIRMED veto: never flees
    v.fleeing = true;
    v.aiMode = 2; // CONFIRMED: AI mode 2 = flee
    return 0;
}

// vehicle_get_max_speed (Sec52.2). CONFIRMED: "returns miles per hour...
// and -1 for an unresolved vehicle." The writer of +0x1d84 is itself a
// residual OPEN item per spec's own closing list - a resolved vehicle with
// no known max speed honestly refuses (OpenStateError) rather than
// fabricating a number.
int stub_vehicle_get_max_speed(lua_State* L) {
    logCall(L, upLog(L), "vehicle_get_max_speed", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) {
        lua_pushnumber(L, -1);
        return 1;
    }
    lua_pushnumber(L, st->getOrCreateVehicle(name).maxSpeedMph.get());
    return 1;
}

// vehicle_in_air / vehicle_is_flipped (Sec52.2). "Resolved in full, no
// further crash shapes" - no physics model in this project, OPEN until a
// test sets either flag for a resolved vehicle.
int stub_vehicle_in_air(lua_State* L) {
    logCall(L, upLog(L), "vehicle_in_air", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(name)) result = st->getOrCreateVehicle(name).inAir.get();
    lua_pushboolean(L, result);
    return 1;
}

int stub_vehicle_is_flipped(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_flipped", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(name)) result = st->getOrCreateVehicle(name).flipped.get();
    lua_pushboolean(L, result);
    return 1;
}

// vault_heli_hack_for_m01 (Sec52.2). "A narrow, mission-1-specific
// component-index cache setter" - no further text. CHOSEN shape: 1
// vehicle name + 1 number (component index), no return.
int stub_vault_heli_hack_for_m01(lua_State* L) {
    logCall(L, upLog(L), "vault_heli_hack_for_m01", upStateTag(L));
    std::string name = argString(L, 1);
    int32_t index = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 2)));
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).vaultHeliComponentIndexCache = index;
    return 0;
}

// =======================================================================
// Sec41.5/41.6/53.2 - helicopter cluster (8 names, incl. get_vehicle_speed)
// =======================================================================

// get_vehicle_speed (Sec41.6). 1 name (vehicle, character, or a third
// object kind). Returns 1 integer speed, 0 if nothing resolves. The
// third-tier "unchecked virtual result" crash shape (Sec41.6) needs real
// per-object-kind dispatch this project's flat objectResolves() does not
// distinguish - only the vehicle-resolve path is modelled; resolution
// without a known speed value honestly refuses (OpenStateError).
int stub_get_vehicle_speed(lua_State* L) {
    logCall(L, upLog(L), "get_vehicle_speed", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    int32_t speed = 0;
    if (st->objectResolves().get(name)) {
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.bodyActive.known() && v.bodyActive.get()) speed = v.speedMph.get();
    }
    lua_pushnumber(L, speed);
    return 1;
}

// helicopter_set_dont_use_constraints (Sec41.5). Vehicle (or character)
// name + boolean. No return. CONFIRMED: "no helicopter-kind check
// anywhere - accepts any vehicle." The rebuild-on-second-bit path is OPEN
// (HYPOTHESIS: rotor-constraint data) - counted only when a test sets the
// gate true.
int stub_helicopter_set_dont_use_constraints(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_dont_use_constraints", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0;
    VehicleState& v = st->getOrCreateVehicle(name);
    v.dontUseConstraints = flag;
    if (v.constraintRebuildBit.known() && v.constraintRebuildBit.get()) ++v.constraintRebuildCount;
    return 0;
}

// helicopter_clear_max_bank_angle (Sec53.2). 1 vehicle name, no return.
// CONFIRMED: "only clears the 'override enabled' bit and leaves the
// previously-set bank angle value in storage untouched." Accepts any
// resolved vehicle (no AI-mode check, per Sec53.2's own text).
int stub_helicopter_clear_max_bank_angle(lua_State* L) {
    logCall(L, upLog(L), "helicopter_clear_max_bank_angle", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).maxBankAngleOverrideEnabled = false; // angle untouched, CONFIRMED
    return 0;
}

// helicopter_land (Sec53.2). CONFIRMED: "returns its success boolean after
// only the cheap preconditions pass" (an optimistic true) - the real
// >200-unit distance veto needs geometry this project does not have
// (VehicleState::landDistanceGatePasses, OPEN until a test sets it).
int stub_helicopter_land(lua_State* L) {
    logCall(L, upLog(L), "helicopter_land", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name);
    if (resolved) {
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.landDistanceGatePasses.known() && v.landDistanceGatePasses.get()) v.landed = true;
    }
    lua_pushboolean(L, resolved); // CONFIRMED: optimistic true after only the cheap precondition (resolve)
    return 1;
}

// helicopter_enter_dropoff (Sec53.2). CONFIRMED: "fewer than 2 live
// occupants [vetoes it] unless explicitly overridden" - an optimistic
// true, same shape as helicopter_land above. "#PLAYER#" sentinel for the
// drop-off target is CONFIRMED to resolve to "whichever player is closest
// to the drop-off point, not necessarily the local player" - this project
// has no coordinate system to pick among players, so CHOSEN (minimal,
// HYPOTHESIS): "#PLAYER#" resolves to the local player's own name when one
// exists (this project's only modelled player), else the call is treated
// as an unresolved target.
int stub_helicopter_enter_dropoff(lua_State* L) {
    logCall(L, upLog(L), "helicopter_enter_dropoff", upStateTag(L));
    std::string name = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "");
    bool overrideOccupancy = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name);
    if (resolved) {
        VehicleState& v = st->getOrCreateVehicle(name);
        bool occupancyOk = overrideOccupancy || (v.dropoffOccupancyGatePasses.known() && v.dropoffOccupancyGatePasses.get());
        if (occupancyOk) v.dropoffRequested = true;
    }
    (void)target; // CHOSEN: "#PLAYER#"'s own closest-player resolution needs geometry this project does not have
    lua_pushboolean(L, resolved); // CONFIRMED: optimistic true after only the cheap precondition (resolve)
    return 1;
}

// helicopter_set_dont_explode_in_air / helicopter_set_dont_move_in_combat /
// helicopter_set_large_dspiral (Sec53.2). Vehicle + boolean, no return.
// CONFIRMED: "accept any resolved vehicle, not just a helicopter - none of
// them actually checks the vehicle's AI mode before writing its flag bit."
int stub_helicopter_set_dont_explode_in_air(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_dont_explode_in_air", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).dontExplodeInAir = flag;
    return 0;
}

int stub_helicopter_set_dont_move_in_combat(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_dont_move_in_combat", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).dontMoveInCombat = flag;
    return 0;
}

int stub_helicopter_set_large_dspiral(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_large_dspiral", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).largeDspiral = flag;
    return 0;
}

// =======================================================================
// Sec39.2 - roadblocks/RC signal/spotlight (5 names)
// =======================================================================

// rc_set_max_signal_range (Sec39.2, RT 1). 1 number, no nil-gate (absent
// reads 0 via lua_tonumber's own semantics). CONFIRMED: "returns the
// PREVIOUS range." The real start-up default is OPEN (not traced) - the
// first real call honestly throws via EngineState::
// setRcMaxSignalRangeReturningPrevious's own OpenValue::get().
int stub_rc_set_max_signal_range(lua_State* L) {
    logCall(L, upLog(L), "rc_set_max_signal_range", upStateTag(L));
    double newRange = lua_tonumber(L, 1); // CONFIRMED: no nil-gate
    double previous = upState(L)->setRcMaxSignalRangeReturningPrevious(newRange);
    lua_pushnumber(L, previous);
    return 1;
}

// roadblock_create (Sec39.2, RT 26 - this batch's own top runtime hit).
// 2 strings (template name, placement object). Returns exactly 1 number:
// the handle as a double, or -1 on any failure.
int stub_roadblock_create(lua_State* L) {
    logCall(L, upLog(L), "roadblock_create", upStateTag(L));
    std::string templateName = argString(L, 1);
    std::string placementObjectName = argString(L, 2);
    int64_t handle = upState(L)->createRoadblock(templateName, placementObjectName);
    lua_pushnumber(L, static_cast<double>(handle));
    return 1;
}

// roadblock_destroy (Sec39.2). 1 number (the handle). No return. CONFIRMED:
// "the full 64-bit form of 0x00ea2596" - this host's truncateEa2596
// already always keeps the full int64_t range, so no separate "wide" path
// is needed here (same helper, same result).
int stub_roadblock_destroy(lua_State* L) {
    logCall(L, upLog(L), "roadblock_destroy", upStateTag(L));
    int64_t handle = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->destroyRoadblock(handle);
    return 0;
}

// spotlight_heli_laser_size (Sec39.2). 1 number, no nil-gate. No return.
// CONFIRMED default 1.0; "no clamp, no replication, no host reset found
// anywhere."
int stub_spotlight_heli_laser_size(lua_State* L) {
    logCall(L, upLog(L), "spotlight_heli_laser_size", upStateTag(L));
    upState(L)->spotlightHeliLaserSize() = lua_tonumber(L, 1);
    return 0;
}

// =======================================================================
// Sec46.4 - vtol_flyby_animation_for_m18 (1 name)
// =======================================================================

// vtol_flyby_animation_for_m18 (Sec46.4). CHOSEN shape: 1 vehicle name,
// no return. CONFIRMED: "a one-off mission-18 helper (teleport + animation
// + an unconditionally broadcast network record with no host or ownership
// gate)" - no teleport/animation system modelled beyond the gated call
// and the unconditional broadcast.
int stub_vtol_flyby_animation_for_m18(lua_State* L) {
    logCall(L, upLog(L), "vtol_flyby_animation_for_m18", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) st->getOrCreateVehicle(name).vtolFlybyM18Played = true;
    ++st->vtolFlybyM18BroadcastCount(); // CONFIRMED: unconditional, no host/ownership gate
    return 0;
}

// =======================================================================
// Sec54.3 - is_a_vehicle (1 name)
// =======================================================================

// is_a_vehicle (Sec54.3). "Resolved in full, each with its own confirmed
// 'unknown input' sentinel value... false" - reuses hasVehicle() directly,
// no new state.
int stub_is_a_vehicle(lua_State* L) {
    logCall(L, upLog(L), "is_a_vehicle", upStateTag(L));
    std::string name = argString(L, 1);
    lua_pushboolean(L, upState(L)->hasVehicle(name));
    return 1;
}

// =======================================================================
// Sec58/Sec59.7 - airplanes (3 names)
// =======================================================================

// airplane_takeoff_do (Sec59.7). "Reports whether the take-off maneuver
// actually started (locally, or was successfully forwarded to a remote
// owner) and resolves in full with no crash shape." 1 vehicle name,
// returns 1 boolean (= resolve success, this project's minimal honest
// reading - no further internal detail given).
int stub_airplane_takeoff_do(lua_State* L) {
    logCall(L, upLog(L), "airplane_takeoff_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name);
    if (resolved) st->airplanes()[name].takeoffRequested = true;
    lua_pushboolean(L, resolved);
    return 1;
}

// airplane_land_do (Sec59.7) - a real return-value trap. 2 name arguments.
// CONFIRMED: "always pushes true once its two name arguments resolve,
// regardless of what the underlying landing routine itself reports."
int stub_airplane_land_do(lua_State* L) {
    logCall(L, upLog(L), "airplane_land_do", upStateTag(L));
    std::string name = argString(L, 1);
    std::string target = argString(L, 2);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name) && st->objectResolves().get(target);
    lua_pushboolean(L, resolved); // CONFIRMED: pushes true regardless of actual landing outcome, once both resolve
    return 1;
}

// airplane_fly_to_do (Sec58) - CRASH. CONFIRMED: "the navpoint form can
// never actually work in the original (one waypoint given, but at least
// two required) - so it always returns false." That is FAITHFUL behaviour,
// reproduced directly, not fixed. HARDENED (host-safety, CHOSEN divergence):
// "the target-name argument is measured for its length before being
// checked for null" - this host refuses (treats as unresolved) a non-
// string target instead of reading it as a C string, counted via
// EngineState::airplaneFlyToNullTargetGuardCount(). Whether the target
// resolves as a "path"-kind object (reusing pathObjectIsPath(), Sec42.6)
// decides which of the two forms applies; the path form's own real
// multi-waypoint flight logic is OPEN (not independently given beyond the
// navpoint form's own failure) - CHOSEN minimal: true only if the vehicle
// resolves (a crewed-airplane check this project cannot perform without a
// real vehicle-type table), else false.
int stub_airplane_fly_to_do(lua_State* L) {
    logCall(L, upLog(L), "airplane_fly_to_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!lua_isstring(L, 2)) {
        ++st->airplaneFlyToNullTargetGuardCount(); // HARDENED: refuse the non-string strlen() crash instead of reproducing it
        lua_pushboolean(L, false);
        return 1;
    }
    std::string target = argString(L, 2);
    EngineState::AirplaneRecord& rec = st->airplanes()[name];
    bool isPathForm = st->pathObjectIsPath().known(target) && st->pathObjectIsPath().get(target);
    if (!isPathForm) {
        // CONFIRMED FAITHFUL: the navpoint form always fails (fewer than
        // 2 waypoints than the shared flight-request routine requires).
        // It also leaks the allocated request id and supersedes/cancels
        // any in-progress path flight for this vehicle - counted, not
        // simulated in detail (no real per-vehicle flight-request slot).
        if (rec.pathFlightInProgress) {
            rec.pathFlightInProgress = false; // CONFIRMED: silently cancels a still-flying path flight
        }
        ++st->airplaneFlyToLeakedRequestCount(); // CONFIRMED: the id is allocated then never released on this path
        lua_pushboolean(L, false);
        return 1;
    }
    bool resolved = st->objectResolves().get(name); // CHOSEN minimal: path form's own flight success needs a
                                                     // crewed-airplane/vehicle-type table this project does not have
    lua_pushboolean(L, resolved);
    return 1;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB26() {
    static const std::vector<std::string> names = {
        // Wave 2, Batch W2-7 (2026-10-08): vehicles/helicopter/airplane/
        // traffic half of spec-lua-api-behaviour.md Sec38-60 (39 of 113
        // names - see lua_spec_confirmed_stubs_b27_wave2_spawn_world_
        // audio.cpp for the other 74).
        "vehicle_anim_playing",
        "vehicle_delete_all_corpses",
        "vehicle_disable_weapon_physics",
        "vehicle_engine_start",
        "vehicle_forced_corpse_removal_enabled",
        "vehicle_hidden",
        "vehicle_set_kneecappers_damage",
        "vehicle_set_npc_engine_audio",
        "vehicle_set_obey_traffic_lights",
        "vehicle_set_radio_controls_locked",
        "vehicle_set_sirens",
        "vehicle_set_use_short_cuts",
        "vehicle_allow_ownership_transfer",
        "vehicle_component_is_shattered",
        "vehicle_component_shatter_glass",
        "vehicle_detonate_random_parked_car",
        "vehicle_evacuate",
        "vehicle_flee",
        "vehicle_get_max_speed",
        "vehicle_in_air",
        "vehicle_is_flipped",
        "vault_heli_hack_for_m01",
        "get_vehicle_speed",
        "helicopter_set_dont_use_constraints",
        "helicopter_clear_max_bank_angle",
        "helicopter_enter_dropoff",
        "helicopter_land",
        "helicopter_set_dont_explode_in_air",
        "helicopter_set_dont_move_in_combat",
        "helicopter_set_large_dspiral",
        "rc_set_max_signal_range",
        "roadblock_create",
        "roadblock_destroy",
        "spotlight_heli_laser_size",
        "vtol_flyby_animation_for_m18",
        "is_a_vehicle",
        "airplane_land_do",
        "airplane_takeoff_do",
        "airplane_fly_to_do",
    };
    return names;
}

void registerB26(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("vehicle_anim_playing")) registerOne(L, state, log, stateTag, "vehicle_anim_playing", openGuard<stub_vehicle_anim_playing>);
    if (wants("vehicle_delete_all_corpses")) registerOne(L, state, log, stateTag, "vehicle_delete_all_corpses", openGuard<stub_vehicle_delete_all_corpses>);
    if (wants("vehicle_disable_weapon_physics")) registerOne(L, state, log, stateTag, "vehicle_disable_weapon_physics", openGuard<stub_vehicle_disable_weapon_physics>);
    if (wants("vehicle_engine_start")) registerOne(L, state, log, stateTag, "vehicle_engine_start", openGuard<stub_vehicle_engine_start>);
    if (wants("vehicle_forced_corpse_removal_enabled")) registerOne(L, state, log, stateTag, "vehicle_forced_corpse_removal_enabled", openGuard<stub_vehicle_forced_corpse_removal_enabled>);
    if (wants("vehicle_hidden")) registerOne(L, state, log, stateTag, "vehicle_hidden", openGuard<stub_vehicle_hidden>);
    if (wants("vehicle_set_kneecappers_damage")) registerOne(L, state, log, stateTag, "vehicle_set_kneecappers_damage", openGuard<stub_vehicle_set_kneecappers_damage>);
    if (wants("vehicle_set_npc_engine_audio")) registerOne(L, state, log, stateTag, "vehicle_set_npc_engine_audio", openGuard<stub_vehicle_set_npc_engine_audio>);
    if (wants("vehicle_set_obey_traffic_lights")) registerOne(L, state, log, stateTag, "vehicle_set_obey_traffic_lights", openGuard<stub_vehicle_set_obey_traffic_lights>);
    if (wants("vehicle_set_radio_controls_locked")) registerOne(L, state, log, stateTag, "vehicle_set_radio_controls_locked", openGuard<stub_vehicle_set_radio_controls_locked>);
    if (wants("vehicle_set_sirens")) registerOne(L, state, log, stateTag, "vehicle_set_sirens", openGuard<stub_vehicle_set_sirens>);
    if (wants("vehicle_set_use_short_cuts")) registerOne(L, state, log, stateTag, "vehicle_set_use_short_cuts", openGuard<stub_vehicle_set_use_short_cuts>);
    if (wants("vehicle_allow_ownership_transfer")) registerOne(L, state, log, stateTag, "vehicle_allow_ownership_transfer", openGuard<stub_vehicle_allow_ownership_transfer>);
    if (wants("vehicle_component_is_shattered")) registerOne(L, state, log, stateTag, "vehicle_component_is_shattered", openGuard<stub_vehicle_component_is_shattered>);
    if (wants("vehicle_component_shatter_glass")) registerOne(L, state, log, stateTag, "vehicle_component_shatter_glass", openGuard<stub_vehicle_component_shatter_glass>);
    if (wants("vehicle_detonate_random_parked_car")) registerOne(L, state, log, stateTag, "vehicle_detonate_random_parked_car", openGuard<stub_vehicle_detonate_random_parked_car>);
    if (wants("vehicle_evacuate")) registerOne(L, state, log, stateTag, "vehicle_evacuate", openGuard<stub_vehicle_evacuate>);
    if (wants("vehicle_flee")) registerOne(L, state, log, stateTag, "vehicle_flee", openGuard<stub_vehicle_flee>);
    if (wants("vehicle_get_max_speed")) registerOne(L, state, log, stateTag, "vehicle_get_max_speed", openGuard<stub_vehicle_get_max_speed>);
    if (wants("vehicle_in_air")) registerOne(L, state, log, stateTag, "vehicle_in_air", openGuard<stub_vehicle_in_air>);
    if (wants("vehicle_is_flipped")) registerOne(L, state, log, stateTag, "vehicle_is_flipped", openGuard<stub_vehicle_is_flipped>);
    if (wants("vault_heli_hack_for_m01")) registerOne(L, state, log, stateTag, "vault_heli_hack_for_m01", openGuard<stub_vault_heli_hack_for_m01>);
    if (wants("get_vehicle_speed")) registerOne(L, state, log, stateTag, "get_vehicle_speed", openGuard<stub_get_vehicle_speed>);
    if (wants("helicopter_set_dont_use_constraints")) registerOne(L, state, log, stateTag, "helicopter_set_dont_use_constraints", openGuard<stub_helicopter_set_dont_use_constraints>);
    if (wants("helicopter_clear_max_bank_angle")) registerOne(L, state, log, stateTag, "helicopter_clear_max_bank_angle", openGuard<stub_helicopter_clear_max_bank_angle>);
    if (wants("helicopter_enter_dropoff")) registerOne(L, state, log, stateTag, "helicopter_enter_dropoff", openGuard<stub_helicopter_enter_dropoff>);
    if (wants("helicopter_land")) registerOne(L, state, log, stateTag, "helicopter_land", openGuard<stub_helicopter_land>);
    if (wants("helicopter_set_dont_explode_in_air")) registerOne(L, state, log, stateTag, "helicopter_set_dont_explode_in_air", openGuard<stub_helicopter_set_dont_explode_in_air>);
    if (wants("helicopter_set_dont_move_in_combat")) registerOne(L, state, log, stateTag, "helicopter_set_dont_move_in_combat", openGuard<stub_helicopter_set_dont_move_in_combat>);
    if (wants("helicopter_set_large_dspiral")) registerOne(L, state, log, stateTag, "helicopter_set_large_dspiral", openGuard<stub_helicopter_set_large_dspiral>);
    if (wants("rc_set_max_signal_range")) registerOne(L, state, log, stateTag, "rc_set_max_signal_range", openGuard<stub_rc_set_max_signal_range>);
    if (wants("roadblock_create")) registerOne(L, state, log, stateTag, "roadblock_create", openGuard<stub_roadblock_create>);
    if (wants("roadblock_destroy")) registerOne(L, state, log, stateTag, "roadblock_destroy", openGuard<stub_roadblock_destroy>);
    if (wants("spotlight_heli_laser_size")) registerOne(L, state, log, stateTag, "spotlight_heli_laser_size", openGuard<stub_spotlight_heli_laser_size>);
    if (wants("vtol_flyby_animation_for_m18")) registerOne(L, state, log, stateTag, "vtol_flyby_animation_for_m18", openGuard<stub_vtol_flyby_animation_for_m18>);
    if (wants("is_a_vehicle")) registerOne(L, state, log, stateTag, "is_a_vehicle", openGuard<stub_is_a_vehicle>);
    if (wants("airplane_land_do")) registerOne(L, state, log, stateTag, "airplane_land_do", openGuard<stub_airplane_land_do>);
    if (wants("airplane_takeoff_do")) registerOne(L, state, log, stateTag, "airplane_takeoff_do", openGuard<stub_airplane_takeoff_do>);
    if (wants("airplane_fly_to_do")) registerOne(L, state, log, stateTag, "airplane_fly_to_do", openGuard<stub_airplane_fly_to_do>);
}

} // namespace stub_sections
} // namespace sr3luahost
