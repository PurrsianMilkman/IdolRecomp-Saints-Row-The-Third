// Parse function for sr3vehicleinfo (spec-vehicle-data.md section 7). Built
// entirely on sr3xtbl's accessors (include/sr3xtbl/xtbl.h). See
// include/sr3vehicleinfo/vehicle_entry.h and vehicle_type.h for the shared
// conventions this file follows (Always<T> vs optional<T>, unit conversions,
// what is deliberately left OPEN/skipped and why).
//
// XML WRAPPER ELEMENTS CONFIRMED BY REAL DATA (not by spec text - the spec's
// section 7.3 offset table gives destination offsets but, for these
// specific fields, does not name an XML parent; real base-game data
// inspection - fair game per this project's rule 2 - shows they DO sit
// under a named wrapper, one level deeper than this reader's first
// implementation assumed). Verified consistently across multiple real
// `*_veh.xtbl` files (tools/validation/validate_vehicleinfo_population.cpp):
//   <Engine>       Torque, Min/Opt/Max_RPM, Min/Max_RPM_Torque_Factor,
//                  Min/Opt/Max_RPM_Resistance, Nitro_Capable (a flags0 bit!)
//   <Transmission> Gear_Ratios, Downshift_RPMs, Upshift_RPM, Diff_Gear_Ratio,
//                  Reverse_Gear_Ratio, Clutch_Delay, Maximum_RPM_Rate,
//                  Clutch_Slip_RPM
//   <Steering>     Max_Steering_Angle, AI_Maximum_Steering_Angle,
//                  Max_Speed_Steering, AI_Max_Speed_Steering, and the four
//                  Steering_Wheel_* fields
//   <Aerodynamics> Air_Density, Frontal_Area, Drag_Coefficient,
//                  Lift_Coefficient, Extra_Gravity
//   <Angular_Damping> Normal_Spin_Damping, Collision_Spin_Damping,
//                  Collision_Spin_Threshold (ONLY these three - the other
//                  "rotation/friction" fields, e.g. Roll_Torque_Factor,
//                  stay flat top-level children, confirmed by the same data)
//   <Effects>      Engine_Fire, Engine_Smoke, Engine_Smoke_Black, Explosion,
//                  Secondary_Explosion, Exhaust_Types, Main_Rotor_Effect,
//                  Tail_Rotor_Effect, Car_Smoke, Exhaust_All_Time (a flags0 bit!)
//   <Audio_Onload_Parameters> Medium_Onload_Threshold, High_Onload_Threshold
//                  (a SEPARATE wrapper from Audio_Hysteresis, not a child of
//                  it as this reader first assumed)
// Two structural corrections, also from real data (multiple consistent
// samples), not guesses:
//   NoCustVariantsGrid's items are <NoCustVariantElement><Variant>NAME</Variant>
//     ...</NoCustVariantElement>, not a flat <Element>NAME</Element> list.
//   Upgrades/<Category>/Value/Levels/Level_N holds the multiplier and
//     Upgrades/<Category>/Cost/Levels/Level_N holds the cost - Level_N's
//     OWN text is the number; there is no separate "Multiplier"/"Cost" leaf
//     under Level_N as this reader first guessed.
//   Watercraft's LBS_* fields (the only Watercraft fields this reader
//     implements) sit inside their own <Lean_Based_Steering> wrapper, not
//     directly under <Watercraft>.
// None of this adds a field the spec does not already document at section
// 7.3/7.4 - it only corrects which XML node this reader searches for an
// already-specified element. See the report accompanying this deliverable
// for the fields real data revealed that the spec does NOT document at all
// (e.g. an Audio_Hysteresis/Drift_Ramp_Time present in 123/123 real
// vehicles) - those are reported, not implemented, per this project's
// spec-only-implementation rule.

#include "sr3vehicleinfo/vehicle_entry.h"

#include <cmath>
#include <cstring>

namespace sr3vehicleinfo {

using namespace sr3xtbl;

namespace {

// ---------------------------------------------------------------------------
// Generic helpers (mirrors sr3tables_weapons/tables_weapons.cpp's style).
// ---------------------------------------------------------------------------
std::optional<std::string> OptText(const Node* node, std::string_view name) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

template <size_t N>
uint32_t FlagBits(const Node* flagsNode, const std::string_view (&names)[N]) {
    return FlagMask(flagsNode, names, N);
}

// deg->rad / mph->m/s (spec section 7, "Units") applied only when the
// underlying element was actually present - matching sr3tables_weapons's
// ReadDegAsRadAlways precedent.
Always<float> ReadDegAsRadAlways(const Node* node, std::string_view name) {
    Always<float> a = ReadFloatAlways(node, name);
    if (a.present) a.value *= kDegToRad;
    return a;
}
// mph->m/s, capped at 100 mph BEFORE conversion (spec section 7).
Always<float> ReadMphAsMsAlways(const Node* node, std::string_view name) {
    Always<float> a = ReadFloatAlways(node, name);
    if (a.present) {
        float mph = a.value;
        if (mph > kMaxSpeedCapMph) mph = kMaxSpeedCapMph;
        a.value = mph * kMphToMs;
    }
    return a;
}

// The flag-word reader's own bool grammar (spec section 7.6): "yes"/"true"/
// "no" are the only recognised literals; ANY other spelling (including the
// otherwise-common "false") leaves the field's default. Used for the
// "default SET, only <No> clears it" family of bits (Drifting_Possible,
// Burnout_Possible, Allow_Passenger_Threat_Extract, Racing_Can_Spawn,
// Drop_off_copilot).
bool DefaultSetUnlessNo(const Node* node, std::string_view name) {
    const std::string* t = ChildText(node, name);
    if (!t) return true;  // absent -> default set
    return !NameEquals(*t, "no");
}

// shooters_allowed (spec 7.6): three-way text match, no generic Flag list.
void ApplyShootersAllowed(const Node* row, uint32_t& f) {
    const std::string* t = ChildText(row, "shooters_allowed");
    if (!t) return;  // "Everybody" (default): both bits clear
    if (NameEquals(*t, "Front Seats")) { f |= flags0::kShootersAllowedFrontSeats; return; }
    if (NameEquals(*t, "Driver Only")) { f |= flags0::kShootersAllowedDriverOnly; return; }
    // "Everybody" or any unrecognised text: both clear (matches the default).
}

// Garage_Can_Store (spec 7.6): "No" clears both bits, "Valet-Only" leaves
// only bit 24 set, absence/anything else leaves both bits set (default).
void ApplyGarageCanStore(const Node* row, uint32_t& f) {
    const std::string* t = ChildText(row, "Garage_Can_Store");
    if (t && NameEquals(*t, "no")) return;  // both clear
    if (t && NameEquals(*t, "Valet-Only")) { f |= flags0::kGarageCanStoreB; return; }
    f |= flags0::kGarageCanStoreA | flags0::kGarageCanStoreB;  // absent or any other text: default (both set)
}

// Strips a case-insensitive trailing suffix if present (spec 7.3:
// Vehicle_Animation_Set_Name/_Rig_Name are stored WITHOUT their ".xtbl"/
// ".rigx" suffix).
std::optional<std::string> StripSuffixCI(std::optional<std::string> s, std::string_view suffix) {
    if (!s || s->size() < suffix.size()) return s;
    std::string_view tail(s->data() + s->size() - suffix.size(), suffix.size());
    if (NameEquals(tail, suffix)) s->resize(s->size() - suffix.size());
    return s;
}

// Display_Name (spec 7.3): UTF-16, 24 code units, "the reader widens ASCII"
// - a plain byte->char16_t widen, no real UTF-8 decoding, truncated to 23
// usable code units (the 24th slot is the real entry's NUL terminator).
std::u16string WidenDisplayName(const std::string* t) {
    std::u16string out;
    if (!t) return out;
    for (size_t i = 0; i < t->size() && out.size() < 23; ++i)
        out.push_back(static_cast<char16_t>(static_cast<unsigned char>((*t)[i])));
    return out;
}

// ---------------------------------------------------------------------------
// Element lists (spec 7.2: "float/int list of Element children").
// ---------------------------------------------------------------------------
std::vector<float> ReadFloatElementList(const Node* wrap, size_t cap) {
    std::vector<float> out;
    for (const Node* e = FindChild(wrap, "Element"); e && out.size() < cap; e = NextSibling(wrap, e, "Element"))
        out.push_back(GetFloat(e).value_or(0.0f));
    return out;
}
std::vector<uint32_t> ReadUInt32ElementList(const Node* wrap, size_t cap) {
    std::vector<uint32_t> out;
    for (const Node* e = FindChild(wrap, "Element"); e && out.size() < cap; e = NextSibling(wrap, e, "Element"))
        out.push_back(GetUInt32(e).value_or(0));
    return out;
}
// NoCustVariantsGrid (spec 7.3): each item is a variant NAME, stored here as
// its hash (the spec explicitly calls these "hashes"). Real data (see the
// file banner above) confirms the item element is <NoCustVariantElement>
// with the name in a <Variant> child - NOT a flat <Element>NAME</Element>.
std::vector<uint32_t> ParseNoCustVariantsGrid(const Node* row, size_t cap) {
    std::vector<uint32_t> out;
    const Node* wrap = FindChild(row, "NoCustVariantsGrid");
    for (const Node* e = FindChild(wrap, "NoCustVariantElement"); e && out.size() < cap; e = NextSibling(wrap, e, "NoCustVariantElement"))
        out.push_back(NameHashOrZero(ChildText(e, "Variant")));
    return out;
}

// ---------------------------------------------------------------------------
// Axles (spec 7.3, positional, capacity 2).
// ---------------------------------------------------------------------------
AxleEntry ParseAxle(const Node* n) {
    AxleEntry r;
    r.wheelDoesSteer = GetBool(n, "Wheel_Does_Steer");
    r.wheelReverseSteer = GetBool(n, "Wheel_Reverse_Steer");
    r.wheelDoesHandbrake = GetBool(n, "Wheel_Does_Handbrake");
    r.wheelEngineTorque = ReadFloatAlways(n, "Wheel_Engine_Torque");
    r.wheelMass = ReadFloatAlways(n, "Wheel_Mass");
    r.wheelFriction = ReadFloatAlways(n, "Wheel_Friction");
    r.aiWheelFriction = ReadFloatAlways(n, "AI_Wheel_Friction");
    r.wheelMaxFriction = ReadFloatAlways(n, "Wheel_Max_Friction");
    r.wheelDriftFrictionModifier = ReadFloatAlways(n, "Wheel_Drift_Friction_Modifier");
    r.wheelBrakingTorque = ReadFloatAlways(n, "Wheel_Braking_Torque");
    r.wheelSpringLength = ReadFloatAlways(n, "Wheel_Spring_Length");
    r.wheelSpringStrength = ReadFloatAlways(n, "Wheel_Spring_Strength");
    r.wheelSpringMaxForce = ReadFloatAlways(n, "Wheel_Spring_Max_Force");
    r.wheelDampComp = ReadFloatAlways(n, "Wheel_Damp_Comp");
    r.wheelDampExp = ReadFloatAlways(n, "Wheel_Damp_Exp");
    r.rampDampingComp = ReadFloatAlways(n, "Ramp_Damping_Comp");
    r.wheelMinLimit = ReadFloatAlways(n, "Wheel_Min_Limit");
    r.wheelMaxLimit = ReadFloatAlways(n, "Wheel_Max_Limit");
    return r;
}
std::vector<AxleEntry> ParseAxles(const Node* row) {
    std::vector<AxleEntry> out;
    const Node* wrap = FindChild(row, "Axles");
    for (const Node* a = FindChild(wrap, "Axle"); a && out.size() < 2; a = NextSibling(wrap, a, "Axle"))
        out.push_back(ParseAxle(a));
    return out;
}

TowableData ParseTowableData(const Node* row) {
    TowableData r;
    const Node* n = FindChild(row, "Towable_Data");
    r.towableFromFront = GetBool(n, "Towable_From_Front");
    r.towableFromRear = GetBool(n, "Towable_From_Rear");
    r.tweakingOffsetFront = ReadVec3Child(n, "Tweaking_Offset_Front");
    r.tweakingOffsetRear = ReadVec3Child(n, "Tweaking_Offset_Rear");
    return r;
}

AudioHysteresisBlock ParseAudioHysteresis(const Node* row) {
    AudioHysteresisBlock r;
    const Node* n = FindChild(row, "Audio_Hysteresis");
    r.audioRpmDecay = ReadFloatAlways(n, "Audio_Rpm_Decay");
    r.audioRpmIncrease = ReadFloatAlways(n, "Audio_Rpm_Increase");
    const Node* dev = FindChild(n, "RPM_Deviation");
    r.rpmDeviation.startTime = ReadFloatAlways(dev, "start_time");
    r.rpmDeviation.minDuration = ReadFloatAlways(dev, "min_duration");
    r.rpmDeviation.maxDuration = ReadUInt32Always(dev, "max_duration");
    r.rpmDeviation.minDeviation = ReadFloatAlways(dev, "min_deviation");
    r.rpmDeviation.maxDeviation = ReadFloatAlways(dev, "max_deviation");
    r.pointABefore = ReadFloatAlways(n, "Point_A_Before");
    r.pointAAfter = ReadFloatAlways(n, "Point_A_After");
    r.pointBBefore = ReadFloatAlways(n, "Point_B_Before");
    r.pointBAfter = ReadFloatAlways(n, "Point_B_After");
    // Medium_/High_Onload_Threshold: real data (see the file banner above)
    // confirms these sit in a SEPARATE <Audio_Onload_Parameters> wrapper,
    // not inside <Audio_Hysteresis> as first assumed.
    const Node* onload = FindChild(row, "Audio_Onload_Parameters");
    r.mediumOnloadThreshold = ReadFloatAlways(onload, "Medium_Onload_Threshold");
    r.highOnloadThreshold = ReadFloatAlways(onload, "High_Onload_Threshold");
    return r;
}

FoleyBlock ParseFoley(const Node* row) {
    FoleyBlock r;
    const Node* n = FindChild(row, "Foley");
    r.engineSoundId = ReadUInt16Always(n, "Engine");
    r.gearShiftHash = NameHashOrZero(ChildText(n, "Gear_Shift"));
    r.gearGrindHash = NameHashOrZero(ChildText(n, "Gear_Grind"));
    r.reverseHash = NameHashOrZero(ChildText(n, "Reverse"));
    r.skidHash = NameHashOrZero(ChildText(n, "Skid"));
    r.skidBrakesHash = NameHashOrZero(ChildText(n, "SkidBrakes"));
    r.lowImpactHash = NameHashOrZero(ChildText(n, "Low_Impact"));
    r.highImpactHash = NameHashOrZero(ChildText(n, "High_Impact"));
    r.scrapingHash = NameHashOrZero(ChildText(n, "Scraping"));
    r.corpseImpactHash = NameHashOrZero(ChildText(n, "Corpse_Impact"));
    r.componentImpactHash = NameHashOrZero(ChildText(n, "Component_Impact"));
    r.wheelImpactHash = NameHashOrZero(ChildText(n, "Wheel_Impact"));
    r.nitroHash = NameHashOrZero(ChildText(n, "Nitro"));
    r.doorOpenHash = NameHashOrZero(ChildText(n, "Door_Open"));
    r.doorCloseHash = NameHashOrZero(ChildText(n, "Door_Close"));
    r.radioSwitchHash = NameHashOrZero(ChildText(n, "RadioSwitch"));
    r.radioOnOffHash = NameHashOrZero(ChildText(n, "RadioOnOff"));
    return r;
}

// `n` is the <Effects> wrapper node (see the file banner above - real data
// confirms all of this reads from within <Effects>, not flat under <Vehicle>).
EffectIds ParseEffectIds(const Node* n) {
    EffectIds r;
    r.engineFireHash = NameHashOrZero(ChildText(n, "Engine_Fire"));
    r.engineSmokeHash = NameHashOrZero(ChildText(n, "Engine_Smoke"));
    r.engineSmokeBlackHash = NameHashOrZero(ChildText(n, "Engine_Smoke_Black"));
    r.explosionHash = NameHashOrZero(ChildText(n, "Explosion"));
    r.secondaryExplosionHash = NameHashOrZero(ChildText(n, "Secondary_Explosion"));
    // Exhaust_Types (real data, see the file banner above): a LIST of
    // <Exhaust_Type> items, each keyed by a <Type> value of "normal"/
    // "normal2"/"special"/"special2" - NOT four literal tag names as this
    // reader first assumed (there is no <normal>/<special> element anywhere
    // in real data; every sample instead shows <Exhaust_Type><Type>normal
    // </Type>...</Exhaust_Type>).
    {
        const Node* ex = FindChild(n, "Exhaust_Types");
        for (const Node* item = FindChild(ex, "Exhaust_Type"); item; item = NextSibling(ex, item, "Exhaust_Type")) {
            const std::string* ty = ChildText(item, "Type");
            if (!ty) continue;
            ExhaustEffectPair p;
            p.acceleratorEffectHash = NameHashOrZero(ChildText(item, "Exhaust_Accelerator"));
            p.noAcceleratorEffectHash = NameHashOrZero(ChildText(item, "Exhaust_No_Accelerator"));
            if (NameEquals(*ty, "normal")) r.exhaustTypes.normal = p;
            else if (NameEquals(*ty, "normal2")) r.exhaustTypes.normal2 = p;
            else if (NameEquals(*ty, "special")) r.exhaustTypes.special = p;
            else if (NameEquals(*ty, "special2")) r.exhaustTypes.special2 = p;
        }
    }
    r.mainRotorEffectHash = NameHashOrZero(ChildText(n, "Main_Rotor_Effect"));
    r.tailRotorEffectHash = NameHashOrZero(ChildText(n, "Tail_Rotor_Effect"));
    r.carSmokeHash = NameHashOrZero(ChildText(n, "Car_Smoke"));
    return r;
}

WheelSizeRange ParseWheelSizeRange(const Node* row) {
    WheelSizeRange r;  // already defaulted 7,7,7,7,2,2,2,2 (spec 7.4)
    const Node* wrap = FindChild(row, "Wheel_Size_Element");
    if (!wrap) return r;  // whole wrapper absent: defaults stand
    if (auto v = GetUInt8(wrap, "Wheel_Min_Size_0")) r.minSize0 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Min_Size_1")) r.minSize1 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Max_Size_0")) r.maxSize0 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Max_Size_1")) r.maxSize1 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Min_Width_0")) r.minWidth0 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Min_Width_1")) r.minWidth1 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Max_Width_0")) r.maxWidth0 = *v;
    if (auto v = GetUInt8(wrap, "Wheel_Max_Width_1")) r.maxWidth1 = *v;
    return r;
}

SeatSpecificData ParseSeat(const Node* seatNode) {
    SeatSpecificData r;
    r.aimModeHash = NameHashOrZero(ChildText(seatNode, "Aim_Mode"));
    r.fineAimModeHash = NameHashOrZero(ChildText(seatNode, "Fine_Aim_Mode"));
    r.zoomModeHash = NameHashOrZero(ChildText(seatNode, "Zoom_Mode"));
    r.targetPlayer = GetBool(seatNode, "Target_Player").value_or(false);
    return r;
}
// Wrapper "Seat_Specific_Data" per spec 7.3; the per-seat item element name
// ("Seat") is an INTERPRETATION - see vehicle_entry.h's field comment.
std::vector<SeatSpecificData> ParseSeatSpecificData(const Node* row) {
    std::vector<SeatSpecificData> out;
    const Node* wrap = FindChild(row, "Seat_Specific_Data");
    for (const Node* s = FindChild(wrap, "Seat"); s && out.size() < 8; s = NextSibling(wrap, s, "Seat"))
        out.push_back(ParseSeat(s));
    return out;
}

// Upgrade table (spec 7.3, +0xAF8..+0xB77). Real data (see the file banner
// above) confirms the shape is <Category>/Value/Levels/Level_N (multiplier)
// and <Category>/Cost/Levels/Level_N (cost) - Level_N's OWN text is the
// number.
UpgradeCategory ParseUpgradeCategory(const Node* catNode) {
    UpgradeCategory r;
    const Node* valueLevels = FindChild(FindChild(catNode, "Value"), "Levels");
    const Node* costLevels = FindChild(FindChild(catNode, "Cost"), "Levels");
    r.level1.multiplier = ReadFloatAlways(valueLevels, "Level_1");
    r.level2.multiplier = ReadFloatAlways(valueLevels, "Level_2");
    r.level3.multiplier = ReadFloatAlways(valueLevels, "Level_3");
    r.level4.multiplier = ReadFloatAlways(valueLevels, "Level_4");
    r.level1.cost = ReadUInt32Always(costLevels, "Level_1");
    r.level2.cost = ReadUInt32Always(costLevels, "Level_2");
    r.level3.cost = ReadUInt32Always(costLevels, "Level_3");
    r.level4.cost = ReadUInt32Always(costLevels, "Level_4");
    return r;
}
UpgradeTable ParseUpgradeTable(const Node* row) {
    UpgradeTable r;
    const Node* wrap = FindChild(row, "Upgrades");
    r.torque = ParseUpgradeCategory(FindChild(wrap, "Torque"));
    r.tire = ParseUpgradeCategory(FindChild(wrap, "Tire"));
    r.collision = ParseUpgradeCategory(FindChild(wrap, "Collision"));
    r.hitpoints = ParseUpgradeCategory(FindChild(wrap, "Hitpoints"));
    return r;
}

// ---------------------------------------------------------------------------
// Vehicle_Type variants (spec 7.5). See vehicle_type.h for the struct shapes
// and per-field OPEN/INTERPRETED notes.
// ---------------------------------------------------------------------------
SurfaceRecord ParseSurfaceRecord(const Node* n) {
    SurfaceRecord r;
    static constexpr std::string_view kSurfaceTypeNames[4] = {"Wing", "Aeleron", "Elevator", "Rudder"};
    r.type = EnumIndex(FindChild(n, "Type"), kSurfaceTypeNames, 4);
    r.xOffset = ReadFloatAlways(n, "X_Offset");
    r.yOffset = ReadFloatAlways(n, "Y_Offset");
    r.zOffset = ReadFloatAlways(n, "Z_Offset");
    r.primaryAngleDegrees = ReadFloatAlways(n, "Primary_Angle");
    r.secondaryAngleDegrees = ReadFloatAlways(n, "Secondary_Angle");
    r.liftFactor = ReadFloatAlways(n, "Lift_Factor");
    r.maxDeflectRadians = ReadDegAsRadAlways(n, "Max_Deflect");
    r.invertInput = GetBool(n, "Invert_Input");
    return r;
}
std::vector<SurfaceRecord> ParseSurfaces(const Node* variantNode) {
    std::vector<SurfaceRecord> out;
    const Node* wrap = FindChild(variantNode, "Surfaces");
    for (const Node* s = FindChild(wrap, "Surface"); s && out.size() < 8; s = NextSibling(wrap, s, "Surface"))
        out.push_back(ParseSurfaceRecord(s));
    return out;
}

AirControlBlock ParseAirControl(const Node* variantNode) {
    AirControlBlock r;
    const Node* n = FindChild(variantNode, "Air_Control");
    r.present = (n != nullptr);
    r.maxRollSpeedRadians = ReadDegAsRadAlways(n, "Max_Roll_Speed");
    r.maxRollAccelRadians = ReadDegAsRadAlways(n, "Max_Roll_Accel");
    r.maxPitchSpeedRadians = ReadDegAsRadAlways(n, "Max_Pitch_Speed");
    r.maxPitchAccelRadians = ReadDegAsRadAlways(n, "Max_Pitch_Accel");
    return r;
}

AutomobileVariant ParseAutomobile(const Node* n) {
    AutomobileVariant r;
    r.driftingYawTorqueFactor = ReadFloatAlways(n, "Drifting_Yaw_Torque_Factor");
    r.driftingForwardForce = ReadFloatAlways(n, "Drifting_Forward_Force");
    r.driftingForwardForceSpeedCapMs = ReadMphAsMsAlways(n, "Drifting_Forward_Force_Speed_Cap");
    r.driftingBoostStrength = ReadFloatAlways(n, "Drifting_Boost_Strength");
    r.peeloutFrictionModifier = ReadFloatAlways(n, "Peelout_Friction_Modifier");
    r.peeloutIdealAcceleratorInput = ReadFloatAlways(n, "Peelout_Ideal_Accelerator_Input");
    r.peeloutEndSpeedMs = ReadMphAsMsAlways(n, "Peelout_End_Speed");
    r.crushHeight = ReadFloatAlways(n, "Crush_Height");
    r.crushFactor = ReadFloatAlways(n, "Crush_Factor");
    r.automobileFlags = static_cast<uint8_t>(FlagBits(FindChild(n, "AutomobileFlags"), kAutomobileFlagNames));
    r.airControl = ParseAirControl(n);
    return r;
}

MotorcycleVariant ParseMotorcycle(const Node* n) {
    MotorcycleVariant r;
    const Node* lean = FindChild(n, "Leaning");
    r.leaning.maxLeanAngleRadians = ReadDegAsRadAlways(lean, "Max_Lean_Angle");
    r.leaning.maxLeanSpeedRadians = ReadDegAsRadAlways(lean, "Max_Lean_Speed");
    r.leaning.maxReturnSpeedRadians = ReadDegAsRadAlways(lean, "Max_Return_Speed");
    r.leaning.leanDampAngleRadians = ReadDegAsRadAlways(lean, "Lean_Damp_Angle");
    r.leaning.returnDampAngleRadians = ReadDegAsRadAlways(lean, "Return_Damp_Angle");
    r.maxSteeringAngleRadians = ReadDegAsRadAlways(n, "Max_Steering_Angle");
    r.turnSpeedMultiplier = ReadFloatAlways(n, "Turn_Speed_Multiplier");

    const Node* wheelie = FindChild(n, "Wheelie");
    r.wheelie.present = (wheelie != nullptr);
    r.wheelie.balanceAngleRadians = ReadDegAsRadAlways(wheelie, "Balance_Angle");
    r.wheelie.balanceRange = ReadFloatAlways(wheelie, "Balance_Range");
    {
        Always<float> raw = ReadFloatAlways(wheelie, "Range_Decay");
        r.wheelie.rangeDecay.present = raw.present;
        r.wheelie.rangeDecay.value = raw.present ? static_cast<int32_t>(std::lround(raw.value)) : 0;
    }
    r.wheelie.maxWheelieSpeedRadians = ReadDegAsRadAlways(wheelie, "Max_Wheelie_Speed");
    r.wheelie.wheelieDampAngleRadians = ReadDegAsRadAlways(wheelie, "Wheelie_Damp_Angle");
    r.wheelie.unbalancingAccelRadians = ReadDegAsRadAlways(wheelie, "Unbalancing_Accel");

    r.airControl = ParseAirControl(n);

    r.driftingYawTorqueFactor = ReadFloatAlways(n, "Drifting_Yaw_Torque_Factor");
    r.driftingForwardForce = ReadFloatAlways(n, "Drifting_Forward_Force");
    r.driftingForwardForceSpeedCapMs = ReadMphAsMsAlways(n, "Drifting_Forward_Force_Speed_Cap");
    r.driftingBoostStrength = ReadFloatAlways(n, "Drifting_Boost_Strength");
    r.peeloutFrictionModifier = ReadFloatAlways(n, "Peelout_Friction_Modifier");
    r.peeloutIdealAcceleratorInput = ReadFloatAlways(n, "Peelout_Ideal_Accelerator_Input");
    r.peeloutEndSpeedMs = ReadMphAsMsAlways(n, "Peelout_End_Speed");
    return r;
}

AirplaneEngineParams ParseAirplaneEngineParams(const Node* blk) {
    AirplaneEngineParams r;
    const Node* ep = FindChild(blk, "Engine_Params");
    r.engineAccel = ReadFloatAlways(ep, "Engine_Accel");
    r.engineReverseAccel = ReadFloatAlways(ep, "Engine_Reverse_Accel");
    r.engineRampUpTime = ReadFloatAlways(ep, "Engine_Ramp_Up_Time");
    r.engineRampDownTime = ReadFloatAlways(ep, "Engine_Ramp_Down_Time");
    return r;
}
// "Engines"/"Engine" wrapper/item names are an INTERPRETATION - see
// vehicle_type.h's EngineOffset comment.
std::vector<EngineOffset> ParseEngines(const Node* blk) {
    std::vector<EngineOffset> out;
    const Node* wrap = FindChild(blk, "Engines");
    for (const Node* e = FindChild(wrap, "Engine"); e && out.size() < 4; e = NextSibling(wrap, e, "Engine")) {
        EngineOffset eo;
        eo.xOffset = ReadFloatAlways(e, "X_Offset");
        eo.yOffset = ReadFloatAlways(e, "Y_Offset");
        eo.zOffset = ReadFloatAlways(e, "Z_Offset");
        out.push_back(eo);
    }
    return out;
}
PilotAssistParams ParsePilotAssistParams(const Node* blk) {
    PilotAssistParams r;
    const Node* n = FindChild(blk, "Pilot_Assist_Params");
    r.rollMaxAngularSpeedRadians = ReadDegAsRadAlways(n, "Roll_Max_Angular_Speed");
    r.rollDampAngleRadians = ReadDegAsRadAlways(n, "Roll_Damp_Angle");
    r.barrelRollMaxAngularSpeedRadians = ReadDegAsRadAlways(n, "Barrel_Roll_Max_Angular_Speed");
    r.horizontalMaxAngularSpeedRadians = ReadDegAsRadAlways(n, "Horizontal_Max_Angular_Speed");
    r.horizontalMaxAngularAccelRadians = ReadDegAsRadAlways(n, "Horizontal_Max_Angular_Accel");
    r.horizontalMaxAngularDecelRadians = ReadDegAsRadAlways(n, "Horizontal_Max_Angular_Decel");
    r.verticalMaxAngularSpeedRadians = ReadDegAsRadAlways(n, "Vertical_Max_Angular_Speed");
    r.verticalMaxAngularAccelRadians = ReadDegAsRadAlways(n, "Vertical_Max_Angular_Accel");
    r.liftMinSpeedMs = ReadMphAsMsAlways(n, "Lift_Min_Speed");
    r.turnLinearAccel = ReadFloatAlways(n, "Turn_Linear_Accel");
    return r;
}
AirplaneVariant ParseAirplane(const Node* n) {
    AirplaneVariant r;
    r.engines = ParseEngines(n);
    r.engineParams = ParseAirplaneEngineParams(n);
    r.reverseSpeedMs = ReadMphAsMsAlways(n, "Reverse_Speed");
    r.surfaces = ParseSurfaces(n);
    r.pilotAssistParams = ParsePilotAssistParams(n);
    return r;
}

HelicopterVariant ParseHelicopter(const Node* n) {
    HelicopterVariant r;
    r.elevationSpeedMs = ReadMphAsMsAlways(n, "Elevation_Speed");
    r.elevationAccel = ReadFloatAlways(n, "Elevation_Accel");
    r.elevationDamp = ReadFloatAlways(n, "Elevation_Damp");
    r.rotorCenter = ReadVec3Child(n, "Rotor_Center");
    r.rotorRadius = ReadFloatAlways(n, "Rotor_Radius");
    r.rotorTilt = ReadFloatAlways(n, "Rotor_Tilt");
    r.steeringAccel = ReadFloatAlways(n, "Steering_Accel");
    r.pitchBias = ReadFloatAlways(n, "Pitch_Bias");
    r.tailRotorCenter = ReadVec3Child(n, "Tail_Rotor_Center");
    r.tailAccel = ReadFloatAlways(n, "Tail_Accel");
    r.autolevelAccelRadians = ReadDegAsRadAlways(n, "Autolevel_Accel");
    r.aiAutolevelAccelRadians = ReadDegAsRadAlways(n, "AI_Autolevel_Accel");
    r.surfaces = ParseSurfaces(n);
    r.extraSpeedAccel = ReadFloatAlways(n, "Extra_Speed_Accel");
    r.aiExtraSpeedAccel = ReadFloatAlways(n, "AI_Extra_Speed_Accel");
    r.aiChaseExtraSpeedAccel = ReadFloatAlways(n, "AI_Chase_Extra_Speed_Accel");
    r.tiltLiftLoss = ReadFloatAlways(n, "Tilt_Lift_Loss");
    r.dropOffHeight = ReadFloatAlways(n, "Drop_Off_Height");
    return r;
}

VtolVariant ParseVtol(const Node* n) {
    VtolVariant r;
    r.airplaneMode = ParseAirplane(FindChild(n, "Airplane_Mode"));
    r.helicopterMode = ParseHelicopter(FindChild(n, "Helicopter_Mode"));
    return r;
}

WatercraftVariant ParseWatercraft(const Node* n) {
    WatercraftVariant r;
    // Real data (see the file banner above) confirms the LBS_* fields sit
    // inside their own <Lean_Based_Steering> wrapper, not directly under
    // <Watercraft> as first assumed.
    const Node* lbs = FindChild(n, "Lean_Based_Steering");
    r.lbsMinRadius = ReadFloatAlways(lbs, "LBS_Min_Radius");
    r.lbsMaxLeanAngleRadians = ReadDegAsRadAlways(lbs, "LBS_Max_Lean_Angle");
    r.lbsCorrectionAccelRadians = ReadDegAsRadAlways(lbs, "LBS_Correction_Acceleration");
    r.lbsTurnRadiusModifier = ReadFloatAlways(lbs, "LBS_Turn_Radius_Modifier");
    return r;
}

// Tests the six class tags in the documented order (spec 7.5: first present
// wins). Returns the matched child node too (needed by the flags pass for
// Hydraulics_Capable/Drifting_Possible/Is_Attack_Aircraft/Drop_off_copilot,
// which live inside the variant's own XML but land in the shared flag
// words, not in the variant struct - see vehicle_type.h).
const Node* ParseVehicleTypeUnion(const Node* row, VehicleTypeUnion& out) {
    const Node* vt = FindChild(row, "Vehicle_Type");
    const Node* c;
    if ((c = FindChild(vt, "Automobile")) != nullptr) {
        out.vehicleClass = VehicleClass::Automobile;
        out.automobile = ParseAutomobile(c);
        return c;
    }
    if ((c = FindChild(vt, "Motorcycle")) != nullptr) {
        out.vehicleClass = VehicleClass::Motorcycle;
        out.motorcycle = ParseMotorcycle(c);
        return c;
    }
    if ((c = FindChild(vt, "Airplane")) != nullptr) {
        out.vehicleClass = VehicleClass::Airplane;
        out.airplane = ParseAirplane(c);
        return c;
    }
    if ((c = FindChild(vt, "Helicopter")) != nullptr) {
        out.vehicleClass = VehicleClass::Helicopter;
        out.helicopter = ParseHelicopter(c);
        return c;
    }
    if ((c = FindChild(vt, "Vtol")) != nullptr) {
        out.vehicleClass = VehicleClass::Vtol;
        out.vtol = ParseVtol(c);
        return c;
    }
    if ((c = FindChild(vt, "Watercraft")) != nullptr) {
        out.vehicleClass = VehicleClass::Watercraft;
        out.watercraft = ParseWatercraft(c);
        return c;
    }
    out.vehicleClass = VehicleClass::Unknown;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Flag words (spec 7.6).
// ---------------------------------------------------------------------------
uint32_t ComputeFlags0(const Node* row, const Node* engineNode, const Node* effectsNode, const Node* classVariantNode, VehicleClass cls) {
    using namespace flags0;
    uint32_t f = 0;
    if (auto v = GetInt32(row, "ParkingSpawn"); v && *v != 0) f |= kParkingSpawn;
    // Nitro_Capable/Exhaust_All_Time: real data (see the file banner above)
    // confirms these read from within <Engine>/<Effects>, not flat top-level.
    if (GetBool(engineNode, "Nitro_Capable").value_or(false)) f |= kNitroCapable;
    if (GetBool(effectsNode, "Exhaust_All_Time").value_or(false)) f |= kExhaustAllTime;
    if (cls == VehicleClass::Automobile && GetBool(classVariantNode, "Hydraulics_Capable").value_or(false)) f |= kHydraulicsCapable;
    if (GetBool(row, "No_Arm").value_or(false)) f |= kNoArm;
    if (GetBool(row, "Unique").value_or(false)) f |= kUnique;
    if ((cls == VehicleClass::Automobile || cls == VehicleClass::Motorcycle) && DefaultSetUnlessNo(classVariantNode, "Drifting_Possible"))
        f |= kDriftingPossible;
    if (DefaultSetUnlessNo(row, "Burnout_Possible")) f |= kBurnoutPossible;
    if (GetBool(row, "Drops_Money").value_or(false)) f |= kDropsMoney;
    if (GetBool(row, "Immune_To_Flat_Tires").value_or(false)) f |= kImmuneToFlatTires;
    if (GetBool(row, "AlternativeRightThrow").value_or(false)) f |= kAlternativeRightThrow;
    if (GetBool(row, "AlternateFire").value_or(false)) f |= kAlternateFire;
    if (DefaultSetUnlessNo(row, "Allow_Passenger_Threat_Extract")) f |= kAllowPassengerThreatExtract;
    if (GetBool(row, "Explosion_In_Vehicle_Memory").value_or(false)) f |= kExplosionInVehicleMemory;
    if (GetBool(row, "Preload").value_or(false)) f |= kPreload;
    if (GetBool(row, "Has_Gullwing_Doors").value_or(false)) f |= kHasGullwingDoors;
    if (GetBool(row, "Use_High_Pullouts").value_or(false)) f |= kUseHighPullouts;
    if (GetBool(row, "Ignore_Convertible_Enter_Exit").value_or(false)) f |= kIgnoreConvertibleEnterExit;
    if (GetBool(row, "Extend_Detour_Hull_Length").value_or(false)) f |= kExtendDetourHullLength;
    ApplyShootersAllowed(row, f);
    if (DefaultSetUnlessNo(row, "Racing_Can_Spawn")) f |= kRacingCanSpawn;
    ApplyGarageCanStore(row, f);
    if (GetBool(row, "RadioTuner").value_or(false)) f |= kRadioTuner;
    if (GetBool(row, "2D_Lod_Fade").value_or(false)) f |= k2dLodFade;  // exact reader spelling (spec 7.7) - NOT "Lod_Fade_2d"
    if (GetBool(row, "Use_Stunts").value_or(false)) f |= kUseStunts;
    if (GetBool(row, "Useable_by_AI_LIFE").value_or(false)) f |= kUseableByAiLife;
    return f;
}

uint32_t ComputeFlags1(const Node* row, const std::optional<uint8_t>& id, const std::optional<std::string>& name,
                        const Node* helicopterLikeNode, VehicleClass cls) {
    using namespace flags1;
    uint32_t f = 0;
    if (GetBool(row, "Open_Doors_Automatically").value_or(false)) f |= kOpenDoorsAutomatically;
    if (GetBool(row, "Use_Advanced_Steering_Blending").value_or(false)) f |= kUseAdvancedSteeringBlending;
    if (GetBool(row, "Wieldable_weapons_disabled").value_or(false)) f |= kWieldableWeaponsDisabled;
    if (id && *id == 129) f |= kIdIs129;
    if (GetBool(row, "Passengers_Cast_Shadows").value_or(false)) f |= kPassengersCastShadows;
    if (GetBool(row, "Causes_Police_Notoriety").value_or(false)) f |= kCausesPoliceNotoriety;
    if ((cls == VehicleClass::Helicopter || cls == VehicleClass::Vtol) && GetBool(helicopterLikeNode, "Is_Attack_Aircraft").value_or(false))
        f |= kIsAttackAircraft;
    if (GetBool(row, "No_Explode_Or_Fire").value_or(false)) f |= kNoExplodeOrFire;
    if (GetBool(row, "Heavy").value_or(false)) f |= kHeavy;
    if (GetBool(row, "Military").value_or(false)) f |= kMilitary;
    if (GetBool(row, "Immune_To_Rc").value_or(false)) f |= kImmuneToRc;
    if (GetBool(row, "Explode_On_Crush").value_or(false)) f |= kExplodeOnCrush;
    if (GetBool(row, "Man_Cannon").value_or(false)) f |= kManCannon;
    if (name && NameEquals(*name, "car_4dr_genki")) f |= kGenkiNameSpecialCase;
    if (GetBool(row, "Requires_Spotlight_Check").value_or(false)) f |= kRequiresSpotlightCheck;
    if (GetBool(row, "BrassEnabled").value_or(false)) f |= kBrassEnabled;
    if ((cls == VehicleClass::Helicopter || cls == VehicleClass::Vtol) && DefaultSetUnlessNo(helicopterLikeNode, "Drop_off_copilot"))
        f |= kDropOffCopilot;
    if (GetBool(row, "Disable_Hair_Scrunch").value_or(false)) f |= kDisableHairScrunch;
    return f;
}

}  // namespace

// ===========================================================================
// ParseVehicleEntry
// ===========================================================================
VehicleEntry ParseVehicleEntry(const Node* row) {
    VehicleEntry e;

    // --- Identity ---
    e.name = CopyText(row, "Name", 0x18);
    e.nameHash = e.name ? NameHash(*e.name) : 0;
    e.isDlc = GetBool(row, "Is_DLC");

    const Node* classVariantNode = ParseVehicleTypeUnion(row, e.vehicleType);
    // For Vtol, Is_Attack_Aircraft/Drop_off_copilot are read from within the
    // reused Helicopter_Mode sub-block (see vehicle_type.h's VtolVariant
    // comment), not from the <Vtol> node directly.
    const Node* helicopterLikeNode = classVariantNode;
    if (e.vehicleType.vehicleClass == VehicleClass::Vtol) helicopterLikeNode = FindChild(classVariantNode, "Helicopter_Mode");

    e.id = GetUInt8(row, "ID");
    e.displayName = WidenDisplayName(ChildText(row, "Display_Name"));
    e.bitmapName = CopyText(FindChild(row, "Bitmap_Name"), "Filename", 0x18);
    e.hostageVehicleClass = EnumIndex(FindChild(row, "Hostage_Vehicle_Class"), kHostageVehicleClassNames, 6);
    e.group = OptText(row, "Group");

    // --- Mass/economy ---
    e.maxHitpoints = ReadUInt32Always(row, "Max_Hitpoints");
    e.mass = ReadFloatAlways(row, "Mass");
    e.componentDensity = ReadFloatAlways(row, "Component_Density");
    e.playerDamageMultiplier = ReadFloatAlways(row, "Player_Damage_Multiplier");
    e.value = ReadFloatAlways(row, "Value");
    e.props = ReadFloatAlways(row, "Props");
    e.mayhemValue = ReadUInt32Always(row, "Mayhem_Value");

    e.lodDistanceRatios = ReadFloatElementList(FindChild(row, "LOD_DistanceRatio"), 4);

    e.specialVehicleType = EnumIndex(FindChild(row, "Special_Vehicle_Type"), kSpecialVehicleTypeNames, 15);
    {
        int idx = EnumIndex(FindChild(row, "Road_Preference"), {"Highway", "No_highway"});
        e.roadPreference = (idx < 0) ? 0 : idx + 1;
    }

    // --- Engine (real data: inside <Engine> - see the file banner above) ---
    const Node* engineNode = FindChild(row, "Engine");
    e.torque = ReadFloatAlways(engineNode, "Torque");
    e.minRpm = ReadFloatAlways(engineNode, "Min_RPM");
    e.optRpm = ReadFloatAlways(engineNode, "Opt_RPM");
    e.maxRpm = ReadFloatAlways(engineNode, "Max_RPM");
    e.minRpmTorqueFactor = ReadFloatAlways(engineNode, "Min_RPM_Torque_Factor");
    e.maxRpmTorqueFactor = ReadFloatAlways(engineNode, "Max_RPM_Torque_Factor");
    e.minRpmResistance = ReadFloatAlways(engineNode, "Min_RPM_Resistance");
    e.optRpmResistance = ReadFloatAlways(engineNode, "Opt_RPM_Resistance");
    e.maxRpmResistance = ReadFloatAlways(engineNode, "Max_RPM_Resistance");

    // --- Axles ---
    e.axles = ParseAxles(row);

    // --- Transmission (real data: inside <Transmission>) ---
    const Node* transmissionNode = FindChild(row, "Transmission");
    e.gearRatios = ReadFloatElementList(FindChild(transmissionNode, "Gear_Ratios"), 6);
    e.downshiftRpms = ReadFloatElementList(FindChild(transmissionNode, "Downshift_RPMs"), 5);
    e.upshiftRpm = ReadFloatAlways(transmissionNode, "Upshift_RPM");
    e.diffGearRatio = ReadFloatAlways(transmissionNode, "Diff_Gear_Ratio");
    e.reverseGearRatio = ReadFloatAlways(transmissionNode, "Reverse_Gear_Ratio");
    e.clutchDelay = ReadFloatAlways(transmissionNode, "Clutch_Delay");
    e.maximumRpmRate = ReadFloatAlways(transmissionNode, "Maximum_RPM_Rate");
    e.clutchSlipRpm = ReadFloatAlways(transmissionNode, "Clutch_Slip_RPM");

    // --- Steering (real data: inside <Steering>) ---
    const Node* steeringNode = FindChild(row, "Steering");
    e.maxSteeringAngleRadians = ReadDegAsRadAlways(steeringNode, "Max_Steering_Angle");
    e.aiMaximumSteeringAngleRadians = ReadDegAsRadAlways(steeringNode, "AI_Maximum_Steering_Angle");
    e.maxSpeedSteeringMs = ReadMphAsMsAlways(steeringNode, "Max_Speed_Steering");
    e.aiMaxSpeedSteeringRaw = ReadFloatAlways(steeringNode, "AI_Max_Speed_Steering");  // spec 7.3: "not converted - observed"
    e.steeringWheelMaxSpeedRadians = ReadDegAsRadAlways(steeringNode, "Steering_Wheel_Max_Speed");
    e.steeringWheelMaxReturnSpeedRadians = ReadDegAsRadAlways(steeringNode, "Steering_Wheel_Max_Return_Speed");
    e.steeringWheelDampAngleRadians = ReadDegAsRadAlways(steeringNode, "Steering_Wheel_Damp_Angle");
    e.steeringWheelReturnDampAngleRadians = ReadDegAsRadAlways(steeringNode, "Steering_Wheel_Return_Damp_Angle");

    // --- Pedal block / aero ---
    e.minPedalToBlock = ReadFloatAlways(row, "Min_Pedal_To_Block");
    e.minTimeToBlock = ReadFloatAlways(row, "Min_Time_To_Block");
    e.aiMinTimeToBlock = ReadFloatAlways(row, "AI_Min_Time_To_Block");
    // real data: inside <Aerodynamics> - see the file banner above.
    const Node* aeroNode = FindChild(row, "Aerodynamics");
    e.airDensity = ReadFloatAlways(aeroNode, "Air_Density");
    e.frontalArea = ReadFloatAlways(aeroNode, "Frontal_Area");
    e.dragCoefficient = ReadFloatAlways(aeroNode, "Drag_Coefficient");
    e.liftCoefficient = ReadFloatAlways(aeroNode, "Lift_Coefficient");
    e.extraGravity = ReadFloatAlways(aeroNode, "Extra_Gravity");

    // --- Towing ---
    e.towableData = ParseTowableData(row);

    // --- Center of mass ---
    e.centerOfMassYOffset = ReadFloatAlways(row, "Center_Of_Mass_Y_Offset");
    e.centerOfMassZOffset = ReadFloatAlways(row, "Center_Of_Mass_Z_Offset");
    e.cameraProximityLockY = ReadFloatAlways(row, "Camera_Proximity_Lock_Y");

    // --- Rotation/friction ---
    e.staticLoadFriction = ReadFloatAlways(row, "Static_Load_Friction");
    e.aiStaticLoadFriction = ReadFloatAlways(row, "AI_Static_Load_Friction");
    e.rollTorqueFactor = ReadFloatAlways(row, "Roll_Torque_Factor");
    e.pitchTorqueFactor = ReadFloatAlways(row, "Pitch_Torque_Factor");
    e.yawTorqueFactor = ReadFloatAlways(row, "Yaw_Torque_Factor");
    e.aiYawTorqueFactor = ReadFloatAlways(row, "AI_Yaw_Torque_Factor");
    e.extraSteeringTorque = ReadFloatAlways(row, "Extra_Steering_Torque");
    e.rollUnitInertia = ReadFloatAlways(row, "Roll_Unit_Inertia");
    e.pitchUnitInertia = ReadFloatAlways(row, "Pitch_Unit_Inertia");
    e.yawUnitInertia = ReadFloatAlways(row, "Yaw_Unit_Inertia");
    e.yawUnitInertiaMultiplier = ReadFloatAlways(row, "Yaw_Unit_Inertia_Multiplier");
    e.rollUnitInertiaMultiplier = ReadFloatAlways(row, "Roll_Unit_Inertia_Multiplier");
    e.pitchUnitInertiaMultiplier = ReadFloatAlways(row, "Pitch_Unit_Inertia_Multiplier");
    e.aiYawUnitInertia = ReadFloatAlways(row, "AI_Yaw_Unit_Inertia");
    e.aiRollUnitInertia = ReadFloatAlways(row, "AI_Roll_Unit_Inertia");
    e.aiPitchUnitInertia = ReadFloatAlways(row, "AI_Pitch_Unit_Inertia");
    e.viscosityFriction = ReadFloatAlways(row, "Viscosity_Friction");
    e.aiMaxBrakingDecel = ReadFloatAlways(row, "AI_Max_Braking_Decel");
    e.aiMaxRadialAccel = ReadFloatAlways(row, "AI_Max_Radial_Accel");

    // --- Audio / Foley / effects ---
    e.audioHysteresis = ParseAudioHysteresis(row);
    e.foley = ParseFoley(row);
    // real data: inside <Effects> - see the file banner above.
    const Node* effectsNode = FindChild(row, "Effects");
    e.effectIds = ParseEffectIds(effectsNode);

    // --- Spin damping (real data: inside <Angular_Damping>) ---
    const Node* angularDampingNode = FindChild(row, "Angular_Damping");
    e.normalSpinDamping = ReadFloatAlways(angularDampingNode, "Normal_Spin_Damping");
    e.collisionSpinDamping = ReadFloatAlways(angularDampingNode, "Collision_Spin_Damping");
    e.collisionSpinThresholdRadians = ReadDegAsRadAlways(angularDampingNode, "Collision_Spin_Threshold");

    // --- Hitch ---
    e.trailerHitchType = EnumIndex(FindChild(row, "Trailer_Hitch_Type"), kHitchTypeNames, 2);
    e.tractorHitchType = EnumIndex(FindChild(row, "Tractor_Hitch_Type"), kHitchTypeNames, 2);
    e.trailerChance = ReadFloatAlways(row, "Trailer_Chance");

    // --- FOV / buoyancy ---
    e.maxFov = ReadFloatAlways(row, "Max_FOV");
    e.minFollowDistMultiplier = ReadFloatAlways(row, "Min_Follow_Dist_Multiplier");
    e.buoyancyModifier = ReadFloatAlways(FindChild(row, "Water_Physics"), "Buoyancy_Modifier");

    // --- Flag words ---
    e.flags0 = ComputeFlags0(row, engineNode, effectsNode, classVariantNode, e.vehicleType.vehicleClass);
    e.flags1 = ComputeFlags1(row, e.id, e.name, helicopterLikeNode, e.vehicleType.vehicleClass);

    // --- Top speed ---
    e.maximumSpeedMs = ReadMphAsMsAlways(row, "Maximum_Speed");
    e.maximumSpeedWithNitrousMs = ReadMphAsMsAlways(row, "Maximum_Speed_With_Nitrous");
    if (e.maximumSpeedWithNitrousMs.present && e.maximumSpeedMs.present &&
        e.maximumSpeedWithNitrousMs.value < e.maximumSpeedMs.value) {
        e.maximumSpeedWithNitrousMs.value = e.maximumSpeedMs.value;
    }

    // --- Hardtop / roof ---
    e.hardtopVids = ReadUInt32ElementList(FindChild(row, "Hardtop_VIDs"), 4);
    e.roofObstructionVids = ReadUInt32ElementList(FindChild(row, "RoofObstruction_VIDs"), 4);

    // --- Climb / seats ---
    e.climbToEntryOffset = ReadVec3Child(row, "Climb_To_Entry_Offset");
    e.prohibitedGunfireSeats = static_cast<uint8_t>(FlagBits(FindChild(row, "ProhibitedGunfireSeats"), kAutomobileFlagsSeatNames));

    // --- Customisation ---
    e.noCustVariantsGrid = ParseNoCustVariantsGrid(row, 16);
    e.custCameraNameHash = NameHashOrZero(ChildText(row, "Cust_Camera_Name"));

    // --- Wheel size range ---
    e.wheelSizeRange = ParseWheelSizeRange(row);
    e.racingSelectability = GetInt32(row, "Racing_Selectability");

    // --- Suspension/Perf ---
    e.suspensionRaiseMax = ReadFloatAlways(row, "Suspension_Raise_Max");
    e.suspensionLowerMin = ReadFloatAlways(row, "Suspension_Lower_Min");
    e.perfTorqueMultiplier = ReadFloatAlways(row, "Perf_Torque_Multiplier");

    // --- Seats ---
    e.seatSpecificData = ParseSeatSpecificData(row);

    // --- Interaction / animation references ---
    e.vehicleInteractionInfo.text = CopyText(row, "Vehicle_Interaction_Info", 0x40);
    e.vehicleInteractionInfo.hash = e.vehicleInteractionInfo.text ? NameHash(*e.vehicleInteractionInfo.text) : 0;
    e.characterAnimationSet.text = CopyText(row, "Character_Animation_Set", 0x40);
    e.characterAnimationSet.hash = e.characterAnimationSet.text ? NameHash(*e.characterAnimationSet.text) : 0;
    e.vehicleAnimationSetName = StripSuffixCI(OptText(row, "Vehicle_Animation_Set_Name"), ".xtbl");
    e.vehicleAnimationRigName = StripSuffixCI(OptText(row, "Vehicle_Animation_Rig_Name"), ".rigx");
    e.defaultAnimationStateHash = NameHashOrZero(ChildText(row, "Default_Animation_State"));

    // --- Upgrade table ---
    e.upgrades = ParseUpgradeTable(row);

    // --- Tail ---
    e.preventBottomingOffset = ReadFloatAlways(row, "Prevent_Bottoming_Offset");

    return e;
}

std::vector<VehicleEntry> ParseAllVehicles(const Document& doc) {
    std::vector<VehicleEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Vehicle"); row; row = NextSibling(table, row, "Vehicle")) out.push_back(ParseVehicleEntry(row));
    return out;
}

std::vector<VehicleEntry> ParseAllVehicleGroups(const Document& doc) {
    std::vector<VehicleEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Vehicle_Group"); row; row = NextSibling(table, row, "Vehicle_Group"))
        out.push_back(ParseVehicleEntry(row));
    return out;
}

}  // namespace sr3vehicleinfo
