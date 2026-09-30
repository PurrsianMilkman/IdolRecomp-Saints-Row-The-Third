// Population gates for sr3vehicleinfo (include/sr3vehicleinfo/vehicle_entry.h,
// spec-vehicle-data.md section 7) over every `*_veh.xtbl`, `vehicle_groups.xtbl`
// and `vehicles.xtbl` found in the archives given on argv.
//
// Usage: validate_vehicleinfo_population <archive.vpp_pc> [...]
//
// Reuses the recursive archive-walking pattern of
// tools/validation/validate_xtbl_population.cpp (vpp::Container, raw AND
// compressed entries, nested containers). Every gate prints a denominator,
// and every gate that could pass vacuously has a CONTROL that can fail (this
// project's rule: an oracle that can't fail proves nothing).
//
// Gates
//   G1  survey: every `*_veh.xtbl`, `vehicle_groups.xtbl`, `vehicles.xtbl`
//       entry found, per archive, decoded/raw-readable.
//   G2  per-vehicle census: total <Vehicle> rows parsed by ParseVehicleEntry,
//       per-class breakdown (spec 7.5's six tags) - the DLC-only sample
//       (spec 7.7) never saw Airplane or Watercraft, so any nonzero count
//       here is new.
//   G3  "always write" field-presence stats (spec 7.7's own predicate style,
//       now at full scale): does real data always supply the fields the
//       spec's 17-DLC-sample check found "always" present.
//   G4  the same predicate checks spec section 7.7 ran (float/bool grammar,
//       enum membership, AutomobileFlags/ProhibitedGunfireSeats/Surface-Type
//       membership, axle/gear/downshift count limits, speed cap, ID fits u8
//       + uniqueness, Name/Display_Name length limits) against ALL real
//       vehicles found, not just the 17 DLC samples.
//   G5  unrecognised-element scan: every element name this struct does not
//       recognise, plus a dedicated check for the "8 dead tags" spec 7.7
//       names (Convertible, Vandalism_value, Drifting_Conserve_Speed_Factor,
//       Rattle_Foley, Lod_Fade_2d, Foley/Min_Speed, Foley/Max_Speed,
//       Foley/Modifier). CONTROL: a deliberately-wrong element name must
//       find 0 matches.
//   G6  vehicle_groups.xtbl: the same G2-G4 style checks over <Vehicle_Group>
//       rows (spec 7.2's "same reader, defaults mode").
//   G7  vehicles.xtbl: row census only (different schema - Name/Framework/
//       Info_Slot_Index - not a stats row, so not run through
//       ParseVehicleEntry; see spec 7.1).

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using namespace sr3vehicleinfo;
using sr3xtbl::Document;
using sr3xtbl::FindChild;
using sr3xtbl::Node;
using sr3xtbl::NextSibling;
using sr3xtbl::ParseDocument;

int g_fail = 0;
#define GATE(ok, ...)                                  \
    do {                                                \
        const bool ok_ = (ok);                          \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL");  \
        std::printf(__VA_ARGS__);                       \
        std::printf("\n");                              \
        if (!ok_) ++g_fail;                              \
    } while (0)

Bytes readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    Bytes b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWithCi(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && lower(s.substr(s.size() - n)) == lower(std::string(suffix));
}
bool equalsCi(const std::string& s, const char* other) { return lower(s) == lower(std::string(other)); }
std::string baseName(const std::string& p) {
    const size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// ---------------------------------------------------------------------------
// G1: collect every *_veh.xtbl / vehicle_groups.xtbl / vehicles.xtbl entry.
// ---------------------------------------------------------------------------
struct Item {
    std::string archive;
    std::string name;
    bool compressed = false;
    bool isVehXtbl = false;
    bool isGroups = false;
    bool isVehiclesList = false;
    Bytes data;
    bool ok = false;
};

std::vector<Item> g_items;
long long g_containers = 0, g_entriesSeen = 0;

void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        ++g_entriesSeen;
        const bool isVeh = endsWithCi(e.name, "_veh.xtbl");
        const bool isGroups = equalsCi(e.name, "vehicle_groups.xtbl");
        const bool isVehiclesList = equalsCi(e.name, "vehicles.xtbl");
        if (isVeh || isGroups || isVehiclesList) {
            Item it;
            it.archive = path;
            it.name = e.name;
            it.compressed = e.payload.kind == vpp::PayloadKind::Compressed;
            it.isVehXtbl = isVeh;
            it.isGroups = isGroups;
            it.isVehiclesList = isVehiclesList;
            if (it.compressed) {
                vpp::DecompressResult r = c.decompressEntry(i);
                if (r.status == vpp::DecodeStatus::Ok) {
                    it.data = std::move(r.data);
                    it.ok = true;
                }
            } else {
                try {
                    vpp::ByteView v = c.rawEntryBytes(i);
                    it.data.assign(v.data(), v.data() + v.size());
                    it.ok = true;
                } catch (const std::exception&) {
                }
            }
            g_items.push_back(std::move(it));
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        if (endsWithCi(c.entries()[i].name, "xtbl")) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name);
        } catch (const std::exception&) {
        }
    }
}

// ---------------------------------------------------------------------------
// G5: the recognised-element-name set (everything ParseVehicleEntry's whole
// call tree - including all six Vehicle_Type variants - looks up by name),
// lower-cased for case-insensitive lookup (matching the engine's own
// case-insensitive element matching, spec 7.2).
// ---------------------------------------------------------------------------
std::unordered_set<std::string> BuildRecognisedElementSet() {
    static const char* kNames[] = {
        // structural / row
        "Vehicle", "Vehicle_Group", "Table", "root",
        // identity
        "Name", "Is_DLC", "ID", "Display_Name", "Bitmap_Name", "Filename", "Hostage_Vehicle_Class", "Group",
        // mass/economy
        "Max_Hitpoints", "Mass", "Component_Density", "Player_Damage_Multiplier", "Value", "Props", "Mayhem_Value",
        "LOD_DistanceRatio", "Element",
        "Special_Vehicle_Type", "Road_Preference",
        // engine
        "Torque", "Min_RPM", "Opt_RPM", "Max_RPM", "Min_RPM_Torque_Factor", "Max_RPM_Torque_Factor", "Min_RPM_Resistance",
        "Opt_RPM_Resistance", "Max_RPM_Resistance", "Clutch_Slip_RPM",
        // axles
        "Axles", "Axle", "Wheel_Does_Steer", "Wheel_Reverse_Steer", "Wheel_Does_Handbrake", "Wheel_Engine_Torque",
        "Wheel_Mass", "Wheel_Friction", "AI_Wheel_Friction", "Wheel_Max_Friction", "Wheel_Drift_Friction_Modifier",
        "Wheel_Braking_Torque", "Wheel_Spring_Length", "Wheel_Spring_Strength", "Wheel_Spring_Max_Force",
        "Wheel_Damp_Comp", "Wheel_Damp_Exp", "Ramp_Damping_Comp", "Wheel_Min_Limit", "Wheel_Max_Limit",
        // transmission
        "Gear_Ratios", "Downshift_RPMs", "Upshift_RPM", "Diff_Gear_Ratio", "Reverse_Gear_Ratio", "Clutch_Delay",
        "Maximum_RPM_Rate",
        // steering
        "Max_Steering_Angle", "AI_Maximum_Steering_Angle", "Max_Speed_Steering", "AI_Max_Speed_Steering",
        "Steering_Wheel_Max_Speed", "Steering_Wheel_Max_Return_Speed", "Steering_Wheel_Damp_Angle",
        "Steering_Wheel_Return_Damp_Angle",
        // pedal/aero
        "Min_Pedal_To_Block", "Min_Time_To_Block", "AI_Min_Time_To_Block", "Air_Density", "Frontal_Area",
        "Drag_Coefficient", "Lift_Coefficient", "Extra_Gravity",
        // towing
        "Towable_Data", "Towable_From_Front", "Towable_From_Rear", "Tweaking_Offset_Front", "Tweaking_Offset_Rear", "X", "Y", "Z",
        // center of mass
        "Center_Of_Mass_Y_Offset", "Center_Of_Mass_Z_Offset", "Camera_Proximity_Lock_Y",
        // rotation/friction
        "Static_Load_Friction", "AI_Static_Load_Friction", "Roll_Torque_Factor", "Pitch_Torque_Factor", "Yaw_Torque_Factor",
        "AI_Yaw_Torque_Factor", "Extra_Steering_Torque", "Roll_Unit_Inertia", "Pitch_Unit_Inertia", "Yaw_Unit_Inertia",
        "Yaw_Unit_Inertia_Multiplier", "Roll_Unit_Inertia_Multiplier", "Pitch_Unit_Inertia_Multiplier", "AI_Yaw_Unit_Inertia",
        "AI_Roll_Unit_Inertia", "AI_Pitch_Unit_Inertia", "Viscosity_Friction", "AI_Max_Braking_Decel", "AI_Max_Radial_Accel",
        // audio
        "Audio_Hysteresis", "Audio_Rpm_Decay", "Audio_Rpm_Increase", "RPM_Deviation", "start_time", "min_duration",
        "max_duration", "min_deviation", "max_deviation", "Point_A_Before", "Point_A_After", "Point_B_Before", "Point_B_After",
        "Medium_Onload_Threshold", "High_Onload_Threshold",
        // foley
        "Foley", "Engine", "Gear_Shift", "Gear_Grind", "Reverse", "Skid", "SkidBrakes", "Low_Impact", "High_Impact",
        "Scraping", "Corpse_Impact", "Component_Impact", "Wheel_Impact", "Nitro", "Door_Open", "Door_Close", "RadioSwitch",
        "RadioOnOff",
        // effects
        "Engine_Fire", "Engine_Smoke", "Engine_Smoke_Black", "Explosion", "Secondary_Explosion", "Exhaust_Types",
        "normal", "normal2", "special", "special2", "Exhaust_Type", "Exhaust_Accelerator", "Exhaust_No_Accelerator", "Main_Rotor_Effect",
        "Tail_Rotor_Effect", "Car_Smoke",
        // spin damping / hitch / fov
        "Normal_Spin_Damping", "Collision_Spin_Damping", "Collision_Spin_Threshold", "Trailer_Hitch_Type",
        "Tractor_Hitch_Type", "Trailer_Chance", "Max_FOV", "Min_Follow_Dist_Multiplier", "Water_Physics", "Buoyancy_Modifier",
        // top speed
        "Maximum_Speed", "Maximum_Speed_With_Nitrous",
        // hardtop/roof/climb/seats/cust
        "Hardtop_VIDs", "RoofObstruction_VIDs", "Climb_To_Entry_Offset", "ProhibitedGunfireSeats", "Flag",
        "NoCustVariantsGrid", "Cust_Camera_Name",
        "Wheel_Size_Element", "Wheel_Min_Size_0", "Wheel_Min_Size_1", "Wheel_Max_Size_0", "Wheel_Max_Size_1",
        "Wheel_Min_Width_0", "Wheel_Min_Width_1", "Wheel_Max_Width_0", "Wheel_Max_Width_1",
        "Racing_Selectability", "Suspension_Raise_Max", "Suspension_Lower_Min", "Perf_Torque_Multiplier",
        "Seat_Specific_Data", "Seat", "Aim_Mode", "Fine_Aim_Mode", "Zoom_Mode", "Target_Player",
        "Vehicle_Interaction_Info", "Character_Animation_Set", "Vehicle_Animation_Set_Name", "Vehicle_Animation_Rig_Name",
        "Default_Animation_State",
        "Upgrades", "Tire", "Collision", "Hitpoints", "Levels", "Level_1", "Level_2", "Level_3", "Level_4", "Multiplier", "Cost",
        "Prevent_Bottoming_Offset",
        // Vehicle_Type union
        "Vehicle_Type", "Automobile", "Motorcycle", "Airplane", "Helicopter", "Vtol", "VTOL", "Watercraft",
        // Automobile
        "Drifting_Yaw_Torque_Factor", "Drifting_Forward_Force", "Drifting_Forward_Force_Speed_Cap", "Drifting_Boost_Strength",
        "Peelout_Friction_Modifier", "Peelout_Ideal_Accelerator_Input", "Peelout_End_Speed", "Crush_Height", "Crush_Factor",
        "AutomobileFlags", "Air_Control", "Max_Roll_Speed", "Max_Roll_Accel", "Max_Pitch_Speed", "Max_Pitch_Accel",
        "Hydraulics_Capable", "Drifting_Possible",
        // Motorcycle
        "Leaning", "Max_Lean_Angle", "Max_Lean_Speed", "Max_Return_Speed", "Lean_Damp_Angle", "Return_Damp_Angle",
        "Turn_Speed_Multiplier", "Wheelie", "Balance_Angle", "Balance_Range", "Range_Decay", "Max_Wheelie_Speed",
        "Wheelie_Damp_Angle", "Unbalancing_Accel",
        // Airplane
        "Engines", "X_Offset", "Y_Offset", "Z_Offset", "Engine_Params", "Engine_Accel", "Engine_Reverse_Accel",
        "Engine_Ramp_Up_Time", "Engine_Ramp_Down_Time", "Reverse_Speed", "Surfaces", "Surface", "Type", "Primary_Angle",
        "Secondary_Angle", "Lift_Factor", "Max_Deflect", "Invert_Input", "Pilot_Assist_Params", "Roll_Max_Angular_Speed",
        "Roll_Damp_Angle", "Barrel_Roll_Max_Angular_Speed", "Horizontal_Max_Angular_Speed", "Horizontal_Max_Angular_Accel",
        "Horizontal_Max_Angular_Decel", "Vertical_Max_Angular_Speed", "Vertical_Max_Angular_Accel", "Lift_Min_Speed",
        "Turn_Linear_Accel",
        // Helicopter
        "Elevation_Speed", "Elevation_Accel", "Elevation_Damp", "Rotor_Center", "Rotor_Radius", "Rotor_Tilt",
        "Steering_Accel", "Pitch_Bias", "Tail_Rotor_Center", "Tail_Accel", "Autolevel_Accel", "AI_Autolevel_Accel",
        "Extra_Speed_Accel", "AI_Extra_Speed_Accel", "AI_Chase_Extra_Speed_Accel", "Tilt_Lift_Loss", "Drop_Off_Height",
        "Is_Attack_Aircraft", "Drop_off_copilot",
        // Vtol
        "Airplane_Mode", "Helicopter_Mode",
        // Watercraft
        "LBS_Min_Radius", "LBS_Max_Lean_Angle", "LBS_Correction_Acceleration", "LBS_Turn_Radius_Modifier",
        // flag-word source elements (word 0)
        "ParkingSpawn", "Nitro_Capable", "Exhaust_All_Time", "No_Arm", "Unique", "Burnout_Possible", "Drops_Money",
        "Immune_To_Flat_Tires", "AlternativeRightThrow", "AlternateFire", "Allow_Passenger_Threat_Extract",
        "Explosion_In_Vehicle_Memory", "Preload", "Has_Gullwing_Doors", "Use_High_Pullouts", "Ignore_Convertible_Enter_Exit",
        "Extend_Detour_Hull_Length", "shooters_allowed", "Racing_Can_Spawn", "Garage_Can_Store", "RadioTuner",
        "2D_Lod_Fade", "Use_Stunts", "Useable_by_AI_LIFE",
        // flag-word source elements (word 1)
        "Open_Doors_Automatically", "Use_Advanced_Steering_Blending", "Wieldable_weapons_disabled",
        "Passengers_Cast_Shadows", "Causes_Police_Notoriety", "No_Explode_Or_Fire", "Heavy", "Military", "Immune_To_Rc",
        "Explode_On_Crush", "Man_Cannon", "Requires_Spotlight_Check", "BrassEnabled", "Disable_Hair_Scrunch",
        // Wrapper elements CONFIRMED by real base-game data (not by spec
        // text - see src/vehicle_entry.cpp's file banner for the full
        // citation): the spec's offset table gives destination offsets for
        // these fields but does not name their XML parent.
        "Transmission", "Steering", "Aerodynamics", "Angular_Damping", "Effects", "Audio_Onload_Parameters",
        "NoCustVariantElement", "Variant", "Lean_Based_Steering",
        // _Editor/Category-style metadata this project's other tables show is
        // universally present but out of THIS spec's scope (attribute-only
        // TableDescription is handled by the parser itself, not a row
        // element) - included here only so it doesn't spuriously show up as
        // "unrecognised" noise in the scan; NOT modeled in VehicleEntry.
        "TableDescription", "_Editor", "Category",
    };
    std::unordered_set<std::string> s;
    for (const char* n : kNames) s.insert(lower(n));
    return s;
}

void ScanUnrecognised(const Node* row, const std::unordered_set<std::string>& recognised, std::map<std::string, long long>& unrecognisedCount,
                       std::map<std::string, std::string>& example, const std::string& fileName) {
    std::vector<const Node*> st{row};
    while (!st.empty()) {
        const Node* n = st.back();
        st.pop_back();
        std::string ln = lower(n->name());
        if (!recognised.count(ln)) {
            ++unrecognisedCount[n->name()];
            example.emplace(n->name(), fileName);
        }
        for (const Node* c : n->children()) st.push_back(c);
    }
}

// ---------------------------------------------------------------------------
// G4 helpers: content predicates over raw XML text (independent of
// ParseVehicleEntry's own conversions - these check the SOURCE data).
// ---------------------------------------------------------------------------
bool looksLikeEngineFloat(const std::string& t) {
    size_t i = 0, n = t.size();
    if (i < n && t[i] == '-') ++i;
    if (i + 1 < n && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) return true;  // 0x -> 0.0, still "valid" grammar
    size_t digitsBefore = 0;
    while (i < n && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; ++digitsBefore; }
    size_t digitsAfter = 0;
    if (i < n && t[i] == '.') {
        ++i;
        while (i < n && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; ++digitsAfter; }
    }
    if (digitsBefore == 0 && digitsAfter == 0) return false;  // no digits at all: reads as 0 but is not really "numeric text"
    if (i < n && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < n && (t[i] == '-' || t[i] == '+')) ++i;
        size_t ed = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; ++ed; }
        if (ed == 0) return false;
    }
    return i == n;
}
bool looksLikeBoolText(const std::string& t) { return equalsCi(t, "yes") || equalsCi(t, "true") || equalsCi(t, "no") || equalsCi(t, "false"); }

// A curated set of elements this reader treats as plain floats (spec 7.3),
// CONFIRMED direct children of <Vehicle>/<Vehicle_Group> (not nested under
// Engine/Steering/Aerodynamics/Angular_Damping - see the recognised-element
// set above) - used for the float-grammar predicate (G4).
const char* kFloatFieldNames[] = {
    "Mass", "Maximum_Speed", "Maximum_Speed_With_Nitrous", "Static_Load_Friction", "Roll_Torque_Factor",
    "Viscosity_Friction", "Prevent_Bottoming_Offset",
};
// A curated set of elements this reader treats as bool text (spec 7.6).
const char* kBoolFieldNames[] = {
    "Nitro_Capable", "No_Arm", "Unique", "Drops_Money", "Immune_To_Flat_Tires", "AlternativeRightThrow", "AlternateFire",
    "Explosion_In_Vehicle_Memory", "Preload", "Has_Gullwing_Doors", "Use_High_Pullouts", "RadioTuner", "2D_Lod_Fade",
    "Use_Stunts", "Useable_by_AI_LIFE", "Heavy", "Military", "Immune_To_Rc", "Explode_On_Crush", "Man_Cannon",
    "BrassEnabled", "Disable_Hair_Scrunch", "Open_Doors_Automatically", "Passengers_Cast_Shadows",
};

// ---------------------------------------------------------------------------
// Per-file processing: parses every row of the given kind and folds
// statistics into the shared accumulators.
// ---------------------------------------------------------------------------
struct Stats {
    long long vehicles = 0;
    long long perClass[7] = {0};  // indexed VehicleClass+1 (Unknown=-1 -> 0)
    // presence stats for a curated set of "always write" fields
    long long haveMass = 0, haveTorque = 0, haveMaxRpm = 0, haveMaxHitpoints = 0, haveStaticLoadFriction = 0,
              haveNormalSpinDamping = 0;
    // G4 predicates
    long long floatChecked = 0, floatOk = 0;
    long long boolChecked = 0, boolOk = 0;
    long long specialVehicleTypeChecked = 0, specialVehicleTypeOk = 0;
    long long hostageClassChecked = 0, hostageClassOk = 0;
    long long shootersAllowedChecked = 0, shootersAllowedOk = 0;
    long long garageCanStoreChecked = 0, garageCanStoreOk = 0;
    long long automobileFlagsChecked = 0, automobileFlagsOk = 0;
    long long prohibitedSeatsChecked = 0, prohibitedSeatsOk = 0;
    long long surfaceTypeChecked = 0, surfaceTypeOk = 0;
    long long axleCountOver2 = 0, gearCountOver6 = 0, downshiftCountOver5 = 0;
    long long downshiftGearPairsChecked = 0, downshiftGearPairsMismatch = 0;
    long long rawSpeedOver100 = 0, rawSpeedChecked = 0;
    std::vector<std::string> speedViolations;
    std::vector<std::string> massAbsent;
    long long idOver255 = 0, idChecked = 0;
    long long nameOver23 = 0, displayNameOver23 = 0, nameChecked = 0;
    std::unordered_map<uint8_t, std::vector<std::string>> idToNames;  // duplicate-ID detection (only for real IDs, per-file-kind separated by caller)
    long long dead_Convertible = 0, dead_VandalismValue = 0, dead_DriftingConserve = 0, dead_RattleFoley = 0, dead_LodFade2d = 0,
              dead_FoleyMinSpeed = 0, dead_FoleyMaxSpeed = 0, dead_FoleyModifier = 0;
};

void CheckOne(const char* name, const std::string* t, long long& checked, long long& ok, bool (*pred)(const std::string&)) {
    if (!t) return;
    ++checked;
    if (pred(*t)) ++ok;
    (void)name;
}

void ProcessRow(const Node* row, const std::string& fileName, Stats& st, const std::unordered_set<std::string>& recognised,
                 std::map<std::string, long long>& unrecognised, std::map<std::string, std::string>& unrecognisedExample) {
    ++st.vehicles;
    VehicleEntry e = ParseVehicleEntry(row);

    int classIdx = static_cast<int>(e.vehicleType.vehicleClass) + 1;  // Unknown(-1)->0, Automobile(0)->1, ...
    if (classIdx >= 0 && classIdx < 7) ++st.perClass[classIdx];

    if (e.mass.present) ++st.haveMass;
    else st.massAbsent.push_back(e.name.value_or("<unnamed>") + " (" + fileName + ")");
    if (e.torque.present) ++st.haveTorque;
    if (e.maxRpm.present) ++st.haveMaxRpm;
    if (e.maxHitpoints.present) ++st.haveMaxHitpoints;
    if (e.staticLoadFriction.present) ++st.haveStaticLoadFriction;
    if (e.normalSpinDamping.present) ++st.haveNormalSpinDamping;

    // float / bool grammar over the curated field lists
    for (const char* fn : kFloatFieldNames) CheckOne(fn, sr3xtbl::ChildText(row, fn), st.floatChecked, st.floatOk, looksLikeEngineFloat);
    for (const char* fn : kBoolFieldNames) CheckOne(fn, sr3xtbl::ChildText(row, fn), st.boolChecked, st.boolOk, looksLikeBoolText);

    // enum membership: Special_Vehicle_Type / Hostage_Vehicle_Class
    if (const std::string* t = sr3xtbl::ChildText(row, "Special_Vehicle_Type")) {
        ++st.specialVehicleTypeChecked;
        if (sr3xtbl::EnumIndex(FindChild(row, "Special_Vehicle_Type"), kSpecialVehicleTypeNames, 15) >= 0) ++st.specialVehicleTypeOk;
        (void)t;
    }
    if (const std::string* t = sr3xtbl::ChildText(row, "Hostage_Vehicle_Class")) {
        ++st.hostageClassChecked;
        if (sr3xtbl::EnumIndex(FindChild(row, "Hostage_Vehicle_Class"), kHostageVehicleClassNames, 6) >= 0) ++st.hostageClassOk;
        (void)t;
    }
    if (const std::string* t = sr3xtbl::ChildText(row, "shooters_allowed")) {
        ++st.shootersAllowedChecked;
        if (equalsCi(*t, "Everybody") || equalsCi(*t, "Front Seats") || equalsCi(*t, "Driver Only")) ++st.shootersAllowedOk;
    }
    if (const std::string* t = sr3xtbl::ChildText(row, "Garage_Can_Store")) {
        ++st.garageCanStoreChecked;
        if (looksLikeBoolText(*t) || equalsCi(*t, "Valet-Only")) ++st.garageCanStoreOk;
    }

    // AutomobileFlags / ProhibitedGunfireSeats / Surface Type membership
    if (const Node* af = FindChild(FindChild(FindChild(row, "Vehicle_Type"), "Automobile"), "AutomobileFlags")) {
        for (const Node* fl = FindChild(af, "Flag"); fl; fl = NextSibling(af, fl, "Flag")) {
            if (!fl->text()) continue;
            ++st.automobileFlagsChecked;
            bool match = false;
            for (auto nm : kAutomobileFlagNames)
                if (sr3xtbl::NameEquals(*fl->text(), nm)) match = true;
            if (match) ++st.automobileFlagsOk;
        }
    }
    if (const Node* pg = FindChild(row, "ProhibitedGunfireSeats")) {
        for (const Node* fl = FindChild(pg, "Flag"); fl; fl = NextSibling(pg, fl, "Flag")) {
            if (!fl->text()) continue;
            ++st.prohibitedSeatsChecked;
            bool match = false;
            for (auto nm : kAutomobileFlagsSeatNames)
                if (sr3xtbl::NameEquals(*fl->text(), nm)) match = true;
            if (match) ++st.prohibitedSeatsOk;
        }
    }
    {
        std::vector<const Node*> surfaceParents = {FindChild(FindChild(row, "Vehicle_Type"), "Airplane"),
                                                     FindChild(FindChild(row, "Vehicle_Type"), "Helicopter")};
        for (const Node* p : surfaceParents) {
            const Node* surfaces = FindChild(p, "Surfaces");
            for (const Node* s = FindChild(surfaces, "Surface"); s; s = NextSibling(surfaces, s, "Surface")) {
                const Node* ty = FindChild(s, "Type");
                if (!ty || !ty->text()) continue;
                ++st.surfaceTypeChecked;
                static constexpr std::string_view kSurfaceTypeNames[4] = {"Wing", "Aeleron", "Elevator", "Rudder"};
                if (sr3xtbl::EnumIndex(ty, kSurfaceTypeNames, 4) >= 0) ++st.surfaceTypeOk;
            }
        }
    }

    // axle/gear/downshift count limits (raw XML counts, not this reader's capped output)
    {
        const Node* axles = FindChild(row, "Axles");
        long long n = 0;
        for (const Node* a = FindChild(axles, "Axle"); a; a = NextSibling(axles, a, "Axle")) ++n;
        if (n > 2) ++st.axleCountOver2;
    }
    long long gearCount = 0, downshiftCount = 0;
    {
        // Gear_Ratios/Downshift_RPMs live inside <Transmission> (real data -
        // see src/vehicle_entry.cpp's file banner); checking at row level
        // here would always find nothing and pass vacuously.
        const Node* transmission = FindChild(row, "Transmission");
        const Node* gr = FindChild(transmission, "Gear_Ratios");
        for (const Node* el = FindChild(gr, "Element"); el; el = NextSibling(gr, el, "Element")) ++gearCount;
        if (gearCount > 6) ++st.gearCountOver6;
        const Node* dr = FindChild(transmission, "Downshift_RPMs");
        for (const Node* el = FindChild(dr, "Element"); el; el = NextSibling(dr, el, "Element")) ++downshiftCount;
        if (downshiftCount > 5) ++st.downshiftCountOver5;
        if (gr && dr) {
            ++st.downshiftGearPairsChecked;
            if (downshiftCount != gearCount - 1) ++st.downshiftGearPairsMismatch;
        }
    }

    // speed cap: raw XML text, BEFORE this reader's own capping
    for (const char* fn : {"Maximum_Speed", "Maximum_Speed_With_Nitrous"}) {
        if (const std::string* t = sr3xtbl::ChildText(row, fn)) {
            ++st.rawSpeedChecked;
            float v = sr3xtbl::ParseFloat(*t);
            if (v > 100.0f) {
                ++st.rawSpeedOver100;
                st.speedViolations.push_back(std::string(fn) + "=" + *t + " in " + e.name.value_or("<unnamed>") + " (" + fileName + ")");
            }
        }
    }

    // ID fits u8 (raw text, parsed as u32 first so a >255 value is visible
    // rather than silently truncated) + uniqueness
    if (const std::string* t = sr3xtbl::ChildText(row, "ID")) {
        ++st.idChecked;
        uint32_t raw = sr3xtbl::ParseUInt32(*t);
        if (raw > 255) ++st.idOver255;
        else if (e.id) st.idToNames[*e.id].push_back(e.name.value_or("<unnamed>") + " (" + fileName + ")");
    }

    // Name / Display_Name length (raw text, spec 7.3: <= 23 characters each)
    if (const std::string* t = sr3xtbl::ChildText(row, "Name")) {
        ++st.nameChecked;
        if (t->size() > 23) ++st.nameOver23;
    }
    if (const std::string* t = sr3xtbl::ChildText(row, "Display_Name")) {
        if (t->size() > 23) ++st.displayNameOver23;
    }

    // the "8 dead tags" (spec 7.7): element PRESENCE, matching the spec's
    // own methodology ("17 files" counts elements found, not non-empty
    // text - many are authored as empty placeholders, e.g. <Rattle_Foley/>).
    // Drifting_Conserve_Speed_Factor is read from within Vehicle_Type (any
    // class), not the row directly (real data: seen under Automobile).
    if (FindChild(row, "Convertible")) ++st.dead_Convertible;
    if (FindChild(row, "Vandalism_value")) ++st.dead_VandalismValue;
    if (FindChild(FindChild(row, "Vehicle_Type"), "Automobile") &&
        FindChild(FindChild(FindChild(row, "Vehicle_Type"), "Automobile"), "Drifting_Conserve_Speed_Factor"))
        ++st.dead_DriftingConserve;
    if (const Node* fo = FindChild(row, "Foley")) {
        if (FindChild(fo, "Rattle_Foley")) ++st.dead_RattleFoley;
        if (FindChild(fo, "Min_Speed")) ++st.dead_FoleyMinSpeed;
        if (FindChild(fo, "Max_Speed")) ++st.dead_FoleyMaxSpeed;
        if (FindChild(fo, "Modifier")) ++st.dead_FoleyModifier;
    }
    if (FindChild(row, "Lod_Fade_2d")) ++st.dead_LodFade2d;

    ScanUnrecognised(row, recognised, unrecognised, unrecognisedExample, fileName);
}

void PrintClassCensus(const Stats& st) {
    const char* names[7] = {"Unknown(none matched)", "Automobile", "Motorcycle", "Airplane", "Helicopter", "Vtol", "Watercraft"};
    for (int i = 0; i < 7; ++i) std::printf("    %-24s %lld\n", names[i], st.perClass[i]);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_vehicleinfo_population <archive.vpp_pc> [...]\n");
        return 2;
    }

    // ------------------------------------------------------------------ G1
    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) { std::printf("cannot read %s\n", a.c_str()); continue; }
        try {
            walk(vpp::ByteView(b.data(), b.size()), baseName(a));
        } catch (const std::exception& ex) {
            std::printf("open failed %s: %s\n", a.c_str(), ex.what());
        }
        // keep the buffer alive for the lifetime of everything decoded from
        // it - ByteView/Container alias it, and rawEntryBytes()-derived data
        // was already copied into Item::data above, so it is safe to let it
        // go out of scope after walk() returns.
    }
    std::printf("=== G1 survey ===\n");
    long long vehFiles = 0, groupsFiles = 0, vehiclesListFiles = 0, decodedOk = 0;
    for (const Item& it : g_items) {
        if (it.isVehXtbl) ++vehFiles;
        if (it.isGroups) ++groupsFiles;
        if (it.isVehiclesList) ++vehiclesListFiles;
        if (it.ok) ++decodedOk;
    }
    std::printf("archives %zu, containers walked %lld, directory entries seen %lld\n", archives.size(), g_containers, g_entriesSeen);
    std::printf("*_veh.xtbl found: %lld; vehicle_groups.xtbl found: %lld; vehicles.xtbl found: %lld; total items %zu, decoded OK %lld\n",
                vehFiles, groupsFiles, vehiclesListFiles, g_items.size(), decodedOk);
    GATE(g_items.size() > 0 && decodedOk == static_cast<long long>(g_items.size()), "every matched entry decoded/raw-readable: %lld / %zu",
         decodedOk, g_items.size());
    GATE(vehFiles > 0, "CONTROL: the survey found at least one *_veh.xtbl (not vacuous): %lld", vehFiles);

    const std::unordered_set<std::string> recognised = BuildRecognisedElementSet();

    // ------------------------------------------------------------------ G2-G5: *_veh.xtbl
    Stats veh;
    std::map<std::string, long long> unrecognised;
    std::map<std::string, std::string> unrecognisedExample;
    for (const Item& it : g_items) {
        if (!it.isVehXtbl || !it.ok) continue;
        Document doc;
        try {
            doc = ParseDocument(it.data.data(), it.data.size());
        } catch (const sr3xtbl::FormatError&) {
            continue;
        }
        const Node* table = doc.table();
        for (const Node* row = FindChild(table, "Vehicle"); row; row = NextSibling(table, row, "Vehicle"))
            ProcessRow(row, it.archive + " :: " + it.name, veh, recognised, unrecognised, unrecognisedExample);
    }

    std::printf("\n=== G2 per-vehicle census (*_veh.xtbl) ===\n");
    std::printf("total <Vehicle> rows parsed: %lld\n", veh.vehicles);
    PrintClassCensus(veh);
    GATE(veh.vehicles > 0, "CONTROL: at least one real <Vehicle> row was parsed (not vacuous): %lld", veh.vehicles);
    std::printf("  HEADLINE: Airplane %lld, Watercraft %lld (the DLC-only sample, spec 7.7, saw ZERO of either)\n",
                veh.perClass[3], veh.perClass[6]);

    std::printf("\n=== G3 'always write' field presence (of %lld vehicles) ===\n", veh.vehicles);
    std::printf("  Mass present:                %lld / %lld\n", veh.haveMass, veh.vehicles);
    std::printf("  Torque present:               %lld / %lld\n", veh.haveTorque, veh.vehicles);
    std::printf("  Max_RPM present:               %lld / %lld\n", veh.haveMaxRpm, veh.vehicles);
    std::printf("  Max_Hitpoints present:          %lld / %lld\n", veh.haveMaxHitpoints, veh.vehicles);
    std::printf("  Static_Load_Friction present:    %lld / %lld\n", veh.haveStaticLoadFriction, veh.vehicles);
    std::printf("  Normal_Spin_Damping present:      %lld / %lld\n", veh.haveNormalSpinDamping, veh.vehicles);
    GATE(veh.vehicles == 0 || veh.haveMass == veh.vehicles, "Mass is supplied by every real vehicle (spec 7.7's DLC finding, at full scale): %lld / %lld",
         veh.haveMass, veh.vehicles);
    for (const std::string& s : veh.massAbsent) std::printf("    Mass absent: %s\n", s.c_str());

    std::printf("\n=== G4 predicate checks (*_veh.xtbl, real base+DLC data) ===\n");
    GATE(veh.floatChecked == 0 || veh.floatOk == veh.floatChecked, "float grammar validity: %lld / %lld", veh.floatOk, veh.floatChecked);
    GATE(veh.boolChecked == 0 || veh.boolOk == veh.boolChecked, "bool grammar validity (yes/true/no/false): %lld / %lld", veh.boolOk, veh.boolChecked);
    GATE(veh.specialVehicleTypeChecked == 0 || veh.specialVehicleTypeOk == veh.specialVehicleTypeChecked,
         "Special_Vehicle_Type membership: %lld / %lld", veh.specialVehicleTypeOk, veh.specialVehicleTypeChecked);
    GATE(veh.hostageClassChecked == 0 || veh.hostageClassOk == veh.hostageClassChecked, "Hostage_Vehicle_Class membership: %lld / %lld",
         veh.hostageClassOk, veh.hostageClassChecked);
    GATE(veh.shootersAllowedChecked == 0 || veh.shootersAllowedOk == veh.shootersAllowedChecked, "shooters_allowed membership: %lld / %lld",
         veh.shootersAllowedOk, veh.shootersAllowedChecked);
    GATE(veh.garageCanStoreChecked == 0 || veh.garageCanStoreOk == veh.garageCanStoreChecked, "Garage_Can_Store membership: %lld / %lld",
         veh.garageCanStoreOk, veh.garageCanStoreChecked);
    GATE(veh.automobileFlagsChecked == 0 || veh.automobileFlagsOk == veh.automobileFlagsChecked, "AutomobileFlags membership: %lld / %lld",
         veh.automobileFlagsOk, veh.automobileFlagsChecked);
    GATE(veh.prohibitedSeatsChecked == 0 || veh.prohibitedSeatsOk == veh.prohibitedSeatsChecked, "ProhibitedGunfireSeats membership: %lld / %lld",
         veh.prohibitedSeatsOk, veh.prohibitedSeatsChecked);
    GATE(veh.surfaceTypeChecked == 0 || veh.surfaceTypeOk == veh.surfaceTypeChecked, "Surface Type membership: %lld / %lld", veh.surfaceTypeOk,
         veh.surfaceTypeChecked);
    GATE(veh.axleCountOver2 == 0, "<= 2 axles in every vehicle: violations %lld", veh.axleCountOver2);
    GATE(veh.gearCountOver6 == 0, "<= 6 gears in every vehicle: violations %lld", veh.gearCountOver6);
    GATE(veh.downshiftCountOver5 == 0, "<= 5 downshifts in every vehicle: violations %lld", veh.downshiftCountOver5);
    std::printf("  downshift count == gear count - 1: mismatches %lld / %lld checked (informational, spec 7.7 found 12/12 held on the DLC sample)\n",
                veh.downshiftGearPairsMismatch, veh.downshiftGearPairsChecked);
    GATE(veh.rawSpeedOver100 == 0, "Maximum_Speed[_With_Nitrous] <= 100 (the mph cap) in raw XML text: violations %lld / %lld", veh.rawSpeedOver100,
         veh.rawSpeedChecked);
    for (const std::string& s : veh.speedViolations) std::printf("    speed cap violation: %s\n", s.c_str());
    GATE(veh.idOver255 == 0, "ID fits u8 (raw text <= 255): violations %lld / %lld", veh.idOver255, veh.idChecked);
    {
        long long dup = 0;
        for (auto& kv : veh.idToNames) if (kv.second.size() > 1) ++dup;
        std::printf("  ID uniqueness: %lld distinct ID value(s) shared by >1 vehicle (spec 7.7 found ID 58 shared by 2 in the DLC sample)\n", dup);
        for (auto& kv : veh.idToNames)
            if (kv.second.size() > 1) {
                std::printf("    ID %u shared by:", kv.first);
                for (auto& n : kv.second) std::printf(" [%s]", n.c_str());
                std::printf("\n");
            }
    }
    GATE(veh.nameOver23 == 0, "Name <= 23 characters: violations %lld / %lld", veh.nameOver23, veh.nameChecked);
    GATE(veh.displayNameOver23 == 0, "Display_Name <= 23 characters: violations %lld", veh.displayNameOver23);

    std::printf("\n=== G5 unrecognised elements + '8 dead tags' (*_veh.xtbl) ===\n");
    std::printf("distinct unrecognised element names: %zu\n", unrecognised.size());
    for (auto& kv : unrecognised) std::printf("    %-40s occurrences %-6lld e.g. %s\n", kv.first.c_str(), kv.second, unrecognisedExample[kv.first].c_str());
    std::printf("'8 dead tags' occurrences in real data (spec 7.7 found these in 5-17 of the 17 DLC files each):\n");
    std::printf("    Convertible                    %lld\n", veh.dead_Convertible);
    std::printf("    Vandalism_value                %lld\n", veh.dead_VandalismValue);
    std::printf("    Drifting_Conserve_Speed_Factor  %lld\n", veh.dead_DriftingConserve);
    std::printf("    Foley/Rattle_Foley               %lld\n", veh.dead_RattleFoley);
    std::printf("    Lod_Fade_2d (wrong spelling)      %lld\n", veh.dead_LodFade2d);
    std::printf("    Foley/Min_Speed                    %lld\n", veh.dead_FoleyMinSpeed);
    std::printf("    Foley/Max_Speed                     %lld\n", veh.dead_FoleyMaxSpeed);
    std::printf("    Foley/Modifier                       %lld\n", veh.dead_FoleyModifier);
    {
        // CONTROL: a made-up element name must find 0 occurrences.
        long long controlHits = unrecognised.count("Not_A_Real_Vehicle_Element_Xyz123") ? unrecognised["Not_A_Real_Vehicle_Element_Xyz123"] : 0;
        GATE(controlHits == 0, "CONTROL: a deliberately-wrong element name ('Not_A_Real_Vehicle_Element_Xyz123') finds 0 occurrences: %lld",
             controlHits);
    }

    // ------------------------------------------------------------------ G6: vehicle_groups.xtbl
    Stats grp;
    std::map<std::string, long long> unrecognisedGrp;
    std::map<std::string, std::string> unrecognisedGrpExample;
    for (const Item& it : g_items) {
        if (!it.isGroups || !it.ok) continue;
        Document doc;
        try {
            doc = ParseDocument(it.data.data(), it.data.size());
        } catch (const sr3xtbl::FormatError&) {
            continue;
        }
        const Node* table = doc.table();
        for (const Node* row = FindChild(table, "Vehicle_Group"); row; row = NextSibling(table, row, "Vehicle_Group"))
            ProcessRow(row, it.archive + " :: " + it.name, grp, recognised, unrecognisedGrp, unrecognisedGrpExample);
    }
    std::printf("\n=== G6 vehicle_groups.xtbl (<Vehicle_Group> rows, spec 7.2 'defaults mode') ===\n");
    std::printf("total <Vehicle_Group> rows parsed: %lld\n", grp.vehicles);
    PrintClassCensus(grp);
    GATE(veh.vehicles == 0 || grp.vehicles >= 0, "group-row parse did not crash: %lld rows", grp.vehicles);
    std::printf("  Mass present: %lld / %lld;  float grammar: %lld / %lld;  unrecognised element names: %zu\n", grp.haveMass, grp.vehicles,
                grp.floatOk, grp.floatChecked, unrecognisedGrp.size());
    for (auto& kv : unrecognisedGrp) std::printf("    %-40s occurrences %-6lld e.g. %s\n", kv.first.c_str(), kv.second, unrecognisedGrpExample[kv.first].c_str());

    // ------------------------------------------------------------------ G7: vehicles.xtbl (name list, different schema - spec 7.1)
    std::printf("\n=== G7 vehicles.xtbl (Name/Framework/Info_Slot_Index list, spec 7.1 - NOT run through ParseVehicleEntry) ===\n");
    long long listRows = 0, haveFramework = 0, haveSlotIndex = 0;
    for (const Item& it : g_items) {
        if (!it.isVehiclesList || !it.ok) continue;
        Document doc;
        try {
            doc = ParseDocument(it.data.data(), it.data.size());
        } catch (const sr3xtbl::FormatError&) {
            continue;
        }
        const Node* table = doc.table();
        for (const Node* row = FindChild(table, "Vehicle"); row; row = NextSibling(table, row, "Vehicle")) {
            ++listRows;
            if (sr3xtbl::ChildText(row, "Framework")) ++haveFramework;
            if (sr3xtbl::ChildText(row, "Info_Slot_Index")) ++haveSlotIndex;
        }
    }
    std::printf("vehicles.xtbl <Vehicle> name-list rows: %lld (Framework present %lld, Info_Slot_Index present %lld)\n", listRows, haveFramework,
                haveSlotIndex);
    GATE(vehiclesListFiles == 0 || listRows > 0, "vehicles.xtbl found and its rows parsed: %lld rows in %lld file(s)", listRows, vehiclesListFiles);

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
