// Host-level tests for the per-NPC AI loop wired into the mission host (EngineState::tickNpcAi, run from the
// per-human update tickCharacterDeaths): hostile NPCs notice the player and attack/cover, civilians flee when
// attacked, notoriety escalates the police response through the Sec19.4 team-matrix flip, and NPC fire reaches the
// player through the damage sink. Real Host, real Lua natives (notoriety_set), real EngineState.
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "npc_ai_test_tables.h"
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
    auto r = host.runChunk(L, code, "npc_ai_host_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> v;
    for (const char* n : {"notoriety_set", "team_make_hostile"}) v.push_back({n, "gameplay"});
    return v;
}

void frame(Host& host, double dt = 1.0 / 30.0) {
    host.engineState().gameClockAdvanceFrame(dt);
    host.engineState().tickCharacterDeaths();
    host.runScriptThreadSchedulerPass();
}

struct Fixture {
    Host host{fixtureNames()};
    EngineState& es = host.engineState();
    std::vector<std::string> log;
    Fixture() {
        es.registerNotorietyAliasTrackCodeForTesting("Police", 3);
        es.getOrCreateCharacter("#PLAYER1#").worldPosition.set(WorldPosition{0, 0, 0});
        es.getOrCreateCharacter("#PLAYER1#").maxHitPoints.set(100);
        es.getOrCreateCharacter("#PLAYER1#").currentHitPoints.set(100);
        es.enableNpcAi(npc_ai_test::MakeTables(), sr3airuntime::NpcAiConfig{});
        es.npcAiLogSink = [this](const std::string& s) { log.push_back(s); };
    }
    void addNpc(const char* name, const char* team, double x) {
        es.objectResolves().set(name, true);
        CharacterState& c = es.getOrCreateCharacter(name);
        c.worldPosition.set(WorldPosition{x, 0, 0});
        c.teamName = team;
        c.maxHitPoints.set(100);
        c.currentHitPoints.set(100);
    }
    int32_t mode(const char* name) {
        const uint64_t h = es.npcAiHandleOf(name);
        return h ? es.npcAi()->modeOf(h) : -1;
    }
    bool attackOrCover(const char* name) {
        const int32_t m = mode(name);
        return m == sr3airuntime::mode::FireSpray || m == sr3airuntime::mode::CombatMovement ||
               m == sr3airuntime::mode::CoverStay || m == sr3airuntime::mode::CoverPopout;
    }
    bool run(int frames, const std::function<bool()>& until) {
        for (int i = 0; i < frames; ++i) {
            frame(host);
            if (until()) return true;
        }
        return false;
    }
    double x(const char* name) { return es.getOrCreateCharacter(name).worldPosition.get().x; }
};

}  // namespace

int main() {
    // A hostile NPC (its team is hostile to the player's team in the relation matrix) near the player transitions to an
    // attack/cover mode within a few seconds and the transitions are logged.
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);  // Playas <-> Luchadores: hostile (what team_make_hostile writes)
        f.addNpc("lucha1", "Luchadores", 20.0);
        CHECK(f.run(180, [&] { return f.attackOrCover("lucha1"); }));  // 6 s of game time
        CHECK(!f.log.empty());
        bool sawNpc = false;
        for (const auto& l : f.log) sawNpc = sawNpc || l.find("npc=lucha1") != std::string::npos;
        CHECK(sawNpc);
    }

    // A far hostile NPC advances toward the player (moves its world position).
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("lucha2", "Luchadores", 40.0);
        f.run(150, [] { return false; });
        CHECK(f.x("lucha2") < 36.0);
    }

    // A gang NPC whose relation to the player was never written is not hostile: it never attacks.
    {
        Fixture f;
        f.addNpc("lucha3", "Luchadores", 10.0);
        f.run(300, [] { return false; });
        CHECK(!f.attackOrCover("lucha3"));
        CHECK(std::fabs(f.x("lucha3") - 10.0) < 1e-6);
    }

    // The attack_player_on_sight force flag makes a neutral-team NPC hostile.
    {
        Fixture f;
        f.addNpc("guard", "Morningstar", 15.0);
        f.es.getOrCreateCharacter("guard").attackPlayerOnSightBit9.set(true);
        CHECK(f.run(180, [&] { return f.attackOrCover("guard"); }));
    }

    // A civilian that the player attacks flees or cowers.
    {
        Fixture f;
        f.addNpc("civ1", "Civilian", 6.0);
        f.run(30, [] { return false; });
        CHECK(!f.attackOrCover("civ1"));
        f.es.applyDamageModelHit("civ1", "#PLAYER1#", 10.0f, 0);
        CHECK(f.run(60, [&] {
            return f.mode("civ1") == sr3airuntime::mode::Flee || f.mode("civ1") == sr3airuntime::mode::Cower;
        }));
    }

    // Notoriety escalates the response: police are not hostile at notoriety 0; notoriety_set('Police', 3) flips the
    // relation (Sec19.4) and the police NPC attacks within a few seconds. Back to 0 flips it to unfriendly again.
    {
        Fixture f;
        // notoriety_set is host-gated (a session exists and this machine is host, spec Sec6.7); the escalation path
        // therefore needs the host branch, exactly like tests/synthetic_luahost_b29_sec6_notoriety_mission_test.cpp.
        f.es.coopSession().present.set(true);
        f.es.coopSession().localIsHost.set(true);
        f.addNpc("cop1", "Police", 15.0);
        f.run(300, [] { return false; });
        CHECK(!f.attackOrCover("cop1"));
        ok(f.host, f.host.gameplayState(), "notoriety_set('Police', 3)");
        CHECK(f.run(180, [&] { return f.attackOrCover("cop1"); }));
        CHECK(f.es.teamRelationForTesting(0, 5) == 0);
        ok(f.host, f.host.gameplayState(), "notoriety_set('Police', 0)");
        f.run(2, [] { return false; });
        CHECK(f.es.teamRelationForTesting(0, 5) == 1);
    }

    // NPC fire reaches the player through the damage sink: the player's hit points drop, and the stats count it.
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("lucha4", "Luchadores", 8.0);
        f.run(600, [] { return false; });
        CHECK(f.es.npcAiStats().shotsFired > 0);
        CHECK(f.es.npcAiStats().shotsHit > 0);
        CHECK(f.es.getOrCreateCharacter("#PLAYER1#").currentHitPoints.get() < 100);
        CHECK(f.es.getOrCreateCharacter("#PLAYER1#").lastAttackerName == "lucha4");
    }

    // A dead NPC stops: no more shots.
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("lucha5", "Luchadores", 8.0);
        f.run(120, [] { return false; });
        f.es.getOrCreateCharacter("lucha5").isDeadHighConfidence = true;
        f.run(3, [] { return false; });
        const uint64_t fired = f.es.npcAiStats().shotsFired;
        f.run(300, [] { return false; });
        CHECK(f.es.npcAiStats().shotsFired == fired);
    }

    // The m21 situation: Luchadores NPCs marked hostile by set_attack_player_flag (the attackPlayerBitAc field), at
    // 8.9 / 13.3 / 25 m. Live ones notice the player and attack. An NPC the script (or the playtest harness's
    // character_kill) killed before it came into range, and an NPC under ignore_ai (m21's distractors), cannot act: they
    // must not be reported as hostile AI ticks (the live m21 summary counted them: hostile_npc_ticks=855,
    // min_hostile_distance=8.9 were all dead/ignore_ai NPCs), and they are counted as suppressed instead.
    {
        Fixture f;
        f.addNpc("Harasser_Npc 004", "Luchadores", 13.3);
        f.addNpc("Harasser_Npc 001", "Luchadores", 25.0);
        f.addNpc("Distractor_NPC 002", "Luchadores", 8.9);   // ignore_ai (m21: "They ignore all AI from here on out")
        f.addNpc("Harasser_Npc 009", "Luchadores", 20.0);    // killed by the harness before the player arrives
        for (const char* n : {"Harasser_Npc 004", "Harasser_Npc 001", "Distractor_NPC 002", "Harasser_Npc 009"})
            f.es.getOrCreateCharacter(n).attackPlayerBitAc.set(true);
        f.es.getOrCreateCharacter("Distractor_NPC 002").ignoreAI.set(true);
        f.es.getOrCreateCharacter("Harasser_Npc 009").lifeState.set(5);
        CHECK(f.run(240, [&] { return f.attackOrCover("Harasser_Npc 004") && f.attackOrCover("Harasser_Npc 001"); }));
        CHECK(!f.attackOrCover("Distractor_NPC 002"));
        CHECK(!f.attackOrCover("Harasser_Npc 009"));
        const auto& st = f.es.npcAiStats();
        CHECK(st.minHostileDistance > 13.0 && st.minHostileDistance < 13.4);  // the closest LIVE AI-driven hostile
        CHECK(st.hostileSuppressedTicks > 0);
        bool loggedDead = false, loggedIgnore = false;
        for (const auto& l : f.log) {
            if (l.find("npc=Harasser_Npc 009") != std::string::npos && l.find("is hostile") != std::string::npos) loggedDead = true;
            if (l.find("npc=Distractor_NPC 002") != std::string::npos && l.find("is hostile") != std::string::npos) loggedIgnore = true;
        }
        CHECK(!loggedDead);
        CHECK(!loggedIgnore);
    }

    // AI-3 (Sec21.1 / Sec10 vision test): the host's line-of-sight seam (EngineState::npcAiLineOfSight, a ray query) gates
    // the sense pass and fire. A hostile NPC at 40 m (beyond the 25 m proximity notice) behind a wall never notices the
    // player and never shoots; the same NPC with a clear ray does.
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("wall1", "Luchadores", 40.0);
        f.es.npcAiLineOfSight = [](double, double, double, double, double, double) { return false; };  // blocked
        f.run(600, [] { return false; });
        const uint64_t h = f.es.npcAiHandleOf("wall1");
        CHECK(h != 0);
        CHECK(!f.es.npcAi()->state(h)->ai->perception.hasEntry(sr3airuntime::plist::KnownHostiles, 1));
        CHECK(f.es.npcAiStats().shotsFired == 0);
        CHECK(!f.attackOrCover("wall1"));
    }
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("wall2", "Luchadores", 40.0);
        f.es.npcAiLineOfSight = [](double, double, double, double, double, double) { return true; };  // clear
        CHECK(f.run(300, [&] { return f.attackOrCover("wall2"); }));
    }
    // AI-5 (Sec10 list 8): a homie (relation 4 = allied) hit by the player produces friendly fire only; he does not turn on
    // the player. A neutral-attitude NPC (relation 2) hit by the player does.
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 4);
        f.addNpc("homie", "Luchadores", 12.0);
        f.run(10, [] { return false; });
        for (int i = 0; i < 4; ++i) {
            f.es.applyDamageModelHit("homie", "#PLAYER1#", 5.0f, 0);
            f.run(10, [] { return false; });
        }
        {  // list 8 (friendly fire, 3 s lifetime) holds the player right after the hits
            const uint64_t hh = f.es.npcAiHandleOf("homie");
            CHECK(hh != 0 && f.es.npcAi()->state(hh)->ai->perception.hasEntry(sr3airuntime::plist::FriendlyFire, 1));
        }
        f.run(200, [] { return false; });
        CHECK(!f.attackOrCover("homie"));
        CHECK(f.es.npcAiStats().shotsFired == 0);
        const uint64_t h = f.es.npcAiHandleOf("homie");
        CHECK(h != 0);
        CHECK(!f.es.npcAi()->state(h)->ai->perception.hasEntry(sr3airuntime::plist::KnownHostiles, 1));
    }
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 2);
        f.addNpc("neutral", "Luchadores", 12.0);
        f.run(10, [] { return false; });
        f.es.applyDamageModelHit("neutral", "#PLAYER1#", 5.0f, 0);
        CHECK(f.run(180, [&] { return f.attackOrCover("neutral"); }));
    }
    // AI-10/11 wiring: a downed NPC (stance 6) does not act (the Dbno mode only waits; no shots from a downed NPC).
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("brute1", "Luchadores", 8.0);
        CharacterState& c = f.es.getOrCreateCharacter("brute1");
        c.downedFacts.isBrute = true;
        c.downedFacts.isGangMember = true;
        c.bleedOutHitPoints = 300;
        f.run(30, [] { return false; });
        f.es.applyDamageModelHit("brute1", "#PLAYER1#", 160.0f, 0);  // 100 HP, overkill 60 <= 300 -> downed
        CHECK(c.lifeState.get() == 6);
        f.run(3, [] { return false; });
        const uint64_t fired = f.es.npcAiStats().shotsFired;
        f.run(150, [] { return false; });  // 5 s < the 7 s down time
        CHECK(c.lifeState.get() == 6);
        CHECK(f.es.npcAiStats().shotsFired == fired);
    }

    // AI-3: a host-assigned line-of-sight seam is used: a wall between the player and a hostile NPC 40 m away (beyond the
    // 25 m proximity notice) keeps it from noticing; the rays are counted in the stats.
    {
        Fixture f;
        f.es.npcAiLineOfSight = [](double, double, double, double, double, double) { return false; };  // always blocked
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("walled", "Luchadores", 40.0);
        f.run(300, [] { return false; });
        const uint64_t h = f.es.npcAiHandleOf("walled");
        CHECK(h != 0 && !f.es.npcAi()->state(h)->ai->perception.hasEntry(sr3airuntime::plist::KnownHostiles, 1));
        CHECK(f.es.npcAiStats().losChecks >= 1 && f.es.npcAiStats().losChecks == f.es.npcAiStats().losBlocked);
    }
    // AI-16: on_detection is queued once with (detector, player) when the NPC first notices the player (a refresh does not
    // fire again); character_is_ready is false while the NPC's AI mode is Stunned.
    {
        Fixture f;
        f.es.setTeamRelationIfInBounds(0, 1, 0);
        f.addNpc("watcher", "Luchadores", 20.0);
        f.es.getOrCreateCharacter("watcher").onDetectionCallback = "on_det_cb";
        f.run(300, [] { return false; });
        int fired = 0;
        for (const auto& r : f.es.engineCallbackLog())
            if (r.callback.name == "on_det_cb") ++fired;
        CHECK(fired == 1);
        const uint64_t h = f.es.npcAiHandleOf("watcher");
        CHECK(h != 0 && f.es.npcAi()->state(h)->ai->perception.hasEntry(sr3airuntime::plist::KnownHostiles, 1));
    }
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "npc_ai_host_test: all checks passed\n";
    return 0;
}
