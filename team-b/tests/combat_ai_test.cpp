// Tests for include/sr3airuntime/combat_ai.h - spec-ai-runtime.md Sec21 (combat and NPC behaviour) and Sec22
// (mission-driving AI), checked against the spec's own test vectors (Sec21.6, Sec22.9, Sec22.11).
#include <cmath>
#include <cstdio>

#include "sr3airuntime/combat_ai.h"

using namespace sr3airuntime::combat;

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

int main() {
    // --- Sec21.4 "flee rather than fight" 0x004ead80 port rule ---
    {
        FleeInputs gang;  // every gang/police record: Flee_health_pct 0, no flags
        gang.eligible = true;
        gang.canFight = true;
        gang.fleeHealthPct = 0;
        gang.healthFraction = 1.0;
        CHECK(!FleeRatherThanFight(gang));
        gang.healthFraction = 0.0;  // 0 > 0 is false: a record with 0 never flees on health
        CHECK(!FleeRatherThanFight(gang));

        FleeInputs coward = gang;  // "civilian cowardly" Flee_health_pct 80: percent vs FRACTION, always true
        coward.fleeHealthPct = 80;
        coward.healthFraction = 1.0;
        CHECK(FleeRatherThanFight(coward));
        FleeInputs tough = gang;
        tough.fleeHealthPct = 20;
        CHECK(FleeRatherThanFight(tough));  // 20 > 1.0 as well (retail's percent-vs-fraction compare)
        FleeInputs one = gang;
        one.fleeHealthPct = 1;
        one.healthFraction = 1.0;
        CHECK(!FleeRatherThanFight(one));  // 1 > 1.0 is false: the compare is strict

        FleeInputs cantCower = gang;  // step 1 wins over everything below it
        cantCower.eligible = false;
        cantCower.cantCower = true;
        cantCower.alwaysCower = true;
        cantCower.fleeHealthPct = 80;
        CHECK(!FleeRatherThanFight(cantCower));

        FleeInputs cannotFight = gang;
        cannotFight.canFight = false;
        CHECK(FleeRatherThanFight(cannotFight));
        FleeInputs cow = gang;
        cow.alwaysCower = true;
        CHECK(FleeRatherThanFight(cow));
        FleeInputs bit6 = gang;
        bit6.flag317Bit6 = true;
        CHECK(FleeRatherThanFight(bit6));

        FleeInputs events = gang;  // flee_events bits: bullet hit 0x08, melee 0x02, explosion 0x10 (needs list 15)
        events.fleeEvents = 0x08;
        events.bullet = true;
        CHECK(FleeRatherThanFight(events));
        events.bullet = false;
        CHECK(!FleeRatherThanFight(events));  // melee test reads bit 0x02 only
        events.fleeEvents = 0x02;
        CHECK(FleeRatherThanFight(events));
        events.fleeEvents = 0x10;
        events.list15NonEmpty = false;
        CHECK(!FleeRatherThanFight(events));
        events.list15NonEmpty = true;
        CHECK(FleeRatherThanFight(events));
    }

    // --- Sec21.4 flee/cower chooser 0x004eaae0, TV4 ---
    {
        ChooserInputs ci;
        CHECK(ChooseFleeOrCower(ci, 0.79) == FleeCower::Flee);   // 0.8 threshold, random <= constant
        CHECK(ChooseFleeOrCower(ci, 0.80) == FleeCower::Flee);
        CHECK(ChooseFleeOrCower(ci, 0.81) == FleeCower::Cower);
        ChooserInputs cowering;
        cowering.inCower = true;  // a cowering civilian re-rolls at 50%
        CHECK(ChooseFleeOrCower(cowering, 0.49) == FleeCower::Flee);
        CHECK(ChooseFleeOrCower(cowering, 0.51) == FleeCower::None);  // already Cower: no new request, keeps cowering
        ChooserInputs cantFlee;
        cantFlee.cantFlee = true;  // always cowers
        CHECK(ChooseFleeOrCower(cantFlee, 0.0) == FleeCower::Cower);
        ChooserInputs cantCower;
        cantCower.cantCower = true;  // always flees
        CHECK(ChooseFleeOrCower(cantCower, 0.99) == FleeCower::Flee);
        ChooserInputs panic;
        panic.panicPending = true;  // step 1: nothing
        CHECK(ChooseFleeOrCower(panic, 0.1) == FleeCower::None);
        ChooserInputs gateRefuses;
        gateRefuses.fleeGateOk = false;  // preferFlee but the flee gate refuses: falls to cower
        CHECK(ChooseFleeOrCower(gateRefuses, 0.1) == FleeCower::Cower);
        ChooserInputs alwaysCower;
        alwaysCower.alwaysCower = true;  // skips the flee branch entirely
        CHECK(ChooseFleeOrCower(alwaysCower, 0.0) == FleeCower::Cower);
        ChooserInputs noCower;
        noCower.fleeGateOk = false;
        noCower.cowerEligible = false;
        CHECK(ChooseFleeOrCower(noCower, 0.9) == FleeCower::None);
    }

    // --- Sec21.4 flee tick stop rule: squared distance against 900 (30 m) ---
    CHECK(FleeFromStillActive(899.0));
    CHECK(FleeFromStillActive(900.0));
    CHECK(!FleeFromStillActive(900.5));

    // --- Sec21.1 advance step 6 / 0x004e68e0 band test ---
    {
        // a = minimum range 5, b = maximum range 20; human at origin, target on +x.
        CHECK(WeaponBand(4.0 * 4.0, 5.0, 20.0) == -1);
        CHECK(WeaponBand(10.0 * 10.0, 5.0, 20.0) == 0);
        CHECK(WeaponBand(21.0 * 21.0, 5.0, 20.0) == 1);
        CHECK(WeaponBand(20.0 * 20.0, 5.0, 20.0) == 0);  // d2 > b^2 is strict
        CHECK(AdvanceBand(/*targetKnown*/ false, false, false, 10.0 * 10.0, 5.0, 20.0) == 1);
        CHECK(AdvanceBand(true, false, false, 10.0 * 10.0, 5.0, 20.0) == 0);
        CHECK(AdvanceBand(true, false, false, 21.0 * 21.0, 5.0, 20.0) == 1);
        CHECK(AdvanceBand(true, true, /*withinAxisBox*/ false, 10.0 * 10.0, 5.0, 20.0) == 1);   // downed: 1 unless close
        CHECK(AdvanceBand(true, true, /*withinAxisBox*/ true, 10.0 * 10.0, 5.0, 20.0) == 0);
        CHECK(AdvanceStepSixPasses(AdvanceBand(true, false, false, 25.0 * 25.0, 5.0, 20.0)));
        CHECK(!AdvanceStepSixPasses(AdvanceBand(true, false, false, 3.0 * 3.0, 5.0, 20.0)));
        // out point on the near side: entry + unit(human - entry) * (a + 0.1 (b - a)) = 6.5 from the entry
        CHECK(std::fabs(WeaponBandOutDistance(-1, 5.0, 20.0) - 6.5) < 1e-9);
        CHECK(std::fabs(WeaponBandOutDistance(+1, 5.0, 20.0) - 18.5) < 1e-9);
    }

    // --- Sec21.3 cover: search box, threat cost, timers ---
    {
        // TV3: NPC at origin, brute false: box x,z in [-14,14], y in [-4,4]
        CHECK(CoverNodeInSearchBox(10, 0, 10, false));
        CHECK(CoverNodeInSearchBox(13, 0, 13, false));
        CHECK(!CoverNodeInSearchBox(15, 0, 0, false));
        CHECK(!CoverNodeInSearchBox(5, 5, 0, false));
        CHECK(CoverNodeInSearchBox(15, 0, 0, true));  // brute half-width 15.0
        // threat term: cost = (anyPrimary ? 0 : wA) + (count - shielded) * wB; aggression 2 -> 8/7, else 5/10
        CHECK(std::fabs(CoverThreatCost(false, 2, 1, 2) - (8.0 + 1 * 7.0)) < 1e-9);
        CHECK(std::fabs(CoverThreatCost(true, 2, 1, 2) - 7.0) < 1e-9);
        CHECK(std::fabs(CoverThreatCost(false, 3, 1, 0) - (5.0 + 2 * 10.0)) < 1e-9);
        CHECK(std::fabs(CoverThreatCost(true, 1, 1, 0)) < 1e-9);
        // evaluator counting: an entry also in list 16 (damage) or 3 (shot at) counts twice
        // fields: primaryArc, defArc (shielded), inList16 (damage), inList3 (shot at)
        ThreatEntry es[3] = {{true, true, false, false}, {true, false, true, false}, {false, false, false, false}};
        auto ev = EvaluateCoverThreats(es, 3, /*threatIsPlayer*/ false);
        // entry0: count 1, shielded 1; entry1 (also list 16): count 2, shielded 0; entry2: count 1, shielded 0
        CHECK(ev.count == 4 && ev.shielded == 1 && ev.anyPrimary && !ev.disqualified);
    }
    {
        // evaluator: only list-0 entries with flag 0x8 resolving to a live object are passed in (flag-less are skipped
        // by the caller); a player threat that is NOT shielded disqualifies the node with count 0.
        ThreatEntry e[1] = {{true, false, false, false}};
        auto ev = EvaluateCoverThreats(e, 1, /*threatIsPlayer*/ true);
        CHECK(ev.disqualified && ev.count == 0);
        auto ev2 = EvaluateCoverThreats(e, 1, /*threatIsPlayer*/ false);
        CHECK(!ev2.disqualified && ev2.count == 1 && ev2.shielded == 0);
    }
    {
        // TV2: Gang Average timers. stay-down 3 s x U(0.8, 1.25) = 2.4..3.75 s; find-new 15 s x U = 12.0..18.75 s
        CHECK(std::fabs(CoverTimerMs(3000, 0.0) - 2400.0) < 1e-6);
        CHECK(std::fabs(CoverTimerMs(3000, 1.0) - 3750.0) < 1e-6);
        CHECK(std::fabs(CoverTimerMs(15000, 0.0) - 12000.0) < 1e-6);
        CHECK(std::fabs(CoverTimerMs(15000, 1.0) - 18750.0) < 1e-6);
        CHECK(CoverAdvanceEligible(4000, 4000));   // cover advance when time-in-cover reached advance_cover_time
        CHECK(!CoverAdvanceEligible(3999, 4000));
        CHECK(CoverPopUpWindowMs(5000, false) == 5000);
        CHECK(CoverPopUpWindowMs(5000, true) == 1000);  // 1 s when being shot at (+0x76f bit 3 and list 3 non-empty)
    }

    // --- Sec21.4 vehicle hostage release (mode 20) ---
    {
        HostageState h;
        HostageEnter(h, /*rc*/ false, /*nowMs*/ 0, /*rand*/ 0.0);
        CHECK(h.exitAtMs == 5000);  // initial delay D = 5000, U(D, D+100)
        HostageEnter(h, false, 0, 1.0);
        CHECK(h.exitAtMs == 5100);
        HostageEnter(h, true, 0, 0.0);
        CHECK(h.exitAtMs == 6000);
        // TV: non-RC car held at 20 mph for 10 s never exits; drops to 10 mph at t0 and exits at t0 + 2000
        HostageEnter(h, false, 0, 0.0);
        bool exited = false;
        for (int64_t t = 0; t <= 10000; t += 100) exited = exited || HostageTick(h, false, 20.0, false, t);
        CHECK(!exited);
        CHECK(!HostageTick(h, false, 10.0, false, 10000));  // re-armed to now + 2000 at the last fast tick
        CHECK(!HostageTick(h, false, 10.0, false, 11900));
        CHECK(HostageTick(h, false, 10.0, false, 12000));
        // freshly grabbed, already below 15 mph: exits 5000-5100 ms after entry
        HostageState f;
        HostageEnter(f, false, 1000, 0.5);
        CHECK(!HostageTick(f, false, 5.0, false, 5999));
        CHECK(HostageTick(f, false, 5.0, false, 6050));
        // limits: 15 mph normal, 5 mph RC (table value; the 10.0 constant is the registration default)
        HostageState r;
        HostageEnter(r, true, 0, 0.0);
        CHECK(!HostageTick(r, true, 5.0, false, 100));  // >= limit re-arms to now + 4000
        CHECK(r.exitAtMs == 4100);
        CHECK(HostageMph(10.0) > 22.3 && HostageMph(10.0) < 22.4);  // 10 m/s * 2.236936
        HostageState air;
        HostageEnter(air, false, 0, 0.0);
        CHECK(!HostageTick(air, false, 0.0, /*airborne*/ true, 100));
        CHECK(air.exitAtMs == 2100);
    }

    // --- Sec21.4 downed type 0: deadline, revive heal (third pass) ---
    {
        CHECK(DownedDeadlineMs(0, 1.0, false) == 30000);
        CHECK(DownedDeadlineMs(1000, 2.0, false) == 61000);   // 30000 x difficulty scale
        CHECK(DownedDeadlineMs(0, 2.0, /*mode5*/ true) == 15000);  // fixed 15000 in game mode 5
        CHECK(DownedGetUpHeal(200, /*enemySide*/ true) == 50.0f);   // get-up/revive path: enemy +0x1ca8 != 0 -> 0.25
        CHECK(DownedGetUpHeal(200, false) == 200);               // human side -> 1.0
        // Sec21.10.1 FINAL (supersedes the old "DownedStandsUp"): type-0 expiry KILLS (vector 1).
        CHECK(DownedType0ExpiryKills(29999, 30000, false) == false);
        CHECK(DownedType0ExpiryKills(30000, 30000, false) == true);
        CHECK(DownedType0ExpiryKills(99999, 30000, /*noTimer*/ true) == false);  // flag bit 3: no timer
    }

    // --- Sec21.10.1 selector and type-1 vectors 4-6 ---
    {
        DownedSelectorInputs in;
        CHECK(SelectDownedType(in) == -1);  // plain death
        in.isBrute = true; in.isGangMember = true;
        CHECK(SelectDownedType(in) == 1);
        in.bit22 = true;
        CHECK(SelectDownedType(in) == -1);
        in = {}; in.isNonPlayerFollower = true; in.isBrute = true; in.isGangMember = true;
        CHECK(SelectDownedType(in) == 0);  // follower is checked before the brute rule
        in = {}; in.noDownedBit17 = true; in.isNonPlayerFollower = true;
        CHECK(SelectDownedType(in) == -1);
        in = {}; in.killbaneWrestling = true; CHECK(SelectDownedType(in) == 2);
        in = {}; in.killbaneSciFi = true; CHECK(SelectDownedType(in) == 3);
        in = {}; in.isAvatarNotPlayerClass = true; CHECK(SelectDownedType(in) == 4);
        // vector 4: threshold 300, killing hit leaves -50 -> type 1, buffer 250, deadline 7000
        Type1Inputs t1;
        t1.hpAfterKillingHit = -50; t1.threshold = 300;
        CHECK(Type1Predicate(t1));
        CHECK(Type1EnterBuffer(-50, 300) == 250);
        // hit for 100 at t=1000: buffer 150, remaining min(6000 + 5000, 7000) = 7000
        Type1DamageResult r = Type1ApplyDamage(250, 6000, 100.0, false);
        CHECK(!r.dies && r.buffer == 150 && r.remainingMs == 7000);
        // hit for 200: buffer -50 -> dies
        r = Type1ApplyDamage(150, 7000, 200.0, false);
        CHECK(r.dies);
        CHECK(Type1ApplyDamage(150, 7000, 1.0, /*lethal -1.0 constant*/ true).dies);
        // a small hit extends by 50 ms per point, capped at 7000
        r = Type1ApplyDamage(250, 2000, 10.0, false);
        CHECK(!r.dies && r.remainingMs == 2500);
        // vector 5: -400 < -300 -> no downed state
        t1.hpAfterKillingHit = -400;
        CHECK(!Type1Predicate(t1));
        t1.hpAfterKillingHit = -50; t1.burning = true;
        CHECK(!Type1Predicate(t1));
        t1.burning = false; t1.attackerIsLiveBruteWithBit7 = true;
        CHECK(!Type1Predicate(t1));
    }

    // --- Sec21.5 / Sec22.11 scripted order ticket and move arrival ---
    {
        // TV (a): point nav object at (10,0,0), human at (8.7,0,0): squared 1.69 < 2.0 completes; 8.5 does not
        CHECK(ScriptedMoveArrived(8.7 - 10.0, 0, 0, 0, 0, 0, 0, 0, 0));
        CHECK(!ScriptedMoveArrived(8.5 - 10.0, 0, 0, 0, 0, 0, 0, 0, 0));
        // TV (b): box local x 0..4, y 0..2, z 0..4, local human point (6,1,2): clamp (4,1,2) -> 4.0 not arrived;
        // (5,1,2): 1.0 arrived
        CHECK(!ScriptedMoveArrived(6, 1, 2, 0, 0, 0, 4, 2, 4));
        CHECK(ScriptedMoveArrived(5, 1, 2, 0, 0, 0, 4, 2, 4));
        // a point already inside the box has distance 0
        CHECK(ScriptedMoveArrived(2, 1, 2, 0, 0, 0, 4, 2, 4));

        // ticket: status codes 0 pending, 1 done (consumed on read), 2 none
        ScriptedOrderTicket t;
        CHECK(t.statusCode() == 2);
        CHECK(t.isComplete());                 // no ticket reads as complete
        t.stage(24);
        CHECK(t.statusCode() == 0);
        CHECK(!t.isComplete());
        CHECK(!t.finish(18));                  // a different action finishing completes nothing
        CHECK(t.statusCode() == 0);
        CHECK(t.finish(24));
        CHECK(t.statusCode() == 1);            // done: first read says 1 and consumes
        CHECK(t.statusCode() == 2);            // second read says none
        t.stage(24);
        t.stage(8);                            // a second call frees the earlier ticket
        CHECK(t.stagedAction() == 8 && !t.isComplete());
        t.clear();                             // ai_clear_scripted_action returns control at once
        CHECK(t.isComplete() && t.statusCode() == 2);
        // a refused/replaced mode never completes: the give-up end leaves the ticket pending
        ScriptedOrderTicket g;
        g.stage(24);
        g.giveUp();
        CHECK(!g.isComplete());
    }

    // --- Sec21.2 / Sec21.9 CHOSEN stand-in: nearer of two players, 2000 ms cadence ---
    {
        CHECK(NearerPlayerIndex(10.0 * 10.0, 20.0 * 20.0) == 0);
        CHECK(NearerPlayerIndex(20.0 * 20.0, 10.0 * 10.0) == 1);
        CHECK(NearerPlayerIndex(10.0, 10.0) == 0);  // strictly smaller wins, ties keep the first
        CHECK(!TargetCopyDue(1999, 0));
        CHECK(TargetCopyDue(2000, 0));
    }

    // --- Sec22.2 chase: hand-off, first-ram delay, speed law (TV1-TV3) ---
    {
        CHECK(ChaseHandsOffToOccupants(99.9, false));
        CHECK(!ChaseHandsOffToOccupants(100.0, false));  // comparison is <
        CHECK(ChaseHandsOffToOccupants(5000.0, /*storeGarageState*/ true));
        CHECK(FirstRamDelayLoMs(3) == 9000 && FirstRamDelayHiMs(3) == 10000);
        CHECK(FirstRamDelayLoMs(4) == 6000 && FirstRamDelayHiMs(4) == 7000);
        CHECK(FirstRamDelayLoMs(5) == 3000 && FirstRamDelayHiMs(5) == 4000);
        CHECK(FirstRamDelayLoMs(0) == 1000 && FirstRamDelayHiMs(0) == 1000);
        CHECK(FirstRamDelayLoMs(2) == 1000 && FirstRamDelayHiMs(7) == 1000);
        CHECK(ChaseClassFromTeam(1, 0x2) == 0x2);  // teams 1-3: low byte of the dword at 0x014a4f70 + (team-1)*0x4c
        CHECK(ChaseClassFromTeam(5, 0x2) == 0 && ChaseClassFromTeam(6, 0x2) == 0);
        CHECK(ChaseClassFromTeam(4, 0x2) == 3 && ChaseClassFromTeam(0, 0x2) == 3 && ChaseClassFromTeam(7, 0x2) == 3);
    }

    // --- Sec22.3 drive-to arrival (TV4) and waypoint directive (TV5) ---
    {
        CHECK(PathfindArrived(2.9, false, 0.0, 8.0));
        CHECK(!PathfindArrived(3.0, false, 0.0, 8.0));   // 3.0 does not complete
        // stopAtGoal: braking starts when d < v^2/a_b + r = 100/8 + 3 = 15.5; completes when speed < 1.0
        CHECK(PathfindBrakingStarts(15.4, 10.0, 8.0));
        CHECK(!PathfindBrakingStarts(15.6, 10.0, 8.0));
        CHECK(PathfindArrived(2.0, true, 0.99, 8.0));
        CHECK(!PathfindArrived(2.0, true, 1.0, 8.0));
        // waypoint: node A passes when (A - p) . (B - A) <= 0; last node: squared X/Z <= 16 (4 m), 4.0 stop-at-end
        CHECK(WaypointNodePassed(/*A-p*/ -1, 0, 0, /*B-A*/ 5, 0, 0));
        CHECK(!WaypointNodePassed(1, 0, 0, 5, 0, 0));
        CHECK(WaypointLastNodeArrived(16.0, false, false));
        CHECK(!WaypointLastNodeArrived(16.1, false, false));
        CHECK(WaypointLastNodeArrived(4.0, true, false));
        CHECK(!WaypointLastNodeArrived(4.1, true, false));
        CHECK(WaypointLastNodeArrived(2500.0, false, true));  // mode 2 overrides to 2500
        CHECK(WaypointFirstNodeSkipped(3.9));   // car within 2.0 m (squared 4.0) of node 0 skips it
        CHECK(!WaypointFirstNodeSkipped(4.1));
    }

    if (g_fail == 0) std::printf("combat_ai tests passed\n");
    return g_fail == 0 ? 0 : 1;
}
