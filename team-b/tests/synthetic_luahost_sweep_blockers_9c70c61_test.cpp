// Synthetic tests for the Team A spec sync 9c70c61 answers to the mission-sweep OPEN blockers:
// tutorial descriptor flags (Sec6.19), current-vehicle link (Sec30.5), door +0xc8 anchor (Sec3.9),
// continuous-explosion bool (Sec22.24), customization_screen_is_ready (Sec60.1), trigger geometry
// on-demand registration (Sec1.12). The notoriety tier ranges (Sec6.2) are covered in
// synthetic_luahost_b29_sec6_notoriety_mission_test.cpp.
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
    auto r = host.runChunk(L, code, "sweep_9c70c61_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sweep_9c70c61_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

uint32_t flags(EngineState& es, int idx) {
    return es.tutorialStart().flagsByte24.get(EngineState::tutorialStateKey(idx));
}

} // namespace

int main() {
    std::vector<RegisteredName> names = {
        {"character_is_in_vehicle", "gameplay"},
        {"customization_screen_is_ready", "gameplay"},
        {"object_is_in_trigger", "gameplay"},
        {"continuous_explosion_stop", "gameplay"},
    };
    Host host(names);
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // Tutorial descriptor flags (Sec6.19): 12 / 193 / 204 are all 0; bit 0x04 never set statically.
    CHECK(flags(es, 12) == 0);
    CHECK(flags(es, 193) == 0);
    CHECK(flags(es, 204) == 0);
    CHECK(flags(es, 20) == 0x1au);  // 0x02 | 0x08 | 0x10
    CHECK(flags(es, 39) == 0x1au);
    CHECK(flags(es, 40) == 0);
    CHECK(flags(es, 54) == 0x02u);
    CHECK(flags(es, 145) == 0x08u);
    CHECK(flags(es, 148) == 0x10u);
    CHECK(flags(es, 191) == 0x18u);
    CHECK(flags(es, 194) == 0x1au);
    for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) CHECK((flags(es, i) & 0x04u) == 0);
    CHECK(es.tutorialStart().noMissionLive006d2910.get());

    // Current vehicle (Sec30.5): null at construction; seated-in-v needs the link to be v.
    es.objectResolves().set("#PLAYER1#", true);
    es.objectResolves().set("truck", true);
    CharacterState& c = es.getOrCreateCharacter("#PLAYER1#");
    CHECK(c.currentVehicleName.known());
    CHECK(c.currentVehicleName.get().empty());
    c.stateEnum.set(3);
    ok(host, gp, "assert(character_is_in_vehicle('#PLAYER1#') == true)");
    ok(host, gp, "assert(character_is_in_vehicle('#PLAYER1#', 'truck') == false)");
    c.currentVehicleName.set("truck");
    ok(host, gp, "assert(character_is_in_vehicle('#PLAYER1#', 'truck') == true)");
    c.currentVehicleName.set(std::string());
    c.stateEnum.set(0);
    ok(host, gp, "assert(character_is_in_vehicle('#PLAYER1#', 'truck') == false)");

    // Door +0xc8 anchor (Sec3.9): absent unless the host models it.
    CHECK(es.getOrCreatePlacedObject("some_door").hasInteractionAnchor.known());
    CHECK(!es.getOrCreatePlacedObject("some_door").hasInteractionAnchor.get());

    // Continuous explosion bool (Sec22.24): false at start, stop keeps it false.
    CHECK(es.continuousExplosion().active.known());
    CHECK(!es.continuousExplosion().active.get());
    ok(host, gp, "continuous_explosion_stop()");
    CHECK(!es.continuousExplosion().active.get());

    // customization_screen_is_ready (Sec60.1): false until the store_character screen sets the global.
    ok(host, gp, "assert(customization_screen_is_ready() == false)");
    es.customizationCreation().screenIsReady.set(true);
    ok(host, gp, "assert(customization_screen_is_ready() == true)");

    // Trigger containment (Sec1.12): no zone data -> geometry cannot be registered, so the test stays OPEN.
    CHECK(!es.registerTriggerGeometryFromZoneData("trg_nowhere"));
    es.objectResolves().set("trg_nowhere", true);
    fails(host, gp, "object_is_in_trigger('trg_nowhere', '#PLAYER1#')", "trigger/object containment");
    // With registered geometry (sphere r=5 at origin) the on-demand path is not needed and the test is geometric.
    EngineState::TriggerVolumeGeometry g;
    g.px = 0; g.py = 0; g.pz = 0; g.positionKnown = true;
    g.shapeCode = 1; g.radius = 5.0; g.shapeKnown = true;
    es.registerTriggerGeometry("trg_nowhere", g);
    c.worldPosition.set(WorldPosition{1.0, 0.0, 1.0});
    ok(host, gp, "assert(object_is_in_trigger('trg_nowhere', '#PLAYER1#') == true)");
    c.worldPosition.set(WorldPosition{50.0, 0.0, 50.0});
    ok(host, gp, "assert(object_is_in_trigger('trg_nowhere', '#PLAYER1#') == false)");

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_sweep_blockers_9c70c61_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_sweep_blockers_9c70c61_test: " << g_failures << " check(s) failed\n";
    return 1;
}
