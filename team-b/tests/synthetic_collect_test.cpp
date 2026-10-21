// sr3collect without game data: collectibles (spec-tables-progression.md C.2) and challenges
// (C.3) on small tables shaped like the shipped ones. The shipped-data vectors are in
// tests/real_collect_tables_test.cpp.
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3collect/challenges.h"
#include "sr3collect/collectibles.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(cond)) {                                                                          \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n";        \
            ++g_failures;                                                                       \
        }                                                                                       \
    } while (0)

using namespace sr3collect;

std::array<CollectibleGroupDef, kCollectibleGroups> ShippedLikeDefs() {
    std::vector<sr3tables_progression::Collectible> rows;
    const char* names[] = {"Drug Package", "money pallet", "Sex Doll", "Photo Op", "Unknown Thing"};
    const char* titles[] = {"DRUG_PACKAGE_COLLECTED", "MONEY_PALLET_COLLECTED", "SEX_DOLL_COLLECTED",
                            "PHOTO_OP_COLLECTED", "X"};
    for (int i = 0; i < 5; ++i) {
        sr3tables_progression::Collectible c;
        c.name = names[i];
        c.titleMessage = titles[i];
        c.cashAward = 1000.0f;
        for (int t : {5, 10, 15, 20}) {
            sr3tables_progression::ThresholdReward r;
            r.threshold = {t, true};
            r.respectAward = {0, true};
            r.cashAward = {1000.0f, true};
            c.thresholdRewards.push_back(r);
        }
        sr3tables_progression::ThresholdReward zero;  // Threshold 0: dropped
        zero.threshold = {0, true};
        zero.cashAward = {99999.0f, true};
        c.thresholdRewards.push_back(zero);
        rows.push_back(c);
    }
    return CollectibleDefsFromTable(rows);
}

void testCollectibles() {
    const auto defs = ShippedLikeDefs();
    CHECK(defs[1].name == "money pallet" && defs[3].title == "PHOTO_OP_COLLECTED");
    CHECK(defs[0].thresholds.size() == 4);  // the Threshold 0 record is dropped
    CollectibleRegistry reg;
    for (int g = 0; g < 4; ++g)
        for (uint64_t h = 1; h <= 20; ++h) CHECK(reg.registerHandle(g, (static_cast<uint64_t>(g) << 32) | h));
    CHECK(!reg.registerHandle(0, 999));  // 21st handle: refused (vector 7)
    CHECK(reg.registerHandle(0, 5));     // find-or-append: already there
    CHECK(reg.registered(0) == 20 && reg.totalRegistered() == 80);

    double cash = 0;
    int32_t respect = 0;
    CollectResult r = reg.collect(0, 1, defs);  // vector 1
    CHECK(r.accepted && r.collected == 1 && r.registered == 20 && r.cash == 1000.0 && r.respect == 0);
    CHECK(r.screen == CollectionScreen::Generic && r.title == "DRUG_PACKAGE_COLLECTED");
    CHECK(!reg.collect(0, 1, defs).accepted);    // already collected
    CHECK(!reg.collect(0, 999, defs).accepted);  // not registered
    CHECK(!reg.collect(4, 1, defs).accepted);    // group > 3
    CHECK(!reg.hasMarker(0, 1) && reg.hasMarker(0, 2));
    cash += r.cash;
    for (uint64_t h = 2; h <= 20; ++h) {
        r = reg.collect(0, h, defs);
        cash += r.cash;
        respect += r.respect;
        if (h == 5) CHECK(r.cash == 2000.0);  // vector 2 (threshold 5)
        if (h == 6) CHECK(r.cash == 1000.0);
    }
    CHECK(r.collected == 20 && r.cash == 2000.0);  // vector 3
    CHECK(cash == 24000.0 && respect == 0);         // per group $24,000
    for (int g = 1; g < 4; ++g)
        for (uint64_t h = 1; h <= 20; ++h) {
            r = reg.collect(g, (static_cast<uint64_t>(g) << 32) | h, defs);
            cash += r.cash;
        }
    CHECK(r.screen == CollectionScreen::PhotoOp && r.title == "PHOTO_OP_COLLECTED");  // vector 6
    CHECK(cash == 96000.0 && reg.totalCollected() == 80 && reg.completion() == 1.0);  // vector 5

    // Save entry 1: collected slots group by group; restore re-registers them as collected.
    const auto recs = reg.saveRecords();
    CHECK(recs.size() == 80 && recs[0].group == 0 && recs[0].handleLo == 1 && recs[79].group == 3 &&
          recs[79].handleHi == 3);
    CollectibleRegistry back;
    back.restore(recs);
    CHECK(back.totalCollected() == 80 && !back.hasMarker(2, (2ull << 32) | 7));

    const CollectionAchievements a = EvaluateCollectionAchievements(20, 0);
    CHECK(a.gettingTheGoods && !a.lifeOfTheParty && !a.shakeAndBake);  // vector 4
    const CollectionAchievements b = EvaluateCollectionAchievements(80, 41);
    CHECK(b.lifeOfTheParty && b.shakeAndBake && b.youreMyHero);
}

const char* kChallenges =
    "<root><Table>"
    "<Challenge><Name>Get Headshots</Name><Goal>500</Goal><Reward><Respect>500</Respect><Cash>1500</Cash></Reward>"
    "<HUD><HUD_Percent><HUD_Percent_Value>10</HUD_Percent_Value></HUD_Percent></HUD>"
    "<Trigger><Trigger_Kill><Team>Any Hostile But Police</Team><Weapon>Any</Weapon><Weapon_Slot>Any</Weapon_Slot>"
    "<Damage_Type>Headshot</Damage_Type></Trigger_Kill></Trigger><Units>None</Units>"
    "<Unlock_Requirement><Unlock_Name>m03</Unlock_Name></Unlock_Requirement></Challenge>"
    "<Challenge><Name>Drive Distance</Name><Goal>762000</Goal><Reward><Respect>500</Respect><Cash>1500</Cash></Reward>"
    "<HUD><HUD_Percent><HUD_Percent_Value>5</HUD_Percent_Value></HUD_Percent></HUD>"
    "<Trigger><Trigger_Stat><Stat>distance by vehicle</Stat><Units>Miles</Units></Trigger_Stat></Trigger>"
    "<Units>Meters</Units><Unlock_Requirement><Unlock_Name>m03</Unlock_Name></Unlock_Requirement></Challenge>"
    "<Challenge><Name>Kill Tanks</Name><Goal>2</Goal><Reward><Respect>500</Respect><Cash>1500</Cash></Reward>"
    "<HUD><HUD_Linear><HUD_Linear_Value>1</HUD_Linear_Value></HUD_Linear></HUD>"
    "<Trigger><Trigger_Destroy><Vehicle_List><Vehicles><Vehicle>sp_tank01</Vehicle></Vehicles>"
    "<Vehicles><Vehicle>sp_tank02</Vehicle></Vehicles></Vehicle_List><Vehicle_Type>Any</Vehicle_Type>"
    "<Team>Any</Team><Weapon>Any</Weapon><Weapon_Slot>Any</Weapon_Slot></Trigger_Destroy></Trigger></Challenge>"
    "<Challenge><Name>Spend Money Modding Vehicles</Name><Goal>5000</Goal><Reward><Respect>500</Respect>"
    "<Cash>1500</Cash></Reward><Trigger><Trigger_Special><Special_Case_Name>Items Purchased</Special_Case_Name>"
    "<Store>Vehicle Customization</Store></Trigger_Special></Trigger></Challenge>"
    "</Table></root>";

void testChallenges() {
    const auto defs = ReadChallenges(sr3xtbl::ParseDocument(kChallenges));
    CHECK(defs.size() == 4);
    CHECK(defs[0].trigger == ChallengeTrigger::Kill && defs[0].damageType == "Headshot" && defs[0].respect == 500 &&
          defs[0].cash == 1500.0 && defs[0].unlockName && *defs[0].unlockName == "m03");
    CHECK(defs[1].unit == ChallengeUnit::Meters && defs[1].displayUnit == ChallengeUnit::Miles && defs[1].hudStep == 5.0);
    CHECK(defs[2].vehicles.size() == 2 && !defs[2].hudPercent && !defs[2].unlockName);
    CHECK(defs[3].specialCase == "Items Purchased" && defs[3].store == "Vehicle Customization");
    CHECK(std::fabs(ChallengeDisplayValue(ChallengeUnit::Miles, 762000.0) - 473.48) < 0.01);  // vector 10
    CHECK(ChallengeDisplayValue(ChallengeUnit::Minutes, 600.0) == 10.0);

    ChallengeTracker t(defs);
    CHECK(!t.states()[0].unlocked && t.states()[2].unlocked);  // no requirement: unlocked (CHOSEN)
    ChallengeKill head{"Brotherhood", true, "Pistol", "Pistol", "Headshot"};
    CHECK(t.kill(head).empty() && t.states()[0].value == 0.0);  // vector 9: locked before m03
    t.missionCompleted("M03");
    ChallengeKill cop{"Police", true, "Pistol", "Pistol", "Headshot"};
    CHECK(t.kill(cop).empty() && t.states()[0].value == 0.0);  // police excluded
    ChallengeKill body{"Brotherhood", true, "Pistol", "Pistol", "Bullet"};
    t.kill(body);
    CHECK(t.states()[0].value == 0.0);  // wrong damage type
    int notices = 0;
    bool done = false;
    for (int i = 0; i < 500; ++i)
        for (const ChallengeUpdate& u : t.kill(head)) {
            notices += u.notice ? 1 : 0;
            done = done || u.completedNow;
        }
    CHECK(done && t.states()[0].completed && t.states()[0].value == 500.0);  // vector 8
    CHECK(notices == 10);  // every 10 % (50 kills), the last being the completion
    CHECK(t.kill(head).empty());  // completed: no more progress

    // Stat: clamps to the goal.
    t.statChanged("distance by vehicle", 700000.0);
    CHECK(!t.states()[1].completed);
    const auto up = t.statChanged("Distance By Vehicle", 100000.0);
    CHECK(up.size() == 1 && up[0].completedNow && t.states()[1].value == 762000.0);
    CHECK(t.progress(1, 10.0).index == 1 && t.states()[1].value == 762000.0);

    ChallengeDestroy car{"Automobile", "car_4dr_standard03", "Any", "", ""};
    CHECK(t.destroy(car).empty());
    ChallengeDestroy tank{"Tank", "SP_Tank02", "Any", "", ""};
    CHECK(t.destroy(tank).size() == 1);  // linear step 1: a notice
    CHECK(t.destroy(tank).at(0).completedNow);

    CHECK(t.special("Items Purchased", 3000.0, "Clothing").empty());
    t.special("Items Purchased", 3000.0, "Vehicle Customization");
    CHECK(t.states()[3].value == 3000.0);
    CHECK(t.special("Items Purchased", 2500.0, "Vehicle Customization").at(0).completedNow);
    CHECK(t.completedCount() == 4);
    CHECK(t.progress(0, 0.0).completedNow == false);
}

}  // namespace

int main() {
    testCollectibles();
    testChallenges();
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
