// Synthetic tests for sr3progression. Every expected value comes from the
// TEXT of spec-tables-progression.md (section 5 and "Free-roam respect
// grants and level-up flow" B.1-B.3, including the B.3 vectors), never from
// this project's own source.
//
// Style follows tests/synthetic_tables_progression_test.cpp.

#include <iostream>
#include <string>
#include <vector>

#include "sr3progression/respect.h"
#include "sr3xtbl/xtbl.h"

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

using namespace sr3progression;

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

// The shipped costs with the unlockables of rows 2-5 (B.2 table) and a
// stand-in list for row 50.
RespectTable ShippedWithUnlocks() {
    const RespectTable costs = RespectTable::ShippedCosts();
    std::vector<RespectLevelRow> rows;
    for (int i = 0; i < costs.levelCount(); ++i) rows.push_back({costs.record(i).respect, {}});
    rows[1].unlockables = {"Sprint_1", "Damage_Bullet_1", "Notoriety_Mstar_1",
                           "Upgrade_Ammo_Pool_Pistol_1", "Vehicle_delivery"};
    rows[2].unlockables = {"Upgrade_Ammo_Pool_SMG_1", "Health_Regen_1", "Damage_Vehicle_1",
                           "Upgrade_Ammo_Pool_Grenades_1", "Dual_Wield_Pistols"};
    rows[3].unlockables = {"Upgrade_Ammo_Pool_Shotgun_1", "Money_Drop_Multiplier", "Weekly_Payment_1",
                           "Pickpocket"};
    rows[4].unlockables = {"Upgrade_Ammo_Pool_Rifle_1", "Faster_Homey_Revive_1", "Muscles_1",
                           "Cash_for_Respect_1"};
    for (int i = 5; i < 49; ++i) rows[static_cast<size_t>(i)].unlockables = {"row_" + std::to_string(i + 1)};
    rows[49].unlockables = {"Unlimited_Ammo_1", "Unlimited_Ammo_2", "Unlimited_Ammo_3", "Unlimited_Ammo_4",
                            "Unlimited_Ammo_5", "Unlimited_Ammo_6", "Unlimited_Ammo_7", "Damage_Vehicle_4"};
    return RespectTable(rows);
}

// ===========================================================================
void testTable() {
    const RespectTable t = RespectTable::ShippedCosts();
    CHECK(t.levelCount() == 50);
    CHECK(t.record(0).respect == 2115);
    CHECK(t.record(9).respect == 3150);   // row 10
    CHECK(t.record(48).respect == 7635);  // row 49
    CHECK(t.record(49).respect == 0);
    CHECK(t.cap() == 238875);
    CHECK(t.cost(0) == 2115);
    CHECK(t.cost(4) == 2575);
    CHECK(t.cost(50) == 22000);
    CHECK(t.cost(77) == 22000);
    // Cumulative column of B.2: 2115, 4345, 6690, 9150, 11725, ... 26325.
    const int64_t cum[] = {2115, 4345, 6690, 9150, 11725, 14415, 17220, 20140, 23175, 26325};
    for (int L = 1; L <= 10; ++L) CHECK(t.cumulative(L) == cum[L - 1]);
    for (int L = 1; L <= 49; ++L) CHECK(t.cumulative(L) == 2115LL * L + 115LL * L * (L - 1) / 2);
    CHECK(t.cumulative(49) == 238875);

    // Rows past 50 are ignored.
    std::vector<RespectLevelRow> many(60, RespectLevelRow{100, {}});
    CHECK(RespectTable(many).levelCount() == 50);
    CHECK(RespectTable(many).cap() == 4900);
    // Below the loaded count but under 50: record default 0.
    CHECK(RespectTable(std::vector<RespectLevelRow>(3, RespectLevelRow{10, {}})).cost(10) == 0);
}

void testFromParsed() {
    const char* xml =
        "<root><Table><respect_levels><levels>"
        "<respect_level><Rank>R1</Rank><Respect>2115</Respect></respect_level>"
        "<respect_level><Rank>R2</Rank><Respect>2230</Respect>"
        "<Unlockables><Unlockable>Sprint_1</Unlockable><Unlockable>Vehicle_delivery</Unlockable></Unlockables>"
        "</respect_level>"
        "<respect_level><Rank>R3</Rank><Respect>0</Respect></respect_level>"
        "</levels></respect_levels></Table></root>";
    const sr3xtbl::Document d = sr3xtbl::ParseDocument(xml);
    const RespectTable t = RespectTable::FromParsed(sr3tables_progression::ParseRespectLevelsTable(d));
    CHECK(t.levelCount() == 3);
    CHECK(t.cap() == 4345);
    CHECK(t.record(1).unlockables.size() == 2);
    CHECK(t.record(1).unlockables[1] == "Vehicle_delivery");
}

void testTotalsTable() {
    const RespectTable t = RespectTable::ShippedCosts();
    struct Row { int32_t total, L, inLevel; };
    const Row rows[] = {{0, 0, 0}, {2114, 0, 2114}, {2115, 1, 0}, {10000, 4, 850},
                        {100000, 27, 2530}, {238875, 49, 0}};
    for (const Row& r : rows) {
        const PlayerRespect p = RespectStateFromTotal(r.total, t);
        CHECK(p.total == r.total);
        CHECK(p.level == r.L);
        CHECK(p.inLevel == r.inLevel);
        CHECK(RespectStateConsistent(p, t));
    }
    // Any split gives the same result.
    PlayerRespect p;
    AppliedUnlocks a;
    for (int i = 0; i < 100; ++i) AwardRespect(p, 1000, t, a);
    CHECK(p.total == 100000 && p.level == 27 && p.inLevel == 2530);
    // A.4 save vector: 10,000 / 850 / level 4.
    CHECK(RespectStateConsistent({10000, 850, 4}, t));
    CHECK(!RespectStateConsistent({10000, 851, 4}, t));
    // B.2 data-check examples.
    CHECK(RespectStateConsistent({73628, 533, 22}, t));
    CHECK(RespectStateConsistent({167547, 6332, 38}, t));
    CHECK(RespectStateConsistent({183816, 2801, 41}, t));
}

void testSequenceA() {
    const RespectTable t = ShippedWithUnlocks();
    AppliedUnlocks applied;
    for (int L = 1; L <= 2; ++L)
        for (const std::string& u : t.record(L).unlockables) applied.insert(u);
    PlayerRespect p{6600, 2255, 2};
    CHECK(RespectStateConsistent(p, t));
    const float m = 1.0f;

    // 1. Low flying at fraction 1.0: 75.
    const DiversionReward lf = DiversionRewardFor(1.0f, 75, 5000, 100, 0);
    CHECK(lf.respect == 75 && lf.cashDollars == 0);
    AwardResult r = AwardRespect(p, ScaleByMultiplier(m, lf.respect), t, applied);
    CHECK(r.granted == 75 && r.returned == 75);
    CHECK(p.total == 6675 && p.level == 2 && p.inLevel == 2330);
    CHECK(r.levelUps.empty());
    CHECK(r.respectHint && r.respectHintAmount == 75 && r.respectHintLevel == 3);

    // 2. Activity _02, Props_Award 400: the second, scaled call is 0.
    const ActivityAwards act = ActivityCompletionAwards(400, m);
    CHECK(act.first == 400 && act.second == 0);
    r = AwardRespect(p, act.first, t, applied);
    CHECK(p.total == 7075 && p.level == 3 && p.inLevel == 385);
    CHECK(r.levelUps.size() == 1);
    CHECK(r.levelUps[0].level == 3);
    CHECK(r.levelUps[0].newlyApplied == std::vector<std::string>({"Upgrade_Ammo_Pool_Shotgun_1",
                                                                   "Money_Drop_Multiplier",
                                                                   "Weekly_Payment_1", "Pickpocket"}));
    CHECK(r.levelUps[0].levelUpHint);
    CHECK(!r.levelUps[0].milestone);
    CHECK(r.respectHintAmount == 400 && r.respectHintLevel == 4);
    const AwardResult zero = AwardRespect(p, act.second, t, applied);
    CHECK(zero.returned == 0 && !zero.respectHint && p.total == 7075);

    // 3. game_award_respect(4000).
    r = AwardRespect(p, ScaleByMultiplier(m, 4000), t, applied);
    CHECK(p.total == 11075 && p.level == 4 && p.inLevel == 1925);
    CHECK(r.levelUps.size() == 1 && r.levelUps[0].level == 4);
    CHECK(r.levelUps[0].newlyApplied.back() == "Cash_for_Respect_1");
    CHECK(r.respectHintAmount == 4000 && r.respectHintLevel == 5);
    CHECK(applied.count("Cash_for_Respect_1") == 1);
}

void testSequenceB() {
    const RespectTable t = ShippedWithUnlocks();
    AppliedUnlocks applied;
    PlayerRespect p{238800, 7560, 48};
    CHECK(RespectStateConsistent(p, t));
    AwardResult r = AwardRespect(p, 100, t, applied);
    CHECK(r.granted == 75);  // clamped to the cap
    CHECK(p.total == 238875 && p.level == 49 && p.inLevel == 0);
    CHECK(r.levelUps.size() == 1);
    CHECK(r.levelUps[0].newlyApplied.size() == 8);
    CHECK(r.levelUps[0].newlyApplied.back() == "Damage_Vehicle_4");
    CHECK(r.respectHintLevel == 50);

    r = AwardRespect(p, 50, t, applied);
    CHECK(r.granted == 0 && r.returned == 0);
    CHECK(r.overflowCashRaw == 5000);  // +$50.00
    CHECK(!r.respectHint);
    CHECK(p.total == 238875 && p.level == 49);
}

void testAwardGuardsAndEvents() {
    const RespectTable t = ShippedWithUnlocks();
    AppliedUnlocks applied;
    PlayerRespect p;
    CHECK(AwardRespect(p, 0, t, applied).returned == 0);
    CHECK(AwardRespect(p, -5, t, applied).returned == 0);
    AwardContext horde;
    horde.hordeMode = true;
    CHECK(AwardRespect(p, 100, t, applied, horde).returned == 0);
    CHECK(p.total == 0);
    AwardContext remote;
    remote.locallyOwned = false;
    const AwardResult net = AwardRespect(p, 100, t, applied, remote);
    CHECK(net.sentToNetwork && net.returned == 100 && p.total == 0);

    // One big award crosses several levels; the decade level is a milestone
    // only in the city.
    AwardResult r = AwardRespect(p, static_cast<int32_t>(t.cumulative(11)), t, applied);
    CHECK(p.level == 11 && p.inLevel == 0);
    CHECK(r.levelUps.size() == 11);
    int milestones = 0;
    for (const LevelUp& u : r.levelUps) milestones += u.milestone ? 1 : 0;
    CHECK(milestones == 1 && r.levelUps[9].level == 10 && r.levelUps[9].milestone);
    PlayerRespect q;
    AppliedUnlocks a2;
    AwardContext outside;
    outside.inCityZone = false;
    r = AwardRespect(q, static_cast<int32_t>(t.cumulative(10)), t, a2, outside);
    CHECK(!r.levelUps.back().milestone);

    // Unlocks already applied are not re-applied, and then no 0xA9 unless
    // forced.
    PlayerRespect s{0, 0, 0};
    AppliedUnlocks all;
    for (int L = 0; L < t.levelCount(); ++L)
        for (const std::string& u : t.record(L).unlockables) all.insert(u);
    r = AwardRespect(s, 2115, t, all);
    CHECK(r.levelUps.size() == 1 && r.levelUps[0].newlyApplied.empty() && !r.levelUps[0].levelUpHint);
    AwardContext forced;
    forced.forceLevelUpHint = true;
    r = AwardRespect(s, 2230, t, all, forced);
    CHECK(r.levelUps.size() == 1 && r.levelUps[0].levelUpHint);

    // Overflow at the cap converts to dollars, clamped.
    PlayerRespect capped{238875, 0, 49};
    CHECK(AwardRespect(capped, 30000000, t, all).overflowCashRaw == 2000000000);
    CHECK(DollarsToRawCash(-30000000) == -2000000000);
}

void testMultiplierAndSources() {
    float m = kNewGameRespectMultiplier;
    m = AddRespectBonus(m, 0.05f);
    m = AddRespectBonus(m, 0.05f);
    m = AddRespectBonus(m, 0.05f);
    m = AddRespectBonus(m, 0.10f);
    CHECK(m > 1.2499f && m < 1.2501f);  // saves hold 1.25
    CHECK(ScaleByMultiplier(1.25f, 75) == 94);  // round(93.75)
    CHECK(ScaleByMultiplier(1.0f, 4000) == 4000);
    const ActivityAwards a = ActivityCompletionAwards(600, 1.25f);
    CHECK(a.first == 600 && a.second == 150);

    // Diversion rule: trunc respect, or round cash once the lifetime stat
    // reaches Max_Lifetime_Respect.
    DiversionReward d = DiversionRewardFor(0.5f, 75, 5000, 100, 4999);
    CHECK(d.respect == 37 && d.cashDollars == 0);
    d = DiversionRewardFor(0.5f, 75, 5000, 100, 5000);
    CHECK(d.respect == 0 && d.cashDollars == 50);
    d = DiversionRewardFor(0.555f, 30, 3000, 80, 3000);
    CHECK(d.cashDollars == 44);  // floor(44.4) (D-2)
    d = DiversionRewardFor(1.7f, 30, 3000, 80, 0);  // clamped to 1
    CHECK(d.respect == 30);
    d = DiversionRewardFor(0.0f, 30, 3000, 80, 0);
    CHECK(d.respect == 0 && d.cashDollars == 0);
    CHECK(kShippedStuntDiversions[5].maxRespect == 75);  // low flying
    CHECK(kShippedStuntDiversions[7].maxRespect == 150 && kShippedStuntDiversions[7].statId == 0xB0);

    // Toast finalisation: round(multiplier x respect), cash x 100.
    const RespectTable t = RespectTable::ShippedCosts();
    AppliedUnlocks ap;
    PlayerRespect p;
    ToastFinalisation f = FinaliseRewardToast({100, 12.5f}, 1.35f, p, t, ap);
    CHECK(f.award.granted == 135 && p.total == 135);
    CHECK(f.cashRaw == 0);  // a toast with respect pays no cash (spec-differential D-3)
    CHECK(!f.zeroGrantHint);
    f = FinaliseRewardToast({0, 3000.0f}, 1.0f, p, t, ap);
    CHECK(!f.zeroGrantHint && f.cashRaw == 300000);  // no respect: cash only, no notification (D-3)

    // Kills: 10 by rank, unscaled, gang members only.
    const KillRespectTable props;
    CHECK(KillRespect(props, 0, true, false) == 10);
    CHECK(KillRespect(props, 3, true, false) == 10);
    CHECK(KillRespect(props, 1, false, false) == 0);
    CHECK(KillRespect(props, 2, true, true) == 0);
    KillRespectTable custom{1, 2, 3, 4};
    CHECK(KillRespect(custom, 0, true, false) == 1);
    CHECK(KillRespect(custom, 1, true, false) == 2);
    CHECK(KillRespect(custom, 2, true, false) == 3);
    CHECK(KillRespect(custom, 5, true, false) == 4);
    CHECK(KillRespect(props, 0, true, false, 2.0f) == 20);

    // Store purchases: the shipped multiplier 0 gives 0 (rejected by the award).
    CHECK(StorePurchaseRespect(5000.0, 0.0, 1.45f) == 0);
    CHECK(StorePurchaseRespect(1000.0, 0.1, 1.5f) == 150);

}

void testUnlockableApply() {
    sr3tables_progression::Unlockable bonus;
    bonus.name = "Respect_Bonus_Purchase1";
    bonus.respectBonusModifier = 0.05f;
    sr3tables_progression::Unlockable other;
    other.name = "Sprint_1";
    float m = kNewGameRespectMultiplier;
    m = ApplyRespectBonusUnlockable(m, bonus);
    CHECK(m > 1.0499f && m < 1.0501f);
    CHECK(ApplyRespectBonusUnlockable(m, other) == m);

    sr3tables_progression::Unlockable cfr;
    cfr.name = "Cash_for_Respect_1";
    cfr.payCashForRespect = sr3tables_progression::UnlockablePayCashForRespectPayload{0, 4000};
    const RespectTable t = RespectTable::ShippedCosts();
    AppliedUnlocks ap;
    PlayerRespect p;
    PayCashForRespectResult r = ApplyPayCashForRespectUnlockable(cfr, 1.25f, p, t, ap);
    CHECK(r.applies && r.cashCost == 0);
    CHECK(r.award.granted == 5000 && p.total == 5000);
    r = ApplyPayCashForRespectUnlockable(other, 1.25f, p, t, ap);
    CHECK(!r.applies && r.award.granted == 0 && p.total == 5000);
}

}  // namespace

// spec-tables-progression.md 5.4 note (2026-10-20, Team D emulation FINAL): the level table 2115, 3000, 4000,
// 5000, ...; one pass pays record L's cost and applies the unlockables of record L + 1 (the new level).
void testLevelUpRecordVectors() {
    std::vector<RespectLevelRow> rows;
    const int32_t costs[] = {2115, 3000, 4000, 5000, 6000};
    for (int i = 0; i < 5; ++i) rows.push_back({costs[i], {"u" + std::to_string(i)}});
    const RespectTable t(rows);
    {
        PlayerRespect p;
        AppliedUnlocks a;
        const auto ups = ApplyLevelUp(p, 2115, t, a);
        CHECK(p.level == 1 && p.inLevel == 0);
        CHECK(ups.size() == 1 && ups[0].level == 1);
        CHECK(ups.size() == 1 && ups[0].newlyApplied.size() == 1 && ups[0].newlyApplied[0] == "u1");
    }
    {
        PlayerRespect p;
        AppliedUnlocks a;
        CHECK(ApplyLevelUp(p, 2114, t, a).empty());
        CHECK(p.level == 0 && p.inLevel == 2114);
    }
    {
        PlayerRespect p{0, 100, 0};
        AppliedUnlocks a;
        CHECK(ApplyLevelUp(p, 2015, t, a).size() == 1);
        CHECK(p.level == 1 && p.inLevel == 0);
    }
    {
        PlayerRespect p;
        AppliedUnlocks a;
        const auto ups = ApplyLevelUp(p, 5115, t, a);
        CHECK(p.level == 2 && p.inLevel == 0);
        CHECK(ups.size() == 2 && ups[0].level == 1 && ups[1].level == 2);
    }
    {
        // A needed value <= 1 blocks levelling, also after a level-up inside one award.
        std::vector<RespectLevelRow> r2 = {{100, {}}, {1, {}}, {100, {}}, {100, {}}};
        PlayerRespect p;
        AppliedUnlocks a;
        CHECK(ApplyLevelUp(p, 150, RespectTable(r2), a).size() == 1);
        CHECK(p.level == 1 && p.inLevel == 50);
        PlayerRespect q;
        CHECK(ApplyLevelUp(q, -5, t, a).empty() && q.inLevel == 0);  // negative floors at 0
    }
}

int main() {
    run("testTable", testTable);
    run("testFromParsed", testFromParsed);
    run("testTotalsTable", testTotalsTable);
    run("testSequenceA", testSequenceA);
    run("testSequenceB", testSequenceB);
    run("testAwardGuardsAndEvents", testAwardGuardsAndEvents);
    run("testMultiplierAndSources", testMultiplierAndSources);
    run("testUnlockableApply", testUnlockableApply);
    run("testLevelUpRecordVectors", testLevelUpRecordVectors);
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
