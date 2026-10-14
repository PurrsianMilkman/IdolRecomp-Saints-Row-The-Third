// Synthetic tests for Team A spec sync e6691f1 answers: tutorial +0x20 default param (Sec6.19),
// alive predicate 0x0096f4f0 (Sec30.5/34.5), mover +0xc4 link (Sec25.6), mission_conv slot (Sec33.4),
// trigger_get_radius (Sec21.13), on_vehicle_enter kind (Sec4.12).
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
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "e6691f1_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    std::vector<RegisteredName> names = {
        {"tutorial_start", "gameplay"},          {"audio_any_conversation_playing", "gameplay"},
        {"mesh_mover_update_control", "gameplay"}, {"trigger_get_radius", "gameplay"},
        {"on_vehicle_enter", "gameplay"},          {"character_is_ready", "gameplay"},
    };
    Host host(names);
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // Tutorial default param: 2 everywhere except 188 and 196-201 (=1).
    CHECK(EngineState::tutorialDescriptorDefaultParam20(12) == 2);
    CHECK(EngineState::tutorialDescriptorDefaultParam20(193) == 2);
    CHECK(EngineState::tutorialDescriptorDefaultParam20(204) == 2);
    CHECK(EngineState::tutorialDescriptorDefaultParam20(188) == 1);
    CHECK(EngineState::tutorialDescriptorDefaultParam20(196) == 1);
    CHECK(EngineState::tutorialDescriptorDefaultParam20(201) == 1);
    CHECK(EngineState::tutorialDescriptorDefaultParam20(202) == 2);
    int ones = 0;
    for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) ones += EngineState::tutorialDescriptorDefaultParam20(i) == 1;
    CHECK(ones == 7);

    // Alive predicate: fresh character alive; state 5 or e3 latch -> not alive; explicit set wins.
    CharacterState& a = es.getOrCreateCharacter("Alice");
    CHECK(a.aliveByPredicate());
    a.lifeState.set(6);
    CHECK(a.aliveByPredicate()); // downed still counts as alive for 0x0096f4f0
    a.lifeState.set(5);
    CHECK(!a.aliveByPredicate());
    a.lifeState.set(0);
    a.killFinalizedFlagE3.set(true);
    CHECK(!a.aliveByPredicate());
    a.isAlive.set(true);
    CHECK(a.aliveByPredicate());

    // mission_conv: false at start; explicit set wins.
    ok(host, gp, "assert(audio_any_conversation_playing() == false)");
    es.conversationChannelActive().set("mission_conv", true);
    ok(host, gp, "assert(audio_any_conversation_playing() == true)");

    // mesh_mover_update_control: +0xc4 link modelled present -> bool control recorded.
    es.objectResolves().set("Horsie", true);
    es.getOrCreatePlacedObject("Horsie").hasInteractionAnchor.set(false);
    ok(host, gp, "mesh_mover_update_control('Horsie', 'is statue broken', true)");
    CHECK(es.getOrCreatePlacedObject("Horsie").moverBoolControls.count("is statue broken") == 1);
    es.getOrCreatePlacedObject("Horsie").hasMoverControlSubRecord.set(false);
    es.getOrCreatePlacedObject("Horsie").moverBoolControls.clear();
    ok(host, gp, "mesh_mover_update_control('Horsie', 'x', true)");
    CHECK(es.getOrCreatePlacedObject("Horsie").moverBoolControls.empty());

    // trigger_get_radius per shape kind.
    es.objectResolves().set("t_sphere", true);
    es.objectResolves().set("t_cyl", true);
    es.objectResolves().set("t_box", true);
    EngineState::TriggerVolumeGeometry g;
    g.positionKnown = true; g.shapeKnown = true;
    g.shapeCode = 1; g.radius = 7.5;
    es.registerTriggerGeometry("t_sphere", g);
    g.shapeCode = 2; g.radius = 4.25; g.length = 99; // f752324: +0x90 is axis length, +0x94 is the radius returned
    es.registerTriggerGeometry("t_cyl", g);
    g.shapeCode = 0; g.boxMinX = 0; g.boxMinY = 0; g.boxMinZ = 0; g.boxMaxX = 3; g.boxMaxY = 4; g.boxMaxZ = 12;
    es.registerTriggerGeometry("t_box", g);
    ok(host, gp, "assert(trigger_get_radius('t_sphere') == 7.5)");
    ok(host, gp, "assert(trigger_get_radius('t_cyl') == 4.25)");
    ok(host, gp, "assert(trigger_get_radius('t_box') == 6.5)"); // 0.5 * sqrt(9+16+144)
    es.objectResolves().set("t_gone", false);
    ok(host, gp, "assert(trigger_get_radius('t_gone') == 0)");

    // on_vehicle_enter: a non-character placed name registers on the vehicle's slot 7.
    es.objectResolves().set("veh_daedalus_vtol 001", true);
    ok(host, gp, "on_vehicle_enter('cb_vtol', 'veh_daedalus_vtol 001')");
    CHECK(es.getOrCreateVehicle("veh_daedalus_vtol 001").onVehicleEnterCallback == "cb_vtol");

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_mission_values_e6691f1_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_mission_values_e6691f1_test: " << g_failures << " check(s) failed\n";
    return 1;
}
