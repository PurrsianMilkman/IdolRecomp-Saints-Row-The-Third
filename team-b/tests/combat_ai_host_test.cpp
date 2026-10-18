// Host-level tests for the combat/NPC AI wiring: spec-ai-runtime.md Sec21.5 / Sec22.11 (scripted order ticket and
// move arrival), and the spec-conformance-review.md findings N-7 / N-9 on the engine-callback code. Real Host, real
// Lua natives, real EngineState per-human update (tickCharacterDeaths).
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
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "combat_ai_host_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> v;
    for (const char* n : {"ai_do_scripted_move", "ai_scripted_action_is_complete", "ai_clear_scripted_action",
                          "ai_scripted_action_is_processing", "get_dist", "character_kill", "on_death",
                          "character_damage", "on_take_damage"})
        v.push_back({n, "gameplay"});
    return v;
}

const char* kLuaPrelude = R"LUA(
LOG = {}
function F(...)
  local n = select('#', ...)
  local s = ""
  for i = 1, n do
    local v = select(i, ...)
    if i > 1 then s = s .. "," end
    if v == nil then s = s .. "nil" else s = s .. tostring(v) end
  end
  return s
end
function mk(tag) return function(...) LOG[#LOG + 1] = tag .. '(' .. F(...) .. ')' end end
function LOGJ() local s = "" for i = 1, #LOG do if i > 1 then s = s .. ";" end s = s .. LOG[i] end return s end
)LUA";

std::string logText(Host& host) {
    lua_State* L = host.gameplayState();
    lua_getglobal(L, "LOGJ");
    lua_call(L, 0, 1);
    std::string s = lua_tostring(L, -1);
    lua_pop(L, 1);
    return s;
}

// One 30 Hz frame of the per-human update plus a scheduler pass.
void frame(Host& host, double dt = 1.0 / 30.0) {
    host.engineState().gameClockAdvanceFrame(dt);
    host.engineState().tickCharacterDeaths();
    host.runScriptThreadSchedulerPass();
}

bool luaBool(Host& host, const std::string& expr) {
    lua_State* L = host.gameplayState();
    const std::string code = "RESULT = (" + expr + ")";
    auto r = host.runChunk(L, code, "combat_ai_host_test_eval.lua");
    if (!(r.loadOk && r.pcallOk)) { ++g_failures; return false; }
    lua_getglobal(L, "RESULT");
    const bool b = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return b;
}

double dist(EngineState& es, const char* a, const char* b) {
    const WorldPosition pa = es.getOrCreateCharacter(a).worldPosition.get();
    const WorldPosition pb = es.getOrCreateCharacter(b).worldPosition.get();
    return std::sqrt((pa.x - pb.x) * (pa.x - pb.x) + (pa.y - pb.y) * (pa.y - pb.y) + (pa.z - pb.z) * (pa.z - pb.z));
}

}  // namespace

int main() {
    // ---- Sec21.5 / Sec22.11: ai_do_scripted_move stages a kind-3 ticket; the mode finish completes it when the
    // NPC is within the 1.414 m box test of the destination; ai_scripted_action_is_complete reads the ticket.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("nav").worldPosition.set(WorldPosition{20, 0, 0});
        CHECK(luaBool(host, "ai_scripted_action_is_complete('npc')"));  // no ticket reads as complete
        ok(host, gp, "assert(ai_do_scripted_move('npc', 'nav', true, false) == true)");
        CHECK(!luaBool(host, "ai_scripted_action_is_complete('npc')"));  // ticket pending
        for (int i = 0; i < 30; ++i) frame(host);  // 1 s at the CHOSEN run speed: not there yet
        CHECK(!luaBool(host, "ai_scripted_action_is_complete('npc')"));
        CHECK(dist(es, "npc", "nav") > 5.0);
        CHECK(luaBool(host, "get_dist('npc', 'nav') > 2.0"));
        for (int i = 0; i < 200; ++i) frame(host);  // plenty of time to cover 20 m
        CHECK(luaBool(host, "ai_scripted_action_is_complete('npc')"));
        CHECK(dist(es, "npc", "nav") < 1.4143);  // arrival radius sqrt(2) for a point object
        CHECK(luaBool(host, "get_dist('npc', 'nav') < 2.0"));  // satisfies sh03/sh01 style geometry polls
    }

    // walking (run = false) is slower than running
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("nav").worldPosition.set(WorldPosition{100, 0, 0});
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', false, false)");
        for (int i = 0; i < 30; ++i) frame(host);
        const double walked = es.getOrCreateCharacter("npc").worldPosition.get().x;
        CHECK(walked > 0.5 && walked < 3.0);  // about one second at walking pace
    }

    // ai_clear_scripted_action returns control at once and stops the move
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("nav").worldPosition.set(WorldPosition{100, 0, 0});
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        for (int i = 0; i < 10; ++i) frame(host);
        ok(host, host.gameplayState(), "ai_clear_scripted_action('npc')");
        CHECK(luaBool(host, "ai_scripted_action_is_complete('npc')"));
        const double x = es.getOrCreateCharacter("npc").worldPosition.get().x;
        for (int i = 0; i < 30; ++i) frame(host);
        CHECK(std::fabs(es.getOrCreateCharacter("npc").worldPosition.get().x - x) < 1e-9);
    }

    // a second order frees the earlier ticket; re-issuing the same move while pending keeps it pending
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("nav").worldPosition.set(WorldPosition{100, 0, 0});
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        for (int i = 0; i < 5; ++i) {
            frame(host);
            ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");  // sh01 re-issue loop
        }
        CHECK(!luaBool(host, "ai_scripted_action_is_complete('npc')"));
        CHECK(es.getOrCreateCharacter("npc").worldPosition.get().x > 0.5);  // still moving
    }

    // nothing to measure (unknown positions): completes at once, as the host always did before the ticket existed
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        frame(host);
        CHECK(luaBool(host, "ai_scripted_action_is_complete('npc')"));
    }

    // a destination that does not resolve stages nothing (unchanged): complete
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.objectResolves().set("ghost", false);
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'ghost', true, false)");
        frame(host);
        CHECK(luaBool(host, "ai_scripted_action_is_complete('npc')"));
    }

    // a character that dies mid-move never blocks the script: its order is dropped (CHOSEN, documented)
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("nav").worldPosition.set(WorldPosition{100, 0, 0});
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        for (int i = 0; i < 5; ++i) frame(host);
        ok(host, host.gameplayState(), "character_kill('npc')");
        frame(host);
        frame(host);
        CHECK(luaBool(host, "ai_scripted_action_is_complete('npc')"));
    }

    // AI-14, Sec22.11 port rule: a destination that fails the gate (stops resolving) or is dead skips arrival test A and the
    // order ends WITHOUT completing: the ticket stays pending until a new order replaces it, and the character stops moving.
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("nav").worldPosition.set(WorldPosition{100, 0, 0});
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        for (int i = 0; i < 5; ++i) frame(host);
        es.objectResolves().set("nav", false);  // the destination no longer resolves
        for (int i = 0; i < 3; ++i) frame(host);
        const double x = es.getOrCreateCharacter("npc").worldPosition.get().x;
        for (int i = 0; i < 60; ++i) frame(host);
        CHECK(!luaBool(host, "ai_scripted_action_is_complete('npc')"));  // never completes
        CHECK(std::fabs(es.getOrCreateCharacter("npc").worldPosition.get().x - x) < 1e-9);  // and stops moving
        // a new order replaces the stuck ticket
        es.objectResolves().set("nav", true);
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        for (int i = 0; i < 10; ++i) frame(host);
        CHECK(es.getOrCreateCharacter("npc").worldPosition.get().x > x + 1.0);
    }
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.objectResolves().set("nav", true);
        es.getOrCreateCharacter("npc").worldPosition.set(WorldPosition{0, 0, 0});
        CharacterState& dest = es.getOrCreateCharacter("nav");  // a character destination that is dead
        dest.worldPosition.set(WorldPosition{100, 0, 0});
        ok(host, host.gameplayState(), "ai_do_scripted_move('npc', 'nav', true, false)");
        for (int i = 0; i < 5; ++i) frame(host);
        dest.lifeState.set(5);
        for (int i = 0; i < 60; ++i) frame(host);
        CHECK(!luaBool(host, "ai_scripted_action_is_complete('npc')"));
    }

    // an explicit test-set lookup result still wins over the ticket (existing tests rely on it)
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.objectResolves().set("npc", true);
        es.getOrCreateCharacter("npc").scriptedActionLookupNonNull.set(false);
        CHECK(!luaBool(host, "ai_scripted_action_is_complete('npc')"));
    }

    // ---- Review N-7: the player's death timer is armed at the finalizer, not at the first tick that sees the
    // death: 4.1 s pass between the lethal hit and the first per-human update, so on_death queues at that tick.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_pd = mk('pd')");
        es.objectResolves().set("#PLAYER1#", true);
        ok(host, gp, "on_death('cb_pd', '#PLAYER1#'); character_kill('#PLAYER1#')");
        es.gameClockAdvanceFrame(4.1);  // no per-human update in between (a world that ticks rarely)
        es.tickCharacterDeaths();
        host.runScriptThreadSchedulerPass();
        CHECK(logText(host) == "pd(#PLAYER1#,nil)");
    }

    // ---- Review N-9: on_take_damage fires for every non-0x200 hit, also when max hit points read 0 (the lethal
    // sink already fires with fraction 0 in that case).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_td = mk('td')");
        es.objectResolves().set("zero", true);
        es.getOrCreateCharacter("zero").maxHitPoints.set(0);
        es.getOrCreateCharacter("zero").currentHitPoints.set(0);
        es.getOrCreateCharacter("zero").damageImmuneDescriptorBit33.set(false);
        es.getOrCreateCharacter("zero").controlLockGatePasses.set(true);
        ok(host, gp, "on_take_damage('cb_td', 'zero'); character_damage('zero', 10, false)");
        host.runScriptThreadSchedulerPass();
        CHECK(logText(host).rfind("td(zero,nil,0", 0) == 0);
    }

    // ---- spec-lua-bindings.md Sec14 bootstrap literals (6 luaL_loadstring sites): the engine runs
    // `_UGGlobals = getfenv()` before any mission script, so system_lib.lua's `_GetAnyGlobalSilent` (which reads
    // rawget(_UGGlobals, k)) finds a mission's own global functions and thread_new(name) can start them. Without it
    // every thread_new of a mission function returned 65535 and no mission worker thread ever ran (sh01 hack thread).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        // the real system_lib.lua text: `_UGGlobals = { }` and the two-table lookup
        ok(host, gp,
           "_UGGlobals = { }\n_DynamicGlobals = { }\n"
           "function _GetAnyGlobalSilent( k )\n  local v = rawget( _UGGlobals, k )\n  if v ~= nil then return v end\n"
           "  for i, t in pairs( _DynamicGlobals ) do v = rawget( t, k ) if v ~= nil then return v end end\n"
           "  return nil\nend\n");
        ok(host, gp, "WORKER_RAN = 0\nfunction worker() WORKER_RAN = WORKER_RAN + 1 thread_yield() WORKER_RAN = WORKER_RAN + 1 end\n");
        // the host's thread table needs a current record for thread_new: start a parent thread through the table
        ok(host, gp, "function parent() TID = thread_new('worker') thread_yield() end\n");
        std::string fetchError;
        host.bareGlobals().threads().startThread(gp, "parent", fetchError);
        ok(host, gp, "assert(TID == 65535, 'before the bootstrap thread_new cannot find a mission global')");
        host.applyDynamicGlobalsBootstrap(gp);
        host.bareGlobals().threads().startThread(gp, "parent", fetchError);
        ok(host, gp, "assert(TID ~= 65535 and WORKER_RAN == 1, 'after the bootstrap the worker thread starts')");
    }

    if (g_failures == 0) std::cout << "combat_ai_host tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
