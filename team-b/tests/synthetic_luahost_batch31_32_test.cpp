// Synthetic tests for the Sec31 ("pause_map_stag_current_district_control"
// - location and "stag mode", 5 names) + Sec32 ("ranking tranche 04", 25
// names) batch (2026-10-02) - see include/sr3luahost/engine_state.h and
// src/lua_spec_confirmed_stubs.cpp for the full per-function reasoning.
// Same convention as tests/synthetic_luahost_batch2728_test.cpp: a small,
// hand-written fixture (no real tagged-registration-list file or real
// archive read here - that is exercised for real by tools/lua_host_run.cpp).
// Real cluster tags grepped directly from
// tools/lua_all_registered_1490_tagged.txt: Sec31's 5 pause-map names (all
// members of the same UI pause-map registrar, FUN_007dfae0) are `ui`;
// pause_map_tutorial_mode (Sec32.1's setter half of the same flag) plus
// Sec32's other 24 gameplay-registrar names are `gameplay`.
//
// Includes the single-player (no co-op session) case the task asked for,
// pinning the 0x009df3d0 correction (Sec14.31/Sec32.8): see the
// `satellite_weapon_mode_exit` block below - with no co-op session
// (playerRig().coopPlayerPresent explicitly false), the remote-player
// selector bit must be a correct no-op, never a fallback onto the local
// player.
#include <iostream>
#include <limits>
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
        }                                                                                     \
    } while (0)

// Runs `code` and expects it to succeed (asserts inside the chunk do the work).
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "batch31_32_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code` and expects a Lua error whose message contains `needle`.
void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "batch31_32_test.lua");
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

// The 30 (name, cluster) rows this batch registers - real tags grepped
// directly from tools/lua_all_registered_1490_tagged.txt.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"pause_map_stag_current_district_control", "ui"},
        {"pause_map_is_stag_mode", "ui"},
        {"pause_map_is_tutorial_mode", "ui"},
        {"pause_map_set_gps", "ui"},
        {"pause_map_stag_takeover_do_reward", "ui"},
        {"store_interface_is_active", "gameplay"},
        {"spawn_region_max_spawn_dist", "gameplay"},
        {"spawn_region_max_spawn_dist_reset", "gameplay"},
        {"set_ped_override_density", "gameplay"},
        {"pause_map_tutorial_mode", "gameplay"},
        {"set_time_of_day", "gameplay"},
        {"satellite_weapon_mode_exit", "gameplay"},
        {"set_seatbelt_flag", "gameplay"},
        {"set_trailing_aim_flag", "gameplay"},
        {"set_never_turn_on_player", "gameplay"},
        {"player_revive", "gameplay"},
        {"player_warp_to_shore_disable", "gameplay"},
        {"skydive_setup_tank_bailout", "gameplay"},
        {"qte_human_is_used", "gameplay"},
        {"party_add_do", "gameplay"},
        {"npc_is_in_party", "gameplay"},
        {"npc_go_idle", "gameplay"},
        {"object_destroy", "gameplay"},
        {"object_indicator_remove_do", "gameplay"},
        {"minimap_icon_remove_do", "gameplay"},
        {"shop_enable_nearest", "gameplay"},
        {"item_show", "gameplay"},
        {"item_anim_play", "gameplay"},
        {"radio_set_station", "gameplay"},
        {"helicopter_shoot_vehicle", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 30 names land as real functions in their own real
    // cluster's state, and nowhere else.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.uiState(), "pause_map_stag_current_district_control"));
        CHECK(!hasGlobal(host.gameplayState(), "pause_map_stag_current_district_control"));
        CHECK(hasGlobal(host.gameplayState(), "satellite_weapon_mode_exit"));
        CHECK(!hasGlobal(host.uiState(), "satellite_weapon_mode_exit"));
        for (const auto& rn : batchFixtureNames()) {
            lua_State* want = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            CHECK(hasGlobal(want, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec31.2 pause_map_stag_current_district_control: no zone selected
    // (default 0, see pauseMapSelectedZone()'s own doc comment) -> the
    // CONFIRMED 2-value quirk, 0 then 1.0.
    ok(host, ui, "local a,b = pause_map_stag_current_district_control(); assert(a == 0 and b == 1.0)");

    // A selected zone with no registered members -> 1 value, 1.0 (the
    // general "total <= 0" rule, no zone-0 special-casing needed).
    es.setPauseMapSelectedZoneForTesting(7);
    ok(host, ui, "local a = pause_map_stag_current_district_control(); assert(a == 1.0)");

    // A selected zone with members: owned/total.
    es.registerZoneMemberForTesting(7, 30.0f, true);  // owned
    es.registerZoneMemberForTesting(7, 70.0f, false); // not owned
    ok(host, ui,
       "local a = pause_map_stag_current_district_control(); local d = a - 0.3; "
       "if d < 0 then d = -d end; assert(d < 0.0001)");
    es.setPauseMapSelectedZoneForTesting(0); // restore the no-selection default for later tests

    // ------------------------------------------------------------------
    // Sec31.5 pause_map_is_stag_mode: reads the SAME global 0x0229a317
    // game_autosave's own gate reads (Sec27.12) - OPEN until set.
    fails(host, ui, "pause_map_is_stag_mode()", "0x0229a317");
    es.pauseMapStagMode().set(false);
    ok(host, ui, "assert(pause_map_is_stag_mode() == false)");

    // ------------------------------------------------------------------
    // Sec31.1 table item 5 (getter) / Sec32.1 pause_map_tutorial_mode
    // (setter) - a SEPARATE global (0x0229a318) from the stag-mode flag.
    fails(host, ui, "pause_map_is_tutorial_mode()", "0x0229a318");
    ok(host, gp, "pause_map_tutorial_mode(true)");
    ok(host, ui, "assert(pause_map_is_tutorial_mode() == true)");
    ok(host, gp, "pause_map_tutorial_mode()"); // absent/nil -> false, CONFIRMED (lua_toboolean's own real semantics)
    ok(host, ui, "assert(pause_map_is_tutorial_mode() == false)");

    // ------------------------------------------------------------------
    // Sec31.3 pause_map_set_gps's stag-mode branch. Outside stag mode, no
    // effect on the selected zone even with a valid hover fixture.
    es.pauseMapStagMode().set(false);
    es.setPauseMapHoveredZoneForTesting(9, true, 1);
    ok(host, ui, "pause_map_set_gps()");
    CHECK(es.pauseMapSelectedZone() == 0);
    CHECK(es.pauseMapStagCompletionHookFiredCount() == 0);

    // In stag mode, an invalid hover (+0x54 <= 0) still has no effect.
    es.pauseMapStagMode().set(true);
    es.setPauseMapHoveredZoneForTesting(9, true, 0);
    ok(host, ui, "pause_map_set_gps()");
    CHECK(es.pauseMapSelectedZone() == 0);

    // In stag mode with a valid hover: copies into the selected zone and
    // counts the hook firing (the real per-call dispatch already has its
    // own general firing pass elsewhere, see this project's hook_registry.cpp).
    es.setPauseMapHoveredZoneForTesting(9, true, 1);
    ok(host, ui, "pause_map_set_gps()");
    CHECK(es.pauseMapSelectedZone() == 9);
    CHECK(es.pauseMapStagCompletionHookFiredCount() == 1);

    // ------------------------------------------------------------------
    // Sec31.6 pause_map_stag_takeover_do_reward. No zone selected: the
    // real engine's own unchecked null-deref; this host refuses safely
    // (no crash, no counters move).
    es.setPauseMapSelectedZoneForTesting(0);
    ok(host, ui, "pause_map_stag_takeover_do_reward()");
    CHECK(es.pauseMapTakeoverClaimedCount() == 0);
    CHECK(es.pauseMapTakeoverAutosaveRequestCount() == 0);

    // A valid selection with unowned members: claims them, clears stag
    // mode, requests an autosave.
    es.registerZoneMemberForTesting(9, 10.0f, false);
    es.registerZoneMemberForTesting(9, 20.0f, false);
    es.setPauseMapSelectedZoneForTesting(9);
    es.pauseMapStagMode().set(true);
    ok(host, ui, "pause_map_stag_takeover_do_reward()");
    CHECK(es.pauseMapTakeoverClaimedCount() == 2);
    CHECK(es.pauseMapTakeoverAutosaveRequestCount() == 1);
    CHECK(es.pauseMapStagMode().get() == false); // CONFIRMED: clears stag mode
    {
        const auto* members = es.zoneMembersForTesting(9);
        CHECK(members != nullptr);
        if (members) {
            CHECK(members->size() == 2);
            for (const auto& m : *members) CHECK(m.owned == true);
        }
    }
    // Querying the same zone now reports fully owned (1.0).
    ok(host, ui, "local a = pause_map_stag_current_district_control(); assert(a == 1.0)");

    // ------------------------------------------------------------------
    // Sec32.1 store_interface_is_active: OPEN until set.
    fails(host, gp, "store_interface_is_active()", "0x022cce86");
    es.storeInterfaceActive().set(true);
    ok(host, gp, "assert(store_interface_is_active() == true)");

    // ------------------------------------------------------------------
    // Sec32.1 spawn_region_max_spawn_dist / _reset: CONFIRMED FLT_MAX
    // default, squaring with no range check, and the reset value.
    CHECK(es.spawnRegionMaxSpawnDistSquared() == std::numeric_limits<float>::max());
    ok(host, gp, "spawn_region_max_spawn_dist(10)");
    CHECK(es.spawnRegionMaxSpawnDistSquared() == 100.0f);
    ok(host, gp, "spawn_region_max_spawn_dist(-5)"); // CONFIRMED: negative squares positive
    CHECK(es.spawnRegionMaxSpawnDistSquared() == 25.0f);
    ok(host, gp, "spawn_region_max_spawn_dist_reset()");
    CHECK(es.spawnRegionMaxSpawnDistSquared() == std::numeric_limits<float>::max());

    // ------------------------------------------------------------------
    // Sec32.1 set_ped_override_density: CONFIRMED branch structure.
    CHECK(es.pedOverrideDensity() == -1.0f);
    ok(host, gp, "set_ped_override_density(0.5)");
    CHECK(es.pedOverrideDensity() == 0.5f);
    ok(host, gp, "set_ped_override_density(1.5)"); // above 1.0: silently ignored, prior value kept
    CHECK(es.pedOverrideDensity() == 0.5f);
    ok(host, gp, "set_ped_override_density(0)"); // <= 0 -> -1.0
    CHECK(es.pedOverrideDensity() == -1.0f);

    // ------------------------------------------------------------------
    // Sec32.1 set_time_of_day: CONFIRMED forward-only delta arithmetic,
    // corrected post-condition per Sec48.4 (2026-10-03): the real engine
    // snaps the clock to the nearest time-of-day KEY afterward, which this
    // project cannot compute (key list is OPEN loaded data) - so hour/
    // minute are forgotten, not set to the requested value. Host's own
    // construction (applySpecInitialState, Sec48.2) already sets hour/
    // minute to the CONFIRMED single-player initial 10:00:00 - the raw,
    // un-initialised OPEN case is covered separately in
    // synthetic_luahost_test.cpp's own dedicated Sec48 block.
    CHECK(es.gameClock().hour.get() == 10 && es.gameClock().minute.get() == 0);
    ok(host, gp, "set_time_of_day(10, 30)"); // same-day forward: 30m = 1800s
    CHECK(es.gameClock().lastAdvanceSeconds == 1800);
    CHECK(es.gameClock().advanceRequestCount == 1);
    // Sec48.4: the real post-snap value is unknown, not 10:30 - both forgotten.
    CHECK(!es.gameClock().hour.known());
    CHECK(!es.gameClock().minute.known());
    // A second call genuinely cannot compute its own delta without knowing
    // where the first call's snap landed - correctly OPEN again, the honest
    // consequence, not a bug.
    fails(host, gp, "set_time_of_day(8, 0)", "0x014ff33c");
    CHECK(es.gameClock().advanceRequestCount == 1); // unchanged: the 2nd call never reached the delta arithmetic
    // Re-establishing a known time (e.g. this project's own initial-state
    // default, Sec48.2) lets a further call succeed again.
    es.gameClock().hour.set(8);
    es.gameClock().minute.set(0);
    ok(host, gp, "set_time_of_day(8, 0)"); // same time requested: 0 delta
    CHECK(es.gameClock().lastAdvanceSeconds == 0);
    CHECK(es.gameClock().advanceRequestCount == 2);

    // ------------------------------------------------------------------
    // Sec32.1 satellite_weapon_mode_exit + the 0x009df3d0 correction
    // (Sec14.31/Sec32.8) - THE SINGLE-PLAYER PIN. With the controller
    // active but explicitly NO co-op session (coopPlayerPresent ==
    // false, the real "no co-op session -> no remote entry" state), the
    // default mask (3, both bits) applies the local exit but correctly
    // SKIPS the remote one - never falling back onto the local player
    // for bit 0x2 (the corrected reading, replacing the old "matching"
    // misreading Sec8.17/Sec14.31 previously carried).
    es.satelliteWeapon().active.set(true);
    es.playerRig().coopPlayerPresent.set(false); // single player: confirmed no remote co-op player
    ok(host, gp, "satellite_weapon_mode_exit()"); // default mask 3 (both bits)
    CHECK(es.satelliteWeapon().localExitCount == 1);
    CHECK(es.satelliteWeapon().remoteExitCount == 0); // THE CORRECTED BEHAVIOR: no co-op session -> bit 0x2 is a correct no-op

    // With a co-op session present, bit 0x2 does apply.
    es.playerRig().coopPlayerPresent.set(true);
    ok(host, gp, "satellite_weapon_mode_exit(2)"); // remote-only selector
    CHECK(es.satelliteWeapon().remoteExitCount == 1);
    CHECK(es.satelliteWeapon().localExitCount == 1); // unchanged (bit 0x1 not selected this call)

    // Without an active controller, every selected exit is a no-op
    // (CONFIRMED: "with authority AND the controller active").
    es.satelliteWeapon().active.set(false);
    ok(host, gp, "satellite_weapon_mode_exit(3)");
    CHECK(es.satelliteWeapon().localExitCount == 1);
    CHECK(es.satelliteWeapon().remoteExitCount == 1);

    // ------------------------------------------------------------------
    // Sec32.2 character force-flag setters: resolve-gated (reuses
    // objectResolves()), default-true boolean.
    fails(host, gp, "set_seatbelt_flag('npc1')", "named-object resolution");
    es.objectResolves().set("npc1", true);
    ok(host, gp, "set_seatbelt_flag('npc1')"); // default true
    CHECK(es.seatbeltForceFlag()["npc1"] == true);
    ok(host, gp, "set_seatbelt_flag('npc1', false)");
    CHECK(es.seatbeltForceFlag()["npc1"] == false);

    es.objectResolves().set("npc2", true);
    ok(host, gp, "set_trailing_aim_flag('npc2')");
    CHECK(es.trailingAimForceFlag()["npc2"] == true);

    es.objectResolves().set("npc3", true);
    ok(host, gp, "set_never_turn_on_player('npc3')");
    CHECK(es.neverTurnOnPlayerFlag()["npc3"] == true);

    // ------------------------------------------------------------------
    // Sec32.3 player_revive: only acts on a downed (+0xcc8 == 6) character.
    es.objectResolves().set("#PLAYER1#", true);
    es.getOrCreateCharacter("#PLAYER1#").lifeState.set(0); // not downed
    ok(host, gp, "player_revive('#PLAYER1#')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").reviveCount == 0);
    es.getOrCreateCharacter("#PLAYER1#").lifeState.set(6); // downed
    ok(host, gp, "player_revive('#PLAYER1#')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").reviveCount == 1);

    // Sec32.3 player_warp_to_shore_disable: always true, hardcoded.
    ok(host, gp, "player_warp_to_shore_disable('#PLAYER1#')");
    CHECK(es.warpToShoreDisabled()["#PLAYER1#"] == true);

    // Sec32.3 skydive_setup_tank_bailout: always broadcasts; stage-gated counters.
    ok(host, gp, "skydive_setup_tank_bailout(1)");
    CHECK(es.skydiveTankBailout().broadcastCount == 1);
    CHECK(es.skydiveTankBailout().armedCount == 1);
    ok(host, gp, "skydive_setup_tank_bailout(2)");
    CHECK(es.skydiveTankBailout().broadcastCount == 2);
    CHECK(es.skydiveTankBailout().animStartedCount == 1);
    ok(host, gp, "skydive_setup_tank_bailout(9)"); // unrecognised stage: still broadcasts (record always sent)
    CHECK(es.skydiveTankBailout().broadcastCount == 3);
    CHECK(es.skydiveTankBailout().armedCount == 1);
    CHECK(es.skydiveTankBailout().animStartedCount == 1);

    // Sec32.3 qte_human_is_used: owning player or any of the slot's
    // participants; unresolved -> OPEN (same as every other name here).
    es.setQteSlotForTesting(0, true, "#PLAYER1#", {"npc1"});
    ok(host, gp, "assert(qte_human_is_used('#PLAYER1#') == true)"); // owning player
    ok(host, gp, "assert(qte_human_is_used('npc1') == true)");      // participant
    es.objectResolves().set("npc_not_used", true);
    ok(host, gp, "assert(qte_human_is_used('npc_not_used') == false)");
    fails(host, gp, "qte_human_is_used('npc_unknown')", "named-object resolution");

    // ------------------------------------------------------------------
    // Sec32.4 party_add_do / npc_is_in_party: resolve-gated, bounded to 5
    // real adds (the real 6th-or-later follower stack-overrun hazard is
    // flagged, not reproduced as an actual crash).
    es.objectResolves().set("leader1", true);
    for (int i = 1; i <= 7; ++i) es.objectResolves().set("follower" + std::to_string(i), true);
    ok(host, gp,
       "party_add_do('leader1', "
       "{'follower1','follower2','follower3','follower4','follower5','follower6','follower7'}, true)");
    CHECK(es.party().membersByLeader["leader1"].size() == 5); // CONFIRMED structural bound
    CHECK(es.party().overflowDetectedCount == 1);              // flagged, not reproduced as a crash
    ok(host, gp, "assert(npc_is_in_party('follower1') == true)");
    es.objectResolves().set("bystander", true);
    ok(host, gp, "assert(npc_is_in_party('bystander') == false)");

    // Sec32.4 npc_go_idle: reuses set_ignore_ai_flag's own stateEnum rule (Sec3.4).
    es.objectResolves().set("npc4", true);
    es.getOrCreateCharacter("npc4").stateEnum.set(0); // not in a vehicle
    ok(host, gp, "npc_go_idle('npc4')");
    CHECK(es.getOrCreateCharacter("npc4").aiOrdersResetCount == 1);
    CHECK(es.getOrCreateCharacter("npc4").actionOverrideId.get() == 0);
    es.getOrCreateCharacter("npc4").stateEnum.set(3); // in a vehicle
    ok(host, gp, "npc_go_idle('npc4')");
    CHECK(es.getOrCreateCharacter("npc4").actionOverrideId.get() == 0x19);

    // ------------------------------------------------------------------
    // Sec32.5 object_destroy / object_indicator_remove_do /
    // minimap_icon_remove_do / shop_enable_nearest.
    ok(host, gp, "object_destroy(nil)"); // CONFIRMED: non-string/nil -> no-op
    ok(host, gp, "object_destroy(42)");  // CONFIRMED: non-string -> no-op
    es.objectResolves().set("crate1", true);
    ok(host, gp, "object_destroy('crate1')");
    CHECK(es.destroyedObjects().count("crate1") == 1);

    es.objectResolves().set("crate2", true);
    es.getOrCreateCharacter("crate2").objectIndicators.push_back(ObjectIndicatorRecord{});
    ok(host, gp, "object_indicator_remove_do('crate2', 1)"); // bit 1 only: clear locally, no record
    CHECK(es.getOrCreateCharacter("crate2").objectIndicators.empty());
    CHECK(es.indicatorRemoveRecordCount() == 0);
    ok(host, gp, "object_indicator_remove_do('crate2', 2)"); // bit 2 only: record, nothing left to clear
    CHECK(es.indicatorRemoveRecordCount() == 1);

    es.objectResolves().set("crate3", true);
    es.getOrCreateCharacter("crate3").minimapIcons.push_back(MinimapIconRecord{});
    ok(host, gp, "minimap_icon_remove_do('crate3')"); // default mask 3
    CHECK(es.getOrCreateCharacter("crate3").minimapIcons.empty());

    es.shops().push_back(EngineState::Shop{false, true, true}); // only within 75
    es.shops().push_back(EngineState::Shop{true, true, true});  // within 15 (preferred)
    ok(host, gp, "shop_enable_nearest()");                      // default enable=true
    CHECK(es.shops()[1].disabled == false);                     // the within-15 shop is picked first
    CHECK(es.shops()[0].disabled == true);                      // untouched
    CHECK(es.shopEnableRecordCount() == 1);

    es.shops().clear();
    es.shops().push_back(EngineState::Shop{false, true, false}); // only within 75 available now
    ok(host, gp, "shop_enable_nearest('', false)");               // disable
    CHECK(es.shops()[0].disabled == true);
    CHECK(es.shopEnableRecordCount() == 2);

    es.shops().clear();
    ok(host, gp, "shop_enable_nearest()"); // no candidate shops at all: no-op
    CHECK(es.shopEnableRecordCount() == 2); // unchanged

    // ------------------------------------------------------------------
    // Sec32.6 item_show / item_anim_play.
    ok(host, gp, "item_show(nil)"); // CONFIRMED: nil -> no-op
    es.objectResolves().set("item1", true);
    es.itemHidden()["item1"] = true;
    ok(host, gp, "item_show('item1')");
    CHECK(es.itemHidden()["item1"] == false);

    es.objectResolves().set("item2", true);
    ok(host, gp, "item_anim_play('item2', 'walk', true)"); // single-state branch, arg3 true -> flag 0x40
    CHECK(es.itemAnimState()["item2"].lastSingleStateAnim == "walk");
    CHECK(es.itemAnimState()["item2"].lastFlag == 0x40);
    CHECK(es.itemAnimState()["item2"].singleStateStartCount == 1);
    ok(host, gp, "item_anim_play('item2', nil, false, 'blend_anim')"); // blend-transition branch, arg3 ignored
    CHECK(es.itemAnimState()["item2"].lastBlendTransitionAnim == "blend_anim");
    CHECK(es.itemAnimState()["item2"].blendTransitionCount == 1);

    // ------------------------------------------------------------------
    // Sec32.7 radio_set_station / helicopter_shoot_vehicle.
    es.objectResolves().set("veh1", true);
    // CONFIRMED: the record is sent as soon as the vehicle resolves, BEFORE
    // the hasRadio/stationCount gate is even consulted - so this failing
    // call (OPEN on hasRadio) already counts as "sent" once.
    fails(host, gp, "radio_set_station('veh1', 3)", "vehicle has a radio");
    CHECK(es.vehicleRadio("veh1").recordSentCount == 1);
    es.vehicleRadio("veh1").hasRadio.set(true);
    es.vehicleRadio("veh1").stationCount.set(5);
    es.vehicleRadio("veh1").stationRefused.set("3", false);
    ok(host, gp, "radio_set_station('veh1', 3)");
    CHECK(es.vehicleRadio("veh1").currentStation == 3);
    CHECK(es.vehicleRadio("veh1").recordSentCount == 2);
    ok(host, gp, "radio_set_station('veh1', 0)"); // CONFIRMED: station <= 0 rejected locally, record still sent
    CHECK(es.vehicleRadio("veh1").currentStation == 3); // unchanged
    CHECK(es.vehicleRadio("veh1").recordSentCount == 3);

    es.objectResolves().set("unknownheli", false); // explicitly confirmed "does not resolve"
    es.objectResolves().set("target1", true);
    ok(host, gp, "assert(helicopter_shoot_vehicle('unknownheli', 'target1') == false)");

    es.objectResolves().set("heli1", true);
    fails(host, gp, "helicopter_shoot_vehicle('heli1', 'target1')", "helicopter AI-drive-state gate");
    es.helicopter("heli1").aiDriveStateOk.set(false);
    ok(host, gp, "assert(helicopter_shoot_vehicle('heli1', 'target1') == false)");
    es.helicopter("heli1").aiDriveStateOk.set(true);
    fails(host, gp, "helicopter_shoot_vehicle('heli1', 'target1')", "shared helicopter-fire dispatcher");
    es.helicopterFireDispatcherResult().set(true);
    ok(host, gp, "assert(helicopter_shoot_vehicle('heli1', 'target1') == true)");

    if (g_failures == 0) {
        std::cout << "All batch31_32 checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
