// Synthetic tests for sr3tables_environment, built FROM THE TEXT of
// spec-tables-environment.md only (hand-rolled XML fixtures as string
// literals) - never derived from this project's own reader source. A pass
// here proves the reader implements the spec's text, not that the spec is
// right; the real-data statement is
// tools/validation/validate_tables_environment_population.cpp.
//
// One test function per implemented table (26; materials.xtbl is skipped,
// see tables.h). Each covers at least: a normal row: an absent optional
// element giving nullopt/present==false; and, where the spec calls one out,
// a boundary/range value. testTimeOfDayObjects covers the group's
// enum-index table; testCameraFree covers its flag-list table
// (Vehicle_Aim/flags/Flag); testEffects / testVfx / testRefractionSituation
// mirror the spec's own DLC-validated shapes (10.1/10.2/8/14).

#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_environment/tables.h"

namespace {

using namespace sr3tables_environment;
using namespace sr3xtbl;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__   \
                      << "\n";                                                    \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

Document P(std::string_view s) { return ParseDocument(s); }

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

// ===========================================================================
// 2. weather.xtbl
// ===========================================================================
void testWeatherStage() {
    Document d = P(R"(<root><Table>
      <Weather_Stage>
        <Name>Clear Skies</Name>
        <DisplayName>WEATHER_CLEAR</DisplayName>
        <Stage_Settings><Chance>5.0</Chance><Average_Duration>600</Average_Duration><Variance>60</Variance><PedDensity>1.0</PedDensity></Stage_Settings>
        <Rain_Settings><Rain_Density>0</Rain_Density><Lightning_Settings><Frequency>0</Frequency><Variance>0</Variance></Lightning_Settings></Rain_Settings>
        <TOD_Settings><Tod_Lights_Pct>0.5</Tod_Lights_Pct><Skydome_Settings><Blend_Factor>1.0</Blend_Factor><Color><R>255</R><G>128</G><B>0</B></Color></Skydome_Settings></TOD_Settings>
        <Sun_Settings><Sun_Color_Multiply><R>255</R><G>255</G><B>255</B></Sun_Color_Multiply><Sun_Opacity>1.0</Sun_Opacity></Sun_Settings>
        <Next_Stage_List><Next_Stage>Overcast</Next_Stage><Next_Stage>Heavy Rain</Next_Stage></Next_Stage_List>
      </Weather_Stage>
    </Table></root>)");
    std::vector<WeatherStage> rows = ParseWeatherTable(d);
    CHECK(rows.size() == 1);
    const WeatherStage& w = rows[0];
    CHECK(w.name.has_value() && *w.name == "Clear Skies");
    CHECK(w.displayName.has_value() && *w.displayName == "WEATHER_CLEAR");
    CHECK(w.chance.present && w.chance.value == 5.0f);
    CHECK(w.averageDuration.present && w.averageDuration.value == 600.0f);
    // /255 colour convention (spec 1.4, accessor 0x00DAD0A0).
    CHECK(w.skydomeColor.present);
    CHECK(near(w.skydomeColor.r.value, 1.0f, 1e-5f));
    CHECK(near(w.skydomeColor.g.value, 128.0f / 255.0f, 1e-5f));
    CHECK(w.skydomeColor.b.value == 0.0f);
    CHECK(w.sunColorMultiply.present && near(w.sunColorMultiply.r.value, 1.0f, 1e-5f));
    // Moon absent: EXPLICITLY "only if present" -> untouched.
    CHECK(!w.moonColorMultiply.present);
    CHECK(w.nextStageNames.size() == 2 && w.nextStageNames[0] == "Overcast" && w.nextStageNames[1] == "Heavy Rain");
    // A field never mentioned in this row: the Always-hazard case - present()
    // is false, value is sr3xtbl::Always's 0 stand-in (NOT the engine's real
    // indeterminate residue - see tables.h/xtbl.h banners).
    CHECK(!w.rainFogLayer1UScale.present && w.rainFogLayer1UScale.value == 0.0f);
}

// ===========================================================================
// 4. wind.xtbl
// ===========================================================================
void testWindStage() {
    Document d = P(R"(<root><Table>
      <Wind_Stage>
        <Name>Breezy</Name>
        <Display_Name>WIND_BREEZY</Display_Name>
        <Stage_Settings><Chance>3.0</Chance><Average_Duration>300</Average_Duration><Variance>30</Variance></Stage_Settings>
        <Wind_Settings><Average_Intensity>0.4</Average_Intensity></Wind_Settings>
        <Next_Stage_List><Next_Stage>Calm</Next_Stage></Next_Stage_List>
      </Wind_Stage>
    </Table></root>)");
    std::vector<WindStage> rows = ParseWindTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "Breezy");
    CHECK(rows[0].displayName == "WIND_BREEZY");  // spelled differently from weather.xtbl's DisplayName (spec 4)
    CHECK(rows[0].windAverageIntensity.present && rows[0].windAverageIntensity.value == 0.4f);
    CHECK(rows[0].nextStageNames.size() == 1 && rows[0].nextStageNames[0] == "Calm");
}

// ===========================================================================
// 5.1 rain.xtbl
// ===========================================================================
void testRainLevel() {
    Document d = P(R"(<root><Table>
      <Level><Parameters>
        <Density>250</Density><View_Radius>40</View_Radius><Speed>1.0</Speed><Opacity>0.8</Opacity>
        <Length_Near>1</Length_Near><Length_Far>2</Length_Far><Width_Near>0.1</Width_Near><Width_Far>0.2</Width_Far>
        <Splash_Lifetime>1</Splash_Lifetime><Splash_Size_Near>0.1</Splash_Size_Near><Splash_Size_Far>0.2</Splash_Size_Far>
        <Wind_Amount>0.5</Wind_Amount><Effect>rain_splash</Effect><Camera_drop_effect>rain_drop</Camera_drop_effect>
      </Parameters></Level>
      <Level><Parameters><Density>-5</Density></Parameters></Level>
    </Table></root>)");
    std::vector<RainLevel> rows = ParseRainTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].densityRaw.present && rows[0].densityRaw.value == 250u);
    CHECK(near(rows[0].densityFraction(), 2.5f, 1e-6f));  // spec 5.1: stored as f32 = value / 100.0
    CHECK(rows[0].effect.has_value() && *rows[0].effect == "rain_splash");
    CHECK(rows[0].cameraDropEffect.has_value() && *rows[0].cameraDropEffect == "rain_drop");
    // BOUNDARY (xtbl.h 3 / sr3xtbl::ParseUInt32): the unsigned reader does
    // NOT strip a leading '-', so "-5" reads as 0.
    CHECK(rows[1].densityRaw.present && rows[1].densityRaw.value == 0u);
}

// ===========================================================================
// 5.2 lightning.xtbl
// ===========================================================================
void testLightningType() {
    Document d = P(R"(<root><Table>
      <Lightning_Type>
        <Name>discarded</Name>
        <Probability>0.2</Probability>
        <VFX_List><VFX>sky_bolt_1</VFX><VFX>sky_bolt_2</VFX></VFX_List>
        <TOD_Overrides>
          <Fog_Color_Override><R>51</R><G>51</G><B>51</B></Fog_Color_Override>
          <Cloud_Brightness_Override>0.9</Cloud_Brightness_Override>
        </TOD_Overrides>
      </Lightning_Type>
    </Table></root>)");
    std::vector<LightningType> rows = ParseLightningTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].probability.present && rows[0].probability.value == 0.2f);
    CHECK(rows[0].vfxNames.size() == 2);
    CHECK(rows[0].fogColorOverride.present && near(rows[0].fogColorOverride.r.value, 51.0f / 255.0f, 1e-5f));
    // NOTE: the engine float grammar accumulates fraction digits by repeated
    // single-precision x0.1 multiplication (xtbl.h's ParseFloat banner), so a
    // parsed "0.9" is not always bit-identical to the compiler's 0.9f literal
    // - use a tolerance, not ==, for any non-trivial fraction from here on.
    CHECK(rows[0].cloudBrightnessOverride.has_value() && near(*rows[0].cloudBrightnessOverride, 0.9f, 1e-6f));
    // Missing-optional cases: never mentioned in the fixture.
    CHECK(!rows[0].ambientOverride.present);
    CHECK(!rows[0].skyBrightnessOverride.has_value());
    CHECK(!rows[0].todLightOverride.present);
}

// ===========================================================================
// 6.1 lens_flares.xtbl
// ===========================================================================
void testLensFlare() {
    Document d = P(R"(<root><Table>
      <Flare><ImageFilename><Filename>flare_sun.tga</Filename></ImageFilename><Radius>2.0</Radius><Scale>1.5</Scale><BaseAlpha>0.8</BaseAlpha></Flare>
    </Table></root>)");
    std::vector<LensFlare> rows = ParseLensFlaresTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].imageFilename.has_value() && *rows[0].imageFilename == "flare_sun.tga");
    CHECK(rows[0].radius.value == 2.0f && rows[0].scale.value == 1.5f && rows[0].baseAlpha.value == 0.8f);
}

// ===========================================================================
// 6.2 motion_blur.xtbl
// ===========================================================================
void testMotionBlur() {
    Document empty = P("<root><Table></Table></root>");
    CHECK(!ParseMotionBlurSettings(empty).has_value());  // spec 6.2: absent Motion_Blur_Settings -> no-op

    Document d = P(R"(<root><Table><Motion_Blur_Settings>
      <On_Foot_Walk_Run><Strength>0.1</Strength><Max_Offset>0.2</Max_Offset></On_Foot_Walk_Run>
      <Vehicle_Base><World_Strength>0.3</World_Strength><Target_Strength>0.4</Target_Strength><Max_Offset>0.5</Max_Offset><Fake_Velocity_Scale>0.6</Fake_Velocity_Scale></Vehicle_Base>
      <Vehicle_Nitrous><World_Strength>1</World_Strength><Target_Strength>2</Target_Strength><Max_Offset>3</Max_Offset><Duration>4</Duration><Decay_Time>5</Decay_Time><Fake_Velocity_Scale>6</Fake_Velocity_Scale></Vehicle_Nitrous>
    </Motion_Blur_Settings></Table></root>)");
    std::optional<MotionBlurSettings> mb = ParseMotionBlurSettings(d);
    CHECK(mb.has_value());
    CHECK(mb->onFootWalkRunStrength.value == 0.1f && mb->onFootWalkRunMaxOffset.value == 0.2f);
    CHECK(mb->vehicleBase.worldStrength.value == 0.3f && mb->vehicleBase.fakeVelocityScale.value == 0.6f);
    CHECK(mb->vehicleNitrous.duration.value == 4.0f && mb->vehicleNitrous.decayTime.value == 5.0f);
    // Never mentioned: Always-hazard, present()==false.
    CHECK(!mb->airplaneBase.worldStrength.present);
}

// ===========================================================================
// 6.3 radial_blur.xtbl
// ===========================================================================
void testRadialBlur() {
    Document d = P(R"(<root><Table>
      <Radial_blur><Name>Quick large</Name><Strength>0.5</Strength><Duration>0.3</Duration><Radius>1.0</Radius><Distance_fade>0.2</Distance_fade><Priority>3</Priority></Radial_blur>
    </Table></root>)");
    std::vector<RadialBlur> rows = ParseRadialBlurTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Quick large");
    CHECK(rows[0].priority.present && rows[0].priority.value == 3);
}

// ===========================================================================
// 7.1 dof_situations.xtbl
// ===========================================================================
void testDofSituation() {
    // spec 7.1: "name" is looked up case-insensitively, so "Name" also matches.
    Document d = P(R"(<root><Table>
      <DOF_situation><Name>Satellite</Name><Start_Multiplier_A>1</Start_Multiplier_A><Start_Multiplier_B>2</Start_Multiplier_B>
        <End_Multiplier_A>3</End_Multiplier_A><End_Multiplier_B>4</End_Multiplier_B><Blur_Radius>5</Blur_Radius>
        <Transition_Speed>6</Transition_Speed><Human_Spherecast_Radius>7</Human_Spherecast_Radius></DOF_situation>
    </Table></root>)");
    std::vector<DofSituation> rows = ParseDofSituationsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Satellite");
    CHECK(rows[0].blurRadius.value == 5.0f && rows[0].humanSpherecastRadius.value == 7.0f);
}

// ===========================================================================
// 8. refraction_situations.xtbl
// ===========================================================================
void testRefractionSituation() {
    // Row 0 mirrors the ONE real raw DLC sample the spec cites (8, CONFIRMED
    // empirical): dlc2_refraction_situations.xtbl, row "dlf_test",
    // Framework dlc2, Scale 10, a full Spasm block, Duration ABSENT.
    Document d = P(R"(<root><Table>
      <refraction_situation>
        <name>dlf_test</name>
        <Scale>10</Scale><Frequency>1</Frequency><Offset_Delta>0.1</Offset_Delta>
        <Fade_In_Time>0.5</Fade_In_Time><Fade_Out_Time>0.5</Fade_Out_Time>
        <Spasm><Spasm_Time_Min>1</Spasm_Time_Min><Spasm_Time_Max>2</Spasm_Time_Max>
          <Frequency_Min>3</Frequency_Min><Frequency_Max>4</Frequency_Max>
          <Scale_Min>5</Scale_Min><Scale_Max>6</Scale_Max>
          <Duration_Min>7</Duration_Min><Duration_Max>8</Duration_Max></Spasm>
        <Framework>dlc2</Framework>
      </refraction_situation>
      <refraction_situation>
        <name>no_spasm</name>
        <Scale>1</Scale><Frequency>1</Frequency><Offset_Delta>1</Offset_Delta>
        <Fade_In_Time>1</Fade_In_Time><Fade_Out_Time>1</Fade_Out_Time><Duration>2.5</Duration>
      </refraction_situation>
    </Table></root>)");
    std::vector<RefractionSituation> rows = ParseRefractionSituationsTable(d);
    CHECK(rows.size() == 2);
    const RefractionSituation& r0 = rows[0];
    CHECK(r0.name.has_value() && *r0.name == "dlf_test");
    CHECK(r0.scale.value == 10.0f);
    CHECK(r0.framework.has_value() && *r0.framework == "dlc2");
    CHECK(r0.spasmPresent);
    CHECK(r0.spasmTimeMin.value == 1.0f && r0.spasmDurationMax.value == 8.0f);
    // Duration is EXPLICITLY absent in the real sample: nullopt, NOT the Always-hazard.
    CHECK(!r0.duration.has_value());
    CHECK(r0.DurationOrDefault() == -1.0f);  // spec-documented concrete default

    const RefractionSituation& r1 = rows[1];
    CHECK(!r1.spasmPresent);
    // Absent Spasm block -> spec-documented concrete 0 for every sub-field.
    CHECK(r1.spasmScaleMin.value == 0.0f && r1.spasmDurationMax.value == 0.0f);
    CHECK(r1.duration.has_value() && *r1.duration == 2.5f);
    CHECK(r1.DurationOrDefault() == 2.5f);
}

// ===========================================================================
// 9.1 skybox_effects.xtbl
// ===========================================================================
void testSkyboxEffect() {
    Document d = P(R"(<root><Table>
      <Skybox_Effect><Name>lightning_flash</Name><Effect>fx_lightning</Effect>
        <Auto_Spawn><Min_Time_Spacing>1</Min_Time_Spacing><Max_Time_Spacing>3</Max_Time_Spacing></Auto_Spawn>
        <Random_Orientation>True</Random_Orientation>
        <TODRange><Start_Time>600</Start_Time><End_Time>1800</End_Time></TODRange>
        <Weather_Stage>Heavy Rain</Weather_Stage><Skybox_Layer>2</Skybox_Layer></Skybox_Effect>
      <Skybox_Effect><Name>no_range</Name><Skybox_Layer>300</Skybox_Layer></Skybox_Effect>
    </Table></root>)");
    std::vector<SkyboxEffect> rows = ParseSkyboxEffectsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].autoSpawnPresent && rows[0].autoSpawnMinTimeSpacing.value == 1.0f);
    CHECK(rows[0].randomOrientation.present && rows[0].randomOrientation.value == true);
    CHECK(rows[0].todRangeStartTime.has_value() && *rows[0].todRangeStartTime == 600);
    CHECK(rows[0].todRangeEndTime.has_value() && *rows[0].todRangeEndTime == 1800);

    // Missing TODRange -> spec-documented defaults 0 / 0x0937 (2359).
    CHECK(!rows[1].todRangeStartTime.has_value());
    CHECK(rows[1].TodRangeStartOrDefault() == 0);
    CHECK(rows[1].TodRangeEndOrDefault() == 0x0937);
    CHECK(!rows[1].autoSpawnPresent);
    // BOUNDARY: Skybox_Layer is a u8, low byte kept: 300 = 0x12C -> 0x2C = 44.
    CHECK(rows[1].skyboxLayer.value == 44);
}

// ===========================================================================
// 9.2 time_of_day_objects.xtbl - the group's ENUM-INDEX table
// ===========================================================================
void testTimeOfDayObjects() {
    Document d = P(R"(<root><Table>
      <Object_Type><Name>headLIGHTS</Name><On_Time>1830</On_Time><Off_Time>600</Off_Time><Variation>15</Variation></Object_Type>
      <Object_Type><Name>Unknown Thing</Name><On_Time>0</On_Time><Off_Time>0</Off_Time><Variation>0</Variation></Object_Type>
    </Table></root>)");
    std::vector<TimeOfDayObjectType> rows = ParseTimeOfDayObjectsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "headLIGHTS");
    CHECK(rows[0].onTimeHHMM.value == 1830u);
    // ENUM-INDEX (spec 9.2/13): case-insensitive match against the 5-name
    // table this header exposes for a caller/harness to apply.
    const Node* row0 = FindChild(d.table(), "Object_Type");
    const Node* row1 = NextSibling(d.table(), row0, "Object_Type");
    CHECK(EnumIndex(FindChild(row0, "Name"), kTimeOfDayObjectNames.data(), kTimeOfDayObjectNames.size()) == 1);  // "Headlights"
    // Unrecognised name -> -1 (spec: "-1 if ... nothing matches"; the real
    // engine then writes six dwords before the array with no bound check -
    // a derived runtime concern, not modelled by this struct).
    CHECK(EnumIndex(FindChild(row1, "Name"), kTimeOfDayObjectNames.data(), kTimeOfDayObjectNames.size()) == -1);
}

// ===========================================================================
// 9.3 external_light_override.xtbl
// ===========================================================================
void testExternalLightOverride() {
    Document d = P(R"(<root><Table>
      <light_override><Name>Common</Name>
        <front_color>1.0 0.9 0.8</front_color>
        <back_color>0.5 0.5 0.5 0.2</back_color>
        <front_intensity>2.0</front_intensity>
        <back_intensity>0.5</back_intensity>
      </light_override>
      <light_override><Name>NoColors</Name></light_override>
    </Table></root>)");
    std::vector<LightOverride> rows = ParseExternalLightOverrideTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].frontColor.has_value());
    CHECK(rows[0].frontColor->r == 1.0f && near(rows[0].frontColor->g, 0.9f, 1e-6f) &&
          near(rows[0].frontColor->b, 0.8f, 1e-6f));
    CHECK(rows[0].frontColor->a == 1.0f);  // unspecified trailing component stays 1.0 (spec 9.3)
    CHECK(rows[0].backColor.has_value() && near(rows[0].backColor->a, 0.2f, 1e-6f));
    CHECK(rows[0].frontIntensity.has_value() && *rows[0].frontIntensity == 2.0f);
    CHECK(rows[0].BackIntensityOrDefault() == 0.5f);
    // Missing optional colour/intensity elements:
    CHECK(!rows[1].frontColor.has_value());
    CHECK(rows[1].FrontIntensityOrDefault() == 1.0f);  // spec default
}

// ===========================================================================
// 10.1 effects.xtbl
// ===========================================================================
void testEffects() {
    Document d = P(R"(<root><Table>
      <Effect>
        <Name>fx_car_explosion</Name>
        <Framework>dlc1</Framework>
        <Info_Slot_Index>606</Info_Slot_Index>
        <visual>vfx_car_explosion</visual>
        <sound>car_explosion_sound</sound>
        <sound_parent_switch_name>Explosions:Car</sound_parent_switch_name>
        <scale_factor>1.5</scale_factor>
        <Damage_Region><Region_Type>Vehicle</Region_Type><Region_Shape>Box</Region_Shape>
          <Region_Offset><X>0</X><Y>0</Y><Z>1</Z></Region_Offset>
          <Region_Size><X>2</X><Y>2</Y><Z>2</Z></Region_Size></Damage_Region>
        <stop_when_host_destroyed>True</stop_when_host_destroyed>
        <Proximity_Refraction><Situation>dlf_test</Situation><Near_Radius>1</Near_Radius><Far_Radius>5</Far_Radius></Proximity_Refraction>
        <_Editor><Category>Vehicles</Category></_Editor>
      </Effect>
      <Effect><Name>fx_minimal</Name></Effect>
    </Table></root>)");
    std::vector<Effect> rows = ParseEffectsTable(d);
    CHECK(rows.size() == 2);
    const Effect& e0 = rows[0];
    CHECK(e0.name.has_value() && *e0.name == "fx_car_explosion");
    CHECK(e0.framework.has_value() && *e0.framework == "dlc1");
    CHECK(e0.infoSlotIndex.has_value() && *e0.infoSlotIndex == 606);
    CHECK(e0.visual.has_value() && *e0.visual == "vfx_car_explosion");
    CHECK(e0.sound.has_value() && *e0.sound == "car_explosion_sound");
    CHECK(e0.damageRegionPresent && e0.damageRegionIsBox);
    CHECK(e0.damageRegionType.has_value() && *e0.damageRegionType == "Vehicle");
    CHECK(e0.damageRegionOffset.complete() && e0.damageRegionOffset.value().z == 1.0f);
    CHECK(e0.damageRegionSize.value().x == 2.0f);
    CHECK(e0.stopWhenHostDestroyed.has_value() && *e0.stopWhenHostDestroyed == true);
    CHECK(e0.proximityRefractionPresent && e0.proximityRefractionSituation.has_value() &&
          *e0.proximityRefractionSituation == "dlf_test");
    CHECK(e0.proximityRefractionNearRadius.value == 1.0f);

    // Minimal row: every optional/write-if-present element absent.
    const Effect& e1 = rows[1];
    CHECK(!e1.framework.has_value());
    CHECK(!e1.visual.has_value() && !e1.sound.has_value());
    CHECK(!e1.damageRegionPresent);
    CHECK(!e1.damageRegionOffset.present);  // absent block -> (0,0,0), reported as NOT present
    CHECK(e1.damageRegionOffset.value().x == 0.0f);
    CHECK(!e1.proximityRefractionPresent);
    CHECK(e1.proximityRefractionNearRadius.value == 0.0f && !e1.proximityRefractionNearRadius.present);
}

// ===========================================================================
// 10.2 vfx.xtbl
// ===========================================================================
void testVfx() {
    Document d = P(R"(<root><Table>
      <Effect>
        <Name>vfx_car_explosion</Name>
        <VFX><Filename>vfx_car_explosion.effectx</Filename></VFX>
        <Streaming_Category>Vehicle</Streaming_Category>
        <Radius>5.0</Radius>
        <Radius_expands>false</Radius_expands>
        <Blocker><Opacity>0.5</Opacity><LifeSpan>10</LifeSpan></Blocker>
        <Radial_blur><Radial_blur_entry>large</Radial_blur_entry></Radial_blur>
        <LOD><Spawning><Distance>50</Distance><View>true</View></Spawning>
          <Distance><Fading_start>10</Fading_start><Fading_end>20</Fading_end><Restore>true</Restore></Distance>
          <Update><Minimum_time>0.1</Minimum_time></Update></LOD>
        <Start_time>1.0</Start_time>
      </Effect>
      <Effect><Name>vfx_minimal</Name><Streaming_Category>Cutscene</Streaming_Category></Effect>
      <Effect><Name>vfx_lod_empty</Name><LOD/></Effect>
    </Table></root>)");
    std::vector<VfxEffect> rows = ParseVfxTable(d);
    CHECK(rows.size() == 3);
    const VfxEffect& v0 = rows[0];
    CHECK(v0.name.has_value() && *v0.name == "vfx_car_explosion");
    CHECK(v0.vfxFilename.has_value() && *v0.vfxFilename == "vfx_car_explosion.effectx");
    CHECK(v0.streamingCategory.has_value() && *v0.streamingCategory == "Vehicle");
    CHECK(v0.radius.has_value() && *v0.radius == 5.0f);
    CHECK(v0.radiusExpands.has_value() && *v0.radiusExpands == false);
    CHECK(v0.blockerPresent && v0.blockerOpacity.has_value() && *v0.blockerOpacity == 0.5f);
    CHECK(v0.blockerLifeSpan.value == 10);
    CHECK(v0.radialBlurEntry.has_value() && *v0.radialBlurEntry == "large");
    CHECK(v0.lodPresent);
    CHECK(v0.lodSpawningDistance.has_value() && *v0.lodSpawningDistance == 50.0f);
    CHECK(v0.lodDistanceFadingStart.has_value() && *v0.lodDistanceFadingStart == 10.0f);
    CHECK(v0.lodUpdateMinimumTime.has_value() && *v0.lodUpdateMinimumTime == 0.1f);
    CHECK(v0.startTime.has_value() && *v0.startTime == 1.0f);

    // Minimal row: spec-documented concrete defaults apply.
    const VfxEffect& v1 = rows[1];
    CHECK(v1.streamingCategory.has_value() && *v1.streamingCategory == "Cutscene");
    CHECK(!v1.radius.has_value() && v1.RadiusOrDefault() == 1.0f);
    CHECK(!v1.radiusExpands.has_value() && v1.RadiusExpandsOrDefault() == true);
    CHECK(!v1.blockerPresent && v1.BlockerOpacityOrDefault() == -1.0f);
    CHECK(!v1.lodPresent);
    // FOR TEAM B 2026-10-01 CORRECTION: `LOD` itself is WHOLLY ABSENT here (no <LOD> element at
    // all) - the 1.0e8/1.0e10 defaults do NOT apply in this case (fixed from the prior,
    // unconditional *OrDefault() behaviour); the fields stay 0, matching the zero-filled record
    // the disassembly shows.
    CHECK(v1.LodSpawningDistanceOrDefault() == 0.0f);
    CHECK(v1.LodFadingStartOrDefault() == 0.0f && v1.LodFadingEndOrDefault() == 0.0f);
    CHECK(!v1.startTime.has_value() && v1.StartTimeOrDefault() == 0.0f);

    // `<LOD/>` PRESENT but empty (no Spawning/Distance/Update sub-blocks): THIS is the case the
    // 1.0e8/1.0e10 defaults are actually documented for (spec 10.2) - still correct post-fix.
    const VfxEffect& v2 = rows[2];
    CHECK(v2.lodPresent);
    CHECK(!v2.lodSpawningDistance.has_value() && v2.LodSpawningDistanceOrDefault() == 1.0e8f);
    CHECK(!v2.lodDistanceFadingStart.has_value() && v2.LodFadingStartOrDefault() == 1.0e10f);
    CHECK(!v2.lodDistanceFadingEnd.has_value() && v2.LodFadingEndOrDefault() == 1.0e10f);
}

// ===========================================================================
// 11.1 interface_effects.xtbl
// ===========================================================================
void testInterfaceEffect() {
    Document d = P(R"(<root><Table>
      <InterfaceEffect><Name>Pause</Name><Camera><FOV>60</FOV></Camera><Camera_Blur>0.3</Camera_Blur>
        <LUT><LutName>pause_lut</LutName><LutStrength>0.8</LutStrength></LUT></InterfaceEffect>
      <InterfaceEffect><Name>NoLut</Name><Camera><FOV>70</FOV></Camera></InterfaceEffect>
    </Table></root>)");
    std::vector<InterfaceEffect> rows = ParseInterfaceEffectsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].cameraFov.value == 60.0f);
    CHECK(rows[0].cameraBlur.has_value() && *rows[0].cameraBlur == 0.3f);
    CHECK(rows[0].lutPresent && rows[0].lutName.has_value() && *rows[0].lutName == "pause_lut" &&
          rows[0].lutStrength.value == 0.8f);
    CHECK(!rows[1].lutPresent);
    CHECK(!rows[1].cameraBlur.has_value());
}

// ===========================================================================
// 11.2 decal_info.xtbl
// ===========================================================================
void testDecalInfo() {
    Document d = P(R"(<root><Table>
      <Decal_Info><Name>bullet_concrete</Name><Material><Filename>bullet_concrete.mtl</Filename></Material>
        <Preload>True</Preload><Double_sided>False</Double_sided><Life_Time>5000</Life_Time><Fade_Time>1000</Fade_Time>
        <Width>0.2</Width><Length>0.2</Length><Depth>0.01</Depth>
        <Depth_Fade_Start>0.005</Depth_Fade_Start><Depth_Fade_End>0.01</Depth_Fade_End>
        <Slope_Fade_Start>10</Slope_Fade_Start><Slope_Fade_End>80</Slope_Fade_End>
        <has_normal>True</has_normal><alpha_test>0.5</alpha_test></Decal_Info>
    </Table></root>)");
    std::vector<DecalInfo> rows = ParseDecalInfoTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].materialFilename.has_value() && *rows[0].materialFilename == "bullet_concrete.mtl");
    CHECK(rows[0].preload.value == true && rows[0].doubleSided.value == false);
    CHECK(rows[0].lifeTime.value == 5000 && rows[0].fadeTime.value == 1000);
    CHECK(rows[0].slopeFadeStart.value == 10.0f);  // RAW degrees; no trig applied by this reader
}

// ===========================================================================
// 11.3 groundfires.xtbl
// ===========================================================================
void testGroundfire() {
    Document d = P(R"(<root><Table>
      <Groundfire><Name>car_fire</Name><Damage_Radius>3.0</Damage_Radius><Damage_Region>Engine</Damage_Region>
        <effects><effect>fx_fire_small</effect><effect>fx_fire_smoke</effect></effects>
        <Duration_Min>2.0</Duration_Min><Duration_Max>5.0</Duration_Max></Groundfire>
    </Table></root>)");
    std::vector<Groundfire> rows = ParseGroundfiresTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].effectNames.size() == 2 && rows[0].effectNames[1] == "fx_fire_smoke");
    CHECK(rows[0].durationMinSeconds.value == 2.0f);  // RAW seconds (the ms transform is load-time, spec 11.3)
}

// ===========================================================================
// 11.4 shells.xtbl
// ===========================================================================
void testShell() {
    Document d = P(R"(<root><Table>
      <Shell><Name>pistol_shell</Name><Static_Mesh>shell_pistol.cmesh_pc</Static_Mesh><Collision_Foley>shell_metal</Collision_Foley></Shell>
      <Shell><Name>no_foley</Name><Static_Mesh>shell_rifle.cmesh_pc</Static_Mesh></Shell>
    </Table></root>)");
    std::vector<Shell> rows = ParseShellsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].collisionFoley.has_value() && *rows[0].collisionFoley == "shell_metal");
    CHECK(!rows[1].collisionFoley.has_value());  // absent -> 0 (no handle) at load time
}

// ===========================================================================
// 11.5 material_color_variants.xtbl
// ===========================================================================
void testMaterialColorVariants() {
    Document d = P(R"(<root><Table>
      <color_entry><_Entry_ID>3</_Entry_ID><Color>0.5 0.25 0.75 0.9</Color></color_entry>
      <color_entry><_Entry_ID>4</_Entry_ID><Color>1.0 1.0 1.0</Color></color_entry>
      <color_entry><_Entry_ID>500</_Entry_ID><Color>0 0 0 0</Color></color_entry>
    </Table></root>)");
    std::vector<ColorEntry> rows = ParseMaterialColorVariantsTable(d);
    CHECK(rows.size() == 3);
    CHECK(rows[0].entryId.has_value() && *rows[0].entryId == 3);
    CHECK(rows[0].color.has_value() && near(rows[0].color->a, 0.9f, 1e-6f));
    CHECK(rows[1].color.has_value() && rows[1].color->a == 1.0f);  // unspecified trailing -> 1.0 (spec 11.5)
    // BOUNDARY: spec 11.5 says the loader SKIPS a row whose _Entry_ID > 256
    // (table-level policy, not enforced by ParseColorEntry - the raw value is
    // still surfaced for a caller/harness to apply the rule).
    CHECK(rows[2].entryId.has_value() && *rows[2].entryId == 500);
}

// ===========================================================================
// 11.6 bitmap_materials.xtbl
// ===========================================================================
void testBitmapMaterial() {
    Document d = P(R"(<root><Table>
      <Bitmap_Material><Name>concrete</Name><MaterialProperties><Suffix>CO</Suffix></MaterialProperties>
        <Bullet_Decals><Bullet_Decal>bullet_concrete</Bullet_Decal><Bullet_Decal>bullet_concrete_2</Bullet_Decal></Bullet_Decals>
        <Blast_Decal>blast_concrete</Blast_Decal><Crash_Decal>crash_concrete</Crash_Decal><Audio_Occlusion>2</Audio_Occlusion></Bitmap_Material>
      <Bitmap_Material><Name>overlong</Name><MaterialProperties><Suffix>CON</Suffix></MaterialProperties></Bitmap_Material>
    </Table></root>)");
    std::vector<BitmapMaterial> rows = ParseBitmapMaterialsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "concrete");
    CHECK(rows[0].suffix.has_value() && *rows[0].suffix == "CO");
    CHECK(rows[0].bulletDecals.size() == 2);
    CHECK(rows[0].audioOcclusion.has_value() && *rows[0].audioOcclusion == 2);
    CHECK(kBitmapMaterialSlotNames[0] == "concrete");  // slot 0 of the 33-name table (spec 11.6)
    // BOUNDARY: spec 11.6 states the destination is char[3] (room for 2 chars
    // + NUL); CopyText's bounded-copy semantics (xtbl.h) truncate a 3-char
    // value "CON" to "CO", matching the real destination's capacity exactly.
    CHECK(rows[1].suffix.has_value() && *rows[1].suffix == "CO");
}

// ===========================================================================
// 11.7 bitmap_sheets.xtbl
// ===========================================================================
void testBitmapSheet() {
    Document d = P("<root><Table><BitmapSheets><Name>ui_atlas_1</Name></BitmapSheets><BitmapSheets/></Table></root>");
    std::vector<BitmapSheet> rows = ParseBitmapSheetsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "ui_atlas_1");
    CHECK(!rows[1].name.has_value());
    CHECK(rows[1].NameOrDefault().empty());  // spec-documented default: empty string
}

// ===========================================================================
// 11.8 map_districts.xtbl
// ===========================================================================
void testMapDistrict() {
    Document d = P(R"(<root><Table>
      <Map_district><Name>Downtown</Name><data_item_name>di_downtown</data_item_name><team_name>Saints</team_name>
        <contact_icon>icon_downtown</contact_icon><contact_name>Downtown District</contact_name>
        <text_location><X>100.5</X><Y>20.0</Y><Z>-50.25</Z></text_location></Map_district>
    </Table></root>)");
    std::vector<MapDistrict> rows = ParseMapDistrictsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].textLocationX.value == 100.5f);
    CHECK(rows[0].textLocationZ.value == -50.25f);
    // Y is captured even though the engine itself never stores it (spec 11.8).
    CHECK(rows[0].textLocationY.value == 20.0f);
}

// ===========================================================================
// 11.9 fade_categories.xtbl
// ===========================================================================
void testFadeCategory() {
    Document d = P(R"(<root><Table>
      <Category><Name>vehicle</Name><medium_lod_distance>80</medium_lod_distance><low_lod_distance>250</low_lod_distance><Distance>600</Distance></Category>
      <Category><Name>bare</Name></Category>
    </Table></root>)");
    std::vector<FadeCategory> rows = ParseFadeCategoriesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].mediumLodDistance.value == 80.0f);  // RAW; the engine squares it before storing (spec 11.9)
    // The always-write accessor is used: absent means the Always-hazard
    // 0-stand-in, NOT the documented 60.0/200.0/500.0 defaults (spec 11.9's
    // own caveat - see the struct's comment in tables.h).
    CHECK(!rows[1].mediumLodDistance.present && rows[1].mediumLodDistance.value == 0.0f);
}

// ===========================================================================
// 12.1 camera_shake.xtbl
// ===========================================================================
void testCameraShake() {
    Document d = P(R"(<root><Table>
      <Camera_Shake><Name>Explosion</Name>
        <Wobble><Pitch>1</Pitch><Roll>2</Roll><Yaw>3</Yaw><Variation>0.1</Variation></Wobble>
        <Wander><Pitch>0.5</Pitch><Yaw>0.5</Yaw><Frequency>2</Frequency></Wander>
        <Strength_Graph>
          <Strength_Element><Time>0</Time><Wobble>1</Wobble></Strength_Element>
          <Strength_Element><Time>1</Time><Wobble>0</Wobble></Strength_Element>
        </Strength_Graph>
      </Camera_Shake>
    </Table></root>)");
    std::vector<CameraShake> rows = ParseCameraShakeTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].wobblePitch.value == 1.0f && rows[0].wobbleRoll.value == 2.0f);
    CHECK(rows[0].wanderFrequency.value == 2.0f);
    CHECK(rows[0].strengthGraph.size() == 2);
    CHECK(rows[0].strengthGraph[1].time.value == 1.0f && rows[0].strengthGraph[1].wobble.value == 0.0f);
}

// ===========================================================================
// 3. weather_time_of_day.xtbl - presence-bit and colour+intensity semantics
// ===========================================================================
void testWeatherTimeOfDay() {
    Document d = P(R"(<root><Table>
      <Weather_Time_Segment><Name>Morning</Name><Start_Time>600</Start_Time><Ramp_Out_Time>30</Ramp_Out_Time>
        <Weather_Stages>
          <Stage><Stage_Name>Clear Skies</Stage_Name>
            <District_Lighting>
              <Lighting_Parameters>
                <TOD_Light_Color><R>1.0</R><G>0.9</G><B>0.8</B></TOD_Light_Color>
                <TOD_Light_Color_Intensity>1.5</TOD_Light_Color_Intensity>
                <Fog_Ground>0.1</Fog_Ground>
              </Lighting_Parameters>
            </District_Lighting>
            <District_Skybox><West1><R>0.2</R><G>0.3</G><B>0.4</B></West1></District_Skybox>
            <ldr_min>0.0</ldr_min><ldr_max>1.0</ldr_max>
            <particle_ambient_color>0.1 0.2 0.3 1.0</particle_ambient_color>
            <Orbital_Objects><Object><Object_Name>02-noonsun</Object_Name><Tint><R>1</R><G>1</G><B>0.9</B></Tint><Opacity>0.8</Opacity></Object></Orbital_Objects>
          </Stage>
        </Weather_Stages>
      </Weather_Time_Segment>
      <Weather_Time_Segment><Name>Negative</Name><Start_Time>-100</Start_Time><Ramp_Out_Time>-5</Ramp_Out_Time></Weather_Time_Segment>
    </Table></root>)");
    std::vector<WeatherTimeSegment> segs = ParseWeatherTimeOfDayTable(d);
    CHECK(segs.size() == 2);
    CHECK(segs[0].startTimeHHMM.value == 600);
    CHECK(segs[0].stages.size() == 1);
    // FOR TEAM B 2026-10-01 CORRECTION: Start_Time/Ramp_Out_Time use the SIGNED always-write
    // accessor (0x00DABC70), not the unsigned one - a leading '-' is a real negative value, not
    // 0 (the unsigned-accessor rule does not apply to this field).
    CHECK(segs[1].startTimeHHMM.present && segs[1].startTimeHHMM.value == -100);
    CHECK(segs[1].rampOutTimeMinutes.present && segs[1].rampOutTimeMinutes.value == -5);
    const WeatherTimeOfDayCell& c = segs[0].stages[0];
    CHECK(c.stageName.has_value() && *c.stageName == "Clear Skies");
    // PRESENCE-BIT semantics (spec 3.2/3.3): element found == present.
    CHECK(c.lighting.todLightColor.present);
    CHECK(near(c.lighting.todLightColor.rgb.g.value, 0.9f, 1e-6f));
    CHECK(c.lighting.todLightColor.intensityPresent && c.lighting.todLightColor.intensity == 1.5f);
    CHECK(c.lighting.fogGround.has_value() && *c.lighting.fogGround == 0.1f);
    // Never mentioned: absent -> not present / nullopt (the "inherit" sparse-override rule).
    CHECK(!c.lighting.ambientColor.present);
    CHECK(!c.lighting.fogDensity.has_value());
    // A colour with NO "_Intensity" sibling: spec-documented default 1.0.
    CHECK(c.west1.present && !c.west1.intensityPresent && c.west1.intensity == 1.0f);
    CHECK(!c.west2.present);
    CHECK(c.particleAmbientColor.has_value() && c.particleAmbientColor->z == 0.3f);
    CHECK(c.orbitalObjects.size() == 1 && c.orbitalObjects[0].objectName.has_value() &&
          *c.orbitalObjects[0].objectName == "02-noonsun");
    CHECK(c.orbitalObjects[0].tintPresent && c.orbitalObjects[0].opacity.has_value() &&
          *c.orbitalObjects[0].opacity == 0.8f);
    CHECK(!c.orbitalObjects[0].scale.has_value());
}

// ===========================================================================
// 12.2 camera_free.xtbl - whole-file, plus the group's FLAG-LIST table
// ===========================================================================
void testCameraFree() {
    Document empty = P("<root><Table></Table></root>");
    CHECK(!ParseCameraFree(empty).has_value());

    Document d = P(R"(<root><Table><Camera>
      <panning_group>
        <fast_pan_horizontal_multiplier>1.1</fast_pan_horizontal_multiplier>
        <tank_horiz_dampening_genki_mouse>0.7</tank_horiz_dampening_genki_mouse>
        <interior_v_dampening_mouse>0.6</interior_v_dampening_mouse>
      </panning_group>
      <miscellany_group><pitch_reset_time>0.5</pitch_reset_time></miscellany_group>
      <submodes>
        <submode><name>fine aim</name><min_elevation>-80</min_elevation><max_elevation>80</max_elevation>
          <default_elevation>10</default_elevation><z_dist>5</z_dist><y_dist>-2</y_dist>
          <base_fov>40</base_fov><lookat_offset><X>0</X><Y>1</Y><Z>0</Z></lookat_offset></submode>
        <submode><name>fine aim</name><min_elevation>-90</min_elevation><max_elevation>90</max_elevation>
          <base_fov>45</base_fov></submode>
        <submode><name>zoom</name><min_elevation>0</min_elevation><max_elevation>0</max_elevation>
          <base_fov>50</base_fov></submode>
      </submodes>
      <vehicle_fallbacks><vehicle_fallback><name>nitrous</name><distance>10</distance><rampin>1</rampin><duration>2</duration><return>3</return></vehicle_fallback></vehicle_fallbacks>
      <Vehicle_Aims><Vehicle_Aim><name>tank</name><Min_Pitch>-10</Min_Pitch><Max_pitch>10</Max_pitch>
        <flags><Flag>NoZoom</Flag><Flag>Locked</Flag></flags></Vehicle_Aim></Vehicle_Aims>
    </Camera></Table></root>)");
    std::optional<CameraFree> cf = ParseCameraFree(d);
    CHECK(cf.has_value());
    CHECK(cf->panning.fastPanHorizontalMultiplier.value == 1.1f);
    // CONFIRMED literal destination names (spec 12.2).
    CHECK(cf->panning.tankGenkiDampeningMouse.horiz.value == 0.7f);
    CHECK(cf->panning.interiorVDampeningMouse.value == 0.6f);
    CHECK(cf->miscellany.pitchResetTime.value == 0.5f);
    // FOR TEAM B 2026-10-01 CORRECTION: min_elevation/max_elevation/default_elevation are
    // authored in degrees but stored in radians (x 0.017453, the exact disassembly literal).
    CHECK(cf->submodes.size() == 3 && cf->submodes[0].name.has_value() && *cf->submodes[0].name == "fine aim");
    CHECK(near(cf->submodes[0].minElevation.value, -80.0f * 0.017453f, 1e-6f));
    CHECK(near(cf->submodes[0].maxElevation.value, 80.0f * 0.017453f, 1e-6f));
    CHECK(cf->submodes[0].defaultElevation.has_value() &&
          near(*cf->submodes[0].defaultElevation, 10.0f * 0.017453f, 1e-6f));  // "+0x18" == has_value()
    CHECK(cf->submodes[0].lookatOffset.complete() && cf->submodes[0].lookatOffset.value().y == 1.0f);
    // z_dist/y_dist: always-write reads (hazard-free here since both are present), NOT a
    // persisted record field - kept only because they are real data this row supplies.
    CHECK(cf->submodes[0].zDist.present && cf->submodes[0].zDist.value == 5.0f);
    CHECK(cf->submodes[0].yDist.present && cf->submodes[0].yDist.value == -2.0f);
    // Absent default_elevation elsewhere -> not present (the "+0x18 clear" case).
    CHECK(!cf->submodes[1].defaultElevation.has_value());
    // Two rows name "fine aim" (after the engine's own runtime Aspect filter, not evaluated
    // here): this reader keeps BOTH, in document order, in a plain vector - a caller folding
    // forward by name (last write wins) reproduces the engine's "later duplicate overwrites the
    // earlier" slot-array rule exactly; no first-wins / duplicate-error bug exists to fix here.
    CHECK(cf->submodes[1].name.has_value() && *cf->submodes[1].name == "fine aim");
    CHECK(near(cf->submodes[1].minElevation.value, -90.0f * 0.017453f, 1e-6f));
    CHECK(cf->vehicleFallbacks.size() == 1 && cf->vehicleFallbacks[0].returnValue.value == 3);
    // FLAG-LIST table (spec 12.2: "flags/Flag ... name->bit map NOT decoded"):
    // raw flag text captured verbatim, no bit meaning assigned.
    CHECK(cf->vehicleAims.size() == 1);
    CHECK(cf->vehicleAims[0].flagNames.size() == 2);
    CHECK(cf->vehicleAims[0].flagNames[0] == "NoZoom" && cf->vehicleAims[0].flagNames[1] == "Locked");
}

}  // namespace

int main() {
    try {
        testWeatherStage();
        testWindStage();
        testRainLevel();
        testLightningType();
        testLensFlare();
        testMotionBlur();
        testRadialBlur();
        testDofSituation();
        testRefractionSituation();
        testSkyboxEffect();
        testTimeOfDayObjects();
        testExternalLightOverride();
        testEffects();
        testVfx();
        testInterfaceEffect();
        testDecalInfo();
        testGroundfire();
        testShell();
        testMaterialColorVariants();
        testBitmapMaterial();
        testBitmapSheet();
        testMapDistrict();
        testFadeCategory();
        testCameraShake();
        testWeatherTimeOfDay();
        testCameraFree();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}
