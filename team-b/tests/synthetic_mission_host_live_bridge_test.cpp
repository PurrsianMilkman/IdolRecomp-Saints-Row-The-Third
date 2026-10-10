// Synthetic tests for Mission-host Phase 2 (2026-10-09, branch
// feature/mission-host-phase2-live-bridge) - the live position bridge and
// name->live-object resolver added to EngineState
// (EngineState::liveObjectKind()/isLiveObject()/bridgeLiveCharacterPosition()/
// bridgeLiveVehiclePosition()/clearLiveObjectsForTesting(), engine_state.h,
// right after getOrCreateVehicle()/vehicles()). See
// results/design_mission_host_in_engine_loop_20261009.md §3(c) for the
// design this implements, and tools/sr3_viewer.cpp's own runPlaytest()
// (the "MISSION-HOST PHASE 2" doc comment, right before the Phase 1
// per-frame mission-drive block) for the one real production caller.
//
// GROUNDED (2026-10-09, post-merge) against spec-zone-data-format.md
// §11.10 ("Named-object resolution, end to end") and spec-lua-api-
// behaviour.md §4.13's corrected resolver-family paragraph, both landed on
// main mid-task and merged into this branch: the real engine's own name
// map is itself inherently LIVE (an entry exists iff a real producer has
// registered a live handle for it), and "#PLAYER1#" is one of 8 CLOSED
// sentinels the real generic resolver short-circuits straight to "the
// current player object" without ever touching that map - see
// EngineState::liveObjectKind()'s own doc comment (engine_state.h) for the
// full citation trail this test's §2 below exercises concretely.
//
// This file is a pure EngineState unit test (no Host/Lua needed for most
// of it) PLUS one small end-to-end Lua section proving the bridge feeds
// the SAME storage the real get_dist native already reads - i.e. the
// bridge required ZERO changes to any existing stub (src/lua_spec_
// confirmed_stubs_b05_invuln_r15_23.cpp's own stub_get_dist is completely
// untouched by this task).
//
// What this file does NOT claim:
//   - That any REAL mission script currently reaches get_dist with a live
//     position behind it end-to-end (Phase 1's own manual verification
//     found every real mission stem tried hits a DIFFERENT, earlier OPEN
//     gap first - see this task's own HANDOFF/report). This file proves
//     the bridge/resolver mechanism itself, in isolation, the same
//     "mechanism, not an unblock claim" discipline every other synthetic
//     luahost test in this project already follows.
//   - That the one generic PlaytestVehicleRig demo vehicle in
//     runPlaytest() is bridged under any name - it deliberately is NOT
//     (see sr3_viewer.cpp's own doc comment at the bridge call site for
//     why: no mission script anywhere references that car by name, so
//     giving it one would be fabricating an identity). This file's own
//     vehicle-kind coverage below uses an arbitrary fixture name ("v1",
//     the same convention tests/synthetic_luahost_batch5_hud_
//     customization_test.cpp already uses for a vehicle fixture), proving
//     the API itself works for vehicles - not that this project's live
//     playtest currently calls it for one.
#include <cmath>
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

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// Mirrored, not shared - the same throwsOpen() helper several other
// synthetic luahost tests in this project already carry their own copy of
// (e.g. tests/synthetic_minimap_live_transform_test.cpp).
template <typename F>
bool throwsOpen(F f) {
    try {
        f();
    } catch (const OpenStateError&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    // ======================================================================
    // Part 1: pure EngineState coverage - the resolver and the bridge, no
    // Lua involved. A fresh EngineState() still runs applySpecInitialState()
    // (Host's own constructor normally does this - a bare EngineState does
    // NOT, so this part seeds the one fact it needs - "#PLAYER1#"/
    // "#PLAYER2#" pre-resolve true - directly, matching Sec29.4, rather than
    // pulling in a full Host just to prove a C++-level API).
    // ======================================================================
    {
        EngineState es;
        es.objectResolves().set("#PLAYER1#", true);  // Sec29.4's own CONFIRMED pre-seed (applySpecInitialState)
        es.objectResolves().set("#PLAYER2#", true);  // ditto - the exact case this test's own §2 below turns on

        // --- Brand-new resolver: kNone for everything until the bridge writes something. ---
        CHECK(es.liveObjectKind("#PLAYER1#") == EngineState::LiveObjectKind::kNone);
        CHECK(!es.isLiveObject("#PLAYER1#"));
        CHECK(es.liveObjectKind("some_npc_nobody_ever_bridged") == EngineState::LiveObjectKind::kNone);

        // --- Reading worldPosition before the bridge has ever run throws OpenStateError,
        // exactly as get_dist's own doc comment (character_state.h) says it must -
        // "resolved object, unknown position" is a real OPEN gap, not a crash. ---
        CHECK(throwsOpen([&] { (void)es.getOrCreateCharacter("#PLAYER1#").worldPosition.get(); }));

        // --- The bridge: writes real data into that SAME field, and flips the resolver. ---
        es.bridgeLiveCharacterPosition("#PLAYER1#", WorldPosition{10.0, 1.5, -20.0});
        CHECK(es.liveObjectKind("#PLAYER1#") == EngineState::LiveObjectKind::kCharacter);
        CHECK(es.isLiveObject("#PLAYER1#"));
        {
            const WorldPosition& p = es.getOrCreateCharacter("#PLAYER1#").worldPosition.get();
            CHECK(near(p.x, 10.0) && near(p.y, 1.5) && near(p.z, -20.0));
        }

        // ==================================================================
        // §2: the single most important property this resolver has to get
        // right - it is a GENUINELY different axis from objectResolves(),
        // not a restatement of it. "#PLAYER2#" is pre-resolved true in
        // objectResolves() (same Sec29.4 pre-seed as "#PLAYER1#" above) but
        // this project's single-player-only scope means no session this
        // host can construct ever makes a second player LIVE - and indeed,
        // nothing has bridged it, so the resolver must honestly say kNone
        // for it even though objectResolves() says true.
        // ==================================================================
        CHECK(es.objectResolves().get("#PLAYER2#") == true); // resolves (mission-fiction sense)...
        CHECK(es.liveObjectKind("#PLAYER2#") == EngineState::LiveObjectKind::kNone); // ...but never live
        CHECK(!es.isLiveObject("#PLAYER2#"));

        // --- And the converse direction: a name the resolver calls live need not
        // be a name objectResolves() has ever heard of at all (this is the
        // vehicle-fixture case - see Part 1 continued, below) - the two maps
        // are independent, neither implies the other. ---
        CHECK(!es.objectResolves().known("v1"));
        es.bridgeLiveVehiclePosition("v1", WorldPosition{100.0, 0.0, 200.0});
        CHECK(es.liveObjectKind("v1") == EngineState::LiveObjectKind::kVehicle);
        CHECK(es.isLiveObject("v1"));
        CHECK(!es.objectResolves().known("v1")); // still never touched - the bridge does not write objectResolves()
        {
            const WorldPosition& p = es.getOrCreateVehicle("v1").worldPosition.get();
            CHECK(near(p.x, 100.0) && near(p.z, 200.0));
        }

        // --- A character and a vehicle bridged under DIFFERENT names don't
        // collide with each other's kind. ---
        CHECK(es.liveObjectKind("#PLAYER1#") == EngineState::LiveObjectKind::kCharacter);
        CHECK(es.liveObjectKind("v1") == EngineState::LiveObjectKind::kVehicle);

        // --- A later frame's bridge call for the SAME name overwrites the
        // position (the live per-frame case) and keeps the kind. ---
        es.bridgeLiveCharacterPosition("#PLAYER1#", WorldPosition{11.0, 1.5, -21.0});
        {
            const WorldPosition& p = es.getOrCreateCharacter("#PLAYER1#").worldPosition.get();
            CHECK(near(p.x, 11.0) && near(p.z, -21.0));
        }
        CHECK(es.liveObjectKind("#PLAYER1#") == EngineState::LiveObjectKind::kCharacter);

        // ==================================================================
        // clearLiveObjectsForTesting(): resets the LIVENESS answer only -
        // the underlying worldPosition this project's OTHER OpenValue
        // fields already follow a "sticky until set()/forget()" convention
        // for stays exactly as last written (documented behaviour, not an
        // accident) - these are two independently-scoped facts ("is it
        // live right now" vs. "what was the last position ever written"),
        // and a caller that wants the second one cleared too still has
        // worldPosition.forget() available directly, unaffected by this call.
        // ==================================================================
        es.clearLiveObjectsForTesting();
        CHECK(es.liveObjectKind("#PLAYER1#") == EngineState::LiveObjectKind::kNone);
        CHECK(es.liveObjectKind("v1") == EngineState::LiveObjectKind::kNone);
        {
            // Position itself untouched by clearLiveObjectsForTesting().
            const WorldPosition& p = es.getOrCreateCharacter("#PLAYER1#").worldPosition.get();
            CHECK(near(p.x, 11.0) && near(p.z, -21.0));
        }
    }

    // ======================================================================
    // Part 2: end-to-end through real Lua - the actual consumer, get_dist
    // (spec-lua-api-behaviour.md Sec4.2, src/lua_spec_confirmed_stubs_b05_
    // invuln_r15_23.cpp's own stub_get_dist) - completely unmodified by this
    // task. Proves the bridge really does feed the SAME CharacterState::
    // worldPosition field that native reads, with zero stub-side changes.
    // ======================================================================
    {
        std::vector<RegisteredName> fixture = {{"get_dist", "gameplay"}};
        Host host(fixture);
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        // Host's own constructor already ran applySpecInitialState() -
        // "#PLAYER1#" is pre-resolved true (Sec29.4) without this test
        // setting it itself, unlike Part 1's bare EngineState above.
        CHECK(es.objectResolves().get("#PLAYER1#") == true);

        // Before the bridge ever runs: a resolved name with no position yet
        // is the REAL, documented "resolved object, unknown position" case -
        // get_dist's own stub throws OpenStateError via openGuard, which
        // Host::runChunk() surfaces as a normal Lua pcall error.
        es.objectResolves().set("npc1", true);
        {
            auto r = host.runChunk(gp, "return get_dist('#PLAYER1#', 'npc1')", "live_bridge_test.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("OPEN") != std::string::npos ||
                  r.pcallError.find("character world position") != std::string::npos);
        }

        // Bridge BOTH positions live (the SAME shape tools/sr3_viewer.cpp's
        // own per-frame call uses for "#PLAYER1#" - this test adds a second
        // bridged character to exercise the actual distance math, which
        // runPlaytest() itself has no second live character to feed yet -
        // see this task's own report for that honestly-scoped boundary).
        es.bridgeLiveCharacterPosition("#PLAYER1#", WorldPosition{0.0, 0.0, 0.0});
        es.bridgeLiveCharacterPosition("npc1", WorldPosition{3.0, 0.0, 4.0});
        {
            auto r = host.runChunk(gp, "g_d = get_dist('#PLAYER1#', 'npc1')", "live_bridge_test.lua");
            CHECK(r.loadOk && r.pcallOk);
            lua_getglobal(gp, "g_d");
            double d = lua_tonumber(gp, -1);
            lua_pop(gp, 1);
            CHECK(near(d, 5.0, 1e-6)); // real 3-4-5 triangle, now answered by LIVE data, not a stub
        }

        // A name that resolves FALSE (a mission-fiction object this project
        // knows by name but knows does NOT exist, e.g. a group member never
        // spawned) hits get_dist's OWN separate, CONFIRMED "could not
        // resolve" FLT_MAX sentinel path - a genuinely different, honest
        // outcome from the OPEN-position case above, and this bridge
        // changes neither. (NOTE: objectResolves().get() on a name NEVER
        // even registered throws OpenStateError, not a false/FLT_MAX
        // result - stub_get_dist calls .get() directly with no probe
        // fallback, so "never heard of this name" and "heard of it, knows
        // it doesn't resolve" are two different real outcomes; this checks
        // the second one, which is the one get_dist's own Sec4.2 contract
        // actually documents the FLT_MAX sentinel for.)
        es.objectResolves().set("never_spawned_in_this_session", false);
        {
            auto r = host.runChunk(gp, "g_d2 = get_dist('#PLAYER1#', 'never_spawned_in_this_session')",
                                    "live_bridge_test.lua");
            CHECK(r.loadOk && r.pcallOk);
            lua_getglobal(gp, "g_d2");
            double d2 = lua_tonumber(gp, -1);
            lua_pop(gp, 1);
            CHECK(d2 > 1e30); // FLT_MAX sentinel, Sec4.2
        }
    }

    std::cerr << (g_failures == 0 ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
