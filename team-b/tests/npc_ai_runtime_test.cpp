// Integration-style tests for the per-NPC AI loop (include/sr3airuntime/npc_ai_runtime.h): perception -> decision ->
// mode -> action over synthetic ai_goals / ai_behavior / combat_actions tables that mirror the shipped data the
// spec describes (spec-ai-runtime.md Sec21.1: goal order, Gang Average timings, Civilian Unarmed).
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "npc_ai_test_tables.h"

using namespace sr3airuntime;
using npc_ai_test::MakeTables;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

constexpr uint64_t kPlayer = 1;
constexpr int64_t kStepMs = 100;

struct Rig {
    std::shared_ptr<NpcAiTables> tables = MakeTables();
    NpcAiRuntime rt;
    PlayerFacts player;
    std::vector<NpcFacts> facts;
    int64_t now = 0;
    std::vector<NpcShot> allShots;
    explicit Rig(NpcAiConfig cfg = {}) : rt(tables, cfg) { player.handle = kPlayer; player.pos = {0, 0, 0}; }
    void add(uint64_t h, NpcKind k, Vec3 pos, bool hostile) {
        NpcSpawn s;
        s.handle = h;
        s.kind = k;
        s.pos = pos;
        rt.addNpc(s, now);
        NpcFacts f;
        f.handle = h;
        f.pos = pos;
        f.hostileToPlayer = hostile;
        facts.push_back(f);
    }
    NpcFacts& fact(uint64_t h) {
        for (auto& f : facts) if (f.handle == h) return f;
        return facts.front();
    }
    void run(int ticks) {
        std::vector<NpcStep> steps;
        std::vector<NpcShot> shots;
        for (int i = 0; i < ticks; ++i) {
            now += kStepMs;
            rt.tick(now, player, facts, steps, shots);
            for (const auto& s : steps)
                for (auto& f : facts)
                    if (f.handle == s.handle) f.pos = s.pos;
            for (auto& f : facts) f.damageFromPlayer = 0;
            allShots.insert(allShots.end(), shots.begin(), shots.end());
        }
    }
    // Runs until pred() or maxTicks; returns ticks used, or -1.
    template <class Pred> int runUntil(int maxTicks, Pred pred) {
        for (int i = 0; i < maxTicks; ++i) {
            run(1);
            if (pred()) return i + 1;
        }
        return -1;
    }
};

bool IsAttackOrCoverMode(int32_t m) {
    return m == mode::FireSpray || m == mode::CombatMovement || m == mode::CoverStay || m == mode::CoverPopout ||
           m == mode::GrenadeThrow || m == mode::Melee;
}

// --- tables --------------------------------------------------------------------------------------------------------
void testTablesLoad() {
    auto t = MakeTables();
    CHECK(t->loadedFromData);
    // goal-enabled bytes: present elements, plus 0/13/16 always.
    CHECK(t->combatant.goalEnabled[goal::ChangeMode] == 1);
    CHECK(t->combatant.goalEnabled[goal::Frustrated] == 1);
    CHECK(t->combatant.goalEnabled[goal::Idle] == 1);
    CHECK(t->combatant.goalEnabled[goal::Cover] == 1);
    CHECK(t->combatant.goalEnabled[goal::Group] == 0);
    CHECK(t->combatant.goalEnabled[goal::Reload] == 0);
    // seen_in_last in ms, monotone.
    CHECK(t->combatant.killEnemySeenInLastMs == 4000);
    CHECK(t->combatant.flushOutSeenInLastMs == 5000);
    CHECK(t->combatant.acquireTargetSeenInLastMs == 10000);
    CHECK(t->combatant.investigateSeenInLastMs == 20000);
    CHECK(t->combatant.stayDownTimeMs == 3000 && t->combatant.popUpTimeMs == 5000 && t->combatant.advanceCoverTimeMs == 4000);
    CHECK(t->combatant.takeCoverWhenShotAt && t->combatant.takeCoverWhenDamaged && !t->combatant.takeCoverWhenAimedAt);
    CHECK(t->combatant.lowHealthPct > 0.39 && t->combatant.lowHealthPct < 0.41);
    // civilian has no Cover element: loader defaults.
    CHECK(t->civilian.goalEnabled[goal::Cover] == 0);
    CHECK(t->civilian.stayDownTimeMs == 1000 && t->civilian.popUpTimeMs == 30000);
    CHECK(t->civilian.acquireTargetSeenInLastMs == 15000);
    // abilities, hold position always enabled.
    CHECK(t->combatant.abilities[action::Fire].enabled && t->combatant.abilities[action::Fire].repeatMinMs == 500);
    CHECK(t->combatant.abilities[action::HoldPosition].enabled);
    // action lists, pre_flags (on foot 1 | must have target 8), No_interrupt.
    CHECK(t->goalActionLists[goal::KillEnemy][5] == action::Fire);
    CHECK(t->goalActionLists[goal::Idle][0] == action::Idle && t->goalActionLists[goal::Idle][1] == ActionBlock::kNoAction);
    CHECK(t->preFlags[action::Fire] == (0x01u | 0x08u));
    CHECK(t->preFlags[action::CoverStayDown] == (0x01u | 0x08u | 0x10u));
    CHECK(t->noInterruptMs[action::CoverTakeDef] == 1000);
    CHECK(t->civilianFleeHealthPct == 50);
}

// --- hostile combatant -----------------------------------------------------------------------------------------
void testHostileNoticesAndAttacks() {
    Rig r;
    r.add(10, NpcKind::Combatant, {20, 0, 0}, /*hostile=*/true);
    CHECK(r.rt.modeOf(10) == mode::kNone || r.rt.modeOf(10) == mode::Idle);
    const int n = r.runUntil(60, [&] { return IsAttackOrCoverMode(r.rt.modeOf(10)); });
    CHECK(n > 0);  // an attack/cover mode within 6 s of game time
    const NpcState* st = r.rt.state(10);
    CHECK(st && st->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));
    CHECK(st && st->ai->world.targetHandleSet && st->ai->world.targetHandle == kPlayer);
    // With no cover available the NPC ends up shooting from where it stands (b = 30 m > 20 m).
    const int m = r.runUntil(200, [&] { return r.rt.modeOf(10) == mode::FireSpray; });
    CHECK(m > 0);
    CHECK(r.rt.actionOf(10) == action::Fire);
    // The mode transitions were logged.
    const auto log = r.rt.drainLog();
    if (std::getenv("NPC_AI_TEST_VERBOSE"))
        for (const auto& l : log)
            std::cout << "t=" << l.nowMs << " npc=" << l.handle << " mode " << l.fromMode << "->" << l.toMode << " action=" << l.action << " goal=" << l.goalReason << " dist=" << l.distToPlayer << '\n';
    CHECK(!log.empty());
    bool sawFire = false;
    for (const auto& l : log) sawFire = sawFire || l.toMode == mode::FireSpray;
    CHECK(sawFire);
}

void testHostileAdvancesWhenFar() {
    Rig r;
    r.add(11, NpcKind::Combatant, {40, 0, 0}, true);  // inside the 45 m view range, outside weapon range b = 30
    const double d0 = 40.0;
    r.run(80);
    const double d1 = std::sqrt(r.fact(11).pos.x * r.fact(11).pos.x + r.fact(11).pos.z * r.fact(11).pos.z);
    CHECK(d1 < d0 - 3.0);  // it moved toward the player
    bool sawMove = false;
    for (const auto& l : r.rt.drainLog()) sawMove = sawMove || l.toMode == mode::CombatMovement;
    CHECK(sawMove);
}

void testNeutralAndFarNpcIgnorePlayer() {
    Rig r;
    r.add(12, NpcKind::Combatant, {10, 0, 0}, /*hostile=*/false);
    r.add(13, NpcKind::Combatant, {200, 0, 0}, /*hostile=*/true);  // far beyond the view range
    r.run(120);
    CHECK(!IsAttackOrCoverMode(r.rt.modeOf(12)));
    CHECK(!IsAttackOrCoverMode(r.rt.modeOf(13)));
    CHECK(!r.rt.state(12)->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));
    CHECK(!r.rt.state(13)->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));
}

void testCoverNodeIsUsed() {
    Rig r;
    r.rt.findCover = [](const Vec3& npc, const Vec3&) -> std::optional<Vec3> { return Vec3{npc.x - 6.0, 0, 0}; };
    r.add(14, NpcKind::Combatant, {20, 0, 0}, true);
    const int n = r.runUntil(80, [&] { return r.rt.modeOf(14) == mode::CoverStay; });
    CHECK(n > 0);
    r.run(60);
    const NpcState* st = r.rt.state(14);
    CHECK(st && st->hasCover);
    const double dx = r.fact(14).pos.x - st->coverPos.x;
    CHECK(std::fabs(dx) < 2.5);  // walked to the cover node
}

void testDamageTurnsNeutralNpcOnShooter() {
    Rig r;
    r.add(15, NpcKind::Combatant, {15, 0, 0}, /*hostile=*/false);
    r.run(20);
    CHECK(!IsAttackOrCoverMode(r.rt.modeOf(15)));
    r.fact(15).damageFromPlayer = 5.0;
    r.fact(15).provoked = true;
    r.fact(15).hostileToPlayer = true;
    const int n = r.runUntil(60, [&] { return IsAttackOrCoverMode(r.rt.modeOf(15)); });
    CHECK(n > 0);
}

void testFireProducesHitsThroughShots() {
    Rig r;
    r.add(16, NpcKind::Combatant, {10, 0, 0}, true);
    r.rt.findCover = nullptr;
    r.run(300);
    CHECK(!r.allShots.empty());
    int hits = 0;
    for (const auto& s : r.allShots) {
        CHECK(s.shooter == 16);
        if (s.hit) {
            ++hits;
            CHECK(s.damage > 0);
        }
    }
    CHECK(hits > 0 && hits < static_cast<int>(r.allShots.size()));  // accuracy < 1

    NpcAiConfig quiet;
    quiet.fireEnabled = false;
    Rig q(quiet);
    q.add(17, NpcKind::Combatant, {10, 0, 0}, true);
    q.run(300);
    CHECK(q.allShots.empty());
}

// --- civilians -------------------------------------------------------------------------------------------------
void testCivilianFleesWhenAttacked() {
    Rig r;
    r.add(20, NpcKind::Civilian, {5, 0, 0}, /*hostile=*/false);
    r.run(10);
    CHECK(r.rt.modeOf(20) == mode::Idle || r.rt.modeOf(20) == mode::kNone);
    r.fact(20).damageFromPlayer = 3.0;
    r.fact(20).provoked = true;
    const int n = r.runUntil(40, [&] { return r.rt.modeOf(20) == mode::Flee || r.rt.modeOf(20) == mode::Cower; });
    CHECK(n > 0);
}

void testCivilianFleeMovesAwayAndStops() {
    Rig r;
    // Pick a seed/handle where the 80/20 flee-or-cower roll picks flee: try several civilians, require most to flee.
    int fled = 0, total = 0;
    for (uint64_t h = 30; h < 50; ++h) {
        Rig q;
        q.add(h, NpcKind::Civilian, {5, 0, 0}, false);
        q.run(5);
        q.fact(h).damageFromPlayer = 3.0;
        q.fact(h).provoked = true;
        q.run(30);
        ++total;
        if (q.rt.modeOf(h) == mode::Flee) {
            ++fled;
            const double d = std::fabs(q.fact(h).pos.x);
            CHECK(d > 5.0);  // moved away
            q.run(400);       // runs 40 s: beyond 30 m the flee ends (Sec21.4 stop rule)
            CHECK(q.rt.modeOf(h) != mode::Flee);
            CHECK(std::fabs(q.fact(h).pos.x) > 29.0);
        }
    }
    CHECK(fled >= total / 2);  // Sec21.4: about 80% flee
}

void testCivilianReactsToGunfireNearby() {
    Rig r;
    r.add(60, NpcKind::Civilian, {10, 0, 0}, false);
    r.add(61, NpcKind::Civilian, {200, 0, 0}, false);  // too far to react
    r.run(5);
    r.player.firingOrAiming = true;
    const int n = r.runUntil(40, [&] { return r.rt.modeOf(60) == mode::Flee || r.rt.modeOf(60) == mode::Cower; });
    CHECK(n > 0);
    r.run(20);
    CHECK(r.rt.modeOf(61) != mode::Flee && r.rt.modeOf(61) != mode::Cower);
}

// --- AI-4: sense-pass cadence, Sec21.1 / Sec10 sense pass step 7: 650-800 ms, or 350-450 ms when the activity level is
// below 6 and the human is not a civilian. Observed through NpcState::nextSenseMs (re-armed at every pass).
void testSenseCadence() {
    auto intervals = [](Vec3 pos, bool inCombatFirst) {
        auto tables = MakeTables();
        NpcAiRuntime rt(tables);
        PlayerFacts pl;
        pl.pos = {0, 0, 0};
        NpcSpawn sp;
        sp.handle = 5;
        sp.kind = NpcKind::Combatant;
        sp.pos = pos;
        rt.addNpc(sp, 0);
        NpcFacts f;
        f.handle = 5;
        f.pos = pos;
        f.hostileToPlayer = true;
        std::vector<NpcFacts> facts{f};
        std::vector<NpcStep> steps;
        std::vector<NpcShot> shots;
        std::vector<int64_t> out;
        int64_t last = rt.state(5) ? rt.state(5)->nextSenseMs : 0;
        const int64_t limit = inCombatFirst ? 40000 : 12000;
        for (int64_t now = 10; now < limit; now += 10) {
            const bool hadTarget = rt.state(5)->targetSet;  // the state the pass saw (the target is written after it)
            rt.tick(now, pl, facts, steps, shots);
            for (const auto& s : steps) facts[0].pos = s.pos;
            const NpcState* st = rt.state(5);
            if (st->nextSenseMs != last) {
                // only intervals set while the NPC is in the state under test
                if (st->lastTickMs > 10 && (!inCombatFirst || hadTarget)) out.push_back(st->nextSenseMs - now);  // skip the seeding tick
                last = st->nextSenseMs;
            }
        }
        return out;
    };
    // Far hostile (100 m: no sight, no proximity): not in combat, activity level < 6 -> 350-450 ms.
    const auto far = intervals({100, 0, 0}, false);
    CHECK(far.size() >= 10);
    for (int64_t d : far) CHECK(d >= 350 && d <= 450);
    // In combat (a target is set): activity level 6 -> 650-800 ms.
    const auto fight = intervals({20, 0, 0}, true);
    CHECK(fight.size() >= 5);
    for (int64_t d : fight) CHECK(d >= 650 && d <= 800);
}

// --- AI-5: attitude-dependent damage reaction, Sec10 list 16 / list 8 (0x00504130) and notice 0x00500f70.
// Attitude 4+: only a list-8 friendly-fire record, never list 0 (the player does not become a hostile). Attitude < 3: the
// attacker is noticed (list 0), unless force flag never_turn_on_player is set.
void testFriendlyFireDoesNotTurnAlly() {
    Rig r;
    r.add(80, NpcKind::Combatant, {15, 0, 0}, /*hostile=*/false);
    r.fact(80).attitudeToPlayer = 4;  // allied (a homie)
    r.run(10);
    for (int i = 0; i < 5; ++i) {
        r.fact(80).damageFromPlayer = 5.0;
        r.fact(80).provoked = true;
        r.run(10);
    }
    const NpcState* st = r.rt.state(80);
    CHECK(st && st->ai->perception.hasEntry(plist::FriendlyFire, kPlayer));        // list 8: friendly fire
    CHECK(st && !st->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));      // never list 0
    r.run(100);
    CHECK(!IsAttackOrCoverMode(r.rt.modeOf(80)));
    CHECK(r.allShots.empty());
}
void testHostileAttitudeNoticesAttackerButNeverTurnFlag() {
    {
        Rig r;
        r.add(81, NpcKind::Combatant, {15, 0, 0}, false);
        r.fact(81).attitudeToPlayer = 2;
        r.run(10);
        r.fact(81).damageFromPlayer = 5.0;
        r.fact(81).provoked = true;
        CHECK(r.runUntil(60, [&] { return IsAttackOrCoverMode(r.rt.modeOf(81)); }) > 0);
        CHECK(r.rt.state(81)->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));
    }
    {
        Rig r;
        r.add(82, NpcKind::Combatant, {15, 0, 0}, false);
        r.fact(82).attitudeToPlayer = 2;
        r.fact(82).neverTurnOnPlayer = true;  // force flag never_turn_on_player: no player can ever enter list 0
        r.run(10);
        r.fact(82).damageFromPlayer = 5.0;
        r.fact(82).provoked = true;
        r.run(100);
        CHECK(!r.rt.state(82)->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));
        CHECK(!IsAttackOrCoverMode(r.rt.modeOf(82)));
    }
}

// --- AI-6: Sec21.3 timers / TV2: on the FIRST cover entry, while +0x315 bit 7 is clear and recent damage is below 20%,
// the stay-down timer is 800 ms; later entries (and a damaged first entry) use stay_down_time x U(0.8, 1.25).
void testFirstCoverEntryStayDown800() {
    auto firstStayDown = [](double damage) -> int64_t {
        Rig r;
        r.rt.findCover = [](const Vec3& npc, const Vec3&) -> std::optional<Vec3> { return Vec3{npc.x - 3.0, 0, 0}; };
        r.add(90, NpcKind::Combatant, {20, 0, 0}, true);
        if (damage > 0) {
            r.fact(90).damageFromPlayer = damage;  // 30 of max 100 = 0.3 >= 0.2 remembered damage
            r.fact(90).provoked = true;
        }
        int64_t d = -1;
        for (int i = 0; i < 400 && d < 0; ++i) {
            r.run(1);
            const NpcState* st = r.rt.state(90);
            if (st->hasCover) d = st->ai->coverStayDownTimer.durationMs;
        }
        return d;
    };
    CHECK(firstStayDown(0.0) == 800);
    const int64_t damaged = firstStayDown(30.0);
    CHECK(damaged >= 2400 && damaged <= 3750);  // Gang Average stay_down_time 3000 x U(0.8, 1.25)
    // Second entry (after the abandon check dropped the first cover): never 800 again.
    Rig r;
    r.rt.findCover = [](const Vec3& npc, const Vec3&) -> std::optional<Vec3> { return Vec3{npc.x - 3.0, 0, 0}; };
    r.add(91, NpcKind::Combatant, {20, 0, 0}, true);
    bool had = false;
    std::vector<int64_t> durations;
    for (int i = 0; i < 1500 && durations.size() < 2; ++i) {
        r.run(1);
        const NpcState* st = r.rt.state(91);
        if (st->hasCover && !had) durations.push_back(st->ai->coverStayDownTimer.durationMs);
        had = st->hasCover;
    }
    CHECK(durations.size() >= 1 && durations[0] == 800);
    if (durations.size() >= 2) CHECK(durations[1] >= 2400 && durations[1] <= 3750);
}

// --- AI-9: Sec21.4 flee tick / Sec20.6.9(a): the 30 m stop is measured to the LIVE threat position, not a snapshot,
// and the flee speed is the measured cower-run clip speed (4.554 m/s male, 5.745 m/s female). Gunfire beyond 20 m
// (400 squared) is answered by the delayed event 67 (250-750 ms).
void testFleeTracksLivePlayerAndMeasuredSpeed() {
    int checked = 0;
    for (uint64_t h = 100; h < 130 && checked < 4; ++h) {
        Rig q;
        q.add(h, NpcKind::Civilian, {5, 0, 0}, false);
        q.run(5);
        q.fact(h).damageFromPlayer = 3.0;
        q.fact(h).provoked = true;
        q.run(3);
        if (q.rt.modeOf(h) != mode::Flee) continue;
        ++checked;
        // The player chases: stays 6 m behind the civilian for 60 s. A snapshot threat point would end the flee at 30 m.
        double lastX = q.fact(h).pos.x;
        double speed = -1;
        for (int i = 0; i < 600; ++i) {
            q.player.pos = {q.fact(h).pos.x - 6.0, 0, 0};
            q.run(1);
            if (i == 19) lastX = q.fact(h).pos.x;
            if (i == 29) speed = (q.fact(h).pos.x - lastX) / 1.0;
        }
        CHECK(q.rt.modeOf(h) == mode::Flee);
        CHECK(speed > 4.3 && speed < 4.8);  // 4.554 m/s measured (male: the host has no gender fact)
    }
    CHECK(checked >= 1);
    // Female spawn: 5.745 m/s.
    for (uint64_t h = 140; h < 170; ++h) {
        Rig q;
        NpcSpawn sp;
        sp.handle = h;
        sp.kind = NpcKind::Civilian;
        sp.pos = {5, 0, 0};
        sp.female = true;
        q.rt.addNpc(sp, q.now);
        NpcFacts f;
        f.handle = h;
        f.pos = sp.pos;
        q.facts.push_back(f);
        q.run(5);
        q.fact(h).damageFromPlayer = 3.0;
        q.fact(h).provoked = true;
        q.run(3);
        if (q.rt.modeOf(h) != mode::Flee) continue;
        const double x0 = q.fact(h).pos.x;
        for (int i = 0; i < 10; ++i) {
            q.player.pos = {q.fact(h).pos.x - 6.0, 0, 0};
            q.run(1);
        }
        const double v = q.fact(h).pos.x - x0;
        CHECK(v > 5.5 && v < 6.0);
        break;
    }
}
void testDistantGunfireReactionIsDelayed() {
    // 25 m away (beyond the 20 m squared-400 test): the reaction waits 250-750 ms. 10 m: immediate.
    auto reactionDelay = [](double dx) {
        Rig r;
        r.add(150, NpcKind::Civilian, {dx, 0, 0}, false);
        r.run(5);
        r.player.firingOrAiming = true;
        const int64_t t0 = r.now;
        r.run(1);
        r.player.firingOrAiming = false;
        for (int i = 0; i < 40; ++i) {
            const int32_t m = r.rt.modeOf(150);
            if (m == mode::Flee || m == mode::Cower) return r.now - t0;
            r.run(1);
        }
        return int64_t(-1);
    };
    const int64_t far = reactionDelay(25.0);
    CHECK(far >= 250 && far <= 750 + kStepMs);
    const int64_t near = reactionDelay(10.0);
    CHECK(near >= 0 && near <= kStepMs);
}

// --- determinism ----------------------------------------------------------------------------------------------------
void testDeterministic() {
    auto runOnce = [] {
        Rig r;
        r.add(70, NpcKind::Combatant, {25, 0, 0}, true);
        r.add(71, NpcKind::Civilian, {6, 0, 0}, false);
        r.run(20);
        r.fact(71).damageFromPlayer = 2.0;
        r.run(300);
        std::string out;
        for (const auto& l : r.rt.drainLog())
            out += std::to_string(l.handle) + ":" + std::to_string(l.nowMs) + ":" + std::to_string(l.toMode) + ":" +
                   std::to_string(l.action) + ";";
        out += std::to_string(r.allShots.size());
        return out;
    };
    CHECK(runOnce() == runOnce());
}

}  // namespace

// --- AI-3: the LOS seam gates the notice, and the answer is cached (700-4000 ms re-check) ------------------------------
void testLosGatesNoticeAndIsCached() {
    int calls = 0;
    bool blocked = true;
    {
        Rig r;
        r.rt.lineOfSight = [&](const Vec3&, const Vec3&) { ++calls; return !blocked; };
        r.add(30, NpcKind::Combatant, {40, 0, 0}, true);  // inside the 45 m view range, outside the 25 m proximity notice
        r.run(300);                                       // ~10 s of game time at kStepMs
        const NpcState* st = r.rt.state(30);
        CHECK(st && !st->ai->perception.hasEntry(plist::KnownHostiles, kPlayer));  // a wall in between: never noticed
        CHECK(calls >= 1);
        CHECK(calls <= 15);  // cached: at most one ray per 700 ms, not one per tick
        CHECK(r.rt.stats().losChecks == static_cast<uint64_t>(calls));
        CHECK(r.rt.stats().losBlocked == static_cast<uint64_t>(calls));
    }
    {
        Rig r;
        blocked = false;
        r.rt.lineOfSight = [&](const Vec3&, const Vec3&) { return !blocked; };
        r.add(31, NpcKind::Combatant, {40, 0, 0}, true);
        const int n = r.runUntil(300, [&] {
            const NpcState* st = r.rt.state(31);
            return st && st->ai->perception.hasEntry(plist::KnownHostiles, kPlayer);
        });
        CHECK(n > 0);  // a clear ray: noticed
    }
}

// --- AI-7: the Sec21.3 search over a cover node pool -----------------------------------------------------------------
void testCoverSearchUsesNodePool() {
    Rig r;
    // A wall at x = 24: the ray from the player (x = 0) to a node beyond it is blocked; every other ray is clear.
    r.rt.lineOfSight = [](const Vec3& a, const Vec3& b) { return !((std::min)(a.x, b.x) < 24.0 && (std::max)(a.x, b.x) > 24.0); };
    r.rt.setCoverNodes({Vec3{26, 0, 3},     // A: shielded, inside the box
                        Vec3{12, 0, 3},     // B: inside the box but the player sees it (does not shield)
                        Vec3{26, 10, 3},    // C: shielded but outside the +-4 m vertical band
                        Vec3{60, 0, 3}});   // D: shielded but outside the 14 m box
    CHECK(r.rt.coverNodeCount() == 4);
    r.add(14, NpcKind::Combatant, {20, 0, 0}, true);
    const int n = r.runUntil(120, [&] { return r.rt.modeOf(14) == mode::CoverStay; });
    CHECK(n > 0);
    r.run(5);
    const NpcState* st = r.rt.state(14);
    CHECK(st && st->hasCover);
    CHECK(st && st->coverNodeIdx == 0);
    CHECK(st && std::fabs(st->coverPos.x - 26.0) < 1e-9);
    CHECK(r.rt.stats().coverFound >= 1);
    // The node is claimed: a second NPC searching finds no unclaimed shielding node; removing the owner frees it.
    r.add(15, NpcKind::Combatant, {21, 0, 0}, true);
    r.run(240);
    const NpcState* st2 = r.rt.state(15);
    CHECK(st2 && st2->coverNodeIdx != 0);
    CHECK(r.rt.stats().coverNoNode >= 1);
    r.rt.removeNpc(14);
    r.run(240);
    st2 = r.rt.state(15);
    CHECK(st2 && (st2->coverNodeIdx == 0 || st2->coverNodeIdx == -1));
}

void testCoverSearchFailsWithoutShieldTest() {
    Rig r;  // no LOS seam: nothing can be shown to shield, so every search fails and backs off
    r.rt.setCoverNodes({Vec3{26, 0, 3}});
    r.add(16, NpcKind::Combatant, {20, 0, 0}, true);
    r.run(300);
    const NpcState* st = r.rt.state(16);
    CHECK(st && !st->hasCover);
    CHECK(r.rt.stats().coverFound == 0);
    CHECK(r.rt.stats().coverNoNode >= 1);
}

int main() {
    testLosGatesNoticeAndIsCached();
    testCoverSearchUsesNodePool();
    testCoverSearchFailsWithoutShieldTest();
    testTablesLoad();
    testHostileNoticesAndAttacks();
    testHostileAdvancesWhenFar();
    testNeutralAndFarNpcIgnorePlayer();
    testCoverNodeIsUsed();
    testDamageTurnsNeutralNpcOnShooter();
    testFireProducesHitsThroughShots();
    testCivilianFleesWhenAttacked();
    testCivilianFleeMovesAwayAndStops();
    testCivilianReactsToGunfireNearby();
    testSenseCadence();
    testFriendlyFireDoesNotTurnAlly();
    testHostileAttitudeNoticesAttackerButNeverTurnFlag();
    testFirstCoverEntryStayDown800();
    testFleeTracksLivePlayerAndMeasuredSpeed();
    testDistantGunfireReactionIsDelayed();
    testDeterministic();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "npc_ai_runtime_test: all checks passed\n";
    return 0;
}
