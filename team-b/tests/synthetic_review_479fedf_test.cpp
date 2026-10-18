// Tests for spec sync 479fedf: Team A's review of the engine-callback merge (96f7d81) in
// spec-conformance-review.md, and the answers in spec-lua-api-behaviour.md for the OPEN values left by the
// post-96f7d81 mission sweep. Each block cites the spec section it checks. Real Host, real Lua natives.
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3save/save_crc.h"
#include "sr3zone/zone_objects.h"

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
    auto r = host.runChunk(L, code, "review_479fedf_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> v;
    for (const char* n :
         {"ai_do_scripted_take_cover", "character_is_in_a_driver_seat", "effect_play", "effect_play_finisher",
          "get_team", "character_hidden", "character_hide", "character_show", "character_is_ready", "flee",
          "vehicle_turret_base_to_do", "on_take_damage", "on_damage", "character_damage", "character_kill",
          "on_death", "on_pickup", "vehicle_pathfind_to_do"})
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

void frame(Host& host) {
    host.engineState().tickCharacterDeaths();
    host.runScriptThreadSchedulerPass();
}

}  // namespace

int main() {
    // ---- spec-lua-api-behaviour.md 18.3 (blocker m02/m10/m12): 0x00540b90 is the "cover take def" ability gate,
    // true for a freshly spawned NPC; the native commits scripted action 8 and returns true.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("goon", true);
        ok(host, gp, "assert(ai_do_scripted_take_cover('goon') == true)");
        CHECK(es.getOrCreateCharacter("goon").scriptedTakeCoverDispatchCount == 1);
        es.getOrCreateCharacter("goon").coverStayDownActive = true;  // inside a stay-down window: false, no action
        ok(host, gp, "assert(ai_do_scripted_take_cover('goon') == false)");
        CHECK(es.getOrCreateCharacter("goon").scriptedTakeCoverDispatchCount == 1);
    }

    // ---- 21.26 (blocker dlc2_m01): character_is_in_a_driver_seat = in the vehicle AND (seat-0 claim == c OR
    // pending == c OR seated == c). A passenger (seat 1) and a character on foot read false.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        for (const char* n : {"drv", "pax", "walker"}) es.objectResolves().set(n, true);
        es.getOrCreateCharacter("drv").currentVehicleName.set("car1");
        es.getOrCreateCharacter("pax").currentVehicleName.set("car1");
        es.getOrCreateCharacter("walker");
        es.commitVehicleSeat("drv", "car1", 0);
        es.commitVehicleSeat("pax", "car1", 1);
        ok(host, gp, "assert(character_is_in_a_driver_seat('drv') == true)");
        ok(host, gp, "assert(character_is_in_a_driver_seat('pax') == false)");
        ok(host, gp, "assert(character_is_in_a_driver_seat('walker') == false)");
        // Review B-3: a character that is only entering (claim) or reserving (pending) seat 0 reads true.
        for (const char* n : {"claimer", "pender"}) {
            es.objectResolves().set(n, true);
            es.getOrCreateCharacter(n).currentVehicleName.set("car2");
        }
        es.getOrCreateVehicle("car2").seat0Claim = "claimer";
        es.getOrCreateVehicle("car2").seat0Pending = "pender";
        ok(host, gp, "assert(character_is_in_a_driver_seat('claimer') == true)");
        ok(host, gp, "assert(character_is_in_a_driver_seat('pender') == true)");
        CHECK(es.getOrCreateVehicle("car2").seat0OccupantPresent() == false);  // no seated occupant
    }

    // ---- 46.4 m06 note (blocker m06, review B-4): seat-0 occupant at start = false (default). A vehicle nothing
    // was seated in reads "no occupant" instead of throwing OPEN.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("ball_veh", true);
        ok(host, gp, "assert(vehicle_turret_base_to_do('ball_veh', 'somewhere') == false)");
    }

    // ---- 28.9 (blocker sh02 icon index): effect_play_finisher returns the effect handle (port-allocated), not
    // -1.0, when the icon name is in effects.xtbl and the target resolves. It differs between calls.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.installEffectNameTable({"vfx_pc_icon", "vfx_xbox_icon"});
        es.objectResolves().set("nav_hack_1", true);
        ok(host, gp, "local h = effect_play_finisher('nav_hack_1', 2.5); assert(h ~= -1 and h >= 0)");
        ok(host, gp, "local a = effect_play_finisher('nav_hack_1', 2.5); local b = effect_play_finisher('nav_hack_1', 2.5); assert(a ~= b)");
    }

    // ---- 9.1 (blocker dlc3_m01): navpoints of a loaded zone resolve by lower-cased name, case-insensitively.
    // effect_play('Vfx Transformer DTH<004>', true) resolves against the stored 'Vfx Transformer Dth<004>'.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        sr3zone::ObjectRecord rec;
        rec.hasName = true;
        rec.nameText = "Vfx Transformer Dth<004>";
        rec.classHash = sr3save::nameHash("navpoint");
        sr3zone::ObjectsSection section;
        section.records.push_back(rec);
        es.loadZoneObjectsSection(section);
        ok(host, gp, "local h = effect_play('Vfx Transformer DTH<004>', true); assert(h ~= -1)");
    }

    // ---- 20.23 (blocker m09): get_team('#PLAYER1#') == "Playas" (id 0); the id table is
    // 0 Playas, 1 Luchadores, 2 Deckers, 3 Morningstar, 4 Neutral Gang, 5 Police, 6 STAG, 7 Civilian, 9 None;
    // an id outside it returns "None".
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("#PLAYER1#", true);
        ok(host, gp, "assert(get_team('#PLAYER1#') == 'Playas')");
        ok(host, gp, "assert(get_team('#PLAYER1#') ~= 'Morningstar')");
    }

    // ---- 45.1 (blocker m16): character_hidden = bit 0 of +0x3b, default 0 (visible) for a spawned NPC and the
    // player; set by character_hide, cleared by character_show; unresolved name reads true.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("npc_h", true);
        es.objectResolves().set("ghost", false);
        ok(host, gp, "assert(character_hidden('npc_h') == false)");
        ok(host, gp, "character_hide('npc_h'); assert(character_hidden('npc_h') == true)");
        ok(host, gp, "character_show('npc_h'); assert(character_hidden('npc_h') == false)");
        ok(host, gp, "assert(character_hidden('ghost') == true)");
    }

    // ---- 45.1 flee (review B-1): when EITHER the actor or the target fails to resolve, silently no order.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("flee_npc", true);
        es.objectResolves().set("#PLAYER1#", true);
        es.objectResolves().set("no_target", false);
        ok(host, gp, "flee('flee_npc', 'no_target')");
        CHECK(es.getOrCreateCharacter("flee_npc").fleeRequestCount == 0);
        ok(host, gp, "flee('flee_npc')");
        CHECK(es.getOrCreateCharacter("flee_npc").fleeRequestCount == 1);
    }

    // ---- 20.12 (review B-5): on_pickup arg 1 = callback, arg 2 = item (479fedf settles it: 20.12 note).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("minigun", true);
        ok(host, gp, "on_pickup('m23_minigun_cb', 'minigun')");
        CHECK(es.getOrCreatePlacedObject("minigun").onPickupCallback == "m23_minigun_cb");
    }

    // ---- Review L-1 (death section B, 2026-10-14 RESOLVED): the player's on_death is queued about 4000 ms
    // (respawn_delay_ms) after the death finalizer, not on the next frame.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_pd = mk('pd')");
        es.objectResolves().set("#PLAYER1#", true);
        ok(host, gp, "on_death('cb_pd', '#PLAYER1#'); character_kill('#PLAYER1#')");
        frame(host);                       // finalizer runs, timer armed
        CHECK(logText(host) == "");
        es.gameClockAdvanceFrame(3.9);
        frame(host);
        CHECK(logText(host) == "");
        es.gameClockAdvanceFrame(0.2);
        frame(host);
        CHECK(logText(host) == "pd(#PLAYER1#,nil)");
        es.gameClockAdvanceFrame(10.0);
        frame(host);
        CHECK(logText(host) == "pd(#PLAYER1#,nil)");  // once per life
    }

    // ---- Review L-2 (section F): on_take_damage(victim, attacker|nil, hp_fraction, explosion, melee, sonic)
    // fires on every non-0x200 hit; character_damage's default 'true' suppresses it; character_kill fires it with
    // attacker nil and hitpoints <= 0, before the death callbacks.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_td = mk('td'); cb_d = mk('d')");
        es.objectResolves().set("grunt", true);
        es.applyCharacterSpawnDefaults("grunt", 100, 1.0);
        es.getOrCreateCharacter("grunt").damageImmuneDescriptorBit33.set(false);
        es.getOrCreateCharacter("grunt").controlLockGatePasses.set(true);
        ok(host, gp, "on_take_damage('cb_td', 'grunt'); on_death('cb_d', 'grunt')");
        ok(host, gp, "character_damage('grunt', 25)");  // default true => flag 0x200 => suppressed
        frame(host);
        CHECK(logText(host) == "");
        ok(host, gp, "character_damage('grunt', 25, false)");
        frame(host);
        CHECK(logText(host) == "td(grunt,nil,0.5,false,false,false)");
        ok(host, gp, "character_kill('grunt')");
        frame(host);
        frame(host);
        CHECK(logText(host) == "td(grunt,nil,0.5,false,false,false);td(grunt,nil,0,false,false,false);d(grunt,nil)");
    }

    // ---- Review L-2 (section F): on_damage fires with no arguments when the health fraction crosses from above
    // the registered threshold to at or below it on one application.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_od = mk('od')");
        es.objectResolves().set("brute", true);
        es.applyCharacterSpawnDefaults("brute", 100, 1.0);
        es.getOrCreateCharacter("brute").damageImmuneDescriptorBit33.set(false);
        es.getOrCreateCharacter("brute").controlLockGatePasses.set(true);
        ok(host, gp, "on_damage('cb_od', 'brute', 0.5)");
        ok(host, gp, "character_damage('brute', 30, false)");  // 0.7 > 0.5
        frame(host);
        CHECK(logText(host) == "");
        ok(host, gp, "character_damage('brute', 30, false)");  // 0.4 <= 0.5: crossing
        frame(host);
        CHECK(logText(host) == "od()");
        ok(host, gp, "character_damage('brute', 10, false)");  // already below: no second fire
        frame(host);
        CHECK(logText(host) == "od()");
    }

    // ---- Review L-4 (7.27 port rule): character_is_ready is also false for a hidden character (+0x3b bit 0).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("rdy", true);
        es.getOrCreateCharacter("rdy").isAlive.set(true);
        ok(host, gp, "assert(character_is_ready('rdy') == true)");
        ok(host, gp, "character_hide('rdy'); assert(character_is_ready('rdy') == false)");
        ok(host, gp, "character_show('rdy'); assert(character_is_ready('rdy') == true)");
    }

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "synthetic_review_479fedf_test: all checks passed\n";
    return 0;
}
