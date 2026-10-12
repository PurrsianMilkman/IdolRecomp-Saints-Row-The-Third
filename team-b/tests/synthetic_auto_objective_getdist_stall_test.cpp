// Synthetic test for feature/auto-objective-harness (2026-10-12):
// EngineState::recordGetDistCall()/getDistStallInfo(), the generic
// "same get_dist(a,b) pair N times in a row" tracker --auto-objective
// (tools/sr3_viewer.cpp) reads to decide when/where to teleport. Exercises
// the tracker exactly as stub_get_dist() (src/lua_spec_confirmed_stubs_
// b05_invuln_r15_23.cpp) drives it, through real get_dist Lua calls - not a
// white-box call straight into EngineState - so this also doubles as a
// regression guard on stub_get_dist() itself still calling
// recordGetDistCall() on every invocation.
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
    auto r = host.runChunk(L, code, "auto_objective_getdist_stall_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {{"get_dist", "gameplay"}};
}

} // namespace

int main() {
    Host host(fixtureNames());
    lua_State* L = host.gameplayState();
    EngineState& st = host.engineState();

    // Both names must resolve (objectResolves) for get_dist to reach the
    // recordGetDistCall() call at all - "#PLAYER1#" is a CONFIRMED
    // pre-resolved sentinel (Sec29.4); register a second name the same way
    // applySpecInitialState()/tryAutoRegister would for a real in-world
    // object, with a real known worldPosition set directly (CharacterState)
    // so this test does not depend on any real .czn_pc zone data existing.
    // "#PLAYER1#" also needs both objectResolves() AND a known worldPosition
    // (this minimal fixture-only Host does not run applySpecInitialState(),
    // which is what pre-seeds it in a real run) - worldPositionOr() throws
    // exactly like a real OPEN gap when neither it nor
    // tryResolveWorldPositionFromZoneData() (no real .czn_pc exists in this
    // unit test) can resolve a position, by design (Sec4.2) - never
    // fabricate 0,0,0.
    st.objectResolves().set("#PLAYER1#", true);
    st.getOrCreateCharacter("#PLAYER1#").worldPosition.set(WorldPosition{-1280.0, 0.0, -560.0});
    st.objectResolves().set("Trigger Barge Dock", true);
    CharacterState& barge = st.getOrCreateCharacter("Trigger Barge Dock");
    barge.worldPosition.set(WorldPosition{-704.06, 0.47, -236.01});

    // Call 1: fresh pair -> repeatCount == 1.
    ok(host, L, "get_dist('#PLAYER1#', 'Trigger Barge Dock')");
    {
        const EngineState::GetDistStallInfo& s = st.getDistStallInfo();
        CHECK(s.repeatCount == 1);
        CHECK((s.nameA == "#PLAYER1#" && s.nameB == "Trigger Barge Dock"));
    }

    // Calls 2-5: SAME pair, repeated - repeatCount must climb by exactly 1
    // each time (the "stalled on the same wait loop" signal), and the
    // resolved target position/known-ness must be exactly what was set
    // above, never a fabricated value.
    for (int i = 2; i <= 5; ++i) {
        ok(host, L, "get_dist('#PLAYER1#', 'Trigger Barge Dock')");
        const EngineState::GetDistStallInfo& s = st.getDistStallInfo();
        CHECK(s.repeatCount == static_cast<uint64_t>(i));
    }
    {
        const EngineState::GetDistStallInfo& s = st.getDistStallInfo();
        CHECK(s.knownB == true);
        CHECK(s.posB.x == -704.06);
        CHECK(s.posB.y == 0.47);
        CHECK(s.posB.z == -236.01);
        // "#PLAYER1#" is nameA here (argument order preserved) - the
        // "which side is the player" check --auto-objective itself does.
        CHECK(s.nameA == "#PLAYER1#");
    }

    // A DIFFERENT pair resets the counter to 1, even though one of the two
    // names (the player) repeats - the pair must be compared as a whole
    // unordered set, not name-by-name.
    st.objectResolves().set("Trigger Elevator", true);
    CharacterState& elevator = st.getOrCreateCharacter("Trigger Elevator");
    elevator.worldPosition.set(WorldPosition{10.0, 20.0, 30.0});
    ok(host, L, "get_dist('#PLAYER1#', 'Trigger Elevator')");
    {
        const EngineState::GetDistStallInfo& s = st.getDistStallInfo();
        CHECK(s.repeatCount == 1);
        CHECK(s.nameB == "Trigger Elevator");
    }

    // Argument order reversed is still the SAME unordered pair -> counter
    // keeps climbing rather than resetting (a real mission script's own
    // get_dist call order for one wait loop is fixed, but this guards the
    // tracker's own "unordered" comparison contract, matching the m07
    // diagnostic's observation of 340 IDENTICAL calls, not merely 340 calls
    // with the same two names in either order).
    ok(host, L, "get_dist('Trigger Elevator', '#PLAYER1#')");
    {
        const EngineState::GetDistStallInfo& s = st.getDistStallInfo();
        CHECK(s.repeatCount == 2);
    }

    // A genuinely unregistered name throws OPEN (objectResolves().get()'s
    // own real, honest behavior for a name no fixture ever seeded - Sec3.9/
    // Sec10.6/Sec29) BEFORE stub_get_dist() ever reaches recordGetDistCall()
    // - the LAST recorded stall info must stay exactly as it was, not get
    // silently overwritten with garbage/unknown state on an errored call.
    {
        auto r = host.runChunk(L, "get_dist('#PLAYER1#', 'Never Registered Name')",
                                "auto_objective_getdist_stall_test.lua");
        CHECK(r.loadOk && !r.pcallOk); // errors, as a genuinely-OPEN name should
    }
    {
        const EngineState::GetDistStallInfo& s = st.getDistStallInfo();
        CHECK(s.repeatCount == 2);
        CHECK(s.nameB == "#PLAYER1#" || s.nameA == "#PLAYER1#");
        CHECK(s.nameB == "Trigger Elevator" || s.nameA == "Trigger Elevator");
    }

    if (g_failures == 0) {
        std::cout << "synthetic_auto_objective_getdist_stall_test: OK\n";
        return 0;
    }
    std::cerr << "synthetic_auto_objective_getdist_stall_test: " << g_failures << " failure(s)\n";
    return 1;
}
