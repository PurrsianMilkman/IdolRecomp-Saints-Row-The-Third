// Synthetic tests for wave 1, Batch 3 of the 7-agent parallel fan-out
// (results/lua_natives_sec10_28_fanout_scoping_20261008.md) -
// spec-lua-api-behaviour.md Sec11 (12 PCR/PCU/store/pause-menu UI-cluster
// names) + Sec12 (28 of its 29 gameplay-cluster names - the 29th,
// traffic_disable_lanes, is already covered by tests/synthetic_luahost_
// sec6_7_batch_test.cpp-style fixtures for b06). 40 names total. Same
// hand-written-fixture convention as every other synthetic_luahost_*_test.
// Real cluster tags grepped directly from
// tools/lua_all_registered_1490_tagged.txt: all 12 Sec11 names are `ui`;
// all 28 Sec12 names here are `gameplay`.
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
    auto r = host.runChunk(L, code, "b10_batch3_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "b10_batch3_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

// The 40 (name, cluster) rows this batch registers.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        // Sec11 - all `ui`.
        {"pcr_is_plastic_surgeon", "ui"},
        {"pcr_mark_changed", "ui"},
        {"pcr_apply_morph_value", "ui"},
        {"pcr_get_morph_value", "ui"},
        {"pcu_is_streaming_done", "ui"},
        {"horde_mode_is_active", "ui"},
        {"game_audio_stop", "ui"},
        {"get_localized_crc_for_tag", "ui"},
        {"store_common_mouse_zoom", "ui"},
        {"store_set_camera_pos", "ui"},
        {"pause_menu_accept_options", "ui"},
        {"vint_options_remap_set_key_binding", "ui"},
        // Sec12 - all `gameplay`.
        {"hud_critical_timer_stop", "gameplay"},
        {"audio_play_for_mission_cellphone", "gameplay"},
        {"inv_item_remove_all", "gameplay"},
        {"character_set_skydiving_state", "gameplay"},
        {"character_hide", "gameplay"},
        {"vehicle_set_smoke_and_fire_state", "gameplay"},
        {"set_always_sees_player_flag", "gameplay"},
        {"inv_weapon_disable_all_but_this_slot", "gameplay"},
        {"get_char_vehicle_name", "gameplay"},
        {"effect_play_on_script_object", "gameplay"},
        {"helicopter_fly_to_do", "gameplay"},
        {"effect_play_on_human", "gameplay"},
        {"character_set_skydiving", "gameplay"},
        {"vehicle_get_driver", "gameplay"},
        {"on_vehicle_exit", "gameplay"},
        {"inv_weapon_disable_slot", "gameplay"},
        {"guardian_angel_increment_node_progress", "gameplay"},
        {"vehicle_show", "gameplay"},
        {"radio_block", "gameplay"},
        {"vehicle_set_invulnerable", "gameplay"},
        {"vehicle_prevent_explosion_fling", "gameplay"},
        {"camera_restrict_target_object", "gameplay"},
        {"waypoint_add", "gameplay"},
        {"get_current_hit_points", "gameplay"},
        {"vehicle_freeze", "gameplay"},
        {"set_player_can_enter_exit_rc_vehicles", "gameplay"},
        {"player_parachute_wear_backpack", "gameplay"},
        {"persona_override_persona_stop", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 40 names land as real functions in their own real
    // cluster's state, and nowhere else.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.uiState(), "pcr_is_plastic_surgeon"));
        CHECK(!hasGlobal(host.gameplayState(), "pcr_is_plastic_surgeon"));
        CHECK(hasGlobal(host.gameplayState(), "vehicle_show"));
        CHECK(!hasGlobal(host.uiState(), "vehicle_show"));
        for (const auto& rn : batchFixtureNames()) {
            lua_State* want = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            CHECK(hasGlobal(want, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // Section 11.
    // ==================================================================

    // Sec11.1 pcr_is_plastic_surgeon: OPEN until set, then reflects it.
    fails(host, ui, "pcr_is_plastic_surgeon()", "pcr_is_plastic_surgeon global byte");
    es.pcrPlasticSurgeonMode().set(true);
    ok(host, ui, "assert(pcr_is_plastic_surgeon() == true)");
    es.pcrPlasticSurgeonMode().set(false);
    ok(host, ui, "assert(pcr_is_plastic_surgeon() == false)");

    // Sec11.2 pcr_mark_changed: unconditional write.
    CHECK(!es.pcrDirty());
    ok(host, ui, "pcr_mark_changed()");
    CHECK(es.pcrDirty());

    // Sec11.3/Sec11.4 pcr_apply_morph_value/pcr_get_morph_value: unresolved
    // session/slot -> getter returns zero Lua values (nil in a `local v =`
    // assignment); registered session+slot -> write then read back, both
    // the index form and the category-name form (case-insensitive).
    ok(host, ui, "local v = pcr_get_morph_value('unknownsession', 0); assert(v == nil)");
    es.pcrMorph().sessionResolves.set("sess1", true);
    ok(host, ui, "local v = pcr_get_morph_value('Sess1', 0); assert(v == nil)"); // session known, slot not yet
    es.pcrMorph().slotResolves.set(EngineState::pcrMorphKey("sess1", "idx:0"), true);
    ok(host, ui, "pcr_apply_morph_value('Sess1', 0, 0.75)");
    ok(host, ui, "local v = pcr_get_morph_value('sess1', 0); assert(v == 0.75)");
    CHECK(es.pcrMorph().pendingRecompute["sess1"]);
    es.pcrMorph().slotResolves.set(EngineState::pcrMorphKey("sess1", "cat:bodygender"), true);
    ok(host, ui, "pcr_apply_morph_value('sess1', 'BodyGender', 0.9)");
    ok(host, ui, "local v = pcr_get_morph_value('sess1', 'bodygender'); assert(v == 0.9)");

    // Sec11.5 pcu_is_streaming_done: AND of two OPEN gates.
    fails(host, ui, "pcu_is_streaming_done()", "0x009f0aa0");
    es.pcuStreaming().rowCounterPending.set(false);
    fails(host, ui, "pcu_is_streaming_done()", "0x009f07d0");
    es.pcuStreaming().listMembershipPending.set(true);
    ok(host, ui, "assert(pcu_is_streaming_done() == false)");
    es.pcuStreaming().listMembershipPending.set(false);
    ok(host, ui, "assert(pcu_is_streaming_done() == true)");

    // Sec11.6 horde_mode_is_active: OPEN until set.
    fails(host, ui, "horde_mode_is_active()", "0x006e44f0");
    es.hordeModeActive().set(true);
    ok(host, ui, "assert(horde_mode_is_active() == true)");

    // Sec11.7 game_audio_stop: call-count only, no Lua-visible effect.
    ok(host, ui, "game_audio_stop(5)");
    CHECK(es.gameAudioStopCount() == 1);

    // Sec11.8 get_localized_crc_for_tag: always 0 - never-populated registry.
    ok(host, ui, "assert(get_localized_crc_for_tag('ANY_TAG') == 0.0)");
    ok(host, ui, "assert(get_localized_crc_for_tag('OTHER_TAG') == 0.0)");

    // Sec11.9 store_common_mouse_zoom: accumulates, real contribution 0.
    ok(host, ui, "store_common_mouse_zoom(2.0)");
    CHECK(es.storeCommonRotateMouseDrag().zoomCallCount == 1);
    CHECK(es.storeCommonRotateMouseDrag().pendingZoomDelta == 0.0);

    // Sec11.10 store_set_camera_pos: never-populated preset table -> no-op;
    // a test-registered preset applies.
    ok(host, ui, "store_set_camera_pos('unknownpreset')");
    CHECK(es.storeCameraPreset().appliedCount == 0);
    es.storeCameraPreset().presetResolves.set("face", true);
    ok(host, ui, "store_set_camera_pos('face')");
    CHECK(es.storeCameraPreset().lastAppliedPreset == "face");
    CHECK(es.storeCameraPreset().appliedCount == 1);

    // Sec11.11 pause_menu_accept_options: 3-way AND gate collapsed to one
    // field, OPEN until set.
    fails(host, ui, "pause_menu_accept_options()", "0x007060e0");
    es.pauseMenuOptions().acceptGatePasses.set(false);
    ok(host, ui, "pause_menu_accept_options()");
    CHECK(!es.pauseMenuOptions().committed);
    es.pauseMenuOptions().acceptGatePasses.set(true);
    ok(host, ui, "pause_menu_accept_options()");
    CHECK(es.pauseMenuOptions().committed);
    CHECK(es.pauseMenuOptions().acceptCalls == 3);

    // Sec11.12 vint_options_remap_set_key_binding: sentinel -1 skip (no
    // gate consulted); otherwise OPEN gate; dialog-already-open -> no-op;
    // dialog-not-open -> stages the triple.
    ok(host, ui, "vint_options_remap_set_key_binding(1, -1, true)");
    CHECK(!es.vintRemapStaging().hasPending);
    fails(host, ui, "vint_options_remap_set_key_binding(1, 42, true)", "0x007c2060");
    es.vintRemapStaging().dialogAlreadyOpen.set(true);
    ok(host, ui, "vint_options_remap_set_key_binding(1, 42, true)");
    CHECK(!es.vintRemapStaging().hasPending);
    CHECK(es.vintRemapStaging().dialogRaisedCount == 0);
    es.vintRemapStaging().dialogAlreadyOpen.set(false);
    ok(host, ui, "vint_options_remap_set_key_binding(3, 42, true)");
    CHECK(es.vintRemapStaging().hasPending);
    CHECK(es.vintRemapStaging().pendingActionIndex0Based == 2); // 3 - 1
    CHECK(es.vintRemapStaging().pendingKeyCode == 42);
    CHECK(es.vintRemapStaging().pendingFlag == true);
    CHECK(es.vintRemapStaging().dialogRaisedCount == 1);

    // ==================================================================
    // Section 12.
    // ==================================================================

    // Sec12.1 hud_critical_timer_stop: call-count only.
    ok(host, gp, "hud_critical_timer_stop()");
    CHECK(es.hudCriticalTimerStopCount() == 1);

    // Sec12.2 audio_play_for_mission_cellphone: registers up to 2 hook
    // slots per cellphone-audio identifier.
    ok(host, gp, "audio_play_for_mission_cellphone('call1', true, false, 'OnAnswer', 'OnHangup', 2)");
    CHECK(es.missionCellphoneAudio("call1").hookSlot1 == "OnAnswer");
    CHECK(es.missionCellphoneAudio("call1").hookSlot2 == "OnHangup");
    ok(host, gp, "audio_play_for_mission_cellphone('call2')"); // all optional args defaulted
    CHECK(es.missionCellphoneAudio("call2").hookSlot1.empty());

    // Sec12.3 inv_item_remove_all: resolve-only, default literal + explicit name.
    es.objectResolves().set("#PLAYER1#", true);
    ok(host, gp, "inv_item_remove_all()");
    es.objectResolves().set("Bob", true);
    ok(host, gp, "inv_item_remove_all('Bob')");

    // Sec12.4 character_set_skydiving_state.
    ok(host, gp, "character_set_skydiving_state('Bob', 2, true)");
    CHECK(es.getOrCreateCharacter("Bob").skydivingStateCode == 2);

    // Sec12.5 character_hide.
    ok(host, gp, "character_hide('Bob')");
    CHECK(es.getOrCreateCharacter("Bob").hidden);

    // Sec12.6 vehicle_set_smoke_and_fire_state.
    es.objectResolves().set("Car1", true);
    ok(host, gp, "vehicle_set_smoke_and_fire_state('Car1', true, false)");
    CHECK(es.getOrCreateVehicle("Car1").smokeOn);
    CHECK(!es.getOrCreateVehicle("Car1").fireVisualOn);

    // Sec12.7 set_always_sees_player_flag: default true, then explicit false.
    ok(host, gp, "set_always_sees_player_flag('Bob')");
    CHECK(es.getOrCreateCharacter("Bob").alwaysSeesPlayer);
    ok(host, gp, "set_always_sees_player_flag('Bob', false)");
    CHECK(!es.getOrCreateCharacter("Bob").alwaysSeesPlayer);

    // Sec12.8/Sec12.16 inv_weapon_disable_all_but_this_slot / inv_weapon_
    // disable_slot: shared local-player weapon-slot array, both the
    // in-range and sentinel-OOB branches.
    ok(host, gp, "inv_weapon_disable_all_but_this_slot(3)");
    {
        auto& slots = es.weaponSlots();
        for (size_t i = 0; i < slots.disabled.size(); ++i) {
            CHECK(slots.disabled[i] == (i != 3));
        }
    }
    ok(host, gp, "inv_weapon_disable_all_but_this_slot(99)");
    CHECK(es.weaponSlots().sentinelOobCount == 1);
    ok(host, gp, "inv_weapon_disable_slot(5)");
    CHECK(es.weaponSlots().disabled[5]);
    ok(host, gp, "inv_weapon_disable_slot(-1)");
    CHECK(es.weaponSlots().sentinelOobCount == 2);

    // Sec12.9 get_char_vehicle_name: empty fallback, then a known cached
    // association.
    ok(host, gp, "assert(get_char_vehicle_name('Bob') == '')");
    es.getOrCreateCharacter("Bob").currentVehicleName.set("Car1");
    ok(host, gp, "assert(get_char_vehicle_name('Bob') == 'Car1')");
    es.objectResolves().set("Nobody", false);
    ok(host, gp, "assert(get_char_vehicle_name('Nobody') == '')");

    // Sec12.10 effect_play_on_script_object: -1.0 on failure, a fresh
    // distinct handle per success call.
    es.objectResolves().set("Fx1", false);
    ok(host, gp, "assert(effect_play_on_script_object('Fx1','Obj1') == -1.0)");
    es.objectResolves().set("Fx1", true);
    es.objectResolves().set("Obj1", true);
    ok(host, gp,
       "local h1 = effect_play_on_script_object('Fx1','Obj1'); "
       "local h2 = effect_play_on_script_object('Fx1','Obj1'); "
       "assert(h1 ~= -1.0 and h2 ~= h1)");

    // Sec12.11 helicopter_fly_to_do: unresolved vehicle -> false; resolved
    // but un-set qualifying gate -> OPEN; gate false -> false; gate true +
    // resolved destination -> true, request recorded.
    //
    // CONFIRMED CASE-BY-CASE (vehicle-class xtbl wiring task, 2026-10-10):
    // "Heli1" here is a pure Lua-host-only test fixture with no real
    // zone-spawn backing - and this one test body needs qualifies to visit
    // OPEN, then false, then true within a single test, which the real,
    // general, data-driven path (EngineState::loadZoneObjectsSection(),
    // proven end-to-end by tests/synthetic_luahost_batch2728_test.cpp's own
    // m05_heli_pierce fixture) can only ever assign ONE fixed value for.
    // Manual .set() stays, legitimately, per this task's own "confirm
    // case-by-case" instruction.
    es.objectResolves().set("Heli1", false);
    ok(host, gp, "assert(helicopter_fly_to_do('Heli1', 1, true, 'Dest1', true) == false)");
    es.objectResolves().set("Heli1", true);
    fails(host, gp, "helicopter_fly_to_do('Heli1', 1, true, 'Dest1', true)", "0x00ad31a0");
    es.helicopterFlyTo().qualifies.set("Heli1", false);
    ok(host, gp, "assert(helicopter_fly_to_do('Heli1', 1, true, 'Dest1', true) == false)");
    es.helicopterFlyTo().qualifies.set("Heli1", true);
    es.objectResolves().set("Dest1", true);
    ok(host, gp, "assert(helicopter_fly_to_do('Heli1', 1, true, 'Dest1', true, '', 0, true) == true)");
    CHECK(es.helicopterFlyTo().lastRequest["Heli1"].targetName == "Dest1");
    CHECK(es.helicopterFlyTo().lastRequest["Heli1"].useOrientationMode == true);
    CHECK(es.helicopterFlyTo().lastRequest["Heli1"].applied == true);

    // Sec12.12 effect_play_on_human: same sentinel/handle shape as Sec12.10.
    es.objectResolves().set("Bob2", true);
    ok(host, gp,
       "local h1 = effect_play_on_human('Fx1','Bob2'); "
       "local h2 = effect_play_on_human('Fx1','Bob2'); "
       "assert(h1 ~= -1.0 and h2 ~= h1)");

    // Sec12.13 character_set_skydiving: two independent internal dispatch
    // functions, observed via one boolean.
    ok(host, gp, "character_set_skydiving('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").skydivingOn);
    ok(host, gp, "character_set_skydiving('Bob', false)");
    CHECK(!es.getOrCreateCharacter("Bob").skydivingOn);

    // Sec12.14 vehicle_get_driver: unresolved -> nil, no OPEN refusal;
    // resolved but driver name unset -> OPEN; resolved + set -> name.
    es.objectResolves().set("NoCar", false);
    ok(host, gp, "assert(vehicle_get_driver('NoCar') == nil)");
    ok(host, gp, "assert(vehicle_get_driver('Car1') == nil)"); // Sec12.14 (78fb40a): empty seat 0 -> nil
    es.getOrCreateVehicle("Car1").driverName.set("Bob");
    ok(host, gp, "assert(vehicle_get_driver('Car1') == 'Bob')");

    // Sec12.15 on_vehicle_exit: collapsed 3-tier resolver, records the pair.
    ok(host, gp, "on_vehicle_exit('Bob', 'Car1')");
    CHECK(es.onVehicleExitEvents().size() == 1);
    CHECK(es.onVehicleExitEvents()[0].occupant == "Bob");
    CHECK(es.onVehicleExitEvents()[0].vehicleOrSeat == "Car1");

    // Sec12.17 guardian_angel_increment_node_progress: call-count only.
    ok(host, gp, "guardian_angel_increment_node_progress()");
    CHECK(es.guardianAngelNodeProgressCount() == 1);

    // Sec12.18 vehicle_show: unscaled fade arg, request tracked separately
    // from the raw value.
    ok(host, gp, "vehicle_show('Car1')");
    CHECK(es.getOrCreateVehicle("Car1").shownViaVehicleShow);
    CHECK(!es.getOrCreateVehicle("Car1").vehicleShowFadeRequested);
    ok(host, gp, "vehicle_show('Car1', true, 2.5)");
    CHECK(es.getOrCreateVehicle("Car1").vehicleShowFadeRequested);
    CHECK(es.getOrCreateVehicle("Car1").vehicleShowFadeRawArg == 2.5);

    // Sec12.20 radio_block: tier-1 (vehicle name directly) vs. tier-2
    // (character's cached vehicle association).
    ok(host, gp, "radio_block('Car1')");
    CHECK(es.radioBlocked("Car1"));
    es.objectResolves().set("Dave", true);
    es.getOrCreateCharacter("Dave").currentVehicleName.set("Car2");
    ok(host, gp, "radio_block('Dave')");
    CHECK(es.radioBlocked("Car2"));

    // Sec12.21 vehicle_set_invulnerable: RESOLVED (Sec46.1) - merges into
    // the existing forceFlags1d7a bit.
    ok(host, gp, "vehicle_set_invulnerable('Car1')"); // default true
    CHECK(es.getOrCreateVehicle("Car1").forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) ==
          VehicleState::kInvulnerableBit1d7a);
    ok(host, gp, "vehicle_set_invulnerable('Car1', false)");
    CHECK(es.getOrCreateVehicle("Car1").forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) == 0u);

    // Sec12.22 vehicle_prevent_explosion_fling.
    ok(host, gp, "vehicle_prevent_explosion_fling('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").preventExplosionFling);

    // Sec12.23 camera_restrict_target_object: default (local+replicate);
    // "except the local player" (replicate suppressed); any other name
    // (local suppressed).
    ok(host, gp, "camera_restrict_target_object('Obj1', 5.0)");
    CHECK(es.cameraRestrictTarget().localApplied);
    CHECK(es.cameraRestrictTarget().replicated);
    CHECK(es.cameraRestrictTarget().hasTarget);
    ok(host, gp, "camera_restrict_target_object('Obj1', 5.0, '#PLAYER#')");
    CHECK(es.cameraRestrictTarget().localApplied);
    CHECK(!es.cameraRestrictTarget().replicated);
    ok(host, gp, "camera_restrict_target_object('Obj1', 5.0, 'SomeoneElse')");
    CHECK(!es.cameraRestrictTarget().localApplied);
    CHECK(es.cameraRestrictTarget().replicated);

    // Sec12.24 waypoint_add: -1.0 sentinel on failure, plain 0.0 on success,
    // consistent with the documented "no replication" asymmetry.
    es.objectResolves().set("NoObj", false);
    ok(host, gp, "assert(waypoint_add('NoObj') == -1.0)");
    ok(host, gp, "assert(waypoint_add('Obj1') == 0.0)");

    // Sec12.25 get_current_hit_points: unresolved -> 0.0; character path
    // reuses the existing +0x1cb8 field; vehicle path uses the new field.
    es.objectResolves().set("NoOne", false);
    ok(host, gp, "assert(get_current_hit_points('NoOne') == 0.0)");
    es.getOrCreateCharacter("Bob").currentHitPoints.set(42);
    ok(host, gp, "assert(get_current_hit_points('Bob') == 42)");
    es.getOrCreateVehicle("Car1").hitPointsForQuery.set(99.0);
    ok(host, gp, "assert(get_current_hit_points('Car1') == 99.0)");

    // Sec12.26 vehicle_freeze.
    ok(host, gp, "vehicle_freeze('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").frozen);

    // Sec12.27 set_player_can_enter_exit_rc_vehicles: stored inverted.
    ok(host, gp, "set_player_can_enter_exit_rc_vehicles('Bob')"); // default true -> not blocked
    CHECK(!es.getOrCreateCharacter("Bob").rcVehiclesBlocked);
    ok(host, gp, "set_player_can_enter_exit_rc_vehicles('Bob', false)");
    CHECK(es.getOrCreateCharacter("Bob").rcVehiclesBlocked);

    // Sec12.28 player_parachute_wear_backpack: gate seeded true at
    // construction (HIGH CONFIDENCE address-identity with
    // controlLockGatePasses) -> direct local apply; gate false -> the
    // replicate-only branch leaves the field untouched.
    ok(host, gp, "player_parachute_wear_backpack('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").parachuteBackpackWorn);
    es.getOrCreateCharacter("Bob").parachuteBackpackGatePasses.set(false);
    ok(host, gp, "player_parachute_wear_backpack('Bob', false)");
    CHECK(es.getOrCreateCharacter("Bob").parachuteBackpackWorn); // unchanged - replicate-only branch, no local write

    // Sec12.29 persona_override_persona_stop: resolve-only, no crash.
    ok(host, gp, "persona_override_persona_stop('Bob')");

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_b10_batch3_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_b10_batch3_test: " << g_failures << " check(s) failed\n";
    return 1;
}
