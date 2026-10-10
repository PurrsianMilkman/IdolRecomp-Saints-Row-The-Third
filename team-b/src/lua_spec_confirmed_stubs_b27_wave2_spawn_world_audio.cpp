// lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp
//
// Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60,
// "gameplay A: vehicles/world/spawn/effects/audio/camera" (113 names total
// across this file and its sibling lua_spec_confirmed_stubs_b26_wave2_
// vehicles_helicopter.cpp). This file holds the spawn/world/effects/audio/
// camera/mission-specific half (74 names); the other 39 (vehicle/
// helicopter/airplane/traffic) are in b26. See each stub's own doc comment
// below for its exact spec citation; see engine_state.h's own "Wave 2,
// Batch W2-7" doc comments for the shared-state design this file reads/
// writes.
//
// CLEAN-ROOM: every stub below is written ONLY from spec-lua-api-
// behaviour.md's own text (cited per-function) plus this project's
// existing, already-merged EngineState infrastructure - never from the
// game executable or any disassembly directly. Two names in this batch
// have NO spec text anywhere this project can find (checked directly
// against both spec-lua-api-behaviour.md and spec-lua-bindings.md):
//   - `flee` (distinct from the documented flee_to_navpoint/vehicle_flee/
//     action_nodes_shouldnt_flee) - refused honestly as OPEN below, per
//     this project's standing no-invented-fixes policy, rather than
//     guessed at.
//   - `assert_screen_is_faded_out` IS fully resolved, just not inside its
//     own tranche write-up: Sec35.1 names it directly as "the no-op
//     assertion assert_screen_is_faded_out (0x007c9f50, already catalogued
//     elsewhere in this document)" - the SAME shared no-op stub
//     debug_print/assert_msg/game_show_party_ui already use (lua_bare_
//     globals.cpp's own bare_debug_print/bare_assert_msg). Implemented as
//     the confirmed no-op below; if the orchestrator's own "Team A's §2.3
//     answer" note refers to something beyond this, it was not found
//     anywhere in the merged spec text this task's clean-room boundary
//     allows reading - flagged in this batch's own report, not guessed at.
//
// HARDENING POLICY (orchestrator's standing directive) applied in this
// file: debris_flow_set_inactive (null virtual call through a stale
// object handle, Sec45.2), dlc2_m02_clapboards_set (the same signed-index
// bounding scheme as its already-registered _get/_reset siblings,
// MEMSAFE, Sec55.5), the audio_play_* family's uninitialized-stack-value
// shape on an uninitialized audio system (Sec59.6) and audio_play_for_
// character_weapon_do's null-character read (Sec59.6), force_throw_char_do
// (derived-pointer-before-null-test, Sec55.4), boss_battle_matt_qte_begin
// (three independent crash paths, Sec59.5), los_check_do and
// airplane_fly_to_do's own null-target shapes are the OTHER file's own
// concern where relevant. Each is labelled CHOSEN (hardened divergence)
// at its own stub below with a named per-site counter.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <algorithm>

namespace sr3luahost {
namespace {

// =======================================================================
// assert_screen_is_faded_out (Sec35.1, RT 7, AGG) - see this file's own
// top comment for the full "already resolved" reasoning.
// =======================================================================
int stub_assert_screen_is_faded_out(lua_State* L) {
    logCall(L, upLog(L), "assert_screen_is_faded_out", upStateTag(L));
    return 0; // CONFIRMED: the shared no-op stub 0x007c9f50 (Sec35.1)
}

// =======================================================================
// flee (§45/T13) - NO SPEC TEXT FOUND anywhere in spec-lua-api-behaviour.md
// or spec-lua-bindings.md for a native literally named "flee" (checked
// directly, including a dedicated search of tranche 13's own §45.1-§45.6).
// Honest OPEN refusal per this project's no-invented-fixes policy, rather
// than guessing it is an alias of flee_to_navpoint/vehicle_flee/
// action_nodes_shouldnt_flee (each of which IS documented, separately, and
// already registered or implemented elsewhere under its own distinct name).
// =======================================================================
int stub_flee(lua_State* L) {
    logCall(L, upLog(L), "flee", upStateTag(L));
    throw OpenStateError("flee",
                          "no entry for a native literally named 'flee' anywhere in spec-lua-api-behaviour.md "
                          "or spec-lua-bindings.md (checked directly) - flee_to_navpoint/vehicle_flee/"
                          "action_nodes_shouldnt_flee are documented but are different natives");
}

// =======================================================================
// Sec39.1 - rappel_enter / rappel_exit (2 names)
// =======================================================================

// rappel_enter (Sec39.1). character + optional "queue" boolean (default
// false) + optional target name + optional integer (default -1, bounds-
// checked < 8 unsigned so the default is always skipped - not modelled,
// no in-scope reader). CONFIRMED: "a queued rappel request is stored in a
// fixed 2-slot table (slot 0 = local player, slot 1 = every other
// character)."
int stub_rappel_enter(lua_State* L) {
    logCall(L, upLog(L), "rappel_enter", upStateTag(L));
    std::string character = argString(L, 1);
    bool queue = optionalBoolDefault(L, 2, false); // CONFIRMED default false
    std::string target = optionalStringDefault(L, 3, "");
    EngineState* st = upState(L);
    EngineState::RappelQueue& q = st->rappelQueue();
    if (queue) {
        bool isLocalPlayer = st->localPlayerNameForTesting().known() && st->localPlayerNameForTesting().get() == character;
        if (isLocalPlayer) q.slot0 = target; else q.slot1 = target; // CONFIRMED collision: slot1 shared by every other character
        ++st->rappelEnterQueuedCount();
    } else {
        ++st->rappelEnterImmediateCount(); // CONFIRMED: forwards to the owner only when not locally authoritative - not
                                            // separately modelled (no network peer in this host)
    }
    return 0;
}

// rappel_exit (Sec39.1). character + optional boolean (default TRUE,
// opposite of enter's default) + optional target name.
int stub_rappel_exit(lua_State* L) {
    logCall(L, upLog(L), "rappel_exit", upStateTag(L));
    std::string character = argString(L, 1);
    (void)optionalBoolDefault(L, 2, true); // CONFIRMED default true (opposite of enter)
    (void)character;
    ++upState(L)->rappelExitCount();
    return 0;
}

// =======================================================================
// Sec39.3 - spawning/world-state globals (3 of 5 names in this file;
// spawning_allow_gang_ambient_spawns/spawn_region_enable_los are here too)
// =======================================================================

// spawning_allow_gang_ambient_spawns (Sec39.3, RT 1). Optional boolean,
// default true.
int stub_spawning_allow_gang_ambient_spawns(lua_State* L) {
    logCall(L, upLog(L), "spawning_allow_gang_ambient_spawns", upStateTag(L));
    upState(L)->gangAmbientSpawnsAllowed() = optionalBoolDefault(L, 1, true);
    return 0;
}

// spawn_region_enable_los (Sec39.3). Spawn-region name + a BARE boolean
// (absent = disables - the opposite framing from most boolean setters in
// this document). No return. CONFIRMED: "writes the inverse of the
// argument."
int stub_spawn_region_enable_los(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_enable_los", upStateTag(L));
    std::string region = argString(L, 1);
    bool arg = lua_toboolean(L, 2) != 0; // CONFIRMED: bare boolean, absent -> false -> inverse -> stored true ("skip LOS")
    upState(L)->spawnRegionLos()[region] = !arg; // CONFIRMED: stores the INVERSE
    return 0;
}

// set_cops_shooting_from_vehicles (Sec39.3). Optional boolean, default
// true.
int stub_set_cops_shooting_from_vehicles(lua_State* L) {
    logCall(L, upLog(L), "set_cops_shooting_from_vehicles", upStateTag(L));
    upState(L)->copsShootingFromVehicles() = optionalBoolDefault(L, 1, true);
    return 0;
}

// set_saints_hated (Sec39.3). Bare boolean.
int stub_set_saints_hated(lua_State* L) {
    logCall(L, upLog(L), "set_saints_hated", upStateTag(L));
    upState(L)->saintsHated() = lua_toboolean(L, 1) != 0;
    return 0;
}

// set_stag_notoriety_area_active (Sec39.3). Bare boolean. CONFIRMED:
// "AND - only when this machine is host and the value actually changed -
// runs a host-gated spawn/despawn step... when turning off."
int stub_set_stag_notoriety_area_active(lua_State* L) {
    logCall(L, upLog(L), "set_stag_notoriety_area_active", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    bool changed = st->stagNotorietyAreaActive() != flag;
    st->stagNotorietyAreaActive() = flag;
    if (st->coopLocalIsHost() && changed && !flag) ++st->stagNotorietyAreaHostStepCount();
    return 0;
}

// =======================================================================
// Sec40.3/40.2 - projectile/radio (3 names)
// =======================================================================

// projectile_fire_from_navpoint (Sec40.3). "No crash shapes found - every
// resolution step is properly guarded." Counted only (no coordinate
// system to compute a real trajectory).
int stub_projectile_fire_from_navpoint(lua_State* L) {
    logCall(L, upLog(L), "projectile_fire_from_navpoint", upStateTag(L));
    std::string source = argString(L, 1);
    std::string target = argString(L, 2);
    std::string weapon = optionalStringDefault(L, 3, "");
    double spreadDegrees = optionalNumberDefault(L, 4, 0.0);
    EngineState* st = upState(L);
    if (source == target) return 0;             // CONFIRMED: no-op if positions are exactly equal
    if (spreadDegrees >= 90.0) return 0;         // CONFIRMED: no-op if spread >= 90 degrees
    if (weapon.empty()) return 0;                // CONFIRMED: null-safe/case-insensitive weapon lookup, nil/unknown -> no-op
    ++st->projectileFireFromNavpointCount();
    return 0;
}

// radio_get_station (Sec40.2). 1 string (vehicle, or a character whose
// current vehicle is used - this project flattens that to one resolve,
// same convention used throughout). Returns exactly 1 number.
int stub_radio_get_station(lua_State* L) {
    logCall(L, upLog(L), "radio_get_station", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    int32_t station = -1; // CONFIRMED: -1 for both "unresolved" and "resolved but no station" - indistinguishable
    if (st->objectResolves().get(name)) station = st->radioStationForVehicle(name);
    lua_pushnumber(L, station);
    return 1;
}

// radio_set_sing_along_allowed_during_missions (Sec40.2). Bare boolean.
int stub_radio_set_sing_along_allowed_during_missions(lua_State* L) {
    logCall(L, upLog(L), "radio_set_sing_along_allowed_during_missions", upStateTag(L));
    upState(L)->singAlongAllowedDuringMissions() = lua_toboolean(L, 1) != 0;
    return 0;
}

// =======================================================================
// Sec42.6 - small globals (2 names here; parking_spot_set_max is in B26? -
// no, kept together here since both parking_spot natives belong to the
// same small-globals cluster)
// =======================================================================

// parking_spot_set_max (Sec42.6, RT 1). 1 number; CONFIRMED: "values above
// 18 or negative are silently ignored (only 0-18 accepted) - not clamped,
// simply dropped, leaving the previous value in place."
int stub_parking_spot_set_max(lua_State* L) {
    logCall(L, upLog(L), "parking_spot_set_max", upStateTag(L));
    int32_t value = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    if (value >= 0 && value <= 18) upState(L)->parkingSpotMax() = value; // CONFIRMED: out-of-range silently dropped
    return 0;
}

// parking_spot_reset_max (Sec42.6). No arguments. CONFIRMED: writes the
// hard-coded default 18.
int stub_parking_spot_reset_max(lua_State* L) {
    logCall(L, upLog(L), "parking_spot_reset_max", upStateTag(L));
    upState(L)->parkingSpotMax() = 18;
    return 0;
}

// path_name_is_path (Sec42.6). 1 string. Returns 1 boolean: true only for
// a live object of the specific "path" kind - no object-kind table in
// this project, OPEN until a test registers it.
int stub_path_name_is_path(lua_State* L) {
    logCall(L, upLog(L), "path_name_is_path", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = st->pathObjectIsPath().known(name) && st->pathObjectIsPath().get(name);
    lua_pushboolean(L, result);
    return 1;
}

// =======================================================================
// Sec41.6/53.5 - get_closest_npc_to_object / get_closest_object (2 names)
// =======================================================================

// get_closest_npc_to_object (Sec41.6). Reference object name + radius.
// CONFIRMED: "always returns exactly 2 values: (distance, name)... or
// (float-max, '') when nothing qualifies."
int stub_get_closest_npc_to_object(lua_State* L) {
    logCall(L, upLog(L), "get_closest_npc_to_object", upStateTag(L));
    std::string ref = argString(L, 1);
    EngineState* st = upState(L);
    if (st->closestNpcOverride().known(ref)) {
        const auto& r = st->closestNpcOverride().get(ref);
        lua_pushnumber(L, r.distance);
        lua_pushstring(L, r.name.c_str());
    } else {
        lua_pushnumber(L, 3.402823466e+38); // CONFIRMED: FLT_MAX sentinel, "nothing qualifies"
        lua_pushstring(L, "");
    }
    return 2;
}

// get_closest_object (Sec53.5). CONFIRMED: "returns the empty string,
// never nil, when nothing qualifies."
int stub_get_closest_object(lua_State* L) {
    logCall(L, upLog(L), "get_closest_object", upStateTag(L));
    std::string ref = argString(L, 1);
    EngineState* st = upState(L);
    std::string result;
    if (st->closestObjectOverride().known(ref)) result = st->closestObjectOverride().get(ref);
    lua_pushstring(L, result.c_str()); // CONFIRMED: "" default, never nil
    return 1;
}

// =======================================================================
// Sec41.1/50.5 - mesh movers (4 names)
// =======================================================================

// mesh_mover_apply_impulse (Sec41.1). Mover name + target name +
// magnitude. No return. No coordinate system to compute a real impulse
// vector - counted only (both gates: local ownership, kind-flag bit, are
// CONFIRMED structural requirements this project cannot independently
// verify without real per-mover physics data, so a resolved mover is
// treated as passing both, the common case per spec's own framing).
int stub_mesh_mover_apply_impulse(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_apply_impulse", upStateTag(L));
    std::string mover = argString(L, 1);
    std::string target = argString(L, 2);
    (void)lua_tonumber(L, 3); // magnitude - not modelled (no physics system)
    EngineState* st = upState(L);
    if (st->objectResolves().get(mover) && st->objectResolves().get(target)) {
        st->getOrCreateMeshMover(mover);
        ++st->meshMoverApplyImpulseCount();
    }
    return 0;
}

// mesh_mover_ready (Sec41.1). 1 name. CONFIRMED: "1 value (true) if the
// name resolves to ANY object, 2 values (false, true) if not" - note BOTH
// outcomes ultimately push a final true.
int stub_mesh_mover_ready(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_ready", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) {
        lua_pushboolean(L, true);
        return 1;
    }
    lua_pushboolean(L, false);
    lua_pushboolean(L, true);
    return 2;
}

// mesh_mover_set_damaged_by_player_only (Sec41.1). Mover name + boolean
// (read unconditionally, so a missing argument reads false). CONFIRMED:
// "0 values on success, 1 value (false) on any failure."
int stub_mesh_mover_set_damaged_by_player_only(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_set_damaged_by_player_only", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) {
        st->getOrCreateMeshMover(name).damagedByPlayerOnly = flag;
        return 0;
    }
    lua_pushboolean(L, false);
    return 1;
}

// mesh_mover_reset_by_zone (Sec50.5) - CRASH. 1 zone-name string.
// CONFIRMED null-read crash on a non-string argument in the real engine -
// HARDENED (host-safety): this host's argString() already returns "" for
// a non-string Lua value (never dereferences it as a C string), so the
// real crash shape structurally cannot occur here; an unknown (but
// valid-string) zone name is "handled correctly and does nothing"
// (CONFIRMED), matching the empty-string no-op path too.
int stub_mesh_mover_reset_by_zone(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_reset_by_zone", upStateTag(L));
    std::string zone = argString(L, 1); // HARDENED: NULL-safe by construction (argString), never a raw C-string crash
    if (!zone.empty()) ++upState(L)->meshMoverResetByZoneCounts()[zone];
    return 0;
}

// =======================================================================
// Sec44.5 - fov_check_xz_plane (1 name)
// =======================================================================

// fov_check_xz_plane (Sec44.5). CONFIRMED logic defect: "when the target
// is in view it returns 2 Lua values (true, false) instead of 1."
int stub_fov_check_xz_plane(lua_State* L) {
    logCall(L, upLog(L), "fov_check_xz_plane", upStateTag(L));
    std::string ref = argString(L, 1);
    std::string target = argString(L, 2);
    double halfAngleDegrees = optionalNumberDefault(L, 3, 30.0); // CONFIRMED default 30 (a 60-degree cone)
    (void)halfAngleDegrees;
    EngineState* st = upState(L);
    std::string key = ref + "|" + target;
    bool inView = st->fovCheckXzPlaneResult().known(key) && st->fovCheckXzPlaneResult().get(key);
    if (inView) {
        lua_pushboolean(L, true);
        lua_pushboolean(L, false); // CONFIRMED logic defect: extra false pushed when in view
        return 2;
    }
    lua_pushboolean(L, false);
    return 1;
}

// =======================================================================
// Sec45.6/45.5/45.2 - smaller findings, boss battle, camera script,
// cutscene, debris flow (8 names)
// =======================================================================

// audio_suppress_ambient_player_lines (Sec45.6). Host-only write, session-
// synced variable "persona_supress".
int stub_audio_suppress_ambient_player_lines(lua_State* L) {
    logCall(L, upLog(L), "audio_suppress_ambient_player_lines", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    if (st->coopLocalIsHost()) st->audioSuppressAmbientPlayerLines() = flag; // CONFIRMED: host-only
    return 0;
}

// boss_battle_kb_enable (Sec45.5). Optional boolean, default true (both
// Killbane natives "default to enabling when called with no argument").
int stub_boss_battle_kb_enable(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_kb_enable", upStateTag(L));
    upState(L)->bossBattleKbEnable(optionalBoolDefault(L, 1, true));
    return 0;
}

// boss_battle_mars_killbane_active (Sec45.5). Optional boolean, default
// true. CONFIRMED: "clears the flag WITHOUT running the QTE-abort cleanup"
// that boss_battle_kb_enable(false) runs.
int stub_boss_battle_mars_killbane_active(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_mars_killbane_active", upStateTag(L));
    upState(L)->bossBattleMarsKillbaneActive(optionalBoolDefault(L, 1, true));
    return 0;
}

// camera_script_disable (Sec45.2). CONFIRMED: writes byte 0x013c9382 and,
// whenever the network module is up, UNCONDITIONALLY broadcasts, no host
// check.
int stub_camera_script_disable(lua_State* L) {
    logCall(L, upLog(L), "camera_script_disable", upStateTag(L));
    EngineState* st = upState(L);
    st->scriptCameraOwnsView() = false;
    ++st->cameraScriptBroadcastCount(); // CONFIRMED: unconditional, no host check
    return 0;
}

// cutscene_check_exiting (Sec45.2). CONFIRMED: "true for cutscene state
// > 12." Reuses the EXISTING cutsceneState() accessor directly.
int stub_cutscene_check_exiting(lua_State* L) {
    logCall(L, upLog(L), "cutscene_check_exiting", upStateTag(L));
    EngineState* st = upState(L);
    bool result = st->cutsceneState().known() && st->cutsceneState().get() > 12;
    lua_pushboolean(L, result);
    return 1;
}

// cutscene_was_skipped (Sec45.2). CONFIRMED: "reads bit 0x40 of the
// cutscene flag byte; stays true after the skipped cutscene has ended,
// clearing only when the next cutscene starts." This batch owns only the
// READ - no in-scope setter marks a cutscene as skipped, so the honest
// CHOSEN false default (see engine_state.h's own doc comment) is what a
// fresh host reports.
int stub_cutscene_was_skipped(lua_State* L) {
    logCall(L, upLog(L), "cutscene_was_skipped", upStateTag(L));
    lua_pushboolean(L, upState(L)->cutsceneWasSkippedFlag());
    return 1;
}

// debris_flow_set_inactive (Sec45.2) - CRASH. 1 debris-flow id (number).
// No return. HARDENED (host-safety): see engine_state.h's own doc comment
// on particleDebrisFlowKnownIds_/debrisFlowSetInactiveNullHandleGuardCount
// above. "Always replicates (opcode 0x45/0x25) with no host/existence
// check" - replicateStateChange() is called unconditionally regardless of
// whether the guard fired.
int stub_debris_flow_set_inactive(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_set_inactive", upStateTag(L));
    int64_t flowId = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    if (st->particleDebrisFlowKnownIds().count(flowId) == 0) {
        ++st->debrisFlowSetInactiveNullHandleGuardCount(); // HARDENED: refuse the null-virtual-call shape instead of reproducing it
    }
    EngineState::replicateStateChange("debris_flow_set_inactive", std::to_string(flowId)); // CONFIRMED: always, no gate
    return 0;
}

// =======================================================================
// Sec49.2 - spawning (3 of 7 names: set/_clear_category, clear_hood)
// =======================================================================

// spawn_global_override_set_category (Sec49.2, RT 1). 1 string. No real
// category-name table to validate against in this project (same "no real
// data table" gap as elsewhere) - stored directly, unlike the per-hood
// sibling's own missing null-check crash (out of this batch's scope).
int stub_spawn_global_override_set_category(lua_State* L) {
    logCall(L, upLog(L), "spawn_global_override_set_category", upStateTag(L));
    upState(L)->spawnGlobalOverride().category = argString(L, 1);
    return 0;
}

// spawn_global_override_clear_category (Sec49.2). No arguments.
int stub_spawn_global_override_clear_category(lua_State* L) {
    logCall(L, upLog(L), "spawn_global_override_clear_category", upStateTag(L));
    upState(L)->spawnGlobalOverride().category.clear();
    return 0;
}

// spawn_override_clear_override_for_hood (Sec49.2, `0x00a5fb40`). UPDATED
// 2026-10-09 (spec sync 3ecbf84): now delegates to
// EngineState::spawnOverrideClearOverrideForHood(), which clears the SAME
// real per-hood state the SET sibling above actually writes (see that
// method's own doc comment in engine_state.h for the disconnected-map bug
// this fixes - previously this stub erased from a map SET never touched).
int stub_spawn_override_clear_override_for_hood(lua_State* L) {
    logCall(L, upLog(L), "spawn_override_clear_override_for_hood", upStateTag(L));
    std::string hood = argString(L, 1);
    upState(L)->spawnOverrideClearOverrideForHood(hood);
    return 0;
}

// spawn_region_min_spawn_dist / _reset (Sec32.1, `0x00a5dc40`/`0x00a5dc70`).
// UPDATED 2026-10-09 (spec sync 3ecbf84): CONFIRMED to square the argument
// before storing, the exact sibling shape of spawn_region_max_spawn_dist
// above (previously this stub stored the raw value, a real bug this sync
// fixes) - see EngineState::spawnRegionMinSpawnDistSquared()'s own doc
// comment.
int stub_spawn_region_min_spawn_dist(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_min_spawn_dist", upStateTag(L));
    float f = static_cast<float>(lua_tonumber(L, 1));
    upState(L)->setSpawnRegionMinSpawnDistSquared(f * f);
    return 0;
}

int stub_spawn_region_min_spawn_dist_reset(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_min_spawn_dist_reset", upStateTag(L));
    upState(L)->resetSpawnRegionMinSpawnDist();
    return 0;
}

// =======================================================================
// Sec51.3 - radio sing-along / unblock (3 names)
// =======================================================================

// radio_clear_sing_along_track (Sec51.3). 1 number (song index). CONFIRMED:
// "song index 0 can be set... but can never be cleared from script (the
// clear function explicitly rejects index 0)," and clearing "leaves a
// separate 'current sing-along index' global stale rather than resetting
// it" (modelled as: currentIndex is simply never touched by clear).
int stub_radio_clear_sing_along_track(lua_State* L) {
    logCall(L, upLog(L), "radio_clear_sing_along_track", upStateTag(L));
    int32_t index = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    upState(L)->clearSingAlongTrack(index);
    return 0;
}

// radio_set_sing_along_track (Sec51.3). 2 song-name strings. CONFIRMED:
// "the second argument is never actually read by the local-apply logic at
// all" - only the first name drives the local currentIndex (via the
// song-name resolver this project has no real table for, OPEN until a
// test registers it); the cross-machine divergence this tranche documents
// is a network-layer effect this single-process host has no second
// machine to exhibit, so it is not fabricated here.
int stub_radio_set_sing_along_track(lua_State* L) {
    logCall(L, upLog(L), "radio_set_sing_along_track", upStateTag(L));
    std::string localSong = argString(L, 1);
    (void)argString(L, 2); // CONFIRMED: never read by the local-apply logic
    upState(L)->setSingAlongTrack(upState(L)->songNameToIndex().get(localSong));
    return 0;
}

// radio_unblock (Sec51.3). 1 vehicle name. Inverse of the already-
// registered radio_block (Sec12.20).
int stub_radio_unblock(lua_State* L) {
    logCall(L, upLog(L), "radio_unblock", upStateTag(L));
    upState(L)->recordRadioUnblock(argString(L, 1));
    return 0;
}

// =======================================================================
// Sec51.5 - satellite_weapon_mode_set (1 name)
// =======================================================================

// satellite_weapon_mode_set (Sec51.5). Boolean + optional player mask
// (CHOSEN default 1 = local only, matching this document's own bit
// convention elsewhere: bit 0 local, bit 1 remote). CONFIRMED: "a remote
// player's request is simply forwarded to that player's own owning
// machine" - reuses the SAME 0x009df3d0 correction satellite_weapon_
// mode_exit's own doc comment establishes (bit 0x2 only ever selects a
// player when playerRig().coopPlayerPresent is true).
int stub_satellite_weapon_mode_set(lua_State* L) {
    logCall(L, upLog(L), "satellite_weapon_mode_set", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0;
    int mask = static_cast<int>(optionalNumberDefault(L, 2, 1.0));
    EngineState* st = upState(L);
    if (mask & 0x1) {
        st->satelliteWeapon().active.set(enable);
        ++st->satelliteWeapon().localSetCount;
    }
    if ((mask & 0x2) && st->playerRig().coopPlayerPresent.known() && st->playerRig().coopPlayerPresent.get()) {
        ++st->satelliteWeapon().remoteForwardCount;
    }
    return 0;
}

// =======================================================================
// Sec54.3 - is_syn_tower_destroyed (1 name)
// =======================================================================

// is_syn_tower_destroyed (Sec54.3). CONFIRMED wrong-arity return: "2
// values when true and 1 when false." Reuses the EXISTING
// cityZoneSwapValue()/recordCityZoneSwap() map (Sec28.18/Sec1.9) under the
// CHOSEN key kSynTowerZoneSwapKey - see engine_state.h's own doc comment.
int stub_is_syn_tower_destroyed(lua_State* L) {
    logCall(L, upLog(L), "is_syn_tower_destroyed", upStateTag(L));
    EngineState* st = upState(L);
    bool destroyed = st->cityZoneSwapValue(EngineState::kSynTowerZoneSwapKey);
    lua_pushboolean(L, destroyed);
    if (destroyed) {
        lua_pushboolean(L, false); // CONFIRMED: extra false on the "destroyed" (true) path only
        return 2;
    }
    return 1;
}

// =======================================================================
// Sec54.2 - level_light_enable_all_do / light_group_* / los_check_do
// (4 names)
// =======================================================================

// level_light_enable_all_do (Sec54.2). 1 number (mode). No return.
// "Three lighting modes on a world-object list's light channel" - no
// real world-object/light-channel model in this project, counted only.
int stub_level_light_enable_all_do(lua_State* L) {
    logCall(L, upLog(L), "level_light_enable_all_do", upStateTag(L));
    (void)lua_tonumber(L, 1);
    ++upState(L)->levelLightEnableAllCount();
    return 0;
}

// light_group_attach_to_camera / light_group_detach_from_camera (Sec54.2).
// 1 group name each. No return. CONFIRMED: "a reference-counted 16-slot
// table... a 17th attach is silently dropped."
int stub_light_group_attach_to_camera(lua_State* L) {
    logCall(L, upLog(L), "light_group_attach_to_camera", upStateTag(L));
    std::string group = argString(L, 1);
    EngineState* st = upState(L);
    if (st->lightGroupAttachedSlotsUsed() < EngineState::kLightGroupAttachSlots) {
        ++st->lightGroupAttachRefCounts()[group];
        ++st->lightGroupAttachedSlotsUsed();
    } // CONFIRMED: a 17th attach anywhere is silently dropped
    return 0;
}

int stub_light_group_detach_from_camera(lua_State* L) {
    logCall(L, upLog(L), "light_group_detach_from_camera", upStateTag(L));
    std::string group = argString(L, 1);
    EngineState* st = upState(L);
    auto it = st->lightGroupAttachRefCounts().find(group);
    if (it != st->lightGroupAttachRefCounts().end() && it->second > 0) {
        --it->second;
        if (st->lightGroupAttachedSlotsUsed() > 0) --st->lightGroupAttachedSlotsUsed();
        if (it->second == 0) st->lightGroupAttachRefCounts().erase(it); // "removal only at zero"
    }
    return 0;
}

// los_check_do (Sec54.2) - CRASH, stuck-poll hazard. 2 object names.
// CONFIRMED: "an unresolvable object name returns 0." This host's own
// no-request-pool-to-exhaust design means the real stuck-poll hazard
// structurally cannot fire here (see engine_state.h's own doc comment);
// a genuinely resolved pair needs real line-of-sight geometry this
// project does not have - OPEN until a test sets it.
int stub_los_check_do(lua_State* L) {
    logCall(L, upLog(L), "los_check_do", upStateTag(L));
    std::string a = argString(L, 1);
    std::string b = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(a) || !st->objectResolves().get(b)) {
        lua_pushnumber(L, 0); // CONFIRMED: unresolvable -> 0, same as "no line of sight"
        return 1;
    }
    std::string key = a + "|" + b;
    lua_pushnumber(L, st->losCheckResult().get(key));
    return 1;
}

// =======================================================================
// Sec53.3 - guardian angel zone pinning / attack groups (3 names)
// =======================================================================

int stub_guardian_angel_start_zone_pinning(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_start_zone_pinning", upStateTag(L));
    upState(L)->guardianAngelStartZonePinning(argString(L, 1));
    return 0;
}

int stub_guardian_angel_clear_zone_pinning(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_clear_zone_pinning", upStateTag(L));
    upState(L)->guardianAngelStartZonePinning(""); // CONFIRMED: release-only shape, same unconditional-release path
    return 0;
}

int stub_guardian_angel_release_attack_groups(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_release_attack_groups", upStateTag(L));
    ++upState(L)->guardianAngel().releaseAttackGroupsCount;
    return 0;
}

// =======================================================================
// Sec55.5 - dlc2/dlc3/fade/flashlights (6 names)
// =======================================================================

// dlc2_m02_clapboards_set (Sec55.5) - MEMSAFE. See EngineState::
// dlc2ClapboardsSet's own doc comment (lua_engine_state.cpp) for the full
// hardened-as-one-unit-with-its-siblings reasoning.
int stub_dlc2_m02_clapboards_set(lua_State* L) {
    logCall(L, upLog(L), "dlc2_m02_clapboards_set", upStateTag(L));
    int32_t clapboardNumber = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    bool value = lua_toboolean(L, 2) != 0;
    upState(L)->dlc2ClapboardsSet(clapboardNumber, value);
    return 0;
}

// dlc3_m03_punch_load / _unload (Sec55.5). 1 number bitmask (bit 0 =
// local, bit 1 = broadcast). CONFIRMED logic defect: "_load overwrites
// its stored document-id global without checking it was already set."
int stub_dlc3_m03_punch_load(lua_State* L) {
    logCall(L, upLog(L), "dlc3_m03_punch_load", upStateTag(L));
    int mask = static_cast<int>(optionalNumberDefault(L, 1, 1.0));
    EngineState* st = upState(L);
    if (mask & 0x1) { st->dlc3Punch().loaded = true; ++st->dlc3Punch().loadCount; } // CONFIRMED: unconditional overwrite
    if (mask & 0x2) ++st->dlc3Punch().broadcastCount;
    return 0;
}

int stub_dlc3_m03_punch_unload(lua_State* L) {
    logCall(L, upLog(L), "dlc3_m03_punch_unload", upStateTag(L));
    int mask = static_cast<int>(optionalNumberDefault(L, 1, 1.0));
    EngineState* st = upState(L);
    if (mask & 0x1) { st->dlc3Punch().loaded = false; ++st->dlc3Punch().unloadCount; }
    if (mask & 0x2) ++st->dlc3Punch().broadcastCount;
    return 0;
}

// fade_get_percent (Sec55.5) - naming trap. CONFIRMED: "a hard 0.0/1.0
// step... 0.0 only while the screen-fade state is fully-faded-in [state
// 2, Sec26.24's own table], 1.0 in every other state." Reuses the
// EXISTING screenFade().state accessor directly.
int stub_fade_get_percent(lua_State* L) {
    logCall(L, upLog(L), "fade_get_percent", upStateTag(L));
    EngineState* st = upState(L);
    bool fullyFadedIn = st->screenFade().state.known() && st->screenFade().state.get() == 2;
    lua_pushnumber(L, fullyFadedIn ? 0.0 : 1.0);
    return 1;
}

// flashlights_enable (Sec55.5). No arguments.
int stub_flashlights_enable(lua_State* L) {
    logCall(L, upLog(L), "flashlights_enable", upStateTag(L));
    upState(L)->flashlightsEnabledGlobal() = true;
    return 0;
}

// flashlights_disable (Sec55.5, RT 1). No arguments.
int stub_flashlights_disable(lua_State* L) {
    logCall(L, upLog(L), "flashlights_disable", upStateTag(L));
    upState(L)->flashlightsEnabledGlobal() = false;
    return 0;
}

// =======================================================================
// Sec55.4 - force fire/throw (4 names)
// =======================================================================

// force_fire_do / force_fire_target_do (Sec55.4). Character + target name.
// CONFIRMED: "commit CAE action 0 (fire) immediately - unlike the throw
// bindings, they do NOT reset the staged action block first... no 'busy'
// test, so a running action is simply overridden." This project flattens
// the navpoint-like vs. object-kind target resolver distinction to the
// same objectResolves() gate (both are documented to behave identically
// at this level of detail).
int forceFireCommit(lua_State* L, const char* name) {
    std::string character = argString(L, 1);
    std::string target = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(character)) {
        CharacterState& c = st->getOrCreateCharacter(character);
        c.caeCommittedActionIndex = 0;   // CONFIRMED literal: action 0 = fire
        c.caeCommittedTargetName = target; // CONFIRMED: staged block is NOT reset first
    }
    (void)name;
    return 0;
}

int stub_force_fire_do(lua_State* L) {
    logCall(L, upLog(L), "force_fire_do", upStateTag(L));
    return forceFireCommit(L, "force_fire_do");
}

int stub_force_fire_target_do(lua_State* L) {
    logCall(L, upLog(L), "force_fire_target_do", upStateTag(L));
    return forceFireCommit(L, "force_fire_target_do");
}

// force_throw_do (Sec55.4). Character + optional target name. CONFIRMED:
// "only STAGES CAE action 4 (throw grenade)... requires the character to
// have a throwable item equipped/held; it is null-safe throughout."
int stub_force_throw_do(lua_State* L) {
    logCall(L, upLog(L), "force_throw_do", upStateTag(L));
    std::string character = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    if (st->objectResolves().get(character)) {
        CharacterState& c = st->getOrCreateCharacter(character);
        if (c.hasThrowableItemEquipped.get()) { // CONFIRMED gate; OPEN until a test sets it (no inventory model here)
            c.caeStagedActionIndex = 4; // CONFIRMED literal: action 4 = throw grenade
            c.caeStagedTargetName = target;
        }
    }
    return 0;
}

// force_throw_char_do (Sec55.4) - CRASH. Character + target character
// name. CONFIRMED: "computes character + 0x40 as a position pointer...
// BEFORE the character is null-checked." HARDENED (host-safety): checks
// the character for null FIRST, the fix order its own sibling
// force_throw_do already uses (per spec's own text), instead of reading
// through address 0x40.
int stub_force_throw_char_do(lua_State* L) {
    logCall(L, upLog(L), "force_throw_char_do", upStateTag(L));
    std::string character = argString(L, 1);
    std::string targetCharacter = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) {
        ++st->forceThrowCharNullCharacterGuardCount(); // HARDENED: refuse instead of reading through address 0x40
        return 0;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    if (c.hasThrowableItemEquipped.get()) {
        c.caeStagedActionIndex = 4; // CONFIRMED literal: action 4 = throw grenade (same as force_throw_do)
        c.caeStagedTargetName = targetCharacter;
    }
    return 0;
}

// =======================================================================
// Sec25.23/57.4 - continuous_explosion_follow_target_remove (1 name)
// =======================================================================

int stub_continuous_explosion_follow_target_remove(lua_State* L) {
    logCall(L, upLog(L), "continuous_explosion_follow_target_remove", upStateTag(L));
    std::string target = argString(L, 1);
    auto& targets = upState(L)->continuousExplosionFollowTargets();
    targets.erase(std::remove(targets.begin(), targets.end(), target), targets.end());
    return 0;
}

// =======================================================================
// Sec59.6 - audio_play_* family (7 names)
// =======================================================================

// Shared event-based body (audio_play_do / audio_play_id_do / audio_play_
// id_for_navpoint_do, Sec59.6): CONFIRMED "can be left uninitialized on
// the stack if audio itself was never initialized." HARDENED (host-
// safety): returns the documented failure value 0 instead - see engine_
// state.h's own doc comment on audioSystemInitialized_ for the CHOSEN
// numeric-value reasoning.
int audioPlayEventBased(lua_State* L, const char* name) {
    logCall(L, upLog(L), name, upStateTag(L));
    EngineState* st = upState(L);
    if (!st->audioSystemInitialized()) {
        ++st->audioEventUninitializedGuardCount();
        lua_pushnumber(L, 0);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(st->allocateAudioSerial()));
    return 1;
}

int stub_audio_play_do(lua_State* L) {
    (void)argString(L, 1); // event name - not modelled further (no Wwise event table in this project)
    return audioPlayEventBased(L, "audio_play_do");
}

int stub_audio_play_id_do(lua_State* L) {
    (void)(static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1))) & 0xFFFF); // CONFIRMED: low-16-bit truncation
    return audioPlayEventBased(L, "audio_play_id_do");
}

// audio_play_id_for_navpoint_do (AGG): grouped with the event-based trio
// per this batch's own reconciliation of Sec59.6's "three event-based
// bindings" count against only 2 being individually named - see this
// file's own top comment / this batch's report for the full reasoning.
int stub_audio_play_id_for_navpoint_do(lua_State* L) {
    (void)(static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1))) & 0xFFFF);
    (void)optionalStringDefault(L, 2, "");
    return audioPlayEventBased(L, "audio_play_id_for_navpoint_do");
}

// audio_play_for_character_do (Sec59.6). Character + event name.
// CONFIRMED: tests its resolved character (the sibling that DOES check);
// "an unresolved character makes this return TWO values (255 and 0) where
// every success path returns ONE."
int stub_audio_play_for_character_do(lua_State* L) {
    logCall(L, upLog(L), "audio_play_for_character_do", upStateTag(L));
    std::string character = argString(L, 1);
    (void)argString(L, 2); // event name - not modelled further
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) {
        lua_pushnumber(L, 255); // CONFIRMED: failure value 255 for this binding
        lua_pushnumber(L, 0);
        return 2; // CONFIRMED: 2 values on this failure path
    }
    lua_pushnumber(L, static_cast<double>(st->allocateAudioSerial()));
    return 1;
}

// audio_play_for_character_weapon_do (Sec59.6) - CRASH. Character + event
// name. CONFIRMED null-pointer crash: "reads two fields directly off the
// resolved character pointer with no null check." HARDENED the same way
// its sibling audio_play_for_character_do already handles it (test the
// resolver's result FIRST) - failure value is 0 here (per spec: "failure
// is reported as 255 by two bindings but as plain 0 by the rest," and
// this one is in "the rest").
int stub_audio_play_for_character_weapon_do(lua_State* L) {
    logCall(L, upLog(L), "audio_play_for_character_weapon_do", upStateTag(L));
    std::string character = argString(L, 1);
    (void)argString(L, 2); // weapon/event name - not modelled further
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) { // HARDENED: check first, same fix as the sibling's own null-check
        lua_pushnumber(L, 0); // CONFIRMED: "the rest" use plain 0, not 255
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(st->allocateAudioSerial()));
    return 1;
}

// audio_play_for_mover_do (Sec59.6, AGG). Mover + event name. One of "the
// four... object-sound bindings" - no individual paragraph beyond the
// family categorization, so failure uses "the rest" convention (plain 0).
int stub_audio_play_for_mover_do(lua_State* L) {
    logCall(L, upLog(L), "audio_play_for_mover_do", upStateTag(L));
    std::string mover = argString(L, 1);
    (void)argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(mover)) {
        lua_pushnumber(L, 0);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(st->allocateAudioSerial()));
    return 1;
}

// audio_play_id_for_character_do (Sec59.6). Character + numeric cue id
// (low-16-bit truncated). CONFIRMED: "pushes its single result TWICE" -
// and is one of the "two bindings" reporting failure as 255 (paired here
// with audio_play_for_character_do above, both explicitly described with
// a 255 failure value in the SAME paragraph - see this file's own stub
// doc comment reasoning in the batch report for the full reconciliation).
int stub_audio_play_id_for_character_do(lua_State* L) {
    logCall(L, upLog(L), "audio_play_id_for_character_do", upStateTag(L));
    std::string character = argString(L, 1);
    (void)(static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 2))) & 0xFFFF); // CONFIRMED: low-16-bit truncation
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) {
        lua_pushnumber(L, 255); // CONFIRMED: pushed TWICE, both on failure
        lua_pushnumber(L, 255);
        return 2;
    }
    double serial = static_cast<double>(st->allocateAudioSerial());
    lua_pushnumber(L, serial); // CONFIRMED: "pushes its single result TWICE"
    lua_pushnumber(L, serial);
    return 2;
}

// =======================================================================
// Sec59.5 - boss_battle_matt_qte_begin / _set_phase (2 names)
// =======================================================================

// boss_battle_matt_set_phase (Sec59.5). 1 number. CONFIRMED: "stores ANY
// value with no bound... never resets it even when it rejects the value
// as out-of-range" later.
int stub_boss_battle_matt_set_phase(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_set_phase", upStateTag(L));
    upState(L)->bossBattleMatt().phase = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    return 0;
}

// boss_battle_matt_qte_begin (Sec59.5) - CRASH, three independent paths,
// all HARDENED (host-safety) rather than reproduced: (1) "Matt" not live;
// (2) phase out of the real 4-entry name table; (3) an unresolved player
// argument (the real engine writes a completion hook through literal
// address 0x1f00 in this case). Optional target + player name arguments
// (CHOSEN shape; thin beyond the three crash paths' own text).
int stub_boss_battle_matt_qte_begin(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_qte_begin", upStateTag(L));
    std::string target = optionalStringDefault(L, 1, "");
    std::string player = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    EngineState::BossBattleMatt& m = st->bossBattleMatt();
    if (!st->objectResolves().get("Matt")) { // CHOSEN literal name, matching the spec's own prose
        ++m.qteBeginMattNotLiveGuardCount;
        return 0;
    }
    if (m.phase < 1 || m.phase > EngineState::BossBattleMatt::kPhaseTableSize) {
        ++m.qteBeginOutOfRangePhaseGuardCount;
        return 0;
    }
    if (!player.empty() && !st->objectResolves().get(player)) {
        ++m.qteBeginUnresolvedPlayerGuardCount;
        return 0;
    }
    (void)target;
    m.qteActive = true;
    return 0;
}

// =======================================================================
// Sec59.4 - camera_look_through_do / camera_script_is_finished (2 names)
// =======================================================================

// camera_look_through_do (Sec59.4). Target object + duration. CONFIRMED:
// "the HUD-hide reference is taken BEFORE the target object is resolved"
// - reproduced faithfully (a real logic defect, not hardened away).
int stub_camera_look_through_do(lua_State* L) {
    logCall(L, upLog(L), "camera_look_through_do", upStateTag(L));
    std::string target = argString(L, 1);
    double durationMs = optionalNumberDefault(L, 2, 0.0);
    EngineState* st = upState(L);
    EngineState::CameraLookThrough& clt = st->cameraLookThrough();
    ++clt.hudHideRefCount; // CONFIRMED: taken before resolve, unconditionally
    if (st->objectResolves().get(target)) {
        clt.active = true;
        clt.durationMs = durationMs;
        clt.elapsedMs.set(0.0);
        ++clt.broadcastCount;
    }
    return 0;
}

// camera_script_is_finished (Sec59.4). No arguments. Returns 1 boolean.
int stub_camera_script_is_finished(lua_State* L) {
    logCall(L, upLog(L), "camera_script_is_finished", upStateTag(L));
    EngineState::CameraLookThrough& clt = upState(L)->cameraLookThrough();
    bool finished = true;
    if (clt.active) {
        if (clt.durationMs <= 0.0) finished = true; // CHOSEN: "harmless" zero-duration reading, see doc comment
        else if (clt.elapsedMs.known()) finished = clt.elapsedMs.get() >= clt.durationMs;
        else finished = false;
    }
    lua_pushboolean(L, finished);
    return 1;
}

// =======================================================================
// Sec46.3 - world_despawn_all_vehicles / _pedestrians, Sec46/T14
// waypoint_is_placed (3 names)
// =======================================================================

int stub_world_despawn_all_vehicles(lua_State* L) {
    logCall(L, upLog(L), "world_despawn_all_vehicles", upStateTag(L));
    upState(L)->despawnAllVehicles();
    return 0;
}

int stub_world_despawn_all_pedestrians(lua_State* L) {
    logCall(L, upLog(L), "world_despawn_all_pedestrians", upStateTag(L));
    upState(L)->despawnAllPedestrians();
    return 0;
}

// waypoint_is_placed (§46/T14, AGG). No arguments. See engine_state.h's
// own doc comment for the full "thin cluster text, global query" reasoning.
int stub_waypoint_is_placed(lua_State* L) {
    logCall(L, upLog(L), "waypoint_is_placed", upStateTag(L));
    lua_pushboolean(L, upState(L)->waypointPlaced());
    return 1;
}

// =======================================================================
// Sec52/T15 trigger_allow_dead_guys, Sec53/T20 get_stag_active (2 names)
// =======================================================================

// trigger_allow_dead_guys (§52/T15, RT 1). See engine_state.h's own doc
// comment for the full "thin spec, CHOSEN minimal setter" reasoning.
int stub_trigger_allow_dead_guys(lua_State* L) {
    logCall(L, upLog(L), "trigger_allow_dead_guys", upStateTag(L));
    std::string trigger = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    upState(L)->triggerAllowDeadGuys()[trigger] = flag;
    return 0;
}

// get_stag_active (§53/T20, RT 1). No arguments. "Resolved in full, no
// crash shape" - no further detail.
int stub_get_stag_active(lua_State* L) {
    logCall(L, upLog(L), "get_stag_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->stagActive());
    return 1;
}

// =======================================================================
// Sec45.2 - camera_script_enable (1 name; camera_script_disable is above)
// =======================================================================

// camera_script_enable (Sec45.2, RT 2). Same shape as camera_script_
// disable above: writes byte 0x013c9382 and unconditionally broadcasts.
int stub_camera_script_enable(lua_State* L) {
    logCall(L, upLog(L), "camera_script_enable", upStateTag(L));
    EngineState* st = upState(L);
    st->scriptCameraOwnsView() = true;
    ++st->cameraScriptBroadcastCount(); // CONFIRMED: unconditional, no host check
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB27() {
    static const std::vector<std::string> names = {
        // Wave 2, Batch W2-7 (2026-10-08): spawn/world/effects/audio/
        // camera/mission-specific half of spec-lua-api-behaviour.md
        // Sec38-60 (74 of 113 names - see lua_spec_confirmed_stubs_b26_
        // wave2_vehicles_helicopter.cpp for the other 39).
        "assert_screen_is_faded_out",
        "camera_script_enable",
        "spawning_allow_gang_ambient_spawns",
        "parking_spot_set_max",
        "trigger_allow_dead_guys",
        "spawn_global_override_set_category",
        "spawn_region_min_spawn_dist",
        "get_stag_active",
        "flashlights_disable",
        "rappel_enter",
        "rappel_exit",
        "set_cops_shooting_from_vehicles",
        "set_saints_hated",
        "set_stag_notoriety_area_active",
        "spawn_region_enable_los",
        "projectile_fire_from_navpoint",
        "radio_get_station",
        "radio_set_sing_along_allowed_during_missions",
        "parking_spot_reset_max",
        "path_name_is_path",
        "get_closest_npc_to_object",
        "mesh_mover_apply_impulse",
        "mesh_mover_ready",
        "mesh_mover_set_damaged_by_player_only",
        "fov_check_xz_plane",
        "audio_suppress_ambient_player_lines",
        "boss_battle_kb_enable",
        "boss_battle_mars_killbane_active",
        "camera_script_disable",
        "cutscene_check_exiting",
        "cutscene_was_skipped",
        "debris_flow_set_inactive",
        "flee",
        "spawn_global_override_clear_category",
        "spawn_override_clear_override_for_hood",
        "spawn_region_min_spawn_dist_reset",
        "radio_clear_sing_along_track",
        "radio_set_sing_along_track",
        "radio_unblock",
        "satellite_weapon_mode_set",
        "mesh_mover_reset_by_zone",
        "is_syn_tower_destroyed",
        "level_light_enable_all_do",
        "light_group_attach_to_camera",
        "light_group_detach_from_camera",
        "los_check_do",
        "get_closest_object",
        "guardian_angel_clear_zone_pinning",
        "guardian_angel_release_attack_groups",
        "guardian_angel_start_zone_pinning",
        "dlc2_m02_clapboards_set",
        "dlc3_m03_punch_load",
        "dlc3_m03_punch_unload",
        "fade_get_percent",
        "flashlights_enable",
        "force_fire_do",
        "force_fire_target_do",
        "force_throw_char_do",
        "force_throw_do",
        "continuous_explosion_follow_target_remove",
        "audio_play_do",
        "audio_play_for_character_do",
        "audio_play_for_character_weapon_do",
        "audio_play_for_mover_do",
        "audio_play_id_do",
        "audio_play_id_for_character_do",
        "audio_play_id_for_navpoint_do",
        "boss_battle_matt_qte_begin",
        "boss_battle_matt_set_phase",
        "camera_look_through_do",
        "camera_script_is_finished",
        "world_despawn_all_pedestrians",
        "world_despawn_all_vehicles",
        "waypoint_is_placed",
    };
    return names;
}

void registerB27(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("assert_screen_is_faded_out")) registerOne(L, state, log, stateTag, "assert_screen_is_faded_out", openGuard<stub_assert_screen_is_faded_out>);
    if (wants("camera_script_enable")) registerOne(L, state, log, stateTag, "camera_script_enable", openGuard<stub_camera_script_enable>);
    if (wants("spawning_allow_gang_ambient_spawns")) registerOne(L, state, log, stateTag, "spawning_allow_gang_ambient_spawns", openGuard<stub_spawning_allow_gang_ambient_spawns>);
    if (wants("parking_spot_set_max")) registerOne(L, state, log, stateTag, "parking_spot_set_max", openGuard<stub_parking_spot_set_max>);
    if (wants("trigger_allow_dead_guys")) registerOne(L, state, log, stateTag, "trigger_allow_dead_guys", openGuard<stub_trigger_allow_dead_guys>);
    if (wants("spawn_global_override_set_category")) registerOne(L, state, log, stateTag, "spawn_global_override_set_category", openGuard<stub_spawn_global_override_set_category>);
    if (wants("spawn_region_min_spawn_dist")) registerOne(L, state, log, stateTag, "spawn_region_min_spawn_dist", openGuard<stub_spawn_region_min_spawn_dist>);
    if (wants("get_stag_active")) registerOne(L, state, log, stateTag, "get_stag_active", openGuard<stub_get_stag_active>);
    if (wants("flashlights_disable")) registerOne(L, state, log, stateTag, "flashlights_disable", openGuard<stub_flashlights_disable>);
    if (wants("rappel_enter")) registerOne(L, state, log, stateTag, "rappel_enter", openGuard<stub_rappel_enter>);
    if (wants("rappel_exit")) registerOne(L, state, log, stateTag, "rappel_exit", openGuard<stub_rappel_exit>);
    if (wants("set_cops_shooting_from_vehicles")) registerOne(L, state, log, stateTag, "set_cops_shooting_from_vehicles", openGuard<stub_set_cops_shooting_from_vehicles>);
    if (wants("set_saints_hated")) registerOne(L, state, log, stateTag, "set_saints_hated", openGuard<stub_set_saints_hated>);
    if (wants("set_stag_notoriety_area_active")) registerOne(L, state, log, stateTag, "set_stag_notoriety_area_active", openGuard<stub_set_stag_notoriety_area_active>);
    if (wants("spawn_region_enable_los")) registerOne(L, state, log, stateTag, "spawn_region_enable_los", openGuard<stub_spawn_region_enable_los>);
    if (wants("projectile_fire_from_navpoint")) registerOne(L, state, log, stateTag, "projectile_fire_from_navpoint", openGuard<stub_projectile_fire_from_navpoint>);
    if (wants("radio_get_station")) registerOne(L, state, log, stateTag, "radio_get_station", openGuard<stub_radio_get_station>);
    if (wants("radio_set_sing_along_allowed_during_missions")) registerOne(L, state, log, stateTag, "radio_set_sing_along_allowed_during_missions", openGuard<stub_radio_set_sing_along_allowed_during_missions>);
    if (wants("parking_spot_reset_max")) registerOne(L, state, log, stateTag, "parking_spot_reset_max", openGuard<stub_parking_spot_reset_max>);
    if (wants("path_name_is_path")) registerOne(L, state, log, stateTag, "path_name_is_path", openGuard<stub_path_name_is_path>);
    if (wants("get_closest_npc_to_object")) registerOne(L, state, log, stateTag, "get_closest_npc_to_object", openGuard<stub_get_closest_npc_to_object>);
    if (wants("mesh_mover_apply_impulse")) registerOne(L, state, log, stateTag, "mesh_mover_apply_impulse", openGuard<stub_mesh_mover_apply_impulse>);
    if (wants("mesh_mover_ready")) registerOne(L, state, log, stateTag, "mesh_mover_ready", openGuard<stub_mesh_mover_ready>);
    if (wants("mesh_mover_set_damaged_by_player_only")) registerOne(L, state, log, stateTag, "mesh_mover_set_damaged_by_player_only", openGuard<stub_mesh_mover_set_damaged_by_player_only>);
    if (wants("fov_check_xz_plane")) registerOne(L, state, log, stateTag, "fov_check_xz_plane", openGuard<stub_fov_check_xz_plane>);
    if (wants("audio_suppress_ambient_player_lines")) registerOne(L, state, log, stateTag, "audio_suppress_ambient_player_lines", openGuard<stub_audio_suppress_ambient_player_lines>);
    if (wants("boss_battle_kb_enable")) registerOne(L, state, log, stateTag, "boss_battle_kb_enable", openGuard<stub_boss_battle_kb_enable>);
    if (wants("boss_battle_mars_killbane_active")) registerOne(L, state, log, stateTag, "boss_battle_mars_killbane_active", openGuard<stub_boss_battle_mars_killbane_active>);
    if (wants("camera_script_disable")) registerOne(L, state, log, stateTag, "camera_script_disable", openGuard<stub_camera_script_disable>);
    if (wants("cutscene_check_exiting")) registerOne(L, state, log, stateTag, "cutscene_check_exiting", openGuard<stub_cutscene_check_exiting>);
    if (wants("cutscene_was_skipped")) registerOne(L, state, log, stateTag, "cutscene_was_skipped", openGuard<stub_cutscene_was_skipped>);
    if (wants("debris_flow_set_inactive")) registerOne(L, state, log, stateTag, "debris_flow_set_inactive", openGuard<stub_debris_flow_set_inactive>);
    if (wants("flee")) registerOne(L, state, log, stateTag, "flee", openGuard<stub_flee>);
    if (wants("spawn_global_override_clear_category")) registerOne(L, state, log, stateTag, "spawn_global_override_clear_category", openGuard<stub_spawn_global_override_clear_category>);
    if (wants("spawn_override_clear_override_for_hood")) registerOne(L, state, log, stateTag, "spawn_override_clear_override_for_hood", openGuard<stub_spawn_override_clear_override_for_hood>);
    if (wants("spawn_region_min_spawn_dist_reset")) registerOne(L, state, log, stateTag, "spawn_region_min_spawn_dist_reset", openGuard<stub_spawn_region_min_spawn_dist_reset>);
    if (wants("radio_clear_sing_along_track")) registerOne(L, state, log, stateTag, "radio_clear_sing_along_track", openGuard<stub_radio_clear_sing_along_track>);
    if (wants("radio_set_sing_along_track")) registerOne(L, state, log, stateTag, "radio_set_sing_along_track", openGuard<stub_radio_set_sing_along_track>);
    if (wants("radio_unblock")) registerOne(L, state, log, stateTag, "radio_unblock", openGuard<stub_radio_unblock>);
    if (wants("satellite_weapon_mode_set")) registerOne(L, state, log, stateTag, "satellite_weapon_mode_set", openGuard<stub_satellite_weapon_mode_set>);
    if (wants("mesh_mover_reset_by_zone")) registerOne(L, state, log, stateTag, "mesh_mover_reset_by_zone", openGuard<stub_mesh_mover_reset_by_zone>);
    if (wants("is_syn_tower_destroyed")) registerOne(L, state, log, stateTag, "is_syn_tower_destroyed", openGuard<stub_is_syn_tower_destroyed>);
    if (wants("level_light_enable_all_do")) registerOne(L, state, log, stateTag, "level_light_enable_all_do", openGuard<stub_level_light_enable_all_do>);
    if (wants("light_group_attach_to_camera")) registerOne(L, state, log, stateTag, "light_group_attach_to_camera", openGuard<stub_light_group_attach_to_camera>);
    if (wants("light_group_detach_from_camera")) registerOne(L, state, log, stateTag, "light_group_detach_from_camera", openGuard<stub_light_group_detach_from_camera>);
    if (wants("los_check_do")) registerOne(L, state, log, stateTag, "los_check_do", openGuard<stub_los_check_do>);
    if (wants("get_closest_object")) registerOne(L, state, log, stateTag, "get_closest_object", openGuard<stub_get_closest_object>);
    if (wants("guardian_angel_clear_zone_pinning")) registerOne(L, state, log, stateTag, "guardian_angel_clear_zone_pinning", openGuard<stub_guardian_angel_clear_zone_pinning>);
    if (wants("guardian_angel_release_attack_groups")) registerOne(L, state, log, stateTag, "guardian_angel_release_attack_groups", openGuard<stub_guardian_angel_release_attack_groups>);
    if (wants("guardian_angel_start_zone_pinning")) registerOne(L, state, log, stateTag, "guardian_angel_start_zone_pinning", openGuard<stub_guardian_angel_start_zone_pinning>);
    if (wants("dlc2_m02_clapboards_set")) registerOne(L, state, log, stateTag, "dlc2_m02_clapboards_set", openGuard<stub_dlc2_m02_clapboards_set>);
    if (wants("dlc3_m03_punch_load")) registerOne(L, state, log, stateTag, "dlc3_m03_punch_load", openGuard<stub_dlc3_m03_punch_load>);
    if (wants("dlc3_m03_punch_unload")) registerOne(L, state, log, stateTag, "dlc3_m03_punch_unload", openGuard<stub_dlc3_m03_punch_unload>);
    if (wants("fade_get_percent")) registerOne(L, state, log, stateTag, "fade_get_percent", openGuard<stub_fade_get_percent>);
    if (wants("flashlights_enable")) registerOne(L, state, log, stateTag, "flashlights_enable", openGuard<stub_flashlights_enable>);
    if (wants("force_fire_do")) registerOne(L, state, log, stateTag, "force_fire_do", openGuard<stub_force_fire_do>);
    if (wants("force_fire_target_do")) registerOne(L, state, log, stateTag, "force_fire_target_do", openGuard<stub_force_fire_target_do>);
    if (wants("force_throw_char_do")) registerOne(L, state, log, stateTag, "force_throw_char_do", openGuard<stub_force_throw_char_do>);
    if (wants("force_throw_do")) registerOne(L, state, log, stateTag, "force_throw_do", openGuard<stub_force_throw_do>);
    if (wants("continuous_explosion_follow_target_remove")) registerOne(L, state, log, stateTag, "continuous_explosion_follow_target_remove", openGuard<stub_continuous_explosion_follow_target_remove>);
    if (wants("audio_play_do")) registerOne(L, state, log, stateTag, "audio_play_do", openGuard<stub_audio_play_do>);
    if (wants("audio_play_for_character_do")) registerOne(L, state, log, stateTag, "audio_play_for_character_do", openGuard<stub_audio_play_for_character_do>);
    if (wants("audio_play_for_character_weapon_do")) registerOne(L, state, log, stateTag, "audio_play_for_character_weapon_do", openGuard<stub_audio_play_for_character_weapon_do>);
    if (wants("audio_play_for_mover_do")) registerOne(L, state, log, stateTag, "audio_play_for_mover_do", openGuard<stub_audio_play_for_mover_do>);
    if (wants("audio_play_id_do")) registerOne(L, state, log, stateTag, "audio_play_id_do", openGuard<stub_audio_play_id_do>);
    if (wants("audio_play_id_for_character_do")) registerOne(L, state, log, stateTag, "audio_play_id_for_character_do", openGuard<stub_audio_play_id_for_character_do>);
    if (wants("audio_play_id_for_navpoint_do")) registerOne(L, state, log, stateTag, "audio_play_id_for_navpoint_do", openGuard<stub_audio_play_id_for_navpoint_do>);
    if (wants("boss_battle_matt_qte_begin")) registerOne(L, state, log, stateTag, "boss_battle_matt_qte_begin", openGuard<stub_boss_battle_matt_qte_begin>);
    if (wants("boss_battle_matt_set_phase")) registerOne(L, state, log, stateTag, "boss_battle_matt_set_phase", openGuard<stub_boss_battle_matt_set_phase>);
    if (wants("camera_look_through_do")) registerOne(L, state, log, stateTag, "camera_look_through_do", openGuard<stub_camera_look_through_do>);
    if (wants("camera_script_is_finished")) registerOne(L, state, log, stateTag, "camera_script_is_finished", openGuard<stub_camera_script_is_finished>);
    if (wants("world_despawn_all_pedestrians")) registerOne(L, state, log, stateTag, "world_despawn_all_pedestrians", openGuard<stub_world_despawn_all_pedestrians>);
    if (wants("world_despawn_all_vehicles")) registerOne(L, state, log, stateTag, "world_despawn_all_vehicles", openGuard<stub_world_despawn_all_vehicles>);
    if (wants("waypoint_is_placed")) registerOne(L, state, log, stateTag, "waypoint_is_placed", openGuard<stub_waypoint_is_placed>);
}

} // namespace stub_sections
} // namespace sr3luahost
