// Synthetic tests for the 2026-10-07 native-implementation batch (fresh
// mission-drive stub-hit ranking, cross-referenced against spec coverage):
// audio_object_post_event (spec-lua-api-behaviour.md Sec2.4),
// character_is_ready (Sec7.27), character_playing_combat_move (Sec25.27),
// mesh_mover_hide/_show/_reset/_set_invulnerable (Sec9.16/9.17/9.12/14.9),
// door_close/door_lock (Sec9.14/9.15), object_spawn_pause/
// mip_streaming_pause (Sec60.1), city_zone_swap (Sec1.9),
// traffic_disable_lanes (Sec12.19), party_dismiss_all (Sec14.17),
// sidewalk_disable_nodes (Sec15.3), group_create_check_done (Sec22.18).
// Same fixture convention as tests/synthetic_luahost_tranche_realhits_test.cpp;
// all 16 names are "gameplay" in tools/lua_all_registered_1490_tagged.txt
// (grepped directly, not assumed).
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
        }                                                                                     \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "native_batch1007_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code`, expecting a Lua error whose message contains `needle`.
void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "native_batch1007_test.lua");
    if (!r.loadOk || r.pcallOk || r.pcallError.find(needle) == std::string::npos) {
        std::cerr << "expected refusal containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("success") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"audio_object_post_event", "gameplay"},
        {"character_is_ready", "gameplay"},
        {"character_playing_combat_move", "gameplay"},
        {"mesh_mover_hide", "gameplay"},
        {"mesh_mover_show", "gameplay"},
        {"mesh_mover_reset", "gameplay"},
        {"mesh_mover_set_invulnerable", "gameplay"},
        {"door_close", "gameplay"},
        {"door_lock", "gameplay"},
        {"object_spawn_pause", "gameplay"},
        {"mip_streaming_pause", "gameplay"},
        {"city_zone_swap", "gameplay"},
        {"traffic_disable_lanes", "gameplay"},
        {"party_dismiss_all", "gameplay"},
        {"sidewalk_disable_nodes", "gameplay"},
        {"group_create_check_done", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay only, for 15 of the 16. audio_object_post_event
    // is the one pre-existing exception (src/lua_host.cpp's own
    // unconditional `uiNames.push_back({"audio_object_post_event", "ui"})`,
    // real CONFIRMED dual-registration, spec-lua-bindings.md Sec13.5
    // addendum - not something this batch changed) - also registered into
    // the UI state regardless of its own "gameplay" tag.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            bool expectUi = (rn.name == "audio_object_post_event");
            CHECK(hasGlobal(host.uiState(), rn.name.c_str()) == expectUi);
        }
    }

    // audio_object_post_event: arg1 absent/non-string -> push 0; a resolved
    // name always succeeds (this project models no real Wwise ID table,
    // Sec2.4's own doc comment) and returns a fresh, increasing handle from
    // the same counter game_UI_audio_play uses. A >2-element table arg
    // aborts with push 0 regardless of arg1.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "assert(audio_object_post_event() == 0)");
        ok(host, gp, "local h1 = audio_object_post_event('ev1')\n"
                     "local h2 = audio_object_post_event('ev2')\n"
                     "assert(h1 > 0 and h2 == h1 + 1)"); // sequential counter, same as game_UI_audio_play
        ok(host, gp, "assert(audio_object_post_event('ev3', {1,2,3}) == 0)"); // >2 elements - CONFIRMED immediate failure
        ok(host, gp, "assert(audio_object_post_event('ev4', {1,2}, 'x') > 0)"); // exactly 2 + a string arg3 - within shape, succeeds
        // Real CONFIRMED dual-registration (spec-lua-bindings.md Sec13.5
        // addendum, src/lua_host.cpp's own specUiNames.push_back fix this
        // batch): the UI state must get the SAME real implementation, not
        // a stale generic nil-stub left over from before this native was
        // promoted to specConfirmedStubNames().
        ok(host, host.uiState(), "assert(audio_object_post_event('ev_ui') > 0)");
    }

    // character_is_ready: unresolved -> false (no throw: resolve failure is
    // itself an OPEN refusal at the SAME shared gate every other native
    // uses, tested separately below); dead -> false; alive+not-blocked ->
    // the remaining AND-chain is OPEN, refuses. 0x009a1dc0 (ragdolled) is
    // CONFIRMED: unseeded = false (standing NPC) so it reaches the AND-chain.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "character_is_ready('Nobody')", "named-object resolution");
        es.objectResolves().set("Alice", true);
        es.getOrCreateCharacter("Alice").isAlive.set(false);
        ok(host, gp, "assert(character_is_ready('Alice') == false)");
        es.getOrCreateCharacter("Alice").isAlive.set(true);
        ok(host, gp, "assert(character_is_ready('Alice') == true)");  // ce4556f: ordinary standing NPC is ready
        es.characterReadyBlocked().set("Alice", true);
        ok(host, gp, "assert(character_is_ready('Alice') == false)");
        es.characterReadyBlocked().set("Alice", false);
        ok(host, gp, "assert(character_is_ready('Alice') == true)");
    }

    // character_playing_combat_move: unresolved (OPEN, no .set() at all) ->
    // refuses, the SAME shared named-object-resolution gate every other
    // native uses; known-not-resolved (explicit .set(name,false)) -> false;
    // resolved -> the OPEN predicate, once seeded, passes through directly.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "character_playing_combat_move('Nobody')", "named-object resolution");
        es.objectResolves().set("NotAThing", false);
        ok(host, gp, "assert(character_playing_combat_move('NotAThing') == false)");
        es.objectResolves().set("Bob", true);
        ok(host, gp, "assert(character_playing_combat_move('Bob') == false)"); // ce4556f: default none
        es.characterPlayingCombatMove().set("Bob", true);
        ok(host, gp, "assert(character_playing_combat_move('Bob') == true)");
        es.characterPlayingCombatMove().set("Bob", false);
        ok(host, gp, "assert(character_playing_combat_move('Bob') == false)");
    }

    // mesh_mover_hide / mesh_mover_show: unresolved -> false, no state
    // mutation; resolved -> true, the shared +0x82 bit toggles.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "mesh_mover_hide('NoMover')", "named-object resolution");
        es.objectResolves().set("KnownMissingMover", false);
        ok(host, gp, "assert(mesh_mover_hide('KnownMissingMover') == false)");
        CHECK(es.findPlacedObject("NoMover") == nullptr);
        es.objectResolves().set("Mover1", true);
        ok(host, gp, "assert(mesh_mover_hide('Mover1') == true)");
        CHECK((es.getOrCreatePlacedObject("Mover1").flags082 & PlacedObjectState::kMeshMoverHiddenBit) != 0);
        ok(host, gp, "assert(mesh_mover_show('Mover1') == true)");
        CHECK((es.getOrCreatePlacedObject("Mover1").flags082 & PlacedObjectState::kMeshMoverHiddenBit) == 0);
    }

    // mesh_mover_reset: true on resolve, false otherwise, no bit claimed.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "mesh_mover_reset('NoMover')", "named-object resolution");
        es.objectResolves().set("KnownMissingMover2", false);
        ok(host, gp, "assert(mesh_mover_reset('KnownMissingMover2') == false)");
        es.objectResolves().set("Mover2", true);
        ok(host, gp, "assert(mesh_mover_reset('Mover2') == true)");
    }

    // mesh_mover_set_invulnerable: return reflects RESOLVE success, not the
    // invulnerable state itself (CONFIRMED, Sec14.9); bit 0x8 tracks arg2.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "mesh_mover_set_invulnerable('NoMover', true)", "named-object resolution");
        es.objectResolves().set("KnownMissingMover3", false);
        ok(host, gp, "assert(mesh_mover_set_invulnerable('KnownMissingMover3', true) == false)");
        es.objectResolves().set("Mover3", true);
        ok(host, gp, "assert(mesh_mover_set_invulnerable('Mover3', true) == true)");
        CHECK((es.getOrCreatePlacedObject("Mover3").flags082 & PlacedObjectState::kMeshMoverInvulnerableBit) != 0);
        ok(host, gp, "assert(mesh_mover_set_invulnerable('Mover3', false) == true)");
        CHECK((es.getOrCreatePlacedObject("Mover3").flags082 & PlacedObjectState::kMeshMoverInvulnerableBit) == 0);
    }

    // door_close: resolve-dependent (HIGH CONFIDENCE true/false reading,
    // see this function's own doc comment); clears the close-transient bits.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "door_close('NoDoor')", "named-object resolution");
        es.objectResolves().set("KnownMissingDoor", false);
        ok(host, gp, "assert(door_close('KnownMissingDoor') == false)");
        es.objectResolves().set("Door1", true);
        es.getOrCreatePlacedObject("Door1").flags082 = PlacedObjectState::kDoorCloseTransientBits;
        ok(host, gp, "assert(door_close('Door1') == true)");
        CHECK((es.getOrCreatePlacedObject("Door1").flags082 & PlacedObjectState::kDoorCloseTransientBits) == 0);
        // Default-argument form (no explicit boolean) must not crash.
        ok(host, gp, "assert(door_close('Door1') == true)");
    }

    // door_lock: no return value; bit 0x4 tracks arg2 unconditionally once resolved.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "door_lock('NoDoor', true)", "named-object resolution");
        es.objectResolves().set("KnownMissingDoor2", false);
        ok(host, gp, "door_lock('KnownMissingDoor2', true)"); // known not to resolve: silent no-op, no crash
        es.objectResolves().set("Door2", true);
        ok(host, gp, "door_lock('Door2', true)");
        CHECK((es.getOrCreatePlacedObject("Door2").flags082 & PlacedObjectState::kDoorLockBit) != 0);
        ok(host, gp, "door_lock('Door2', false)");
        CHECK((es.getOrCreatePlacedObject("Door2").flags082 & PlacedObjectState::kDoorLockBit) == 0);
    }

    // object_spawn_pause: the shared no-op stub - never refuses, never
    // mutates anything, regardless of resolve state.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "object_spawn_pause(true, 'anything')");
        ok(host, gp, "object_spawn_pause()");
    }

    // mip_streaming_pause: a single process-wide bit, aliased regardless of
    // the name argument (CONFIRMED real cross-script collision, Sec60.1).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.mipStreamingPaused() == false);
        ok(host, gp, "mip_streaming_pause(true, 'script_a_ref')");
        CHECK(es.mipStreamingPaused() == true);
        ok(host, gp, "mip_streaming_pause(false, 'totally_unrelated_script_b_ref')");
        CHECK(es.mipStreamingPaused() == false); // aliasing: a different name still clears the SAME bit
    }

    // city_zone_swap: no resolve dependency (Sec1.9 has no liveness-gate
    // step at all); records the raw (name, value) pair, default true.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "city_zone_swap('zone_pair_1')");
        CHECK(es.cityZoneSwapKnown("zone_pair_1") && es.cityZoneSwapValue("zone_pair_1") == true);
        ok(host, gp, "city_zone_swap('zone_pair_2', false)");
        CHECK(es.cityZoneSwapKnown("zone_pair_2") && es.cityZoneSwapValue("zone_pair_2") == false);
    }

    // traffic_disable_lanes: resolve-gated; records the raw enable/disable
    // boolean once resolved, untouched otherwise.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "traffic_disable_lanes('NoTrigger', false)", "named-object resolution");
        es.objectResolves().set("KnownMissingTrigger", false);
        ok(host, gp, "traffic_disable_lanes('KnownMissingTrigger', false)");
        CHECK(!es.trafficLanesKnown("KnownMissingTrigger"));
        es.objectResolves().set("LaneTrigger1", true);
        ok(host, gp, "traffic_disable_lanes('LaneTrigger1', false)");
        CHECK(es.trafficLanesKnown("LaneTrigger1") && es.trafficLanesEnabledValue("LaneTrigger1") == false);
        ok(host, gp, "traffic_disable_lanes('LaneTrigger1', true)");
        CHECK(es.trafficLanesEnabledValue("LaneTrigger1") == true);
    }

    // party_dismiss_all: zero-argument command, call-count only.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.partyDismissAllCount() == 0);
        ok(host, gp, "party_dismiss_all()");
        ok(host, gp, "party_dismiss_all()");
        CHECK(es.partyDismissAllCount() == 2);
    }

    // sidewalk_disable_nodes: resolve-gated; records the raw boolean.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "sidewalk_disable_nodes('NoGroup', true)", "named-object resolution");
        es.objectResolves().set("KnownMissingGroup", false);
        ok(host, gp, "sidewalk_disable_nodes('KnownMissingGroup', true)");
        CHECK(!es.sidewalkNodesKnown("KnownMissingGroup"));
        es.objectResolves().set("NodeGroup1", true);
        ok(host, gp, "sidewalk_disable_nodes('NodeGroup1', true)");
        CHECK(es.sidewalkNodesKnown("NodeGroup1") && es.sidewalkNodesDisabledValue("NodeGroup1") == true);
    }

    // group_create_check_done: always true - this project builds no
    // per-member group registry, so the resolved+alive branch's own
    // two-list walk is vacuously "done" (Sec22.18's own doc comment) -
    // true BOTH for a never-created group and for one group_create_do did
    // create, and never refuses (the one point of this native: unblocking
    // a `repeat thread_yield() until group_create_check_done(name)` poll).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "assert(group_create_check_done('NeverCreated') == true)");
        es.markGroupCreated("grp_real");
        ok(host, gp, "assert(group_create_check_done('grp_real') == true)");
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_native_batch1007_test: all checks passed\n";
    return 0;
}
