// Tests for spec-player-runtime.md Sec18 (player vitals, 2026-10-18): the pure rules in sr3player/vitals.h (regen wait
// and rate, the 1/30 s accumulator, respawn point selection, the death penalty) and their engine hooks in
// sr3luahost::EngineState (Damage_Received_Mult inside the sink - PV-6, the wait re-arm, the regen tick).
// Test vectors are Sec18.9 rows 1-9.
#include <cmath>
#include <cstdio>
#include <string>

#include "sr3luahost/engine_state.h"
#include "sr3player/vitals.h"

using namespace sr3player;

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

static bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// Runs the regen accumulator for `seconds` of 1/30 s frames; returns the hp reached and the time it first hit max.
static double runRegen(double hp, double maxHp, double waitMs, double rate, double seconds, double* fullAtMs) {
    HealthRegen r;
    *fullAtMs = -1.0;
    const double dt = 1.0 / 30.0;
    for (int f = 1; f * dt <= seconds + 1e-9; ++f) {
        const double nowMs = f * dt * 1000.0;
        hp = r.step(hp, maxHp, nowMs >= waitMs, rate, dt);
        if (*fullAtMs < 0.0 && hp >= maxHp) *fullAtMs = nowMs;
    }
    return hp;
}

int main() {
    // ---- wait / rate (Sec18.3, vectors 1-3) ----
    const DifficultyVitals easy = SpDifficultyVitals(0), normal = SpDifficultyVitals(1), hard = SpDifficultyVitals(2);
    CHECK(RegenWaitMs(0.0, normal.healthRegenWaitMult, false, false) == 6000);
    CHECK(RegenWaitMs(0.0, easy.healthRegenWaitMult, false, false) == 3600);
    CHECK(RegenWaitMs(0.0, hard.healthRegenWaitMult, false, false) == 7799);  // D-12: 1.3f x 6000 truncates (no guard)
    CHECK(RegenWaitMs(3000.0, normal.healthRegenWaitMult, false, false) == 3000);  // cheat reduction
    CHECK(RegenWaitMs(0.0, normal.healthRegenWaitMult, true, false) == 0);        // human shield
    CHECK(RegenWaitMs(0.0, normal.healthRegenWaitMult, false, true) == -1);       // dont_regen_health
    CHECK(near(RegenRatePerSec(normal.healthRegenSpeedMult, 1.0, false), 125.0, 1e-9));
    CHECK(near(RegenRatePerSec(easy.healthRegenSpeedMult, 1.0, false), 162.5, 1e-4));
    CHECK(near(RegenRatePerSec(hard.healthRegenSpeedMult, 2.0, false), 250.0, 1e-9));
    CHECK(near(RegenRatePerSec(normal.healthRegenSpeedMult, 1.0, true), 500.0, 1e-9));

    // Vector 1: SP normal, max 1000, 400 left at t=0: starts at 6000 ms, +4.1667/tick, full after 144 ticks (~10800).
    double fullAt = 0;
    double hp = runRegen(400.0, 1000.0, 6000.0, 125.0, 5.9, &fullAt);
    CHECK(near(hp, 400.0, 1e-9) && fullAt < 0.0);  // nothing before the wait expires
    hp = runRegen(400.0, 1000.0, 6000.0, 125.0, 20.0, &fullAt);
    CHECK(near(hp, 1000.0, 1e-9));
    CHECK(fullAt > 10700.0 && fullAt < 10900.0);
    // Vector 2: easy: wait 3600, rate 162.5: full ~7300 ms.
    hp = runRegen(400.0, 1000.0, 3600.0, RegenRatePerSec(easy.healthRegenSpeedMult, 1.0, false), 20.0, &fullAt);
    CHECK(near(hp, 1000.0, 1e-9) && fullAt > 7200.0 && fullAt < 7400.0);
    // one 1/30 s step adds exactly rate/30
    {
        HealthRegen r;
        const double h = r.step(400.0, 1000.0, true, 125.0, 1.0 / 30.0);
        CHECK(near(h, 400.0 + 125.0 / 30.0, 1e-4));  // D-13: single-precision accumulation
        // never past max
        CHECK(near(r.step(999.9, 1000.0, true, 125.0, 1.0), 1000.0, 1e-9));
        // not ready: unchanged
        CHECK(near(r.step(500.0, 1000.0, false, 125.0, 1.0), 500.0, 1e-9));
    }

    // ---- respawn point selection (vectors 9, island rule, flags) ----
    size_t n = 0;
    const RespawnPoint* pts = ShippedRespawnPoints(n);
    CHECK(n == 4);
    const RespawnPoint* p = PickRespawnPoint(pts, n, 1300.0, 20.0, 100.0, kRespawnFlagDeath, -1);
    CHECK(p && std::string(p->name) == "nw_hospital");
    p = PickRespawnPoint(pts, n, 0.0, 0.0, 0.0, kRespawnFlagDeath, -1);
    CHECK(p && std::string(p->name) == "dt_hospital");
    p = PickRespawnPoint(pts, n, -1600.0, 0.0, -600.0, kRespawnFlagDeath, 1);
    CHECK(p && std::string(p->name) == "ne_hospital");
    p = PickRespawnPoint(pts, n, 800.0, 0.0, 1000.0, kRespawnFlagDeath, 0);
    CHECK(p && std::string(p->name) == "sw_hospital");
    // busted: no `busted`-flagged point ships -> nothing qualifies (the caller falls back to the start object)
    CHECK(PickRespawnPoint(pts, n, 0.0, 0.0, 0.0, kRespawnFlagBusted, -1) == nullptr);
    // island rule: a player on the prison island (2) needs a point in a type-2 hood: none ship
    CHECK(PickRespawnPoint(pts, n, 0.0, 0.0, 0.0, kRespawnFlagDeath, 2) == nullptr);
    // a point in a restricted hood is excluded for a mainland player
    {
        const RespawnPoint custom[2] = {{"prison_pt", 0, 0, 0, 0, 0, 0, 1, kRespawnFlagDeath, 2},
                                        {"main_pt", 500, 0, 0, 0, 0, 0, 1, kRespawnFlagDeath, 1}};
        p = PickRespawnPoint(custom, 2, 1.0, 0.0, 0.0, kRespawnFlagDeath, -1);
        CHECK(p && std::string(p->name) == "main_pt");
        p = PickRespawnPoint(custom, 2, 1.0, 0.0, 0.0, kRespawnFlagDeath, 2);
        CHECK(p && std::string(p->name) == "prison_pt");
    }

    // ---- penalty (vectors 6-8) ----
    CHECK(DeathPenaltyCents(123456, kDeathCostDollars) == 50000);  // cash 73456 left
    CHECK(DeathPenaltyCents(32000, kDeathCostDollars) == 32000);   // everything: cash 0
    CHECK(DeathPenaltyCents(0, kDeathCostDollars) == 0);
    CHECK(DeathPenaltyCents(1000000, kBustedCostDollars) == 50000);
    CHECK(DeathPenaltyCents(50000, kDeathCostDollars) == 50000);   // exactly $500: not "cost > cash"

    // ---- phases of PlayerVitals (vector 10: on_death ~4000, fade 4000, teleport ~8000) ----
    {
        PlayerVitals v({100, 4000});
        CHECK(v.fadeMs() == 4000);
        CHECK(v.applyDamage(100.0, 0) && v.dead());
        CHECK(!v.beginFade(0) == false);  // beginFade is allowed while dead
        CHECK(!v.beginFade(1));           // only once
        CHECK(v.fading() && !v.fadeComplete(3999) && v.fadeComplete(4000));
        CHECK(near(v.fadeAlpha(2000), 0.5, 1e-9) && near(v.fadeAlpha(9999), 1.0, 1e-9));
        CHECK(v.respawn() && !v.fading() && near(v.fadeAlpha(0), 0.0, 1e-9));
        PlayerVitals alive({100, 4000});
        CHECK(!alive.beginFade(0));
    }

    // ---- engine hooks (PV-6 + regen wait re-arm) ----
    {
        sr3luahost::EngineState es;
        es.difficultyLevel() = 1;  // normal: Damage_Received_Mult 0.75
        auto& pl = es.getOrCreateCharacter("#PLAYER1#");
        pl.maxHitPoints.set(1000);
        pl.currentHitPoints.set(1000);
        // vector 5: raw 100 damage -> normal 75
        es.applyCharacterDamage("#PLAYER1#", "", 100.0f, 0u);
        CHECK(pl.currentHitPoints.get() == 925);
        CHECK(es.damageReceivedScaledCount == 1);
        es.difficultyLevel() = 0;  // easy 0.3
        pl.currentHitPoints.set(1000);
        es.applyCharacterDamage("#PLAYER1#", "", 100.0f, 0u);
        CHECK(pl.currentHitPoints.get() == 970);
        es.difficultyLevel() = 2;  // hard 1.5
        pl.currentHitPoints.set(1000);
        es.applyCharacterDamage("#PLAYER1#", "", 100.0f, 0u);
        CHECK(pl.currentHitPoints.get() == 850);
        // a non-player victim is not scaled
        auto& npc = es.getOrCreateCharacter("some_npc");
        npc.maxHitPoints.set(1000);
        npc.currentHitPoints.set(1000);
        es.applyCharacterDamage("some_npc", "", 100.0f, 0u);
        CHECK(npc.currentHitPoints.get() == 900);
        // a follower of the local player is scaled
        es.difficultyLevel() = 1;
        es.party().membersByLeader["#PLAYER1#"].push_back("homie1");
        auto& homie = es.getOrCreateCharacter("homie1");
        homie.maxHitPoints.set(1000);
        homie.currentHitPoints.set(1000);
        es.applyCharacterDamage("homie1", "", 100.0f, 0u);
        CHECK(homie.currentHitPoints.get() == 925);
        // the free-roam name "player" counts too
        auto& fr = es.getOrCreateCharacter("player");
        fr.maxHitPoints.set(1000);
        fr.currentHitPoints.set(1000);
        es.applyCharacterDamage("player", "", 100.0f, 0u);
        CHECK(fr.currentHitPoints.get() == 925);
    }
    {
        // vector 1 through the engine: normal, 1000 max; a hit leaves 400 (raw 800 x 0.75 = 600 damage).
        sr3luahost::EngineState es;
        es.difficultyLevel() = 1;
        auto& pl = es.getOrCreateCharacter("player");
        pl.maxHitPoints.set(1000);
        pl.currentHitPoints.set(1000);
        es.tickHealthRegen(0.5);  // idle, full health: nothing happens
        CHECK(pl.currentHitPoints.get() == 1000);
        es.applyCharacterDamage("player", "", 800.0f, 0u);
        CHECK(pl.currentHitPoints.get() == 400);
        double t = 0.0;
        auto run = [&](double toSeconds) {
            while (t < toSeconds - 1e-9) {
                es.tickHealthRegen(1.0 / 30.0);
                t += 1.0 / 30.0;
            }
        };
        run(5.9);
        CHECK(pl.currentHitPoints.get() == 400);  // wait 6000 ms not yet over
        run(6.5);
        CHECK(pl.currentHitPoints.get() > 400 && pl.currentHitPoints.get() < 1000);
        // vector 4: a second hit re-arms the wait (6000 ms from the hit)
        const int32_t before = pl.currentHitPoints.get();
        es.applyCharacterDamage("player", "", 40.0f, 0u);  // 30 damage
        CHECK(pl.currentHitPoints.get() == before - 30);
        const int32_t afterHit = pl.currentHitPoints.get();
        const double hitAt = t;
        run(hitAt + 5.9);
        CHECK(pl.currentHitPoints.get() == afterHit);
        run(hitAt + 7.0);
        CHECK(pl.currentHitPoints.get() > afterHit);
        run(hitAt + 30.0);
        CHECK(pl.currentHitPoints.get() == 1000);  // runs to full
        // dont_regen_health: no regeneration, and the wait is not armed
        es.applyCharacterDamage("player", "", 400.0f, 0u);
        pl.forceFlags1c98.setBits(sr3luahost::CharacterState::kDontRegenHealthBit1c98,
                                  sr3luahost::CharacterState::kDontRegenHealthBit1c98);
        const int32_t low = pl.currentHitPoints.get();
        run(t + 30.0);
        CHECK(pl.currentHitPoints.get() == low);
    }

    // ======== spec-player-runtime.md Sec18.18 vectors 11-23 ========
    {
        // Vector 11: no code path enters AI mode 4 in retail: the port keeps the trigger opt-in (viewer --enable-bust). Pure
        // gates (Sec18.10): a ragdolled player at 50 % HP with a cop at 4 m passes ONLY when the gates are all met.
        BustGateInput g;
        g.hp = 500;
        g.maxHp = 1000;
        g.playerRagdolled = true;
        g.distSqToApproach = 16.0;
        CHECK(BustGatesPass(g));
        g.hp = 601;  // 60.1 %
        CHECK(!BustGatesPass(g));
        g.hp = 600;  // exactly 0.6 passes (<=)
        CHECK(BustGatesPass(g));
        g.distSqToApproach = 25.1;
        CHECK(!BustGatesPass(g));
        g.distSqToApproach = 25.0;
        CHECK(BustGatesPass(g));
        g.playerRagdolled = false;
        CHECK(!BustGatesPass(g));
        g.playerRagdolled = true;
        g.playerDead = true;
        CHECK(!BustGatesPass(g));
        g.playerDead = false;
        g.arrestLockClear = false;
        CHECK(!BustGatesPass(g));
        g.arrestLockClear = true;
        g.maxHp = 0;
        CHECK(!BustGatesPass(g));  // maxHP <= 0 gives ratio 0 gate-fail
        // Vector 12: cuff ramp reaches 1.0 after 45 ticks; commit at phase >= 0.75.
        CuffState cs;
        for (int i = 0; i < 44; ++i) cs.tick();
        CHECK(cs.ramp < 1.0);
        cs.tick();
        CHECK(cs.ticks == 45 && near(cs.ramp, 1.0, 1e-9));
        CHECK(!CuffState::Commit(0.7499) && CuffState::Commit(0.75));
        // on_death at commit + 4000, teleport ~ +8000 (the vitals timers)
        PlayerVitals v({1000, 4000});
        CHECK(v.bust(1000) && v.busted() && v.dead() && v.stats().arrests == 1 && v.stats().deaths == 0);
        CHECK(v.health() == 1000);  // a bust does not change health
        CHECK(!v.bust(1001) && v.stats().arrests == 1);
        CHECK(!v.respawnDue(4999) && v.respawnDue(5000));
        v.beginFade(5000);
        CHECK(!v.fadeComplete(8999) && v.fadeComplete(9000));
        CHECK(v.respawn() && !v.busted() && !v.dead());
    }
    {
        // Vector 13: busted -> player_start, quaternion, cash 1,000,000 -> 950000.
        size_t n = 0;
        const RespawnPoint* pts = ShippedRespawnPoints(n);
        const RespawnChoice c = ResolveRespawn(pts, n, 0, 0, 0, kRespawnFlagBusted, -1);
        CHECK(c.fallbackStart && c.point);
        CHECK(near(c.point->x, 1220.9398, 1e-4) && near(c.point->y, 12.1574, 1e-4) && near(c.point->z, 1047.1863, 1e-4));
        CHECK(near(c.point->qy, -0.9827, 1e-4) && near(c.point->qw, 0.185, 1e-4) && c.point->qx == 0.0 && c.point->qz == 0.0);
        const int64_t pen = DeathPenaltyCents(1000000, kBustedCostDollars);
        CHECK(1000000 - pen == 950000);
        // Vector 14: death in a prison hood (type 2): no hospital qualifies -> same player_start point.
        const RespawnChoice p2 = ResolveRespawn(pts, n, 1300, 20, 100, kRespawnFlagDeath, 2);
        CHECK(p2.fallbackStart && p2.point == c.point);
        const RespawnChoice p3 = ResolveRespawn(pts, n, 1300, 20, 100, kRespawnFlagDeath, 3);
        CHECK(p3.fallbackStart);
        // Vector 9 again via ResolveRespawn: mainland death -> nw_hospital, no fallback.
        const RespawnChoice mc = ResolveRespawn(pts, n, 1300, 20, 100, kRespawnFlagDeath, 1);
        CHECK(!mc.fallbackStart && std::string(mc.point->name) == "nw_hospital");
        // Co-op offset: 1.5 m along row 0, negated on the non-host (CHOSEN row-0 convention).
        const Vec3d h = CoopRespawnPosition(pts[3], true), cl = CoopRespawnPosition(pts[3], false);
        CHECK(near(std::hypot(h.x - pts[3].x, h.z - pts[3].z), 1.5, 1e-3));
        CHECK(near(h.x + cl.x, 2.0 * pts[3].x, 1e-9) && near(h.z + cl.z, 2.0 * pts[3].z, 1e-9));
    }
    {
        // Vectors 15 and 16: the fade-in wipe plan.
        FadeInPlan a = PlanFadeIn(8000, 8300);
        CHECK(a.wipeStartMs == 8300 && a.wipeEndMs == 9133 && !a.logoShown);
        FadeInPlan b = PlanFadeIn(8000, 9500);
        CHECK(b.logoShown && b.logoAtMs == 9000 && b.wipeStartMs == 10000 && b.wipeEndMs == 10833);
    }
    {
        // Vector 17: ambulance marker at t=0, player 300/1000: no damage during [0, 500) ms, HP 1000 at the first tick >= 500.
        sr3luahost::EngineState es;
        es.difficultyLevel() = 1;
        auto& pl = es.getOrCreateCharacter("player");
        pl.maxHitPoints.set(1000);
        pl.currentHitPoints.set(300);
        es.armPlayerFullRestore();
        CHECK(es.playerFullRestoreArmed("player"));
        pl.healthRegenReadyAtSeconds = 100.0;  // isolate: no ordinary regeneration inside the 500 ms window
        es.applyCharacterDamage("player", "", 100.0f, 0u);
        CHECK(pl.currentHitPoints.get() == 300);
        for (int i = 0; i < 14; ++i) es.tickHealthRegen(1.0 / 30.0);  // 466 ms
        CHECK(es.playerFullRestoreArmed("player") && pl.currentHitPoints.get() == 300);
        es.applyCharacterDamage("player", "", 100.0f, 0u);
        CHECK(pl.currentHitPoints.get() == 300);
        es.tickHealthRegen(1.0 / 30.0);  // 500 ms
        es.tickHealthRegen(1.0 / 30.0);
        CHECK(!es.playerFullRestoreArmed("player") && pl.currentHitPoints.get() == 1000);
        es.applyCharacterDamage("player", "", 100.0f, 0u);  // timer disarmed: damage applies again
        CHECK(pl.currentHitPoints.get() == 925);
        // The weapons sink path honours the same gate (step 10d).
        es.armPlayerFullRestore();
        const auto r = es.applyDamageModelHit("player", "", 100.0f, 0u);
        CHECK(!r.applied && pl.currentHitPoints.get() == 925);
    }
    {
        // Vectors 18-19: Player_Health_Bonus.
        MaxHpBonusState s{1000, 0, 1000};
        ApplyPlayerHealthBonus(s, 1.5);
        CHECK(s.maxHp == 1500 && s.hp == 1000);  // current HP unchanged
        ApplyPlayerHealthBonus(s, 1.75);
        CHECK(s.maxHp == 1750);  // replaces, not 2250
        MaxHpBonusState t{1337, 0, 1337};
        ApplyPlayerHealthBonus(t, 1.75);
        CHECK(t.bonus == 1002 && t.maxHp == 2339);
        // HP above the new max is clamped (never heals)
        MaxHpBonusState u{1000, 500, 1400};
        ApplyPlayerHealthBonus(u, 1.25);  // base 500 -> bonus 125 -> max 625
        CHECK(u.maxHp == 625 && u.hp == 625);
        // through the engine
        sr3luahost::EngineState es;
        auto& pl = es.getOrCreateCharacter("player");
        pl.maxHitPoints.set(1000);
        pl.currentHitPoints.set(1000);
        CHECK(es.applyPlayerHealthBonus(1.5) == 1500 && pl.currentHitPoints.get() == 1000);
        CHECK(es.applyPlayerHealthBonus(1.75) == 1750);
    }
    {
        // Vectors 20-21: on_respawn fires once with the script name; no slot 0xa (nothing for a bust).
        sr3luahost::EngineState es;
        CHECK(!es.fireOnRespawn("#PLAYER1#"));  // nothing registered
        es.playerOnRespawnCallback_ = "cb";
        CHECK(es.fireOnRespawn("#PLAYER1#"));
        CHECK(es.pendingEngineCallbacks().size() == 1 && es.pendingEngineCallbacks()[0].name == "cb");
        CHECK(es.pendingEngineCallbacks()[0].args.size() == 1);
    }
    {
        // Vectors 22-23: mission deaths.
        const MissionDeathOutcome retry = ResolveMissionDeath(MissionDeathChoice::Retry, false);
        CHECK(retry.checkpointRevive && !retry.hospitalRespawn && !retry.penalty);
        const MissionDeathOutcome quit = ResolveMissionDeath(MissionDeathChoice::Quit, false);
        CHECK(quit.instantWipeToBlack && quit.hospitalRespawn && quit.penalty && !quit.checkpointRevive);
        CHECK(kMissionWipeToBlackMs == 1033);
    }

    if (g_fail) {
        std::printf("player_vitals_engine_test: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("player_vitals_engine_test: all checks passed\n");
    return 0;
}
