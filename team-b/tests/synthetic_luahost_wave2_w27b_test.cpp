// Synthetic tests for wave 2, Batch W2-7 (2026-10-08) - the spawn/world/
// effects/audio/camera/mission-specific half (74 names) of spec-lua-api-
// behaviour.md Sec38-60 - see src/lua_spec_confirmed_stubs_b27_wave2_
// spawn_world_audio.cpp's own top comment for the full citation list.
// Sibling test tests/synthetic_luahost_wave2_w27a_test.cpp covers the
// other 39 (vehicle/helicopter/airplane/traffic) names. Explicit coverage
// here (per this batch's own task instructions): the dlc2_m02_clapboards_
// set hardening-unification with its already-registered _get/_reset
// siblings, and the audio-family's return-shape quirks (255-vs-0 failure
// values, the double-push native, the uninitialized-stack hardening on the
// event-based trio, and the null-character hardening sibling-fix). All 74
// names are `gameplay` (every §38-§60 source line this batch cites places
// them in the gameplay registrar 0x00a20840, never a tranche's own UI
// sub-registrar).
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "w27b_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "w27b_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"assert_screen_is_faded_out", "gameplay"},
        {"camera_script_enable", "gameplay"},
        {"spawning_allow_gang_ambient_spawns", "gameplay"},
        {"parking_spot_set_max", "gameplay"},
        {"trigger_allow_dead_guys", "gameplay"},
        {"spawn_global_override_set_category", "gameplay"},
        {"spawn_region_min_spawn_dist", "gameplay"},
        {"get_stag_active", "gameplay"},
        {"flashlights_disable", "gameplay"},
        {"rappel_enter", "gameplay"},
        {"rappel_exit", "gameplay"},
        {"set_cops_shooting_from_vehicles", "gameplay"},
        {"set_saints_hated", "gameplay"},
        {"set_stag_notoriety_area_active", "gameplay"},
        {"spawn_region_enable_los", "gameplay"},
        {"projectile_fire_from_navpoint", "gameplay"},
        {"radio_get_station", "gameplay"},
        {"radio_set_sing_along_allowed_during_missions", "gameplay"},
        {"parking_spot_reset_max", "gameplay"},
        {"path_name_is_path", "gameplay"},
        {"get_closest_npc_to_object", "gameplay"},
        {"mesh_mover_apply_impulse", "gameplay"},
        {"mesh_mover_ready", "gameplay"},
        {"mesh_mover_set_damaged_by_player_only", "gameplay"},
        {"fov_check_xz_plane", "gameplay"},
        {"audio_suppress_ambient_player_lines", "gameplay"},
        {"boss_battle_kb_enable", "gameplay"},
        {"boss_battle_mars_killbane_active", "gameplay"},
        {"camera_script_disable", "gameplay"},
        {"cutscene_check_exiting", "gameplay"},
        {"cutscene_was_skipped", "gameplay"},
        {"debris_flow_set_inactive", "gameplay"},
        {"flee", "gameplay"},
        {"spawn_global_override_clear_category", "gameplay"},
        {"spawn_override_clear_override_for_hood", "gameplay"},
        {"spawn_region_min_spawn_dist_reset", "gameplay"},
        {"radio_clear_sing_along_track", "gameplay"},
        {"radio_set_sing_along_track", "gameplay"},
        {"radio_unblock", "gameplay"},
        {"satellite_weapon_mode_set", "gameplay"},
        {"mesh_mover_reset_by_zone", "gameplay"},
        {"is_syn_tower_destroyed", "gameplay"},
        {"level_light_enable_all_do", "gameplay"},
        {"light_group_attach_to_camera", "gameplay"},
        {"light_group_detach_from_camera", "gameplay"},
        {"los_check_do", "gameplay"},
        {"get_closest_object", "gameplay"},
        {"guardian_angel_clear_zone_pinning", "gameplay"},
        {"guardian_angel_release_attack_groups", "gameplay"},
        {"guardian_angel_start_zone_pinning", "gameplay"},
        {"dlc2_m02_clapboards_set", "gameplay"},
        // Already-registered siblings (wave-1 Batch 5, b05) - included here
        // too so this test's own Host actually wires them up, proving the
        // "hardened as ONE unit" sharing against the real registered globals,
        // not just EngineState's own internal fields.
        {"dlc2_m02_clapboards_get", "gameplay"},
        {"dlc2_m02_clapboards_reset", "gameplay"},
        {"dlc3_m03_punch_load", "gameplay"},
        {"dlc3_m03_punch_unload", "gameplay"},
        {"fade_get_percent", "gameplay"},
        {"flashlights_enable", "gameplay"},
        {"force_fire_do", "gameplay"},
        {"force_fire_target_do", "gameplay"},
        {"force_throw_char_do", "gameplay"},
        {"force_throw_do", "gameplay"},
        {"continuous_explosion_follow_target_remove", "gameplay"},
        {"audio_play_do", "gameplay"},
        {"audio_play_for_character_do", "gameplay"},
        {"audio_play_for_character_weapon_do", "gameplay"},
        {"audio_play_for_mover_do", "gameplay"},
        {"audio_play_id_do", "gameplay"},
        {"audio_play_id_for_character_do", "gameplay"},
        {"audio_play_id_for_navpoint_do", "gameplay"},
        {"boss_battle_matt_qte_begin", "gameplay"},
        {"boss_battle_matt_set_phase", "gameplay"},
        {"camera_look_through_do", "gameplay"},
        {"camera_script_is_finished", "gameplay"},
        {"world_despawn_all_pedestrians", "gameplay"},
        {"world_despawn_all_vehicles", "gameplay"},
        {"waypoint_is_placed", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration smoke test.
    // ------------------------------------------------------------------
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            lua_getglobal(host.gameplayState(), rn.name.c_str());
            CHECK(lua_isfunction(host.gameplayState(), -1));
            lua_pop(host.gameplayState(), 1);
        }
    }

    Host host(fixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();
    es.objectResolves().set("Bob", true);
    es.objectResolves().set("Mover1", true);
    es.objectResolves().set("Zone1", true);
    es.objectResolves().set("Matt", true);
    // Explicitly-registered "known to not resolve" names (objectResolves()
    // is OPEN-until-set per name - a name never registered at all throws,
    // matching every other stub's own established "a name never
    // registered throws, a name explicitly registered false is a real,
    // honest resolve failure" discipline, same as b03/b14/etc.).
    es.objectResolves().set("NoSuchVehicle", false);
    es.objectResolves().set("NoSuchA", false);
    es.objectResolves().set("NoSuchB", false);
    es.objectResolves().set("NoSuchChar", false);
    es.objectResolves().set("NoSuchMover", false);
    es.objectResolves().set("NoSuchPlayer", false);
    es.objectResolves().set("NoSuchTarget", false);

    // --- flee: no spec text anywhere - honest OPEN refusal ------------------
    fails(host, gp, "flee()", "flee");

    // --- assert_screen_is_faded_out: confirmed no-op (Sec35.1) --------------
    ok(host, gp, "assert_screen_is_faded_out()");

    // --- Sec45.2 camera_script_enable/_disable ------------------------------
    ok(host, gp, "camera_script_enable()");
    CHECK(es.scriptCameraOwnsView());
    CHECK(es.cameraScriptBroadcastCount() == 1);
    ok(host, gp, "camera_script_disable()");
    CHECK(!es.scriptCameraOwnsView());
    CHECK(es.cameraScriptBroadcastCount() == 2); // CONFIRMED: unconditional broadcast, no host gate, every call

    // --- Sec39.3 spawning_allow_gang_ambient_spawns -------------------------
    CHECK(!es.gangAmbientSpawnsAllowed());
    ok(host, gp, "spawning_allow_gang_ambient_spawns()"); // default true
    CHECK(es.gangAmbientSpawnsAllowed());

    // --- Sec42.6 parking_spot_set_max/_reset_max ----------------------------
    ok(host, gp, "parking_spot_set_max(10)");
    CHECK(es.parkingSpotMax() == 10);
    ok(host, gp, "parking_spot_set_max(19)"); // CONFIRMED: silently dropped, out of [0,18]
    CHECK(es.parkingSpotMax() == 10);
    ok(host, gp, "parking_spot_set_max(-1)");
    CHECK(es.parkingSpotMax() == 10);
    ok(host, gp, "parking_spot_reset_max()");
    CHECK(es.parkingSpotMax() == 18);

    // --- §52/T15 trigger_allow_dead_guys ------------------------------------
    ok(host, gp, "trigger_allow_dead_guys('Trig1')"); // default true (CHOSEN)
    CHECK(es.triggerAllowDeadGuys()["Trig1"]);
    ok(host, gp, "trigger_allow_dead_guys('Trig1', false)");
    CHECK(!es.triggerAllowDeadGuys()["Trig1"]);

    // --- Sec49.2 spawn_global_override_set/_clear_category ------------------
    ok(host, gp, "spawn_global_override_set_category('gang')");
    CHECK(es.spawnGlobalOverride().category == "gang");
    ok(host, gp, "spawn_global_override_clear_category()");
    CHECK(es.spawnGlobalOverride().category.empty());

    // --- §49/T16 spawn_region_min_spawn_dist/_reset (AGG) -------------------
    ok(host, gp, "spawn_region_min_spawn_dist(12.5)");
    CHECK(es.spawnRegionMinSpawnDist() == 12.5f);
    ok(host, gp, "spawn_region_min_spawn_dist_reset()");
    CHECK(es.spawnRegionMinSpawnDist() == 0.0f);

    // --- §49/T16 spawn_override_clear_override_for_hood (AGG) ---------------
    es.spawnOverrideCategoryForHood()["Saints Row"] = "gang"; // simulating the out-of-scope SET sibling
    ok(host, gp, "spawn_override_clear_override_for_hood('Saints Row')");
    CHECK(es.spawnOverrideCategoryForHood().count("Saints Row") == 0);

    // --- §53/T20 get_stag_active ---------------------------------------------
    ok(host, gp, "assert(get_stag_active() == false)");
    es.stagActive() = true;
    ok(host, gp, "assert(get_stag_active() == true)");

    // --- §55/T22 flashlights_enable/_disable ---------------------------------
    CHECK(es.flashlightsEnabledGlobal()); // CONFIRMED true initial
    ok(host, gp, "flashlights_disable()");
    CHECK(!es.flashlightsEnabledGlobal());
    ok(host, gp, "flashlights_enable()");
    CHECK(es.flashlightsEnabledGlobal());

    // --- Sec39.1 rappel_enter/_exit ------------------------------------------
    ok(host, gp, "rappel_enter('Bob', false)"); // immediate
    CHECK(es.rappelEnterImmediateCount() == 1);
    ok(host, gp, "rappel_enter('Bob', true, 'Ledge1')"); // queued, not the local player -> slot1
    CHECK(es.rappelQueue().slot1 == "Ledge1");
    ok(host, gp, "rappel_exit('Bob')");
    CHECK(es.rappelExitCount() == 1);

    // --- Sec39.3 set_cops_shooting_from_vehicles/set_saints_hated/
    // set_stag_notoriety_area_active --------------------------------------
    ok(host, gp, "set_cops_shooting_from_vehicles()"); // default true
    CHECK(es.copsShootingFromVehicles());
    ok(host, gp, "set_saints_hated(true)");
    CHECK(es.saintsHated());
    ok(host, gp, "set_stag_notoriety_area_active(true)");
    CHECK(es.stagNotorietyAreaActive());
    CHECK(es.stagNotorietyAreaHostStepCount() == 0); // no session -> no host step, CONFIRMED no-op shape
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    ok(host, gp, "set_stag_notoriety_area_active(false)"); // turning off, host, changed -> host step runs
    CHECK(es.stagNotorietyAreaHostStepCount() == 1);

    // --- Sec39.3 spawn_region_enable_los -------------------------------------
    ok(host, gp, "spawn_region_enable_los('Region1', true)");
    CHECK(!es.spawnRegionLos()["Region1"]); // CONFIRMED: stores the INVERSE of the argument
    ok(host, gp, "spawn_region_enable_los('Region1')"); // bare, absent -> false -> stores true
    CHECK(es.spawnRegionLos()["Region1"]);

    // --- Sec40.3 projectile_fire_from_navpoint -------------------------------
    ok(host, gp, "projectile_fire_from_navpoint('A', 'B', 'pistol')");
    CHECK(es.projectileFireFromNavpointCount() == 1);
    ok(host, gp, "projectile_fire_from_navpoint('A', 'A', 'pistol')"); // CONFIRMED: equal positions -> no-op
    CHECK(es.projectileFireFromNavpointCount() == 1);

    // --- Sec40.2 radio_get_station / sing-along-allowed ----------------------
    ok(host, gp, "assert(radio_get_station('NoSuchVehicle') == -1)");
    es.objectResolves().set("Car1", true);
    ok(host, gp, "assert(radio_get_station('Car1') == -1)"); // resolved, no station tuned
    es.setRadioStationForVehicle("Car1", 3);
    ok(host, gp, "assert(radio_get_station('Car1') == 3)");
    ok(host, gp, "radio_set_sing_along_allowed_during_missions(true)");
    CHECK(es.singAlongAllowedDuringMissions());

    // --- Sec42.6 path_name_is_path --------------------------------------------
    ok(host, gp, "assert(path_name_is_path('Zone1') == false)"); // OPEN -> honest false default (not registered)
    es.pathObjectIsPath().set("Path1", true);
    ok(host, gp, "assert(path_name_is_path('Path1') == true)");

    // --- Sec41.6/53.5 get_closest_npc_to_object / get_closest_object ---------
    ok(host, gp, "local d, n = get_closest_npc_to_object('Bob', 10); assert(n == '')"); // CONFIRMED default
    es.closestNpcOverride().set("Bob", EngineState::ClosestNpcResult{3.5, "Guy1"});
    ok(host, gp, "local d, n = get_closest_npc_to_object('Bob', 10); assert(d == 3.5 and n == 'Guy1')");
    ok(host, gp, "assert(get_closest_object('Bob') == '')"); // CONFIRMED: "" never nil
    es.closestObjectOverride().set("Bob", "Thing1");
    ok(host, gp, "assert(get_closest_object('Bob') == 'Thing1')");

    // --- Sec41.1/50.5 mesh movers ---------------------------------------------
    ok(host, gp, "mesh_mover_apply_impulse('Mover1', 'Bob', 5.0)");
    CHECK(es.meshMoverApplyImpulseCount() == 1);
    ok(host, gp, "assert(mesh_mover_ready('Mover1') == true)");
    ok(host, gp, "mesh_mover_set_damaged_by_player_only('Mover1', true)");
    CHECK(es.getOrCreateMeshMover("Mover1").damagedByPlayerOnly);
    ok(host, gp, "mesh_mover_reset_by_zone('Zone1')");
    CHECK(es.meshMoverResetByZoneCounts()["Zone1"] == 1);

    // --- Sec44.5 fov_check_xz_plane -------------------------------------------
    ok(host, gp, "local a, b = fov_check_xz_plane('Bob', 'Mover1'); assert(a == false and b == nil)");
    es.fovCheckXzPlaneResult().set("Bob|Mover1", true);
    ok(host, gp, "local a, b = fov_check_xz_plane('Bob', 'Mover1'); assert(a == true and b == false)"); // CONFIRMED defect

    // --- Sec45.6 audio_suppress_ambient_player_lines (host-only) -------------
    CHECK(es.coopLocalIsHost()); // still true from the stag-notoriety section above
    ok(host, gp, "audio_suppress_ambient_player_lines(true)");
    CHECK(es.audioSuppressAmbientPlayerLines());

    // --- Sec45.5 boss_battle_kb_enable / _mars_killbane_active --------------
    ok(host, gp, "boss_battle_kb_enable()"); // default true
    CHECK(es.killbaneQteActive());
    ok(host, gp, "boss_battle_kb_enable(false)"); // CONFIRMED: runs cleanup
    CHECK(!es.killbaneQteActive());
    CHECK(es.killbaneCleanupPerformed());
    // Fresh host for the mars_killbane_active half, so killbaneCleanup
    // Performed's own sticky test-observable flag starts at its real
    // default (false) rather than needing a test-only reset method.
    {
        Host host3(fixtureNames());
        EngineState& es3 = host3.engineState();
        lua_State* gp3 = host3.gameplayState();
        ok(host3, gp3, "boss_battle_mars_killbane_active(true)");
        ok(host3, gp3, "boss_battle_mars_killbane_active(false)"); // CONFIRMED: clears WITHOUT cleanup
        CHECK(!es3.killbaneQteActive());
        CHECK(!es3.killbaneCleanupPerformed());
    }

    // --- Sec45.2 cutscene_check_exiting / cutscene_was_skipped --------------
    ok(host, gp, "assert(cutscene_was_skipped() == false)");
    es.cutsceneWasSkippedFlag() = true;
    ok(host, gp, "assert(cutscene_was_skipped() == true)");
    es.cutsceneState().set(5);
    ok(host, gp, "assert(cutscene_check_exiting() == false)");
    es.cutsceneState().set(13);
    ok(host, gp, "assert(cutscene_check_exiting() == true)"); // CONFIRMED: > 12

    // --- Sec45.2 debris_flow_set_inactive (CRASH, HARDENED) ------------------
    CHECK(es.debrisFlowSetInactiveNullHandleGuardCount() == 0);
    ok(host, gp, "debris_flow_set_inactive(42)"); // unknown id -> HARDENED guard, not a null-pointer crash
    CHECK(es.debrisFlowSetInactiveNullHandleGuardCount() == 1);
    es.particleDebrisFlowKnownIds().insert(42);
    ok(host, gp, "debris_flow_set_inactive(42)"); // now known -> no guard fires
    CHECK(es.debrisFlowSetInactiveNullHandleGuardCount() == 1);

    // --- Sec51.3 radio sing-along / unblock ----------------------------------
    es.songNameToIndex().set("Song A", 7);
    ok(host, gp, "radio_set_sing_along_track('Song A', 'Song B')"); // 2nd arg never read locally
    CHECK(es.singAlongTrack().currentIndex == 7);
    ok(host, gp, "radio_clear_sing_along_track(0)"); // CONFIRMED: index 0 explicitly rejected
    CHECK(es.singAlongTrack().currentIndex == 7);    // CONFIRMED: left stale either way
    ok(host, gp, "radio_clear_sing_along_track(7)");
    CHECK(es.singAlongTrack().currentIndex == 7); // CONFIRMED: clear never resets currentIndex (stays stale)
    es.recordRadioBlock("Car1");
    CHECK(es.radioBlocked("Car1"));
    ok(host, gp, "radio_unblock('Car1')");
    CHECK(!es.radioBlocked("Car1"));

    // --- Sec51.5 satellite_weapon_mode_set -----------------------------------
    ok(host, gp, "satellite_weapon_mode_set(true, 1)"); // local only
    CHECK(es.satelliteWeapon().active.get());
    CHECK(es.satelliteWeapon().localSetCount == 1);
    CHECK(es.satelliteWeapon().remoteForwardCount == 0); // no co-op partner present
    es.playerRig().coopPlayerPresent.set(true);
    ok(host, gp, "satellite_weapon_mode_set(false, 2)"); // remote only
    CHECK(es.satelliteWeapon().remoteForwardCount == 1);
    CHECK(es.satelliteWeapon().active.get()); // bit 0x1 not set this call -> local flag untouched

    // --- Sec54.3 is_syn_tower_destroyed --------------------------------------
    ok(host, gp, "local a, b = is_syn_tower_destroyed(); assert(a == false and b == nil)");
    es.recordCityZoneSwap(EngineState::kSynTowerZoneSwapKey, true);
    ok(host, gp, "local a, b = is_syn_tower_destroyed(); assert(a == true and b == false)"); // CONFIRMED wrong-arity

    // --- Sec54.2 level_light/light_group/los_check ---------------------------
    ok(host, gp, "level_light_enable_all_do(1)");
    CHECK(es.levelLightEnableAllCount() == 1);
    for (int i = 0; i < 16; ++i) ok(host, gp, "light_group_attach_to_camera('G" + std::to_string(i) + "')");
    CHECK(es.lightGroupAttachedSlotsUsed() == 16);
    ok(host, gp, "light_group_attach_to_camera('Overflow')"); // CONFIRMED: 17th attach silently dropped
    CHECK(es.lightGroupAttachedSlotsUsed() == 16);
    ok(host, gp, "light_group_detach_from_camera('G0')");
    CHECK(es.lightGroupAttachedSlotsUsed() == 15);

    ok(host, gp, "assert(los_check_do('NoSuchA', 'NoSuchB') == 0)"); // CONFIRMED: unresolvable -> 0
    es.losCheckResult().set("Bob|Mover1", 1);
    ok(host, gp, "assert(los_check_do('Bob', 'Mover1') == 1)");

    // --- Sec53.3 guardian angel zone pinning / attack groups -----------------
    ok(host, gp, "guardian_angel_start_zone_pinning('ZoneSetA')");
    CHECK(es.guardianAngel().currentZonePinSetName == "ZoneSetA");
    CHECK(!es.guardianAngel().zonePinSucceeded); // unregistered size -> CONFIRMED "pins nothing"
    es.guardianAngel().zoneSetSize.set("ZoneSetB", 4);
    ok(host, gp, "guardian_angel_start_zone_pinning('ZoneSetB')");
    CHECK(es.guardianAngel().zonePinSucceeded);
    es.guardianAngel().zoneSetSize.set("ZoneSetC", 9); // > 8 -> CONFIRMED: pins nothing, but still recorded as current
    ok(host, gp, "guardian_angel_start_zone_pinning('ZoneSetC')");
    CHECK(es.guardianAngel().currentZonePinSetName == "ZoneSetC");
    CHECK(!es.guardianAngel().zonePinSucceeded);
    ok(host, gp, "guardian_angel_clear_zone_pinning()");
    CHECK(es.guardianAngel().currentZonePinSetName.empty());
    ok(host, gp, "guardian_angel_release_attack_groups()");
    CHECK(es.guardianAngel().releaseAttackGroupsCount == 1);

    // ========================================================================
    // dlc2_m02_clapboards_set - MEMSAFE, hardened AS ONE UNIT with the
    // already-registered _get/_reset siblings (explicit task requirement).
    // ========================================================================
    {
        // Before any _reset, count is -1 (CONFIRMED file-default) - every
        // index fails the upper bound, so a positive in-range index is
        // still a no-op write (matches _get's own documented "no value at
        // all" pre-reset behaviour).
        ok(host, gp, "dlc2_m02_clapboards_set(1, true)");
        CHECK(es.dlc2ClapboardCount() == -1);
        // HARDENED: argument <= 0 is HOST-SAFETY-refused with its OWN
        // named counter (distinct from dlc2ClapboardsGet's own counter -
        // "count every hardened-path firing via a NAMED PER-SITE counter").
        CHECK(es.dlc2ClapboardsSetNegativeIndexGuardCount() == 0);
        ok(host, gp, "dlc2_m02_clapboards_set(0, true)");
        CHECK(es.dlc2ClapboardsSetNegativeIndexGuardCount() == 1);
        ok(host, gp, "dlc2_m02_clapboards_set(-5, true)");
        CHECK(es.dlc2ClapboardsSetNegativeIndexGuardCount() == 2);
        CHECK(es.dlc2ClapboardsGetNegativeIndexGuardCount() == 0); // the SIBLING's own counter, untouched by _set

        // Reset to a real count, then _set actually writes within bounds,
        // and _get (the already-registered sibling, SAME array) reads it
        // back - proving the "one hardened unit" sharing.
        ok(host, gp, "dlc2_m02_clapboards_reset(5)");
        CHECK(es.dlc2ClapboardCount() == 5);
        ok(host, gp, "dlc2_m02_clapboards_set(3, true)"); // 1-based index 3 -> array slot 2
        ok(host, gp, "assert(dlc2_m02_clapboards_get(3) == true)");
        ok(host, gp, "assert(dlc2_m02_clapboards_get(1) == false)"); // CONFIRMED: zero-filled by reset
        ok(host, gp, "dlc2_m02_clapboards_set(1, true)");
        ok(host, gp, "assert(dlc2_m02_clapboards_get(1) == true)");

        // Upper-bound: out-of-range index (beyond count) is a silent no-op
        // for _set (no return value to shape, unlike _get's own "no
        // value" push) - same CONFIRMED real-engine bound, just a
        // different observable shape since _set has nothing to return.
        ok(host, gp, "dlc2_m02_clapboards_set(6, true)"); // index 5 >= count 5 -> no-op
        ok(host, gp, "local v = dlc2_m02_clapboards_get(6); assert(v == nil)"); // CONFIRMED: "no value at all"
    }

    // ========================================================================
    // dlc3_m03_punch_load / _unload (Sec55.5)
    // ========================================================================
    ok(host, gp, "dlc3_m03_punch_load(1)"); // local only
    CHECK(es.dlc3Punch().loaded);
    CHECK(es.dlc3Punch().loadCount == 1);
    ok(host, gp, "dlc3_m03_punch_load(3)"); // CONFIRMED logic defect: overwrites with no "already set" check
    CHECK(es.dlc3Punch().loadCount == 2);
    CHECK(es.dlc3Punch().broadcastCount == 1);
    ok(host, gp, "dlc3_m03_punch_unload(1)");
    CHECK(!es.dlc3Punch().loaded);
    CHECK(es.dlc3Punch().unloadCount == 1);

    // --- Sec55.5 fade_get_percent ----------------------------------------------
    es.screenFade().state.set(2); // "fully faded in"
    ok(host, gp, "assert(fade_get_percent() == 0.0)");
    es.screenFade().state.set(0); // fading in
    ok(host, gp, "assert(fade_get_percent() == 1.0)"); // naming trap: NOT an intermediate percentage
    es.screenFade().state.set(3); // fully faded out
    ok(host, gp, "assert(fade_get_percent() == 1.0)");

    // ========================================================================
    // Sec55.4 force_fire_do / force_fire_target_do / force_throw_do /
    // force_throw_char_do
    // ========================================================================
    ok(host, gp, "force_fire_do('Bob', 'Navpoint1')"); // CONFIRMED: commits action 0 immediately
    CHECK(es.getOrCreateCharacter("Bob").caeCommittedActionIndex == 0);
    CHECK(es.getOrCreateCharacter("Bob").caeCommittedTargetName == "Navpoint1");
    es.getOrCreateCharacter("Bob").caeStagedActionIndex = 99; // sentinel: force_fire must NOT reset this
    ok(host, gp, "force_fire_target_do('Bob', 'Object1')");
    CHECK(es.getOrCreateCharacter("Bob").caeCommittedTargetName == "Object1");
    CHECK(es.getOrCreateCharacter("Bob").caeStagedActionIndex == 99); // CONFIRMED: staged block NOT reset first

    fails(host, gp, "force_throw_do('Bob')", "has a throwable item"); // OPEN until a test sets it (no inventory model)
    es.getOrCreateCharacter("Bob").hasThrowableItemEquipped.set(true);
    ok(host, gp, "force_throw_do('Bob', 'Target1')");
    CHECK(es.getOrCreateCharacter("Bob").caeStagedActionIndex == 4); // CONFIRMED literal: action 4 = throw grenade
    CHECK(es.getOrCreateCharacter("Bob").caeStagedTargetName == "Target1");

    // force_throw_char_do - CRASH, HARDENED: null character checked FIRST.
    CHECK(es.forceThrowCharNullCharacterGuardCount() == 0);
    ok(host, gp, "force_throw_char_do('NoSuchChar', 'Victim1')");
    CHECK(es.forceThrowCharNullCharacterGuardCount() == 1); // HARDENED: refused instead of reading address 0x40
    ok(host, gp, "force_throw_char_do('Bob', 'Victim1')");
    CHECK(es.getOrCreateCharacter("Bob").caeStagedTargetName == "Victim1");

    // --- Sec25.23/57.4 continuous_explosion_follow_target_remove -------------
    es.continuousExplosionFollowTargets() = {"TargetA", "TargetB"};
    ok(host, gp, "continuous_explosion_follow_target_remove('TargetA')");
    CHECK(es.continuousExplosionFollowTargets().size() == 1);
    CHECK(es.continuousExplosionFollowTargets()[0] == "TargetB");

    // ========================================================================
    // Sec59.6 audio_play_* family - return-shape quirks (explicit task
    // requirement).
    // ========================================================================
    {
        // Event-based trio: audio_play_do / audio_play_id_do / audio_play_
        // id_for_navpoint_do - 1 value, HARDENED to 0 when audio is not
        // initialized (instead of an uninitialized stack read).
        CHECK(es.audioSystemInitialized()); // CHOSEN true initial
        ok(host, gp, "local r = audio_play_do('evt'); assert(r ~= nil and r > 0)");
        ok(host, gp, "local r = audio_play_id_do(100); assert(r > 0)");
        ok(host, gp, "local r = audio_play_id_for_navpoint_do(200, 'Nav1'); assert(r > 0)");
        CHECK(es.audioEventUninitializedGuardCount() == 0);
        es.audioSystemInitialized() = false;
        ok(host, gp, "assert(audio_play_do('evt') == 0)"); // HARDENED documented failure value
        CHECK(es.audioEventUninitializedGuardCount() == 1);
        ok(host, gp, "assert(audio_play_id_do(100) == 0)");
        CHECK(es.audioEventUninitializedGuardCount() == 2);
        ok(host, gp, "assert(audio_play_id_for_navpoint_do(200, 'Nav1') == 0)");
        CHECK(es.audioEventUninitializedGuardCount() == 3);
        es.audioSystemInitialized() = true;

        // audio_play_for_character_do: CONFIRMED tests its resolved
        // character; success -> 1 value; failure -> 2 values (255, 0).
        ok(host, gp, "local r = audio_play_for_character_do('Bob', 'evt'); assert(r > 0)");
        ok(host, gp, "local a, b = audio_play_for_character_do('NoSuchChar', 'evt'); assert(a == 255 and b == 0)");

        // audio_play_for_character_weapon_do - CRASH, HARDENED the same way
        // its sibling above handles it: failure value is 0 (the "rest"
        // convention), 1 value only (not 255/2-values like its sibling).
        ok(host, gp, "local r = audio_play_for_character_weapon_do('Bob', 'evt'); assert(r > 0)");
        ok(host, gp, "local a, b = audio_play_for_character_weapon_do('NoSuchChar', 'evt'); assert(a == 0 and b == nil)");

        // audio_play_for_mover_do (AGG): "the rest" convention, 1 value, 0 on failure.
        ok(host, gp, "local r = audio_play_for_mover_do('Mover1', 'evt'); assert(r > 0)");
        ok(host, gp, "local a, b = audio_play_for_mover_do('NoSuchMover', 'evt'); assert(a == 0 and b == nil)");

        // audio_play_id_for_character_do: CONFIRMED "pushes its single
        // result TWICE" - success (serial, serial); failure (255, 255),
        // one of the "two bindings" reporting 255 on failure.
        ok(host, gp, "local a, b = audio_play_id_for_character_do('Bob', 300); assert(a == b and a > 0)");
        ok(host, gp, "local a, b = audio_play_id_for_character_do('NoSuchChar', 300); assert(a == 255 and b == 255)");
    }

    // --- Sec59.5 boss_battle_matt_qte_begin / _set_phase (CRASH, HARDENED) --
    ok(host, gp, "boss_battle_matt_set_phase(2)");
    CHECK(es.bossBattleMatt().phase == 2);
    ok(host, gp, "boss_battle_matt_qte_begin()"); // "Matt" resolves, phase 2 in [1,4], no player arg
    CHECK(es.bossBattleMatt().qteActive);
    CHECK(es.bossBattleMatt().qteBeginMattNotLiveGuardCount == 0);
    CHECK(es.bossBattleMatt().qteBeginOutOfRangePhaseGuardCount == 0);
    CHECK(es.bossBattleMatt().qteBeginUnresolvedPlayerGuardCount == 0);
    // HARDENED path 1: "Matt" not live.
    {
        Host host2(fixtureNames());
        EngineState& es2 = host2.engineState();
        lua_State* gp2 = host2.gameplayState();
        es2.objectResolves().set("Matt", false);
        ok(host2, gp2, "boss_battle_matt_qte_begin()");
        CHECK(es2.bossBattleMatt().qteBeginMattNotLiveGuardCount == 1);
    }
    // HARDENED path 2: phase out of [1,4].
    ok(host, gp, "boss_battle_matt_set_phase(7)"); // CONFIRMED: stores ANY value, no bound
    CHECK(es.bossBattleMatt().phase == 7);
    ok(host, gp, "boss_battle_matt_qte_begin()");
    CHECK(es.bossBattleMatt().qteBeginOutOfRangePhaseGuardCount == 1);
    // HARDENED path 3: unresolved player argument.
    ok(host, gp, "boss_battle_matt_set_phase(1)");
    ok(host, gp, "boss_battle_matt_qte_begin(nil, 'NoSuchPlayer')");
    CHECK(es.bossBattleMatt().qteBeginUnresolvedPlayerGuardCount == 1);

    // --- Sec59.4 camera_look_through_do / camera_script_is_finished ---------
    ok(host, gp, "camera_look_through_do('NoSuchTarget', 1000)");
    CHECK(es.cameraLookThrough().hudHideRefCount == 1); // CONFIRMED: taken BEFORE resolve, even on failure
    CHECK(!es.cameraLookThrough().active);
    ok(host, gp, "camera_look_through_do('Bob', 1000)");
    CHECK(es.cameraLookThrough().hudHideRefCount == 2);
    CHECK(es.cameraLookThrough().active);
    ok(host, gp, "assert(camera_script_is_finished() == false)");
    es.cameraLookThrough().elapsedMs.set(1000.0);
    ok(host, gp, "assert(camera_script_is_finished() == true)");

    // --- Sec46.3 world_despawn_all_vehicles/_pedestrians, waypoint_is_placed
    es.getOrCreateVehicle("Ambient1");
    es.getOrCreateVehicle("PlayerCar").despawnPlayerOccupied = true;
    ok(host, gp, "world_despawn_all_vehicles()");
    CHECK(es.getOrCreateVehicle("Ambient1").despawnedPendingDelete);
    CHECK(!es.getOrCreateVehicle("PlayerCar").despawnedPendingDelete);
    es.getOrCreateCharacter("AmbientPed1");
    ok(host, gp, "world_despawn_all_pedestrians()");
    CHECK(es.getOrCreateCharacter("AmbientPed1").despawnedPendingDelete);
    ok(host, gp, "assert(waypoint_is_placed() == false)");
    es.waypointPlaced() = true;
    ok(host, gp, "assert(waypoint_is_placed() == true)");

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_wave2_w27b_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_wave2_w27b_test: " << g_failures << " check(s) failed\n";
    return 1;
}
