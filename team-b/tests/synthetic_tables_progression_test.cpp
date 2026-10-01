// Synthetic tests for sr3tables_progression. Every fixture is hand-built XML
// from the TEXT of spec-tables-progression.md (element names, defaults, enum
// spellings, conversions quoted in the spec), never derived from this
// project's own reader source. Real game data is validated separately by
// tools/validation/validate_tables_progression_population.cpp.
//
// Style follows tests/synthetic_xtbl_test.cpp / synthetic_effects_test.cpp:
// a tiny hand-rolled CHECK-based runner, a running checks-passed count, and
// "N tests passed." on the last line.

#include <iostream>
#include <string>

#include "sr3tables_progression/tables.h"

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

using namespace sr3tables_progression;
using sr3xtbl::Document;
using sr3xtbl::ParseDocument;

Document P(std::string_view s) { return ParseDocument(s); }

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

// ===========================================================================
// stats.xtbl (spec S2)
// ===========================================================================
void testStats() {
    // A normal `percent` row (spec 2.2: the Value_Type child test order, and
    // Percentage_Of_Stat as the percent-class cross-reference).
    const char* xml =
        "<root><Table>"
        "<Stat><Name>pistol hit pct</Name><DisplayName>KEY_PISTOL_HIT</DisplayName>"
        "<Value_Type><percent><Percentage_Of_Stat>pistol shots fired</Percentage_Of_Stat></percent></Value_Type>"
        "<Allow_Update_By_Server>True</Allow_Update_By_Server>"
        "<LivePropertyID>7</LivePropertyID><LiveLeaderboardID>9</LiveLeaderboardID></Stat>"
        "<Stat><Name>time played</Name><Value_Type><time/></Value_Type>"
        "<Allow_Update_By_Server>false</Allow_Update_By_Server></Stat>"
        "</Table></root>";
    Document d = P(xml);
    std::vector<Stat> rows = ParseStatsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "pistol hit pct");
    CHECK(rows[0].displayName.has_value() && *rows[0].displayName == "KEY_PISTOL_HIT");
    CHECK(rows[0].valueKind == StatValueKind::Percent);
    CHECK(rows[0].percentageOfStat.has_value() && *rows[0].percentageOfStat == "pistol shots fired");
    CHECK(rows[0].allowUpdateByServer.present && rows[0].allowUpdateByServer.value == true);
    // "write-only-if-present" (spec 2.2 rows 7-8): both present here.
    CHECK(rows[0].livePropertyId.has_value() && *rows[0].livePropertyId == 7);
    CHECK(rows[0].liveLeaderboardId.has_value() && *rows[0].liveLeaderboardId == 9);

    // Missing optional: LivePropertyID/LiveLeaderboardID absent -> nullopt (not 0).
    CHECK(!rows[1].livePropertyId.has_value());
    CHECK(!rows[1].liveLeaderboardId.has_value());
    CHECK(rows[1].valueKind == StatValueKind::Time);
    CHECK(!rows[1].percentageOfStat.has_value());

    // Value_Type absent entirely -> None (spec 2.2 row 3: "none present" -> handler 0x33/class 6).
    Document d2 = P("<root><Table><Stat><Name>x</Name></Stat></Table></root>");
    Stat s2 = ParseStat(sr3xtbl::FindChild(d2.table(), "Stat"));
    CHECK(s2.valueKind == StatValueKind::None);
    // Allow_Update_By_Server absent: bool-always is deterministic false (xtbl.h: "always (false if absent)").
    CHECK(!s2.allowUpdateByServer.present && s2.allowUpdateByServer.value == false);

    // integer/float/distance/money/boolean/complex all select via first-present-child order.
    Document d3 = P(
        "<root><Table>"
        "<Stat><Name>a</Name><Value_Type><integer/></Value_Type></Stat>"
        "<Stat><Name>b</Name><Value_Type><float/></Value_Type></Stat>"
        "<Stat><Name>c</Name><Value_Type><distance/></Value_Type></Stat>"
        "<Stat><Name>d</Name><Value_Type><money/></Value_Type></Stat>"
        "<Stat><Name>e</Name><Value_Type><boolean/></Value_Type></Stat>"
        "<Stat><Name>f</Name><Value_Type><complex/></Value_Type></Stat>"
        "</Table></root>");
    std::vector<Stat> kinds = ParseStatsTable(d3);
    CHECK(kinds[0].valueKind == StatValueKind::Integer);
    CHECK(kinds[1].valueKind == StatValueKind::Float);
    CHECK(kinds[2].valueKind == StatValueKind::Distance);
    CHECK(kinds[3].valueKind == StatValueKind::Money);
    CHECK(kinds[4].valueKind == StatValueKind::Boolean);
    CHECK(kinds[5].valueKind == StatValueKind::Complex);
}

// ===========================================================================
// achievements.xtbl (spec S3)
// ===========================================================================
void testAchievements() {
    const char* xml =
        "<root><Table>"
        "<Achievement><Name>Dead Presidents</Name><DisplayName>KEY_DP</DisplayName><Image>ach_dp</Image>"
        "<Requirements><Requirement><Stat>cash earned</Stat><Condition>at least</Condition><Value>100000</Value></Requirement>"
        "<Requirement><Stat>unknown stat</Stat><Condition>bogus</Condition><Value>1.5</Value></Requirement></Requirements>"
        "<HudUpdateFrequency>1</HudUpdateFrequency><HudUpdateNumSuppress>True</HudUpdateNumSuppress>"
        "<Avatar_award>some_award</Avatar_award><PC_Only>True</PC_Only><Framework>main</Framework></Achievement>"
        "<Achievement><Name>No Reqs</Name></Achievement>"
        "</Table></root>";
    Document d = P(xml);
    std::vector<Achievement> rows = ParseAchievementsTable(d);
    CHECK(rows.size() == 2);
    const Achievement& a = rows[0];
    CHECK(a.name == "Dead Presidents");
    CHECK(a.displayName.has_value() && *a.displayName == "KEY_DP");
    CHECK(a.image.has_value() && *a.image == "ach_dp");
    CHECK(a.requirements.size() == 2);
    CHECK(a.requirements[0].stat.has_value() && *a.requirements[0].stat == "cash earned");
    CHECK(a.requirements[0].condition.has_value() && *a.requirements[0].condition == RequirementCondition::AtLeast);
    CHECK(a.requirements[0].valueAsInt.has_value() && *a.requirements[0].valueAsInt == 100000);
    // An unrecognised Condition text "leaves the field unset" (spec 3.3).
    CHECK(!a.requirements[1].condition.has_value());
    CHECK(a.requirements[1].valueAsFloat.has_value() && *a.requirements[1].valueAsFloat == 1.5f);
    // "always written" (spec 3.2): present even though this is a normal row.
    CHECK(a.hudUpdateFrequency.present && a.hudUpdateFrequency.value == 1);
    CHECK(a.hudUpdateNumSuppress.present && a.hudUpdateNumSuppress.value == true);
    // Avatar_award: presence-tested only (spec: "the element's text is never used").
    CHECK(a.avatarAwardPresent);
    CHECK(a.framework.has_value() && *a.framework == "main");

    // Missing optional row: DisplayName/Image/Requirements/Framework all absent -> empty/nullopt;
    // HudUpdateFrequency/HudUpdateNumSuppress still "always written" but present=false (element absent).
    const Achievement& b = rows[1];
    CHECK(!b.displayName.has_value() && !b.image.has_value());
    CHECK(b.requirements.empty());
    CHECK(!b.avatarAwardPresent);
    CHECK(!b.framework.has_value());
    CHECK(!b.hudUpdateFrequency.present && b.hudUpdateFrequency.value == 0);
    CHECK(!b.hudUpdateNumSuppress.present && b.hudUpdateNumSuppress.value == false);
}

// ===========================================================================
// unlockables.xtbl (spec S4) - save-hash-verified table (NameHash of Name)
// ===========================================================================
void testUnlockables() {
    // Vehicle type (#0): Type/Vehicle/Vehicles/Vehicle list (spec 4.4 row 0).
    const char* xml =
        "<root><Table>"
        "<Unlockable><Name>Reward_Vehicle_Bundle</Name>"
        "<Type><Vehicle><Vehicles>"
        "<Vehicle><Type>Bootlegger</Type><Variant>Gold</Variant></Vehicle>"
        "<Vehicle><Type>Compensator</Type></Vehicle>"
        "</Vehicles></Vehicle></Type>"
        "<DisplayName>KEY_RV</DisplayName><Description>KEY_RVD</Description>"
        "<Image_Source><Filename>ui_reward_vehicle.tga</Filename></Image_Source>"
        "<Category>Vehicles</Category><Price>5000</Price><Priority>Reward_Vehicle_2</Priority>"
        "<Auto_Unlock>Loudly (Helipad)</Auto_Unlock><Is_DLC>Sometimes</Is_DLC><Framework>dlc1</Framework></Unlockable>"
        // Taunts type (#25): Actions/Animation_Action/Action list.
        "<Unlockable><Name>Reward_Taunt</Name>"
        "<Type><Taunts><Actions><Animation_Action><Action>Wave</Action></Animation_Action>"
        "<Animation_Action><Action>Point</Action></Animation_Action></Actions></Taunts></Type></Unlockable>"
        // Discount type (#4): name/name_2.. shop list.
        "<Unlockable><Name>Reward_Discount</Name>"
        "<Type><Discount><name>Rim Jobs</name><name_2>Impress</name_2><Amount>0.25</Amount></Discount></Type></Unlockable>"
        // Minimal row: no Type child, no Category/Auto_Unlock/Is_DLC -> all defaults.
        "<Unlockable><Name>Plain</Name></Unlockable>"
        "</Table></root>";
    Document d = P(xml);
    std::vector<Unlockable> rows = ParseUnlockablesTable(d);
    CHECK(rows.size() == 4);

    const Unlockable& veh = rows[0];
    CHECK(veh.name == "Reward_Vehicle_Bundle");
    CHECK(veh.typeIndex == 0 && veh.typeName == "Vehicle");
    CHECK(veh.vehicleEntries.size() == 2);
    CHECK(veh.vehicleEntries[0].vehicleName == "Bootlegger");
    CHECK(veh.vehicleEntries[0].variant.has_value() && *veh.vehicleEntries[0].variant == "Gold");
    CHECK(!veh.vehicleEntries[1].variant.has_value()); // optional, absent
    CHECK(veh.category == UnlockableCategory::Vehicles);
    CHECK(veh.price.present && veh.price.value == 5000);
    CHECK(veh.priority.has_value() && *veh.priority == "Reward_Vehicle_2");
    CHECK(veh.autoUnlock == UnlockableAutoUnlock::LoudlyHelipad);
    CHECK(veh.isDlc == UnlockableIsDlc::Sometimes);
    CHECK(veh.framework.has_value() && *veh.framework == "dlc1");
    // spec 4.1: record id = NameHash(lower(Name)) - sanity that the raw name round-trips
    // through the same hash the population harness/save cross-check uses.
    CHECK(sr3xtbl::NameHash(veh.name) == sr3xtbl::NameHash("reward_vehicle_bundle"));

    const Unlockable& taunt = rows[1];
    CHECK(taunt.typeIndex == 25 && taunt.typeName == "Taunts");
    CHECK(taunt.tauntsActions.size() == 2);
    CHECK(taunt.tauntsActions[0] == "Wave" && taunt.tauntsActions[1] == "Point");

    const Unlockable& disc = rows[2];
    CHECK(disc.typeIndex == 4 && disc.typeName == "Discount");
    CHECK(disc.discount.has_value());
    CHECK(disc.discount->shopNames.size() == 2);
    CHECK(disc.discount->shopNames[0] == "Rim Jobs" && disc.discount->shopNames[1] == "Impress");
    CHECK(disc.discount->amount.has_value() && *disc.discount->amount == 0.25f);

    // Defaults: no <Type> -> typeIndex -1 (spec 4.1: such a row is dropped by the real
    // loader; this reader still surfaces it rather than silently skipping).
    const Unlockable& plain = rows[3];
    CHECK(plain.typeIndex == -1 && plain.typeName.empty());
    CHECK(plain.category == UnlockableCategory::PlayerAbilities); // default/unmatched = 0 (spec 4.2)
    CHECK(plain.autoUnlock == UnlockableAutoUnlock::None);         // absent = 0
    CHECK(plain.isDlc == UnlockableIsDlc::False);                  // absent = 0
    CHECK(!plain.framework.has_value());
    CHECK(!plain.priority.has_value());

    // Enum-index table: Category's 10 names, boundary-checking the first and last.
    Document dCat = P(
        "<root><Table>"
        "<Unlockable><Name>a</Name><Category>Player Abilities</Category></Unlockable>"
        "<Unlockable><Name>b</Name><Category>Activities</Category></Unlockable>"
        "<Unlockable><Name>c</Name><Category>Not A Real Category</Category></Unlockable>"
        "</Table></root>");
    std::vector<Unlockable> cats = ParseUnlockablesTable(dCat);
    CHECK(cats[0].category == UnlockableCategory::PlayerAbilities);
    CHECK(cats[1].category == UnlockableCategory::Activities);
    // Unrecognised text is also "default 0 if unmatched" (spec 4.2), same as absent.
    CHECK(cats[2].category == UnlockableCategory::PlayerAbilities);
}

// ===========================================================================
// cheats.xtbl (spec S8) - save-hash-verified table (NameHash of UnlockString)
// ===========================================================================
void testCheats() {
    const char* xml =
        "<root><Table>"
        // AI type.
        "<Cheats><Name>Evil Cars Cheat</Name><UnlockString>evilcars</UnlockString>"
        "<DisplayName>Evil Cars</DisplayName><Cheat_Description>Cars go nuts</Cheat_Description>"
        "<Dont_Flag_As_Cheating>true</Dont_Flag_As_Cheating><Is_DLC>false</Is_DLC>"
        "<Type><AI><Action>Evil Cars</Action></AI></Type></Cheats>"
        // Time type: Set_Hour.
        "<Cheats><Name>Set Hour Cheat</Name><UnlockString>sethour</UnlockString>"
        "<Type><Time><Action><Set_Hour><Hour>13</Hour></Set_Hour></Action></Time></Type></Cheats>"
        // Vehicles type: Drop.
        "<Cheats><Name>Drop Cheat</Name><UnlockString>dropveh</UnlockString>"
        "<Type><Vehicles><Action><Drop><Vehicle_Name>Bootlegger</Vehicle_Name><Vehicle_Variant>Gold</Vehicle_Variant></Drop></Action></Vehicles></Type></Cheats>"
        // Weapon type: Give.
        "<Cheats><Name>Give Cheat</Name><UnlockString>giveweap</UnlockString>"
        "<Type><Weapon><Action><Give><Weapon>rocket_launcher</Weapon><Ammo>10</Ammo></Give></Action></Weapon></Type></Cheats>"
        // Weather type: Set_To.
        "<Cheats><Name>Weather Cheat</Name><UnlockString>wthr</UnlockString>"
        "<Type><Weather><Action><Set_To><Conditions>rain_heavy</Conditions></Set_To></Action></Weather></Type></Cheats>"
        // Minimal row: no Dont_Flag_As_Cheating/Is_DLC -> always-write, present=false.
        "<Cheats><Name>Bare</Name></Cheats>"
        "</Table></root>";
    Document d = P(xml);
    std::vector<Cheat> rows = ParseCheatsTable(d);
    CHECK(rows.size() == 6);

    CHECK(rows[0].name == "Evil Cars Cheat");
    CHECK(rows[0].unlockString.has_value() && *rows[0].unlockString == "evilcars");
    // spec 8.3: save id = NameHash(lower(UnlockString)).
    CHECK(sr3xtbl::NameHash(*rows[0].unlockString) == sr3xtbl::NameHash("EVILCARS"));
    CHECK(rows[0].dontFlagAsCheating.present && rows[0].dontFlagAsCheating.value == true);
    CHECK(rows[0].isDlc.present && rows[0].isDlc.value == false);
    CHECK(rows[0].typeName == "AI");
    CHECK(rows[0].aiAction.has_value() && *rows[0].aiAction == "Evil Cars");

    CHECK(rows[1].typeName == "Time");
    CHECK(rows[1].timeAction.has_value());
    CHECK(rows[1].timeAction->setHour.has_value() && *rows[1].timeAction->setHour == 13);
    CHECK(!rows[1].timeAction->stopTime && !rows[1].timeAction->todCycle);

    CHECK(rows[2].typeName == "Vehicles");
    CHECK(rows[2].vehiclesAction.has_value() && rows[2].vehiclesAction->hasDrop);
    CHECK(*rows[2].vehiclesAction->dropVehicleName == "Bootlegger");
    CHECK(*rows[2].vehiclesAction->dropVehicleVariant == "Gold");
    CHECK(!rows[2].vehiclesAction->infiniteMass);

    CHECK(rows[3].typeName == "Weapon");
    CHECK(rows[3].weaponAction.has_value() && rows[3].weaponAction->hasGive);
    CHECK(*rows[3].weaponAction->giveWeapon == "rocket_launcher");
    CHECK(rows[3].weaponAction->giveAmmo.has_value() && *rows[3].weaponAction->giveAmmo == 10);

    CHECK(rows[4].typeName == "Weather");
    CHECK(rows[4].weatherAction.has_value());
    CHECK(rows[4].weatherAction->setToConditions.has_value() && *rows[4].weatherAction->setToConditions == "rain_heavy");
    CHECK(!rows[4].weatherAction->clear && !rows[4].weatherAction->wrathOfGod);

    // Missing optional: "always write" bools with the element absent -> present=false.
    CHECK(!rows[5].dontFlagAsCheating.present && rows[5].dontFlagAsCheating.value == false);
    CHECK(!rows[5].isDlc.present);
    CHECK(rows[5].typeName.empty());

    // Gameplay_Physics action list boundary: first and last of the 27 (spec 8.2).
    Document dGp = P(
        "<root><Table>"
        "<Cheats><Name>a</Name><Type><Gameplay_Physics><Action>Instant Cash (+$100000)</Action></Gameplay_Physics></Type></Cheats>"
        "<Cheats><Name>b</Name><Type><Gameplay_Physics><Action>Pimps and Hos Peds</Action></Gameplay_Physics></Type></Cheats>"
        "</Table></root>");
    std::vector<Cheat> gp = ParseCheatsTable(dGp);
    CHECK(gp[0].gameplayPhysicsAction.has_value() && *gp[0].gameplayPhysicsAction == kCheatGameplayPhysicsActions[0]);
    CHECK(gp[1].gameplayPhysicsAction.has_value() && *gp[1].gameplayPhysicsAction == kCheatGameplayPhysicsActions[26]);
}

// ===========================================================================
// collectibles.xtbl (spec S9)
// ===========================================================================
void testCollectibles() {
    const char* xml =
        "<root><Table>"
        "<collectible><Name>drug package</Name><Title_Message>KEY_T</Title_Message>"
        "<Respect_Award>500</Respect_Award><Cash_Award>1000.5</Cash_Award>"
        "<Threshold_Rewards>"
        "<Threshold_Reward><Threshold>10</Threshold><Respect_Award>100</Respect_Award><Cash_Award>50</Cash_Award><Unlockable>Reward_A</Unlockable></Threshold_Reward>"
        "<Threshold_Reward><Threshold>20</Threshold><Respect_Award>200</Respect_Award><Cash_Award>100</Cash_Award></Threshold_Reward>"
        "<Threshold_Reward><Threshold>30</Threshold></Threshold_Reward>"
        "<Threshold_Reward><Threshold>40</Threshold></Threshold_Reward>"
        "<Threshold_Reward><Threshold>50</Threshold></Threshold_Reward>"
        "<Threshold_Reward><Threshold>60</Threshold></Threshold_Reward>"
        "<Threshold_Reward><Threshold>70</Threshold></Threshold_Reward>" // 7th: real loader never reads it (cap 6)
        "</Threshold_Rewards></collectible>"
        "<collectible><Name>Money Pallet</Name></collectible>" // missing optionals
        "</Table></root>";
    Document d = P(xml);
    std::vector<Collectible> rows = ParseCollectiblesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "drug package"); // case-insensitive match is the CALLER's job (spec 9.2); raw text kept
    CHECK(rows[0].respectAward.has_value() && *rows[0].respectAward == 500);
    CHECK(rows[0].cashAward.has_value() && *rows[0].cashAward == 1000.5f);
    // "at most 6 kept" (spec 9.2): the 7th Threshold_Reward is not parsed.
    CHECK(rows[0].thresholdRewards.size() == 6);
    CHECK(rows[0].thresholdRewards[0].threshold.value == 10);
    CHECK(rows[0].thresholdRewards[0].unlockable.has_value() && *rows[0].thresholdRewards[0].unlockable == "Reward_A");
    CHECK(!rows[0].thresholdRewards[1].unlockable.has_value());

    // Missing optionals: Title_Message/Subtitle_Message/Respect_Award/Cash_Award all if-present.
    CHECK(!rows[1].titleMessage.has_value());
    CHECK(!rows[1].respectAward.has_value());
    CHECK(!rows[1].cashAward.has_value());
    CHECK(rows[1].thresholdRewards.empty());
}

// ===========================================================================
// respect_levels.xtbl (spec S5) - nested <respect_levels><levels><respect_level>
// ===========================================================================
void testRespectLevels() {
    const char* xml =
        "<root><Table><respect_levels><levels>"
        "<respect_level><Rank>KEY_RANK_1</Rank><Respect>1000</Respect>"
        "<Unlockables><Unlockable>Reward_A</Unlockable><Unlockable>Reward_B</Unlockable></Unlockables></respect_level>"
        "<respect_level><Respect>2000</Respect></respect_level>" // Rank absent, no Unlockables
        "</levels></respect_levels></Table></root>";
    Document d = P(xml);
    std::vector<RespectLevel> rows = ParseRespectLevelsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].rank.has_value() && *rows[0].rank == "KEY_RANK_1");
    CHECK(rows[0].respect.present && rows[0].respect.value == 1000);
    CHECK(rows[0].unlockables.size() == 2 && rows[0].unlockables[1] == "Reward_B");
    CHECK(!rows[1].rank.has_value());
    CHECK(rows[1].unlockables.empty());

    // Missing wrapper entirely: no <respect_levels> -> empty result, not a crash.
    Document empty = P("<root><Table></Table></root>");
    CHECK(ParseRespectLevelsTable(empty).empty());
}

// ===========================================================================
// notoriety.xtbl (spec S6.1) - static 30-name activity index, Check_Detection spelling
// ===========================================================================
void testNotoriety() {
    const char* xml =
        "<root><Table>"
        "<Entry><Name>Carjacking</Name><Data><Activity><Delay>5</Delay>"
        "<Gang><Points>10</Points><Min_Level>0</Min_Level><Max_Level>5</Max_Level><Check_Detection>true</Check_Detection></Gang>"
        "<Police><Points>20</Points><Min_Level>1</Min_Level><Max_Level>6</Max_Level><Check_Detection>yes</Check_Detection></Police>"
        "</Activity></Data></Entry>"
        "<Entry><Name>not a real activity</Name></Entry>" // outside the 30 names: activityIndex == -1
        "</Table></root>";
    Document d = P(xml);
    std::vector<NotorietyEntry> rows = ParseNotorietyTable(d);
    CHECK(rows.size() == 2);
    // "carjacking" is name index 2 (0-based) in the spec's 30-name list.
    CHECK(rows[0].activityIndex == 2);
    CHECK(rows[0].delay.present && rows[0].delay.value == 5);
    CHECK(rows[0].gang.points.value == 10 && rows[0].gang.maxLevel.value == 5);
    // spec 6.1: "true" is the ONLY accepted spelling - "yes" must NOT set checkDetection,
    // unlike sr3xtbl::ParseBool (which accepts both).
    CHECK(rows[0].gang.checkDetection == true);
    CHECK(rows[0].police.checkDetection == false);
    CHECK(rows[1].activityIndex == -1);

    CHECK(std::string(kNotorietyActivityNames[0]) == "armored truck assault");
    CHECK(std::string(kNotorietyActivityNames[29]) == "enter owned store");
}

// ===========================================================================
// notoriety_levels.xtbl (spec S6.2) - two NotorietyLevels sets
// ===========================================================================
void testNotorietyLevels() {
    const char* xml =
        "<root><Table>"
        "<NotorietyLevels><NotorietyLevelsData>"
        "<NotorietyLevelElement><NotorietyLevel>1</NotorietyLevel><NotorietyLevelLimit>100</NotorietyLevelLimit>"
        "<NotorietyDecayRate>5</NotorietyDecayRate><NotorietyFirstDecay>10</NotorietyFirstDecay>"
        "<RoadblockSpawnTimeMin>2</RoadblockSpawnTimeMin><RoadblockSpawnTimeMax>4</RoadblockSpawnTimeMax>"
        "<BruteSpawnTimeMin>6</BruteSpawnTimeMin><BruteSpawnTimeMax>8</BruteSpawnTimeMax></NotorietyLevelElement>"
        "</NotorietyLevelsData></NotorietyLevels>"
        "<NotorietyLevels><NotorietyLevelsData>"
        "<NotorietyLevelElement><NotorietyLevel>1</NotorietyLevel><NotorietyLevelLimit>200</NotorietyLevelLimit></NotorietyLevelElement>"
        "</NotorietyLevelsData></NotorietyLevels>"
        "</Table></root>";
    Document d = P(xml);
    NotorietyLevelsTable t = ParseNotorietyLevelsTable(d);
    CHECK(t.setA.has_value() && t.setA->levels.size() == 1);
    CHECK(t.setA->levels[0].notorietyLevelLimit.value == 100);
    CHECK(t.setA->levels[0].notorietyFirstDecay.value == 10); // raw seconds, x1000 NOT applied here
    CHECK(t.setB.has_value() && t.setB->levels[0].notorietyLevelLimit.value == 200);

    // Only one set present -> setB stays nullopt.
    Document one = P("<root><Table><NotorietyLevels><NotorietyLevelsData/></NotorietyLevels></Table></root>");
    NotorietyLevelsTable t1 = ParseNotorietyLevelsTable(one);
    CHECK(t1.setA.has_value() && !t1.setB.has_value());
}

// ===========================================================================
// difficulty_levels.xtbl (spec S7) - Always<float> caveat on RankN
// ===========================================================================
void testDifficultyLevels() {
    const char* xml =
        "<root><Table>"
        "<Dynamic_Difficulty><Difficulty_Levels>"
        "<Difficulty_Level><Damage_Received_Mult>1.5</Damage_Received_Mult><Notoriety_Decay_Mult>2.0</Notoriety_Decay_Mult>"
        "<Player_Ram_Delay>0.5</Player_Ram_Delay></Difficulty_Level>"
        "</Difficulty_Levels>"
        "<Rank_Occurrence_Percentages>"
        "<Rank_Occurrence_Element><Mission_Name>m1</Mission_Name><Rank1>0.4</Rank1><Rank2>0.3</Rank2></Rank_Occurrence_Element>"
        "</Rank_Occurrence_Percentages></Dynamic_Difficulty>"
        "</Table></root>";
    Document d = P(xml);
    DifficultyLevelsTable t = ParseDifficultyLevelsTable(d);
    CHECK(t.setA.has_value() && !t.setB.has_value());
    CHECK(t.setA->levels.size() == 1);
    CHECK(t.setA->levels[0].damageReceivedMult.value == 1.5f);
    CHECK(t.setA->levels[0].notorietyDecayMult.value == 2.0f); // raw; reciprocal NOT applied here
    CHECK(t.setA->levels[0].playerRamDelaySeconds.value == 0.5f);
    CHECK(t.setA->rankOccurrences.size() == 1);
    CHECK(t.setA->rankOccurrences[0].missionName.has_value() && *t.setA->rankOccurrences[0].missionName == "m1");
    CHECK(t.setA->rankOccurrences[0].rank1.present && t.setA->rankOccurrences[0].rank1.value == 0.4f);
    // Rank3/Rank4 absent: "always" float reader -> present=false (spec: NOT guaranteed to keep the
    // 0.25 preset when the element is absent inside a present Rank_Occurrence_Element).
    CHECK(!t.setA->rankOccurrences[0].rank3.present);
    CHECK(!t.setA->rankOccurrences[0].rank4.present);
}

// ===========================================================================
// store_discounts.xtbl (spec S10.1)
// ===========================================================================
void testStoreDiscounts() {
    const char* xml =
        "<root><Table>"
        "<StoreDiscounts><Name>Rim Jobs</Name><Discounts>"
        "<DiscountElement><DiscountName>rims</DiscountName><Amount>0.3</Amount><Hours>4</Hours><Triggered>0.2</Triggered></DiscountElement>"
        "<DiscountElement><DiscountName>tires</DiscountName></DiscountElement>" // all if-present fields absent
        "</Discounts></StoreDiscounts>"
        "</Table></root>";
    Document d = P(xml);
    std::vector<StoreDiscount> rows = ParseStoreDiscountsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "Rim Jobs");
    CHECK(rows[0].discounts.size() == 2);
    CHECK(rows[0].discounts[0].amount.has_value() && *rows[0].discounts[0].amount == 0.3f);
    CHECK(rows[0].discounts[0].hours.has_value() && *rows[0].discounts[0].hours == 4u);
    // Missing optional: Amount/RadioEvent/Hours/Triggered all "write-only-if-present" (spec 10.1).
    CHECK(!rows[0].discounts[1].amount.has_value());
    CHECK(!rows[0].discounts[1].radioEvent.has_value());
    CHECK(!rows[0].discounts[1].hours.has_value());
    CHECK(!rows[0].discounts[1].triggered.has_value());
}

// ===========================================================================
// gameplay_constants.xtbl (spec S10.2) - spot check across several sections
// ===========================================================================
void testGameplayConstants() {
    const char* xml =
        "<root><Table><Gameplay_Constants>"
        "<panic_fall_velocity>15.0</panic_fall_velocity><FriendlyFirePercentage>50</FriendlyFirePercentage>"
        "<Fat_bones><Fat_Bones_Arm>2</Fat_Bones_Arm><Fat_Bones_Leg>3</Fat_Bones_Leg></Fat_bones>"
        "<Object_Glow_Colors><Weapons><X>1</X><Y>0</Y><Z>0</Z></Weapons></Object_Glow_Colors>"
        "<Combat_AI><Pepperspray><Spray_Min>5</Spray_Min></Pepperspray></Combat_AI>"
        "<Coop_Meta_Game><Reward_Cash>100</Reward_Cash><_Gang_Vehicle>3</_Gang_Vehicle><_Headshot>7</_Headshot></Coop_Meta_Game>"
        "</Gameplay_Constants></Table></root>";
    Document d = P(xml);
    std::optional<GameplayConstants> gc = ParseGameplayConstants(d);
    CHECK(gc.has_value());
    CHECK(gc->panicFallVelocity.value == 15.0f);
    CHECK(gc->friendlyFirePercentage.value == 50.0f); // raw; /100 NOT applied here
    CHECK(gc->fatBones.arm.value == 2.0f && gc->fatBones.leg.value == 3.0f); // raw; x0.1 NOT applied here
    CHECK(gc->objectGlowColors.weapons.x.value == 1.0f && gc->objectGlowColors.weapons.y.value == 0.0f);
    // spec: the loader reads the SAME element (Spray_Min) for both dwords - a Spray_Max
    // element is never consulted; this reader models a single sprayMin field.
    CHECK(gc->combatAi.pepperspray.sprayMin.value == 5u);
    // Underscore-prefixed element names (spec's literal spelling: _Gang_Vehicle, _Headshot, ...).
    CHECK(gc->coopMetaGame.pointsPerGangVehicle.value == 3);
    CHECK(gc->coopMetaGame.pointsPerHeadshot.value == 7);
    CHECK(!gc->coopMetaGame.pointsPerNutshot.present);

    // Gameplay_Constants element entirely absent -> nullopt (no leaves written, spec S10.2).
    Document empty = P("<root><Table></Table></root>");
    CHECK(!ParseGameplayConstants(empty).has_value());
}

// ===========================================================================
// gameplay_nags.xtbl / Gameplay_nag_globals.xtbl (spec S10.3)
// ===========================================================================
void testGameplayNags() {
    Document d = P(
        "<root><Table>"
        "<Nag_Data><Name>sprinting</Name><Increment>2.5</Increment><Nag_Time>10</Nag_Time></Nag_Data>"
        "<Nag_Data><Name>not_a_real_nag</Name></Nag_Data>"
        "</Table></root>");
    std::vector<GameplayNagEntry> rows = ParseGameplayNagsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].nagIndex == 5); // "sprinting" is index 5 (0-based) of the 20 static names
    CHECK(rows[0].incrementSeconds.value == 2.5f);
    CHECK(rows[1].nagIndex == -1);

    Document g = P("<root><Table><Nag_Data><Mission_postpone_s>45</Mission_postpone_s><Nag_display_postpone_s>90</Nag_display_postpone_s></Nag_Data></Table></root>");
    std::optional<GameplayNagGlobals> globals = ParseGameplayNagGlobals(g);
    CHECK(globals.has_value() && globals->missionPostponeSeconds.value == 45 && globals->nagDisplayPostponeSeconds.value == 90);

    Document gEmpty = P("<root><Table></Table></root>");
    CHECK(!ParseGameplayNagGlobals(gEmpty).has_value());
}

// ===========================================================================
// mission_help.xtbl (spec S10.5) - flattening + English/DisplayText fallback
// ===========================================================================
void testMissionHelp() {
    Document d = P(
        "<root><Table>"
        "<String><Texts>"
        "<Text><Name>help1</Name><English>Press A to jump</English><Duration>5</Duration></Text>"
        "<Text><Name>help2</Name><DisplayText>Fallback text</DisplayText><Duration>-1</Duration></Text>"
        "</Texts></String>"
        "<String><Texts><Text><Name>help3</Name></Text></Texts></String>"
        "</Table></root>");
    std::vector<MissionHelpText> rows = ParseMissionHelpTable(d);
    CHECK(rows.size() == 3);
    CHECK(rows[0].english.has_value() && *rows[0].english == "Press A to jump");
    // English absent -> falls back to DisplayText (spec 10.5).
    CHECK(rows[1].english.has_value() && *rows[1].english == "Fallback text");
    CHECK(rows[1].durationSeconds.value == -1.0f); // raw; FLT_MAX substitution NOT applied here
    CHECK(!rows[2].english.has_value());
}

// ===========================================================================
// spawn_info_categories / groups / ranks (spec S10.6) - flag lists + nesting
// ===========================================================================
void testSpawnInfo() {
    Document dc = P(
        "<root><Table>"
        "<Category><Name>Default Spawn</Name>"
        "<Groups><Group><Name>civ_group</Name></Group></Groups>"
        "<Flags><Flag>No Spawn Outside Category</Flag><Flag>Lockdown Area</Flag></Flags>"
        "<CarDay>0.5</CarDay><LawCap>4</LawCap></Category>"
        "</Table></root>");
    std::vector<SpawnCategory> cats = ParseSpawnInfoCategoriesTable(dc);
    CHECK(cats.size() == 1);
    CHECK(cats[0].groups.size() == 1 && *cats[0].groups[0].name == "civ_group");
    CHECK(cats[0].noSpawnOutsideCategory && cats[0].lockdownArea);
    CHECK(!cats[0].noEnemyGangSpawning && !cats[0].lawSpawningArea);
    CHECK(cats[0].carDay.value == 0.5f && cats[0].lawCap.value == 4);

    Document dg = P(
        "<root><Table>"
        "<Group><Name>civ_group</Name>"
        "<GeneralFlags><Flag>unique</Flag><Flag>vehicle_only</Flag></GeneralFlags>"
        "<Vehicles><Vehicle><Name>Bootlegger</Name><Variant>Gold</Variant></Vehicle></Vehicles></Group>"
        "</Table></root>");
    std::vector<SpawnGroup> groups = ParseSpawnInfoGroupsTable(dg);
    CHECK(groups.size() == 1);
    CHECK(groups[0].unique && groups[0].vehicleOnly && !groups[0].hasDesignatedDriver);
    CHECK(groups[0].vehicles.size() == 1 && *groups[0].vehicles[0].name == "Bootlegger");

    Document dr = P(
        "<root><Table>"
        "<Rank><Name>police_thug</Name><type>Law enforcement</type><rank_numeric>1</rank_numeric>"
        "<Hit_Points>100</Hit_Points><Bleed_Out_Hit_Points>50</Bleed_Out_Hit_Points><Knockdown_Points>30</Knockdown_Points>"
        "<per_team_info><per_team_elm><Team>police</Team>"
        "<weapon_loadout><weapon_loadout_elm><chance>50</chance><firearm>pistol</firearm></weapon_loadout_elm></weapon_loadout>"
        "<personality_rotation><personality>aggressive</personality><personality>cautious</personality></personality_rotation>"
        "</per_team_elm></per_team_info></Rank>"
        "</Table></root>");
    std::vector<SpawnRank> ranks = ParseSpawnInfoRanksTable(dr);
    CHECK(ranks.size() == 1);
    CHECK(ranks[0].type.has_value() && *ranks[0].type == "Law enforcement");
    CHECK(ranks[0].hitPoints.value == 100u);
    CHECK(!ranks[0].pctDmgToFlinch.has_value()); // if-present, absent here (spec: "else 0")
    CHECK(ranks[0].perTeamInfo.size() == 1);
    CHECK(ranks[0].perTeamInfo[0].weaponLoadout.size() == 1);
    CHECK(ranks[0].perTeamInfo[0].weaponLoadout[0].chance.value == 50);
    CHECK(*ranks[0].perTeamInfo[0].weaponLoadout[0].firearm == "pistol");
    CHECK(ranks[0].perTeamInfo[0].personalityRotation.size() == 2);
}

// ===========================================================================
// notoriety_spawn.xtbl (spec S6.3, element tree corrected by S14.6) -
// <Table>-nested rows; level_info / group_info / group_details are each ONE
// wrapper whose same-named children are the records (S14.6, CONFIRMED -
// empirical); spawn_flags list. Values are synthetic, not real data.
// ===========================================================================
void testNotorietySpawn() {
    Document d = P(
        "<root><Table>"
        "<Table><Name>police</Name>"
        "<level_info>"
        "<level_info><level>2</level><min_spawn_time>7</min_spawn_time><max_spawn_time>9</max_spawn_time>"
        "<veh_min_spawn_time>3</veh_min_spawn_time><veh_max_spawn_time>4</veh_max_spawn_time>"
        "<max_vehicle_occupants>2</max_vehicle_occupants><max_vehicles>1</max_vehicles><npc_cap>6</npc_cap>"
        "<specialist_cap>1</specialist_cap><brute_cap>0</brute_cap></level_info>"
        "<level_info><level>3</level><min_spawn_time>5</min_spawn_time></level_info>"
        "</level_info>"
        "<group_info>"
        "<group_info><level>2</level><chance>0.5</chance><tag_name>cop1</tag_name><vehicle_name>Bootlegger</vehicle_name>"
        "<variant_name>v1</variant_name></group_info>"
        "<group_info><level>2</level><chance>0.25</chance><tag_name>cop2</tag_name></group_info>"
        "<group_info><level>3</level><chance>1.0</chance><tag_name>cop1</tag_name></group_info>"
        "</group_info>"
        "<group_details>"
        "<group_details><tag_name>cop1</tag_name><seat_name>Driver</seat_name><npc_name>cop_npc</npc_name>"
        "<outside_seat>true</outside_seat></group_details>"
        "<group_details><tag_name>cop1</tag_name><seat_name>Passenger 1</seat_name></group_details>"
        "</group_details>"
        "<override_group>police_motorcycle</override_group>"
        "<spawn_flags><Flag>Spawn on Land</Flag><Flag>Force No Ram</Flag></spawn_flags>"
        "</Table>"
        "</Table></root>");
    std::vector<NotorietySpawnRow> rows = ParseNotorietySpawnTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "police");
    // Inner records only: the wrapper itself is NOT a record.
    CHECK(rows[0].levelInfos.size() == 2);
    if (rows[0].levelInfos.size() == 2) {
        const SpawnLevelInfo& l = rows[0].levelInfos[0];
        CHECK(l.level.present && l.level.value == 2);
        CHECK(l.minSpawnTime.value == 7 && l.maxSpawnTime.value == 9);
        CHECK(l.vehMinSpawnTime.value == 3 && l.vehMaxSpawnTime.value == 4);
        CHECK(l.maxVehicleOccupants.value == 2 && l.maxVehicles.value == 1 && l.npcCap.value == 6);
        CHECK(l.specialistCap.value == 1 && l.bruteCap.present && l.bruteCap.value == 0);
        CHECK(rows[0].levelInfos[1].level.value == 3 && rows[0].levelInfos[1].minSpawnTime.value == 5);
        CHECK(!rows[0].levelInfos[1].npcCap.present);
    }
    CHECK(rows[0].groupInfos.size() == 3);
    if (rows[0].groupInfos.size() == 3) {
        CHECK(rows[0].groupInfos[0].level.value == 2 && rows[0].groupInfos[0].chance.value == 0.5f);
        CHECK(*rows[0].groupInfos[0].vehicleName == "Bootlegger" && *rows[0].groupInfos[0].variantName == "v1");
        CHECK(*rows[0].groupInfos[1].tagName == "cop2" && !rows[0].groupInfos[1].vehicleName);
        CHECK(rows[0].groupInfos[2].level.value == 3);
    }
    CHECK(rows[0].groupDetails.size() == 2);
    if (rows[0].groupDetails.size() == 2) {
        CHECK(*rows[0].groupDetails[0].seatName == "Driver" && *rows[0].groupDetails[0].npcName == "cop_npc");
        CHECK(rows[0].groupDetails[0].outsideSeat && !rows[0].groupDetails[0].meleeBruteSeat);
        CHECK(*rows[0].groupDetails[1].seatName == "Passenger 1");
    }
    CHECK(rows[0].overrideGroup && *rows[0].overrideGroup == "police_motorcycle");
    CHECK(rows[0].spawnOnLand && rows[0].forceNoRam);
    CHECK(!rows[0].spawnOnWater && !rows[0].attackHelicopter);
    CHECK(std::string(kNotorietySpawnGroupNames[0]) == "police");
    CHECK(std::string(kNotorietySpawnGroupNames[23]) == "survival_bums");
}

// S6.3's flat repeated-sibling tree is STRUCK ("do not implement from the block
// below", superseded by S14.6): records placed directly under the row, with no
// wrapper, are not read - no record is synthesised from them.
void testNotorietySpawnStruckFlatTreeNotRead() {
    Document d = P(
        "<root><Table>"
        "<Table><Name>police</Name>"
        "<level_info><level>2</level><min_spawn_time>7</min_spawn_time></level_info>"
        "<level_info><level>3</level><min_spawn_time>5</min_spawn_time></level_info>"
        "<group_info><level>2</level><chance>0.5</chance><tag_name>cop1</tag_name></group_info>"
        "<group_details><tag_name>cop1</tag_name><seat_name>Driver</seat_name></group_details>"
        "</Table>"
        "</Table></root>");
    std::vector<NotorietySpawnRow> rows = ParseNotorietySpawnTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "police");
    CHECK(rows[0].levelInfos.empty());
    CHECK(rows[0].groupInfos.empty());
    CHECK(rows[0].groupDetails.empty());
}

// Only the FIRST wrapper of each name is read (S6.3 corrected tree: "ONE
// wrapper"); a wrapper with no same-named children yields no records.
void testNotorietySpawnFirstWrapperOnly() {
    Document d = P(
        "<root><Table>"
        "<Table><Name>stag</Name>"
        "<level_info><level_info><level>5</level></level_info></level_info>"
        "<level_info><level_info><level>4</level></level_info></level_info>"
        "<group_info></group_info>"
        "</Table>"
        "</Table></root>");
    std::vector<NotorietySpawnRow> rows = ParseNotorietySpawnTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].levelInfos.size() == 1);
    if (rows[0].levelInfos.size() == 1) CHECK(rows[0].levelInfos[0].level.value == 5);
    CHECK(rows[0].groupInfos.empty() && rows[0].groupDetails.empty());
}

// ===========================================================================
// drunk_levels.xtbl (spec S10.7)
// ===========================================================================
void testDrunkLevels() {
    Document d = P(
        "<root><Table>"
        "<Drunk_Levels><Drug_Type>weed</Drug_Type><Max_Booze_Points>100</Max_Booze_Points><Max_Time_Drunk>60</Max_Time_Drunk>"
        "<Levels><Level><Percent_drunk>0.2</Percent_drunk><Sleepy>True</Sleepy></Level>"
        "<Level><Percent_drunk>0.4</Percent_drunk></Level></Levels></Drunk_Levels>"
        "</Table></root>");
    std::vector<DrunkLevelsEntry> rows = ParseDrunkLevelsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].drugType.has_value() && *rows[0].drugType == "weed");
    CHECK(rows[0].levels.size() == 2);
    CHECK(rows[0].levels[0].sleepy.present && rows[0].levels[0].sleepy.value == true);
    // "always write" bool absent -> present=false (not a guaranteed false, per the S1.3 caveat).
    CHECK(!rows[0].levels[1].sleepy.present);
}

// ===========================================================================
// rank_reactions.xtbl (spec S10.8)
// ===========================================================================
void testRankReactions() {
    Document d = P(
        "<root><Table>"
        "<NPCGroup><Name>Police</Name><Reactions>"
        "<ReactionPercentages><PlayerRank>Rank 1 - Thug</PlayerRank><Ignore>10</Ignore><Threaten>90</Threaten></ReactionPercentages>"
        "</Reactions></NPCGroup>"
        "</Table></root>");
    std::vector<NpcGroup> rows = ParseRankReactionsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Police");
    CHECK(rows[0].reactions.size() == 1);
    CHECK(rows[0].reactions[0].ignore.value == 10 && rows[0].reactions[0].threaten.value == 90);
    CHECK(!rows[0].reactions[0].avoid.present); // absent element, always-reader present=false
}

// ===========================================================================
// default_global.xtbl (spec S10.9) - root used directly, real specified defaults
// ===========================================================================
void testDefaultGlobal() {
    Document withValues = P(
        "<root><skybox_mesh_filename>custom_sky</skybox_mesh_filename>"
        "<orbitals><orbital><map_name>moon</map_name></orbital><orbital><map_name></map_name></orbital></orbitals></root>");
    DefaultGlobal g = ParseDefaultGlobal(withValues);
    CHECK(g.skyboxMeshFilename == "custom_sky");
    CHECK(g.cloudMeshFilename == "skybox_clouds"); // absent -> the spec's own literal default
    CHECK(g.orbitalMapNames.size() == 1 && g.orbitalMapNames[0] == "moon"); // empty map_name not kept (spec: "kept only if non-empty")

    Document empty = P("<root></root>");
    DefaultGlobal g2 = ParseDefaultGlobal(empty);
    CHECK(g2.skyboxMeshFilename == "rfg_skybox");
    CHECK(g2.cloudMeshFilename == "skybox_clouds");
    CHECK(g2.orbitalMapNames.empty());
}

// ===========================================================================
// tweak_table.xtbl (spec S10.10) - the one real DLC-validated table of this group
// ===========================================================================
void testTweakTable() {
    // The one real raw DLC row named in spec 10.10 (dlc1_tweak_table.xtbl).
    Document d = P(
        "<root><Table>"
        "<Tweak_Table_Entry><Name>Genki_ball_bounce_timer_ms</Name><Value>3000</Value></Tweak_Table_Entry>"
        "<Tweak_Table_Entry><Name>Other_Tunable</Name><Value>1.5</Value><Description>Not read by the game</Description><Framework>dlc2</Framework></Tweak_Table_Entry>"
        "</Table></root>");
    std::vector<TweakTableEntry> rows = ParseTweakTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "Genki_ball_bounce_timer_ms");
    CHECK(rows[0].value.present && rows[0].value.value == 3000.0f);
    CHECK(!rows[0].framework.has_value());
    CHECK(rows[1].description.has_value() && *rows[1].description == "Not read by the game");
    CHECK(rows[1].framework.has_value() && *rows[1].framework == "dlc2");
}

// ===========================================================================
// metered_sprint.xtbl (spec S10.11)
// ===========================================================================
void testMeteredSprint() {
    Document d = P(
        "<root><Table>"
        "<SprintEntry><Name>Single Player</Name><UseTime>3000</UseTime><RechargeTime>5000</RechargeTime>"
        "<DelayTime>500</DelayTime><PantPercentage>0.8</PantPercentage><JumpPenalty>0.1</JumpPenalty><StartPenalty>0.2</StartPenalty></SprintEntry>"
        "</Table></root>");
    std::vector<SprintEntry> rows = ParseMeteredSprintTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Single Player");
    CHECK(rows[0].useTime.value == 3000 && rows[0].rechargeTime.value == 5000);
    CHECK(rows[0].pantPercentage.value == 0.8f);
}

// ===========================================================================
// activity_types.xtbl (spec S10.12) - flag bytes, optional Disable_flags, extension strip
// ===========================================================================
void testActivityTypes() {
    Document d = P(
        "<root><Table>"
        "<Activity><Name>Heli_Assault</Name><Framework>dlc1</Framework>"
        "<Unlockables><Unlockable>Reward_A</Unlockable></Unlockables>"
        "<Completion_Image><Filename>ui_activity_heli.tga</Filename></Completion_Image>"
        "<Activity_type_flags><Flag>fail on death</Flag><Flag>racing</Flag></Activity_type_flags>"
        "<Disable_flags><Flag>disable HUD</Flag></Disable_flags>"
        "<Soundbank_Name>heli_bank</Soundbank_Name></Activity>"
        "<Activity><Name>Bare</Name></Activity>"
        "</Table></root>");
    std::vector<Activity> rows = ParseActivityTypesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].unlockableNames.size() == 1 && rows[0].unlockableNames[0] == "Reward_A");
    // extension stripped (spec 10.12: "extension stripped, pooled").
    CHECK(rows[0].completionImageName == "ui_activity_heli");
    CHECK(rows[0].typeFlags.failOnDeath && rows[0].typeFlags.racing);
    CHECK(!rows[0].typeFlags.autoAdvanceLevel);
    CHECK(rows[0].disableFlags.has_value() && rows[0].disableFlags->disableHud);
    CHECK(!rows[0].disableFlags->disableCrib);

    // Disable_flags absent -> nullopt ("processed only if the element exists", spec 10.12).
    CHECK(!rows[1].disableFlags.has_value());
    CHECK(rows[1].completionImageName.empty()); // "empty string constant if absent" (spec, a real default)
    CHECK(!rows[1].typeFlags.failOnDeath && !rows[1].typeFlags.racing);
}

} // namespace

int main() {
    run("stats", testStats);
    run("achievements", testAchievements);
    run("unlockables", testUnlockables);
    run("cheats", testCheats);
    run("collectibles", testCollectibles);
    run("respect_levels", testRespectLevels);
    run("notoriety", testNotoriety);
    run("notoriety_levels", testNotorietyLevels);
    run("difficulty_levels", testDifficultyLevels);
    run("store_discounts", testStoreDiscounts);
    run("gameplay_constants", testGameplayConstants);
    run("gameplay_nags", testGameplayNags);
    run("mission_help", testMissionHelp);
    run("spawn_info", testSpawnInfo);
    run("notoriety_spawn", testNotorietySpawn);
    run("notoriety_spawn_struck_flat_tree_not_read", testNotorietySpawnStruckFlatTreeNotRead);
    run("notoriety_spawn_first_wrapper_only", testNotorietySpawnFirstWrapperOnly);
    run("drunk_levels", testDrunkLevels);
    run("rank_reactions", testRankReactions);
    run("default_global", testDefaultGlobal);
    run("tweak_table", testTweakTable);
    run("metered_sprint", testMeteredSprint);
    run("activity_types", testActivityTypes);

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed.\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}
