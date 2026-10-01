// Synthetic tests for sr3vehicleinfo (spec-vehicle-data.md section 7). Every
// fixture below is a hand-built XML string transcribed from the SPEC TEXT
// (section/offset cited per test), never derived from this project's own
// reader source - a pass here proves the reader implements what the spec
// says, not that the spec is right. The real-data cross-check is
// tools/validation/validate_vehicleinfo_population.cpp.
//
// Style follows this project's synthetic-suite convention (see
// tests/synthetic_tables_weapons_test.cpp): a tiny hand-rolled CHECK macro,
// a running pass count, "<N> tests passed." on the last line.
//
// Mutation-check note (this project's process, measurement-discipline.md):
// at least 3 of the CHECKs below were confirmed to actually catch a broken
// reader by hand, before this file was finalised (each done by temporarily
// editing src/vehicle_entry.cpp, rebuilding, confirming the expected CHECK
// failed, then reverting):
//   1. testSpeedCapAndNitrousRaise: temporarily removed the "if (mph >
//      kMaxSpeedCapMph) mph = kMaxSpeedCapMph;" cap in ReadMphAsMsAlways ->
//      `CHECK(near(e.maximumSpeedMs.value, kMaxSpeedCapMs, 1e-4f))` failed
//      (uncapped 150mph*0.44704 != the capped 44.704 the test expects);
//      reverted.
//   2. testAiMaxSpeedSteeringNotConverted: temporarily made
//      aiMaxSpeedSteeringRaw go through ReadMphAsMsAlways instead of plain
//      ReadFloatAlways -> `CHECK(e.aiMaxSpeedSteeringRaw.value == 60.0f)`
//      failed (got 60*0.44704 instead); reverted.
//   3. testMotorcycleVariant's Range_Decay rounding: temporarily changed
//      `std::lround(raw.value)` to a plain `(int32_t)raw.value` (truncation)
//      in ParseMotorcycle -> `CHECK(mc.wheelie.rangeDecay.value == 3)` (from
//      authored 2.6) failed (truncation gives 2); reverted.

#include <cmath>
#include <iostream>
#include <string>

#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3xtbl/xtbl.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) {                                                                     \
            ++g_passed;                                                                 \
        } else {                                                                        \
            ++g_failed;                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
        }                                                                               \
    } while (0)

using namespace sr3xtbl;
using namespace sr3vehicleinfo;

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

Document P(std::string_view s) { return ParseDocument(s); }

const Node* FirstVehicle(const Document& doc) { return FindChild(doc.table(), "Vehicle"); }

// ---------------------------------------------------------------------------
// A normal Automobile entry - a "kitchen sink" row exercising most of
// section 7.3-7.6's Automobile-relevant sub-blocks.
// ---------------------------------------------------------------------------
void testAutomobileNormalRow() {
    // Field placement (which fields sit inside which wrapper element) below
    // follows tools/validation/validate_vehicleinfo_population.cpp's G5
    // real-data findings (Engine/Transmission/Steering/Aerodynamics/
    // Angular_Damping/Effects/Audio_Onload_Parameters wrappers,
    // NoCustVariantsGrid/NoCustVariantElement/Variant, and the
    // Upgrades/Value|Cost/Levels/Level_N shape) - NOT from the spec text
    // itself, which gives destination offsets but not these XML parents;
    // see src/vehicle_entry.cpp's file banner for the full citation.
    const char* xml =
        "<root><Table><Vehicle>"
        "<Name>sp_test01_car</Name>"
        "<Is_DLC>false</Is_DLC>"
        "<ID>42</ID>"
        "<Display_Name>Test Car</Display_Name>"
        "<Bitmap_Name><Filename>test_car_bitmap</Filename></Bitmap_Name>"
        "<Hostage_Vehicle_Class>SUV</Hostage_Vehicle_Class>"
        "<Group>car_standard</Group>"
        "<Max_Hitpoints>500</Max_Hitpoints>"
        "<Mass>1500.5</Mass>"
        "<Value>10000</Value>"
        "<LOD_DistanceRatio><Element>0.1</Element><Element>0.5</Element><Element>1.0</Element></LOD_DistanceRatio>"
        "<Special_Vehicle_Type>Taxi</Special_Vehicle_Type>"
        "<Road_Preference>Highway</Road_Preference>"
        "<Engine><Torque>400</Torque><Min_RPM>1000</Min_RPM><Opt_RPM>4000</Opt_RPM><Max_RPM>6000</Max_RPM><Nitro_Capable>yes</Nitro_Capable></Engine>"
        "<Axles>"
        "<Axle><Wheel_Does_Steer>yes</Wheel_Does_Steer><Wheel_Friction>0.9</Wheel_Friction></Axle>"
        "<Axle><Wheel_Does_Handbrake>yes</Wheel_Does_Handbrake><Wheel_Friction>0.8</Wheel_Friction></Axle>"
        "<Axle><Wheel_Friction>0.5</Wheel_Friction></Axle>"  // 3rd axle: must be ignored (capacity 2)
        "</Axles>"
        "<Transmission>"
        "<Gear_Ratios><Element>3.5</Element><Element>2.1</Element><Element>1.4</Element><Element>1.0</Element></Gear_Ratios>"
        "<Downshift_RPMs><Element>2000</Element><Element>2500</Element><Element>3000</Element></Downshift_RPMs>"
        "</Transmission>"
        "<Steering><Max_Steering_Angle>35</Max_Steering_Angle><Max_Speed_Steering>25</Max_Speed_Steering></Steering>"
        "<Towable_Data><Towable_From_Rear>yes</Towable_From_Rear>"
        "<Tweaking_Offset_Front><X>1</X><Y>2</Y><Z>3</Z></Tweaking_Offset_Front></Towable_Data>"
        "<Audio_Hysteresis><Audio_Rpm_Decay>0.5</Audio_Rpm_Decay>"
        "<RPM_Deviation><start_time>1</start_time><min_duration>2</min_duration><max_duration>3</max_duration></RPM_Deviation>"
        "</Audio_Hysteresis>"
        "<Audio_Onload_Parameters><Medium_Onload_Threshold>0.4</Medium_Onload_Threshold><High_Onload_Threshold>0.7</High_Onload_Threshold></Audio_Onload_Parameters>"
        "<Foley><Engine>7</Engine><Gear_Shift>snd_gear_shift</Gear_Shift></Foley>"
        "<Angular_Damping><Normal_Spin_Damping>2</Normal_Spin_Damping></Angular_Damping>"
        "<Effects><Engine_Smoke>fx_smoke</Engine_Smoke><Exhaust_All_Time>Yes</Exhaust_All_Time>"
        "<Exhaust_Types><Exhaust_Type><Type>normal</Type><Exhaust_Accelerator>fx_exh_a</Exhaust_Accelerator>"
        "<Exhaust_No_Accelerator>fx_exh_na</Exhaust_No_Accelerator></Exhaust_Type></Exhaust_Types></Effects>"
        "<Trailer_Hitch_Type>Semi</Trailer_Hitch_Type>"
        "<Maximum_Speed>85</Maximum_Speed>"
        "<Maximum_Speed_With_Nitrous>95</Maximum_Speed_With_Nitrous>"
        "<Wheel_Size_Element><Wheel_Min_Size_0>15</Wheel_Min_Size_0></Wheel_Size_Element>"
        "<ProhibitedGunfireSeats><Flag>Front Driver Side</Flag><Flag>Rear Passenger Side</Flag></ProhibitedGunfireSeats>"
        "<Vehicle_Interaction_Info>car_interaction</Vehicle_Interaction_Info>"
        "<Character_Animation_Set>car_charanim</Character_Animation_Set>"
        "<Vehicle_Animation_Set_Name>car_animset.xtbl</Vehicle_Animation_Set_Name>"
        "<Vehicle_Animation_Rig_Name>car_rig.rigx</Vehicle_Animation_Rig_Name>"
        "<NoCustVariantsGrid><NoCustVariantElement><Variant>Sports_T</Variant></NoCustVariantElement></NoCustVariantsGrid>"
        "<ParkingSpawn>1</ParkingSpawn>"
        "<Heavy>yes</Heavy>"
        "<Open_Doors_Automatically>yes</Open_Doors_Automatically>"
        "<Upgrades><Torque><Value><Levels><Level_1>1.2</Level_1></Levels></Value><Cost><Levels><Level_1>500</Level_1></Levels></Cost></Torque></Upgrades>"
        "<Vehicle_Type><Automobile>"
        "<Drifting_Possible>No</Drifting_Possible>"
        "<Hydraulics_Capable>yes</Hydraulics_Capable>"
        "<AutomobileFlags><Flag>Is Tank</Flag><Flag>No Treads</Flag></AutomobileFlags>"
        "<Air_Control><Max_Roll_Speed>90</Max_Roll_Speed></Air_Control>"
        "</Automobile></Vehicle_Type>"
        "</Vehicle></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    const Node* row = FirstVehicle(doc);
    CHECK(row != nullptr);
    VehicleEntry e = ParseVehicleEntry(row);

    CHECK(e.name.has_value() && *e.name == "sp_test01_car");
    CHECK(e.nameHash == NameHash("sp_test01_car"));
    CHECK(e.isDlc.has_value() && *e.isDlc == false);
    CHECK(e.id.has_value() && *e.id == 42);
    CHECK(e.displayName == u"Test Car");
    CHECK(e.bitmapName.has_value() && *e.bitmapName == "test_car_bitmap");
    CHECK(e.hostageVehicleClass == 4);  // "SUV" is index 4 of the 6-name table (spec 7.3 +0x47C)
    CHECK(e.group.has_value() && *e.group == "car_standard");

    CHECK(e.maxHitpoints.present && e.maxHitpoints.value == 500);
    CHECK(e.mass.value == 1500.5f);
    CHECK(e.value.value == 10000.0f);

    CHECK(e.lodDistanceRatios.size() == 3);
    CHECK(e.lodDistanceRatios[1] == 0.5f);

    CHECK(e.specialVehicleType == 10);  // "Taxi" is index 10 (spec 7.3 +0x4C4)
    CHECK(e.roadPreference == 1);       // "Highway" -> 1 (spec: "0 absent, 1 Highway, 2 No_highway")

    CHECK(e.torque.value == 400.0f && e.maxRpm.value == 6000.0f);

    // Axles: positional, capacity 2 - the 3rd <Axle> must be dropped.
    CHECK(e.axles.size() == 2);
    CHECK(e.axles[0].wheelDoesSteer.has_value() && *e.axles[0].wheelDoesSteer == true);
    CHECK(e.axles[1].wheelDoesHandbrake.has_value() && *e.axles[1].wheelDoesHandbrake == true);
    CHECK(near(e.axles[0].wheelFriction.value, 0.9f, 1e-6f));

    CHECK(e.gearRatios.size() == 4 && e.gearRatios[0] == 3.5f);
    CHECK(e.downshiftRpms.size() == 3 && e.downshiftRpms[2] == 3000.0f);

    // deg->rad (spec 7.3, marked (r)).
    CHECK(near(e.maxSteeringAngleRadians.value, 35.0f * kDegToRad, 1e-5f));
    // mph->m/s (spec 7.3, marked (v)), 25 mph is below the 100mph cap.
    CHECK(near(e.maxSpeedSteeringMs.value, 25.0f * kMphToMs, 1e-5f));

    CHECK(e.towableData.towableFromRear.has_value() && *e.towableData.towableFromRear == true);
    CHECK(e.towableData.tweakingOffsetFront.complete());
    CHECK(e.towableData.tweakingOffsetFront.value().y == 2.0f);

    CHECK(e.audioHysteresis.audioRpmDecay.value == 0.5f);
    CHECK(e.audioHysteresis.rpmDeviation.maxDuration.present && e.audioHysteresis.rpmDeviation.maxDuration.value == 3);
    // Medium_/High_Onload_Threshold live in a SEPARATE <Audio_Onload_Parameters>
    // wrapper, not inside <Audio_Hysteresis> (real-data finding).
    CHECK(e.audioHysteresis.mediumOnloadThreshold.present && e.audioHysteresis.mediumOnloadThreshold.value == 0.4f);
    CHECK(e.audioHysteresis.highOnloadThreshold.value == 0.7f);

    CHECK(e.foley.engineSoundId.present && e.foley.engineSoundId.value == 7);
    CHECK(e.foley.gearShiftHash == NameHash("snd_gear_shift"));

    // Engine_Smoke/Exhaust_All_Time: inside <Effects> (real-data finding);
    // Exhaust_All_Time also sets flags0 bit 2.
    CHECK(e.effectIds.engineSmokeHash == NameHash("fx_smoke"));
    CHECK((e.flags0 & flags0::kExhaustAllTime) != 0);
    // Exhaust_Types real shape: a list of <Exhaust_Type> items keyed by a
    // <Type> value ("normal"/"normal2"/"special"/"special2"), not four
    // literal tag names.
    CHECK(e.effectIds.exhaustTypes.normal.acceleratorEffectHash == NameHash("fx_exh_a"));
    CHECK(e.effectIds.exhaustTypes.normal.noAcceleratorEffectHash == NameHash("fx_exh_na"));
    CHECK(e.effectIds.exhaustTypes.special.acceleratorEffectHash == 0);  // not authored

    // Normal_Spin_Damping: inside <Angular_Damping> (real-data finding).
    CHECK(e.normalSpinDamping.present && e.normalSpinDamping.value == 2.0f);

    // NoCustVariantsGrid item shape: <NoCustVariantElement><Variant>NAME</Variant>
    // ...</NoCustVariantElement>, not a flat <Element> (real-data finding).
    CHECK(e.noCustVariantsGrid.size() == 1);
    CHECK(e.noCustVariantsGrid[0] == NameHash("Sports_T"));

    CHECK(e.trailerHitchType == 0);  // "Semi" is index 0 (spec 7.3 +0x854)

    // Top speed: 85/95 mph, both under the 100mph cap.
    CHECK(near(e.maximumSpeedMs.value, 85.0f * kMphToMs, 1e-4f));
    CHECK(near(e.maximumSpeedWithNitrousMs.value, 95.0f * kMphToMs, 1e-4f));

    // Wheel size range: only Wheel_Min_Size_0 overridden, the rest keep the
    // documented absent-defaults (spec 7.4: 7,7 / 7,7 / 2,2 / 2,2).
    CHECK(e.wheelSizeRange.minSize0 == 15);
    CHECK(e.wheelSizeRange.minSize1 == 7 && e.wheelSizeRange.maxSize0 == 7 && e.wheelSizeRange.maxSize1 == 7);
    CHECK(e.wheelSizeRange.minWidth0 == 2 && e.wheelSizeRange.minWidth1 == 2);
    CHECK(e.wheelSizeRange.maxWidth0 == 2 && e.wheelSizeRange.maxWidth1 == 2);

    // ProhibitedGunfireSeats bitmask (spec 7.3 +0x8B4): Front Driver Side (bit
    // 0) and Rear Passenger Side (bit 3).
    CHECK((e.prohibitedGunfireSeats & 0x01u) != 0);
    CHECK((e.prohibitedGunfireSeats & 0x08u) != 0);
    CHECK((e.prohibitedGunfireSeats & 0x02u) == 0);  // Front Passenger Side: not authored

    CHECK(e.vehicleInteractionInfo.text.has_value() && *e.vehicleInteractionInfo.text == "car_interaction");
    CHECK(e.vehicleInteractionInfo.hash == NameHash("car_interaction"));
    CHECK(e.characterAnimationSet.text.has_value() && *e.characterAnimationSet.text == "car_charanim");
    // Vehicle_Animation_Set_Name/_Rig_Name: stored WITHOUT their .xtbl/.rigx suffix (spec 7.3).
    CHECK(e.vehicleAnimationSetName.has_value() && *e.vehicleAnimationSetName == "car_animset");
    CHECK(e.vehicleAnimationRigName.has_value() && *e.vehicleAnimationRigName == "car_rig");

    CHECK(e.upgrades.torque.level1.multiplier.value == 1.2f);
    CHECK(e.upgrades.torque.level1.cost.value == 500);
    CHECK(!e.upgrades.torque.level2.multiplier.present);  // Level_2 not authored

    // Vehicle_Type: Automobile, with Hydraulics_Capable (flags0 bit 3) set
    // and Drifting_Possible explicitly "No" (flags0 bit 6 CLEARED, despite
    // the "default set" rule, because "No" is the one recognised clearer).
    CHECK(e.vehicleType.vehicleClass == VehicleClass::Automobile);
    CHECK((e.flags0 & flags0::kHydraulicsCapable) != 0);
    CHECK((e.flags0 & flags0::kDriftingPossible) == 0);
    CHECK(e.vehicleType.automobile.automobileFlags == (0x10u | 0x20u));  // "Is Tank" | "No Treads"
    CHECK(e.vehicleType.automobile.airControl.present);
    CHECK(near(e.vehicleType.automobile.airControl.maxRollSpeedRadians.value, 90.0f * kDegToRad, 1e-5f));

    // Plain flags0/flags1 bits.
    CHECK((e.flags0 & flags0::kNitroCapable) != 0);
    CHECK((e.flags0 & flags0::kParkingSpawn) != 0);
    CHECK((e.flags1 & flags1::kHeavy) != 0);
    CHECK((e.flags1 & flags1::kOpenDoorsAutomatically) != 0);
    // "default set" bits not overridden here stay set (Burnout_Possible,
    // Allow_Passenger_Threat_Extract, Racing_Can_Spawn, Garage_Can_Store).
    CHECK((e.flags0 & flags0::kBurnoutPossible) != 0);
    CHECK((e.flags0 & flags0::kGarageCanStoreA) != 0 && (e.flags0 & flags0::kGarageCanStoreB) != 0);
}

// ---------------------------------------------------------------------------
// A Motorcycle entry (spec 7.5 table row 1): Leaning, Wheelie (with the
// documented Range_Decay rounding), Air_Control, drifting fields.
// ---------------------------------------------------------------------------
void testMotorcycleVariant() {
    const char* xml =
        "<root><Table><Vehicle><Name>bike_test01</Name>"
        "<Vehicle_Type><Motorcycle>"
        "<Leaning><Max_Lean_Angle>45</Max_Lean_Angle><Lean_Damp_Angle>5</Lean_Damp_Angle></Leaning>"
        "<Max_Steering_Angle>30</Max_Steering_Angle>"
        "<Turn_Speed_Multiplier>1.5</Turn_Speed_Multiplier>"
        "<Wheelie><Balance_Angle>10</Balance_Angle><Range_Decay>2.6</Range_Decay></Wheelie>"
        "<Air_Control><Max_Pitch_Speed>60</Max_Pitch_Speed></Air_Control>"
        "<Drifting_Yaw_Torque_Factor>0.75</Drifting_Yaw_Torque_Factor>"
        "</Motorcycle></Vehicle_Type>"
        "</Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));

    CHECK(e.vehicleType.vehicleClass == VehicleClass::Motorcycle);
    const MotorcycleVariant& mc = e.vehicleType.motorcycle;
    CHECK(near(mc.leaning.maxLeanAngleRadians.value, 45.0f * kDegToRad, 1e-5f));
    CHECK(near(mc.leaning.leanDampAngleRadians.value, 5.0f * kDegToRad, 1e-5f));
    CHECK(!mc.leaning.maxLeanSpeedRadians.present);  // not authored
    CHECK(near(mc.maxSteeringAngleRadians.value, 30.0f * kDegToRad, 1e-5f));
    CHECK(mc.turnSpeedMultiplier.value == 1.5f);

    CHECK(mc.wheelie.present);
    CHECK(near(mc.wheelie.balanceAngleRadians.value, 10.0f * kDegToRad, 1e-5f));
    // Range_Decay "stored as a rounded integer" (spec 7.5): 2.6 -> 3.
    CHECK(mc.wheelie.rangeDecay.present && mc.wheelie.rangeDecay.value == 3);

    CHECK(mc.airControl.present);
    CHECK(near(mc.airControl.maxPitchSpeedRadians.value, 60.0f * kDegToRad, 1e-5f));

    CHECK(mc.driftingYawTorqueFactor.value == 0.75f);

    // Drifting_Possible absent under Motorcycle -> flags0 bit 6 "default set".
    CHECK((e.flags0 & flags0::kDriftingPossible) != 0);
}

// ---------------------------------------------------------------------------
// A Helicopter entry (spec 7.5 table row 3): Rotor_Center/Radius/Tilt (raw,
// not the derived 12-point ring), Surfaces, and the two flag-word bits that
// live inside this variant (Is_Attack_Aircraft -> flags1 bit 6,
// Drop_off_copilot -> flags1 bit 17).
// ---------------------------------------------------------------------------
void testHelicopterVariant() {
    const char* xml =
        "<root><Table><Vehicle><Name>heli_test01</Name>"
        "<Vehicle_Type><Helicopter>"
        "<Rotor_Center><X>0</X><Y>1</Y><Z>2</Z></Rotor_Center>"
        "<Rotor_Radius>5.5</Rotor_Radius>"
        "<Steering_Accel>0.3</Steering_Accel>"
        "<Autolevel_Accel>20</Autolevel_Accel>"
        "<Surfaces>"
        "<Surface><Type>Wing</Type><X_Offset>1</X_Offset><Max_Deflect>15</Max_Deflect></Surface>"
        "<Surface><Type>Rudder</Type><X_Offset>-1</X_Offset></Surface>"
        "</Surfaces>"
        "<Is_Attack_Aircraft>yes</Is_Attack_Aircraft>"
        "<Drop_off_copilot>No</Drop_off_copilot>"
        "</Helicopter></Vehicle_Type>"
        "</Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));

    CHECK(e.vehicleType.vehicleClass == VehicleClass::Helicopter);
    const HelicopterVariant& h = e.vehicleType.helicopter;
    CHECK(h.rotorCenter.complete() && h.rotorCenter.value().y == 1.0f);
    CHECK(h.rotorRadius.value == 5.5f);
    CHECK(h.steeringAccel.value == 0.3f);
    CHECK(near(h.autolevelAccelRadians.value, 20.0f * kDegToRad, 1e-5f));

    CHECK(h.surfaces.size() == 2);
    CHECK(h.surfaces[0].type == 0);  // "Wing"
    CHECK(h.surfaces[1].type == 3);  // "Rudder"
    CHECK(near(h.surfaces[0].maxDeflectRadians.value, 15.0f * kDegToRad, 1e-5f));
    CHECK(!h.surfaces[1].maxDeflectRadians.present);

    CHECK((e.flags1 & flags1::kIsAttackAircraft) != 0);
    CHECK((e.flags1 & flags1::kDropOffCopilot) == 0);  // "No" clears the default-set bit
}

// ---------------------------------------------------------------------------
// A Watercraft entry (spec 7.5 table row 5): only the tabulated LBS_* fields
// are implemented. Real base-game data (see src/vehicle_entry.cpp's file
// banner) confirms they sit inside their own <Lean_Based_Steering> wrapper.
// ---------------------------------------------------------------------------
void testWatercraftVariant() {
    const char* xml =
        "<root><Table><Vehicle><Name>boat_test01</Name>"
        "<Vehicle_Type><Watercraft>"
        "<Lean_Based_Steering>"
        "<LBS_Min_Radius>10</LBS_Min_Radius>"
        "<LBS_Max_Lean_Angle>57</LBS_Max_Lean_Angle>"
        "<LBS_Correction_Acceleration>1000</LBS_Correction_Acceleration>"
        "<LBS_Turn_Radius_Modifier>0.5</LBS_Turn_Radius_Modifier>"
        "</Lean_Based_Steering>"
        "</Watercraft></Vehicle_Type>"
        "</Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));

    CHECK(e.vehicleType.vehicleClass == VehicleClass::Watercraft);
    const WatercraftVariant& w = e.vehicleType.watercraft;
    CHECK(w.lbsMinRadius.present && w.lbsMinRadius.value == 10.0f);
    CHECK(near(w.lbsMaxLeanAngleRadians.value, 57.0f * kDegToRad, 1e-5f));
    CHECK(near(w.lbsCorrectionAccelRadians.value, 1000.0f * kDegToRad, 1e-4f));
    CHECK(w.lbsTurnRadiusModifier.value == 0.5f);
}

// ---------------------------------------------------------------------------
// A row missing every optional element: absence must stay observable
// (nullopt / present=false / documented sentinel), never a silently
// invented default.
// ---------------------------------------------------------------------------
void testVehicleMissingOptional() {
    const char* xml = "<root><Table><Vehicle><Name>bare_vehicle</Name></Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));

    CHECK(e.name.has_value() && *e.name == "bare_vehicle");
    CHECK(!e.isDlc.has_value());
    CHECK(!e.id.has_value());
    CHECK(e.displayName.empty());
    CHECK(!e.bitmapName.has_value());
    CHECK(e.hostageVehicleClass == -1);  // "other -1" (spec 7.3)
    CHECK(!e.group.has_value());
    CHECK(!e.maxHitpoints.present);
    CHECK(e.lodDistanceRatios.empty());
    CHECK(e.specialVehicleType == -1);
    CHECK(e.roadPreference == 0);  // explicit documented absent value (spec 7.3)
    CHECK(e.axles.empty());
    CHECK(e.gearRatios.empty() && e.downshiftRpms.empty());
    CHECK(!e.maxSteeringAngleRadians.present);
    CHECK(!e.towableData.towableFromFront.has_value());
    CHECK(!e.towableData.tweakingOffsetFront.present);
    CHECK(e.trailerHitchType == -1 && e.tractorHitchType == -1);
    CHECK(!e.maximumSpeedMs.present);
    CHECK(e.hardtopVids.empty() && e.roofObstructionVids.empty());
    CHECK(!e.climbToEntryOffset.present);
    CHECK(e.prohibitedGunfireSeats == 0);
    CHECK(e.noCustVariantsGrid.empty());
    CHECK(!e.racingSelectability.has_value());
    CHECK(e.seatSpecificData.empty());
    CHECK(!e.vehicleInteractionInfo.text.has_value() && e.vehicleInteractionInfo.hash == 0);
    CHECK(!e.vehicleAnimationSetName.has_value());
    CHECK(e.vehicleType.vehicleClass == VehicleClass::Unknown);
    // Drifting_Possible (flags0 bit 6) is only read from WITHIN an
    // Automobile/Motorcycle Vehicle_Type block (spec 7.5); this bare row has
    // no Vehicle_Type at all, so that bit stays clear even though it is
    // otherwise a "default set" field - only the row-level "default set"
    // bits are set here.
    CHECK(e.flags0 == (flags0::kBurnoutPossible | flags0::kAllowPassengerThreatExtract | flags0::kRacingCanSpawn |
                        flags0::kGarageCanStoreA | flags0::kGarageCanStoreB));
    CHECK(e.flags1 == 0);  // no "default set" bits in word1

    // Wheel size range: whole wrapper absent -> the documented defaults
    // (spec 7.4: 7,7 / 7,7 / 2,2 / 2,2).
    CHECK(e.wheelSizeRange.minSize0 == 7 && e.wheelSizeRange.minSize1 == 7);
    CHECK(e.wheelSizeRange.maxSize0 == 7 && e.wheelSizeRange.maxSize1 == 7);
    CHECK(e.wheelSizeRange.minWidth0 == 2 && e.wheelSizeRange.minWidth1 == 2);
    CHECK(e.wheelSizeRange.maxWidth0 == 2 && e.wheelSizeRange.maxWidth1 == 2);
}

// Mass is absent in 3/123 real vehicles (spec-vehicle-data.md s7.11, labelled
// OPEN for its accessor kind in s7.2): a row without <Mass> must read as
// ABSENT, never as a defaulted value, while a present Mass still reads.
void testMassAbsentNotDefaulted() {
    const char* xml =
        "<root><Table><Vehicle><Name>no_mass</Name><Max_Hitpoints>500</Max_Hitpoints>"
        "<Value>10000</Value></Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));
    CHECK(e.maxHitpoints.present);
    CHECK(!e.mass.present);
    CHECK(e.mass.value == 0.0f);

    const char* xml2 = "<root><Table><Vehicle><Name>has_mass</Name><Mass>15000</Mass></Vehicle></Table></root>";
    Document doc2 = P(xml2);
    VehicleEntry e2 = ParseVehicleEntry(FirstVehicle(doc2));
    CHECK(e2.mass.present && e2.mass.value == 15000.0f);
}

// ---------------------------------------------------------------------------
// mph->m/s with the 100mph cap (spec 7, "Units"): Maximum_Speed is capped
// BEFORE conversion, and Maximum_Speed_With_Nitrous is then raised to at
// least Maximum_Speed if it would otherwise be lower.
// ---------------------------------------------------------------------------
void testSpeedCapAndNitrousRaise() {
    const char* xml =
        "<root><Table><Vehicle><Name>speedtest</Name>"
        "<Maximum_Speed>150</Maximum_Speed>"          // over the 100mph cap
        "<Maximum_Speed_With_Nitrous>50</Maximum_Speed_With_Nitrous>"  // under 100mph, and lower than the (capped) plain speed
        "</Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));

    CHECK(e.maximumSpeedMs.present);
    CHECK(near(e.maximumSpeedMs.value, kMaxSpeedCapMs, 1e-4f));  // 100mph cap applied before *0.44704
    CHECK(e.maximumSpeedWithNitrousMs.present);
    // 50mph*0.44704 (22.352) is less than the capped plain speed (44.704),
    // so the nitrous value must be raised to match it exactly (spec 7.3).
    CHECK(near(e.maximumSpeedWithNitrousMs.value, kMaxSpeedCapMs, 1e-4f));
}

// ---------------------------------------------------------------------------
// deg->rad (spec 7, "Units"): a plain, uncapped angle field.
// ---------------------------------------------------------------------------
void testDegToRadCase() {
    // Max_Steering_Angle lives inside a <Steering> wrapper (real-data
    // finding, see testAutomobileNormalRow's banner comment).
    const char* xml =
        "<root><Table><Vehicle><Name>angletest</Name><Steering><Max_Steering_Angle>90</Max_Steering_Angle></Steering></Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));
    CHECK(e.maxSteeringAngleRadians.present);
    CHECK(near(e.maxSteeringAngleRadians.value, 90.0f * kDegToRad, 1e-6f));
    CHECK(near(e.maxSteeringAngleRadians.value, 1.5707963f, 1e-5f));  // 90 degrees in radians
}

// ---------------------------------------------------------------------------
// AI_Max_Speed_Steering: the spec explicitly marks this field "not
// converted - observed" (section 7.3), unlike every sibling steering field.
// This is exactly the kind of field an over-eager "fix" would wrongly
// convert - hence its own dedicated test (see the mutation-check note at
// the top of this file).
// ---------------------------------------------------------------------------
void testAiMaxSpeedSteeringNotConverted() {
    // AI_Max_Speed_Steering lives inside a <Steering> wrapper (real-data finding).
    const char* xml =
        "<root><Table><Vehicle><Name>aitest</Name><Steering><AI_Max_Speed_Steering>60</AI_Max_Speed_Steering></Steering></Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));
    CHECK(e.aiMaxSpeedSteeringRaw.present);
    CHECK(e.aiMaxSpeedSteeringRaw.value == 60.0f);  // exact: neither capped nor multiplied by kMphToMs
}

// ---------------------------------------------------------------------------
// Flag words (spec 7.6): at least one bit exercised per word, including the
// "default set, only literal 'no' clears" family, the enum-selected
// multi-bit fields, the ParkingSpawn integer test, and the two special
// cases with no named element (ID==129, name=="car_4dr_genki").
// ---------------------------------------------------------------------------
void testFlagWordsWord0() {
    // shooters_allowed enum (bits 20/21) and Garage_Can_Store's 3-state text.
    Document a = P(
        "<root><Table><Vehicle><Name>flagtest_a</Name>"
        "<shooters_allowed>Driver Only</shooters_allowed>"
        "<Garage_Can_Store>Valet-Only</Garage_Can_Store>"
        "<RadioTuner>yes</RadioTuner>"
        "<2D_Lod_Fade>yes</2D_Lod_Fade>"
        "</Vehicle></Table></root>");
    VehicleEntry ea = ParseVehicleEntry(FirstVehicle(a));
    CHECK((ea.flags0 & flags0::kShootersAllowedDriverOnly) != 0);
    CHECK((ea.flags0 & flags0::kShootersAllowedFrontSeats) == 0);
    CHECK((ea.flags0 & flags0::kGarageCanStoreA) == 0);  // Valet-Only clears bit 23
    CHECK((ea.flags0 & flags0::kGarageCanStoreB) != 0);  // ... but leaves bit 24
    CHECK((ea.flags0 & flags0::kRadioTuner) != 0);
    CHECK((ea.flags0 & flags0::k2dLodFade) != 0);

    // The exact reader spelling is "2D_Lod_Fade", NOT "Lod_Fade_2d" - the
    // latter is one of spec section 7.7's "8 dead tags" (a real DLC-data
    // authoring mistake the retail engine never reads). Confirms this
    // reader matches the documented spelling, not the more "natural" one.
    Document b = P("<root><Table><Vehicle><Name>flagtest_b</Name><Lod_Fade_2d>yes</Lod_Fade_2d></Vehicle></Table></root>");
    VehicleEntry eb = ParseVehicleEntry(FirstVehicle(b));
    CHECK((eb.flags0 & flags0::k2dLodFade) == 0);

    // ParkingSpawn: "integer != 0", not bool text (spec 7.6).
    Document c = P("<root><Table><Vehicle><Name>flagtest_c</Name><ParkingSpawn>0</ParkingSpawn></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(c)).flags0 & flags0::kParkingSpawn) == 0);
    Document d = P("<root><Table><Vehicle><Name>flagtest_d</Name><ParkingSpawn>5</ParkingSpawn></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(d)).flags0 & flags0::kParkingSpawn) != 0);

    // "default set, only 'No' clears" - Burnout_Possible: absent stays set,
    // explicit "No" clears, any other spelling (including "false") leaves it
    // set (spec 7.6: "any other spelling leaves the default").
    Document e1 = P("<root><Table><Vehicle><Name>flagtest_e1</Name></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(e1)).flags0 & flags0::kBurnoutPossible) != 0);
    Document e2 = P("<root><Table><Vehicle><Name>flagtest_e2</Name><Burnout_Possible>No</Burnout_Possible></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(e2)).flags0 & flags0::kBurnoutPossible) == 0);
    Document e3 = P("<root><Table><Vehicle><Name>flagtest_e3</Name><Burnout_Possible>false</Burnout_Possible></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(e3)).flags0 & flags0::kBurnoutPossible) != 0);  // "false" is not a recognised clearer
}

void testFlagWordsWord1() {
    Document a = P("<root><Table><Vehicle><Name>car_4dr_genki</Name></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(a)).flags1 & flags1::kGenkiNameSpecialCase) != 0);
    Document b = P("<root><Table><Vehicle><Name>some_other_car</Name></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(b)).flags1 & flags1::kGenkiNameSpecialCase) == 0);

    Document c = P("<root><Table><Vehicle><Name>idtest</Name><ID>129</ID></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(c)).flags1 & flags1::kIdIs129) != 0);
    Document d = P("<root><Table><Vehicle><Name>idtest2</Name><ID>5</ID></Vehicle></Table></root>");
    CHECK((ParseVehicleEntry(FirstVehicle(d)).flags1 & flags1::kIdIs129) == 0);

    Document e1 = P(
        "<root><Table><Vehicle><Name>flagtest_w1</Name>"
        "<Heavy>yes</Heavy><Military>yes</Military><Wieldable_weapons_disabled>yes</Wieldable_weapons_disabled>"
        "</Vehicle></Table></root>");
    VehicleEntry ee = ParseVehicleEntry(FirstVehicle(e1));
    CHECK((ee.flags1 & flags1::kHeavy) != 0);
    CHECK((ee.flags1 & flags1::kMilitary) != 0);
    CHECK((ee.flags1 & flags1::kWieldableWeaponsDisabled) != 0);
    CHECK((ee.flags1 & flags1::kNoExplodeOrFire) == 0);  // not authored
}

// ---------------------------------------------------------------------------
// AutomobileFlags (spec 7.5): exactly six accepted strings; an unrecognised
// Flag contributes nothing (matches the general FlagMask contract).
// ---------------------------------------------------------------------------
void testAutomobileFlagsBitmask() {
    const char* xml =
        "<root><Table><Vehicle><Name>flagbits</Name><Vehicle_Type><Automobile>"
        "<AutomobileFlags><Flag>Has Security Alarm</Flag><Flag>Always Open Topped</Flag><Flag>Not A Real Flag</Flag></AutomobileFlags>"
        "</Automobile></Vehicle_Type></Vehicle></Table></root>";
    Document doc = P(xml);
    VehicleEntry e = ParseVehicleEntry(FirstVehicle(doc));
    CHECK(e.vehicleType.automobile.automobileFlags == (0x01u | 0x08u));
}

}  // namespace

int main() {
    testAutomobileNormalRow();
    testMotorcycleVariant();
    testHelicopterVariant();
    testWatercraftVariant();
    testVehicleMissingOptional();
    testMassAbsentNotDefaulted();
    testSpeedCapAndNitrousRaise();
    testDegToRadCase();
    testAiMaxSpeedSteeringNotConverted();
    testFlagWordsWord0();
    testFlagWordsWord1();
    testAutomobileFlagsBitmask();

    if (g_failed) {
        std::cerr << g_failed << " check(s) FAILED (" << g_passed << " passed).\n";
        return 1;
    }
    std::cout << g_passed << " tests passed.\n";
    return 0;
}
