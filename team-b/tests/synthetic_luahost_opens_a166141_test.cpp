// Spec sync a166141 sweep OPENs: m02 +0xccd default, m06 0x00b24fa0 result, m10/m16 checkpoint vehicle.
#include <iostream>
#include <string>
#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
using namespace sr3luahost;
namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "CHECK FAILED: " #cond " at line " << __LINE__ << "\n"; ++g_failures; } } while (0)
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "opens_a166141_test.lua");
    if (!(r.loadOk && r.pcallOk)) { std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n"; ++g_failures; }
}
}
int main() {
    Host host({{"character_is_jumping", "gameplay"}, {"vehicle_pathfind_navmesh_do", "gameplay"}, {"mission_set_checkpoint", "gameplay"}});
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();
    // m02: +0xccd defaults 0 -> character_is_jumping returns exactly one value, false.
    es.objectResolves().set("j_c", true);
    CharacterState& jc = es.getOrCreateCharacter("j_c");
    ok(host, gp, "assert(select('#', character_is_jumping('j_c')) == 1 and character_is_jumping('j_c') == false)");
    jc.movementStateByteCcd.set(4);
    jc.movementStateByteCcc.set(3);
    jc.stateEnum.set(0);
    ok(host, gp, "assert(select('#', character_is_jumping('j_c')) == 2)");
    // m06: authority -> 1.0, non-authority -> 2.0 without a test-set result.
    es.objectResolves().set("pn_v", true);
    es.objectResolves().set("pn_dest", true);
    VehicleState& pv = es.getOrCreateVehicle("pn_v");
    pv.seat0Occupied.set(true);
    ok(host, gp, "assert(vehicle_pathfind_navmesh_do('pn_v', 'pn_dest', true, false, true, 0) == 1)");
    pv.forceFlagGatePasses.set(false);
    ok(host, gp, "assert(vehicle_pathfind_navmesh_do('pn_v', 'pn_dest', true, false, true, 0) == 2)");
    // m10/m16: player on foot -> player-position branch, no OPEN refusal.
    es.missionContext().activeAndRunning.set(true);
    es.objectResolves().set("#PLAYER1#", true);
    CharacterState& pc = es.getOrCreateCharacter("#PLAYER1#");
    pc.stateEnum.set(0);
    ok(host, gp, "mission_set_checkpoint('survive')");
    CHECK(es.missionCheckpoint().setCount == 1);
    CHECK(es.missionCheckpoint().lastUsedVehicleTransform == false);
    // seated in a resolved vehicle -> vehicle transform.
    pc.stateEnum.set(3);
    pc.currentVehicleName.set("pn_v");
    ok(host, gp, "mission_set_checkpoint('drive')");
    CHECK(es.missionCheckpoint().lastUsedVehicleTransform == true);
    if (g_failures == 0) { std::cout << "synthetic_luahost_opens_a166141_test: OK\n"; return 0; }
    std::cerr << g_failures << " failure(s)\n";
    return 1;
}
