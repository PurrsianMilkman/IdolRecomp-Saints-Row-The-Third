// Synthetic coverage for the in-world trigger-volume overlap test and
// callback dispatch (this task, 2026-10-10): EngineState::
// TriggerVolumeContainsPoint (spec-lua-api-behaviour.md Sec1.14's own
// CONFIRMED shape-code semantics: 0=oriented box, 1=sphere, 2=capped
// cylinder) and the real enter/exit transition -> BareThreadTable::
// allocate() dispatch pattern tools/sr3_viewer.cpp's own runPlaytest() now
// uses in its live --mission path.
//
// This test exercises the exact production EngineState methods (not a
// reimplementation) directly - the only thing tools/sr3_viewer.cpp's own
// per-frame block does beyond what's tested here is loop over real zone
// data and real per-frame player position, both already covered
// separately (tools/zone_object_resolver_probe.cpp against real data;
// tests/synthetic_mission_host_live_bridge_test.cpp for the position
// bridge).
//
// UPDATED 2026-10-10 (Trigger_Volume on-disk shape/extent/radius wiring
// task): shape/extent IS now resolvable from real on-disk zone data
// (spec-lua-api-behaviour.md Sec1.14's own "On-disk gaps - RESOLVED
// 2026-10-10" paragraph; the real reader lives in include/sr3zone/
// zone_objects.h's TriggerShapeType()/TriggerShapeParam1()/
// TriggerShapeParam2()/TriggerShapeCodeForTypeString(), wired into
// tools/sr3_viewer.cpp's own tryAutoRegister() - see that function's own
// doc comment). This test still exercises the containment/dispatch
// MECHANISM with caller-supplied geometry (same shape as before this task -
// the mechanism itself does not care where a TriggerVolumeGeometry came
// from), but the "on_boat_trigger" sub-test below now uses the REAL shape
// this project's own tools/zone_object_resolver_probe.cpp read directly off
// the real shipped `sr3_city_0.vpp_pc` (reported stdout, re-confirmed this
// task): position (2488.54, 6.82348, -4.15918), `trigger_shape_type` =
// "bounding box" (enum 0, NOT a sphere - the old CHOSEN radius=3.0 sphere
// stand-in this test previously used was never the real shape), local-space
// min corner (-87.8214, 0, -22.2829), max corner (87.8214, 30.7825,
// 22.2829) - a large box, consistent with a trigger meant to catch the
// player anywhere on/near a boat deck. The cylinder sub-test above still
// uses a CHOSEN axis endpoint (Sec1.14's own "true source not identified"
// OPEN item for that one field, unresolved by this task).
#include <iostream>
#include <string>

#include "sr3luahost/bare_globals.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"

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

} // namespace

int main() {
    // --- TriggerVolumeContainsPoint: the three real shape codes ---------
    {
        EngineState::TriggerVolumeGeometry sphere;
        sphere.px = 10; sphere.py = 0; sphere.pz = 0;
        sphere.shapeCode = 1; sphere.radius = 5.0;
        CHECK(EngineState::TriggerVolumeContainsPoint(sphere, 10, 0, 0));   // dead centre
        CHECK(EngineState::TriggerVolumeContainsPoint(sphere, 14, 0, 0));   // on the boundary (<=)
        CHECK(!EngineState::TriggerVolumeContainsPoint(sphere, 16, 0, 0));  // just outside

        // Box geometry: REAL on-disk shape (trigger_shape_param1/param2,
        // Sec1.14 2026-10-10 resolution) is a local-space MIN/MAX corner, not
        // a symmetric half-extent - this is a (deliberately asymmetric, to
        // actually exercise that it's a corner pair and not a half-extent)
        // min/max box around the origin.
        EngineState::TriggerVolumeGeometry box;
        box.px = 0; box.py = 0; box.pz = 0;
        box.shapeCode = 0;
        box.boxMinX = -2; box.boxMinY = -3; box.boxMinZ = -1;
        box.boxMaxX = 2;  box.boxMaxY = 3;  box.boxMaxZ = 4;
        CHECK(EngineState::TriggerVolumeContainsPoint(box, 1.9, -2.9, 3.9));
        CHECK(EngineState::TriggerVolumeContainsPoint(box, 2.0, 3.0, -1.0)); // exact boundary (min Z)
        CHECK(EngineState::TriggerVolumeContainsPoint(box, 2.0, 3.0, 4.0));  // exact boundary (max Z)
        CHECK(!EngineState::TriggerVolumeContainsPoint(box, 2.1, 0, 0));
        CHECK(!EngineState::TriggerVolumeContainsPoint(box, 0, 3.1, 0));
        CHECK(!EngineState::TriggerVolumeContainsPoint(box, 0, 0, -1.1)); // below min Z
        CHECK(!EngineState::TriggerVolumeContainsPoint(box, 0, 0, 4.1));  // above max Z

        EngineState::TriggerVolumeGeometry cyl;
        cyl.px = 0; cyl.py = 0; cyl.pz = 0;
        cyl.shapeCode = 2; cyl.ex = 0; cyl.ey = 10; cyl.ez = 0; // axis straight up, length 10
        cyl.radius = 2.0;
        CHECK(EngineState::TriggerVolumeContainsPoint(cyl, 1.0, 5.0, 0.0));   // mid-axis, inside radius
        CHECK(!EngineState::TriggerVolumeContainsPoint(cyl, 3.0, 5.0, 0.0));  // mid-axis, outside radius
        CHECK(!EngineState::TriggerVolumeContainsPoint(cyl, 0.0, -1.0, 0.0)); // below the cap (t<0)
        CHECK(!EngineState::TriggerVolumeContainsPoint(cyl, 0.0, 11.0, 0.0)); // above the cap (t>1)

        EngineState::TriggerVolumeGeometry unknownShape;
        unknownShape.shapeCode = 7; // not 0/1/2
        CHECK(!EngineState::TriggerVolumeContainsPoint(unknownShape, 0, 0, 0)); // Sec1.14: any other value fails
    }

    // --- registerTriggerGeometry()/triggerGeometry()/triggerGeometryMap() ---
    // REAL on_boat_trigger shape/extent (not the old CHOSEN sphere stand-in),
    // per tools/zone_object_resolver_probe.cpp's own real-cache read - see
    // this file's own top comment for the full citation.
    {
        EngineState es;
        CHECK(es.triggerGeometry("on_boat_trigger") == nullptr);
        EngineState::TriggerVolumeGeometry g;
        g.px = 2488.54; g.py = 6.82348; g.pz = -4.15918;
        g.shapeCode = 0; // REAL: "bounding box", not a sphere
        g.boxMinX = -87.8214; g.boxMinY = 0;       g.boxMinZ = -22.2829;
        g.boxMaxX = 87.8214;  g.boxMaxY = 30.7825; g.boxMaxZ = 22.2829;
        g.positionKnown = true;
        g.shapeKnown = true;
        es.registerTriggerGeometry("on_boat_trigger", g);
        const EngineState::TriggerVolumeGeometry* got = es.triggerGeometry("on_boat_trigger");
        CHECK(got != nullptr && got->positionKnown && got->px == g.px);
        CHECK(got != nullptr && got->shapeKnown && got->shapeCode == 0);
        CHECK(es.triggerGeometryMap().size() == 1);
        // A point well inside the real boat deck box (e.g. the boat's own
        // position, dx=dy=dz=0) is contained; a point far outside is not -
        // confirms the real extent (a ~176x31x45-unit box) is actually wired
        // through, not just stored.
        CHECK(EngineState::TriggerVolumeContainsPoint(g, g.px, g.py, g.pz));
        CHECK(!EngineState::TriggerVolumeContainsPoint(g, g.px + 200, g.py, g.pz));
    }

    // --- onTriggerCallbacksMap()/onTriggerExitCallbacksMap() iteration ---
    {
        EngineState es;
        CHECK(es.onTriggerCallbacksMap().empty());
        es.registerOnTriggerCallback("on_boat_trigger", "m09_player_on_boat_enter_cb");
        es.registerOnTriggerExitCallback("on_boat_trigger", "m09_player_on_boat_exit_cb");
        CHECK(es.onTriggerCallbacksMap().size() == 1);
        CHECK(es.onTriggerCallbacksMap().at("on_boat_trigger") == "m09_player_on_boat_enter_cb");
        CHECK(es.onTriggerExitCallbacksMap().at("on_boat_trigger") == "m09_player_on_boat_exit_cb");
    }

    // --- end-to-end: enter/exit TRANSITION (never a steady per-frame
    // refire) drives a real BareThreadTable::allocate() spawn, exactly the
    // runPlaytest() per-frame block's own pattern, reusing the SAME
    // coroutine-spawn primitive commit 0b6c934 already wired for
    // tools/lua_host_run.cpp's own --probe-autopilot path. -----------------
    {
        Host host({});
        EngineState& es = host.engineState();
        lua_State* L = host.gameplayState();
        BareThreadTable& threads = host.bareGlobals().threads();

        EngineState::TriggerVolumeGeometry g;
        g.px = 0; g.py = 0; g.pz = 0; g.shapeCode = 1; g.radius = 5.0;
        es.registerTriggerGeometry("on_boat_trigger", g);
        es.registerOnTriggerCallback("on_boat_trigger", "m09_player_on_boat_enter_cb");
        es.registerOnTriggerExitCallback("on_boat_trigger", "m09_player_on_boat_exit_cb");

        // 2026-10-11 (m09/m13/m05 trigger-stall task): both callbacks now
        // read their own real first parameter ("human") and compare it
        // against LOCAL_PLAYER, matching every real mission script's own
        // identity-gated on_trigger callback shape (m09_player_on_boat_enter_cb
        // et al) - this is the regression coverage for the nargs=0 bug fixed
        // in tools/sr3_viewer.cpp's own identical dispatch pattern below.
        auto r1 = host.runChunk(
            L,
            "ENTER_RAN = false\nfunction m09_player_on_boat_enter_cb(human) if human == LOCAL_PLAYER then "
            "ENTER_RAN = true end end\n",
            "trigger_test.lua");
        CHECK(r1.loadOk && r1.pcallOk);
        auto r2 = host.runChunk(
            L,
            "EXIT_RAN = false\nfunction m09_player_on_boat_exit_cb(human) if human == LOCAL_PLAYER then "
            "EXIT_RAN = true end end\n",
            "trigger_test.lua");
        CHECK(r2.loadOk && r2.pcallOk);

        auto driveOneFrame = [&](double x, double y, double z) {
            bool transitioned = false;
            bool entered = false;
            for (const auto& kv : es.triggerGeometryMap()) {
                const std::string& name = kv.first;
                const bool nowInside = EngineState::TriggerVolumeContainsPoint(kv.second, x, y, z);
                bool& wasInside = es.triggerCurrentlyInside(name);
                if (nowInside == wasInside) continue;
                wasInside = nowInside;
                transitioned = true;
                entered = nowInside;
                const std::string& cb = nowInside ? es.onTriggerCallbackValue(name) : es.onTriggerExitCallbackValue(name);
                // 2026-10-11 fix (m09/m13/m05 trigger-stall task): push the
                // "human" argument every real on_trigger/on_trigger_exit
                // callback declares - mirrors the exact fix applied to
                // tools/sr3_viewer.cpp's own identical dispatch (same
                // "#PLAYER1#" sentinel, nargs=1 instead of the old nargs=0).
                if (!cb.empty()) {
                    lua_pushstring(L, "#PLAYER1#");
                    threads.allocate(L, cb, 1, false);
                }
            }
            return std::make_pair(transitioned, entered);
        };

        // Frame 0: starts outside (triggerCurrentlyInside defaults false) -
        // player far away - no transition, nothing spawned.
        auto t0 = driveOneFrame(1000, 1000, 1000);
        CHECK(!t0.first);
        CHECK(threads.liveCount() == 0);

        // Frame 1: player walks INTO the volume - ENTER transition, spawns
        // the real on_trigger callback - but it has NOT run yet (spawn-only,
        // matching Sec1.14's own confirmed spawn-now/run-later dispatch).
        auto t1 = driveOneFrame(0, 0, 0);
        CHECK(t1.first && t1.second);
        CHECK(threads.liveCount() == 1);
        auto ok1 = host.runChunk(L, "assert(ENTER_RAN == false)", "trigger_test.lua");
        CHECK(ok1.loadOk && ok1.pcallOk);

        // Frame 2 (still inside): no re-fire - the real poll's own "only on
        // a transition" rule, same set-membership edge-detector Sec1.14
        // confirms for the real per-frame poll.
        auto t2 = driveOneFrame(0, 0, 0);
        CHECK(!t2.first);

        // A scheduler pass (one per rendered frame, same cadence runPlaytest()
        // itself drives) is what actually resumes the spawned coroutine.
        BareThreadTable::PassResult pass = host.runScriptThreadSchedulerPass();
        CHECK(pass.resumed == 1 && pass.released == 1);
        auto ok2 = host.runChunk(L, "assert(ENTER_RAN == true)", "trigger_test.lua");
        CHECK(ok2.loadOk && ok2.pcallOk);

        // Player walks back OUT - EXIT transition, spawns the exit callback.
        auto t3 = driveOneFrame(1000, 1000, 1000);
        CHECK(t3.first && !t3.second);
        CHECK(threads.liveCount() == 1);
        pass = host.runScriptThreadSchedulerPass();
        CHECK(pass.resumed == 1 && pass.released == 1);
        auto ok3 = host.runChunk(L, "assert(EXIT_RAN == true)", "trigger_test.lua");
        CHECK(ok3.loadOk && ok3.pcallOk);
    }

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost TRIGGER-VOLUME OVERLAP TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
