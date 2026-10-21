// Downed (Dbno) state, spec-ai-runtime.md Sec21.10.1 (FINAL; supersedes the 21.4 "type-0 expiry stands up healed" rule
// and the misplaced type 1-4 note). Covers AI-10 (type-0 expiry is a bleed-out DEATH, the team-percent heal belongs to
// the get-up path and the type-1 expiry) and AI-11 (a lethal hit enters the downed state for qualifying NPCs: selector,
// type-1 overkill buffer and timer extension; types 0/2/3/4 ignore further damage). Vectors cited are the spec's.
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
    auto r = host.runChunk(L, code, "downed_expiry_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> names() {
    std::vector<RegisteredName> v;
    for (const char* n : {"human_enter_downed", "on_take_damage"}) v.push_back({n, "gameplay"});
    return v;
}

void frames(Host& host, int n) {
    for (int i = 0; i < n; ++i) {
        host.engineState().gameClockAdvanceFrame(1.0 / 30.0);
        host.engineState().tickCharacterDeaths();
        host.runScriptThreadSchedulerPass();
    }
}

CharacterState& mkNpc(Host& host, const char* name, int hp, int type, int maxHp = 200) {
    EngineState& es = host.engineState();
    es.objectResolves().set(name, true);
    CharacterState& c = es.getOrCreateCharacter(name);
    c.maxHitPoints.set(maxHp);
    c.currentHitPoints.set(hp);
    c.lifeState.set(0);
    c.enterDownedApplyGate.set(true);
    c.downedType = type;
    return c;
}

// An enemy-team gang brute with the host facts the selector reads (Sec21.10.1 type 1).
CharacterState& mkBrute(Host& host, const char* name, int hp, int maxHp, int bleedOut) {
    CharacterState& c = mkNpc(host, name, hp, 0, maxHp);
    c.downedFacts.isBrute = true;
    c.downedFacts.isGangMember = true;
    c.bleedOutHitPoints = bleedOut;
    c.currentTeamCode = 2;  // team byte != 0: enemy side (Enemy_revive_percent_hit_points 0.25)
    return c;
}

double remainingSeconds(Host& host, const CharacterState& c) {
    return c.downedDeadlineSeconds - host.engineState().syncedActionNowSeconds();
}

int kbFired(Host& host) {
    int n = 0;
    for (const auto& r : host.engineState().engineCallbackLog())
        if (r.callback.name == "m21_killbane_damaged") ++n;
    return n;
}
}  // namespace

int main() {
    // AI-10, Sec21.10.1 vector 1: follower (type 0), downed at t=0 with deadline 30000; no help -> at t=30000 stance 5, HP 0.
    {
        Host host(names());
        lua_State* gp = host.gameplayState();
        CharacterState& c = mkNpc(host, "n0", 20, 0);
        ok(host, gp, "human_enter_downed('n0', 30000)");
        CHECK(c.lifeState.get() == 6);
        frames(host, 30 * 29);
        CHECK(c.lifeState.get() == 6);  // still downed before the deadline
        CHECK(!EngineState::characterReadsDead(c));
        frames(host, 30 * 2);
        CHECK(c.lifeState.get() == 5);          // bleed-out DEATH, not a stand-up
        CHECK(c.currentHitPoints.get() == 0);
        CHECK(EngineState::characterReadsDead(c));
        CHECK(c.downedReviveAtExpiryCount == 0);
    }
    // Team A 2026-10-20 (Sec21.10.1 note): an absent or non-positive duration keeps the type's default (30000 ms).
    for (const char* call : {"human_enter_downed('d0')", "human_enter_downed('d0', 0)", "human_enter_downed('d0', -5)"}) {
        Host host(names());
        lua_State* gp = host.gameplayState();
        CharacterState& c = mkNpc(host, "d0", 20, 0);
        ok(host, gp, call);
        CHECK(c.lifeState.get() == 6);
        frames(host, 30 * 29);
        CHECK(c.lifeState.get() == 6);  // default 30000 ms, not an instant expiry
        frames(host, 30 * 2);
        CHECK(c.lifeState.get() == 5);
    }
    // Types 3 and 4: stand up at the deadline, no heal (Sec21.10.1 table: type 3 heal only while the attack flag is set -
    // that flag has no host source, OPEN; type 4 "stands, no heal").
    for (int type = 3; type <= 4; ++type) {
        Host host(names());
        lua_State* gp = host.gameplayState();
        CharacterState& c = mkNpc(host, "nt", 20, type);
        ok(host, gp, "human_enter_downed('nt', 5000)");
        CHECK(c.lifeState.get() == 6);
        frames(host, 30 * 6);
        CHECK(c.lifeState.get() == 0);
        CHECK(c.currentHitPoints.get() == 20);
        CHECK(c.downedMarkerReleasedCount == 1);
        CHECK(c.downedEndedAtExpiryCount == 1);
        CHECK(!EngineState::characterReadsDead(c));
        CHECK(kbFired(host) == 0);
    }
    // Type 1 via the script entry (human_enter_downed): gets up at the deadline and HP is SET to int(max) x 0.25 (enemy).
    {
        Host host(names());
        lua_State* gp = host.gameplayState();
        CharacterState& c = mkNpc(host, "n1", 0, 1, 400);
        c.currentTeamCode = 2;
        ok(host, gp, "human_enter_downed('n1', 7000)");
        frames(host, 30 * 8);
        CHECK(c.lifeState.get() == 0);
        CHECK(c.currentHitPoints.get() == 100);  // vector 6: +0x1cac 400, enemy team
        CHECK(!EngineState::characterReadsDead(c));
    }
    // Type 2 + the Killbane hook: fires only when the (OPEN) counter condition holds and the character carries the
    // m21_killbane_damaged on_take_damage slot; then gets up (get-up heal, enemy side 0.25 x 200 = 50).
    for (int condition = 0; condition <= 1; ++condition) {
        Host host(names());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "function m21_killbane_damaged() end");
        CharacterState& c = mkNpc(host, "M21_Killbane", 100, 2);
        ok(host, gp, "on_take_damage('m21_killbane_damaged', 'M21_Killbane')");
        if (condition) host.engineState().downedType2MissionCountersEqual().set(true);
        ok(host, gp, "human_enter_downed('M21_Killbane', 3000)");
        frames(host, 30 * 4);
        CHECK(c.lifeState.get() == 0);
        CHECK(kbFired(host) == condition);
    }
    // A type-2 character without the m21 hook slot fires nothing even when the condition holds.
    {
        Host host(names());
        lua_State* gp = host.gameplayState();
        mkNpc(host, "other", 100, 2);
        host.engineState().downedType2MissionCountersEqual().set(true);
        ok(host, gp, "human_enter_downed('other', 1000)");
        frames(host, 60);
        CHECK(kbFired(host) == 0);
    }
    // Types 0/2/3/4 ignore further damage while downed (Sec21.10.1 "damage while downed"); type 0 then still dies at its
    // deadline.
    for (int type : {0, 2, 3, 4}) {
        Host host(names());
        lua_State* gp = host.gameplayState();
        CharacterState& c = mkNpc(host, "ni", 10, type);
        ok(host, gp, "human_enter_downed('ni', 5000)");
        const auto r = host.engineState().applyDamageModelHit("ni", "#PLAYER1#", 500.0f, 0);
        CHECK(!r.applied);
        frames(host, 3);
        CHECK(c.lifeState.get() == 6);
        CHECK(!EngineState::characterReadsDead(c));
    }
    // AI-11, vectors 4/5/6: a lethal hit on a qualifying NPC (gang brute) enters the downed state instead of dying.
    // Vector 4: Bleed_Out_Hit_Points 300, killing hit leaves HP -50 -> type 1, buffer 250, deadline 7000.
    {
        Host host(names());
        CharacterState& c = mkBrute(host, "b4", 40, 400, 300);
        host.engineState().applyDamageModelHit("b4", "#PLAYER1#", 90.0f, 0);  // 40 HP - 90 = -50
        CHECK(c.lifeState.get() == 6);
        CHECK(c.downedType == 1);
        CHECK(c.downedBuffer == 250);
        CHECK(!EngineState::characterReadsDead(c));
        CHECK(c.currentHitPoints.get() == 0);
        CHECK(remainingSeconds(host, c) > 6.99 && remainingSeconds(host, c) < 7.01);
        frames(host, 30);  // t = 1 s: remaining 6 s
        CHECK(remainingSeconds(host, c) > 5.9 && remainingSeconds(host, c) < 6.1);
        host.engineState().applyDamageModelHit("b4", "#PLAYER1#", 100.0f, 0);  // buffer 150, remaining min(6+5, 7) = 7
        CHECK(c.lifeState.get() == 6);
        CHECK(c.downedBuffer == 150);
        CHECK(remainingSeconds(host, c) > 6.99 && remainingSeconds(host, c) < 7.01);
        host.engineState().applyDamageModelHit("b4", "#PLAYER1#", 200.0f, 0);  // buffer -50 -> dies
        frames(host, 2);
        CHECK(c.lifeState.get() == 5);
        CHECK(EngineState::characterReadsDead(c));
    }
    // Vector 5: killing hit leaves -400 < -300 -> no downed state, plain death.
    {
        Host host(names());
        CharacterState& c = mkBrute(host, "b5", 40, 400, 300);
        host.engineState().applyDamageModelHit("b5", "#PLAYER1#", 440.0f, 0);
        CHECK(c.lifeState.get() != 6);
        CHECK(EngineState::characterReadsDead(c));
    }
    // Vector 6: brute left alone, +0x1cac 400, enemy team: at t = 7000 stands, HP 100.
    {
        Host host(names());
        CharacterState& c = mkBrute(host, "b6", 40, 400, 300);
        host.engineState().applyDamageModelHit("b6", "#PLAYER1#", 90.0f, 0);
        CHECK(c.lifeState.get() == 6);
        frames(host, 30 * 6);
        CHECK(c.lifeState.get() == 6);
        frames(host, 30 * 2);
        CHECK(c.lifeState.get() == 0);
        CHECK(c.currentHitPoints.get() == 100);
        CHECK(!EngineState::characterReadsDead(c));
    }
    // The selector refuses a plain NPC (no brute/follower facts): a lethal hit is a normal death (Sec21.10.1 "-1").
    {
        Host host(names());
        CharacterState& c = mkNpc(host, "plain", 40, 0);
        host.engineState().applyDamageModelHit("plain", "#PLAYER1#", 90.0f, 0);
        CHECK(c.lifeState.get() != 6);
        CHECK(EngineState::characterReadsDead(c));
    }
    // A gang brute without a known Bleed_Out_Hit_Points (record source not wired: OPEN) is not downed - no invention.
    {
        Host host(names());
        CharacterState& c = mkBrute(host, "bo", 40, 400, 300);
        c.bleedOutHitPoints.reset();
        host.engineState().applyDamageModelHit("bo", "#PLAYER1#", 90.0f, 0);
        CHECK(EngineState::characterReadsDead(c));
    }
    // A downed follower hit by a killing blow stays downed (type 0 ignores damage) and a dead NPC is never revived.
    {
        Host host(names());
        CharacterState& c = mkNpc(host, "f0", 20, 0);
        c.downedFacts.isNonPlayerFollower = true;
        host.engineState().applyDamageModelHit("f0", "#PLAYER1#", 50.0f, 0);
        CHECK(c.lifeState.get() == 6);
        CHECK(c.downedType == 0);
        CHECK(remainingSeconds(host, c) > 29.9 && remainingSeconds(host, c) < 30.1);  // 30000 x scale 1.0
        frames(host, 30 * 31);
        CHECK(c.lifeState.get() == 5);  // vector 1 through the sink
        frames(host, 30 * 2);
        CHECK(EngineState::characterReadsDead(c));
    }
    // AI-11: the follower fact is derived from live data (the NPC sits on a leader's party list), no host/test flag needed;
    // a character on no party list still dies at once (no brute/gang/Killbane data source: selector -1).
    {
        Host host(names());
        CharacterState& homie = mkNpc(host, "homie", 20, 0);
        CharacterState& thug = mkNpc(host, "thug", 20, 0);
        host.engineState().party().membersByLeader["#PLAYER1#"].push_back("homie");
        host.engineState().applyDamageModelHit("homie", "gunner", 50.0f, 0);
        host.engineState().applyDamageModelHit("thug", "gunner", 50.0f, 0);
        CHECK(homie.lifeState.get() == 6);
        CHECK(homie.downedType == 0);
        CHECK(EngineState::characterReadsDead(thug));
        CHECK(host.engineState().npcAiStats().downedEntered == 1);
    }
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "downed_expiry_test: all checks passed\n";
    return 0;
}
