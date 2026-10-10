// Synthetic tests for the Sec6 backlog close-out
// (src/lua_spec_confirmed_stubs_b29_sec6_notoriety_mission.cpp):
// disable_specific_notoriety_group (Sec6.2), notoriety_set_min_and_max
// (Sec6.3), notoriety_set_max (Sec6.5), notoriety_set (Sec6.7),
// notoriety_force_no_spawn_group (Sec6.10), notoriety_set_min (Sec6.11),
// mission_end_success (Sec6.12), notoriety_reset (Sec6.16),
// mission_end_to_activity (Sec6.20), mission_set_cancel_warp_location
// (Sec6.21). Same hand-written-fixture convention as every other
// synthetic_luahost_*_test. All 10 are `gameplay` per Sec6's own scope note
// (the 1,014-entry gameplay table, tools/lua_all_registered_1430_tagged.txt).
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
    auto r = host.runChunk(L, code, "b29_sec6_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "b29_sec6_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"disable_specific_notoriety_group", "gameplay"},
        {"notoriety_set_min_and_max", "gameplay"},
        {"notoriety_set_max", "gameplay"},
        {"notoriety_set", "gameplay"},
        {"notoriety_force_no_spawn_group", "gameplay"},
        {"notoriety_set_min", "gameplay"},
        {"mission_end_success", "gameplay"},
        {"notoriety_reset", "gameplay"},
        {"mission_end_to_activity", "gameplay"},
        {"mission_set_cancel_warp_location", "gameplay"},
    };
}

} // namespace

int main() {
    // ----------------------------------------------------------------
    // disable_specific_notoriety_group (Sec6.2): unresolved group throws;
    // resolved group + unresolved tier-range throws; resolved + out-of-
    // range -> false, no write; resolved + in-range -> true, item written.
    //
    // 'police' is one of the 24 CONFIRMED spawn-group names applySpecInitialState
    // now pre-seeds (lua_spec_initial_state.cpp item 13, cross-referenced against
    // spec-tables-progression.md Sec6.3's own independently-derived name list for
    // the SAME 0x008ed350 table) - it resolves with no manual set() call below.
    // A name genuinely outside the 24 still throws, which the first check proves.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        fails(host, gp, "disable_specific_notoriety_group('not_a_real_group', 1, 'riot_gear', true)",
              "notoriety spawn-group resolution");
        fails(host, gp, "disable_specific_notoriety_group('police', 1, 'riot_gear', true)",
              "notoriety group tier in-range");
        es.notorietyGroupTierInRangeResolves().set("police#1", false);
        ok(host, gp, "assert(disable_specific_notoriety_group('police', 1, 'riot_gear', true) == false)");
        CHECK(es.getOrCreateNotorietySpawnGroup("police").disableSpecificGroupWriteCount == 0);
        es.notorietyGroupTierInRangeResolves().set("police#1", true);
        ok(host, gp, "assert(disable_specific_notoriety_group('police', 1, 'riot_gear', true) == true)");
        auto& group = es.getOrCreateNotorietySpawnGroup("police");
        CHECK(group.disableSpecificGroupWriteCount == 1);
        CHECK(group.tierItemFlags[1]["riot_gear"] == true);
    }

    // ----------------------------------------------------------------
    // notoriety_set_min_and_max (Sec6.3): unknown name -> sentinel 5,
    // CONFIRMED skip (no track entry created at all); known name, default
    // (non-host) -> replicate only, no field write but the call still
    // counts; host mode -> direct write.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "notoriety_set_min_and_max('nonexistent_track', 1.0, 9.0)");
        CHECK(es.notorietyAliasTrackStates().empty()); // CONFIRMED sentinel-checked skip - no entry at all
        ok(host, gp, "notoriety_set_min_and_max('luchadores', 1.0, 9.0)"); // non-host default: replicate only
        {
            auto& t = es.notorietyAliasTrackStates()[0];
            CHECK(t.setMinAndMaxCount == 1);
            CHECK(t.minValue == 0.0 && t.maxValue == 0.0); // no direct write while non-host
        }
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true);
        ok(host, gp, "notoriety_set_min_and_max('luchadores', 1.0, 9.0)");
        {
            auto& t = es.notorietyAliasTrackStates()[0];
            CHECK(t.minValue == 1.0 && t.maxValue == 9.0);
            CHECK(t.setMinAndMaxCount == 2);
        }
        es.coopSession().present.set(false);
    }

    // ----------------------------------------------------------------
    // notoriety_set_max/notoriety_set/notoriety_set_min (Sec6.5/6.7/6.11):
    // NO sentinel guard - an unrecognized name still writes using code 5.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true);
        ok(host, gp, "notoriety_set_max('totally_unknown', 42.0)");
        CHECK(es.notorietyAliasTrackStates()[EngineState::kNotorietyAliasTrackNotFound].maxValue == 42.0);
        ok(host, gp, "notoriety_set('brotherhood', 3.0)");
        CHECK(es.notorietyAliasTrackStates()[1].currentValue == 3.0);
        ok(host, gp, "notoriety_set_min('los_carnales', -1.0)");
        CHECK(es.notorietyAliasTrackStates()[2].minValue == -1.0);
    }

    // ----------------------------------------------------------------
    // notoriety_force_no_spawn_group (Sec6.10): SAME group table as
    // Sec6.2, unconditional ungated write - no host-check at all.
    //
    // 'police' is CONFIRMED-pre-seeded (see the disable_specific_notoriety_group
    // block above) - it resolves below with no manual set() call; a name
    // outside the 24 still throws.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        fails(host, gp, "notoriety_force_no_spawn_group('not_a_real_group', true)", "notoriety spawn-group resolution");
        ok(host, gp, "notoriety_force_no_spawn_group('police', true)"); // non-host, STILL writes directly (no gate at all)
        CHECK(es.getOrCreateNotorietySpawnGroup("police").forceNoSpawn == true);
        CHECK(es.getOrCreateNotorietySpawnGroup("police").forceNoSpawnSetCount == 1);
        ok(host, gp, "notoriety_force_no_spawn_group('police', false)");
        CHECK(es.getOrCreateNotorietySpawnGroup("police").forceNoSpawn == false);
    }

    // ----------------------------------------------------------------
    // notoriety_reset (Sec6.16): sentinel-checked skip; host-only UI/meter
    // reset with NO replicated fallback; min/max both reset to 0xffffffff.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "notoriety_reset('nonexistent_track')");
        CHECK(es.notorietyAliasTrackStates().empty()); // sentinel-checked skip
        ok(host, gp, "notoriety_reset('luchadores')"); // non-host: NO UI/meter reset at all
        CHECK(es.notorietyUiMeterResetCount() == 0);
        {
            auto& t = es.notorietyAliasTrackStates()[0];
            CHECK(t.resetCount == 1);
            CHECK(t.minValue == 0.0 && t.maxValue == 0.0); // non-host: replicate only, no direct write
        }
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true);
        ok(host, gp, "notoriety_reset('luchadores')");
        CHECK(es.notorietyUiMeterResetCount() == 1); // host-only effect fired exactly once now
        {
            auto& t = es.notorietyAliasTrackStates()[0];
            CHECK(t.minValue == 4294967295.0 && t.maxValue == 4294967295.0);
            CHECK(t.resetCount == 2);
        }
    }

    // ----------------------------------------------------------------
    // mission_end_success (Sec6.12): SAME gate as mission_end_failure;
    // shares the SAME timer but its own dispatch counter; arg3 table
    // resolves up to 2 slots.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        // Fixed 2026-10-08 (Sec21.27a): activeAndRunning is now KNOWN false
        // by default (the real engine's own idle start-up phase), not OPEN.
        ok(host, gp, "mission_end_success('m1')"); // gate KNOWN false by default: silent no-op
        CHECK(es.missionContext().missionEndSuccessDispatchCount == 0);
        es.missionContext().activeAndRunning.set(false); // redundant with the real default - kept explicit for clarity
        ok(host, gp, "mission_end_success('m1')"); // gate false: silent no-op
        CHECK(es.missionContext().missionEndSuccessDispatchCount == 0);
        es.missionContext().activeAndRunning.set(true);
        es.missionContext().currentMissionName.set("m1");
        es.missionContext().currentMissionEnding.set(true);
        es.objectResolves().set("obj_a", true);
        es.objectResolves().set("obj_b", false);
        ok(host, gp, "mission_end_success('m1', 'text', {'obj_a', 'obj_b'})");
        CHECK(es.missionContext().missionEndTimer1Armed == true); // SAME shared timer mission_end_failure arms
        CHECK(es.missionContext().missionEndSuccessDispatchCount == 1);
        CHECK(es.missionContext().lastMissionEndSuccessSlot1Resolved == true);
        CHECK(es.missionContext().lastMissionEndSuccessSlot2Resolved == false);
        ok(host, gp, "mission_end_success('m1')"); // arg3 absent - both slots false, no throw
        CHECK(es.missionContext().lastMissionEndSuccessSlot1Resolved == false);
    }

    // ----------------------------------------------------------------
    // mission_end_to_activity (Sec6.20): SAME gate; gate miss -> nothing
    // happens at all; gate pass -> dispatch always runs, position capture
    // only when (resolved AND host).
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        // Fixed 2026-10-08 (Sec21.27a): activeAndRunning is now KNOWN false
        // by default (the real engine's own idle start-up phase), not OPEN.
        ok(host, gp, "mission_end_to_activity('m1', 'act1')"); // gate KNOWN false by default: nothing at all
        CHECK(es.missionContext().missionEndToActivityDispatchCount == 0);
        es.missionContext().activeAndRunning.set(false); // redundant with the real default - kept explicit for clarity
        ok(host, gp, "mission_end_to_activity('m1', 'act1')"); // gate false: nothing at all, no further throws
        CHECK(es.missionContext().missionEndToActivityDispatchCount == 0);
        es.missionContext().activeAndRunning.set(true);
        es.missionContext().currentMissionName.set("m1");
        es.missionContext().currentMissionEnding.set(true);
        es.objectResolves().set("act1", true);
        ok(host, gp, "mission_end_to_activity('m1', 'act1')"); // non-host: no position capture, dispatch still runs
        CHECK(es.missionContext().missionEndToActivityDispatchCount == 1);
        CHECK(es.missionContext().missionEndToActivityPositionCaptureCount == 0);
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true);
        ok(host, gp, "mission_end_to_activity('m1', 'act1')");
        CHECK(es.missionContext().missionEndToActivityDispatchCount == 2);
        CHECK(es.missionContext().missionEndToActivityPositionCaptureCount == 1);
    }

    // ----------------------------------------------------------------
    // mission-context-name task (2026-10-08): mission_end_success and
    // mission_end_to_activity read the EXACT SAME MissionContext::
    // currentMissionName/currentMissionEnding fields mission_end_failure
    // does (Sec1.5/Sec6.12/Sec6.20 all share the identical gate function
    // 0x006d7240) - so the one fix at the real bind call site
    // (EngineState::bindMissionPhaseMachine(), engine_state.h) that stops
    // mission_end_failure throwing OpenStateError on a real mission (see
    // tests/synthetic_luahost_trigger_mission_cluster_test.cpp's own new
    // coverage) closes the identical gap for BOTH natives here too, with no
    // separate per-native wiring needed. Proven here via the real
    // production bind path (beginMissionPhaseTracking()), not by a test
    // calling currentMissionName/currentMissionEnding.set() directly (that
    // case is already covered by the two blocks just above).
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        es.beginMissionPhaseTracking("mC");
        CHECK(es.missionContext().currentMissionName.get() == "mC");
        CHECK(es.missionContext().currentMissionEnding.get() == false);

        // Freshly bound, still alive: both natives fail CLOSED - no throw,
        // no dispatch - exactly the real behaviour that was previously
        // unreachable (an OpenStateError fired first).
        ok(host, gp, "mission_end_success('mC')");
        CHECK(es.missionContext().missionEndSuccessDispatchCount == 0);
        ok(host, gp, "mission_end_to_activity('mC', 'act1')");
        CHECK(es.missionContext().missionEndToActivityDispatchCount == 0);

        // Mission starts tearing down -> full gate pass for both, same
        // shared fields, same real bound name.
        es.missionContext().currentMissionEnding.set(true);
        ok(host, gp, "mission_end_success('mC')");
        CHECK(es.missionContext().missionEndSuccessDispatchCount == 1);
        es.objectResolves().set("act1", true);
        ok(host, gp, "mission_end_to_activity('mC', 'act1')");
        CHECK(es.missionContext().missionEndToActivityDispatchCount == 1);
    }

    // ----------------------------------------------------------------
    // mission_set_cancel_warp_location (Sec6.21): both absent -> clear
    // (gated on mission active); both resolve -> set; exactly one resolves
    // -> CONFIRMED no-op regardless of mission-active state (no throw even
    // with activeAndRunning left OPEN).
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("from1", true);
        // Exactly one resolved (arg2 never given) - CONFIRMED no-op, no
        // mission-gate read at all (activeAndRunning stays OPEN/unset).
        ok(host, gp, "mission_set_cancel_warp_location('from1')");
        CHECK(es.missionCancelWarpLocation().partialNoOpCount == 1);
        CHECK(es.missionCancelWarpLocation().setCount == 0);
        // Both absent. Fixed 2026-10-08 (Sec21.27a): activeAndRunning is now
        // KNOWN false by default (the real engine's own idle start-up
        // phase), not OPEN - the gate reads false, so this is a no-op, not
        // a throw.
        ok(host, gp, "mission_set_cancel_warp_location()");
        CHECK(es.missionCancelWarpLocation().clearCount == 0);
        es.missionContext().activeAndRunning.set(true);
        ok(host, gp, "mission_set_cancel_warp_location()"); // both absent, gate true -> clears
        CHECK(es.missionCancelWarpLocation().clearCount == 1);
        es.objectResolves().set("to1", true);
        ok(host, gp, "mission_set_cancel_warp_location('from1', 'to1')");
        CHECK(es.missionCancelWarpLocation().setCount == 1);
        CHECK(es.missionCancelWarpLocation().set == true);
        CHECK(es.missionCancelWarpLocation().fromName == "from1");
        CHECK(es.missionCancelWarpLocation().toName == "to1");
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_b29_sec6_notoriety_mission_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_b29_sec6_notoriety_mission_test: " << g_failures << " check(s) failed\n";
    return 1;
}
