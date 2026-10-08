// Synthetic tests for player_controls_disable (spec-lua-api-behaviour.md
// Sec7.22), player_controls_enable (Sec7.7) and player_movement_disable
// (Sec7.25), wired to the real control-state stack and input context of
// spec-player-runtime.md Sec2.5/Sec2.6/Sec2.9 and Sec1.11.1. Same fixture
// convention as tests/synthetic_luahost_tranche_realhits_test.cpp; all three
// names are "gameplay" in tools/lua_all_registered_1490_tagged.txt.
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
    auto r = host.runChunk(L, code, "player_controls_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code`, expecting a Lua error whose message contains `needle`.
void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "player_controls_test.lua");
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
        {"player_controls_disable", "gameplay"},
        {"player_controls_enable", "gameplay"},
        {"player_movement_disable", "gameplay"},
    };
}

const char* kPlayer = "#PLAYER1#";  // pre-resolved literal (Sec29.4)

} // namespace

int main() {
    // Registration: gameplay only.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    // CONFIRMED default (spec-player-runtime.md Sec2.10, 2026-10-03): a
    // fresh character's +0x1d99 byte and the global lock handle are both
    // KNOWN at construction - a zero-filled-allocation default (HIGH
    // CONFIDENCE for the byte, CONFIRMED self-balancing for the handle; see
    // CharacterState::controlsDisabled/movementDisabled and
    // EngineState::playerControlsLockHandle_'s own doc comments). No
    // refusal: the disable/enable calls run the real direct-apply branch.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.getOrCreateCharacter(kPlayer).controlsDisabled.known() &&
              es.getOrCreateCharacter(kPlayer).controlsDisabled.get() == false);
        CHECK(es.playerControlsLockHandle().known() && es.playerControlsLockHandle().get() == 0);
        ok(host, gp, "player_controls_disable('#PLAYER1#')");
        CHECK(es.getOrCreateCharacter(kPlayer).controlsDisabled.get() == true);
        CHECK(es.controlStateMachine().stack().size() == 1);
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        CHECK(es.getOrCreateCharacter(kPlayer).controlsDisabled.get() == false);
        CHECK(es.controlStateMachine().stack().empty());
        // An unresolved (OPEN) name refuses at resolution, as every other
        // native does - unrelated to the now-seeded lock fields.
        refuses(host, gp, "player_controls_disable('Killbane')", "named-object resolution");
        // movement_disable only WRITES its bit: already known at construction.
        ok(host, gp, "player_movement_disable('#PLAYER1#')");
        CHECK(es.getOrCreateCharacter(kPlayer).movementDisabled.get() == true);
        CHECK(es.controlStateMachine().stack().empty());  // no stack bookkeeping (Sec7.25)
    }

    // The direct-apply branch exercised in full. The explicit .set() calls
    // below are redundant resets to the same CONFIRMED defaults construction
    // already seeds (Sec2.10) - kept for explicitness/determinism.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CharacterState& c = es.getOrCreateCharacter(kPlayer);
        c.controlsDisabled.set(false);
        es.playerControlsLockHandle().set(0);
        auto& m = es.controlStateMachine();

        // A synthetic device so "clear latched" is observable on live input.
        sr3player::SyntheticDeviceSource dev;
        dev.keyDown[0x39] = true;
        es.inputSystem().setDevice(&dev);
        es.inputSystem().latchFrame();
        CHECK(es.inputSystem().context().keys[0x39].enabled == 1);

        // Disable: bit set, push state 1, handle kept, clear latched ran.
        ok(host, gp, "player_controls_disable('#PLAYER1#')");
        CHECK(c.controlsDisabled.get() == true);
        CHECK(m.stack().size() == 1 && m.stack()[0].state == 1 && m.stack()[0].handle == 1);
        CHECK(es.playerControlsLockHandle().get() == 1);
        CHECK(m.desiredState() == 1);
        CHECK(es.inputSystem().context().keys[0x39].enabled == 0);
        // The dispatcher would now run exit(2)->enter(1) in one update.
        m.dispatch();
        CHECK(m.currentState() == 1);

        // Disabling again: no change -> no second push (an unused boolean too).
        ok(host, gp, "player_controls_disable('#PLAYER1#', true)");
        CHECK(m.stack().size() == 1 && m.handleCounter() == 1);

        // Enable: pops that exact handle, clears it, clears the bit.
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        CHECK(c.controlsDisabled.get() == false && es.playerControlsLockHandle().get() == 0);
        CHECK(m.stack().empty() && m.desiredState() == 2);
        // Enable while already enabled: no-op.
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        CHECK(m.stack().empty() && c.controlsDisabled.get() == false);

        // Out-of-order release: the lock sits BELOW a later free-camera
        // request; enabling pops it from the middle and 7 stays on top.
        ok(host, gp, "player_controls_disable('#PLAYER1#')");
        const uint32_t lockHandle = es.playerControlsLockHandle().get();
        CHECK(lockHandle == 2);
        const uint32_t cam = m.push(sr3player::kFreeCameraState);
        CHECK(m.desiredState() == 7);
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        CHECK(m.stack().size() == 1 && m.stack()[0].handle == cam && m.desiredState() == 7);
        m.pop(cam);

        // Enable with the bit set but a null handle: a no-op, the bit stays
        // set (Sec7.7: only "bit set AND handle non-null" delegates).
        c.controlsDisabled.set(true);
        es.playerControlsLockHandle().set(0);
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        CHECK(c.controlsDisabled.get() == true && m.stack().empty());
        c.controlsDisabled.set(false);

        // Full stack: the push is silently dropped, but the handle is non-zero
        // and kept; the matching enable's pop is then a no-op (Sec2.9).
        for (int i = 0; i < sr3player::kControlStackCapacity; ++i) m.push(0);
        ok(host, gp, "player_controls_disable('#PLAYER1#')");
        const uint32_t dropped = es.playerControlsLockHandle().get();
        CHECK(dropped != 0 && m.stack().size() == 5);
        for (const auto& e : m.stack()) CHECK(e.handle != dropped && e.state == 0);
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        CHECK(es.playerControlsLockHandle().get() == 0 && m.stack().size() == 5 && !c.controlsDisabled.get());

        es.inputSystem().setDevice(nullptr);
    }

    // Gate false (0x008addb0): no co-op session (CONFIRMED start-up value)
    // -> silent no-op; a session present -> the state-counter comparison is
    // not modelled, so it refuses.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CharacterState& c = es.getOrCreateCharacter(kPlayer);
        c.controlLockGatePasses.set(false);
        ok(host, gp, "player_controls_disable('#PLAYER1#')");
        ok(host, gp, "player_controls_enable('#PLAYER1#')");
        ok(host, gp, "player_movement_disable('#PLAYER1#', true)");
        // Untouched: gate false skips the bit-write branch entirely, so both
        // fields stay at their construction-seeded default (not re-derived
        // from this no-op call).
        CHECK(c.controlsDisabled.get() == false && c.movementDisabled.get() == false);
        CHECK(es.controlStateMachine().stack().empty());
        es.coopSession().present.set(true);
        refuses(host, gp, "player_movement_disable('#PLAYER1#', true)", "mode 4");
        refuses(host, gp, "player_controls_disable('#PLAYER1#')", "mode 0");
    }

    // Probe mode (--probe-unresolved-names): a synthetic name's +0x1d99 byte
    // and the lock handle are seeded CONFIRMED at construction the same as
    // any other character (Sec2.10) - the name-resolution probe only widens
    // the earlier named-object resolution gate, so once resolved, disable/
    // enable run their real direct-apply branch exactly as for a real name.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.enableUnresolvedNameProbe();
        ok(host, gp, "player_controls_disable('probe_npc')");
        CHECK(es.controlStateMachine().stack().size() == 1);
        ok(host, gp, "player_controls_enable('probe_npc')");
        CHECK(es.controlStateMachine().stack().empty());
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_player_controls_test: all checks passed\n";
    return 0;
}
