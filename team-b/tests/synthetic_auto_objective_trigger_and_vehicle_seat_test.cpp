// Synthetic test for feature/auto-objective-trigger-vehicle-seat (2026-10-12):
// extends the generic "stalled N frames" idea feature/auto-objective-harness
// established for get_dist (tests/synthetic_auto_objective_getdist_stall_
// test.cpp) to the two OTHER wait-shapes that take no get_dist call at all -
// EngineState::recordTriggerWaitFrame()/triggerWaitStallInfo()/
// triggerRegistrationOrder()/triggerEverEntered()/
// TriggerVolumeRepresentativePoint() (the on_trigger stall --auto-objective
// reads) and the m05-specific vehicle_enter_do seat stand-in
// (--auto-seat-mission-vehicle in tools/sr3_viewer.cpp - exercised here via
// the SAME real registered Lua native, not a white-box state poke, matching
// this project's own "drive through the real native" test convention).
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
    auto r = host.runChunk(L, code, "auto_objective_trigger_vehicle_seat_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {{"vehicle_enter_do", "gameplay"}};
}

} // namespace

int main() {
    // ---------------------------------------------------------------
    // Part 1: on_trigger wait-stall tracker (recordTriggerWaitFrame()/
    // triggerWaitStallInfo()/triggerRegistrationOrder()/triggerEverEntered())
    // - a pure EngineState-level exercise, mirroring the white-box half of
    // the real runPlaytest() loop that drives these (tools/sr3_viewer.cpp),
    // since none of these are reachable from a Lua chunk directly (unlike
    // get_dist, there is no single native call that drives them - the real
    // driver is runPlaytest()'s own per-frame trigger-overlap block).
    // ---------------------------------------------------------------
    {
        EngineState st;

        // Fresh name -> framesWaiting == 1.
        st.recordTriggerWaitFrame("Trigger Boat Dock");
        CHECK(st.triggerWaitStallInfo().framesWaiting == 1);
        CHECK(st.triggerWaitStallInfo().triggerName == "Trigger Boat Dock");

        // SAME name repeated -> climbs by exactly 1 each time (the "stalled
        // on the same wait loop" signal, same contract as get_dist's own
        // repeatCount).
        for (int i = 2; i <= 5; ++i) {
            st.recordTriggerWaitFrame("Trigger Boat Dock");
            CHECK(st.triggerWaitStallInfo().framesWaiting == static_cast<uint64_t>(i));
        }

        // A DIFFERENT name resets the counter to 1 - e.g. the previously-
        // pending trigger finally fired (triggerEverEntered() became true)
        // and a later-registered one is now the earliest still-pending.
        st.recordTriggerWaitFrame("Trigger Crate Pickup");
        CHECK(st.triggerWaitStallInfo().framesWaiting == 1);
        CHECK(st.triggerWaitStallInfo().triggerName == "Trigger Crate Pickup");

        // Registration order: first-seen-wins, no duplicates, insertion order
        // preserved - exactly what the "earliest still-pending trigger" scan
        // in runPlaytest() needs (an unordered_map's own iteration order is
        // not this).
        st.noteTriggerRegistrationOrder("Trigger B");
        st.noteTriggerRegistrationOrder("Trigger A");
        st.noteTriggerRegistrationOrder("Trigger B"); // duplicate - must not re-insert or reorder
        st.noteTriggerRegistrationOrder("Trigger C");
        const std::vector<std::string>& order = st.triggerRegistrationOrder();
        CHECK(order.size() == 3);
        CHECK(order[0] == "Trigger B");
        CHECK(order[1] == "Trigger A");
        CHECK(order[2] == "Trigger C");

        // triggerEverEntered(): defaults false (never entered), settable,
        // and distinct per name - the "stall candidate" scan's own filter.
        CHECK(st.triggerEverEntered("Trigger B") == false);
        st.triggerEverEntered("Trigger B") = true;
        CHECK(st.triggerEverEntered("Trigger B") == true);
        CHECK(st.triggerEverEntered("Trigger A") == false); // untouched name stays false

        // TriggerVolumeRepresentativePoint(): every shape returns a point
        // TriggerVolumeContainsPoint() itself reports as inside, for the
        // SAME geometry - the teleport target must actually land inside the
        // volume it claims to, not merely near it.
        {
            EngineState::TriggerVolumeGeometry sphere;
            sphere.shapeCode = 1;
            sphere.px = 10.0; sphere.py = 20.0; sphere.pz = 30.0;
            sphere.radius = 5.0;
            const WorldPosition rep = EngineState::TriggerVolumeRepresentativePoint(sphere);
            CHECK(rep.x == 10.0 && rep.y == 20.0 && rep.z == 30.0);
            CHECK(EngineState::TriggerVolumeContainsPoint(sphere, rep.x, rep.y, rep.z));
        }
        {
            EngineState::TriggerVolumeGeometry box;
            box.shapeCode = 0;
            box.px = 0.0; box.py = 0.0; box.pz = 0.0;
            box.boxMinX = -4.0; box.boxMinY = -1.0; box.boxMinZ = -2.0;
            box.boxMaxX = 2.0;  box.boxMaxY = 3.0;  box.boxMaxZ = 6.0;
            const WorldPosition rep = EngineState::TriggerVolumeRepresentativePoint(box);
            CHECK(rep.x == -1.0 && rep.y == 1.0 && rep.z == 2.0); // real on-disk min/max midpoint, offset from px/py/pz
            CHECK(EngineState::TriggerVolumeContainsPoint(box, rep.x, rep.y, rep.z));
        }
        {
            EngineState::TriggerVolumeGeometry cyl;
            cyl.shapeCode = 2;
            cyl.px = 100.0; cyl.py = 0.0; cyl.pz = -50.0;
            cyl.ex = 0.0; cyl.ey = 10.0; cyl.ez = 0.0;
            cyl.radius = 2.0;
            const WorldPosition rep = EngineState::TriggerVolumeRepresentativePoint(cyl);
            CHECK(rep.x == 100.0 && rep.y == 5.0 && rep.z == -50.0); // axis midpoint
            CHECK(EngineState::TriggerVolumeContainsPoint(cyl, rep.x, rep.y, rep.z));
        }
    }

    // ---------------------------------------------------------------
    // Part 2: m05 vehicle-seating stand-in - drives the REAL registered
    // vehicle_enter_do Lua native (the exact call --auto-seat-mission-
    // vehicle issues, tools/sr3_viewer.cpp) and checks it produces the SAME
    // CharacterState::stateEnum == 3 ("seated") / currentVehicleName write
    // stub_vehicle_exit_do's own seated gate requires, closing the m05
    // `vehicle_exit(LOCAL_PLAYER)` stall this task's own brief names.
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* L = host.gameplayState();
        EngineState& st = host.engineState();

        st.objectResolves().set("#PLAYER1#", true);
        st.objectResolves().set("m05_heli_pierce", true);

        // Before seating: CharacterState starts un-seated (stateEnum OPEN/0
        // default per this project's own "no instance writes a default
        // itself" convention) - the exact precondition that made m05's real
        // vehicle_exit(LOCAL_PLAYER) spin forever before this task.
        CharacterState& player = st.getOrCreateCharacter("#PLAYER1#");
        CHECK(player.stateEnum.get() == 0);

        // The exact call --auto-seat-mission-vehicle issues.
        ok(host, L, "vehicle_enter_do('#PLAYER1#', 'm05_heli_pierce', true)");

        CHECK(player.stateEnum.get() == 3); // seated - stub_vehicle_exit_do's own gate
        CHECK(player.currentVehicleName.get() == "m05_heli_pierce");
        // The seat-0 commit is the seat-0 occupant fact (m06 vehicle_pathfind_to blocker): no longer OPEN.
        CHECK(st.getOrCreateVehicle("m05_heli_pierce").seat0OccupantPresent() == true);
    }

    if (g_failures == 0) {
        std::cout << "synthetic_auto_objective_trigger_and_vehicle_seat_test: OK\n";
        return 0;
    }
    std::cerr << "synthetic_auto_objective_trigger_and_vehicle_seat_test: " << g_failures << " failure(s)\n";
    return 1;
}
