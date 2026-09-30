// Synthetic tests for sr3tables_diversions, built FROM THE TEXT of
// spec-tables-diversions.md only (hand-rolled XML fixtures as string
// literals) - never derived from this project's own reader source. A pass
// here proves the reader implements the spec's text, not that the spec is
// right; the real-data statement is
// tools/validation/validate_tables_diversions_population.cpp.
//
// One test function per implemented table (22 functions covering all 25
// tables assigned to this group - the activity-text test covers the shared
// shape used by fraud_text/snatch_text/snatch_kinzie_text/escort_text.xtbl
// at once, spec 9). Each covers at least: a normal row, an absent-optional
// element giving nullopt/present==false, and, where the spec calls one out,
// a boundary/quirk value (e.g. stunt_peel_out's Record_Threshold engine
// quirk, cat_and_mouse's one un-converted speed field, stunt_wheels' 3
// nested reward triads).

#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_diversions/tables.h"

namespace {

using namespace sr3tables_diversions;
using namespace sr3xtbl;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ \
                      << "\n";                                                  \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

Document P(std::string_view s) { return ParseDocument(s); }

// sr3xtbl's ParseFloat accumulates fraction digits by repeated single-
// precision x0.1 multiplication (xtbl.h's ParseFloat banner), so a parsed
// "0.9"-style literal is not always bit-identical to the compiler's 0.9f -
// use a tolerance, not ==, for any non-trivial fraction (same convention as
// sr3tables_environment's synthetic test).
bool near(float a, float b, float tol = 1e-5f) { return (a > b ? a - b : b - a) <= tol; }

// ===========================================================================
// 2.2 stunt_drifting.xtbl
// ===========================================================================
void testStuntDrifting() {
    Document d = P(R"(<root><Table>
      <Stunt_Drifting>
        <Min_Time>1.0</Min_Time><Max_Time>10.0</Max_Time>
        <Min_Speed>25.0</Min_Speed>
        <Max_Time_Not_Drifting>2.0</Max_Time_Not_Drifting>
        <Min_Drift_Angle>15.0</Min_Drift_Angle>
        <Record_Display_Time>3.0</Record_Display_Time><Record_Queue_Time>1.0</Record_Queue_Time>
        <Record_Threshold>80.0</Record_Threshold>
        <Max_Respect>500</Max_Respect><Max_Lifetime_Respect>1000</Max_Lifetime_Respect><Max_Cash>2000.0</Max_Cash>
      </Stunt_Drifting>
    </Table></root>)");
    std::optional<StuntDrifting> sd = ParseStuntDriftingTable(d);
    CHECK(sd.has_value());
    CHECK(sd->minTimeSeconds.present && sd->minTimeSeconds.value == 1.0f);
    CHECK(sd->maxTimeSeconds.value == 10.0f);
    CHECK(sd->minSpeedMph.value == 25.0f);
    CHECK(sd->maxTimeNotDriftingSeconds.value == 2.0f);
    CHECK(sd->minDriftAngleDegrees.value == 15.0f);
    CHECK(sd->record.displayTimeSeconds.has_value() && *sd->record.displayTimeSeconds == 3.0f);
    CHECK(sd->record.queueTimeSeconds.has_value() && *sd->record.queueTimeSeconds == 1.0f);
    CHECK(sd->record.thresholdPercent.value == 80.0f);
    CHECK(sd->reward.maxRespect.value == 500 && sd->reward.maxLifetimeRespect.value == 1000);
    CHECK(sd->reward.maxCash.value == 2000.0f);

    // Absent record triad -> the spec-confirmed zero default (spec 1.3), not
    // the general Always-hazard.
    Document d2 = P(R"(<root><Table><Stunt_Drifting><Min_Time>1</Min_Time></Stunt_Drifting></Table></root>)");
    std::optional<StuntDrifting> sd2 = ParseStuntDriftingTable(d2);
    CHECK(sd2.has_value());
    CHECK(!sd2->record.displayTimeSeconds.has_value());
    CHECK(sd2->record.DisplayTimeSecondsOrZero() == 0.0f);
    CHECK(!sd2->record.queueTimeSeconds.has_value());
    // Never-mentioned Always field: Always-hazard, present()==false, 0 stand-in.
    CHECK(!sd2->reward.maxCash.present && sd2->reward.maxCash.value == 0.0f);

    // Missing row entirely.
    Document d3 = P("<root><Table></Table></root>");
    CHECK(!ParseStuntDriftingTable(d3).has_value());
}

// ===========================================================================
// 2.3 stunt_hijacking.xtbl - the ONLY member with no Record_* triad
// ===========================================================================
void testStuntHijacking() {
    Document d = P(R"(<root><Table>
      <Stunt_Hijacking>
        <Hud_Time>5.0</Hud_Time><Cooldown_Time>2.0</Cooldown_Time>
        <Only_Reward_Hijacks>True</Only_Reward_Hijacks>
        <Max_Respect>100</Max_Respect><Max_Lifetime_Respect>200</Max_Lifetime_Respect><Max_Cash>50.0</Max_Cash>
      </Stunt_Hijacking>
    </Table></root>)");
    std::optional<StuntHijacking> sh = ParseStuntHijackingTable(d);
    CHECK(sh.has_value());
    CHECK(sh->hudTimeSeconds.value == 5.0f);
    CHECK(sh->cooldownTimeSeconds.value == 2.0f);
    CHECK(sh->onlyRewardHijacks.present && sh->onlyRewardHijacks.value == true);
    CHECK(sh->reward.maxRespect.value == 100);
}

// ===========================================================================
// 2.4 stunt_near_miss.xtbl - Vehicle_Distance single-element fan-out
// ===========================================================================
void testStuntNearMiss() {
    Document d = P(R"(<root><Table>
      <Near_Miss>
        <Min_Vehicles>1</Min_Vehicles><Max_Vehicles>5</Max_Vehicles>
        <Min_Speed>20.0</Min_Speed><Relative_Speed>10.0</Relative_Speed>
        <Vehicle_Distance>3.5</Vehicle_Distance>
        <Max_Time_Between_Misses>4.0</Max_Time_Between_Misses><Min_Time_Between_Misses>0.5</Min_Time_Between_Misses>
        <Delay_After_Crash>1.0</Delay_After_Crash><Delay_After_Completion>2.0</Delay_After_Completion>
        <Record_Threshold>90</Record_Threshold>
        <Max_Respect>10</Max_Respect><Max_Lifetime_Respect>20</Max_Lifetime_Respect><Max_Cash>30</Max_Cash>
      </Near_Miss>
    </Table></root>)");
    std::optional<StuntNearMiss> nm = ParseStuntNearMissTable(d);
    CHECK(nm.has_value());
    CHECK(nm->vehicleDistance.value == 3.5f);
    CHECK(nm->maxVehicles.value == 5 && nm->minVehicles.value == 1);
    CHECK(nm->delayAfterCrashSeconds.value == 1.0f && nm->delayAfterCompletionSeconds.value == 2.0f);
}

// ===========================================================================
// 2.5 stunt_back_seat_driver.xtbl
// ===========================================================================
void testStuntBackSeatDriver() {
    Document d = P(R"(<root><Table>
      <Back_Seat_Driver>
        <Max_Distance>10.0</Max_Distance><Min_Distance>2.0</Min_Distance>
        <Min_Speed>15.0</Min_Speed><End_Time>8.0</End_Time>
        <Max_Respect>50</Max_Respect><Max_Lifetime_Respect>60</Max_Lifetime_Respect><Max_Cash>70</Max_Cash>
      </Back_Seat_Driver>
    </Table></root>)");
    std::optional<StuntBackSeatDriver> bsd = ParseStuntBackSeatDriverTable(d);
    CHECK(bsd.has_value());
    CHECK(bsd->maxDistance.value == 10.0f && bsd->minDistance.value == 2.0f);
    CHECK(bsd->endTimeSeconds.value == 8.0f);
    CHECK(!bsd->record.displayTimeSeconds.has_value());  // never mentioned
}

// ===========================================================================
// 2.6 stunt_oncoming.xtbl
// ===========================================================================
void testStuntOncoming() {
    Document d = P(R"(<root><Table>
      <Stunt_Oncoming>
        <Max_Distance>20</Max_Distance><Min_Distance>5</Min_Distance>
        <Min_Speed>25</Min_Speed><Max_Damage>10</Max_Damage>
        <Angle_Tolerance>45.0</Angle_Tolerance><Lane_Tolerance>2.5</Lane_Tolerance>
        <Max_Time_Not_Oncoming>1.5</Max_Time_Not_Oncoming>
        <Max_Respect>1</Max_Respect><Max_Lifetime_Respect>2</Max_Lifetime_Respect><Max_Cash>3</Max_Cash>
      </Stunt_Oncoming>
    </Table></root>)");
    std::optional<StuntOncoming> so = ParseStuntOncomingTable(d);
    CHECK(so.has_value());
    // RAW value kept - the [0,180] clamp + "stored as 180-x" transform (spec
    // 1.4/2.6) is explicitly NOT applied by this reader.
    CHECK(so->angleToleranceDegrees.value == 45.0f);
    CHECK(so->laneTolerance.value == 2.5f);
    CHECK(so->maxDamage.value == 10);
}

// ===========================================================================
// 2.7 stunt_peel_out.xtbl - spec 1.5 engine quirk field
// ===========================================================================
void testStuntPeelOut() {
    Document d = P(R"(<root><Table>
      <Peel_Out>
        <Max_Spectators>15</Max_Spectators><Min_Spectators>1</Min_Spectators>
        <Max_Watch_Distance>30.0</Max_Watch_Distance>
        <Instance_Delay>5.0</Instance_Delay>
        <Record_Display_Time>2.0</Record_Display_Time><Record_Queue_Time>1.0</Record_Queue_Time>
        <Record_Threshold>75.0</Record_Threshold>
        <Max_Respect>5</Max_Respect><Max_Lifetime_Respect>6</Max_Lifetime_Respect><Max_Cash>7</Max_Cash>
      </Peel_Out>
    </Table></root>)");
    std::optional<StuntPeelOut> po = ParseStuntPeelOutTable(d);
    CHECK(po.has_value());
    CHECK(po->maxSpectators.value == 15);
    CHECK(po->instanceDelaySeconds.value == 5.0f);
    // The RAW authored value (75.0, authored identically to every sibling
    // table's percent-shaped Record_Threshold) is stored as-is regardless of
    // which downstream conversion applies - spec 1.5.
    CHECK(po->recordThresholdRaw.value == 75.0f);
    CHECK(po->recordDisplayTimeSeconds.has_value() && *po->recordDisplayTimeSeconds == 2.0f);
}

// ===========================================================================
// 2.8 stunt_wheels.xtbl - 3 nested reward triads
// ===========================================================================
void testStuntWheels() {
    Document d = P(R"(<root><Table>
      <Stunt_Wheels>
        <Record_Display_Time>1.0</Record_Display_Time><Record_Threshold>50</Record_Threshold>
        <Two_Wheels>
          <Max_Distance>10</Max_Distance><Min_Distance>1</Min_Distance>
          <Stunt_Tolerance>2.0</Stunt_Tolerance><End_Time>3.0</End_Time>
          <Max_Respect>1</Max_Respect><Max_Lifetime_Respect>2</Max_Lifetime_Respect><Max_Cash>3</Max_Cash>
        </Two_Wheels>
        <Wheelie>
          <Max_Distance>20</Max_Distance><Min_Distance>2</Min_Distance>
          <Stunt_Tolerance>4.0</Stunt_Tolerance><End_Time>5.0</End_Time>
          <Max_Respect>10</Max_Respect><Max_Lifetime_Respect>20</Max_Lifetime_Respect><Max_Cash>30</Max_Cash>
        </Wheelie>
        <Stoppie>
          <Max_Distance>30</Max_Distance><Min_Distance>3</Min_Distance>
          <Stunt_Tolerance>6.0</Stunt_Tolerance><End_Time>7.0</End_Time>
          <Max_Respect>100</Max_Respect><Max_Lifetime_Respect>200</Max_Lifetime_Respect><Max_Cash>300</Max_Cash>
        </Stoppie>
      </Stunt_Wheels>
    </Table></root>)");
    std::optional<StuntWheels> sw = ParseStuntWheelsTable(d);
    CHECK(sw.has_value());
    CHECK(sw->record.displayTimeSeconds.has_value() && *sw->record.displayTimeSeconds == 1.0f);
    CHECK(sw->twoWheels.maxDistance.value == 10 && sw->twoWheels.reward.maxRespect.value == 1);
    CHECK(sw->wheelie.maxDistance.value == 20 && sw->wheelie.reward.maxRespect.value == 10);
    CHECK(sw->stoppie.maxDistance.value == 30 && sw->stoppie.reward.maxRespect.value == 100);
    // Each sub-stunt's own reward triad is independent (three DIFFERENT values).
    CHECK(sw->twoWheels.reward.maxCash.value != sw->wheelie.reward.maxCash.value);
}

// ===========================================================================
// 2.9 stunt_jumping_diversion.xtbl - 3 vehicle types x 6 axes
// ===========================================================================
void testStuntJumpingDiversion() {
    Document d = P(R"(<root><Table>
      <Stunt_Jumping>
        <End_Time>3.0</End_Time>
        <Record_Threshold>60</Record_Threshold>
        <Max_Respect>1</Max_Respect><Max_Lifetime_Respect>2</Max_Lifetime_Respect><Max_Cash>3</Max_Cash>
        <Vehicle_Types>
          <Vehicle_Type>
            <Vehicle_Name>Car</Vehicle_Name>
            <Land_Distance><Max_Value>10</Max_Value><Min_Value>1</Min_Value></Land_Distance>
            <Air_Height><Max_Value>5</Max_Value><Min_Value>0.5</Min_Value></Air_Height>
            <Fall_Dist><Max_Value>8</Max_Value><Min_Value>0.8</Min_Value></Fall_Dist>
            <Pitch_Rotation><Max_Value>90</Max_Value><Min_Value>0</Min_Value><Max_Percent>50</Max_Percent></Pitch_Rotation>
            <Roll_Rotation><Max_Value>45</Max_Value><Min_Value>0</Min_Value></Roll_Rotation>
            <Spin_Rotation><Max_Value>360</Max_Value><Min_Value>0</Min_Value></Spin_Rotation>
          </Vehicle_Type>
          <Vehicle_Type>
            <Vehicle_Name>Motorcycle</Vehicle_Name>
            <Land_Distance><Max_Value>6</Max_Value><Min_Value>0.6</Min_Value></Land_Distance>
            <Air_Height><Max_Value>3</Max_Value><Min_Value>0.3</Min_Value></Air_Height>
            <Fall_Dist><Max_Value>4</Max_Value><Min_Value>0.4</Min_Value></Fall_Dist>
            <Pitch_Rotation><Max_Value>80</Max_Value><Min_Value>0</Min_Value></Pitch_Rotation>
            <Roll_Rotation><Max_Value>40</Max_Value><Min_Value>0</Min_Value></Roll_Rotation>
            <Spin_Rotation><Max_Value>300</Max_Value><Min_Value>0</Min_Value></Spin_Rotation>
          </Vehicle_Type>
          <Vehicle_Type>
            <Vehicle_Name>Boat</Vehicle_Name>
            <Land_Distance><Max_Value>2</Max_Value><Min_Value>0.2</Min_Value></Land_Distance>
            <Air_Height><Max_Value>1</Max_Value><Min_Value>0.1</Min_Value></Air_Height>
            <Fall_Dist><Max_Value>1</Max_Value><Min_Value>0.1</Min_Value></Fall_Dist>
            <Pitch_Rotation><Max_Value>20</Max_Value><Min_Value>0</Min_Value></Pitch_Rotation>
            <Roll_Rotation><Max_Value>10</Max_Value><Min_Value>0</Min_Value></Roll_Rotation>
            <Spin_Rotation><Max_Value>100</Max_Value><Min_Value>0</Min_Value></Spin_Rotation>
          </Vehicle_Type>
        </Vehicle_Types>
      </Stunt_Jumping>
    </Table></root>)");
    std::optional<StuntJumpingDiversion> sj = ParseStuntJumpingDiversionTable(d);
    CHECK(sj.has_value());
    CHECK(sj->vehicleTypes.size() == 3);
    CHECK(sj->vehicleTypes[0].name.has_value() && *sj->vehicleTypes[0].name == "Car");
    CHECK(sj->vehicleTypes[0].landDistance.maxValue.value == 10.0f);
    CHECK(sj->vehicleTypes[0].pitchRotation.maxPercent.has_value() && *sj->vehicleTypes[0].pitchRotation.maxPercent == 50.0f);
    CHECK(!sj->vehicleTypes[0].rollRotation.maxPercent.has_value());  // never mentioned -> nullopt
    CHECK(sj->vehicleTypes[1].name == "Motorcycle");
    CHECK(sj->vehicleTypes[2].name == "Boat");
    CHECK(sj->vehicleTypes[2].spinRotation.maxValue.value == 100.0f);
}

// ===========================================================================
// 2.10 stunt_low_flying.xtbl - non-zero absent-fallback fields
// ===========================================================================
void testStuntLowFlying() {
    Document d = P(R"(<root><Table>
      <Low_Flying>
        <Max_Distance>10</Max_Distance><Min_Distance>1</Min_Distance>
        <Verticality_Mult>1.5</Verticality_Mult>
        <Max_Height>200</Max_Height><Activation_Height>150</Activation_Height>
        <Min_Speed>40</Min_Speed>
        <Max_Time_Not_Low_Flying>2.0</Max_Time_Not_Low_Flying><Completion_Cooldown>5.0</Completion_Cooldown>
        <Max_Respect>1</Max_Respect><Max_Lifetime_Respect>2</Max_Lifetime_Respect><Max_Cash>3</Max_Cash>
      </Low_Flying>
    </Table></root>)");
    std::optional<StuntLowFlying> lf = ParseStuntLowFlyingTable(d);
    CHECK(lf.has_value());
    CHECK(lf->maxHeight.has_value() && *lf->maxHeight == 200.0f);
    CHECK(lf->activationHeight.has_value() && *lf->activationHeight == 150.0f);

    // Absent Max_Height/Activation_Height -> nullopt (this reader does not
    // apply the shared non-zero fallback constant, spec 2.10 / this file's
    // OPEN-constant-value rule).
    Document d2 = P(R"(<root><Table><Low_Flying><Min_Speed>10</Min_Speed></Low_Flying></Table></root>)");
    std::optional<StuntLowFlying> lf2 = ParseStuntLowFlyingTable(d2);
    CHECK(lf2.has_value());
    CHECK(!lf2->maxHeight.has_value());
    CHECK(!lf2->activationHeight.has_value());
}

// ===========================================================================
// 3. barnstorming.xtbl - no Max_Lifetime_Respect (design difference)
// ===========================================================================
void testBarnstorming() {
    Document d = P(R"(<root><Table>
      <Barnstorming>
        <Max_Distance>80</Max_Distance><Min_Speed>60</Min_Speed><Max_Damage>10</Max_Damage>
        <Crash_Delay>1.0</Crash_Delay><Restart_Delay>2.0</Restart_Delay>
        <Knife_Edge_Tolerance>30</Knife_Edge_Tolerance><Knife_Edge_Multiplier>1.5</Knife_Edge_Multiplier>
        <Inverted_Tolerance>30</Inverted_Tolerance><Inverted_Multiplier>2.0</Inverted_Multiplier>
        <Allow_Replay>True</Allow_Replay>
        <Max_Respect>250</Max_Respect><Max_Cash>3000</Max_Cash>
      </Barnstorming>
    </Table></root>)");
    std::optional<Barnstorming> b = ParseBarnstormingTable(d);
    CHECK(b.has_value());
    CHECK(b->maxDistance.has_value() && *b->maxDistance == 80.0f);
    CHECK(b->knifeEdgeToleranceDegrees.value == 30.0f && b->invertedToleranceDegrees.value == 30.0f);
    CHECK(b->allowReplay.present && b->allowReplay.value == true);
    CHECK(b->maxRespect.value == 250 && b->maxCash.value == 3000.0f);

    // Absent Max_Distance -> nullopt (spec: falls back to a shared constant,
    // NOT 0 - this reader does not apply that fallback, only records presence).
    Document d2 = P(R"(<root><Table><Barnstorming><Max_Damage>1</Max_Damage></Barnstorming></Table></root>)");
    CHECK(!ParseBarnstormingTable(d2)->maxDistance.has_value());
}

// ===========================================================================
// 4. base_jumping.xtbl - 4 sub-blocks, Reward_Tiers grid
// ===========================================================================
void testBaseJumping() {
    Document d = P(R"(<root><Table>
      <Base_Jumping>
        <Gen_Info>
          <Start_Time>1.0</Start_Time><Complete_Display_Time>2.0</Complete_Display_Time>
          <Min_Altitude>10</Min_Altitude><Max_Altitude>100</Max_Altitude><Y_Tolerance>5</Y_Tolerance>
          <Record_Threshold>70</Record_Threshold>
        </Gen_Info>
        <Target_Info>
          <Min_Target_Distance>5</Min_Target_Distance><Min_Target_Check_Radius>1</Min_Target_Check_Radius>
          <Max_Target_Distance>50</Max_Target_Distance>
          <Min_Target_Percent>10</Min_Target_Percent><Max_Target_Percent>90</Max_Target_Percent>
          <Disable_Highway_Spawn>True</Disable_Highway_Spawn>
          <Target_Location_Indicator>
            <Indicator_Type>Location</Indicator_Type>
            <Indicator_Flags><Flag>Sticky</Flag><Flag>Display Distance</Flag><Flag>Pulse</Flag></Indicator_Flags>
          </Target_Location_Indicator>
          <Primary_Effect>vfx_red_flare</Primary_Effect>
          <Secondary_Effects><Effect_Name>vfx_smoke</Effect_Name><Num_Effects>2</Num_Effects><Effects_Dist>3.0</Effects_Dist></Secondary_Effects>
        </Target_Info>
        <Veh_Info><Min_Veh_Distance>2</Min_Veh_Distance><Max_Veh_Distance>20</Max_Veh_Distance></Veh_Info>
        <Reward_Info>
          <Max_Lifetime_Respect>500</Max_Lifetime_Respect>
          <Reward_Tiers>
            <Reward_Tier><Max_Distance>6</Max_Distance><Respect>100</Respect><Cash>500</Cash></Reward_Tier>
            <Reward_Tier><Max_Distance>4</Max_Distance><Respect>50</Respect><Cash>250</Cash></Reward_Tier>
            <Reward_Tier><Max_Distance>2</Max_Distance><Respect>10</Respect><Cash>50</Cash></Reward_Tier>
          </Reward_Tiers>
        </Reward_Info>
      </Base_Jumping>
    </Table></root>)");
    std::optional<BaseJumping> bj = ParseBaseJumpingTable(d);
    CHECK(bj.has_value());
    CHECK(bj->genInfoPresent && bj->genInfo.minAltitude.value == 10.0f && bj->genInfo.maxAltitude.value == 100.0f);
    CHECK(bj->genInfo.record.thresholdPercent.value == 70.0f);
    CHECK(bj->targetInfoPresent);
    CHECK(bj->targetInfo.targetLocationIndicatorPresent);
    CHECK(bj->targetInfo.targetLocationIndicator.indicatorType == "Location");
    const uint32_t expectFlags = (1u << 0) | (1u << 1) | (1u << 2);  // Sticky, Display Distance, Pulse
    CHECK(bj->targetInfo.targetLocationIndicator.indicatorFlags == expectFlags);
    CHECK(bj->targetInfo.primaryEffect == "vfx_red_flare");
    CHECK(bj->targetInfo.secondaryEffectsPresent);
    CHECK(bj->targetInfo.secondaryEffects.numEffects.has_value() && *bj->targetInfo.secondaryEffects.numEffects == 2);
    CHECK(bj->vehInfoPresent && bj->vehInfo.minVehDistance.value == 2.0f);
    CHECK(bj->rewardInfoPresent && bj->rewardInfo.maxLifetimeRespect.value == 500);
    CHECK(bj->rewardInfo.rewardTiers.size() == 3);
    CHECK(bj->rewardInfo.rewardTiers[0].maxDistance.value == 6.0f && bj->rewardInfo.rewardTiers[0].respect.value == 100);
    CHECK(bj->rewardInfo.rewardTiers[2].cash.value == 50.0f);

    // Absent sub-block.
    Document d2 = P("<root><Table><Base_Jumping></Base_Jumping></Table></root>");
    std::optional<BaseJumping> bj2 = ParseBaseJumpingTable(d2);
    CHECK(bj2.has_value() && !bj2->genInfoPresent && !bj2->targetInfoPresent);
}

// ===========================================================================
// 5. cat_and_mouse.xtbl - the one un-converted speed field
// ===========================================================================
void testCatAndMouse() {
    Document d = P(R"(<root><Table>
      <Cat_And_Mouse>
        <Vehicle_Matchups>
          <Vehicle_Matchup>
            <Cat_Vehicle>cheetah</Cat_Vehicle><Cat_Vehicle_Health>0</Cat_Vehicle_Health>
            <Mouse_Vehicle>voxel</Mouse_Vehicle><Mouse_Vehicle_Health>0</Mouse_Vehicle_Health>
            <Mouse_Min_Speed>15.0</Mouse_Min_Speed>
          </Vehicle_Matchup>
        </Vehicle_Matchups>
        <Trigger_Hold_Start_Delay>1.0</Trigger_Hold_Start_Delay>
        <Round_Time>60</Round_Time>
        <Rounds_Per_Match>3</Rounds_Per_Match>
        <Cash_Reward>500</Cash_Reward>
        <Mouse_Score_Interval>2.0</Mouse_Score_Interval>
        <Mouse_Interval_Points>10</Mouse_Interval_Points><Mouse_Navpoint_Points>5</Mouse_Navpoint_Points>
        <Mouse_Navpoint_Time_Added_ms>3000</Mouse_Navpoint_Time_Added_ms>
        <Mouse_Navpoint_Start_Distance>50</Mouse_Navpoint_Start_Distance>
        <Mouse_Navpoint_Distance_Increment>10</Mouse_Navpoint_Distance_Increment>
        <Cat_Spawn_Dist>30</Cat_Spawn_Dist>
      </Cat_And_Mouse>
    </Table></root>)");
    std::optional<CatAndMouse> cm = ParseCatAndMouseTable(d);
    CHECK(cm.has_value());
    CHECK(cm->vehicleMatchups.size() == 1);
    CHECK(cm->vehicleMatchups[0].catVehicle == "cheetah" && cm->vehicleMatchups[0].mouseVehicle == "voxel");
    // Mouse_Min_Speed: the ONE speed field in the whole group not converted
    // mph->m/s (spec 1.4/5) - raw value regardless, but this test locks the
    // field name/path down explicitly since it's the group's documented
    // exception.
    CHECK(cm->vehicleMatchups[0].mouseMinSpeedMph.value == 15.0f);
    CHECK(cm->roundTimeSeconds.value == 60.0f);  // f32, no conversion (spec 5)
    CHECK(cm->mouseNavpointTimeAddedMs.present && cm->mouseNavpointTimeAddedMs.value == 3000u);  // u32, no conversion
    CHECK(cm->roundsPerMatch.value == 3);
}

// ===========================================================================
// 6.1 exploration_diversion.xtbl - row tag is "Exploration"
// ===========================================================================
void testExplorationDiversion() {
    Document d = P(R"(<root><Table>
      <Exploration>
        <Max_Exploration_Dist>200</Max_Exploration_Dist><Message_Time>5.0</Message_Time>
        <District_Respect>50</District_Respect><Hood_Respect>25</Hood_Respect>
        <Secret_Location_Respect>10</Secret_Location_Respect><Stunt_Jump_Respect>15</Stunt_Jump_Respect>
        <Secret_Location_Cash>100</Secret_Location_Cash><Stunt_Jump_Cash>150</Stunt_Jump_Cash>
      </Exploration>
    </Table></root>)");
    std::optional<ExplorationDiversion> e = ParseExplorationDiversionTable(d);
    CHECK(e.has_value());
    CHECK(e->maxExplorationDist.value == 200.0f);
    CHECK(e->districtRespect.value == 50 && e->hoodRespect.value == 25);
    CHECK(e->secretLocationCash.value == 100.0f && e->stuntJumpCash.value == 150.0f);

    // The row tag must be "Exploration", NOT "Exploration_Diversion" (spec 6.1).
    Document dWrong = P(R"(<root><Table><Exploration_Diversion><Max_Exploration_Dist>1</Max_Exploration_Dist></Exploration_Diversion></Table></root>)");
    CHECK(!ParseExplorationDiversionTable(dWrong).has_value());
}

// ===========================================================================
// 6.2 mugging_diversion.xtbl
// ===========================================================================
void testMuggingDiversion() {
    Document d = P(R"(<root><Table>
      <Mugging_Diversion>
        <Mugging_Start_Time>1.0</Mugging_Start_Time><Mugging_Complete_Time>3.0</Mugging_Complete_Time>
        <Barter_Line>2.0</Barter_Line><Cower_Line>1.5</Cower_Line>
        <Max_Distance>15.0</Max_Distance>
        <Default_Min_Cash>50</Default_Min_Cash><Default_Max_Cash>200</Default_Max_Cash>
      </Mugging_Diversion>
    </Table></root>)");
    std::optional<MuggingDiversion> m = ParseMuggingDiversionTable(d);
    CHECK(m.has_value());
    CHECK(m->maxDistance.value == 15.0f);  // RAW, NOT squared (spec 6.2 transform not applied)
    CHECK(m->defaultMinCash.value == 50.0f && m->defaultMaxCash.value == 200.0f);
}

// ===========================================================================
// 6.3 streaking.xtbl - Levels grid, existence-gated file
// ===========================================================================
void testStreaking() {
    Document d = P(R"(<root><Table>
      <Streaking>
        <Max_Lifetime_Respect>1000</Max_Lifetime_Respect>
        <Levels>
          <Level><Shock_Quota>5</Shock_Quota><Time_Limit>30.0</Time_Limit><Respect>50</Respect><Cash>100</Cash></Level>
          <Level><Shock_Quota>10</Shock_Quota><Time_Limit>60.0</Time_Limit><Respect>100</Respect><Cash>200</Cash></Level>
        </Levels>
      </Streaking>
    </Table></root>)");
    std::optional<Streaking> s = ParseStreakingTable(d);
    CHECK(s.has_value());
    CHECK(s->maxLifetimeRespect.value == 1000);
    CHECK(s->levels.size() == 2);
    CHECK(s->levels[0].shockQuota.value == 5 && s->levels[0].cash.value == 100.0f);
    CHECK(s->levels[1].respect.value == 100);
}

// ===========================================================================
// 7. photo_op.xtbl - Saints_Loved/Saints_Hated shared shape
// ===========================================================================
void testPhotoOp() {
    Document d = P(R"(<root><Table>
      <Photo_Op>
        <Saints_Loved>
          <Char_Presets><Char_Preset><Preset_Name>npc_ped_saintfan01_mw</Preset_Name></Char_Preset></Char_Presets>
          <Line_Situations><Line_Situation><Line_Situation_Name>Special_Photo_Op</Line_Situation_Name></Line_Situation></Line_Situations>
        </Saints_Loved>
        <Saints_Hated>
          <Char_Presets><Char_Preset><Preset_Name>npc_ped_hater01</Preset_Name></Char_Preset></Char_Presets>
          <Line_Situations><Line_Situation><Line_Situation_Name>Special_Photo_Op</Line_Situation_Name></Line_Situation></Line_Situations>
        </Saints_Hated>
        <Trigger_Effect>VFX_PhotoOpIcon</Trigger_Effect>
      </Photo_Op>
    </Table></root>)");
    std::optional<PhotoOp> p = ParsePhotoOpTable(d);
    CHECK(p.has_value());
    CHECK(p->saintsLoved.charPresetNames.size() == 1 && p->saintsLoved.charPresetNames[0] == "npc_ped_saintfan01_mw");
    CHECK(p->saintsHated.charPresetNames[0] == "npc_ped_hater01");
    CHECK(p->saintsLoved.lineSituationNames[0] == "Special_Photo_Op");
    CHECK(p->triggerEffect == "VFX_PhotoOpIcon");
}

// ===========================================================================
// 11. escort_name_generator.xtbl - existence-gated, Naughty_names grid
// ===========================================================================
void testEscortNameGenerator() {
    Document d = P(R"(<root><Table>
      <Name_Generator>
        <Naughty_names>
          <Naughty_name><First_name>ESCORT_NAME_1A</First_name><Second_name>ESCORT_NAME_1B</Second_name></Naughty_name>
          <Naughty_name><First_name>ESCORT_NAME_2A</First_name><Second_name>ESCORT_NAME_2B</Second_name></Naughty_name>
        </Naughty_names>
      </Name_Generator>
    </Table></root>)");
    std::optional<EscortNameGenerator> g = ParseEscortNameGeneratorTable(d);
    CHECK(g.has_value());
    CHECK(g->naughtyNames.size() == 2);
    CHECK(g->naughtyNames[0].firstName == "ESCORT_NAME_1A" && g->naughtyNames[0].secondName == "ESCORT_NAME_1B");
}

// ===========================================================================
// 12. shop_names.xtbl - genuine multi-row table, concrete defaults
// ===========================================================================
void testShopNames() {
    Document d = P(R"(<root><Table>
      <Shop_Names>
        <Name>Rim Jobs</Name><Localized_Name>{localize}rim_jobs</Localized_Name>
        <Ownership><Cost>15000</Cost><Income>750</Income><Discount>10</Discount><Total_Owner_Discount>25</Total_Owner_Discount><Reward>unlockable_rim_jobs</Reward></Ownership>
        <Use_Message>store_use_msg</Use_Message>
        <Minimap_Icon>icon_rimjobs</Minimap_Icon>
        <Shop_Type>vehicle dealer</Shop_Type>
      </Shop_Names>
      <Shop_Names>
        <Name>No Defaults Shop</Name>
        <Shop_Type>clothing</Shop_Type>
      </Shop_Names>
    </Table></root>)");
    std::vector<ShopName> rows = ParseShopNamesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "Rim Jobs");
    CHECK(rows[0].cost.has_value() && *rows[0].cost == 15000);
    CHECK(rows[0].discount.has_value() && *rows[0].discount == 10.0f);
    CHECK(rows[0].reward == "unlockable_rim_jobs");
    CHECK(rows[0].shopType == "vehicle dealer");

    // Row 2: every optional/default field absent -> nullopt, OrDefault()
    // helpers give the spec-stated concrete defaults (spec 12).
    CHECK(!rows[1].localizedName.has_value());
    CHECK(rows[1].LocalizedNameOrDefault() == "{localize}franchise");
    CHECK(!rows[1].cost.has_value());
    CHECK(rows[1].CostOrDefault() == 10000);
    CHECK(rows[1].IncomeOrDefault() == 500);
    CHECK(rows[1].DiscountOrDefault() == 15.0f);
    CHECK(rows[1].TotalOwnerDiscountOrDefault() == 20.0f);

    // kShopTypeNames covers the 8 schema choices exactly (spec 12).
    CHECK(kShopTypeNames.size() == 8);
    CHECK(kShopTypeNames[0] == "clothing" && kShopTypeNames[7] == "misc property");
}

// ===========================================================================
// 9. Activity-text descriptor registry data shape (fraud_text.xtbl /
// snatch_text.xtbl / snatch_kinzie_text.xtbl / escort_text.xtbl, spec 9) -
// all four share this exact empirically-confirmed shape.
// ===========================================================================
void testActivityText() {
    Document d = P(R"(<root><Table>
      <String>
        <Name>Escort</Name>
        <Texts>
          <Text><Name>ESCORT_RECRUIT</Name><English>Recruit text</English><DisplayText>ESCORT_RECRUIT_KEY</DisplayText><Duration>4.0</Duration></Text>
          <Text><Name>ESCORT_FAIL</Name><DisplayText>ESCORT_FAIL_KEY</DisplayText></Text>
        </Texts>
      </String>
    </Table></root>)");
    std::optional<ActivityTextTable> t = ParseActivityTextTable(d);
    CHECK(t.has_value());
    // spec 9: row-name literal is "Escort" even for the Snatch-family files.
    CHECK(t->rowName == "Escort");
    CHECK(t->texts.size() == 2);
    CHECK(t->texts[0].name == "ESCORT_RECRUIT" && t->texts[0].english == "Recruit text");
    CHECK(t->texts[0].duration.has_value() && *t->texts[0].duration == 4.0f);
    // Second Text: English absent, Duration absent -> spec-stated default 5.0.
    CHECK(!t->texts[1].english.has_value());
    CHECK(!t->texts[1].duration.has_value());
    CHECK(t->texts[1].DurationOrDefault() == 5.0f);
}

// ===========================================================================
// 8.2 horde_mode_text.xtbl - a DIFFERENT, flatter shape (spec 8.2)
// ===========================================================================
void testHordeModeText() {
    Document d = P(R"(<root><Table>
      <Horde_Mode_Identifier><Name>HORDE_MODE_WAVE_TAG</Name></Horde_Mode_Identifier>
      <Horde_Mode_Identifier><Name>HORDE_MODE_TITLE_COMPLETE</Name></Horde_Mode_Identifier>
    </Table></root>)");
    std::vector<HordeModeIdentifier> rows = ParseHordeModeTextTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "HORDE_MODE_WAVE_TAG");
    CHECK(rows[1].name == "HORDE_MODE_TITLE_COMPLETE");
}

// ===========================================================================
// 10. fraud_globals.xtbl - the 12x-repeated tiered-multiplier shape
// ===========================================================================
void testFraudGlobals() {
    // Element names below (Minimum_Vehicles plural, Windshield's
    // "_Cannon"-on-the-inner-element-only quirk) match the REAL shipped
    // fraud_globals.xtbl exactly, confirmed via
    // tools/validation/validate_tables_diversions_population.cpp against
    // real game DATA - see the long comment on FraudMultiplierCategory in
    // tables.h for why this deviates from spec 10's own general-formula prose.
    Document d = P(R"(<root><Table>
      <Fraud_Values>
        <Vehicle_Multiplier><Multiplier_Data><Minimum_Vehicles>1</Minimum_Vehicles><Multiplier_Min>1.0</Multiplier_Min><Multiplier_Max>2.0</Multiplier_Max><Per_Multiplier>0.5</Per_Multiplier></Multiplier_Data></Vehicle_Multiplier>
        <Vehicle_Multiplier_Element>
          <Cash_Instead>True</Cash_Instead>
          <Vehicle_Multiplier>
            <Multiplier_Data><Minimum_Vehicles>1</Minimum_Vehicles><Multiplier_Min>1.0</Multiplier_Min><Multiplier_Max>1.5</Multiplier_Max><Per_Multiplier>0.1</Per_Multiplier></Multiplier_Data>
            <Multiplier_Data><Minimum_Vehicles>5</Minimum_Vehicles><Multiplier_Min>1.5</Multiplier_Min><Multiplier_Max>2.0</Multiplier_Max><Per_Multiplier>0.2</Per_Multiplier></Multiplier_Data>
          </Vehicle_Multiplier>
        </Vehicle_Multiplier_Element>
        <Windshield_Multiplier_Element>
          <Cash_Instead></Cash_Instead>
          <Windshield_Cannon_Multiplier>
            <Multiplier_Data><Minimum_Windshield_Cannon>1</Minimum_Windshield_Cannon><Multiplier_Min>500</Multiplier_Min><Multiplier_Max>5000</Multiplier_Max><Per_Multiplier>225</Per_Multiplier></Multiplier_Data>
          </Windshield_Cannon_Multiplier>
        </Windshield_Multiplier_Element>
        <Witness_Multiplier><Multiplier_Data><Minimum_Witnesses>3</Minimum_Witnesses><Multiplier_Min>1.0</Multiplier_Min><Multiplier_Max>2.0</Multiplier_Max><Per_Multiplier>0.25</Per_Multiplier></Multiplier_Data></Witness_Multiplier>
        <Body_Multipliers><Body><Head>2.0</Head><Chest>1.0</Chest><Groin>1.5</Groin><Arm>0.8</Arm><Leg>0.9</Leg></Body></Body_Multipliers>
        <Luxury_Cars><Vehicles><Vehicle>bulldog</Vehicle><Vehicle>attrazione</Vehicle></Vehicles></Luxury_Cars>
        <Adrenaline_Grid><Adrenaline_Element><Cash>100</Cash><Bonus>1.1</Bonus></Adrenaline_Element></Adrenaline_Grid>
        <Damage_Multiplier>1.5</Damage_Multiplier>
        <Money_Conversion>2.0</Money_Conversion>
        <Min_Money>10</Min_Money>
        <News_Van_Type>news_van</News_Van_Type>
        <Witness_Bonus_Radius>25</Witness_Bonus_Radius>
        <On_Fire_Bonus>1.2</On_Fire_Bonus>
      </Fraud_Values>
    </Table></root>)");
    std::optional<FraudGlobals> f = ParseFraudGlobalsTable(d);
    CHECK(f.has_value());
    CHECK(f->vehicle.flatPresent);
    CHECK(f->vehicle.flat.minimum.value == 1.0f && f->vehicle.flat.multiplierMax.value == 2.0f);
    CHECK(f->vehicle.tieredElementPresent);
    CHECK(f->vehicle.cashInstead.present && f->vehicle.cashInstead.value == true);
    CHECK(f->vehicle.tiers.size() == 2);
    CHECK(f->vehicle.tiers[0].minimum.value == 1.0f && f->vehicle.tiers[1].minimum.value == 5.0f);
    // A category never mentioned at all: both forms absent.
    CHECK(!f->airtime.flatPresent && !f->airtime.tieredElementPresent);
    // Windshield: the "_Cannon"-only-on-the-inner-element real-data
    // irregularity (see tables.h) - both wrapper and inner element parse
    // correctly despite the name mismatch between them.
    CHECK(f->windshield.tieredElementPresent);
    CHECK(f->windshield.tiers.size() == 1 && f->windshield.tiers[0].minimum.value == 1.0f);
    CHECK(f->windshield.tiers[0].multiplierMax.value == 5000.0f);
    // Witness: flat form only (a category confirmed, in real data, to
    // sometimes carry only the flat scheme - spec 10's "all twelve
    // simultaneously" claim does not hold universally, see tables.h).
    CHECK(f->witness.flatPresent && !f->witness.tieredElementPresent);
    CHECK(f->witness.flat.minimum.value == 3.0f);
    CHECK(f->bodyMultipliersPresent);
    CHECK(f->bodyMultipliers.head.value == 2.0f && near(f->bodyMultipliers.leg.value, 0.9f));
    CHECK(f->luxuryCars.size() == 2 && f->luxuryCars[0] == "bulldog");
    CHECK(f->adrenalineGrid.size() == 1 && f->adrenalineGrid[0].cash.value == 100.0f);
    CHECK(f->newsVanType == "news_van");
    CHECK(near(f->onFireBonus.value, 1.2f));
}

// ===========================================================================
// 8.1 horde_mode.xtbl - the group's largest/richest schema
// ===========================================================================
void testHordeMode() {
    Document d = P(R"(<root><Table>
      <Horde_Mode>
        <UI_Settings><Floating_Points><Intensity>1.0</Intensity><Scale>2.0</Scale><Location_Scale>0.5</Location_Scale></Floating_Points></UI_Settings>
        <Wave_Failsafe_Completion_Delay_ms>30000</Wave_Failsafe_Completion_Delay_ms>
        <Wave_Failsafe_Completion_Min_Enemy_Kills_Pct>90</Wave_Failsafe_Completion_Min_Enemy_Kills_Pct>
        <Wave_Failsafe_Completion_No_Enemies_Radius>50</Wave_Failsafe_Completion_No_Enemies_Radius>
        <Levels>
          <Level><Unique_ID>zombie_area</Unique_ID><xtbl><Filename>zombie_area.xtbl</Filename></xtbl><display_name>ZOMBIE</display_name></Level>
        </Levels>
        <Player_Characters>
          <Player_Character>
            <Name>Cyril</Name><display_name>CYRIL_DISPLAY</display_name>
            <Customization>
              <Gender>Male</Gender><Race>White</Race><Figure>Heavy</Figure>
              <Persona>cyril_persona</Persona><Outfit>cyril_outfit</Outfit>
              <Customization_Items>
                <Customization_Item>
                  <Customization_Item>cm_lpg_mask_m_maskdog</Customization_Item>
                  <Custom_Colors><Custom_Color><Color_Name>Browns7</Color_Name></Custom_Color><Custom_Color><Color_Name>Browns6</Color_Name></Custom_Color></Custom_Colors>
                </Customization_Item>
              </Customization_Items>
            </Customization>
            <Sprint_Time_Seconds>5.0</Sprint_Time_Seconds><Health>100</Health><Health_Recharge_Rate>1.0</Health_Recharge_Rate>
            <Default_Weapons>
              <Default_Weapon><Name>pistol</Name><Upgraded_Level>1</Upgraded_Level></Default_Weapon>
            </Default_Weapons>
            <Lines><Death>cyril_death</Death><Wave_Victory>cyril_wave</Wave_Victory><Level_Victory>cyril_level</Level_Victory></Lines>
          </Player_Character>
        </Player_Characters>
        <Cooperative_Multiplayer_Constants>
          <Enemy_Health>1.0</Enemy_Health><Enemy_Damage>1.0</Enemy_Damage>
          <Multipliers><Enemy_Health>1.5</Enemy_Health><Enemy_Damage>1.2</Enemy_Damage></Multipliers>
          <Revive_Limit>3</Revive_Limit><Revive_Reset_Wave_Interval>2</Revive_Reset_Wave_Interval>
        </Cooperative_Multiplayer_Constants>
        <Special_Spawn_Conditions>
          <Special_Spawn_Condition><Name>cond_kills</Name><Data><Kills><Repeat>True</Repeat><Count>50</Count></Kills></Data></Special_Spawn_Condition>
          <Special_Spawn_Condition><Name>cond_wave</Name><Data><Wave_Number><Repeat>False</Repeat><Number>3</Number></Wave_Number></Data></Special_Spawn_Condition>
        </Special_Spawn_Conditions>
        <Powerups>
          <Powerup><Type>Invulnerability</Type><Item_Name>item_invuln</Item_Name></Powerup>
          <Powerup><Type>Weapon Upgrade</Type><Item_Name>item_upgrade</Item_Name><Audio_Cue>cue_upgrade</Audio_Cue></Powerup>
        </Powerups>
        <Enemy_Data>
          <Default_AI_Settings><Personality>zombie_personality</Personality><Behavior>zombie_behavior</Behavior></Default_AI_Settings>
          <Point_Values><Point_Value><Name>zombie_basic</Name><Value>10</Value></Point_Value></Point_Values>
        </Enemy_Data>
        <Point_Multipliers>
          <Weapons><Weapon><Name>pistol</Name><Point_Multiplier>1.5</Point_Multiplier></Weapon></Weapons>
          <Melee_Point_Multiplier>2.0</Melee_Point_Multiplier>
        </Point_Multipliers>
      </Horde_Mode>
    </Table></root>)");
    std::optional<HordeMode> h = ParseHordeModeTable(d);
    CHECK(h.has_value());
    CHECK(h->floatingPoints.intensity.value == 1.0f && h->floatingPoints.scale.value == 2.0f);
    CHECK(h->waveFailsafeCompletionDelayMs.value == 30000);  // read directly, NO seconds->ms conversion
    CHECK(h->levels.size() == 1 && h->levels[0].filename == "zombie_area.xtbl");
    CHECK(h->playerCharacters.size() == 1);
    const HordeModePlayerCharacter& pc = h->playerCharacters[0];
    CHECK(pc.name == "Cyril" && pc.customizationPresent);
    CHECK(pc.customization.gender == "Male" && pc.customization.persona == "cyril_persona");
    CHECK(pc.customization.customizationItemsPresent);
    CHECK(pc.customization.customizationItems.size() == 1);
    CHECK(pc.customization.customizationItems[0].itemName == "cm_lpg_mask_m_maskdog");
    CHECK(pc.customization.customizationItems[0].customColorNames.size() == 2);
    CHECK(pc.customization.customizationItems[0].customColorNames[0] == "Browns7");
    CHECK(pc.defaultWeapons.size() == 1 && pc.defaultWeapons[0].name == "pistol" && pc.defaultWeapons[0].upgradedLevel.value == 1);
    CHECK(pc.lines.death == "cyril_death");
    CHECK(h->coopMultiplayerConstantsPresent);
    CHECK(h->coopMultiplayerConstants.base.enemyHealth.value == 1.0f);
    CHECK(h->coopMultiplayerConstants.multipliers.enemyHealth.value == 1.5f);
    CHECK(h->coopMultiplayerConstants.reviveLimit.value == 3);
    CHECK(h->specialSpawnConditions.size() == 2);
    CHECK(h->specialSpawnConditions[0].variant == "Kills" && h->specialSpawnConditions[0].data.count.value == 50);
    CHECK(h->specialSpawnConditions[1].variant == "Wave_Number" && h->specialSpawnConditions[1].data.number.value == 3);
    CHECK(h->powerups.size() == 2);
    CHECK(h->powerups[0].type == "Invulnerability");
    CHECK(h->powerups[1].type == "Weapon Upgrade" && h->powerups[1].audioCue == "cue_upgrade");
    CHECK(kHordeModePowerupTypeNames.size() == 4);
    CHECK(h->enemyDataPresent);
    CHECK(h->enemyData.defaultAiSettings.personality == "zombie_personality");
    CHECK(h->enemyData.pointValues.size() == 1 && h->enemyData.pointValues[0].points.value == 10);
    CHECK(h->pointMultipliersPresent);
    CHECK(h->pointMultipliers.weapons.size() == 1 && h->pointMultipliers.weapons[0].multiplier.value == 1.5f);
    CHECK(h->pointMultipliers.meleePointMultiplier.value == 2.0f);
}

}  // namespace

int main() {
    testStuntDrifting();
    testStuntHijacking();
    testStuntNearMiss();
    testStuntBackSeatDriver();
    testStuntOncoming();
    testStuntPeelOut();
    testStuntWheels();
    testStuntJumpingDiversion();
    testStuntLowFlying();
    testBarnstorming();
    testBaseJumping();
    testCatAndMouse();
    testExplorationDiversion();
    testMuggingDiversion();
    testStreaking();
    testPhotoOp();
    testEscortNameGenerator();
    testShopNames();
    testActivityText();
    testHordeModeText();
    testFraudGlobals();
    testHordeMode();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
