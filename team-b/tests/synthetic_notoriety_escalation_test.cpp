// Synthetic tests for include/sr3airuntime/notoriety_escalation.h (spec-ai-runtime.md Sec19.8 gain/decay recipe)
// and include/sr3airuntime/roadblock_notoriety_schedule.h (Sec20.6.10 (a)/(c) roadblock arm conditions and timers).
// Values are the shipped notoriety_levels numbers (spec-tables-progression.md Sec14.6) as encoded in notoriety.h.

#include <iostream>

#include "sr3airuntime/notoriety_escalation.h"
#include "sr3airuntime/roadblock_notoriety_schedule.h"

using namespace sr3airuntime;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

constexpr NotorietyTrack kPol = NotorietyTrack::kPolice;
constexpr NotorietyTrack kGang = NotorietyTrack::kDeckers;

CrimeActivity Plain(int points, int minL = 0, int maxL = 5, bool detect = false, int delay = 0) {
    return CrimeActivity{"test", delay, {points, minL, maxL, detect}, {points, minL, maxL, detect}};
}

void TestLevelRiseAndLimits() {
    // Police limits 75/300/700/1200/2500; gang 75/225/500/1200/2500.
    CHECK(NotorietyEscalation::Limit(kPol, 2) == 300);
    CHECK(NotorietyEscalation::Limit(kGang, 2) == 225);
    NotorietyEscalation n;
    n.ReportCrime(kPol, Plain(74), true, false, 0);
    n.Tick(0);
    CHECK(n.level(kPol) == 0);
    CHECK(n.fraction(kPol) == 74.0);
    n.ReportCrime(kPol, Plain(1), true, false, 0);
    n.Tick(0);
    CHECK(n.level(kPol) == 1);
    // G4: fresh level starts at least 25% of limit(2)=300 -> 75.
    CHECK(n.fraction(kPol) == 75.0);
    // 300 more banked inside level 1 -> level 2 (fraction 75+300-300 = 75 -> floor 0.25*700=175).
    n.ReportCrime(kPol, Plain(225), true, false, 0);
    n.Tick(0);
    CHECK(n.level(kPol) == 2);
    CHECK(n.fraction(kPol) == 175.0);
    // Big single crime rolls several levels: from level 2 frac 175, +2000 -> L3 (needs 700) ... check cap 5.
    n.ReportCrime(kPol, Plain(100000), true, false, 0);
    n.Tick(0);
    CHECK(n.level(kPol) == 5);
    CHECK(n.fraction(kPol) == 0.0);
    // Gang track is independent and uses the gang table.
    CHECK(n.level(kGang) == 0);
}

void TestDelayAndQueue() {
    NotorietyEscalation n;
    n.ReportCrime(kPol, Plain(100, 0, 5, false, /*delay=*/500), true, false, 1000);
    n.Tick(1499);
    CHECK(n.level(kPol) == 0 && n.pendingCount() == 1);
    n.Tick(1500);
    CHECK(n.level(kPol) == 1 && n.pendingCount() == 0);
    // Duplicate assault reports pending at once are dropped (G1).
    NotorietyEscalation m;
    CrimeActivity assault = Plain(10, 0, 5, false, 500);
    assault.name = "civilian assault";
    m.ReportCrime(kPol, assault, true, false, 0);
    m.ReportCrime(kPol, assault, true, false, 0);
    CHECK(m.pendingCount() == 1);
    // Full ring (40) processes the 41st immediately.
    NotorietyEscalation r;
    for (int i = 0; i < 40; ++i) r.ReportCrime(kPol, Plain(0, 0, 5, false, 500), true, false, 0);
    CHECK(r.pendingCount() == 40);
    r.ReportCrime(kPol, Plain(100, 0, 5, false, 500), true, false, 0);
    CHECK(r.pendingCount() == 40 && r.level(kPol) == 1);
}

void TestAwardRules() {
    // Unwitnessed Check_Detection crime: P/4 (min 1).
    NotorietyEscalation n;
    n.ReportCrime(kPol, Plain(40, 0, 5, true), /*witnessed=*/false, false, 0);
    n.Tick(0);
    CHECK(n.fraction(kPol) == 10.0);
    // Min_Level: witnessed crime at level 0 with Min_Level 2 lifts to at least level 2 (pointsTo(2) = 75+300 = 375).
    NotorietyEscalation m;
    m.ReportCrime(kPol, Plain(100, 2, 3), true, false, 0);
    m.Tick(0);
    CHECK(m.level(kPol) == 2);
    // Max_Level: at level >= Max_Level nothing is awarded; A is also capped to pointsTo(Max_Level).
    NotorietyEscalation k;
    k.ReportCrime(kPol, Plain(100000, 0, 1), true, false, 0);
    k.Tick(0);
    CHECK(k.level(kPol) == 1);  // capped at the points needed for level 1 (75), then 25% floor
    const double f = k.fraction(kPol);
    k.ReportCrime(kPol, Plain(500, 0, 1), true, false, 0);
    k.Tick(0);
    CHECK(k.level(kPol) == 1 && k.fraction(kPol) == f);
    // x3 bonus flag.
    NotorietyEscalation b;
    b.ReportCrime(kPol, Plain(20), true, /*bonusX3=*/true, 0);
    b.Tick(0);
    CHECK(b.fraction(kPol) == 60.0);
    // Zero and negative points are skipped (CHOSEN literal reading of G3).
    NotorietyEscalation z;
    z.ReportCrime(kPol, Plain(-20), true, false, 0);
    z.Tick(0);
    CHECK(z.fraction(kPol) == 0.0);
    // Gain suppress.
    NotorietyEscalation g;
    g.gainSuppressed = true;
    g.ReportCrime(kPol, Plain(500), true, false, 0);
    g.Tick(0);
    CHECK(g.level(kPol) == 0);
}

void TestDecay() {
    NotorietyEscalation n;
    n.ReportCrime(kPol, Plain(125), true, false, 0);  // police: level 1, fraction 50 -> floor 75
    n.Tick(0);
    CHECK(n.level(kPol) == 1 && n.fraction(kPol) == 75.0);
    CHECK(n.decayArmed(kPol));
    // First decay is FirstDecay(level 1)=15000 ms; nothing before that.
    n.Tick(14999);
    CHECK(n.fraction(kPol) == 75.0);
    // At 15000: fraction -= DecayRate(police, 1)=6 -> 69, re-armed 1000 ms later.
    n.Tick(15000);
    CHECK(n.fraction(kPol) == 69.0);
    n.Tick(15999);
    CHECK(n.fraction(kPol) == 69.0);
    n.Tick(16000);
    CHECK(n.fraction(kPol) == 63.0);
    // Run to completion: 63 -> ... rolls below 0 -> level 0, fraction 0, decay disarmed.
    int64_t t = 16000;
    while (n.level(kPol) > 0 && t < 200000) {
        t += 1000;
        n.Tick(t);
    }
    CHECK(n.level(kPol) == 0 && n.fraction(kPol) == 72.0);  // D-21 rule 3: landing at level 0 keeps the remainder (75-6*13 = -3 -> +75)
    // k-th decay step (k=1 at 15000 ms) leaves 75-6k; k=13 gives -3 and rolls down: 15000+12*1000 = 27000.
    CHECK(t == 27000);
    CHECK(n.decayArmed(kPol));  // decay continues at the floor level's rate (1 per step) until the fraction reaches 0
    for (int k = 0; k < 72; ++k) { t += 1000; n.Tick(t); }
    CHECK(n.level(kPol) == 0 && n.fraction(kPol) == 0.0);
    CHECK(!n.decayArmed(kPol));
    // A new crime re-arms the first-decay timer from its own time.
    n.ReportCrime(kPol, Plain(10), true, false, 150000);
    n.Tick(150000);
    n.Tick(164999);
    CHECK(n.fraction(kPol) == 10.0);
    n.Tick(165000);  // level 0 FirstDecay 15000, DecayRate(police,0)=1
    CHECK(n.fraction(kPol) == 9.0);
}

void TestDecayGates() {
    NotorietyEscalation n;
    n.ReportCrime(kPol, Plain(125), true, false, 0);
    n.Tick(0);
    n.decaySuppressed = true;
    n.Tick(20000);
    CHECK(n.fraction(kPol) == 75.0);
    n.decaySuppressed = false;
    n.Tick(20000, /*playerInDecayBlockingVehicle=*/true);
    CHECK(n.fraction(kPol) == 75.0);
    n.Tick(20000);
    CHECK(n.fraction(kPol) == 69.0);
    // Hold flag keeps level >= 1: set on a level-0 floor... a level-1 track decays to exactly floor 1.
    NotorietyEscalation h;
    h.ReportCrime(kPol, Plain(100), true, false, 0);  // L1 frac 25 -> floor 75
    h.Tick(0);
    for (int64_t t = 15000; t < 60000; t += 1000) {
        h.SetHoldFlag(kPol);
        h.Tick(t);
    }
    CHECK(h.level(kPol) == 1);
    CHECK(h.fraction(kPol) == 0.0);
}

void TestLevelChangeCallback() {
    NotorietyEscalation n;
    int calls = 0;
    int lastNew = -1;
    n.onLevelChange = [&](const NotorietyLevelEvent& e) {
        ++calls;
        lastNew = e.newLevel;
    };
    n.ReportCrime(kPol, Plain(75), true, false, 0);
    n.Tick(0);
    CHECK(calls == 1 && lastNew == 1);
    n.SetLevel(kPol, 3, 0);
    CHECK(calls == 2 && lastNew == 3 && n.level(kPol) == 3);
    n.SetLevel(kPol, 3, 0);
    CHECK(calls == 2);
}

void TestChosenRecordsShape() {
    CHECK(kChosenCivilianKill.police.checkDetection && kChosenCivilianKill.gang.points == 0);
    // Confirmed record is applied through the recipe: helicopter destroy 500 pts needs Min_Level 4 window.
    NotorietyEscalation n;
    n.ReportCrime(kPol, kConfirmedHelicopterDestroy, true, false, 0);
    n.Tick(500);
    CHECK(n.level(kPol) == 4);  // witnessed crime lifts at least to Min_Level (G3)
}

// ---------------------------------------------------------------------------
// Roadblock schedule (Sec20.6.10).
// ---------------------------------------------------------------------------
void TestRoadblockSchedule() {
    NotorietyRoadblockScheduler s;
    s.uniform = [](int64_t lo, int64_t) { return lo; };  // deterministic: always the minimum
    // Gates.
    CHECK(!s.Update(0, 3, /*driving=*/false, 0).place);
    CHECK(!s.timerArmed(3));  // not driving: nothing armed
    CHECK(!s.Update(0, 0, true, 0).place);
    CHECK(!s.Update(0, 1, true, 0).place);  // level 1 never spawns (no shipped row)
    CHECK(!s.timerArmed(1));
    CHECK(!NotorietyRoadblockScheduler::HasRow(1) && NotorietyRoadblockScheduler::HasRow(2));
    // Level 2: first call arms the timer (15 s), does not place; after 15 s it places.
    CHECK(!s.Update(1000, 2, true, 0).place);
    CHECK(s.timerArmed(2) && s.timerRemainingMs(2, 1000) == 15000);
    CHECK(!s.Update(15999, 2, true, 0).place);
    auto d = s.Update(16000, 2, true, 0);
    CHECK(d.place && d.level == 2);
    // Failure retries in 2000 ms.
    s.ReportPlacement(2, false, 16000);
    CHECK(!s.Update(17999, 2, true, 0).place);
    CHECK(s.Update(18000, 2, true, 0).place);
    // Success re-arms the level interval (15 s at level 2).
    s.ReportPlacement(2, true, 18000);
    CHECK(!s.Update(32999, 2, true, 0).place);
    CHECK(s.Update(33000, 2, true, 0).place);
    // Max active: level 2 allows 1, level 3 allows 2; global cap 3.
    NotorietyRoadblockScheduler c;
    c.uniform = [](int64_t lo, int64_t) { return lo; };
    c.Update(0, 2, true, 1);
    CHECK(!c.timerArmed(2));  // live 1 >= maxActive 1: no timer work
    c.Update(0, 3, true, 1);
    CHECK(c.timerArmed(3));   // live 1 < maxActive 2
    CHECK(!c.Update(99999, 3, true, 2).place);  // live 2 >= maxActive 2
    CHECK(!c.Update(99999, 5, true, 3).place);  // global cap 3
    // Level 5: interval 0..10 s, min 0 -> due immediately on the next call.
    NotorietyRoadblockScheduler e;
    e.uniform = [](int64_t lo, int64_t) { return lo; };
    CHECK(!e.Update(0, 5, true, 0).place);
    CHECK(e.Update(0, 5, true, 0).place);
    // Disabled cvar.
    NotorietyRoadblockScheduler off;
    off.enabled = false;
    CHECK(!off.Update(0, 5, true, 0).place && !off.timerArmed(5));
    // Default RNG stays within [min,max].
    NotorietyRoadblockScheduler r;
    r.Update(0, 4, true, 0);
    const int64_t rem = r.timerRemainingMs(4, 0);
    CHECK(rem >= 5000 && rem <= 20000);
}

void TestEndToEndEscalationToRoadblock() {
    // Kill-level crime spree drives the level up; scheduler only fires from level 2 up, and only while driving.
    NotorietyEscalation n;
    NotorietyRoadblockScheduler s;
    s.uniform = [](int64_t lo, int64_t) { return lo; };
    n.ReportCrime(kPol, kChosenCivilianKill, true, false, 0);
    n.Tick(500);
    CHECK(n.level(kPol) == 1);
    CHECK(!s.Update(500, n.level(kPol), true, 0).place && !s.timerArmed(1));
    n.ReportCrime(kPol, kChosenCivilianKill, true, false, 500);
    n.ReportCrime(kPol, kChosenCivilianKill, true, false, 500);
    n.ReportCrime(kPol, kChosenCivilianKill, true, false, 500);
    n.Tick(1000);
    CHECK(n.level(kPol) >= 2);
    CHECK(!s.Update(1000, n.level(kPol), true, 0).place);  // arms
    CHECK(s.timerArmed(n.level(kPol)));
}
}  // namespace

int main() {
    TestLevelRiseAndLimits();
    TestDelayAndQueue();
    TestAwardRules();
    TestDecay();
    TestDecayGates();
    TestLevelChangeCallback();
    TestChosenRecordsShape();
    TestRoadblockSchedule();
    TestEndToEndEscalationToRoadblock();
    if (g_failures == 0) std::cout << "notoriety escalation tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
