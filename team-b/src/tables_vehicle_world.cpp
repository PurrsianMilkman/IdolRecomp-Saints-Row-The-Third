// Parse functions for sr3tables_vehicle_world (see include/sr3tables_vehicle_world/
// tables.h, lightset.h, world_items.h for the struct definitions and
// per-field spec citations). Built ONLY on sr3xtbl's accessors
// (include/sr3xtbl/xtbl.h); nothing here touches the game executable,
// disassembly or decompiled code. Source: spec-tables-vehicle-world.md, and
// ONLY that document (no other spec file was consulted - see tables.h's
// banner).

#include "sr3tables_vehicle_world/tables.h"

#include <string>

namespace sr3tables_vehicle_world {

namespace {

using sr3xtbl::Always;
using sr3xtbl::Node;

std::optional<std::string> getText(const Node* node, std::string_view name = {}) {
    const std::string* t = sr3xtbl::ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

// The row/document's outermost scope for a "row container" lookup: this
// project's overwhelming convention is <root><Table>...</Table></root>
// (sr3xtbl::Document::table()), but a few sections of this spec (6, 7, 10)
// never explicitly say "Table" in their element-tree prose the way every
// other fully-confirmed section does. Falling back to root() when there is
// no <Table> child reproduces Document::table()'s own documented exception
// ("a few settings files have no Table at all") without having to guess
// which of this group's files that applies to - if <Table> exists, it is
// used (matching the overwhelming majority case); only when it is genuinely
// absent does the search fall back to the document root.
const Node* rootScope(const sr3xtbl::Document& doc) { return doc.table() ? doc.table() : doc.root(); }

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string_view> splitWs(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isWs(s[i])) ++i;
        const size_t start = i;
        while (i < s.size() && !isWs(s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

}  // namespace

// ===========================================================================
// 2. vi_enter.xtbl / vi_exit.xtbl / vi_ride.xtbl
// ===========================================================================
namespace {
VehicleInteractionAnimationElement parseAnimationElement(const Node* el) {
    VehicleInteractionAnimationElement e;
    // Parameter1, Parameter2, ... read sequentially until one is absent
    // (spec 2.1). No fixed cap is applied here (see tables.h's comment: this
    // library has no fixed-size destination for the spec's documented
    // unbounded-write hazard to overrun).
    for (int i = 1;; ++i) {
        const std::string* t = sr3xtbl::ChildText(el, "Parameter" + std::to_string(i));
        if (!t) break;
        e.parameters.push_back(*t);
    }
    e.animation = getText(el, "Animation");
    const Node* camTests = sr3xtbl::FindChild(el, "Animated_Camera_Tests");
    for (const Node* cp : sr3xtbl::Children(camTests, "Camera_Pos")) e.cameraPos.push_back(sr3xtbl::ReadVec3(cp).value());
    return e;
}
}  // namespace

VehicleInteractionAnimationSet ParseVehicleInteractionAnimationSet(const Node* row) {
    VehicleInteractionAnimationSet s;
    s.name = getText(row, "Name");
    const Node* grid = sr3xtbl::FindChild(row, "Animation_Grid");
    for (const Node* el : sr3xtbl::Children(grid, "Element")) s.elements.push_back(parseAnimationElement(el));
    return s;
}

std::vector<VehicleInteractionAnimationSet> ParseVehicleInteractionAnimationSetTable(const sr3xtbl::Document& doc) {
    std::vector<VehicleInteractionAnimationSet> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Vehicle_Interaction_Animation_Set"))
        out.push_back(ParseVehicleInteractionAnimationSet(row));
    return out;
}

// ===========================================================================
// 3. vehicle_interaction_info.xtbl
// ===========================================================================
namespace {
VehicleInteractionSeatInfo parseSeatInfo(const Node* el) {
    VehicleInteractionSeatInfo s;
    s.seatText = getText(el, "Seat");  // naming correction: <Seat>, never <Name> - see tables.h banner
    s.interactionPointSet = getText(el, "Interaction_Point_Set");
    s.capsuleShape = getText(el, "Capsule_Shape");

    const Node* flags = sr3xtbl::FindChild(el, "Flags");
    s.mirrorInteractionPoints = sr3xtbl::HasFlag(flags, "Mirror Interaction Points");
    s.meleeBruteSeat = sr3xtbl::HasFlag(flags, "Melee Brute Seat");
    s.weaponsBruteSeat = sr3xtbl::HasFlag(flags, "Weapons Brute Seat");
    s.rollerbladerSeat = sr3xtbl::HasFlag(flags, "Rollerblader Seat");
    s.riotShieldSeat = sr3xtbl::HasFlag(flags, "Riot Shield Seat");
    s.entryOnly = sr3xtbl::HasFlag(flags, "Entry Only");
    s.checkPointProjectionForUsability = sr3xtbl::HasFlag(flags, "Check Point Projection for Usability");
    s.noDoorRequired = sr3xtbl::HasFlag(flags, "No Door Required");
    s.ragdollOnDeath = sr3xtbl::HasFlag(flags, "Ragdoll On Death");
    s.quickDespawnOnDeath = sr3xtbl::HasFlag(flags, "Quick Despawn On Death");
    s.ignoreDistanceChecks = sr3xtbl::HasFlag(flags, "Ignore Distance Checks");
    s.ignoreTeamDisposition = sr3xtbl::HasFlag(flags, "Ignore Team Disposition");
    s.snapEntryAnimation = sr3xtbl::HasFlag(flags, "Snap Entry Animation");
    s.hideOccupant = sr3xtbl::HasFlag(flags, "Hide Occupant");
    s.ignoreHumanTypeCheck = sr3xtbl::HasFlag(flags, "Ignore Human Type Check");
    s.noAiExit = sr3xtbl::HasFlag(flags, "No AI Exit");
    s.specialEntryOnly = sr3xtbl::HasFlag(flags, "Special Entry Only");
    s.noFreefallAtAltitude = sr3xtbl::HasFlag(flags, "No Freefall at Altitude");
    s.holdWeaponInLeftHand = sr3xtbl::HasFlag(flags, "Hold Weapon in Left Hand");
    s.noDoorCloseAnim = sr3xtbl::HasFlag(flags, "No Door Close Anim");
    s.shouldHideBackpack = sr3xtbl::HasFlag(flags, "Should Hide Backpack");
    s.shouldScrunchHair = sr3xtbl::HasFlag(flags, "Should Scrunch Hair");
    s.shouldHideBigHats = sr3xtbl::HasFlag(flags, "Should Hide Big Hats");
    s.equipRifleOnExit = sr3xtbl::HasFlag(flags, "Equip Rifle on Exit");

    s.queue = getText(el, "Queue");
    s.primaryAccessSeat = getText(el, "Primary_Access_Seat");
    s.secondaryAccessSeat = getText(el, "Secondary_Access_Seat");
    s.enterAnimations = getText(el, "Enter_Animations");
    s.exitAnimations = getText(el, "Exit_Animations");
    s.rideAnimations = getText(el, "Ride_Animations");
    return s;
}
}  // namespace

VehicleInteractionInfo ParseVehicleInteractionInfo(const Node* row) {
    VehicleInteractionInfo info;
    info.name = sr3xtbl::CopyText(row, "Name", 0x40);
    const Node* seatInfo = sr3xtbl::FindChild(row, "Seat_Info");
    for (const Node* el : sr3xtbl::Children(seatInfo, "Element")) info.seats.push_back(parseSeatInfo(el));
    return info;
}

std::vector<VehicleInteractionInfo> ParseVehicleInteractionInfoTable(const sr3xtbl::Document& doc) {
    std::vector<VehicleInteractionInfo> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Vehicle_Interaction_Info"))
        out.push_back(ParseVehicleInteractionInfo(row));
    return out;
}

// ===========================================================================
// 4. vehicle_interaction_point_sets.xtbl
// ===========================================================================
namespace {
InteractionPointSetElement parseInteractionPointSetElement(const Node* el) {
    InteractionPointSetElement e;
    e.interactionPointType = getText(el, "Interaction_Point_Type");
    e.seatOffset = sr3xtbl::ReadVec3Child(el, "Seat_Offset");
    e.heading = sr3xtbl::ReadFloatAlways(el, "Heading");
    e.direction = getText(el, "Direction");
    e.orientToSeat = sr3xtbl::ReadBoolAlways(el, "Orient_To_Seat");
    e.projectOntoWorld = sr3xtbl::ReadBoolAlways(el, "Project_Onto_World");
    e.orientToWorld = sr3xtbl::ReadBoolAlways(el, "Orient_To_World");
    e.ignoreWhenInChassis = sr3xtbl::ReadBoolAlways(el, "Ignore_When_In_Chassis");
    return e;
}
}  // namespace

InteractionPointSet ParseInteractionPointSet(const Node* row) {
    InteractionPointSet s;
    s.name = getText(row, "Name");
    const Node* wrapper = sr3xtbl::FindChild(row, "Interaction_Point_Set_Elements");
    for (const Node* el : sr3xtbl::Children(wrapper, "Interaction_Point_Set_Element"))
        s.elements.push_back(parseInteractionPointSetElement(el));
    return s;
}

std::vector<InteractionPointSet> ParseInteractionPointSetsTable(const sr3xtbl::Document& doc) {
    std::vector<InteractionPointSet> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Interaction_Point_Set"))
        out.push_back(ParseInteractionPointSet(row));
    return out;
}

// ===========================================================================
// 5. vehicle_wheel_groups.xtbl
// ===========================================================================
WheelGroup ParseWheelGroup(const Node* row) {
    WheelGroup g;
    g.name = getText(row, "Name");
    g.displayName = getText(row, "Display_Name");
    g.price = sr3xtbl::GetInt32(row, "Price");

    const Node* rimsGrid = sr3xtbl::FindChild(row, "Rims_Grid");
    for (const Node* re : sr3xtbl::Children(rimsGrid, "Rim_Element")) {
        WheelGroupRimElement r;
        r.displayName = getText(re, "Display_Name");
        r.frontRim = getText(re, "Front_Rim");
        r.rearRim = getText(re, "Rear_Rim");
        g.rims.push_back(r);
    }

    const Node* spinnersGrid = sr3xtbl::FindChild(row, "Spinners_Grid");
    for (const Node* se : sr3xtbl::Children(spinnersGrid, "S_Element")) {
        WheelGroupSpinnerElement s;
        s.displayName = getText(se, "Display_Name");
        s.price = sr3xtbl::GetInt32(se, "Price");
        s.frontSpinner = getText(se, "Front_Spinner");
        if (s.frontSpinner) s.rearSpinner = getText(se, "Rear_Spinner");  // "read only if Front_Spinner was present"
        g.spinners.push_back(s);
    }
    return g;
}

std::vector<WheelGroup> ParseWheelGroupsTable(const sr3xtbl::Document& doc) {
    std::vector<WheelGroup> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Wheel_Group")) out.push_back(ParseWheelGroup(row));
    return out;
}

// ===========================================================================
// 6. vehicle_animation_modifiers.xtbl
// ===========================================================================
VehicleAnimModifiers ParseVehicleAnimModifiers(const sr3xtbl::Document& doc) {
    VehicleAnimModifiers m;
    const Node* modifiers = sr3xtbl::FindChild(rootScope(doc), "Vehicle_Anim_Modifiers");
    m.humanSeatOffsetDefault = sr3xtbl::ReadVec3Child(modifiers, "Human_Seat_Offset");

    const Node* vehicles = sr3xtbl::FindChild(modifiers, "Vehicles");
    for (const Node* veh : sr3xtbl::Children(vehicles, "Vehicle")) {
        VehicleAnimModifierVehicle v;
        v.name = getText(veh, "Name");
        v.humanScale = sr3xtbl::GetFloat(veh, "Human_Scale");
        v.humanSeatOffset = sr3xtbl::ReadVec3Child(veh, "Human_Seat_Offset");

        for (const Node* seatEl : sr3xtbl::Children(veh, "Seat")) {
            VehicleAnimModifierSeat seat;
            seat.seatText = seatEl->text() ? std::make_optional(*seatEl->text()) : std::nullopt;
            seat.humanSeatOffset = sr3xtbl::ReadVec3Child(seatEl, "Human_Seat_Offset");

            const Node* groups = sr3xtbl::FindChild(seatEl, "Weapon_Animation_Groups");
            for (const Node* group : sr3xtbl::Children(groups, "Weapon_Animation_Group")) {
                WeaponAnimationGroupEntry base;
                base.weaponCategory = getText(group, "Name");
                base.humanSeatOffset = sr3xtbl::ReadVec3Child(group, "Human_Seat_Offset");
                seat.weaponAnimationGroups.push_back(base);  // the group-level sub-entry (animationState == nullopt)

                const Node* states = sr3xtbl::FindChild(group, "Animation_States");
                for (const Node* st : sr3xtbl::Children(states, "Animation_State")) {
                    WeaponAnimationGroupEntry e2;
                    e2.weaponCategory = base.weaponCategory;
                    e2.animationState = getText(st, "Name");
                    e2.humanSeatOffset = sr3xtbl::ReadVec3Child(st, "Human_Seat_Offset");
                    seat.weaponAnimationGroups.push_back(e2);
                }
            }
            v.seats.push_back(seat);
        }
        m.vehicles.push_back(v);
    }
    return m;
}

// ===========================================================================
// 7. externalized_vehicle_components.xtbl
// ===========================================================================
ExternalizedComponentSlot ParseExternalizedComponentSlot(const Node* row) {
    ExternalizedComponentSlot s;
    s.name = getText(row, "Name");
    s.cameraInfo = getText(row, "Camera_Info");
    const Node* components = sr3xtbl::FindChild(row, "Components");
    s.componentCount = sr3xtbl::Children(components, "Component").size();
    return s;
}

std::vector<ExternalizedComponentSlot> ParseExternalizedVehicleComponentsTable(const sr3xtbl::Document& doc) {
    std::vector<ExternalizedComponentSlot> out;
    const Node* customProps = sr3xtbl::FindChild(rootScope(doc), "Custom_vehicle_properties");
    const Node* slots = sr3xtbl::FindChild(customProps, "Component_Slots");
    for (const Node* row : sr3xtbl::Children(slots, "Slot")) out.push_back(ParseExternalizedComponentSlot(row));
    return out;
}

// ===========================================================================
// 8.1 vehicle_cust_slots.xtbl
// ===========================================================================
VehicleCustSlot ParseVehicleCustSlot(const Node* row) {
    VehicleCustSlot s;
    s.displayName = getText(row, "DisplayName");
    s.name = getText(row, "Name");
    s.slotType = getText(row, "SlotType");
    s.vehicleComponentType = getText(row, "Vehicle_Component_Type");
    return s;
}

std::vector<VehicleCustSlot> ParseVehicleCustSlotsTable(const sr3xtbl::Document& doc) {
    std::vector<VehicleCustSlot> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Vehicle_slot")) out.push_back(ParseVehicleCustSlot(row));
    return out;
}

// ===========================================================================
// 8.2 vehicle_cust_interface.xtbl
// ===========================================================================
VehicleCustInterfaceEntry ParseVehicleCustInterfaceEntry(const Node* row) {
    VehicleCustInterfaceEntry e;
    e.displayName = getText(row, "Display_Name");  // raw, unbounded copy per spec - not CopyText-truncated
    e.name = getText(row, "Name");
    e.parentCategory = getText(row, "parent_category");

    // naming correction (spec 8.2/1.3): the lowercase `name` CHILD of each
    // inner `slots` wrapper item, never the wrapper's own text.
    const Node* outer = sr3xtbl::FindChild(row, "slots");
    for (const Node* inner : sr3xtbl::Children(outer, "slots")) {
        if (const std::string* n = sr3xtbl::ChildText(inner, "name")) e.slotNames.push_back(*n);
    }

    e.isColorMenu = sr3xtbl::ReadBoolAlways(row, "is_color_menu").value;
    e.isWheelMenu = sr3xtbl::ReadBoolAlways(row, "is_wheel_menu").value;
    e.isPerfMenu = sr3xtbl::ReadBoolAlways(row, "is_perf_menu").value;
    return e;
}

std::vector<VehicleCustInterfaceEntry> ParseVehicleCustInterfaceTable(const sr3xtbl::Document& doc) {
    std::vector<VehicleCustInterfaceEntry> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "vehicle_cust_interface"))
        out.push_back(ParseVehicleCustInterfaceEntry(row));
    return out;
}

// ===========================================================================
// 9.1 vehicle_cust_color_pool.xtbl
// ===========================================================================
ColorPoolEntry ParseColorPoolEntry(const Node* row) {
    ColorPoolEntry c;
    c.name = getText(row, "Name");
    const Node* shaderValues = sr3xtbl::FindChild(row, "Shader_Values");
    c.shaderValuesPresent = shaderValues != nullptr;
    c.vectorElementCount = sr3xtbl::Children(shaderValues, "Vector_Element").size();
    c.floatElementCount = sr3xtbl::Children(shaderValues, "Float_Element").size();
    return c;
}

std::vector<ColorPoolEntry> ParseVehicleCustColorPoolTable(const sr3xtbl::Document& doc) {
    std::vector<ColorPoolEntry> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Color")) out.push_back(ParseColorPoolEntry(row));
    return out;
}

// ===========================================================================
// 9.2 vehicle_cust_color_sets.xtbl
// ===========================================================================
ColorSet ParseColorSet(const Node* row) {
    ColorSet c;
    c.displayName = getText(row, "Display_Name");
    c.name = getText(row, "Name");
    c.price = sr3xtbl::ReadInt32Always(row, "Price");
    const Node* grid = sr3xtbl::FindChild(row, "Color_Grid");
    for (const Node* el : sr3xtbl::Children(grid, "Color_Element")) {
        ColorSetElement ce;
        ce.color = getText(el, "Color");
        c.colors.push_back(ce);
    }
    return c;
}

std::vector<ColorSet> ParseVehicleCustColorSetsTable(const sr3xtbl::Document& doc) {
    std::vector<ColorSet> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Color_Set")) out.push_back(ParseColorSet(row));
    return out;
}

// ===========================================================================
// 10. vehicle_surfing_style_two.xtbl
// ===========================================================================
std::optional<VehicleSurfing> ParseVehicleSurfing(const sr3xtbl::Document& doc) {
    const Node* vs = sr3xtbl::FindChild(rootScope(doc), "Vehicle_Surfing");
    if (!vs) return std::nullopt;

    VehicleSurfing s;
    s.maxSurfingTime = sr3xtbl::ReadFloatAlways(vs, "Max_Surfing_Time");
    s.minSurfingTime = sr3xtbl::ReadFloatAlways(vs, "Min_Surfing_Time");
    s.maxHandstandTime = sr3xtbl::ReadFloatAlways(vs, "Max_Handstand_Time");
    s.minHandstandTime = sr3xtbl::ReadFloatAlways(vs, "Min_Handstand_Time");
    s.maxHandstandPercentReward = sr3xtbl::ReadFloatAlways(vs, "Max_Handstand_Percent_Reward");
    s.surfingMinSpeed = sr3xtbl::ReadFloatAlways(vs, "Surfing_Min_Speed");
    s.surfingTimeout = sr3xtbl::ReadFloatAlways(vs, "Surfing_Timeout");
    s.startTime = sr3xtbl::ReadFloatAlways(vs, "Start_Time");
    s.maxSpeedDelay = sr3xtbl::ReadFloatAlways(vs, "Max_Speed_Delay");
    s.recordDisplayTime = sr3xtbl::GetFloat(vs, "Record_Display_Time");
    s.recordQueueTime = sr3xtbl::GetFloat(vs, "Record_Queue_Time");
    s.recordThreshold = sr3xtbl::ReadFloatAlways(vs, "Record_Threshold");
    s.handstandingSensitivityMultiplier = sr3xtbl::ReadFloatAlways(vs, "Handstanding_Sensitivity_Multiplier");
    s.maxRespect = sr3xtbl::ReadInt32Always(vs, "Max_Respect");
    s.maxLifetimeRespect = sr3xtbl::ReadInt32Always(vs, "Max_Lifetime_Respect");
    s.maxCash = sr3xtbl::ReadFloatAlways(vs, "Max_Cash");

    const Node* bb = sr3xtbl::FindChild(vs, "Balance_Bar_Params");
    s.balanceBarParams.balancedRegionSize = sr3xtbl::ReadFloatAlways(bb, "Balanced_Region_Size");
    s.balanceBarParams.balancedRegionSizeChange = sr3xtbl::ReadFloatAlways(bb, "Balanced_Region_Size_Change");
    s.balanceBarParams.balancedRegionMinSize = sr3xtbl::ReadFloatAlways(bb, "Balanced_Region_Min_Size");
    s.balanceBarParams.balancedRegionAcceleration = sr3xtbl::ReadFloatAlways(bb, "Balanced_Region_Acceleration");
    s.balanceBarParams.balancedAccelerationChange = sr3xtbl::ReadFloatAlways(bb, "Balanced_Acceleration_Change");
    s.balanceBarParams.balancedAccelerationMax = sr3xtbl::ReadFloatAlways(bb, "Balanced_Acceleration_Max");
    s.balanceBarParams.unbalancedRegionAcceleration = sr3xtbl::ReadFloatAlways(bb, "Unbalanced_Region_Acceleration");
    s.balanceBarParams.unbalancedAccelerationChange = sr3xtbl::ReadFloatAlways(bb, "Unbalanced_Acceleration_Change");
    s.balanceBarParams.unbalancedAccelerationMax = sr3xtbl::ReadFloatAlways(bb, "Unbalanced_Acceleration_Max");
    s.balanceBarParams.balancingAcceleration = sr3xtbl::ReadFloatAlways(bb, "Balancing_Acceleration");
    s.balanceBarParams.balancingAccelerationChange = sr3xtbl::ReadFloatAlways(bb, "Balancing_Acceleration_Change");
    s.balanceBarParams.balancingAccelerationMax = sr3xtbl::ReadFloatAlways(bb, "Balancing_Acceleration_Max");

    return s;
}

// ===========================================================================
// 11. The shared "LightSet" table
// ===========================================================================
namespace {
std::optional<LightSetVec3Text> parseLightSetVec3Text(const std::string* text) {
    if (!text) return std::nullopt;
    const std::vector<std::string_view> toks = splitWs(*text);
    if (toks.empty()) return std::nullopt;
    LightSetVec3Text v;
    if (toks.size() >= 1) v.x = sr3xtbl::ParseFloat(toks[0]);
    if (toks.size() >= 2) v.y = sr3xtbl::ParseFloat(toks[1]);
    if (toks.size() >= 3) v.z = sr3xtbl::ParseFloat(toks[2]);
    return v;
}

Light parseLight(const Node* el) {
    Light l;
    l.name = getText(el, "Name");
    l.type = getText(el, "Type");
    l.category0 = sr3xtbl::ReadBoolAlways(el, "Category_0");
    l.category1 = sr3xtbl::ReadBoolAlways(el, "Category_1");
    l.category2 = sr3xtbl::ReadBoolAlways(el, "Category_2");
    l.category3 = sr3xtbl::ReadBoolAlways(el, "Category_3");
    l.castShadows = sr3xtbl::ReadBoolAlways(el, "CastShadows");
    l.color = parseLightSetVec3Text(sr3xtbl::ChildText(el, "Color"));
    l.multiplier = sr3xtbl::ReadFloatAlways(el, "Multiplier");
    l.templateFlag = sr3xtbl::ReadBoolAlways(el, "Template");
    l.position = parseLightSetVec3Text(sr3xtbl::ChildText(el, "Position"));
    l.orientation = getText(el, "Orientation");
    l.indoor = getText(el, "Indoor");
    l.outdoor = getText(el, "Outdoor");
    l.hotspot = getText(el, "Hotspot");
    l.attenuation = getText(el, "Attenuation");
    l.slateName = getText(el, "Slate_Name");
    l.lightCharacter = getText(el, "LightCharacter");
    l.shadowCharacter = getText(el, "ShadowCharacter");
    l.lightLevel = getText(el, "LightLevel");
    l.shadowLevel = getText(el, "ShadowLevel");
    return l;
}
}  // namespace

LightSet ParseLightSet(const Node* row) {
    LightSet s;
    s.name = getText(row, "Name");
    s.startTime = getText(row, "StartTime");
    s.endTime = getText(row, "EndTime");
    s.exposure = sr3xtbl::ReadFloatAlways(row, "Exposure");
    s.rampExposure = sr3xtbl::ReadBoolAlways(row, "RampExposure");
    const Node* lights = sr3xtbl::FindChild(row, "Lights");
    for (const Node* l : sr3xtbl::Children(lights, "Light")) s.lights.push_back(parseLight(l));
    return s;
}

std::optional<LightSet> ParseLightSetTable(const sr3xtbl::Document& doc) {
    const Node* row = sr3xtbl::FindChild(rootScope(doc), "LightSet");
    if (!row) return std::nullopt;
    return ParseLightSet(row);
}

// ===========================================================================
// 12. level_objects.xtbl
// ===========================================================================
LevelObject ParseLevelObject(const Node* row) {
    LevelObject o;
    o.name = sr3xtbl::CopyText(row, "Name", 0x30);
    o.hitpoints = sr3xtbl::ReadUInt32Always(row, "Hitpoints");
    o.material = getText(row, "Material");
    o.lifetimeSeconds = sr3xtbl::ReadFloatAlways(row, "Lifetime_seconds");
    o.weight = sr3xtbl::ReadFloatAlways(row, "Weight");
    o.friction = sr3xtbl::GetFloat(row, "Friction");
    o.restitution = sr3xtbl::GetFloat(row, "Restitution");
    o.angularDamping = sr3xtbl::GetFloat(row, "Angular_Damping");
    o.linearDamping = sr3xtbl::GetFloat(row, "Linear_Damping");
    o.surfaceVelocity = sr3xtbl::GetFloat(row, "Surface_Velocity");
    o.buoyancyModifier = sr3xtbl::GetFloat(row, "Buoyancy_Modifier");

    const Node* anchored = sr3xtbl::FindChild(row, "Anchored");
    o.anchored.present = anchored != nullptr;
    o.anchored.dislodgeHitpoints = sr3xtbl::ReadUInt32Always(anchored, "Dislodge_Hitpoints");
    o.anchored.dislodgeEffect = getText(anchored, "Dislodge_Effect");
    o.anchored.coinsReleased = sr3xtbl::GetUInt32(anchored, "Coins_Released");
    o.anchored.dislodgeOnDeath = sr3xtbl::ReadBoolAlways(anchored, "Dislodge_On_Death").value;
    o.anchored.dislodgeNotoriety = sr3xtbl::ReadBoolAlways(anchored, "Dislodge_Notoriety").value;

    o.emittingSound = getText(row, "Emitting_Sound");

    const Node* collisionSound = sr3xtbl::FindChild(row, "Collision_Sound");
    o.collisionSound.vehicleIVS = sr3xtbl::GetFloat(collisionSound, "VehicleIVS");
    o.collisionSound.objectMVS = sr3xtbl::GetFloat(collisionSound, "ObjectMVS");
    o.collisionSound.foleyCollision = getText(collisionSound, "FoleyCollision");

    o.deathEffect = getText(row, "Death_Effect");
    o.deathExplosion = getText(row, "Death_Explosion");

    const Node* deathMoney = sr3xtbl::FindChild(row, "Death_Money");
    o.deathMoney.min = sr3xtbl::ReadUInt32Always(deathMoney, "Min");
    o.deathMoney.max = sr3xtbl::ReadUInt32Always(deathMoney, "Max");
    o.deathMoney.point = sr3xtbl::ReadVec3(deathMoney);  // Death_Money's own X/Y/Z children (spec 12.1/12.3)
    o.deathMoney.justCoins = sr3xtbl::ReadBoolAlways(deathMoney, "Just_Coins");

    o.vehicleRepulsorScale = sr3xtbl::GetFloat(row, "Vehicle_Repulsor_Scale");

    const Node* com = sr3xtbl::FindChild(row, "center_of_mass");
    o.comOffset = sr3xtbl::ReadVec3Child(com, "com_offset");
    o.comOffsetCorpse = sr3xtbl::ReadVec3Child(com, "com_offset_corpse");

    o.movableByHumans = sr3xtbl::ReadBoolAlways(row, "Movable_By_Humans").value;
    o.vehicleObstacle = getText(row, "Vehicle_Obstacle");

    const Node* flags = sr3xtbl::FindChild(row, "Flags");
    o.receivesBulletImpulse = sr3xtbl::HasFlag(flags, "receives_bullet_impulse");
    o.disappearOnDeath = sr3xtbl::HasFlag(flags, "disappear_on_death");
    o.disableLightsOnDislodge = sr3xtbl::HasFlag(flags, "disable_lights_on_dislodge");
    o.disableEffectsOnDislodge = sr3xtbl::HasFlag(flags, "disable_effects_on_dislodge");
    o.ignoreHumanCollision = sr3xtbl::HasFlag(flags, "ignore_human_collision");
    o.cameraCollide = sr3xtbl::HasFlag(flags, "camera_collide");
    o.vehicleCameraCollide = sr3xtbl::HasFlag(flags, "vehicle_camera_collide");
    o.ignoreBulletCollision = sr3xtbl::HasFlag(flags, "ignore_bullet_collision");
    o.bulletsPenetrate = sr3xtbl::HasFlag(flags, "bullets_penetrate");
    o.fireHydrant = sr3xtbl::HasFlag(flags, "fire_hydrant");
    o.doNotAimAtMe = sr3xtbl::HasFlag(flags, "do_not_aim_at_me");
    o.nonWalkable = sr3xtbl::HasFlag(flags, "non_walkable");
    o.canWalkUp = sr3xtbl::HasFlag(flags, "can_walk_up");
    o.nearbyPlayerDespawn = sr3xtbl::HasFlag(flags, "nearby_player_despawn");
    o.shattersAgainstWorld = sr3xtbl::HasFlag(flags, "shatters_against_world");
    o.blackjack = sr3xtbl::HasFlag(flags, "blackjack");
    o.poker = sr3xtbl::HasFlag(flags, "poker");
    o.zombie = sr3xtbl::HasFlag(flags, "zombie");
    o.basketball = sr3xtbl::HasFlag(flags, "basketball");
    o.tv = sr3xtbl::HasFlag(flags, "tv");
    o.noDetourUntilMoved = sr3xtbl::HasFlag(flags, "no_detour_until_moved");
    o.doesNotGenerateDetour = sr3xtbl::HasFlag(flags, "does_not_generate_detour");
    o.generateDetourEvenIfInAir = sr3xtbl::HasFlag(flags, "generate_detour_even_if_in_air");
    o.breakableGlass = sr3xtbl::HasFlag(flags, "breakable_glass");
    o.aiLosIgnore = sr3xtbl::HasFlag(flags, "ai_los_ignore");
    o.damagedByPlayersOnly = sr3xtbl::HasFlag(flags, "damaged_by_players_only");
    o.noBrutePickup = sr3xtbl::HasFlag(flags, "no_brute_pickup");

    return o;
}

std::vector<LevelObject> ParseLevelObjectsTable(const sr3xtbl::Document& doc) {
    std::vector<LevelObject> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Level_Object")) out.push_back(ParseLevelObject(row));
    return out;
}

// ===========================================================================
// 13. props.xtbl
// ===========================================================================
PropsActivityCount ParsePropsActivityCount(const Node* row) {
    PropsActivityCount p;
    p.name = getText(row, "Name");
    p.numProps = sr3xtbl::ReadInt32Always(row, "Num_Props");
    return p;
}

std::vector<PropsActivityCount> ParsePropsTable(const sr3xtbl::Document& doc) {
    std::vector<PropsActivityCount> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Activity")) out.push_back(ParsePropsActivityCount(row));
    return out;
}

// ===========================================================================
// 14. triggers.xtbl
// ===========================================================================
Trigger ParseTrigger(const Node* row) {
    Trigger t;
    t.name = getText(row, "Name");
    t.effect = getText(row, "Effect");
    t.icon = getText(row, "Icon");
    t.foley = getText(row, "Foley");
    t.iconType = getText(row, "IconType");
    t.useMessage = getText(row, "UseMessage");

    const Node* flags = sr3xtbl::FindChild(row, "Flags");
    t.flagsPresent = flags != nullptr;
    t.checkNpcs = sr3xtbl::HasFlag(flags, "check_npcs");
    t.continuousActivation = sr3xtbl::HasFlag(flags, "continuous_activation");
    t.disabledForDemo = sr3xtbl::HasFlag(flags, "disabled_for_demo");
    t.ignoreVehicles = sr3xtbl::HasFlag(flags, "ignore_vehicles");
    t.ignoreOnFoot = sr3xtbl::HasFlag(flags, "ignore_on_foot");
    return t;
}

std::vector<Trigger> ParseTriggersTable(const sr3xtbl::Document& doc) {
    std::vector<Trigger> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Trigger")) out.push_back(ParseTrigger(row));
    return out;
}

// ===========================================================================
// 15. items_inventory.xtbl
// ===========================================================================
InventoryItem ParseInventoryItem(const Node* row) {
    InventoryItem it;
    it.name = getText(row, "Name");
    it.framework = getText(row, "Framework");
    it.displayName = getText(row, "DisplayName");
    it.bitmap = getText(row, "Bitmap");
    it.impactShapeMinOffset = sr3xtbl::GetFloat(row, "Impact_shape_min_offset");
    it.cost = sr3xtbl::GetInt32(row, "Cost");
    it.defaultCount = sr3xtbl::GetInt32(row, "Default_Count");
    it.maxInventory = sr3xtbl::GetInt32(row, "Max_Inventory");
    it.description = getText(row, "Description");
    it.useScript = getText(row, "Use_Script");
    return it;
}

std::vector<InventoryItem> ParseItemsInventoryTable(const sr3xtbl::Document& doc) {
    std::vector<InventoryItem> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Inventory_Item")) out.push_back(ParseInventoryItem(row));
    return out;
}

// ===========================================================================
// 16. items_3d.xtbl (partial)
// ===========================================================================
Item3D ParseItem3D(const Node* row) {
    Item3D it;
    it.name = getText(row, "Name");

    if (const Node* meshNode = sr3xtbl::FindChild(row, "Mesh")) {
        ItemMesh m;
        m.filename = getText(meshNode, "Filename");
        it.mesh = m;
    }
    if (const Node* cm = sr3xtbl::FindChild(row, "character_mesh")) {
        ItemCharacterMesh c;
        if (const Node* inner = sr3xtbl::FindChild(cm, "character_mesh")) c.filename = getText(inner, "Filename");
        // FindChild is case-insensitive (sr3xtbl::NameEquals), so a single lookup covers both "Rig" and "rig".
        if (const Node* rig = sr3xtbl::FindChild(cm, "Rig")) c.rigFilename = getText(rig, "Filename");
        c.animSet = getText(cm, "Anim_set");
        it.characterMesh = c;
    }
    const Node* propsWrapper = sr3xtbl::FindChild(row, "Props");
    for (const Node* prop : sr3xtbl::Children(propsWrapper, "Prop")) {
        ItemProp p;
        const Node* flagsNode = sr3xtbl::FindChild(prop, "Flags");
        for (const Node* f : sr3xtbl::Children(flagsNode, "Flag"))
            if (f->text()) p.flags.push_back(*f->text());
        it.props.push_back(p);
    }
    it.largeProp = sr3xtbl::ReadBoolAlways(row, "LargeProp");
    it.streamingCategory = getText(row, "streaming_category");

    it.glowType = getText(row, "Glow_Type");
    it.scaleAmbient = getText(row, "Scale_Ambient");
    it.colorVariantsPresent = sr3xtbl::FindChild(row, "Color_Variants") != nullptr;
    const Node* itemFlags = sr3xtbl::FindChild(row, "Item_Flags");
    for (const Node* f : sr3xtbl::Children(itemFlags, "Flag"))
        if (f->text()) it.itemFlags.push_back(*f->text());

    return it;
}

std::vector<Item3D> ParseItems3DTable(const sr3xtbl::Document& doc) {
    std::vector<Item3D> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Item")) out.push_back(ParseItem3D(row));
    return out;
}

// ===========================================================================
// 17. contacts_sr3.xtbl
// ===========================================================================
Contact ParseContact(const Node* row) {
    Contact c;
    c.name = getText(row, "Name");
    c.image = getText(row, "Image");
    c.persona = getText(row, "Persona");
    return c;
}

std::vector<Contact> ParseContactsTable(const sr3xtbl::Document& doc) {
    std::vector<Contact> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Contact")) out.push_back(ParseContact(row));
    return out;
}

// ===========================================================================
// 18. activity_player_persona_replacement.xtbl
// ===========================================================================
ActivityPersonaReplacement ParseActivityPersonaReplacement(const Node* row) {
    ActivityPersonaReplacement a;
    a.name = getText(row, "Name");
    const Node* wrapper = sr3xtbl::FindChild(row, "Persona_Replacements");
    for (const Node* pr : sr3xtbl::Children(wrapper, "Persona_Replacement")) {
        PersonaReplacement p;
        p.original = getText(pr, "Original");
        p.replacement = getText(pr, "Replacement");
        a.personaReplacements.push_back(p);
    }
    return a;
}

std::vector<ActivityPersonaReplacement> ParseActivityPlayerPersonaReplacementTable(const sr3xtbl::Document& doc) {
    std::vector<ActivityPersonaReplacement> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Activity_Persona_Replacement"))
        out.push_back(ParseActivityPersonaReplacement(row));
    return out;
}

// ===========================================================================
// 19. airplane_takeoff_land_curves.xtbl
// ===========================================================================
AirplaneCurveParams ParseAirplaneCurveParams(const Node* row) {
    AirplaneCurveParams c;
    c.name = getText(row, "Name");
    const Node* points = sr3xtbl::FindChild(row, "Points");
    for (const Node* p : sr3xtbl::Children(points, "Point")) {
        AirplaneCurvePoint pt;
        pt.offsetHeight = sr3xtbl::ReadFloatAlways(p, "Offset_Height");
        pt.offsetDist = sr3xtbl::ReadFloatAlways(p, "Offset_Dist");
        pt.speed = sr3xtbl::ReadFloatAlways(p, "Speed");
        c.points.push_back(pt);
    }
    return c;
}

std::vector<AirplaneCurveParams> ParseAirplaneTakeoffLandCurvesTable(const sr3xtbl::Document& doc) {
    std::vector<AirplaneCurveParams> out;
    for (const Node* row : sr3xtbl::Children(rootScope(doc), "Curve_Params")) out.push_back(ParseAirplaneCurveParams(row));
    return out;
}

}  // namespace sr3tables_vehicle_world
