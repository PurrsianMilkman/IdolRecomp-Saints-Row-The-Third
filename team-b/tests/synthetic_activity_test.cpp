// Synthetic tests for sr3activity. Expected values come from the TEXT of
// spec-activity-runtime.md sections 6.1 and 6.2 (formulas and shipped values) and
// spec-tables-progression.md B (respect award), never from this project's own source.
//
// Style follows tests/synthetic_tables_progression_test.cpp.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3activity/activity.h"
#include "sr3activity/fraud.h"
#include "sr3activity/harness.h"
#include "sr3activity/heli.h"
#include "sr3activity/genki.h"
#include "sr3activity/trafficking.h"
#include "sr3activity/escort.h"
#include "sr3activity/mayhem.h"
#include "sr3activity/snatch.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"        \
                      << __LINE__ << "\n";                                 \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                               \
    do {                                                                    \
        ++g_checks;                                                        \
        const double va_ = (a), vb_ = (b);                                 \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                            \
            std::cerr << "CHECK_NEAR FAILED: " #a " = " << va_ << ", " #b  \
                      << " = " << vb_ << " at " __FILE__ ":" << __LINE__  \
                      << "\n";                                             \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

using namespace sr3activity;

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

// _a_if_dt_01 as spec 6.1/6.2 give it: Goal 80000, Duration 210, Max_Spots 2,
// Crazy_Hit_Chance .2, Cash_Award 2000, Props_Award 200, Reward Activity_Unlock_IF.
const char* kFraudDt01 =
    "<root><Table><Activity>"
    "<Name>_a_if_dt_01</Name><Explored_at_start>False</Explored_at_start>"
    "<Start_Location>Enabled</Start_Location><Cleanup_function>fraud_cleanup</Cleanup_function>"
    "<Levels><Level><Goal>80000</Goal><Duration>210</Duration><Max_Spots>2</Max_Spots>"
    "<Crazy_Hit_Chance>.2</Crazy_Hit_Chance><Cash_Award>2000</Cash_Award><Props_Award>200</Props_Award>"
    "<Reward>Activity_Unlock_IF</Reward></Level></Levels>"
    "<Cash_Award>99999</Cash_Award>"
    "</Activity></Table></root>";

FraudParams TestParams() {
    FraudParams p;
    p.crushDamagePerSpeed = 1.0;
    p.scrapeDamagePerSpeed = 0.5;
    p.moneyConversion = 10.0;
    p.damageMultiplier = 1.0;
    p.minMoney = 5.0;
    p.massMinMass = 1000.0;
    p.massMaxMass = 3000.0;
    p.massMinMultiplier = 1.0;
    p.massMaxMultiplier = 3.0;
    p.massMinSpeed = 0.5;
    // Shipped Windshield block: $500 + 225 per extra, capped at 5000 (Cash_Instead).
    p.windshield = {true, true, {{1, 500, 5000, 225}}};
    // Shipped Vehicle block: from 2 vehicles, 1 + 0.5 per extra (a multiplier).
    p.vehicle = {true, false, {{2, 1, 0, 0.5}}};
    p.luxuryCars = {"sp_limo", "car_lux"};
    // Shipped Adrenaline_Grid: 1000/10000/20000 -> 0.05/0.075/0.10.
    p.adrenalineGrid = {{1000, 0.05}, {10000, 0.075}, {20000, 0.10}};
    return p;
}

// ===========================================================================
void testCategoryValue() {
    FraudCategory c{true, true, {{1, 500, 5000, 225}}};
    CHECK(FraudCategoryValue(c, 0.5) == 0.0);
    CHECK(FraudCategoryValue(c, 1) == 500.0);
    CHECK(FraudCategoryValue(c, 3) == 950.0);
    CHECK(FraudCategoryValue(c, 100) == 5000.0);  // capped
    FraudCategory nocap{true, false, {{2, 1, 0, 0.5}}};
    CHECK(FraudCategoryValue(nocap, 6) == 3.0);    // 1 + 4 x 0.5, no cap
    CHECK(FraudCategoryValue(nocap, 1) == 0.0);
    // Several tiers: the maximum wins.
    FraudCategory tiers{true, true, {{1, 0, 500, 250}, {3, 500, 1500, 500}, {5, 1500, 5000, 1000}}};
    CHECK(FraudCategoryValue(tiers, 2) == 250.0);
    CHECK(FraudCategoryValue(tiers, 4) == 1000.0);
    CHECK(FraudCategoryValue(tiers, 6) == 2500.0);
    CHECK(FraudCategoryValue(FraudCategory{}, 10) == 0.0);  // no *_Element block: 0
}

void testInstanceReader() {
    const auto a = ReadActivityInstance(sr3xtbl::ParseDocument(kFraudDt01));
    CHECK(a.has_value());
    CHECK(a->name == "_a_if_dt_01");
    CHECK(a->startLocationEnabled);
    CHECK(a->cleanupFunction && *a->cleanupFunction == "fraud_cleanup");
    CHECK(a->levels.size() == 1);
    const ActivityLevel& l = a->levels[0];
    CHECK(l.goal && *l.goal == 80000.0);
    CHECK(l.duration && *l.duration == 210.0);
    CHECK(l.maxSpots && *l.maxSpots == 2.0);
    CHECK_NEAR(*l.crazyHitChance, 0.2, 1e-6);
    CHECK(l.cashAward == 2000);   // the top-level Cash_Award sibling is not read
    CHECK(l.propsAward == 200);
    CHECK(l.reward && *l.reward == "Activity_Unlock_IF");
    CHECK(!l.reward2);
    CHECK(TypeFromInstanceName(a->name) == ActivityType::InsuranceFraud);
    CHECK(TypeFromInstanceName("_A_SN_NW_01") == ActivityType::Snatch);
    CHECK(TypeFromInstanceName("dlc1_a_es_02") == ActivityType::Escort);
    CHECK(TypeFromInstanceName("m01") == ActivityType::Unknown);

    // At most 6 Level rows; "none" rewards drop out.
    std::string many = "<root><Table><Activity><Name>x</Name><Levels>";
    for (int i = 0; i < 8; ++i) many += "<Level><Cash_Award>1</Cash_Award><Reward>none</Reward></Level>";
    many += "</Levels></Activity></Table></root>";
    const auto m = ReadActivityInstance(sr3xtbl::ParseDocument(many));
    CHECK(m->levels.size() == 6);
    CHECK(!m->levels[0].reward);
    CHECK(!ReadActivityInstance(sr3xtbl::ParseDocument("<root><Table><Other/></Table></root>")).has_value());
}

void testRewardGrant() {
    const ActivityInstance inst = *ReadActivityInstance(sr3xtbl::ParseDocument(kFraudDt01));
    const sr3progression::RespectTable table = sr3progression::RespectTable::ShippedCosts();
    sr3progression::PlayerRespect player;
    sr3progression::AppliedUnlocks applied;
    ActivitySaveRecord save;
    ActivityTypeInfo info;
    info.completionHintId = 0x77;
    GrantResult g = GrantActivityRewards(inst, save, info, 1.25f, player, table, applied);
    CHECK(g.levels.size() == 1);
    CHECK(g.levels[0].respectFirst.granted == 200);
    CHECK(g.levels[0].respectBonus.granted == 50);  // round(200 x (1.25 - 1))
    CHECK(player.total == 250);
    CHECK(g.levels[0].cashRaw == 200000);           // $2,000
    CHECK(g.levels[0].unlocks.size() == 1 && g.levels[0].unlocks[0] == "Activity_Unlock_IF");
    CHECK(applied.count("Activity_Unlock_IF") == 1);
    CHECK(save.doneMask == 1);
    CHECK(g.completionCountIncrement == 1);
    CHECK(g.hints == std::vector<int>({kHintCash, kHintRespect, kHintCompleteA, kHintCompleteB, 0x77}));
    // A replay pays nothing.
    g = GrantActivityRewards(inst, save, info, 1.25f, player, table, applied);
    CHECK(g.levels.empty() && g.hints.empty() && g.completionCountIncrement == 0);
    CHECK(player.total == 250);
    // Multiplier 1.0: the bonus call is 0; suppressed completion hints.
    ActivitySaveRecord fresh;
    sr3progression::PlayerRespect p2;
    ActivityTypeInfo quiet;
    quiet.suppressCompletionHints = true;
    g = GrantActivityRewards(inst, fresh, quiet, 1.0f, p2, table, applied);
    CHECK(g.levels[0].respectBonus.granted == 0 && p2.total == 200);
    CHECK(g.hints == std::vector<int>({kHintCash, kHintRespect}));
}

void testImpacts() {
    ActivityLevel lv;
    lv.goal = 80000;
    lv.duration = 210;
    lv.maxSpots = 2;
    FraudRun run(TestParams(), lv);
    FraudScoringContext inArea{true, -1};
    run.ragdollBegin();
    CHECK(run.hitArmed());
    // dt 1/60 -> t = 1. crush = |4| x 1 x 1 = 4 (a 0 velocity component: no mass
    // multiplier); scrape = 25 x 0.5 = 12.5; raw = 165; head x5 -> 165 + 825.
    FraudImpact head;
    head.relativeVelocity = {3, 0, 4};
    head.normalSpeed = 4;
    head.part = BodyPart::Head;
    CHECK_NEAR(run.impact(head, inArea), 990.0, 1e-9);
    // Mass multiplier: a 2000 kg body, every component fast -> lerp(1, 3, 0.5) = 2.
    FraudImpact heavy;
    heavy.otherMass = 2000;
    heavy.relativeVelocity = {1, 1, 1};
    heavy.normalSpeed = 3;
    heavy.part = BodyPart::Chest;
    // crush = 3 x 2 x 1 = 6; scrape = 3 x 0.5 = 1.5; raw = 75; chest x4 -> 75 + 300.
    CHECK_NEAR(run.impact(heavy, inArea), 375.0, 1e-9);
    // Below Damage_Multiplier x Min_Money: ignored.
    FraudImpact tiny;
    tiny.normalSpeed = 0.2;
    CHECK(run.impact(tiny, inArea) == 0.0);
    // Outside the scoring area: no money.
    FraudImpact out = head;
    CHECK(run.impact(out, FraudScoringContext{false, -1}) == 0.0);
    CHECK_NEAR(run.hitBase(), 1365.0, 1e-9);
    // Half the frame time halves the damage.
    FraudRun r2(TestParams(), lv);
    r2.ragdollBegin();
    FraudImpact halfDt = head;
    halfDt.dt = 1.0 / 120.0;
    halfDt.part = BodyPart::Other;  // x1
    CHECK_NEAR(r2.impact(halfDt, inArea), 82.5 + std::round(82.5), 1e-9);
}

void testHitValueAndBank() {
    ActivityLevel lv;
    lv.goal = 80000;
    lv.duration = 210;
    lv.maxSpots = 2;
    FraudRun run(TestParams(), lv);
    FraudScoringContext spot0{true, 0};
    run.ragdollBegin();
    FraudImpact a;
    a.relativeVelocity = {3, 0, 4};
    a.normalSpeed = 4;
    a.part = BodyPart::Head;
    a.vehicleId = 1;
    a.vehicleName = "SP_LIMO";  // a luxury car
    run.impact(a, spot0);
    FraudImpact b = a;
    b.vehicleId = 2;
    b.vehicleName = "car_plain";
    run.impact(b, spot0);
    // base 1980; 2 vehicles -> Vehicle multiplier 1.0; luxury slot +2 on the base 1.0;
    // windshield 1 -> $500 cash instead.
    FraudHitInputs in;
    in.windshield = 1;
    CHECK_NEAR(run.hitValue(in), (1980.0 + 500.0) * (1.0 + 3.0), 1e-9);
    const auto bank = run.ragdollEnd(in, spot0);
    CHECK(bank.has_value());
    CHECK_NEAR(bank->hitValue, 9920.0, 1e-9);
    CHECK_NEAR(run.score(), 9920.0, 1e-9);
    CHECK_NEAR(run.spotTotals()[0], 9920.0, 1e-9);
    CHECK(!bank->spotConsumed);
    // Adrenaline: 9920 between 1000 and 10000 -> 0.05 + (8920/9000) x 0.025, x3 in a spot.
    CHECK_NEAR(run.adrenalineMeter(), 3.0 * (0.05 + 8920.0 / 9000.0 * 0.025), 1e-9);
    // Cooldown: a new ragdoll within 3 s does not arm a hit.
    run.ragdollBegin();
    CHECK(!run.hitArmed());
    CHECK(!run.ragdollEnd(in, spot0).has_value());
    run.tick(3.1, spot0, false, false, false);
    run.ragdollBegin();
    CHECK(run.hitArmed());
    // On fire: 50 $/s into the armed hit.
    run.tick(2.0, spot0, true, false, true);
    CHECK_NEAR(run.hitBase(), 100.0, 1e-9);

    // A spot is consumed once its total passes Goal x Ring_Money_Percent.
    FraudParams p = TestParams();
    ActivityLevel small;
    small.goal = 1000;
    small.duration = 100;
    small.maxSpots = 1;
    FraudRun r(p, small);
    r.ragdollBegin();
    r.impact(a, FraudScoringContext{true, 0});
    const auto bk = r.ragdollEnd(FraudHitInputs{}, FraudScoringContext{true, 0});
    CHECK(bk->spotConsumed);

    // Adrenaline mode at a full meter.
    FraudParams hot = TestParams();
    hot.adrenalineGrid = {{0, 0.6}};
    FraudRun h(hot, lv);
    for (int i = 0; i < 2; ++i) {
        h.ragdollBegin();
        h.impact(a, FraudScoringContext{true, -1});
        const auto res = h.ragdollEnd(FraudHitInputs{}, FraudScoringContext{true, -1});
        if (i == 1) CHECK(res->adrenalineReady);
        h.tick(3.5, FraudScoringContext{true, -1}, false, false, false);
    }
    CHECK(h.adrenalineMode() && h.adrenalineMeter() == 1.0);
    // Adrenaline mode lets scoring happen outside the area.
    h.ragdollBegin();
    CHECK(h.impact(a, FraudScoringContext{false, -1}) > 0.0);
}

void testWinFail() {
    ActivityLevel lv;
    lv.goal = 900;  // one head hit banks $990
    lv.duration = 10;
    lv.maxSpots = 0;
    FraudRun win(TestParams(), lv);
    win.ragdollBegin();
    FraudImpact a;
    a.relativeVelocity = {3, 0, 4};
    a.normalSpeed = 4;
    a.part = BodyPart::Head;
    win.impact(a, FraudScoringContext{true, -1});
    win.ragdollEnd(FraudHitInputs{}, FraudScoringContext{true, -1});
    FraudTickResult t = win.tick(0.1, FraudScoringContext{true, -1}, false, false, false);
    CHECK(t.showWinHint && win.state() == FraudState::Winning);
    t = win.tick(0.5, FraudScoringContext{true, -1}, false, false, false);
    CHECK(!t.justSucceeded);
    t = win.tick(0.6, FraudScoringContext{true, -1}, false, false, false);
    CHECK(t.justSucceeded && win.state() == FraudState::Succeeded);

    // Timer expiry fails, but not mid-ragdoll or in adrenaline mode.
    FraudRun lose(TestParams(), lv);
    t = lose.tick(9.0, FraudScoringContext{false, -1}, false, false, false);
    CHECK(t.goScoringArea && !t.justFailed);
    t = lose.tick(2.0, FraudScoringContext{true, -1}, true, false, false);
    CHECK(!t.justFailed);  // the player is mid-ragdoll
    t = lose.tick(0.1, FraudScoringContext{true, -1}, false, true, false);
    CHECK(!t.justFailed);  // the partner is mid-ragdoll
    t = lose.tick(0.1, FraudScoringContext{true, -1}, false, false, false);
    CHECK(t.justFailed && lose.state() == FraudState::Failed);

    // Death fails; co-op goal defaults to 2x.
    FraudRun dead(TestParams(), lv);
    dead.playerDied();
    CHECK(dead.state() == FraudState::Failed);
    FraudRun coop(TestParams(), lv, true);
    CHECK(coop.goal() == 1800.0);
}

void testParamsFromTables() {
    const char* xml =
        "<root><Table><Fraud_Values>"
        "<Mass_Min_Multiplier>1.5</Mass_Min_Multiplier><Mass_Max_Multiplier>4</Mass_Max_Multiplier>"
        "<Mass_Min_Mass>500</Mass_Min_Mass><Mass_Max_Mass>5000</Mass_Max_Mass><Mass_Min_Speed>2</Mass_Min_Speed>"
        "<Crush_Damage_Per_Speed>0.3</Crush_Damage_Per_Speed><Scrape_Damage_Per_Speed>0.02</Scrape_Damage_Per_Speed>"
        "<Adrenaline_InArea_Mult>3</Adrenaline_InArea_Mult><Adrenaline_Dollar_Multiplier>1</Adrenaline_Dollar_Multiplier>"
        "<Airtime_Multiplier_Element><Cash_Instead/><Airtime_Multiplier/></Airtime_Multiplier_Element>"
        "<Vehicle_Multiplier_Element><Vehicle_Multiplier/></Vehicle_Multiplier_Element>"
        "<Surfing_Multiplier_Element><Cash_Instead>False</Cash_Instead></Surfing_Multiplier_Element>"
        "</Fraud_Values></Table></root>";
    sr3tables_diversions::FraudGlobals g;
    g.windshield.tieredElementPresent = true;
    g.windshield.cashInstead = {true, true};
    sr3tables_diversions::FraudMultiplierTier t;
    t.minimum = {1, true};
    t.multiplierMin = {500, true};
    t.multiplierMax = {5000, true};
    t.perMultiplier = {225, true};
    g.windshield.tiers.push_back(t);
    g.witness.flatPresent = true;  // flat only: not read
    // As the shipped file: an empty <Cash_Instead/> that the text-valued parsed flag reads as false.
    g.airtime.tieredElementPresent = true;
    g.vehicle.tieredElementPresent = true;
    g.surfing.tieredElementPresent = true;
    g.surfing.cashInstead = {true, true};
    const FraudParams p = FraudParamsFromTables(g, sr3xtbl::ParseDocument(xml));
    CHECK(p.windshield.present && p.windshield.cashInstead && p.windshield.tiers.size() == 1);
    CHECK(FraudCategoryValue(p.windshield, 3) == 950.0);
    CHECK(p.airtime.present && p.airtime.cashInstead);  // empty flag element = set
    CHECK(p.vehicle.present && !p.vehicle.cashInstead);  // no flag element
    CHECK(!p.surfing.cashInstead);                       // an explicit False
    CHECK_NEAR(p.massMinMultiplier, 1.5, 1e-6);
    CHECK_NEAR(p.massMaxMass, 5000, 1e-6);
    CHECK_NEAR(p.crushDamagePerSpeed, 0.3, 1e-6);
    CHECK_NEAR(p.scrapeDamagePerSpeed, 0.02, 1e-6);
    CHECK(p.adrenalineInAreaMult == 3.0 && p.adrenalineDollarMultiplier == 1.0);
}

// _a_sn_ne_01 as spec 6.1/6.4 give it: Ho_Win_Amount 4, Duration 320, Coop_Duration 350,
// Cash_Award 2000, Props_Award 200; drop-off radii 6 / 8.
const char* kSnatchNe01 =
    "<root><Table><Activity><Name>_a_sn_ne_01</Name>"
    "<Dropoff><On_Foot_Dropoff_Dist>6</On_Foot_Dropoff_Dist><Vehicle_Dropoff_Dist>8</Vehicle_Dropoff_Dist></Dropoff>"
    "<Levels><Level><Snatch_Info><Ho_Win_Amount>4</Ho_Win_Amount><Duration>320</Duration><Coop_Duration>350</Coop_Duration>"
    "<In_Range_Pickup_Dist>60</In_Range_Pickup_Dist></Snatch_Info>"
    "<Cash_Award>2000</Cash_Award><Props_Award>200</Props_Award><Reward>Activity_Unlock_SN</Reward></Level></Levels>"
    "</Activity></Table></root>";

void testSnatch() {
    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(kSnatchNe01);
    const ActivityInstance inst = *ReadActivityInstance(doc);
    CHECK(TypeFromInstanceName(inst.name) == ActivityType::Snatch);
    const ActivityLevel& lv = inst.levels[0];
    CHECK(lv.hoWinAmount && *lv.hoWinAmount == 4.0);
    const SnatchParams sp = ReadSnatchParams(doc);
    CHECK(sp.onFootDropoffDist == 6.0 && sp.vehicleDropoffDist == 8.0 && sp.pickupInRangeDist == 60.0);
    SnatchRun run(sp, lv);
    CHECK(run.target() == 4 && run.timeLeft() == 320.0);
    CHECK(run.pickupInRange(60.0) && !run.pickupInRange(60.5));
    CHECK(run.atDropoff(5.9, false) && !run.atDropoff(6.5, false) && run.atDropoff(7.9, true));
    run.deliver(3);
    CHECK(!run.tick(100.0).justSucceeded);
    run.deliver();
    CHECK(run.tick(0.1).justSucceeded && run.state() == SnatchState::Succeeded);
    run.deliver();  // ignored once finished
    CHECK(run.delivered() == 4);

    SnatchRun late(sp, lv);
    late.deliver(3);
    CHECK(!late.tick(319.0).justFailed);
    CHECK(late.tick(1.0).justFailed && late.state() == SnatchState::Failed);
    // The delivery that reaches the target in the last frame still wins.
    SnatchRun close(sp, lv);
    close.tick(319.9);
    close.deliver(4);
    CHECK(close.tick(0.2).justSucceeded);
    SnatchRun coop(sp, lv, true);
    CHECK(coop.timeLeft() == 350.0);
    SnatchRun dead(sp, lv);
    dead.playerDied();
    CHECK(dead.state() == SnatchState::Failed);
    // Defaults when the elements are absent.
    CHECK(ReadSnatchParams(sr3xtbl::ParseDocument("<root><Table><Activity/></Table></root>")).vehicleDropoffDist == 8.0);

    // Notoriety bump: gang +1, police 0-1, gang 0-3.
    SnatchNotoriety n = ApplySnatchNotorietyBump({0.0, 0.0});
    CHECK(n.gang == 1.0 && n.police == 0.0);
    n = ApplySnatchNotorietyBump({2.5, 2.7});
    CHECK(n.police == 1.0 && n.gang == 3.0);

    // The 6.1 reward grant is the same frame as Fraud.
    const sr3progression::RespectTable table = sr3progression::RespectTable::ShippedCosts();
    sr3progression::PlayerRespect pr;
    sr3progression::AppliedUnlocks applied;
    ActivitySaveRecord save;
    const GrantResult g = GrantActivityRewards(inst, save, ActivityTypeInfo{}, 1.0f, pr, table, applied);
    CHECK(g.levels.size() == 1 && g.levels[0].cashRaw == 200000 && pr.total == 200);
    CHECK(applied.count("Activity_Unlock_SN") == 1);
}

// _a_mh_nw_01 and _a_tm_dt_01 as shipped (element layout and values from the real files;
// values also in spec 6.6/6.7).
const char* kMayhemNw01 =
    "<root><Table><Activity><Name>_A_MH_NW_01</Name><Levels><Level>"
    "<Cash_Award>2000</Cash_Award><Props_Award>200</Props_Award><Duration>300</Duration>"
    "<Destruction_Goal>90000</Destruction_Goal><Destruction_Goal_Coop>200000</Destruction_Goal_Coop>"
    "<Combo_Info><Human_Combo_Multiplier>0.01</Human_Combo_Multiplier>"
    "<Vehicle_Combo_Multiplier>0.04</Vehicle_Combo_Multiplier><Mover_Combo_Multiplier>0.02</Mover_Combo_Multiplier>"
    "<Completion_Mult>4.5</Completion_Mult><Combo_Decay_Cutoffs>"
    "<Combo_Decay_Cutoff><Destruction_Value>4000</Destruction_Value><Decay_Mult>2.0</Decay_Mult></Combo_Decay_Cutoff>"
    "<Combo_Decay_Cutoff><Destruction_Value>8000</Destruction_Value><Decay_Mult>3.0</Decay_Mult></Combo_Decay_Cutoff>"
    "<Combo_Decay_Cutoff><Destruction_Value>16000</Destruction_Value><Decay_Mult>4.0</Decay_Mult></Combo_Decay_Cutoff>"
    "<Combo_Decay_Cutoff><Destruction_Value>25000</Destruction_Value><Decay_Mult>5.0</Decay_Mult></Combo_Decay_Cutoff>"
    "<Combo_Decay_Cutoff><Destruction_Value>35000</Destruction_Value><Decay_Mult>6.0</Decay_Mult></Combo_Decay_Cutoff>"
    "<Combo_Decay_Cutoff><Destruction_Value>50000</Destruction_Value><Decay_Mult>7.0</Decay_Mult></Combo_Decay_Cutoff>"
    "</Combo_Decay_Cutoffs><Combo_Max_Multiplier>2.0</Combo_Max_Multiplier></Combo_Info>"
    "<Reward>Activity_Unlock_MH</Reward></Level></Levels></Activity></Table></root>";

const char* kTankDt01 =
    "<root><Table><Activity><Name>_A_TM_DT_01</Name><Levels><Level><Tank>sp_tank01</Tank>"
    "<Duration>150</Duration><Destruction_Goal>125000</Destruction_Goal>"
    "<Destruction_Goal_Coop>225000</Destruction_Goal_Coop>"
    "<Activity_Center><Center>dt_01_centerpoint</Center><Radius>700</Radius></Activity_Center>"
    "<Combo_Info><Human_Combo_Multiplier>0.01</Human_Combo_Multiplier>"
    "<Vehicle_Combo_Multiplier>0.05</Vehicle_Combo_Multiplier><Mover_Combo_Multiplier>0.02</Mover_Combo_Multiplier>"
    "<Combo_Decay_Cutoffs><Combo_Decay_Cutoff><Destruction_Value>5000</Destruction_Value><Decay_Mult>2.0</Decay_Mult>"
    "</Combo_Decay_Cutoff></Combo_Decay_Cutoffs><Completion_Mult>0.0</Completion_Mult></Combo_Info>"
    "</Level></Levels></Activity></Table></root>";

void testMayhemReader() {
    const auto m = ReadMayhemParams(sr3xtbl::ParseDocument(kMayhemNw01), MayhemKind::Mayhem);
    CHECK(m.has_value());
    CHECK(m->goal == 90000.0 && m->goalCoop == 200000.0 && m->durationS == 300.0);
    CHECK_NEAR(m->humanMult, 0.01, 1e-6);
    CHECK_NEAR(m->vehicleMult, 0.04, 1e-6);
    CHECK_NEAR(m->moverMult, 0.02, 1e-6);
    CHECK_NEAR(m->completionMult, 4.5, 1e-6);
    CHECK(m->comboMaxMult == 2.0);
    CHECK(m->cutoffs.size() == 6 && m->cutoffs[5].destructionValue == 50000.0 && m->cutoffs[5].decayMult == 7.0);
    CHECK(!m->hasActivityCenter);
    const auto t = ReadMayhemParams(sr3xtbl::ParseDocument(kTankDt01), MayhemKind::TankMayhem);
    CHECK(t->hasActivityCenter && t->radius == 700.0 && t->centerName == "dt_01_centerpoint");
    CHECK(t->comboMaxMult == 0.0 && t->completionMult == 0.0);
    CHECK(!ReadMayhemParams(sr3xtbl::ParseDocument(kTankDt01), MayhemKind::TankMayhem, 1).has_value());
    CHECK(MayhemMoverValue(4.0) == 500.0);
    CHECK(MayhemMoverValue(400.0) == 1500.0);  // capped
    CHECK(TypeFromInstanceName("_A_TM_DT_01") == ActivityType::TankMayhem);
}

void testMayhemCombo() {
    const MayhemParams p = *ReadMayhemParams(sr3xtbl::ParseDocument(kMayhemNw01), MayhemKind::Mayhem);
    MayhemRun run(p);
    // Human kill: mult 1.0 + 0.01; add round(100 x 1.01) = 101; the timer starts at 15000.
    CHECK(run.event({MayhemClass::Human}) == 101.0);
    CHECK(run.banked() == 101.0 && run.comboCash() == 101.0 && run.comboCount() == 1);
    CHECK(run.comboRunning() && run.comboRemainingMs() == 15000.0);
    // Vehicle worth 2000: mult 1.05 -> 2100; the timer extends by 2500.
    MayhemEvent car{MayhemClass::Vehicle, 2000.0};
    CHECK(run.event(car) == 2100.0);
    CHECK(run.comboRemainingMs() == 17500.0);
    // Next car: mult 1.09 -> 2180; combo $ 4381 passes the 4000 cutoff: speed 2, the window
    // and remaining time halve (the extension applies first: 17500 + 2500).
    CHECK(run.event(car) == 2180.0);
    CHECK(run.comboSpeed() == 2.0);
    CHECK(run.comboWindowMs() == 15000.0);
    CHECK(run.comboRemainingMs() == 10000.0);
    // Extensions are scaled by the speed and capped at the window.
    run.event({MayhemClass::Mover, MayhemMoverValue(4.0)});
    CHECK(run.comboRemainingMs() == 11250.0);  // + round(2500 / 2)
    // The multiplier caps at Combo_Max_Multiplier.
    for (int i = 0; i < 40; ++i) run.event(car);
    CHECK(run.comboMultiplier() == 2.0);
    CHECK(run.comboSpeed() == 7.0);  // past every cutoff
    // Combo end: nothing extra banked, everything resets.
    const double banked = run.banked();
    MayhemTickResult r;
    for (int i = 0; i < 100 && !r.comboEnded; ++i) r = run.tick(0.5);
    CHECK(r.comboEnded && r.comboTotal > 0.0);
    CHECK(run.banked() == banked);
    CHECK(run.comboMultiplier() == 1.0 && run.comboCash() == 0.0 && run.comboCount() == 0);
    CHECK(run.comboWindowMs() == 30000.0);
    // Gates: outside the assigned hoods, non-player kills, HVT value.
    CHECK(run.event({MayhemClass::Human, std::nullopt, std::nullopt, false}) == 0.0);
    CHECK(run.event({MayhemClass::Human, std::nullopt, std::nullopt, true, true}) == 0.0);
    MayhemRun fresh(p);
    CHECK(fresh.event({MayhemClass::Vehicle, 900.0, 5000.0}) == std::round(5000.0 * 1.04));  // HVT Value replaces the base
}

void testMayhemWinFail() {
    MayhemParams p = *ReadMayhemParams(sr3xtbl::ParseDocument(kMayhemNw01), MayhemKind::Mayhem);
    p.goal = 3000;
    MayhemRun run(p);
    run.event({MayhemClass::Vehicle, 2000.0});
    run.event({MayhemClass::Vehicle, 2000.0});  // banked 2080 + 2160 >= 3000
    MayhemTickResult r = run.tick(0.1);
    CHECK(run.state() == MayhemState::WrapUp);
    // The goal-meeting event also crossed the 4000 cutoff: Decay_Mult 2 + Completion_Mult 4.5;
    // the win then raises the live speed by Completion_Mult again (both steps as 6.6 states them).
    CHECK_NEAR(run.comboSpeed(), 2.0 + 4.5 + 4.5, 1e-9);
    CHECK(!r.justSucceeded);
    for (int i = 0; i < 100 && !r.justSucceeded; ++i) r = run.tick(0.5);
    CHECK(r.justSucceeded && run.state() == MayhemState::Succeeded);
    // Wrap-up waits for the HVT groups.
    MayhemRun hvt(p);
    hvt.event({MayhemClass::Vehicle, 4000.0});
    for (int i = 0; i < 100; ++i) hvt.tick(0.5, 0.0, false);
    CHECK(hvt.state() == MayhemState::WrapUp);
    hvt.tick(0.1, 0.0, true);
    CHECK(hvt.state() == MayhemState::Succeeded);
    // Timer expiry fails; death fails; co-op goal.
    MayhemRun late(*ReadMayhemParams(sr3xtbl::ParseDocument(kMayhemNw01), MayhemKind::Mayhem));
    CHECK(!late.tick(299.0).justFailed);
    CHECK(late.tick(1.0).justFailed && late.state() == MayhemState::Failed);
    MayhemRun dead(p);
    dead.playerDied();
    CHECK(dead.state() == MayhemState::Failed);
    MayhemRun coop(*ReadMayhemParams(sr3xtbl::ParseDocument(kMayhemNw01), MayhemKind::Mayhem), {}, true);
    CHECK(coop.goal() == 200000.0);
    // A Mayhem run ignores "tank destroyed".
    MayhemRun m2(p);
    CHECK(!m2.tick(0.1, 0.0, true, true).justFailed);
}

void testTankLeash() {
    const MayhemParams p = *ReadMayhemParams(sr3xtbl::ParseDocument(kTankDt01), MayhemKind::TankMayhem);
    MayhemRun run(p);
    CHECK(!run.leashRunning());
    CHECK(run.event({MayhemClass::Vehicle, 1000.0}) == 1050.0);  // inside: scores (no max cap)
    MayhemTickResult r = run.tick(1.0, 710.0);
    CHECK(run.leashRunning() && r.strayingAway && r.showRadiusRing);
    CHECK(run.event({MayhemClass::Vehicle, 1000.0}) == 0.0);  // no score while the countdown runs
    run.tick(10.0, 800.0);
    CHECK_NEAR(run.leashRemainingS(), 9.0, 1e-9);
    r = run.tick(1.0, 600.0);  // back inside: paused
    CHECK(!run.leashRunning() && !r.strayingAway && r.showRadiusRing);  // ring from 525
    run.tick(1.0, 750.0);       // out again: a fresh 20 s
    CHECK_NEAR(run.leashRemainingS(), 19.0, 1e-9);
    for (int i = 0; i < 18; ++i) CHECK(!run.tick(1.0, 750.0).justFailed);
    CHECK(run.tick(1.0, 750.0).justFailed && run.state() == MayhemState::Failed);
    MayhemRun tank(p);
    CHECK(tank.tick(0.1, 0.0, true, true).justFailed);  // the tank destroyed
    // No max multiplier: the Tank combo multiplier is uncapped.
    MayhemRun big(p);
    for (int i = 0; i < 40; ++i) big.event({MayhemClass::Vehicle, 100.0});
    CHECK_NEAR(big.comboMultiplier(), 1.0 + 40 * 0.05, 1e-6);
}

// _a_es_ne_01 as shipped (layout from the real file; values also in spec 6.8):
// Satisfaction_Time 60, Footage_Limit 50, Coop_Footage_Limit 60, Num_Complications 3,
// Number_of_combos_to_advance 3.
const char* kEscortNe01 =
    "<root><Table><Activity><Name>_A_ES_NE_01</Name><Time_Between_Complication>10</Time_Between_Complication>"
    "<Levels><Level><Satisfaction_Time>60</Satisfaction_Time><Number_of_combos_to_advance>3"
    "</Number_of_combos_to_advance><Coop_Footage_Limit>60</Coop_Footage_Limit><Footage_Limit>50</Footage_Limit>"
    "<Num_Complications>3</Num_Complications><Cash_Award>2000</Cash_Award><Props_Award>200</Props_Award>"
    "</Level></Levels></Activity></Table></root>";

EscortFrame Driving(double dt) {
    EscortFrame f;
    f.dt = dt;
    f.playerDrivingClientVehicle = true;
    f.playerInClientVehicle = true;
    return f;
}

void testEscort() {
    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(kEscortNe01);
    const EscortParams p = *ReadEscortParams(doc, EscortVariant::Base);
    CHECK(p.satisfactionTimeS == 60.0 && p.footageLimitS == 50.0 && p.coopFootageLimitS == 60.0);
    CHECK(p.numComplications == 3 && p.combosToAdvance == 3);
    CHECK(TypeFromInstanceName("_A_ES_NE_01") == ActivityType::Escort);

    // Pure driving time fills satisfaction; then the drop-off.
    EscortRun run(p);
    for (int i = 0; i < 59; ++i) CHECK(!run.tick(Driving(1.0)).satisfactionFull);
    CHECK_NEAR(run.satisfactionMeter(), 59.0 / 60.0, 1e-9);
    EscortTickResult r = run.tick(Driving(1.0));
    CHECK(r.satisfactionFull && run.phase() == EscortPhase::ToDropoff);
    CHECK(run.arriveAtDropoff() && run.phase() == EscortPhase::Succeeded);

    // No satisfaction while not driving, during a complication, an IRS/Spouse within 25 or a
    // HUD lock.
    EscortRun hold(p);
    EscortFrame passenger = Driving(1.0);
    passenger.playerDrivingClientVehicle = false;
    hold.tick(passenger);
    EscortFrame comp = Driving(1.0);
    comp.complicationActive = true;
    hold.tick(comp);
    EscortFrame irs = Driving(1.0);
    irs.irsOrSpouseWithin25 = true;
    hold.tick(irs);
    EscortFrame lock = Driving(1.0);
    lock.hudLock = true;
    hold.tick(lock);
    CHECK(hold.satisfactionRemainingS() == 60.0);

    // Filming: under 25 m with line of sight drains footage, not satisfaction; no line of
    // sight or 25-50 m is only "near" (a complaint, 15 s cooldown).
    EscortRun film(p);
    EscortFrame f = Driving(1.0);
    f.paparazzi = {{20.0, true}};
    r = film.tick(f);
    CHECK(r.filming && r.footageReaction && film.footageRemainingS() == 49.0);
    CHECK(film.satisfactionRemainingS() == 60.0);
    f.dt = 0.2;
    r = film.tick(f);
    CHECK(r.filming && !r.footageReaction);  // 500 ms throttle
    EscortFrame near = Driving(1.0);
    near.paparazzi = {{20.0, false}, {40.0, true}};
    r = film.tick(near);
    CHECK(!r.filming && r.complaint);
    r = film.tick(near);
    CHECK(!r.complaint);  // cooldown
    CHECK_NEAR(film.footageMeter(), 1.0 - 48.8 / 50.0, 1e-9);
    // Footage warning below 5 s, fail below 0.
    EscortFrame burn = Driving(44.0);
    burn.paparazzi = {{10.0, true}};
    r = film.tick(burn);
    CHECK(r.footageWarning && !r.justFailed);
    burn.dt = 5.0;
    r = film.tick(burn);
    CHECK(r.justFailed && r.failReason == EscortFailReason::Footage);
    // The Tiger variant does not fail on footage.
    EscortRun tiger(*ReadEscortParams(doc, EscortVariant::Tiger));
    burn.dt = 60.0;
    CHECK(!tiger.tick(burn).justFailed);
    tiger.tigerSlapFailed(5.0);
    CHECK(tiger.footageRemainingS() == -15.0);

    // Abandon: a 10 s countdown, cleared on re-entry.
    EscortRun away(p);
    EscortFrame out = Driving(1.0);
    out.playerInClientVehicle = false;
    out.playerDrivingClientVehicle = false;
    r = away.tick(out);
    CHECK(r.getBackMessage && r.getBackSecondsLeft == 10.0);
    for (int i = 0; i < 5; ++i) away.tick(out);
    away.tick(Driving(1.0));  // back in: cleared
    r = away.tick(out);
    CHECK(r.getBackSecondsLeft == 10.0);
    for (int i = 0; i < 9; ++i) CHECK(!away.tick(out).justFailed);
    r = away.tick(out);
    CHECK(r.justFailed && r.failReason == EscortFailReason::LeftCar);

    // Client dead, car destroyed, Genki distance, death; co-op footage limit.
    EscortRun dead(p);
    EscortFrame cd = Driving(0.1);
    cd.clientDead = true;
    CHECK(dead.tick(cd).failReason == EscortFailReason::ClientDead);
    EscortRun car(p);
    EscortFrame cv = Driving(0.1);
    cv.clientVehicleDestroyed = true;
    CHECK(car.tick(cv).failReason == EscortFailReason::CarDestroyed);
    EscortRun genki(*ReadEscortParams(doc, EscortVariant::Genki));
    EscortFrame g = Driving(1.0);
    g.genkiSatisfactionDrainS = 2.0;
    g.genkiDistance = 29.0;
    genki.tick(g);
    CHECK(genki.satisfactionRemainingS() == 58.0);  // the minigame's amount, not driving time
    g.genkiDistance = 31.0;
    CHECK(genki.tick(g).failReason == EscortFailReason::GenkiTooFar);
    EscortRun died(p);
    died.playerDied();
    CHECK(died.phase() == EscortPhase::Failed && died.failReason() == EscortFailReason::PlayerDied);
    EscortRun coop(p, true);
    CHECK(coop.footageRemainingS() == 60.0);
}

// _a_dt_dt_01 as shipped (layout from the real file; values in spec 6.3): Deal_Use_Count 2,
// three Deals, complications buyer_flee .3 / buyer_flee_shooting .4, Max_complications 2,
// Vehicle_Health 25000 and Vehicle_Override_Speed_MPH 60 on the Activity row.
const char* kTraffickingDt01 =
    "<root><Table><Activity><Name>_A_DT_DT_01</Name><Cash_Award>2000</Cash_Award><Props_Award>2000</Props_Award>"
    "<Levels><Level><Police_Notoriety>0</Police_Notoriety><Gang_Notoriety>2</Gang_Notoriety>"
    "<Deal_Use_Count>2</Deal_Use_Count><Starting_Nav>start</Starting_Nav><Dealer_Type>npc_bum</Dealer_Type>"
    "<Deals><Deal><Deal_Nav>deal 003</Deal_Nav><Buyer_Spawn>buyer 003</Buyer_Spawn><Buyer_Type>npc_a</Buyer_Type></Deal>"
    "<Deal><Deal_Nav>deal01</Deal_Nav><Buyer_Spawn>buyer01</Buyer_Spawn><Buyer_Type>npc_b</Buyer_Type></Deal>"
    "<Deal><Deal_Nav>deal 002</Deal_Nav><Buyer_Spawn>buyer 002</Buyer_Spawn><Buyer_Type>npc_c</Buyer_Type></Deal></Deals>"
    "<Level_Complications><Level_Complication><Complication_Name>buyer_flee</Complication_Name>"
    "<Complication_Chance>.3</Complication_Chance></Level_Complication><Level_Complication>"
    "<Complication_Name>buyer_flee_shooting</Complication_Name><Complication_Chance>.4</Complication_Chance>"
    "</Level_Complication></Level_Complications><Cash_Award>2000</Cash_Award><Props_Award>200</Props_Award>"
    "<Ending_Nav>end</Ending_Nav><Max_complications>2</Max_complications><Reward>Activity_Unlock_DT</Reward>"
    "</Level></Levels><Dealer_Type>npc_Pierce</Dealer_Type><Vehicle_Type>car_4dr_sports02</Vehicle_Type>"
    "<Vehicle_Health>25000</Vehicle_Health><Vehicle_Override_Speed_MPH>60</Vehicle_Override_Speed_MPH>"
    "</Activity></Table></root>";

void testTrafficking() {
    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(kTraffickingDt01);
    const TraffickingParams p = *ReadTraffickingParams(doc);
    CHECK(p.dealUseCount == 2 && p.maxComplications == 2 && p.deals.size() == 3);
    CHECK(p.deals[0].dealNav == "deal 003" && p.deals[1].buyerSpawn == "buyer01");
    CHECK(p.complications.size() == 2 && p.complications[1].name == "buyer_flee_shooting");
    CHECK_NEAR(p.complications[0].chance, 0.3, 1e-6);
    CHECK(p.endingNav == "end" && p.startingNav == "start");
    CHECK(p.dealerType == "npc_bum");          // the Level's own value first
    CHECK(p.vehicleType == "car_4dr_sports02");  // the Activity row's when the Level has none
    CHECK(p.vehicleHealth == 25000.0 && p.vehicleOverrideSpeedMph == 60.0);
    CHECK(p.gangNotoriety == 2 && p.policeNotoriety == 0);
    // The awards come from Level only (the top-level 2000/2000 siblings are not read).
    const ActivityInstance inst = *ReadActivityInstance(doc);
    CHECK(inst.levels[0].propsAward == 200 && inst.levels[0].cashAward == 2000);
    CHECK(TypeFromInstanceName("_A_DT_DT_01") == ActivityType::Trafficking);

    TraffickingRun run(p);
    CHECK(run.currentDeal() && run.currentDeal()->dealNav == "deal 003");
    CHECK(run.rollComplication({0.5, 0.35}).value_or("") == "buyer_flee_shooting");
    CHECK(!run.rollComplication({0.9, 0.9}).has_value());
    run.completeDeal();
    CHECK(run.currentDeal()->dealNav == "deal01");
    CHECK(run.rollComplication({0.1, 0.1}).value_or("") == "buyer_flee");
    CHECK(!run.rollComplication({0.0, 0.0}).has_value());  // Max_complications reached
    CHECK(run.complicationsApplied() == 2);
    CHECK(!run.reachEnding());
    run.completeDeal();
    CHECK(run.state() == TraffickingState::ToEnding && !run.currentDeal());
    CHECK(run.reachEnding() && run.state() == TraffickingState::Succeeded);
    run.carDestroyed();  // ignored once finished
    CHECK(run.state() == TraffickingState::Succeeded);

    TraffickingRun dealer(p);
    dealer.dealerDied();
    CHECK(dealer.state() == TraffickingState::Failed && dealer.failReason() == TraffickingFail::DealerDied);
    TraffickingRun car(p);
    car.completeDeal();
    car.carDestroyed();
    CHECK(car.failReason() == TraffickingFail::CarDestroyed);
    TraffickingRun me(p);
    me.playerDied();
    CHECK(me.failReason() == TraffickingFail::PlayerDied);
}

// running_man_globals.xtbl and _a_rm_dt_01 as shipped (layout from the real files; values in
// spec 6.9). Two multiplier rows and one decay row are enough for the reader test.
const char* kRunningManGlobals =
    "<root><Table><Running_man_globals><Name>Running_man_globals</Name><Scoring><On_foot><Kill_scores>"
    "<Kill_score><Type>Civilian</Type><Base>50</Base><One_hit>15</One_hit><Human_shield>0</Human_shield>"
    "<Melee>5</Melee><Sword>5</Sword><Headshot>20</Headshot><Nutshot>25</Nutshot><Trap_Kill>30</Trap_Kill></Kill_score>"
    "<Kill_score><Type>Enemy Gang</Type><Base>50</Base><One_hit>15</One_hit><Human_shield>0</Human_shield>"
    "<Melee>5</Melee><Sword>5</Sword><Headshot>20</Headshot><Nutshot>25</Nutshot><Trap_Kill>30</Trap_Kill></Kill_score>"
    "<Kill_score><Type>Hunter</Type><Base>500</Base><One_hit>75</One_hit><Human_shield>24</Human_shield>"
    "<Melee>50</Melee><Sword>15</Sword><Headshot>25</Headshot><Nutshot>25</Nutshot><Trap_Kill>0</Trap_Kill></Kill_score>"
    "</Kill_scores></On_foot><Targets>"
    "<Ethical_Target><Bonus_Time>5</Bonus_Time><Bonus_Cash>50</Bonus_Cash></Ethical_Target>"
    "<Unethical_Target><Cash_Deduction>100</Cash_Deduction><Time_Deduction>10</Time_Deduction></Unethical_Target>"
    "<Money_Target><Bonus_Cash>100</Bonus_Cash></Money_Target><Time_Target><Bonus_Time>25</Bonus_Time></Time_Target>"
    "<Button><Bonus_Cash>100</Bonus_Cash></Button><Health_Target><Bonus_Health>225</Bonus_Health></Health_Target>"
    "</Targets><Combos><Multiplier_table><Multiplier><Threshold>2</Threshold><Factor>1.5</Factor></Multiplier>"
    "<Multiplier><Threshold>12</Threshold><Factor>6.5</Factor></Multiplier></Multiplier_table>"
    "<Decay_rate_table><Decay_rate><Threshold>200</Threshold><Factor>1.1</Factor></Decay_rate></Decay_rate_table>"
    "<Max_combo_ticks>2000</Max_combo_ticks><Base_decay_rate>100</Base_decay_rate></Combos>"
    "<Trap_Evade><Electrical><Bonus_Cash>70</Bonus_Cash><Bonus_Time>0</Bonus_Time></Electrical>"
    "<Fire><Bonus_Cash>50</Bonus_Cash><Bonus_Time>0</Bonus_Time></Fire></Trap_Evade>"
    "</Scoring></Running_man_globals></Table></root>";

const char* kGenkiDt01 =
    "<root><Table><Activity><Name>_A_RM_DT_01</Name><Levels><Level><Cash_Award>1000</Cash_Award>"
    "<Props_Award>200</Props_Award><Time_limit>180</Time_limit><Level_Exit><End_exit>Exit Door</End_exit>"
    "<Required_cash>1000</Required_cash><End_pos>exit01</End_pos><Required_cash_coop>3000</Required_cash_coop>"
    "</Level_Exit><Reward>Activity_Unlock_RM</Reward><Reward2>Homie_Pierce</Reward2></Level></Levels>"
    "</Activity></Table></root>";

void testGenki() {
    const GenkiGlobals g = *ReadGenkiGlobals(sr3xtbl::ParseDocument(kRunningManGlobals));
    CHECK(g.kills[0].base == 50 && g.kills[2].base == 500 && g.kills[2].humanShield == 24);
    CHECK(g.kills[0].trapKill == 30 && g.kills[2].trapKill == 0);
    CHECK(g.ethicalBonusTime == 5 && g.ethicalBonusCash == 50);
    CHECK(g.unethicalCashDeduction == 100 && g.unethicalTimeDeduction == 10);
    CHECK(g.moneyBonusCash == 100 && g.timeBonusTime == 25 && g.buttonBonusCash == 100 && g.healthBonus == 225);
    CHECK(g.electricEvadeCash == 70 && g.fireEvadeCash == 50);
    CHECK(g.maxComboTicks == 2000 && g.baseDecayRate == 100);
    CHECK(g.multiplierTable.size() == 2 && g.decayRateTable.size() == 1);
    CHECK(GenkiComboFactor(g, 1) == 1.0 && GenkiComboFactor(g, 5) == 1.5 && GenkiComboFactor(g, 12) == 6.5);

    const GenkiLevel lv = *ReadGenkiLevel(sr3xtbl::ParseDocument(kGenkiDt01));
    CHECK(lv.timeLimitS == 180 && lv.requiredCash == 1000 && lv.requiredCashCoop == 3000 && lv.endExit == "Exit Door");
    const ActivityInstance inst = *ReadActivityInstance(sr3xtbl::ParseDocument(kGenkiDt01));
    CHECK(inst.levels[0].reward2 && *inst.levels[0].reward2 == "Homie_Pierce");
    CHECK(TypeFromInstanceName("_A_RM_DT_01") == ActivityType::Genki);

    GenkiRun run(g, lv);
    // Hunter, headshot, melee: 500 + 25 + 50.
    CHECK(run.kill({GenkiVictim::Hunter, false, false, true, false, true}) == 575.0);
    CHECK(run.kill({GenkiVictim::Civilian, true}) == 65.0);  // 50 + one hit 15
    CHECK(!run.reachExit());  // $640 < $1000
    CHECK(run.combo().multiplier() == 1.5);  // two kills: the next score pays x1.5
    CHECK(run.target(GenkiTarget::Unethical) == 0.0);
    CHECK(run.cash() == 490.0 && run.timeLeft() == 170.0);  // trunc(-100 x 1.5); the combo ends
    CHECK(run.lastComboEndBank() == 640 && run.combo().multiplier() == 1.0 && !run.combo().live());
    run.target(GenkiTarget::Ethical);
    CHECK(run.cash() == 540.0 && run.timeLeft() == 175.0);
    run.target(GenkiTarget::Money);
    run.target(GenkiTarget::Button);
    run.target(GenkiTarget::Time);
    CHECK(run.target(GenkiTarget::Health) == 225.0);
    run.trapEvaded(GenkiTrap::Electric);
    run.trapEvaded(GenkiTrap::Fire);
    CHECK(run.cash() == 860.0 && run.timeLeft() == 200.0);
    CHECK(run.combo().bank() == 0);  // no live combo: pickups do not add to B
    run.kill({GenkiVictim::EnemyGang, false, false, false, false, false, false, true});  // 50 + trap kill 30
    run.kill({GenkiVictim::Civilian});
    CHECK(run.cash() == 990.0 && !run.reachExit());
    CHECK(run.kill({GenkiVictim::Civilian}) == 75.0);  // third kill of the combo at x1.5
    CHECK(run.reachExit() && run.state() == GenkiState::Succeeded);

    GenkiRun late(g, lv);
    late.tick(179.0);
    CHECK(late.state() == GenkiState::Running);
    late.tick(1.0);
    CHECK(late.state() == GenkiState::Failed);
    GenkiRun dead(g, lv);
    dead.playerDied();
    CHECK(dead.state() == GenkiState::Failed);
    GenkiRun coop(g, lv, true);
    CHECK(coop.required() == 3000.0);
}

// The shipped combo tables (6.9.1): multiplier 2..12 -> x1.5..x6.5, decay 200..1000 ->
// 1.1/1.25/1.5/2.0/5.0, 2000 ticks, base 100; vectors V1-V7 of the spec.
GenkiGlobals ShippedComboGlobals() {
    GenkiGlobals g = *ReadGenkiGlobals(sr3xtbl::ParseDocument(kRunningManGlobals));
    g.multiplierTable.clear();
    for (int i = 0; i < 11; ++i) g.multiplierTable.push_back({2.0 + i, 1.5 + 0.5 * i});
    g.decayRateTable = {{200, 1.1}, {400, 1.25}, {600, 1.5}, {800, 2.0}, {1000, 5.0}};
    return g;
}

void testGenkiCombo() {
    const GenkiGlobals g = ShippedComboGlobals();
    const GenkiLevel lv = *ReadGenkiLevel(sr3xtbl::ParseDocument(kGenkiDt01));
    const double dt = 1.0 / 30.0;
    {  // V1
        GenkiRun r(g, lv);
        CHECK(r.kill({GenkiVictim::Civilian}) == 50.0 && r.combo().ticks() == 400);
        int frame = 0;
        long long ended = -1;
        while (ended < 0 && frame < 1000) {
            ++frame;
            ended = r.tick(dt);
        }
        CHECK(frame == 134 && ended == 50);
    }
    {  // V2
        GenkiRun r(g, lv);
        const double cash[13] = {50, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300, 325};
        const int dAfter[13] = {-1, -1, -1, 0, 1, 1, 2, 3, 4, 4, 4, 4, 4};
        bool ok = true;
        for (int k = 0; k < 13; ++k) {
            ok = ok && r.kill({GenkiVictim::Civilian}) == cash[k];
            ok = ok && r.combo().ticks() == std::min(400 * (k + 1), 2000) && r.combo().decayLevel() == dAfter[k];
        }
        CHECK(ok);
        CHECK(r.combo().multiplier() == 6.5);
    }
    {  // V3
        GenkiRun r(g, lv);
        GenkiKill h{GenkiVictim::Hunter};
        h.headshot = true;
        CHECK(r.kill(h) == 525.0 && r.combo().bank() == 525 && r.combo().decayLevel() == 0 && r.combo().ticks() == 400);
    }
    {  // V4
        GenkiRun r(g, lv);
        for (int i = 0; i < 3; ++i) r.kill({GenkiVictim::Civilian});
        CHECK(r.cash() == 175.0);
        r.target(GenkiTarget::Unethical);
        CHECK(r.cash() == 0.0 && r.timeLeft() == 170.0 && r.lastComboEndBank() == 175 && !r.combo().live());
    }
    {  // V5
        GenkiRun r(g, lv);
        r.target(GenkiTarget::Money);
        CHECK(r.cash() == 100.0 && r.combo().bank() == 0 && r.combo().ticks() == 0);
    }
    {  // V6
        GenkiRun r(g, lv);
        r.kill({GenkiVictim::Civilian});
        for (int i = 0; i < 60; ++i) r.tick(dt);
        CHECK(r.combo().ticks() == 220);
        CHECK(r.kill({GenkiVictim::Civilian}) == 50.0 && r.combo().ticks() == 620 && r.combo().multiplier() == 1.5);
    }
    {  // V7
        GenkiCombo c(g);
        c.add(0, 1000, 2000);  // B 1000 steps d once per add
        for (int i = 0; i < 4; ++i) c.add(0, 0, 0);
        CHECK(c.decayLevel() == 4 && c.ticks() == 2000 && c.decayFactor() == 5.0);
        int frame = 0;
        while (!c.decay(dt) && frame < 1000) ++frame;
        CHECK(frame + 1 == 125);
    }
}

// _a_ha_nw_02 as shipped (layout from the real file; values in spec 6.5).
const char* kHeliNw02 =
    "<root><Table><Activity><Name>_A_HA_NW_02</Name><Levels><Level><Cash_Award>4000</Cash_Award>"
    "<Props_Award>400</Props_Award><Heli_Health_Multiplier>5.0</Heli_Health_Multiplier>"
    "<Friendly_Fire_Mult>0.2</Friendly_Fire_Mult><Objectives_Needed>1</Objectives_Needed>"
    "<Time_For_Level>300</Time_For_Level><Use_Timer>False</Use_Timer></Level></Levels>"
    "<Protect_target_objective><Protect_Level_info><Level_info><Level_number>1</Level_number>"
    "<Time_between_spawns>12</Time_between_spawns><Chance_of_rocket_launcher>.18</Chance_of_rocket_launcher>"
    "<Vehicle_hit_points>27000</Vehicle_hit_points><Time_between_spawns_coop>9</Time_between_spawns_coop>"
    "</Level_info></Protect_Level_info></Protect_target_objective></Activity></Table></root>";

void testHeli() {
    const HeliParams p = *ReadHeliParams(sr3xtbl::ParseDocument(kHeliNw02));
    CHECK(p.objectivesNeeded == 1 && p.heliHealthMultiplier == 5.0 && p.timeForLevelS == 300.0 && !p.useTimer);
    CHECK_NEAR(p.friendlyFireMult, 0.2, 1e-6);
    CHECK(p.kind == HeliObjectiveKind::ProtectTarget && p.protect.has_value());
    CHECK(p.protect->vehicleHitPoints == 27000.0 && p.protect->timeBetweenSpawnsS == 12.0);
    CHECK(p.protect->timeBetweenSpawnsCoopS == 9.0);
    CHECK_NEAR(p.protect->chanceOfRocketLauncher, 0.18, 1e-6);
    CHECK(TypeFromInstanceName("_A_HA_NW_02") == ActivityType::HeliAssault);

    HeliRun run(p);
    run.tick(1000.0);  // Use_Timer False: untimed
    CHECK(run.state() == HeliState::Running);
    run.objectiveCompleted();
    CHECK(run.state() == HeliState::Succeeded && run.objectivesDone() == 1);
    HeliRun car(p);
    car.protectedCarDestroyed();
    CHECK(car.failReason() == HeliFail::ProtectedCarDestroyed);
    HeliRun heli(p);
    heli.heliDestroyed();
    CHECK(heli.failReason() == HeliFail::HeliDestroyed);
    HeliRun me(p);
    me.playerDied();
    CHECK(me.failReason() == HeliFail::PlayerDied);
    HeliParams timed = p;
    timed.useTimer = true;
    timed.kind = HeliObjectiveKind::VehiclesDestroy;
    HeliRun t(timed);
    t.protectedCarDestroyed();  // no protected car on this kind
    CHECK(t.state() == HeliState::Running);
    t.tick(299.0);
    CHECK(t.state() == HeliState::Running);
    t.tick(1.0);
    CHECK(t.failReason() == HeliFail::Time);
    // A level with no matching objective block.
    const HeliParams none = *ReadHeliParams(sr3xtbl::ParseDocument(
        "<root><Table><Activity><Levels><Level><Objectives_Needed>2</Objectives_Needed></Level></Levels>"
        "</Activity></Table></root>"));
    CHECK(none.kind == HeliObjectiveKind::None && none.objectivesNeeded == 2);
}

}  // namespace

// ===========================================================================
// The start point (CHOSEN: Warp_Location within 3 m) and the warp names of the real rows.
void testActivityStart() {
    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(
        "<root><Table><Activity><Name>_A_SN_NE_01</Name><Start_Location>Disabled</Start_Location>"
        "<Levels><Level><Cash_Award>2000</Cash_Award></Level></Levels>"
        "<Warp_Location>local_start</Warp_Location><Second_Warp_Location>remote_start</Second_Warp_Location>"
        "<End_Warp_Location>none</End_Warp_Location></Activity></Table></root>");
    const ActivityInstance inst = *ReadActivityInstance(doc);
    CHECK(!inst.startLocationEnabled);
    CHECK(inst.warpLocation && *inst.warpLocation == "local_start");
    CHECK(inst.secondWarpLocation && *inst.secondWarpLocation == "remote_start");
    CHECK(!inst.endWarpLocation && !inst.remoteEndWarpLocation);  // "none" and absent
    // local_start of _a_sn_ne_01's zone.
    const ActivityPos start{-1212.52, 6.27, 162.76};
    CHECK(AtActivityStart({-1212.52, 6.27, 159.80}, start));
    CHECK(AtActivityStart({-1210.52, 7.27, 162.76}, start));
    CHECK(!AtActivityStart({-1212.52, 6.27, 158.70}, start));
    CHECK_NEAR(Distance({0, 0, 0}, {3, 4, 12}), 13.0, 1e-12);
}

// _a_sn_ne_01's pickup layout as shipped (element layout and names from the real file;
// the Recapture block's own Navpoint_Name must not be taken for the drop-off).
const char* kSnatchNe01Layout =
    "<root><Table><Activity><Name>_A_SN_NE_01</Name><Levels><Level>"
    "<Snatch_Info><Ho_Win_Amount>4</Ho_Win_Amount><Duration>320</Duration>"
    "<Recruit_Notification_Dist>15</Recruit_Notification_Dist><Navpoint_Name>SN_Ho_Dropoff</Navpoint_Name>"
    "<Vehicle_Name>SN_Player_Start_Veh_01</Vehicle_Name><Max_Dropoff_Dist>8</Max_Dropoff_Dist></Snatch_Info>"
    "<Recapture_Complication><Navpoint_Name>SN_Recapture_Ho_Pickup</Navpoint_Name></Recapture_Complication>"
    "<Pickup_Locations>"
    "<Pickup_Location><Script_Group>SN_Pickup_Group_01</Script_Group>"
    "<Location_Navpoints><Location_Navpoint><Navpoint_Name>SN_Pickup_Group_01_Start_01</Navpoint_Name></Location_Navpoint>"
    "<Location_Navpoint><Navpoint_Name>SN_Pickup_Group_01_Start_02</Navpoint_Name></Location_Navpoint></Location_Navpoints>"
    "<Pickup_Complications><Pickup_Complication_Type><Standard_Pickup><Max_Spawn_Distance>5</Max_Spawn_Distance>"
    "<Pimps><Pimp><Pimp_Name>SN_Pickup_Group_01_Pimp_01</Pimp_Name></Pimp></Pimps>"
    "<Hos><Ho><Ho_Name>SN_Pickup_Group_01_Ho_03</Ho_Name></Ho><Ho><Ho_Name>SN_Pickup_Group_01_Ho_02</Ho_Name></Ho></Hos>"
    "</Standard_Pickup></Pickup_Complication_Type>"
    "<Pickup_Complication_Type><Standard_Pickup>"
    "<Thugs><Thug><Thug_Name>SN_Pickup_Group_01_Thug_02</Thug_Name></Thug></Thugs>"
    "<Hos><Ho><Ho_Name>SN_Pickup_Group_01_Ho_01</Ho_Name></Ho><Ho><Ho_Name>SN_Pickup_Group_01_Ho_04</Ho_Name></Ho></Hos>"
    "</Standard_Pickup></Pickup_Complication_Type></Pickup_Complications></Pickup_Location>"
    "<Pickup_Location><Script_Group>SN_Pickup_Group_02</Script_Group>"
    "<Location_Navpoints><Location_Navpoint><Navpoint_Name>SN_Pickup_Group_02_Start_01</Navpoint_Name></Location_Navpoint></Location_Navpoints>"
    "<Pickup_Complications><Pickup_Complication_Type><Ho_Rescue><Navpoint_Name>SN_Pickup_Group_02_Rescue_01</Navpoint_Name>"
    "<Hos><Ho><Ho_Name>SN_Pickup_Group_02_Ho_01</Ho_Name></Ho><Ho><Ho_Name>SN_Pickup_Group_02_Ho_02</Ho_Name></Ho>"
    "<Ho><Ho_Name>SN_Pickup_Group_02_Ho_ 003</Ho_Name></Ho></Hos>"
    "</Ho_Rescue></Pickup_Complication_Type></Pickup_Complications></Pickup_Location>"
    "</Pickup_Locations><Cash_Award>2000</Cash_Award></Level></Levels></Activity></Table></root>";

void testSnatchLayoutAndHostages() {
    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(kSnatchNe01Layout);
    const SnatchLayout l = ReadSnatchLayout(doc);
    CHECK(l.dropoffNavpoint == "SN_Ho_Dropoff");
    CHECK(l.playerVehicle && *l.playerVehicle == "SN_Player_Start_Veh_01");
    CHECK(l.recruitDist == 15.0);
    CHECK(l.pickups.size() == 2);
    if (l.pickups.size() == 2) {
        const SnatchPickup& a = l.pickups[0];
        CHECK(a.scriptGroup == "SN_Pickup_Group_01" && a.locationNavpoints.size() == 2);
        CHECK(a.options.size() == 2 && !a.options[0].rescue);
        if (a.options.size() == 2) {
            CHECK(a.options[0].hos.size() == 2 && a.options[0].hos[0] == "SN_Pickup_Group_01_Ho_03");
            CHECK(a.options[0].pimps.size() == 1 && a.options[1].thugs.size() == 1);
        }
        const SnatchPickup& b = l.pickups[1];
        CHECK(b.options.size() == 1 && b.options[0].rescue && b.options[0].hos.size() == 3);
        CHECK(b.options[0].rescueNavpoint && *b.options[0].rescueNavpoint == "SN_Pickup_Group_02_Rescue_01");
        CHECK(b.options[0].hos[2] == "SN_Pickup_Group_02_Ho_ 003");  // the shipped name has a space
    }
    CHECK(ReadSnatchLayout(sr3xtbl::ParseDocument("<root><Table><Activity/></Table></root>")).pickups.empty());

    // Hostages: join within the recruit distance, follow, hand over at the drop-off.
    const ActivityInstance inst = *ReadActivityInstance(doc);
    SnatchRun run(ReadSnatchParams(doc), inst.levels[0]);
    SnatchHostages hs;
    const ActivityPos pickup{-1673.13, 2.30, -229.28}, drop{-1206.73, 6.27, 168.78};
    hs.add("SN_Pickup_Group_01_Ho_03", pickup);
    hs.add("SN_Pickup_Group_01_Ho_02", pickup);
    hs.add("SN_Pickup_Group_02_Ho_01", {-854.59, 2.65, -138.68});
    SnatchHostages::Update u = hs.update({-1673.13, 2.30, -213.0}, false, drop, l.recruitDist, run);
    CHECK(u.recruited == 0 && hs.count(SnatchHostages::State::Waiting) == 3);  // 16.3 m away
    u = hs.update({-1673.13, 2.30, -215.0}, false, drop, l.recruitDist, run);
    CHECK(u.recruited == 2 && hs.count(SnatchHostages::State::Following) == 2);
    u = hs.update({-1300.0, 5.0, 0.0}, false, drop, l.recruitDist, run);  // on the way: they follow
    CHECK(u.recruited == 0 && u.delivered == 0 && hs.hostages()[0].pos.x == -1300.0);
    u = hs.update({-1206.73, 6.27, 175.70}, false, drop, l.recruitDist, run);  // 6.92 m on foot: not yet
    CHECK(u.delivered == 0 && run.delivered() == 0);
    u = hs.update({-1206.73, 6.27, 175.70}, true, drop, l.recruitDist, run);  // in a vehicle: within 8
    CHECK(u.delivered == 2 && run.delivered() == 2 && hs.count(SnatchHostages::State::Delivered) == 2);
    u = hs.update({-1206.73, 6.27, 168.78}, false, drop, l.recruitDist, run);
    CHECK(u.delivered == 0 && run.delivered() == 2);  // a delivered hostage is not counted twice
    run.playerDied();
    u = hs.update({-854.59, 2.65, -138.68}, false, drop, l.recruitDist, run);
    CHECK(u.recruited == 0 && hs.count(SnatchHostages::State::Waiting) == 1);  // nothing after the run ended
}

// The harness helpers (CHOSEN test aids, not game behaviour).
void testHarness() {
    const ActivityPos a{0, 0, 0}, b{30, 0, 40};
    const ActivityPos s = StepToward(a, b, 10.0, 1.0);
    CHECK_NEAR(s.x, 6.0, 1e-12);
    CHECK_NEAR(s.z, 8.0, 1e-12);
    const ActivityPos e = StepToward({29, 0, 39}, b, 10.0, 1.0);  // never overshoots
    CHECK(e.x == 30.0 && e.z == 40.0);

    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(kFraudDt01);
    FraudRun run(TestParams(), ReadActivityInstance(doc)->levels[0]);
    FraudStrikeHarness h;
    const FraudScoringContext ctx{true, -1};
    const double dt = 1.0 / 30.0;
    CHECK(!h.step(run, dt, ctx) && h.inEpisode() && h.strikes() == 1 && run.hitArmed());
    CHECK(run.hitBase() > 0.0);  // the car impact
    std::optional<FraudBankResult> banked;
    int frames = 0;
    while (!banked && frames < 200) {
        run.tick(dt, ctx, h.inEpisode(), false, false);
        banked = h.step(run, dt, ctx);
        ++frames;
    }
    CHECK(banked && banked->hitValue > 0.0 && frames >= 45 && frames <= 46);  // 1.5 s of tumbling, then the end
    if (banked) CHECK_NEAR(run.score(), banked->hitValue, 1e-9);
    // The 3 s cooldown keeps the next strike from starting.
    run.tick(dt, ctx, false, false, false);
    CHECK(!h.step(run, dt, ctx) && !h.inEpisode() && h.strikes() == 1);
    for (int i = 0; i < 95; ++i) {
        run.tick(dt, ctx, false, false, false);
        h.step(run, dt, ctx);
    }
    CHECK(h.strikes() == 2 && h.inEpisode());
}

int main() {
    run("testCategoryValue", testCategoryValue);
    run("testInstanceReader", testInstanceReader);
    run("testRewardGrant", testRewardGrant);
    run("testImpacts", testImpacts);
    run("testHitValueAndBank", testHitValueAndBank);
    run("testWinFail", testWinFail);
    run("testParamsFromTables", testParamsFromTables);
    run("testSnatch", testSnatch);
    run("testMayhemReader", testMayhemReader);
    run("testMayhemCombo", testMayhemCombo);
    run("testMayhemWinFail", testMayhemWinFail);
    run("testTankLeash", testTankLeash);
    run("testEscort", testEscort);
    run("testTrafficking", testTrafficking);
    run("testGenki", testGenki);
    run("testGenkiCombo", testGenkiCombo);
    run("testHeli", testHeli);
    run("testActivityStart", testActivityStart);
    run("testSnatchLayoutAndHostages", testSnatchLayoutAndHostages);
    run("testHarness", testHarness);
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
