// Synthetic tests for the engine -> Lua callback firing of spec-lua-api-behaviour.md, "Engine callback
// dispatch: death and destruction (2026-10-14)" and "Engine callback dispatch: vehicle entry/exit, phone calls,
// triggers, group vehicles (2026-10-14)" (spec syncs bfe0542 / 98375ea), plus the values those syncs answered
// (human-shield gate, synced-action instance, RC driver, HUD screen exit hook, mission_set_excluded_reward gate,
// gamepad flag). Real Host, real Lua callbacks, real scheduler passes: a callback is observed only when the
// script-thread pump actually runs it, in creation order, never inside the native that caused it.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3weapons/damage_model.h"

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
#define CHECK_EQ(a, b)                                                                                              \
    do {                                                                                                            \
        const auto va_ = (a);                                                                                       \
        const auto vb_ = (b);                                                                                       \
        if (!(va_ == vb_)) {                                                                                        \
            std::cerr << "CHECK_EQ FAILED: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << "\n  got:      " \
                      << va_ << "\n  expected: " << vb_ << "\n";                                                    \
            ++g_failures;                                                                                           \
        }                                                                                                           \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "engine_callbacks_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> v;
    for (const char* n : {"on_death", "on_human_killed", "character_kill", "character_damage", "on_vehicle_enter",
                          "on_vehicle_exit", "vehicle_enter_do", "vehicle_exit_do", "on_vehicle_destroyed",
                          "vehicle_detonate", "audio_play_for_mission_cellphone", "audio_remove_mission_cellphone",
                          "trigger_enable", "on_trigger_exit", "on_trigger", "character_has_specific_human_shield",
                          "player_is_rc_driver", "action_play_synced_do", "action_play_synced_is_finished",
                          "action_stop_synced_state", "on_hud_screen_exit", "mission_set_excluded_reward",
                          "vehicle_set_sirenlights"})
        v.push_back({n, "gameplay"});
    return v;
}

// One frame of the host loop: the per-human update (death finalizer) then one script-thread pump.
void frame(Host& host) {
    host.engineState().tickCharacterDeaths();
    host.runScriptThreadSchedulerPass();
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
  return "n=" .. n .. ":" .. s
end
function mk(tag) return function(...) LOG[#LOG + 1] = tag .. '(' .. F(...) .. ')' end end
function LOGJ() local s = "" for i = 1, #LOG do if i > 1 then s = s .. ";" end s = s .. LOG[i] end return s end
function LOGCLEAR() LOG = {} end
)LUA";

// The thread table swap-compacts when a record finishes mid-pass (spec Sec26.27, already modelled by
// BareThreadTable), so records created in one frame do not always RUN in creation order. Multi-record checks
// compare the sorted set; creation order is checked on the engine callback log instead.
std::string sortedLog(const std::string& s) {
    std::vector<std::string> parts;
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find(';', p);
        if (q == std::string::npos) q = s.size();
        if (q > p) parts.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (auto& x : parts) out += (out.empty() ? "" : ";") + x;
    return out;
}

std::string creationOrder(EngineState& es) {
    std::string out;
    for (const auto& r : es.engineCallbackLog()) out += (out.empty() ? "" : ";") + r.callback.source + ":" + r.callback.name;
    return out;
}

std::string logText(Host& host) {
    lua_State* L = host.gameplayState();
    ok(host, L, "LOGS = LOGJ()");
    lua_getglobal(L, "LOGS");
    std::string s = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return s;
}

void clearLog(Host& host) { ok(host, host.gameplayState(), "LOGCLEAR()"); }

}  // namespace

int main() {
    // ---------------------------------------------------------------
    // B/C: NPC death. character_kill never runs the callback inside the call; the NEXT per-human update queues
    // on_human_killed(team|nil, killer|nil, victim) then on_death(victim, killer|nil); the pump runs them in
    // creation order; registrations stay; the latch makes it one-shot per life; respawn re-arms it.
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_hk = mk('hk'); cb_d1 = mk('d1'); cb_d2 = mk('d2')");
        es.objectResolves().set("npc1", true);
        es.objectResolves().set("npc2", true);
        es.getOrCreateCharacter("npc1").teamName = "Brotherhood";
        es.getOrCreateCharacter("npc2");
        ok(host, gp, "on_human_killed('cb_hk'); on_death('cb_d1', 'npc1'); on_death('cb_d2', 'npc2')");
        CHECK_EQ(es.onHumanKilledCallback(), std::string("cb_hk"));
        ok(host, gp, "character_kill('npc1'); character_kill('npc2')");
        CHECK_EQ(logText(host), std::string(""));  // nothing ran inside (or right after) the native
        CHECK(es.engineCallbackLog().empty());     // and nothing was even queued before the next per-human update
        frame(host);
        // Creation order (spec B): per victim on_human_killed then on_death, victims in index-list order.
        CHECK_EQ(creationOrder(es), std::string("on_human_killed:cb_hk;on_death:cb_d1;on_human_killed:cb_hk;on_death:cb_d2"));
        const std::string expectedBoth = sortedLog("hk(n=3:Brotherhood,nil,npc1);d1(n=2:npc1,nil);hk(n=3:nil,nil,npc2);d2(n=2:npc2,nil)");
        CHECK_EQ(sortedLog(logText(host)), expectedBoth);
        CHECK_EQ(es.getOrCreateCharacter("npc1").onDeathCallback, std::string("cb_d1"));  // persistent registration
        frame(host);
        CHECK_EQ(sortedLog(logText(host)), expectedBoth);  // latched: nothing new
        // Respawn clears the latch; a second life fires again.
        clearLog(host);
        CharacterState& c1 = es.getOrCreateCharacter("npc1");
        c1.lifeState.set(0);
        c1.isDeadHighConfidence = false;
        c1.killFinalizedFlagE3.set(false);
        frame(host);  // latch cleared (alive)
        ok(host, gp, "character_kill('npc1')");
        frame(host);
        CHECK_EQ(logText(host), std::string("hk(n=3:Brotherhood,nil,npc1);d1(n=2:npc1,nil)"));
        // "" clears the registration (scripts do this inside the callback); a cleared slot fires nothing.
        ok(host, gp, "on_human_killed('')");
        CHECK_EQ(es.onHumanKilledCallback(), std::string(""));
    }

    // Killer = attacker of the killing blow; frozen once dying; missing global silently lost.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_d3 = mk('d3'); cb_d4 = mk('d4')");
        for (const char* n : {"npc3", "npc4", "npc5", "#PLAYER1#"}) es.objectResolves().set(n, true);
        es.applyCharacterSpawnDefaults("npc3", 100, 1.0);
        es.applyCharacterSpawnDefaults("npc4", 100, 1.0);
        es.getOrCreateCharacter("npc3").damageImmuneDescriptorBit33.set(false);
        ok(host, gp, "on_death('cb_d3', 'npc3'); on_death('cb_d4', 'npc4')");
        ok(host, gp, "character_damage('npc3', 1000, true, '#PLAYER1#')");
        CHECK_EQ(es.getOrCreateCharacter("npc3").lastAttackerName, std::string("#PLAYER1#"));
        // The damage model (sr3weapons::ApplyDamageSink) reaches the same death tail.
        EngineState::DamageModelResult hit = es.applyDamageModelHit("npc4", "#PLAYER1#", 30.0f);
        CHECK(hit.applied && !hit.killed && hit.newHp == 70);
        hit = es.applyDamageModelHit("npc4", "npc3", 1000.0f);  // lethal, attacker npc3
        CHECK(hit.applied && hit.killed && hit.newHp == 0);
        es.applyDamageModelHit("npc4", "#PLAYER1#", 5.0f);  // the victim is already dying: the killer stays npc3
        CHECK_EQ(es.getOrCreateCharacter("npc4").lastAttackerName, std::string("npc3"));
        frame(host);
        CHECK_EQ(logText(host), std::string("d3(n=2:npc3,#PLAYER1#);d4(n=2:npc4,npc3)"));
        // A registered name with no Lua global: silently lost, counted, no crash.
        const uint64_t lostBefore = es.engineCallbackCounters().lost;
        ok(host, gp, "on_death('no_such_function', 'npc5'); character_kill('npc5')");
        frame(host);
        CHECK(es.engineCallbackCounters().lost == lostBefore + 1);
        CHECK_EQ(logText(host), std::string("d3(n=2:npc3,#PLAYER1#);d4(n=2:npc4,npc3)"));
    }

    // Player death: on_death(player, killer|nil) once per life from the frame update (no on_human_killed).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_pd = mk('pd'); cb_hk = mk('hk')");
        es.objectResolves().set("#PLAYER1#", true);
        ok(host, gp, "on_human_killed('cb_hk'); on_death('cb_pd', LOCAL_PLAYER); character_kill(LOCAL_PLAYER)");
        // Review L-1 (spec B, 2026-10-14 RESOLVED): the player's on_death is queued about respawn_delay_ms (4000 ms)
        // after the finalizer arms the timer, not on the next frame.
        frame(host);
        CHECK_EQ(logText(host), std::string(""));
        es.gameClockAdvanceFrame(3.9);
        frame(host);
        CHECK_EQ(logText(host), std::string(""));
        es.gameClockAdvanceFrame(0.2);
        frame(host);
        CHECK_EQ(logText(host), std::string("pd(n=2:#PLAYER1#,nil)"));
        frame(host);
        CHECK_EQ(logText(host), std::string("pd(n=2:#PLAYER1#,nil)"));
    }

    // ---------------------------------------------------------------
    // V.1: vehicle enter / exit. Vehicle hook first, then the occupant hook; exactly 3 arguments
    // (occupant_name, vehicle_name, seat); seat 0 = driver; the polls already answer "seated" when the thread runs.
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_ve = mk('ve'); cb_pe = mk('pe'); cb_ne = mk('ne'); cb_vx = mk('vx'); cb_px = mk('px'); cb_nx = mk('nx')");
        for (const char* n : {"#PLAYER1#", "veh_car 001", "npc6"}) es.objectResolves().set(n, true);
        es.getOrCreateCharacter("npc6");
        ok(host, gp, "on_vehicle_enter('cb_ve', 'veh_car 001'); on_vehicle_enter('cb_pe', LOCAL_PLAYER); on_vehicle_enter('cb_ne', 'npc6')");
        ok(host, gp, "on_vehicle_exit('cb_vx', 'veh_car 001'); on_vehicle_exit('cb_px', LOCAL_PLAYER); on_vehicle_exit('cb_nx', 'npc6')");
        ok(host, gp, "vehicle_enter_do(LOCAL_PLAYER, 'veh_car 001', true)");
        CHECK_EQ(logText(host), std::string(""));  // nothing ran inside the native
        frame(host);
        CHECK_EQ(logText(host), std::string("ve(n=3:#PLAYER1#,veh_car 001,0);pe(n=3:#PLAYER1#,veh_car 001,0)"));
        // Persistence: the registrations were not cleared, a second occupant fires them again (seat 1).
        clearLog(host);
        ok(host, gp, "vehicle_enter_do('npc6', 'veh_car 001', true)");
        frame(host);
        CHECK_EQ(logText(host), std::string("ve(n=3:npc6,veh_car 001,1);ne(n=3:npc6,veh_car 001,1)"));
        // Exit: vehicle slot, then the occupant slot; same three arguments; the seat that was released.
        clearLog(host);
        ok(host, gp, "vehicle_exit_do(LOCAL_PLAYER, true)");
        frame(host);
        CHECK_EQ(logText(host), std::string("vx(n=3:#PLAYER1#,veh_car 001,0);px(n=3:#PLAYER1#,veh_car 001,0)"));
    }

    // Placed script_vehicle names that do not look like vehicle names still register on the VEHICLE record
    // once zone data classified them (spec V.2); here the explicit player-class override stands in for that.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_ve = mk('ve')");
        es.objectResolves().set("#PLAYER1#", true);
        es.objectResolves().set("start_truck", true);
        es.playerClassChainResolves().set("start_truck", false);
        ok(host, gp, "on_vehicle_enter('cb_ve', 'start_truck')");
        CHECK_EQ(es.getOrCreateVehicle("start_truck").onVehicleEnterCallback, std::string("cb_ve"));
        ok(host, gp, "vehicle_enter_do(LOCAL_PLAYER, 'start_truck', true)");
        frame(host);
        CHECK_EQ(logText(host), std::string("ve(n=3:#PLAYER1#,start_truck,0)"));
    }

    // ---------------------------------------------------------------
    // D: vehicle destroyed. The vehicle's callback is queued BEFORE its occupants die, so it runs before their
    // on_human_killed / on_death callbacks; one fire per vehicle.
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_vd = mk('vd'); cb_hk = mk('hk'); cb_d7 = mk('d7')");
        for (const char* n : {"veh_boom 001", "npc7"}) es.objectResolves().set(n, true);
        es.getOrCreateCharacter("npc7");
        ok(host, gp, "on_vehicle_destroyed('cb_vd', 'veh_boom 001'); on_death('cb_d7', 'npc7'); on_human_killed('cb_hk')");
        ok(host, gp, "vehicle_enter_do('npc7', 'veh_boom 001', true)");
        ok(host, gp, "vehicle_detonate('veh_boom 001')");
        CHECK_EQ(logText(host), std::string(""));
        frame(host);
        // The vehicle callback is created first (before the occupant's death is even finalized).
        CHECK_EQ(creationOrder(es), std::string("on_vehicle_destroyed:cb_vd;on_human_killed:cb_hk;on_death:cb_d7"));
        CHECK_EQ(sortedLog(logText(host)), sortedLog("vd(n=2:veh_boom 001,nil);hk(n=3:nil,nil,npc7);d7(n=2:npc7,nil)"));
        clearLog(host);
        ok(host, gp, "vehicle_detonate('veh_boom 001')");
        frame(host);
        CHECK_EQ(logText(host), std::string(""));  // once per vehicle
    }

    // E: mover destroyed (callable; no producer is wired - whether mesh_mover_kill reaches the handler is OPEN).
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        es.getOrCreatePlacedObject("mover1").onMoverDestroyedCallback = "cb_md";
        ok(host, host.gameplayState(), kLuaPrelude);
        ok(host, host.gameplayState(), "cb_md = mk('md')");
        es.notifyMoverDestroyed("mover1", "#PLAYER1#");
        es.notifyMoverDestroyed("mover1", "");
        host.runScriptThreadSchedulerPass();
        CHECK_EQ(logText(host), std::string("md(n=2:mover1,#PLAYER1#);md(n=2:mover1,nil)"));
    }

    // ---------------------------------------------------------------
    // V.3: mission cellphone calls. cb1 on answer, cb2 (ZERO arguments) on the end; audio_remove_mission_cellphone
    // clears both slots and NEVER fires cb2; a player reset ends every live call.
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_c1 = mk('c1'); cb_c2 = mk('c2'); cb_r1 = mk('r1'); cb_r2 = mk('r2')");
        ok(host, gp, "audio_play_for_mission_cellphone('call1', true, true, 'cb_c1', 'cb_c2', 7)");
        CHECK(es.missionCellphoneAudio("call1").live);
        CHECK(es.missionCellphoneAudio("call1").personaHandle == 7.0);
        frame(host);
        CHECK_EQ(logText(host), std::string(""));  // ringing: nothing fires by itself
        CHECK(es.answerMissionCellphoneCall("call1"));
        CHECK(!es.answerMissionCellphoneCall("call1"));
        frame(host);
        CHECK_EQ(logText(host), std::string("c1(n=0:)"));
        CHECK(es.endMissionCellphoneCall("call1"));
        CHECK(!es.endMissionCellphoneCall("call1"));
        frame(host);
        CHECK_EQ(logText(host), std::string("c1(n=0:);c2(n=0:)"));
        // Removal by the script: no cb2.
        clearLog(host);
        ok(host, gp, "audio_play_for_mission_cellphone('call2', true, true, 'cb_r1', 'cb_r2')");
        ok(host, gp, "audio_remove_mission_cellphone('call2')");
        CHECK(!es.endMissionCellphoneCall("call2"));
        frame(host);
        CHECK_EQ(logText(host), std::string(""));
        CHECK_EQ(es.missionCellphoneAudio("call2").hookSlot2, std::string(""));
        // Reset ends the live call(s) with cb2.
        ok(host, gp, "audio_play_for_mission_cellphone('call3', false, true, '', 'cb_r2')");
        es.resetMissionCellphoneCalls();
        frame(host);
        CHECK_EQ(logText(host), std::string("r2(n=0:)"));
    }

    // ---------------------------------------------------------------
    // V.4: triggers. Disabling while inside fires on_trigger_exit during the disable call; enabling a disabled
    // trigger starts a fresh overlap set (the viewer's overlap pass then reports a human already inside as ENTER);
    // enabling an already-enabled trigger does nothing.
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_tx = mk('tx')");
        es.objectResolves().set("trg", true);
        ok(host, gp, "on_trigger_exit('cb_tx', 'trg')");
        es.triggerCurrentlyInside("trg") = true;  // the overlap pass saw the player enter
        es.triggerOverlapBaselined("trg") = true;
        ok(host, gp, "trigger_enable('trg', false)");
        CHECK(!es.triggerCurrentlyInside("trg"));
        frame(host);
        CHECK_EQ(logText(host), std::string("tx(n=2:#PLAYER1#,trg)"));
        // Disabled -> enabled: a fresh phantom (baseline armed, inside cleared).
        es.triggerOverlapBaselined("trg") = false;
        ok(host, gp, "trigger_enable('trg', true)");
        CHECK(es.triggerOverlapBaselined("trg"));
        CHECK(!es.triggerCurrentlyInside("trg"));
        // Already enabled: enabling again changes nothing.
        es.triggerCurrentlyInside("trg") = true;
        ok(host, gp, "trigger_enable('trg', true)");
        CHECK(es.triggerCurrentlyInside("trg"));
        // Disabling an already-disabled trigger emits nothing.
        clearLog(host);
        ok(host, gp, "trigger_enable('trg', false)");
        es.triggerCurrentlyInside("trg") = true;
        ok(host, gp, "trigger_enable('trg', false)");
        frame(host);
        CHECK_EQ(logText(host), std::string("tx(n=2:#PLAYER1#,trg)"));  // only the first disable's exit
    }

    // ---------------------------------------------------------------
    // Spec sync answers: human shield gate (Sec13.14), synced action (Sec18.5), RC driver (Sec18.10),
    // HUD screen exit hook (Sec24.24), excluded-reward gate (Sec14.8), siren lights (Sec33.1).
    // ---------------------------------------------------------------
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        for (const char* n : {"#PLAYER1#", "josh", "other"}) es.objectResolves().set(n, true);
        ok(host, gp, "assert(character_has_specific_human_shield(LOCAL_PLAYER, 'josh') == false)");  // ordinary play: false
        es.beginHumanShield("#PLAYER1#", "josh");
        ok(host, gp, "assert(character_has_specific_human_shield(LOCAL_PLAYER, 'josh') == true)");   // state 2 (grab in progress)
        ok(host, gp, "assert(character_has_specific_human_shield(LOCAL_PLAYER, 'other') == false)"); // only that hostage
        ok(host, gp, "assert(character_has_specific_human_shield('josh', LOCAL_PLAYER) == false)");  // hostage is not the holder
        es.getOrCreateCharacter("#PLAYER1#").humanShieldStateByte18d4 = 1;                           // holding
        ok(host, gp, "assert(character_has_specific_human_shield(LOCAL_PLAYER, 'josh') == true)");
        es.getOrCreateCharacter("#PLAYER1#").humanShieldStateByte18d4 = 4;                           // release started
        ok(host, gp, "assert(character_has_specific_human_shield(LOCAL_PLAYER, 'josh') == false)");
        es.getOrCreateCharacter("#PLAYER1#").humanShieldStateByte18d4 = 1;
        es.getOrCreateCharacter("josh").isDeadHighConfidence = true;                                 // hostage died
        ok(host, gp, "assert(character_has_specific_human_shield(LOCAL_PLAYER, 'josh') == false)");
    }
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        for (const char* n : {"atk", "vic"}) es.objectResolves().set(n, true);
        es.getOrCreateCharacter("atk");
        es.getOrCreateCharacter("vic");
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic') == true)");  // no active instance
        ok(host, gp, "assert(action_play_synced_do('atk', 'vic', 'some_sync') == 1)");
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic') == false)"); // playing
        es.gameClockAdvanceFrame(1.5);                                                  // half of the 3 s stand-in
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic', 0.4) == true)");
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic', 0.9) == false)");
        es.gameClockAdvanceFrame(1.5);
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic') == true)");   // played out
        ok(host, gp, "action_play_synced_do('atk', 'vic', 'some_sync')");
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic') == false)");
        ok(host, gp, "action_stop_synced_state('atk')");
        ok(host, gp, "action_stop_synced_state('vic')");
        ok(host, gp, "assert(action_play_synced_is_finished('atk', 'vic') == true)");   // stopped
    }
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        for (const char* n : {"#PLAYER1#", "rc_car", "other_car"}) es.objectResolves().set(n, true);
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER, 'rc_car') == false)");  // null controller by default
        CHECK(es.beginRcControl("rc_car", "#PLAYER1#"));
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER, 'rc_car') == true)");
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER) == true)");             // via the active-vehicle pair
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER, 'other_car') == false)");
        CHECK(!es.beginRcControl("other_car", "#PLAYER1#"));                            // already controlling one
        es.endRcControl("rc_car");
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER, 'rc_car') == false)");
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER) == false)");
        // Physically driving the vehicle does not count.
        ok(host, gp, "vehicle_enter_do(LOCAL_PLAYER, 'rc_car', true)");
        ok(host, gp, "assert(player_is_rc_driver(LOCAL_PLAYER, 'rc_car') == false)");
    }
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, kLuaPrelude);
        ok(host, gp, "cb_ui = mk('ui')");
        ok(host, gp, "on_hud_screen_exit('cb_ui', 'msn_text_adventure')");  // always resolves from boot
        CHECK_EQ(es.namedHookCallback("on_hud_screen_exit:msn_text_adventure"), std::string("cb_ui"));
        CHECK(es.popHudScreen("msn_text_adventure", "#PLAYER1#"));
        frame(host);
        CHECK_EQ(logText(host), std::string("ui(n=2:msn_text_adventure,#PLAYER1#)"));
        ok(host, gp, "on_hud_screen_exit('', 'msn_text_adventure')");  // "" clears (m16's cleanup)
        CHECK(!es.popHudScreen("msn_text_adventure", "#PLAYER1#"));
    }
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.beginMissionPhaseTracking("m09");  // a story mission: started from its scripted_mission_start object
        ok(host, gp, "mission_set_excluded_reward('REWARD_A')");
        ok(host, gp, "mission_set_excluded_reward('REWARD_A')");  // de-duplicated
        ok(host, gp, "mission_set_excluded_reward('REWARD_B')");
        CHECK(es.missionExcludedRewards().excludedRewardHashes.size() == 2);
    }
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("cop_car", true);
        es.getOrCreateVehicle("cop_car").vehicleClass.set(6);  // Special_Vehicle_Type Police
        ok(host, gp, "vehicle_set_sirenlights('cop_car', true)");
        CHECK(es.getOrCreateVehicle("cop_car").forceFlags1d7c.get(0x18) == 0x18u);
        ok(host, gp, "vehicle_set_sirenlights('cop_car', false)");
        CHECK(es.getOrCreateVehicle("cop_car").forceFlags1d7c.get(0x18) == 0u);
        es.getOrCreateVehicle("cop_car").vehicleClass.set(12);  // Industrial: siren bit only
        ok(host, gp, "vehicle_set_sirenlights('cop_car', true)");
        CHECK(es.getOrCreateVehicle("cop_car").forceFlags1d7c.get(0x18) == 0x8u);
        // Default-path types still need the per-instance customisation record (not modelled): stays OPEN.
        es.getOrCreateVehicle("cop_car").vehicleClass.set(1);
        auto r = host.runChunk(gp, "vehicle_set_sirenlights('cop_car', true)", "t.lua");
        CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("light-bar") != std::string::npos);
    }

    if (g_failures == 0) std::cout << "synthetic_engine_callbacks_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
