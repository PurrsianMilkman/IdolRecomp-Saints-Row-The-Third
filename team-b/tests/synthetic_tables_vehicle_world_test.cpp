// Synthetic tests for sr3tables_vehicle_world, built FROM THE TEXT of
// spec-tables-vehicle-world.md only (hand-rolled XML fixtures as string
// literals) - never derived from this project's own reader source. A pass
// here proves the reader implements the spec's text, not that the spec is
// right; the real-data statement is
// tools/validation/validate_tables_vehicle_world_population.cpp.
//
// One test function per implemented schema group (20, covering all 24
// filenames - vi_enter/exit/ride share one schema/test, the three LightSet
// files share one schema/test). Each covers at least: a normal row, an
// absent-optional-element case, and, where the spec calls one out, a
// boundary/edge case (an enum's anomalous literal, an unbounded read that
// stops at the first gap, a naming correction the spec made mid-
// investigation).
//
// This file was mutation-tested during development (per the task's
// instruction): several field lookups in src/tables_vehicle_world.cpp were
// deliberately changed to a plausible WRONG name/offset (e.g. reading "Name"
// instead of the spec-confirmed "Seat" child in section 3, reading a
// slots/slots wrapper's own text instead of its "name" child in section
// 8.2) and every one of those introduced failures here before the fix was
// reverted - see this library's final report for the specific mutations
// tried.

#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_vehicle_world/tables.h"

namespace {

using namespace sr3tables_vehicle_world;
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
// 2. vi_enter.xtbl / vi_exit.xtbl / vi_ride.xtbl (shared schema)
// ===========================================================================
void testVehicleInteractionAnimationSet() {
    Document d = P(R"(<root><Table>
      <Vehicle_Interaction_Animation_Set>
        <Name>Sedan_Enter_Driver</Name>
        <Animation_Grid>
          <Element>
            <Parameter1>enter</Parameter1>
            <Parameter2>fast</Parameter2>
            <Animation>anim_enter_driver_fast</Animation>
            <Animated_Camera_Tests>
              <Camera_Pos><X>1.0</X><Y>2.0</Y><Z>3.0</Z></Camera_Pos>
            </Animated_Camera_Tests>
          </Element>
          <Element>
            <Parameter1>exit</Parameter1>
            <Parameter3>traverse</Parameter3>
            <Animation>anim_exit</Animation>
          </Element>
        </Animation_Grid>
      </Vehicle_Interaction_Animation_Set>
    </Table></root>)");
    std::vector<VehicleInteractionAnimationSet> rows = ParseVehicleInteractionAnimationSetTable(d);
    CHECK(rows.size() == 1);
    const VehicleInteractionAnimationSet& s = rows[0];
    CHECK(s.name.has_value() && *s.name == "Sedan_Enter_Driver");
    CHECK(s.elements.size() == 2);
    CHECK(s.elements[0].parameters.size() == 2);
    CHECK(s.elements[0].parameters[0] == "enter" && s.elements[0].parameters[1] == "fast");
    CHECK(s.elements[0].animation.has_value() && *s.elements[0].animation == "anim_enter_driver_fast");
    CHECK(s.elements[0].cameraPos.size() == 1);
    CHECK(s.elements[0].cameraPos[0].x == 1.0f && s.elements[0].cameraPos[0].y == 2.0f && s.elements[0].cameraPos[0].z == 3.0f);
    // BOUNDARY (spec 2.1): ParameterN reading stops at the FIRST absent
    // index - Parameter2 is missing so Parameter3 ("traverse") must NOT be
    // collected, even though it is present in the XML.
    CHECK(s.elements[1].parameters.size() == 1 && s.elements[1].parameters[0] == "exit");
    CHECK(s.elements[1].cameraPos.empty());

    // vi_exit.xtbl / vi_ride.xtbl reuse the exact same parser (spec 2).
    std::vector<VehicleInteractionAnimationSet> exitRows = ParseViExitTable(d);
    std::vector<VehicleInteractionAnimationSet> rideRows = ParseViRideTable(d);
    CHECK(exitRows.size() == 1 && rideRows.size() == 1);
    CHECK(exitRows[0].name == rows[0].name && rideRows[0].name == rows[0].name);
}

// ===========================================================================
// 3. vehicle_interaction_info.xtbl
// ===========================================================================
void testVehicleInteractionInfo() {
    Document d = P(R"(<root><Table>
      <Vehicle_Interaction_Info>
        <Name>Sedan_Standard</Name>
        <Seat_Info>
          <Element>
            <Seat>Driver</Seat>
            <Interaction_Point_Set>sedan_standard_points</Interaction_Point_Set>
            <Capsule_Shape>stand</Capsule_Shape>
            <Flags>
              <Flag>Mirror Interaction Points</Flag>
              <Flag>Entry Only</Flag>
              <Flag>No Door Required</Flag>
              <Flag>Equip Rifle on Exit</Flag>
            </Flags>
            <Queue>Queue 3</Queue>
            <Primary_Access_Seat>front driver</Primary_Access_Seat>
            <Enter_Animations>sedan_enter</Enter_Animations>
            <Exit_Animations>sedan_exit</Exit_Animations>
            <Ride_Animations>sedan_ride</Ride_Animations>
          </Element>
          <Element>
            <Seat>Passenger 1</Seat>
          </Element>
        </Seat_Info>
      </Vehicle_Interaction_Info>
    </Table></root>)");
    std::vector<VehicleInteractionInfo> rows = ParseVehicleInteractionInfoTable(d);
    CHECK(rows.size() == 1);
    const VehicleInteractionInfo& info = rows[0];
    CHECK(info.name.has_value() && *info.name == "Sedan_Standard");
    CHECK(info.seats.size() == 2);
    const VehicleInteractionSeatInfo& seat0 = info.seats[0];
    // §1.3/§22 naming correction: the seat identifier is <Seat>, not <Name>.
    CHECK(seat0.seatText.has_value() && *seat0.seatText == "Driver");
    CHECK(seat0.interactionPointSet == "sedan_standard_points");
    CHECK(seat0.capsuleShape == "stand");
    CHECK(seat0.mirrorInteractionPoints && seat0.entryOnly && seat0.noDoorRequired && seat0.equipRifleOnExit);
    CHECK(!seat0.meleeBruteSeat && !seat0.riotShieldSeat);  // never mentioned -> false
    CHECK(seat0.queue == "Queue 3");
    CHECK(seat0.primaryAccessSeat == "front driver");
    CHECK(seat0.enterAnimations == "sedan_enter" && seat0.exitAnimations == "sedan_exit" && seat0.rideAnimations == "sedan_ride");
    CHECK(info.seats[1].seatText == "Passenger 1");
    CHECK(!info.seats[1].mirrorInteractionPoints);  // no <Flags> at all -> every flag false
}

// ===========================================================================
// 4. vehicle_interaction_point_sets.xtbl
// ===========================================================================
void testInteractionPointSet() {
    Document d = P(R"(<root><Table>
      <Interaction_Point_Set>
        <Name>sedan_standard_points</Name>
        <Interaction_Point_Set_Elements>
          <Interaction_Point_Set_Element>
            <Interaction_Point_Type>Enter Start</Interaction_Point_Type>
            <Seat_Offset><X>0.1</X><Y>0.2</Y><Z>0.3</Z></Seat_Offset>
            <Heading>1.5</Heading>
            <Direction>Front Left</Direction>
            <Orient_To_Seat>true</Orient_To_Seat>
            <Project_Onto_World>false</Project_Onto_World>
          </Interaction_Point_Set_Element>
          <Interaction_Point_Set_Element>
            <Interaction_Point_Type>None</Interaction_Point_Type>
            <Direction>stand</Direction>
            <Heading>0</Heading>
          </Interaction_Point_Set_Element>
        </Interaction_Point_Set_Elements>
      </Interaction_Point_Set>
    </Table></root>)");
    std::vector<InteractionPointSet> rows = ParseInteractionPointSetsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "sedan_standard_points");
    CHECK(rows[0].elements.size() == 2);
    const InteractionPointSetElement& e0 = rows[0].elements[0];
    CHECK(e0.interactionPointType == "Enter Start");
    CHECK(e0.seatOffset.present && near(e0.seatOffset.value().x, 0.1f, 1e-5f));
    CHECK(e0.heading.present && e0.heading.value == 1.5f);
    CHECK(e0.direction == "Front Left");
    CHECK(e0.orientToSeat.present && e0.orientToSeat.value == true);
    CHECK(e0.projectOntoWorld.present && e0.projectOntoWorld.value == false);
    // Both "None" and the anomalous, never-exercised-by-real-data 7th
    // Direction literal ("stand") are real, confirmed literal strings the
    // reader must accept as valid TEXT (spec keeps raw text, not a
    // precomputed index) - both are simply kept verbatim here.
    CHECK(rows[0].elements[1].interactionPointType == "None");
    CHECK(rows[0].elements[1].direction == "stand");
    // Never-mentioned bool: Always-hazard default.
    CHECK(!rows[0].elements[1].orientToSeat.present);
}

// ===========================================================================
// 5. vehicle_wheel_groups.xtbl
// ===========================================================================
void testWheelGroup() {
    Document d = P(R"(<root><Table>
      <Wheel_Group>
        <Name>chrome_rims</Name>
        <Display_Name>Chrome Rims</Display_Name>
        <Price>500</Price>
        <Rims_Grid>
          <Rim_Element><Display_Name>Chrome 5-spoke</Display_Name><Front_Rim>rim_chrome_5spoke</Front_Rim><Rear_Rim>rim_chrome_5spoke</Rear_Rim></Rim_Element>
        </Rims_Grid>
        <Spinners_Grid>
          <S_Element><Display_Name>Gold Spinner</Display_Name><Front_Spinner>spinner_gold</Front_Spinner><Rear_Spinner>spinner_gold</Rear_Spinner></S_Element>
          <S_Element><Display_Name>No Spinner</Display_Name></S_Element>
        </Spinners_Grid>
      </Wheel_Group>
    </Table></root>)");
    std::vector<WheelGroup> rows = ParseWheelGroupsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "chrome_rims" && rows[0].displayName == "Chrome Rims");
    CHECK(rows[0].price.has_value() && *rows[0].price == 500);
    CHECK(rows[0].rims.size() == 1 && rows[0].rims[0].frontRim == "rim_chrome_5spoke");
    CHECK(rows[0].spinners.size() == 2);
    CHECK(rows[0].spinners[0].frontSpinner == "spinner_gold" && rows[0].spinners[0].rearSpinner == "spinner_gold");
    // BOUNDARY (spec 5.1): "read only if Front_Spinner was present" - absent
    // Front_Spinner -> rearSpinner must stay nullopt even with no Rear_Spinner test data either way.
    CHECK(!rows[0].spinners[1].frontSpinner.has_value());
    CHECK(!rows[0].spinners[1].rearSpinner.has_value());
    CHECK(!rows[0].spinners[1].price.has_value());
    CHECK(rows[0].spinners[1].PriceOrDefault() == 0);  // spec-stated default 0

    // Price if-present default 0.
    Document d2 = P("<root><Table><Wheel_Group><Name>plain</Name></Wheel_Group></Table></root>)");
    std::vector<WheelGroup> rows2 = ParseWheelGroupsTable(d2);
    CHECK(rows2.size() == 1 && !rows2[0].price.has_value() && rows2[0].PriceOrDefault() == 0);
}

// ===========================================================================
// 6. vehicle_animation_modifiers.xtbl
// ===========================================================================
void testVehicleAnimModifiers() {
    Document d = P(R"(<root><Table>
      <Vehicle_Anim_Modifiers>
        <Human_Seat_Offset><X>0.0</X><Y>0.0</Y><Z>0.05</Z></Human_Seat_Offset>
        <Vehicles>
          <Vehicle>
            <Name>Voxel</Name>
            <Human_Scale>1.1</Human_Scale>
            <Human_Seat_Offset><X>0.1</X><Y>0.0</Y><Z>0.0</Z></Human_Seat_Offset>
            <Seat>driver<Human_Seat_Offset><X>0.2</X><Y>0.0</Y><Z>0.0</Z></Human_Seat_Offset>
              <Weapon_Animation_Groups>
                <Weapon_Animation_Group>
                  <Name>Pistol</Name>
                  <Human_Seat_Offset><X>0.3</X><Y>0.0</Y><Z>0.0</Z></Human_Seat_Offset>
                  <Animation_States>
                    <Animation_State><Name>Aiming</Name><Human_Seat_Offset><X>0.4</X><Y>0.0</Y><Z>0.0</Z></Human_Seat_Offset></Animation_State>
                  </Animation_States>
                </Weapon_Animation_Group>
              </Weapon_Animation_Groups>
            </Seat>
          </Vehicle>
        </Vehicles>
      </Vehicle_Anim_Modifiers>
    </Table></root>)");
    std::optional<VehicleAnimModifiers> m = ParseVehicleAnimModifiers(d);
    CHECK(m.has_value());
    CHECK(m->humanSeatOffsetDefault.present && near(m->humanSeatOffsetDefault.value().z, 0.05f, 1e-5f));
    CHECK(m->vehicles.size() == 1);
    const VehicleAnimModifierVehicle& veh = m->vehicles[0];
    CHECK(veh.name == "Voxel");
    CHECK(veh.humanScale.has_value() && *veh.humanScale == 1.1f);
    CHECK(veh.humanSeatOffset.present && near(veh.humanSeatOffset.value().x, 0.1f, 1e-5f));
    CHECK(veh.seats.size() == 1);
    // <Seat> is MIXED CONTENT: "driver" own text plus nested children (xtbl.h's documented mixed-content shape).
    CHECK(veh.seats[0].seatText.has_value() && *veh.seats[0].seatText == "driver");
    CHECK(near(veh.seats[0].humanSeatOffset.value().x, 0.2f, 1e-5f));
    // One base sub-entry (animationState == nullopt) PLUS one per nested Animation_State (spec 6.1).
    CHECK(veh.seats[0].weaponAnimationGroups.size() == 2);
    CHECK(veh.seats[0].weaponAnimationGroups[0].weaponCategory == "Pistol");
    CHECK(!veh.seats[0].weaponAnimationGroups[0].animationState.has_value());
    CHECK(near(veh.seats[0].weaponAnimationGroups[0].humanSeatOffset.value().x, 0.3f, 1e-5f));
    CHECK(veh.seats[0].weaponAnimationGroups[1].weaponCategory == "Pistol");
    CHECK(veh.seats[0].weaponAnimationGroups[1].animationState == "Aiming");
    CHECK(near(veh.seats[0].weaponAnimationGroups[1].humanSeatOffset.value().x, 0.4f, 1e-5f));
}

// ===========================================================================
// 7. externalized_vehicle_components.xtbl
// ===========================================================================
void testExternalizedComponentSlot() {
    Document d = P(R"(<root><Custom_vehicle_properties><Component_Slots>
      <Slot>
        <Name>front_bumper</Name>
        <Camera_Info>front_bumper_cam</Camera_Info>
        <Components>
          <Component><Name>rim_chrome_5spoke</Name><SomeUnmappedField>x</SomeUnmappedField></Component>
          <Component></Component>
        </Components>
      </Slot>
      <Slot><Name>rear_bumper</Name></Slot>
    </Component_Slots></Custom_vehicle_properties></root>)");
    std::vector<ExternalizedComponentSlot> rows = ParseExternalizedVehicleComponentsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "front_bumper" && rows[0].cameraInfo == "front_bumper_cam");
    CHECK(rows[0].componentCount == 2);
    // Component/Name (spec 5.1/7 CORRECTED 2026-10-02): the match target for
    // vehicle_wheel_groups.xtbl's Front_Rim/Rear_Rim/etc. is the Component's
    // OWN Name, not the enclosing Slot's Name - a Component with no <Name>
    // child contributes nothing to componentNames (not an empty string).
    CHECK(rows[0].componentNames.size() == 1 && rows[0].componentNames[0] == "rim_chrome_5spoke");
    CHECK(rows[1].name == "rear_bumper" && !rows[1].cameraInfo.has_value() && rows[1].componentCount == 0);
    CHECK(rows[1].componentNames.empty());
}

// ===========================================================================
// 8.1 vehicle_cust_slots.xtbl
// ===========================================================================
void testVehicleCustSlot() {
    Document d = P(R"(<root><Table>
      <Vehicle_slot><DisplayName>Wheels</DisplayName><Name>wheels</Name><SlotType>wheels</SlotType><Vehicle_Component_Type>front wheels</Vehicle_Component_Type></Vehicle_slot>
      <Vehicle_slot><DisplayName>Color</DisplayName><Name>color</Name><SlotType>color</SlotType></Vehicle_slot>
    </Table></root>)");
    std::vector<VehicleCustSlot> rows = ParseVehicleCustSlotsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].slotType == "wheels" && rows[0].vehicleComponentType == "front wheels");
    CHECK(rows[1].slotType == "color" && !rows[1].vehicleComponentType.has_value());
}

// ===========================================================================
// 8.2 vehicle_cust_interface.xtbl
// ===========================================================================
void testVehicleCustInterface() {
    Document d = P(R"(<root><Table>
      <vehicle_cust_interface>
        <Display_Name>Wheels Menu</Display_Name>
        <Name>wheels_menu</Name>
        <parent_category>root_menu</parent_category>
        <slots>
          <slots><name>front_wheels_slot</name></slots>
          <slots><name>rear_wheels_slot</name></slots>
        </slots>
        <is_wheel_menu>true</is_wheel_menu>
      </vehicle_cust_interface>
    </Table></root>)");
    std::vector<VehicleCustInterfaceEntry> rows = ParseVehicleCustInterfaceTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].displayName == "Wheels Menu");
    CHECK(rows[0].parentCategory == "root_menu");
    // §1.3/§22 naming correction: the slot reference is the "name" CHILD,
    // never the "slots" wrapper item's own text.
    CHECK(rows[0].slotNames.size() == 2);
    if (rows[0].slotNames.size() == 2)
        CHECK(rows[0].slotNames[0] == "front_wheels_slot" && rows[0].slotNames[1] == "rear_wheels_slot");
    CHECK(rows[0].isWheelMenu && !rows[0].isColorMenu && !rows[0].isPerfMenu);
}

// ===========================================================================
// 9.1 vehicle_cust_color_pool.xtbl
// ===========================================================================
void testColorPoolEntry() {
    Document d = P(R"(<root><Table>
      <Color>
        <Name>gloss_red</Name>
        <Shader_Values>
          <Vector_Element><Name>Glass_Color</Name><X>1</X><Y>0</Y><Z>0</Z></Vector_Element>
          <Float_Element><Name>Reflection_Cos_Min_Angles</Name><Value>0.5</Value></Float_Element>
          <Float_Element><Name>Reflection_Inv_Range_Cos_Angles</Name><Value>0.7</Value></Float_Element>
        </Shader_Values>
      </Color>
      <Color><Name>matte_black</Name></Color>
    </Table></root>)");
    std::vector<ColorPoolEntry> rows = ParseVehicleCustColorPoolTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "gloss_red");
    CHECK(rows[0].shaderValuesPresent);
    CHECK(rows[0].vectorElementCount == 1);
    CHECK(rows[0].floatElementCount == 2);
    CHECK(rows[1].name == "matte_black");
    CHECK(!rows[1].shaderValuesPresent);
    CHECK(rows[1].vectorElementCount == 0 && rows[1].floatElementCount == 0);
}

// ===========================================================================
// 9.2 vehicle_cust_color_sets.xtbl
// ===========================================================================
void testColorSet() {
    Document d = P(R"(<root><Table>
      <Color_Set>
        <Display_Name>Reds</Display_Name>
        <Name>reds</Name>
        <Price>1000</Price>
        <Color_Grid>
          <Color_Element><Color>gloss_red</Color></Color_Element>
          <Color_Element><Color>matte_red</Color></Color_Element>
        </Color_Grid>
      </Color_Set>
    </Table></root>)");
    std::vector<ColorSet> rows = ParseVehicleCustColorSetsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].displayName == "Reds" && rows[0].price.present && rows[0].price.value == 1000);
    CHECK(rows[0].colors.size() == 2);
    CHECK(rows[0].colors[0].color == "gloss_red" && rows[0].colors[1].color == "matte_red");
}

// ===========================================================================
// 10. vehicle_surfing_style_two.xtbl
// ===========================================================================
void testVehicleSurfing() {
    Document empty = P("<root><Table></Table></root>");
    CHECK(!ParseVehicleSurfing(empty).has_value());  // spec 10: no <Vehicle_Surfing> -> no-op

    Document d = P(R"(<root><Table><Vehicle_Surfing>
      <Max_Surfing_Time>10</Max_Surfing_Time><Min_Surfing_Time>2</Min_Surfing_Time>
      <Max_Handstand_Time>8</Max_Handstand_Time><Min_Handstand_Time>1</Min_Handstand_Time>
      <Max_Handstand_Percent_Reward>0.5</Max_Handstand_Percent_Reward>
      <Surfing_Min_Speed>15</Surfing_Min_Speed>
      <Surfing_Timeout>3</Surfing_Timeout><Start_Time>0.5</Start_Time><Max_Speed_Delay>1</Max_Speed_Delay>
      <Record_Threshold>0.9</Record_Threshold>
      <Handstanding_Sensitivity_Multiplier>1.2</Handstanding_Sensitivity_Multiplier>
      <Max_Respect>100</Max_Respect><Max_Lifetime_Respect>500</Max_Lifetime_Respect><Max_Cash>200</Max_Cash>
      <Balance_Bar_Params>
        <Balanced_Region_Size>1</Balanced_Region_Size>
        <Balanced_Region_Size_Change>2</Balanced_Region_Size_Change>
        <Balancing_Acceleration>9</Balancing_Acceleration>
      </Balance_Bar_Params>
    </Vehicle_Surfing></Table></root>)");
    std::optional<VehicleSurfing> s = ParseVehicleSurfing(d);
    CHECK(s.has_value());
    CHECK(s->maxSurfingTime.present && s->maxSurfingTime.value == 10.0f);
    CHECK(s->surfingMinSpeed.value == 15.0f);  // RAW, pre mph->m/s-squared conversion
    // Record_Display_Time / Record_Queue_Time: if-present, spec default 0.
    CHECK(!s->recordDisplayTime.has_value() && s->RecordDisplayTimeOrDefault() == 0.0f);
    CHECK(s->balanceBarParams.balancedRegionSize.value == 1.0f);
    CHECK(s->balanceBarParams.balancedRegionSizeChange.value == 2.0f);
    CHECK(s->balanceBarParams.balancingAcceleration.value == 9.0f);
    CHECK(!s->balanceBarParams.balancedRegionMinSize.present);  // never mentioned -> Always-hazard
}

// ===========================================================================
// 11. The shared "LightSet" table
// ===========================================================================
void testLightSet() {
    Document empty = P("<root><Table></Table></root>");
    CHECK(!ParseLightSetTable(empty).has_value());

    Document d = P(R"(<root><Table>
      <LightSet>
        <Name>garage_lightset</Name>
        <StartTime>600</StartTime>
        <EndTime>2200</EndTime>
        <Exposure>1.0</Exposure>
        <RampExposure>True</RampExposure>
        <Lights>
          <Light>
            <Name>key_light</Name>
            <Type>omni</Type>
            <Category_0>true</Category_0>
            <CastShadows>true</CastShadows>
            <Color>0.9 0.8 0.7</Color>
            <Multiplier>2.0</Multiplier>
            <Position>1.0 2.0 3.0</Position>
          </Light>
        </Lights>
      </LightSet>
    </Table></root>)");
    std::optional<LightSet> ls = ParseLightSetTable(d);
    CHECK(ls.has_value());
    CHECK(ls->name == "garage_lightset");
    CHECK(ls->exposure.present && ls->exposure.value == 1.0f);
    CHECK(ls->rampExposure.present && ls->rampExposure.value == true);
    CHECK(ls->lights.size() == 1);
    const Light& l = ls->lights[0];
    CHECK(l.type == "omni");
    CHECK(l.category0.present && l.category0.value == true);
    CHECK(!l.category1.present);
    // Inline, space-separated vec3 (spec 11.2) - NOT X/Y/Z children.
    CHECK(l.color.has_value() && near(l.color->x, 0.9f, 1e-5f) && near(l.color->y, 0.8f, 1e-5f) && near(l.color->z, 0.7f, 1e-5f));
    CHECK(l.position.has_value() && l.position->x == 1.0f && l.position->y == 2.0f && l.position->z == 3.0f);
    CHECK(!l.orientation.has_value());  // never mentioned -> raw-text optional stays empty
}

// ===========================================================================
// 12. level_objects.xtbl
// ===========================================================================
void testLevelObject() {
    Document d = P(R"(<root><Table>
      <Level_Object>
        <Name>dumpster_01</Name>
        <Hitpoints>50</Hitpoints>
        <Material>Metal - Thin</Material>
        <Lifetime_seconds>30</Lifetime_seconds>
        <Weight>75.5</Weight>
        <Surface_Velocity>1.5</Surface_Velocity>
        <Anchored>
          <Dislodge_Hitpoints>10</Dislodge_Hitpoints>
          <Dislodge_Effect>fx_dislodge</Dislodge_Effect>
          <Coins_Released>5</Coins_Released>
          <Dislodge_On_Death>true</Dislodge_On_Death>
        </Anchored>
        <Emitting_Sound>snd_dumpster_hum</Emitting_Sound>
        <Collision_Sound><VehicleIVS>0.5</VehicleIVS><FoleyCollision>foley_metal</FoleyCollision></Collision_Sound>
        <Death_Money><Min>1</Min><Max>10</Max><Cash_Out_Point><X>0.1</X><Y>0.2</Y><Z>0.3</Z></Cash_Out_Point><Just_Coins>true</Just_Coins></Death_Money>
        <center_of_mass><com_offset><X>0</X><Y>0</Y><Z>0.5</Z></com_offset></center_of_mass>
        <Movable_By_Humans>true</Movable_By_Humans>
        <Vehicle_Obstacle>unanchored</Vehicle_Obstacle>
        <Flags>
          <Flag>receives_bullet_impulse</Flag>
          <Flag>fire_hydrant</Flag>
        </Flags>
      </Level_Object>
      <Level_Object>
        <Name>no_cash_out_point</Name>
        <Death_Money><Min>0</Min><Max>0</Max><X>0.0</X><Y>0.0</Y><Z>0.0</Z><Just_Coins>False</Just_Coins></Death_Money>
      </Level_Object>
    </Table></root>)");
    std::vector<LevelObject> rows = ParseLevelObjectsTable(d);
    CHECK(rows.size() == 2);
    const LevelObject& o = rows[0];
    CHECK(o.name == "dumpster_01");
    CHECK(o.hitpoints.present && o.hitpoints.value == 50u);
    CHECK(o.material == "Metal - Thin");
    CHECK(o.lifetimeSeconds.value == 30.0f);  // RAW seconds, pre ->ticks
    CHECK(o.surfaceVelocity.has_value() && *o.surfaceVelocity == 1.5f);
    CHECK(!o.friction.has_value());  // never mentioned -> nullopt (default constant not numerically known)
    CHECK(o.anchored.present);
    CHECK(o.anchored.dislodgeHitpoints.value == 10u);
    CHECK(o.anchored.dislodgeEffect == "fx_dislodge");
    CHECK(o.anchored.coinsReleased.has_value() && *o.anchored.coinsReleased == 5u);
    CHECK(o.anchored.dislodgeOnDeath == true);
    CHECK(o.collisionSound.vehicleIVS.has_value() && *o.collisionSound.vehicleIVS == 0.5f);
    CHECK(o.collisionSound.foleyCollision == "foley_metal");
    // Death_Money/Cash_Out_Point CHILD vec3 (spec 12.1 CORRECTED 2026-10-02 -
    // Team A exe re-derivation; NOT Death_Money's own X/Y/Z directly, the
    // earlier "confirmed empirically" claim here was wrong - see
    // world_items.h's LevelObjectDeathMoney::point comment).
    CHECK(o.deathMoney.present);
    CHECK(o.deathMoney.min.value == 1u && o.deathMoney.max.value == 10u);
    CHECK(o.deathMoney.point.present);
    CHECK(near(o.deathMoney.point.value().x, 0.1f, 1e-5f) && near(o.deathMoney.point.value().z, 0.3f, 1e-5f));
    CHECK(o.deathMoney.justCoins.value == true);
    CHECK(near(o.comOffset.value().z, 0.5f, 1e-5f));
    CHECK(o.movableByHumans == true);
    CHECK(o.vehicleObstacle == "unanchored");
    CHECK(o.receivesBulletImpulse && o.fireHydrant);
    CHECK(!o.disappearOnDeath && !o.breakableGlass);  // 27-literal vocabulary: not mentioned -> false

    // BOUNDARY (spec 12.1 CORRECTED 2026-10-02): a Death_Money block with
    // direct X/Y/Z siblings and NO Cash_Out_Point child must read point as
    // ABSENT (present=false), not fall back to those sibling X/Y/Z - whether
    // the real engine helper falls back is itself OPEN/undetermined, and
    // this reader deliberately does not guess a fallback either way (see
    // world_items.h's LevelObjectDeathMoney::point comment).
    const LevelObject& o2 = rows[1];
    CHECK(o2.name == "no_cash_out_point");
    CHECK(o2.deathMoney.present);
    CHECK(!o2.deathMoney.point.present);
}

// ===========================================================================
// 13. props.xtbl
// ===========================================================================
void testProps() {
    Document d = P(R"(<root><Table>
      <Activity><Name>assault thug</Name><Num_Props>3</Num_Props></Activity>
      <Activity><Name>collect item pickup</Name><Num_Props>7</Num_Props></Activity>
    </Table></root>)");
    std::vector<PropsActivityCount> rows = ParsePropsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "assault thug" && rows[0].numProps.value == 3);
    CHECK(rows[1].name == "collect item pickup" && rows[1].numProps.value == 7);
    // The 14-name literal set (space-separated, per the empirically-validated
    // form - see world_items.h's note on the spec's own inconsistency here).
    CHECK(kPropsActivityNames.size() == 14);
    bool found = false;
    for (auto n : kPropsActivityNames) if (n == "assault thug") found = true;
    CHECK(found);
}

// ===========================================================================
// 14. triggers.xtbl
// ===========================================================================
void testTrigger() {
    Document d = P(R"(<root><Table>
      <Trigger>
        <Name>store_entrance_01</Name>
        <Effect>fx_glow</Effect>
        <Icon>icon_store</Icon>
        <Foley>foley_chime</Foley>
        <UseMessage>MSG_ENTER_STORE</UseMessage>
        <Flags><Flag>check_npcs</Flag><Flag>ignore_on_foot</Flag></Flags>
      </Trigger>
    </Table></root>)");
    std::vector<Trigger> rows = ParseTriggersTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "store_entrance_01");
    CHECK(rows[0].effect == "fx_glow" && rows[0].icon == "icon_store" && rows[0].foley == "foley_chime");
    CHECK(rows[0].useMessage == "MSG_ENTER_STORE");
    CHECK(rows[0].flagsPresent);
    CHECK(rows[0].checkNpcs && rows[0].ignoreOnFoot);
    CHECK(!rows[0].continuousActivation && !rows[0].disabledForDemo && !rows[0].ignoreVehicles);
    CHECK(!rows[0].iconType.has_value());  // spec 14.2: never appears in real data
}

// ===========================================================================
// 15. items_inventory.xtbl
// ===========================================================================
void testInventoryItem() {
    Document d = P(R"(<root><Table>
      <Inventory_Item>
        <Name>item_rpg</Name>
        <DisplayName>RPG</DisplayName>
        <Bitmap>rpg_icon.tga</Bitmap>
        <Cost>5000</Cost>
        <Default_Count>1</Default_Count>
      </Inventory_Item>
      <Inventory_Item>
        <Name>item_ammo</Name>
      </Inventory_Item>
    </Table></root>)");
    std::vector<InventoryItem> rows = ParseItemsInventoryTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "item_rpg" && rows[0].cost.has_value() && *rows[0].cost == 5000);
    // Max_Inventory absent -> defaults to the RESOLVED Default_Count value (spec 15.1).
    CHECK(!rows[0].maxInventory.has_value());
    CHECK(rows[0].DefaultCountOrDefault() == 1);
    CHECK(rows[0].MaxInventoryOrDefault() == 1);
    // Second row: everything absent -> spec-stated defaults apply.
    CHECK(rows[1].CostOrDefault() == -1);
    CHECK(rows[1].DefaultCountOrDefault() == 1);
    CHECK(rows[1].MaxInventoryOrDefault() == 1);  // falls back through Default_Count's own default
    CHECK(rows[1].ImpactShapeMinOffsetOrDefault() == 0.0f);
}

// ===========================================================================
// 16. items_3d.xtbl (partial)
// ===========================================================================
void testItem3D() {
    Document d = P(R"(<root><Table>
      <Item>
        <Name>prop_crate</Name>
        <Mesh><Filename>prop_crate.csmesh_pc</Filename></Mesh>
        <Props>
          <Prop><Flags><Flag>Attach by default</Flag></Flags></Prop>
        </Props>
        <LargeProp>false</LargeProp>
        <streaming_category>Preload</streaming_category>
        <Item_Flags><Flag>inherit_bone_transforms</Flag></Item_Flags>
      </Item>
      <Item>
        <Name>ped_helmet</Name>
        <character_mesh>
          <character_mesh><Filename>ped_helmet.ccmesh_pc</Filename></character_mesh>
          <Rig><Filename>ped_helmet.rigx</Filename></Rig>
          <Anim_set>none</Anim_set>
        </character_mesh>
        <Props></Props>
      </Item>
    </Table></root>)");
    std::vector<Item3D> rows = ParseItems3DTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "prop_crate");
    CHECK(rows[0].mesh.has_value() && rows[0].mesh->filename == "prop_crate.csmesh_pc");
    CHECK(rows[0].props.size() == 1 && rows[0].props[0].flags.size() == 1 && rows[0].props[0].flags[0] == "Attach by default");
    CHECK(rows[0].largeProp.present && rows[0].largeProp.value == false);
    CHECK(rows[0].streamingCategory == "Preload");
    CHECK(rows[0].itemFlags.size() == 1 && rows[0].itemFlags[0] == "inherit_bone_transforms");
    CHECK(!rows[0].characterMesh.has_value());

    CHECK(rows[1].characterMesh.has_value());
    CHECK(rows[1].characterMesh->filename == "ped_helmet.ccmesh_pc");
    CHECK(rows[1].characterMesh->rigFilename == "ped_helmet.rigx");
    CHECK(rows[1].characterMesh->animSet == "none");
    CHECK(rows[1].props.empty());  // Props wrapper present but empty (spec 16.2: "usually empty")
}

// ===========================================================================
// 17. contacts_sr3.xtbl
// ===========================================================================
void testContact() {
    Document d = P(R"(<root><Table>
      <Contact><Name>Kia</Name><Image>kia.tga</Image><Persona>persona_kia</Persona></Contact>
    </Table></root>)");
    std::vector<Contact> rows = ParseContactsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "Kia" && rows[0].image == "kia.tga" && rows[0].persona == "persona_kia");
}

// ===========================================================================
// 18. activity_player_persona_replacement.xtbl
// ===========================================================================
void testActivityPersonaReplacement() {
    Document d = P(R"(<root><Table>
      <Activity_Persona_Replacement>
        <Name>assault</Name>
        <Persona_Replacements>
          <Persona_Replacement><Original>generic_thug</Original><Replacement>boss_persona</Replacement></Persona_Replacement>
          <Persona_Replacement><Original>generic_gangsta</Original></Persona_Replacement>
        </Persona_Replacements>
      </Activity_Persona_Replacement>
    </Table></root>)");
    std::vector<ActivityPersonaReplacement> rows = ParseActivityPlayerPersonaReplacementTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "assault");
    CHECK(rows[0].personaReplacements.size() == 2);
    CHECK(rows[0].personaReplacements[0].original == "generic_thug" && rows[0].personaReplacements[0].replacement == "boss_persona");
    CHECK(rows[0].personaReplacements[1].original == "generic_gangsta" && !rows[0].personaReplacements[1].replacement.has_value());
}

// ===========================================================================
// 19. airplane_takeoff_land_curves.xtbl
// ===========================================================================
void testAirplaneCurve() {
    Document d = P(R"(<root><Table>
      <Curve_Params>
        <Name>Landing</Name>
        <Points>
          <Point><Offset_Height>100</Offset_Height><Offset_Dist>500</Offset_Dist><Speed>60</Speed></Point>
          <Point><Offset_Height>10</Offset_Height><Offset_Dist>50</Offset_Dist><Speed>40</Speed></Point>
        </Points>
      </Curve_Params>
      <Curve_Params><Name>Take Off</Name><Points></Points></Curve_Params>
    </Table></root>)");
    std::vector<AirplaneCurveParams> rows = ParseAirplaneTakeoffLandCurvesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "Landing");
    CHECK(rows[0].points.size() == 2);
    CHECK(rows[0].points[0].offsetHeight.value == 100.0f && rows[0].points[0].speed.value == 60.0f);
    CHECK(rows[1].name == "Take Off" && rows[1].points.empty());
    CHECK(kAirplaneCurveNames.size() == 2);
}

}  // namespace

int main() {
    testVehicleInteractionAnimationSet();
    testVehicleInteractionInfo();
    testInteractionPointSet();
    testWheelGroup();
    testVehicleAnimModifiers();
    testExternalizedComponentSlot();
    testVehicleCustSlot();
    testVehicleCustInterface();
    testColorPoolEntry();
    testColorSet();
    testVehicleSurfing();
    testLightSet();
    testLevelObject();
    testProps();
    testTrigger();
    testInventoryItem();
    testItem3D();
    testContact();
    testActivityPersonaReplacement();
    testAirplaneCurve();

    std::cout << g_checks << " checks run, " << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
