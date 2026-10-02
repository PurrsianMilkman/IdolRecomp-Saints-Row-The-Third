#pragma once

// sr3vehicleinfo - typed reader for the per-vehicle runtime entry that a
// `<name>_veh.xtbl`'s `<Vehicle>` element (and, in "defaults mode",
// vehicle_groups.xtbl's `<Vehicle_Group>` element) parses into
// (spec-vehicle-data.md section 7, added 2026-09-20).
//
// Built ENTIRELY on sr3xtbl's accessors (include/sr3xtbl/xtbl.h): FindChild,
// ChildText, GetX/ReadXAlways, ReadVec3[Child], FlagMask/HasFlag, EnumIndex,
// NameHash/NameHashOrZero. Nothing here touches the XML parser itself, and
// nothing here was read from the game .exe or any disassembly - every field,
// offset and default cited below is transcribed from spec-vehicle-data.md
// section 7 (the spec's own cleanroom boundary: it already encodes what
// another team recovered from the executable; this project consumes only
// the spec text, per the project's hard rule).
//
// ---------------------------------------------------------------------------
// SCOPE: two layers reuse this exact struct/reader (spec section 7.2)
// ---------------------------------------------------------------------------
// (1) `<name>_veh.xtbl`'s `<Vehicle>` element - the main target, sections
//     7.3-7.6, fully described.
// (2) `vehicle_groups.xtbl`'s `<Vehicle_Group>` element - parsed by the
//     SAME reader in "defaults mode" into the SAME 0xB80-stride layout,
//     holding raw/unconverted-adjacent units per section 7.2 (a vehicle row
//     copies the group row's value, overrides if present, and only THEN
//     converts units - so a group row itself is not yet unit-converted in
//     the real engine). This reader does NOT special-case "defaults mode":
//     ParseVehicleEntry always applies the section 7.3 unit conversions
//     (mph->m/s, deg->rad) to whatever it reads, whether the node handed to
//     it is a `<Vehicle>` or a `<Vehicle_Group>` element. A caller comparing
//     a group row's fields against the real engine's raw (pre-conversion)
//     group table should be aware of this - it is a documented, deliberate
//     simplification (a single-row parser has no "previous layer" to copy
//     from anyway), not a spec disagreement.
// NOT implemented: `vehicle_defaults.xtbl`'s `<Defaults>` global block
// (spec section 7.2 item 1, 7.9 item 6): its full offset layout is not
// given by the spec ("only its element vocabulary... was noted, not a full
// offset table") - explicitly OPEN there, so it is skipped here too.
//
// ---------------------------------------------------------------------------
// CONVENTIONS (matching sr3tables_weapons's house style)
// ---------------------------------------------------------------------------
// 1. Always<T> (sr3xtbl.h) vs std::optional<T>: the spec's section 7.2
//    describes the WHOLE reader family's general discipline as "initialise
//    from a default, then overwrite only if the element is present" but,
//    unlike spec-tables-weapons-combat.md, does NOT mark individual vehicle
//    fields "(a)"/"(i)" for which of sr3xtbl's two accessor flavours
//    (GetX = write-only-if-present vs ReadXAlways = always-write) applies.
//    Per this project's precedent (sr3tables_weapons/weapons.h item 1: "where
//    the spec gives no explicit marker, default to Always<T>, matching the
//    document's own general description of an always-write reader grammar"),
//    this reader does the same: Always<T> is the default for every scalar
//    numeric field, and std::optional<T>/plain sentinel values are used only
//    where the spec's own prose gives an explicit, caller-relevant absent-
//    value rule (e.g. Special_Vehicle_Type/-1, Road_Preference/0,
//    Hostage_Vehicle_Class/-1, Trailer_Hitch_Type/-1, the wheel-size-range
//    7/7/2/2 defaults) that this reader implements directly rather than
//    leaving to a caller.
// 2. Unit conversions (spec section 7, "Units"): mph->m/s is `x*0.44704f`,
//    applied to fields the spec marks (v), each first capped at 100 mph
//    (44.704 m/s) BEFORE conversion; deg->rad is `x*(pi/180)`, applied to
//    fields marked (r). Maximum_Speed_With_Nitrous is then raised to at
//    least Maximum_Speed if lower (spec section 7.3), applied here.
//    AI_Max_Speed_Steering is explicitly "not converted - observed" in the
//    spec and is left as raw mph text here, exactly as the spec states -
//    this is the one deliberately-NOT-converted field the spec calls out by
//    name, and the synthetic test suite has a dedicated case for it.
// 3. Cross-reference / resolver-out-of-scope fields (weapon-definition
//    pointers, Ammo/animation-group indices, ...): none of this table's
//    fields need a companion-table resolver except Weapon_Link (see OPEN
//    list below) - everything else is either a plain scalar or a name/text
//    reference already storable as std::string.
// 4. Where the entry layout documents a stored TRANSFORM of the XML value
//    (deg->rad, mph->m/s, a CRC of a name, Range_Decay's "rounded integer"),
//    this reader applies that single documented transform. It does NOT
//    reproduce transforms that fold several XML elements into a derived
//    geometric result with no further schema value here (the Helicopter
//    twelve-point rotor ring, a Surface record's direction vector) - see
//    vehicle_type.h for those.
// 5. Fields the spec marks OPEN/UNKNOWN, or for which no XML element name is
//    given at all, are left OUT of the typed struct - see "DELIBERATELY
//    OPEN / SKIPPED" below. Nothing is silently missing: every gap is either
//    listed there or commented at its would-be field.
//
// ---------------------------------------------------------------------------
// DELIBERATELY OPEN / SKIPPED (nothing below is a silent gap)
// ---------------------------------------------------------------------------
//  * vehicle_defaults.xtbl's <Defaults> global block (7.2 item 1, 7.9 item 6)
//    - offset layout not given by the spec. Not implemented at all (no
//    ParseVehicleDefaults function exists).
//  * Entry +0x20..+0x28: the element array (count/capacity/pointer) built
//    at runtime from a vehicle's .cvtf_pc customisation catalogue (7.1) -
//    not XML-sourced at all, out of scope for an XML-row parser.
//  * Entry +0x480: the resolved group-row POINTER (vs. the raw Group name
//    TEXT this reader keeps in VehicleEntry::group) - resolving it needs the
//    whole loaded group table, out of scope for a single-row parse (7.9
//    item 5 also flags the lookup's own -1/unchecked-index behaviour OPEN).
//  * Entry +0x628..+0x72B: the vehicle-camera parameter region (three
//    camera-angle sets, Camera_Follow_Aggression...). A 2026-10-02 spec
//    update (spec-vehicle-data.md 7.3's own field table, sourced from and
//    partly spot-checked against spec-tables-ui-controls.md 8.1 "Family A")
//    now gives CONFIRMED shapes for the three 32-byte angle sets and
//    Camera_Follow_Aggression/Position_Based/Sphere_Based - implemented, but
//    NOT inside VehicleEntry/ParseVehicleEntry. Per 8.1's own text, this
//    region is filled by a SEPARATE load pass over vehicle_cameras.xtbl
//    (vehicle mode) / vehicle_group_cameras.xtbl (group mode), whose own
//    <Vehicle>/<Vehicle_Group> rows are resolved by NAME against the
//    already-populated vehicle-info table (0x00ACB000/0x00ACE2D0 resolve
//    Name via 0x00AC27A0 to a row index, then call the shared
//    0x00ACA960/0x00AC5CF0 helpers) - it is NOT read from the SAME
//    <name>_veh.xtbl/vehicle_groups.xtbl row ParseVehicleEntry(row)
//    receives (that row has no Lookat_Offset/Camera_Follow_Aggression
//    children at all - this was verified directly against the spec text,
//    not assumed). See VehicleCameraRow/ParseVehicleCameraRow below (after
//    ParseVehicleEntry) for the new, correctly-scoped reader and its own
//    file-level comment for the full discovery note. Still genuinely OPEN
//    even there: four further sub-readers run after the three angle sets
//    with no element names tabulated at all (0x00AC6020, 0x00AC6210,
//    0x00AC6120, 0x00AC9910), and the +0x870 bit 0x40000000 mode-selector /
//    group-row-inheritance mechanics (needs a cross-file join, out of scope
//    for a single-row parser - same footing as the +0x480 group-row-pointer
//    item above).
//  * Vtol's +0x410 eight-dword Camera_Overrides block - untabulated (7.5,
//    7.9 item 2). Not implemented.
//  * Watercraft's un-tabulated offsets (+0x30..+0x54, +0x6C..+0x80) - the
//    spec explicitly says their child element names are not given (7.5).
//    Only the four tabulated LBS_* fields are implemented.
//  * Entry +0xAD0..+0xAEF: Weapon_Link (8 weapon-definition pointers) - the
//    spec gives no XML element name for this region at all (7.3), so per
//    this task's own instruction ("if it doesn't give an XML element name
//    for this, treat as OPEN and skip") it is not implemented.
//  * Entry +0x9AC's Upgrades table's exact per-level leaf element names
//    (Multiplier/Cost) are not given verbatim by the spec (only "f32
//    multipliers... and u32 costs", 7.3) - this reader's choice of
//    "Multiplier"/"Cost" child names is an INTERPRETATION, flagged at
//    UpgradeLevel below.
//  * The gap between Racing_Selectability (+0x908, ends +0x90C) and
//    Suspension_Raise_Max (+0x9AC): 0xA0 bytes with no offset-table entry
//    in the spec at all - nothing to implement (not even a named-but-OPEN
//    field), so nothing is modeled there.
//  * Entry +0xA7C: the "derived id" that follows Vehicle_Interaction_Info's
//    hash - the spec names it only as "a derived id" with no derivation
//    given (7.3); not implemented (the text and its plain NameHash ARE kept
//    - see VehicleEntry::vehicleInteractionInfo).
//  * Bits 15/25/30/31 of the +0x870 flag word and bit 7 (runtime "slot
//    live" bit) of +0x874 have no XML element at all (7.6: "copied from the
//    group row" / runtime-only) - left unset, see the flags0/flags1
//    constants and ComputeFlags870/874 in vehicle_entry.cpp.
//  * The +0x870 bit-14/name-based ("car_4dr_genki") and bike_jet01
//    Engine_Smoke_Black-default special cases (7.6) are DOCUMENTED at their
//    respective fields but only the name-based +0x874 bit 14 special case is
//    APPLIED (it is a pure flag set, unambiguous); the Engine_Smoke_Black
//    "defaults to Engine_Smoke unless bike_jet01" copy rule is documented
//    but NOT applied, matching this project's established convention of not
//    computing a field-copies-another-field default inside a single-row
//    parser (see sr3tables_weapons/weapons.h's AI_Wheel_Friction precedent).
//  * "Preload"'s downstream meaning inside the two runtime callees (7.6,
//    7.9 item 4) is out of scope - only the flag bit itself is modeled.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3vehicleinfo/vehicle_type.h"
#include "sr3xtbl/xtbl.h"

namespace sr3vehicleinfo {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3Result;

// ---------------------------------------------------------------------------
// Unit conversion constants (spec section 7, "Units").
// ---------------------------------------------------------------------------
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kMphToMs = 0.44704f;
inline constexpr float kMaxSpeedCapMph = 100.0f;
inline constexpr float kMaxSpeedCapMs = kMaxSpeedCapMph * kMphToMs;  // 44.704 m/s

// ---------------------------------------------------------------------------
// Enumerations (spec section 7.3/7.4). Index into the array == stored value,
// except where the spec gives an explicit different absent-value rule (noted
// per field in VehicleEntry below).
// ---------------------------------------------------------------------------
inline constexpr std::string_view kHostageVehicleClassNames[6] = {
    "Compact", "Sedan", "Luxury", "Exotic", "SUV", "Truck",
};  // other/absent -> -1 (spec 7.3, entry +0x47C)

inline constexpr std::string_view kSpecialVehicleTypeNames[15] = {
    "none", "Bus", "Limo", "Firetruck", "Tow Truck", "SWAT Van", "Police",
    "FBI", "Ambulance", "News Van", "Taxi", "Meter Maid", "Industrial",
    "ATV", "Tank",
};  // spec 7.3, entry +0x4C4

inline constexpr std::string_view kHitchTypeNames[2] = {"Semi", "Baggage"};  // -1 absent (spec 7.3, +0x854/+0x858)

inline constexpr std::string_view kAutomobileFlagsSeatNames[8] = {
    "Front Driver Side", "Front Passenger Side", "Rear Driver Side", "Rear Passenger Side",
    "Extra 1", "Extra 2", "Extra 3", "Extra 4",
};  // ProhibitedGunfireSeats (spec 7.3, +0x8B4 u8 bitmask)

// ---------------------------------------------------------------------------
// Flag word bit constants (spec section 7.6). Unlike sr3tables_weapons's
// generic <Flags><Flag>text</Flag></Flags> lists, EVERY bit here comes from
// its OWN distinctly-named XML element (a bool, an enum-selected pair of
// bits, or an integer test) - so these are bit positions for
// VehicleEntry::flags0/flags1, computed field-by-field in vehicle_entry.cpp,
// not a FlagMask() literal table.
// ---------------------------------------------------------------------------
namespace flags0 {  // entry +0x870
inline constexpr uint32_t kParkingSpawn = 1u << 0;                       // ParkingSpawn, integer != 0 (NOT bool text)
inline constexpr uint32_t kNitroCapable = 1u << 1;                       // Nitro_Capable
inline constexpr uint32_t kExhaustAllTime = 1u << 2;                     // Exhaust_All_Time
inline constexpr uint32_t kHydraulicsCapable = 1u << 3;                  // Vehicle_Type/Automobile's Hydraulics_Capable
inline constexpr uint32_t kNoArm = 1u << 4;                              // No_Arm
inline constexpr uint32_t kUnique = 1u << 5;                             // Unique
inline constexpr uint32_t kDriftingPossible = 1u << 6;                   // Vehicle_Type/{Automobile,Motorcycle}'s Drifting_Possible; default SET
inline constexpr uint32_t kBurnoutPossible = 1u << 7;                    // Burnout_Possible; "No" clears; default SET
inline constexpr uint32_t kDropsMoney = 1u << 8;                         // Drops_Money
inline constexpr uint32_t kImmuneToFlatTires = 1u << 9;                  // Immune_To_Flat_Tires
inline constexpr uint32_t kAlternativeRightThrow = 1u << 10;             // AlternativeRightThrow
inline constexpr uint32_t kAlternateFire = 1u << 11;                     // AlternateFire
inline constexpr uint32_t kAllowPassengerThreatExtract = 1u << 12;       // Allow_Passenger_Threat_Extract; "No" clears; default SET
inline constexpr uint32_t kExplosionInVehicleMemory = 1u << 13;          // Explosion_In_Vehicle_Memory
inline constexpr uint32_t kPreload = 1u << 14;                           // Preload
// bit 15: no element (copied from group row elsewhere) - never set here
inline constexpr uint32_t kHasGullwingDoors = 1u << 16;                  // Has_Gullwing_Doors
inline constexpr uint32_t kUseHighPullouts = 1u << 17;                   // Use_High_Pullouts
inline constexpr uint32_t kIgnoreConvertibleEnterExit = 1u << 18;        // Ignore_Convertible_Enter_Exit
inline constexpr uint32_t kExtendDetourHullLength = 1u << 19;            // Extend_Detour_Hull_Length
inline constexpr uint32_t kShootersAllowedFrontSeats = 1u << 20;         // shooters_allowed == "Front Seats"
inline constexpr uint32_t kShootersAllowedDriverOnly = 1u << 21;         // shooters_allowed == "Driver Only"
inline constexpr uint32_t kRacingCanSpawn = 1u << 22;                    // Racing_Can_Spawn; "No" clears; default SET
inline constexpr uint32_t kGarageCanStoreA = 1u << 23;                   // Garage_Can_Store: cleared by "No" and by "Valet-Only"
inline constexpr uint32_t kGarageCanStoreB = 1u << 24;                   // Garage_Can_Store: cleared only by "No"
// bit 25: no element (copied from group row elsewhere) - never set here
inline constexpr uint32_t kRadioTuner = 1u << 26;                        // RadioTuner
inline constexpr uint32_t k2dLodFade = 1u << 27;                         // "2D_Lod_Fade" (note the exact reader spelling - see the "8 dead tags" note below)
inline constexpr uint32_t kUseStunts = 1u << 28;                         // Use_Stunts
inline constexpr uint32_t kUseableByAiLife = 1u << 29;                   // Useable_by_AI_LIFE
// bit 30 (0x40000000): spec-tables-ui-controls.md 8.1 now documents this as
// the Camera_Follow_Aggression Position_Based/Sphere_Based mode selector,
// inherited from the group row when neither block is present - but its
// SOURCE element lives in vehicle_cameras.xtbl/vehicle_group_cameras.xtbl
// (see VehicleCameraRow below), a file this struct's ComputeFlags0/row never
// sees, so it is still never set here; no longer "no element at all", just
// not this reader's row to read it from.
// bit 31 (0x80000000): same file-provenance note - set when
// Camera_Follow_Aggression/Sphere_Based/Min_Angle is present (8.1); not set
// here for the same reason as bit 30.
}  // namespace flags0

namespace flags1 {  // entry +0x874
inline constexpr uint32_t kOpenDoorsAutomatically = 1u << 0;             // Open_Doors_Automatically
inline constexpr uint32_t kUseAdvancedSteeringBlending = 1u << 1;        // Use_Advanced_Steering_Blending
inline constexpr uint32_t kWieldableWeaponsDisabled = 1u << 2;           // Wieldable_weapons_disabled
inline constexpr uint32_t kIdIs129 = 1u << 3;                            // set when ID == 129 (0x81), not from a named element
inline constexpr uint32_t kPassengersCastShadows = 1u << 4;              // Passengers_Cast_Shadows
inline constexpr uint32_t kCausesPoliceNotoriety = 1u << 5;              // Causes_Police_Notoriety
inline constexpr uint32_t kIsAttackAircraft = 1u << 6;                   // Vehicle_Type/{Helicopter,Vtol}'s Is_Attack_Aircraft
// bit 7: runtime-only "slot live" bit - never set here
inline constexpr uint32_t kNoExplodeOrFire = 1u << 8;                    // No_Explode_Or_Fire
inline constexpr uint32_t kHeavy = 1u << 9;                              // Heavy
inline constexpr uint32_t kMilitary = 1u << 10;                          // Military
inline constexpr uint32_t kImmuneToRc = 1u << 11;                        // Immune_To_Rc
inline constexpr uint32_t kExplodeOnCrush = 1u << 12;                    // Explode_On_Crush
inline constexpr uint32_t kManCannon = 1u << 13;                         // Man_Cannon
inline constexpr uint32_t kGenkiNameSpecialCase = 1u << 14;              // set by NAME == "car_4dr_genki" (hard-coded), also copied from group row
inline constexpr uint32_t kRequiresSpotlightCheck = 1u << 15;            // Requires_Spotlight_Check
inline constexpr uint32_t kBrassEnabled = 1u << 16;                      // BrassEnabled
inline constexpr uint32_t kDropOffCopilot = 1u << 17;                    // Vehicle_Type/{Helicopter,Vtol}'s Drop_off_copilot; "No" clears; default SET
inline constexpr uint32_t kDisableHairScrunch = 1u << 18;                // Disable_Hair_Scrunch
}  // namespace flags1

// ---------------------------------------------------------------------------
// Sub-blocks, in entry offset order (spec section 7.3).
// ---------------------------------------------------------------------------

// One `Axles > Axle` element (spec 7.3: stride 0x40, capacity 2, POSITIONAL
// - the `<Name>Front</Name>` child is never read by the real reader).
struct AxleEntry {
    std::optional<bool> wheelDoesSteer;       // Wheel_Does_Steer, +0x00
    std::optional<bool> wheelReverseSteer;     // Wheel_Reverse_Steer, +0x01
    std::optional<bool> wheelDoesHandbrake;     // Wheel_Does_Handbrake, +0x02
    Always<float> wheelEngineTorque;             // Wheel_Engine_Torque, +0x04 (renormalised across axles at runtime; NOT done here)
    Always<float> wheelMass;                      // Wheel_Mass, +0x08
    Always<float> wheelFriction;                   // Wheel_Friction, +0x0C
    Always<float> aiWheelFriction;                  // AI_Wheel_Friction, +0x10; documented default = Wheel_Friction, NOT computed here
    Always<float> wheelMaxFriction;                  // Wheel_Max_Friction, +0x14; documented default = Wheel_Friction, NOT computed here
    Always<float> wheelDriftFrictionModifier;         // Wheel_Drift_Friction_Modifier, +0x18
    Always<float> wheelBrakingTorque;                  // Wheel_Braking_Torque, +0x1C
    Always<float> wheelSpringLength;                    // Wheel_Spring_Length, +0x20
    Always<float> wheelSpringStrength;                   // Wheel_Spring_Strength, +0x24
    Always<float> wheelSpringMaxForce;                    // Wheel_Spring_Max_Force, +0x28
    Always<float> wheelDampComp;                           // Wheel_Damp_Comp, +0x2C
    Always<float> wheelDampExp;                             // Wheel_Damp_Exp, +0x30
    Always<float> rampDampingComp;                           // Ramp_Damping_Comp, +0x34
    Always<float> wheelMinLimit;                              // Wheel_Min_Limit, +0x38
    Always<float> wheelMaxLimit;                               // Wheel_Max_Limit, +0x3C
};

// `Towable_Data` (spec 7.3/7.4, +0x5FC..+0x617).
struct TowableData {
    std::optional<bool> towableFromFront;    // Towable_From_Front, +0x5FC (u8)
    std::optional<bool> towableFromRear;      // Towable_From_Rear, +0x5FD (u8)
    Vec3Result tweakingOffsetFront;            // Tweaking_Offset_Front, +0x600 (vec3)
    Vec3Result tweakingOffsetRear;               // Tweaking_Offset_Rear, +0x60C (vec3)
};

// `RPM_Deviation` (spec 7.3, inside Audio_Hysteresis, +0x780..+0x790).
struct RpmDeviationBlock {
    Always<float> startTime;          // start_time, +0x780
    Always<float> minDuration;         // min_duration, +0x784
    Always<uint32_t> maxDuration;       // max_duration, +0x788 (u32)
    Always<float> minDeviation;          // min_deviation, +0x78C
    Always<float> maxDeviation;           // max_deviation, +0x790
};

// `Audio_Hysteresis` (spec 7.3/7.4: parent of Audio_Rpm_Decay - CONFIRMED by
// spec text - and, this reader originally assumed, the rest of the
// +0x778..+0x7A8 region). Real base-game data (src/vehicle_entry.cpp's file
// banner) confirms Audio_Rpm_Decay/Increase, RPM_Deviation and the four
// Point_A/B_Before/After fields ARE inside <Audio_Hysteresis>, but
// Medium_/High_Onload_Threshold are NOT - they sit in a separate
// <Audio_Onload_Parameters> wrapper (still read into this same struct here,
// since the destination offsets are contiguous per spec; only the XML
// SOURCE differs).
struct AudioHysteresisBlock {
    Always<float> audioRpmDecay;         // Audio_Rpm_Decay, +0x778
    Always<float> audioRpmIncrease;       // Audio_Rpm_Increase, +0x77C
    RpmDeviationBlock rpmDeviation;         // RPM_Deviation, +0x780..+0x790
    Always<float> pointABefore;              // Point_A_Before, +0x794
    Always<float> pointAAfter;                // Point_A_After, +0x798
    Always<float> pointBBefore;                // Point_B_Before, +0x79C
    Always<float> pointBAfter;                  // Point_B_After, +0x7A0
    Always<float> mediumOnloadThreshold;         // Medium_Onload_Threshold, +0x7A4; XML source is <Audio_Onload_Parameters>, not <Audio_Hysteresis> (real data)
    Always<float> highOnloadThreshold;            // High_Onload_Threshold, +0x7A8; same note
};

// `Foley` (spec 7.3/7.4, +0x7AC..+0x7EC). Engine is a plain sound id; the
// rest are u32 hashes of the named sound (spec: "u32 hashes of the named
// sounds"). `Rattle_Foley`/`Min_Speed`/`Max_Speed`/`Modifier` under this
// same wrapper are explicitly among the "8 dead tags" (spec 7.7) never
// consumed by the vehicle reader - not modeled here either.
struct FoleyBlock {
    Always<uint16_t> engineSoundId;    // Engine, +0x7AC (u16 sound id, NOT a hash)
    uint32_t gearShiftHash = 0;         // Gear_Shift, +0x7B0
    uint32_t gearGrindHash = 0;          // Gear_Grind, +0x7B4
    uint32_t reverseHash = 0;             // Reverse, +0x7B8
    uint32_t skidHash = 0;                 // Skid, +0x7BC
    uint32_t skidBrakesHash = 0;            // SkidBrakes, +0x7C0
    uint32_t lowImpactHash = 0;              // Low_Impact, +0x7C4
    uint32_t highImpactHash = 0;              // High_Impact, +0x7C8
    uint32_t scrapingHash = 0;                 // Scraping, +0x7CC
    uint32_t corpseImpactHash = 0;              // Corpse_Impact, +0x7D0
    uint32_t componentImpactHash = 0;            // Component_Impact, +0x7D4
    uint32_t wheelImpactHash = 0;                 // Wheel_Impact, +0x7D8
    uint32_t nitroHash = 0;                        // Nitro, +0x7DC
    uint32_t doorOpenHash = 0;                      // Door_Open, +0x7E0
    uint32_t doorCloseHash = 0;                      // Door_Close, +0x7E4
    uint32_t radioSwitchHash = 0;                     // RadioSwitch, +0x7E8
    uint32_t radioOnOffHash = 0;                       // RadioOnOff, +0x7EC
};

// `Exhaust_Types` matrix (spec 7.3: "four types normal/normal2/special/
// special2 x Exhaust_Accelerator/Exhaust_No_Accelerator"). Real data (see
// src/vehicle_entry.cpp's file banner) confirms the XML shape is a LIST of
// <Exhaust_Type> items each keyed by a <Type> value of "normal"/"normal2"/
// "special"/"special2" - not four literal tag names as first assumed.
struct ExhaustEffectPair {
    uint32_t acceleratorEffectHash = 0;      // Exhaust_Accelerator
    uint32_t noAcceleratorEffectHash = 0;     // Exhaust_No_Accelerator
};
struct ExhaustTypes {
    ExhaustEffectPair normal;    // "normal"
    ExhaustEffectPair normal2;    // "normal2"
    ExhaustEffectPair special;     // "special"
    ExhaustEffectPair special2;     // "special2"
};

// Effect ids (spec 7.3, +0x7F0..+0x847, 22 dwords - 16 individually named,
// the remaining 6 unaccounted for by the spec's own list are not modeled).
// Each is a u32 hash of a named effect element's text (NameHashOrZero),
// same treatment as the Foley sound hashes above. XML source confirmed by
// real data: nested inside an <Effects> wrapper (Exhaust_All_Time, a
// flags0 bit, is inside it too).
struct EffectIds {
    uint32_t engineFireHash = 0;             // Engine_Fire
    uint32_t engineSmokeHash = 0;              // Engine_Smoke
    uint32_t engineSmokeBlackHash = 0;          // Engine_Smoke_Black; documented default = Engine_Smoke for class >= 2 EXCEPT name "bike_jet01" (spec 7.3/7.6) - NOT computed here, raw value kept
    uint32_t explosionHash = 0;                  // Explosion
    uint32_t secondaryExplosionHash = 0;          // Secondary_Explosion
    ExhaustTypes exhaustTypes;                     // Exhaust_Types
    uint32_t mainRotorEffectHash = 0;               // Main_Rotor_Effect
    uint32_t tailRotorEffectHash = 0;                // Tail_Rotor_Effect
    uint32_t carSmokeHash = 0;                        // Car_Smoke
};

// `Seat_Specific_Data` (spec 7.3, +0x9B8..+0xA37: 8 seats x 0x10 bytes).
// The spec names only the region and its four per-seat fields, not the
// wrapper's per-seat item element name - this reader's choice of "Seat" is
// an INTERPRETATION (same footing as vehicle_type.h's "Engines"/"Engine").
struct SeatSpecificData {
    uint32_t aimModeHash = 0;          // Aim_Mode
    uint32_t fineAimModeHash = 0;       // Fine_Aim_Mode
    uint32_t zoomModeHash = 0;           // Zoom_Mode
    bool targetPlayer = false;            // Target_Player (bit)
};

// One `Upgrades/{Torque,Tire,Collision,Hitpoints}/Level_N` (spec 7.3,
// +0xAF8..+0xB77: "f32 multipliers... and u32 costs", no further XML shape
// given). Real base-game data (src/vehicle_entry.cpp's file banner)
// confirms the actual shape: <Category>/Value/Levels/Level_N holds the
// multiplier and <Category>/Cost/Levels/Level_N holds the cost - Level_N's
// OWN text is the number, not a further-nested "Multiplier"/"Cost" leaf.
struct UpgradeLevel {
    Always<float> multiplier;      // Value/Levels/Level_N text; documented default 1.0, NOT auto-applied
    Always<uint32_t> cost;          // Cost/Levels/Level_N text; documented default 50, NOT auto-applied
};
struct UpgradeCategory {
    UpgradeLevel level1, level2, level3, level4;   // Level_1..Level_4
};
struct UpgradeTable {
    UpgradeCategory torque;      // Upgrades/Torque
    UpgradeCategory tire;         // Upgrades/Tire
    UpgradeCategory collision;     // Upgrades/Collision
    UpgradeCategory hitpoints;      // Upgrades/Hitpoints
};

// `Wheel_Size_Element` (spec 7.4: eight u8 fields, +0x900..+0x907). When the
// WHOLE wrapper element is absent, the spec states explicit defaults
// (7,7 / 7,7 / 2,2 / 2,2) - this reader applies them (both when the wrapper
// itself is absent, and per-field when an individual child inside a present
// wrapper is absent, as a conservative extension of the same rule).
struct WheelSizeRange {
    uint8_t minSize0 = 7, minSize1 = 7;    // Wheel_Min_Size_0/1
    uint8_t maxSize0 = 7, maxSize1 = 7;    // Wheel_Max_Size_0/1
    uint8_t minWidth0 = 2, minWidth1 = 2;  // Wheel_Min_Width_0/1
    uint8_t maxWidth0 = 2, maxWidth1 = 2;  // Wheel_Max_Width_0/1
};

// Vehicle_Interaction_Info / Character_Animation_Set (spec 7.3, char[0x40]
// each + a name hash). Per this task's instruction, the TEXT is kept, not
// just the hash.
struct NamedHashedText {
    std::optional<std::string> text;   // the element's own text, bounded to 0x40 bytes (0x3F usable + NUL) as the real char[0x40] would be
    uint32_t hash = 0;                  // NameHash(text) if present, else 0
};

// ---------------------------------------------------------------------------
// VehicleEntry - the 0xB80-byte runtime entry, mirrored field-by-field.
// ---------------------------------------------------------------------------
struct VehicleEntry {
    // --- Identity (spec 7.3 +0x00..+0x1C, 7.4) ---
    std::optional<std::string> name;    // Name, +0x00 (char[0x18], <= 23 chars)
    uint32_t nameHash = 0;                // +0x18: NameHash(Name) - computed here, not itself an XML value
    std::optional<bool> isDlc;             // Is_DLC, +0x1C gate byte (0x00 true / 0xFF false in the real entry)

    // Entry +0x20..+0x28 (element array count/capacity/pointer): NOT
    // XML-sourced (built from .cvtf_pc at runtime, spec 7.1) - not modeled.

    VehicleTypeUnion vehicleType;   // +0x2C class enum, +0x30 variant union (spec 7.5, vehicle_type.h)

    std::optional<uint8_t> id;       // ID, +0x430

    std::u16string displayName;       // Display_Name, +0x432 (UTF-16, <= 23 code units + NUL; ASCII-widened per spec 7.3)

    std::optional<std::string> bitmapName;    // Bitmap_Name/Filename, +0x462 (char[0x18])

    int32_t hostageVehicleClass = -1;   // Hostage_Vehicle_Class, +0x47C; -1 if absent/unmatched ("other -1")
    std::optional<std::string> group;     // Group (+0x480 in the real entry is a resolved POINTER to the matching vehicle_groups.xtbl row - NOT reproduced; this is the raw Group NAME text instead, see the OPEN list above)

    // --- Mass/economy (spec 7.3 +0x494..+0x4AC) ---
    Always<uint32_t> maxHitpoints;        // Max_Hitpoints, +0x494 (u32)
    Always<float> mass;                     // Mass, +0x498
    Always<float> componentDensity;           // Component_Density, +0x49C (type not distinguished by the spec beyond "unmarked field in a float-heavy block" - this reader's judgement call, see header banner item 1)
    Always<float> playerDamageMultiplier;       // Player_Damage_Multiplier, +0x4A0; documented default 1.0, NOT auto-applied
    Always<float> value;                          // Value, +0x4A4 (judgement call: float, see Component_Density's note)
    Always<float> props;                            // Props, +0x4A8 (judgement call: float, see Component_Density's note)
    Always<uint32_t> mayhemValue;                     // Mayhem_Value, +0x4AC; spec: "read only in group mode" - this reader reads it whenever present regardless of which element type was handed in (see the SCOPE banner above)

    // --- LOD_DistanceRatio (spec 7.3 +0x4B0..+0x4C3) ---
    std::vector<float> lodDistanceRatios;   // LOD_DistanceRatio/Element, capacity 4 (this reader caps at 4; the real loader does not bound the XML count, which would overflow adjacent memory - not reproduced)

    // --- Special_Vehicle_Type / Road_Preference (spec 7.3 +0x4C4/+0x4C8) ---
    int32_t specialVehicleType = -1;    // Special_Vehicle_Type; -1 if absent/unmatched (index 0 is the literal text "none")
    int32_t roadPreference = 0;           // Road_Preference; spec: "0 absent, 1 Highway, 2 No_highway" (explicit absent value, unlike most enums here)

    // --- Engine (spec 7.3 +0x4CC..+0x4F0). XML source confirmed by real base-
    // game data (see src/vehicle_entry.cpp's file banner): nested inside an
    // <Engine> wrapper (Nitro_Capable, a flags0 bit, is inside it too) ---
    Always<float> torque;                 // Torque, +0x4CC
    Always<float> minRpm;                  // Min_RPM, +0x4D0
    Always<float> optRpm;                   // Opt_RPM, +0x4D4
    Always<float> maxRpm;                    // Max_RPM, +0x4D8
    Always<float> minRpmTorqueFactor;          // Min_RPM_Torque_Factor, +0x4DC
    Always<float> maxRpmTorqueFactor;           // Max_RPM_Torque_Factor, +0x4E0
    Always<float> minRpmResistance;               // Min_RPM_Resistance, +0x4E4
    Always<float> optRpmResistance;                // Opt_RPM_Resistance, +0x4E8
    Always<float> maxRpmResistance;                 // Max_RPM_Resistance, +0x4EC
    Always<float> clutchSlipRpm;                      // Clutch_Slip_RPM, +0x4F0 (a transmission field stored inside the engine run, per spec); XML source is <Transmission>, not <Engine> (real data)

    // --- Axles (spec 7.3 +0x4F4..+0x577) ---
    std::vector<AxleEntry> axles;   // Axles/Axle, POSITIONAL, capacity 2 (this reader caps at 2, matching the documented capacity; a 3rd <Axle> in real data would overrun into the gear count in the real engine - not reproduced)

    // --- Transmission (spec 7.3 +0x578..+0x5B8). XML source confirmed by
    // real data: nested inside a <Transmission> wrapper ---
    std::vector<float> gearRatios;        // Gear_Ratios/Element, capacity 6
    std::vector<float> downshiftRpms;      // Downshift_RPMs/Element, capacity 5
    Always<float> upshiftRpm;               // Upshift_RPM, +0x5A8
    Always<float> diffGearRatio;              // Diff_Gear_Ratio, +0x5AC
    Always<float> reverseGearRatio;             // Reverse_Gear_Ratio, +0x5B0
    Always<float> clutchDelay;                    // Clutch_Delay, +0x5B4
    Always<float> maximumRpmRate;                   // Maximum_RPM_Rate, +0x5B8

    // --- Steering (spec 7.3 +0x5BC..+0x5D8). XML source confirmed by real
    // data: nested inside a <Steering> wrapper ---
    Always<float> maxSteeringAngleRadians;             // Max_Steering_Angle, +0x5BC, (r)
    Always<float> aiMaximumSteeringAngleRadians;         // AI_Maximum_Steering_Angle, +0x5C0, (r)
    Always<float> maxSpeedSteeringMs;                      // Max_Speed_Steering, +0x5C4, (v)
    Always<float> aiMaxSpeedSteeringRaw;                     // AI_Max_Speed_Steering, +0x5C8 - "not converted, observed" (spec 7.3): raw mph text, NOT run through kMphToMs. See the dedicated synthetic test.
    Always<float> steeringWheelMaxSpeedRadians;                // Steering_Wheel_Max_Speed, +0x5CC, (r when present)
    Always<float> steeringWheelMaxReturnSpeedRadians;            // Steering_Wheel_Max_Return_Speed, +0x5D0, (r when present) [shorthand expansion]
    Always<float> steeringWheelDampAngleRadians;                   // Steering_Wheel_Damp_Angle, +0x5D4, (r when present) [shorthand expansion]
    Always<float> steeringWheelReturnDampAngleRadians;               // Steering_Wheel_Return_Damp_Angle, +0x5D8, (r when present) [shorthand expansion]

    // --- +0x5DC..+0x5F8: pedal-block / aero (spec 7.3) ---
    Always<float> minPedalToBlock;      // Min_Pedal_To_Block, +0x5DC
    Always<float> minTimeToBlock;        // Min_Time_To_Block, +0x5E0
    Always<float> aiMinTimeToBlock;       // AI_Min_Time_To_Block, +0x5E4
    // Air_Density..Extra_Gravity: XML source confirmed by real data: nested
    // inside an <Aerodynamics> wrapper (Min_Pedal_To_Block and the two
    // Time_To_Block fields above stay flat top-level, per the same data).
    Always<float> airDensity;              // Air_Density, +0x5E8
    Always<float> frontalArea;              // Frontal_Area, +0x5EC
    Always<float> dragCoefficient;           // Drag_Coefficient, +0x5F0
    Always<float> liftCoefficient;            // Lift_Coefficient, +0x5F4
    Always<float> extraGravity;                // Extra_Gravity, +0x5F8

    // --- Towing (spec 7.3 +0x5FC..+0x617) ---
    TowableData towableData;   // Towable_Data

    // --- Center of mass (spec 7.3 +0x61C..+0x624) ---
    Always<float> centerOfMassYOffset;   // Center_Of_Mass_Y_Offset, +0x61C
    Always<float> centerOfMassZOffset;    // Center_Of_Mass_Z_Offset, +0x620
    Always<float> cameraProximityLockY;    // Camera_Proximity_Lock_Y, +0x624

    // Entry +0x628..+0x72B (vehicle-camera parameters): NOT a field of this
    // struct - the region is populated from a SEPARATE xtbl file
    // (vehicle_cameras.xtbl / vehicle_group_cameras.xtbl), not from the same
    // row ParseVehicleEntry(row) parses everything else from. See
    // VehicleCameraRow/ParseVehicleCameraRow below and the DELIBERATELY OPEN
    // list above for the full explanation and what remains genuinely OPEN.

    // --- Rotation/friction (spec 7.3 +0x72C..+0x774) ---
    Always<float> staticLoadFriction;         // Static_Load_Friction, +0x72C
    Always<float> aiStaticLoadFriction;        // AI_Static_Load_Friction, +0x730
    Always<float> rollTorqueFactor;             // Roll_Torque_Factor, +0x734
    Always<float> pitchTorqueFactor;             // Pitch_Torque_Factor, +0x738
    Always<float> yawTorqueFactor;                 // Yaw_Torque_Factor, +0x73C
    Always<float> aiYawTorqueFactor;                // AI_Yaw_Torque_Factor, +0x740
    Always<float> extraSteeringTorque;               // Extra_Steering_Torque, +0x744
    Always<float> rollUnitInertia;                     // Roll_Unit_Inertia, +0x748
    Always<float> pitchUnitInertia;                     // Pitch_Unit_Inertia, +0x74C
    Always<float> yawUnitInertia;                        // Yaw_Unit_Inertia, +0x750
    Always<float> yawUnitInertiaMultiplier;                // Yaw_Unit_Inertia_Multiplier, +0x754
    Always<float> rollUnitInertiaMultiplier;                 // Roll_Unit_Inertia_Multiplier, +0x758 [shorthand expansion]
    Always<float> pitchUnitInertiaMultiplier;                  // Pitch_Unit_Inertia_Multiplier, +0x75C [shorthand expansion]
    Always<float> aiYawUnitInertia;                              // AI_Yaw_Unit_Inertia, +0x760
    Always<float> aiRollUnitInertia;                               // AI_Roll_Unit_Inertia, +0x764 [shorthand expansion]
    Always<float> aiPitchUnitInertia;                                // AI_Pitch_Unit_Inertia, +0x768 [shorthand expansion]
    Always<float> viscosityFriction;                                   // Viscosity_Friction, +0x76C
    Always<float> aiMaxBrakingDecel;                                     // AI_Max_Braking_Decel, +0x770
    Always<float> aiMaxRadialAccel;                                        // AI_Max_Radial_Accel, +0x774

    // --- Audio (spec 7.3 +0x778..+0x7A8) ---
    AudioHysteresisBlock audioHysteresis;   // Audio_Hysteresis

    // --- Foley (spec 7.3 +0x7AC..+0x7EC) ---
    FoleyBlock foley;   // Foley

    // --- Effect ids (spec 7.3 +0x7F0..+0x847) ---
    EffectIds effectIds;

    // --- Spin damping (spec 7.3 +0x848..+0x850). XML source confirmed by
    // real data: nested inside an <Angular_Damping> wrapper (only these
    // three fields - the OTHER "rotation/friction" fields above, e.g.
    // Roll_Torque_Factor, stay flat top-level per the same data) ---
    Always<float> normalSpinDamping;         // Normal_Spin_Damping, +0x848
    Always<float> collisionSpinDamping;       // Collision_Spin_Damping, +0x84C
    Always<float> collisionSpinThresholdRadians;  // Collision_Spin_Threshold, +0x850, (r) - INTERPRETATION: the (r) marker is read as applying only to this (angle-shaped) field, not the two damping coefficients before it (see header banner item 4-adjacent note)

    // --- Hitch (spec 7.3 +0x854..+0x85C) ---
    int32_t trailerHitchType = -1;   // Trailer_Hitch_Type; -1 absent
    int32_t tractorHitchType = -1;    // Tractor_Hitch_Type; -1 absent
    Always<float> trailerChance;        // Trailer_Chance, +0x85C

    // --- FOV / buoyancy (spec 7.3 +0x860..+0x86C) ---
    // camera_fov_scale (+0x860): no named XML element given (spec: "initialised
    // to 1.0 by the main reader, overridden by the camera helper") - OPEN,
    // not modeled (see the DELIBERATELY OPEN list above).
    Always<float> maxFov;                  // Max_FOV, +0x864
    Always<float> minFollowDistMultiplier;   // Min_Follow_Dist_Multiplier, +0x868
    Always<float> buoyancyModifier;            // Water_Physics/Buoyancy_Modifier, +0x86C

    // --- Flag words (spec 7.6) ---
    uint32_t flags0 = 0;   // entry +0x870 - see the flags0:: constants above
    uint32_t flags1 = 0;   // entry +0x874 - see the flags1:: constants above

    // --- Top speed (spec 7.3 +0x878/+0x87C) ---
    Always<float> maximumSpeedMs;               // Maximum_Speed, +0x878, (v), capped at 100 mph before conversion
    Always<float> maximumSpeedWithNitrousMs;      // Maximum_Speed_With_Nitrous, +0x87C, (v), capped at 100 mph, then raised to at least maximumSpeedMs if lower (applied here)

    // --- Hardtop / roof (spec 7.3 +0x880..+0x89B) ---
    // "VID" = Vehicle ID (a plain u32 list per spec 7.3, not name hashes -
    // contrast with NoCustVariantsGrid below, which the spec explicitly
    // calls out as hashes).
    std::vector<uint32_t> hardtopVids;          // Hardtop_VIDs/Element, capacity 4
    std::vector<uint32_t> roofObstructionVids;   // RoofObstruction_VIDs/Element, capacity 4

    // --- Climb / seats (spec 7.3 +0x8A8..+0x8B4) ---
    Vec3Result climbToEntryOffset;    // Climb_To_Entry_Offset, +0x8A8
    uint8_t prohibitedGunfireSeats = 0;  // ProhibitedGunfireSeats/Flag bitmask, +0x8B4 (8 named seats)

    // --- Customisation (spec 7.3 +0x8B8..+0x8FC) ---
    // Real data confirms the item element is <NoCustVariantElement> with the
    // name in a <Variant> child, not a flat <Element>NAME</Element>.
    std::vector<uint32_t> noCustVariantsGrid;   // NoCustVariantsGrid/NoCustVariantElement/Variant (name-text hashes), capacity 16
    uint32_t custCameraNameHash = 0;              // Cust_Camera_Name, +0x8FC

    // --- Wheel size range (spec 7.3/7.4 +0x900..+0x907) ---
    WheelSizeRange wheelSizeRange;   // Wheel_Size_Element

    std::optional<int32_t> racingSelectability;   // Racing_Selectability, +0x908; documented default -1, NOT auto-applied

    // --- Suspension/Perf (spec 7.3 +0x9AC..+0x9B4) ---
    Always<float> suspensionRaiseMax;    // Suspension_Raise_Max, +0x9AC
    Always<float> suspensionLowerMin;     // Suspension_Lower_Min, +0x9B0
    Always<float> perfTorqueMultiplier;    // Perf_Torque_Multiplier, +0x9B4

    // --- Seats (spec 7.3 +0x9B8..+0xA37) ---
    std::vector<SeatSpecificData> seatSpecificData;   // Seat_Specific_Data/"Seat" (INTERPRETED item name), capacity 8

    // --- Interaction / animation references (spec 7.3 +0xA38..+0xACC) ---
    NamedHashedText vehicleInteractionInfo;   // Vehicle_Interaction_Info, +0xA38 (+0xA7C's "derived id" not modeled - see OPEN list)
    NamedHashedText characterAnimationSet;      // Character_Animation_Set, +0xA80
    std::optional<std::string> vehicleAnimationSetName;   // Vehicle_Animation_Set_Name, +0xAC4; real entry strips a trailing ".xtbl" - applied here
    std::optional<std::string> vehicleAnimationRigName;     // Vehicle_Animation_Rig_Name, +0xAC8; real entry strips a trailing ".rigx" - applied here
    uint32_t defaultAnimationStateHash = 0;                    // Default_Animation_State, +0xACC

    // Entry +0xAD0..+0xAEF (Weapon_Link): OPEN, not modeled - see the
    // DELIBERATELY OPEN list above.

    // --- Upgrade table (spec 7.3 +0xAF8..+0xB77) ---
    UpgradeTable upgrades;

    // --- Tail (spec 7.3 +0xB78) ---
    Always<float> preventBottomingOffset;   // Prevent_Bottoming_Offset, +0xB78
};

// Parses one `<Vehicle>` (or `<Vehicle_Group>`, spec section 7.2 "same
// reader" fact) element into a VehicleEntry. `row` must be that element
// (case-insensitive), directly under `<root><Table>` for a real `.xtbl`.
VehicleEntry ParseVehicleEntry(const Node* row);

// Convenience: parses every `<Vehicle>` row of a whole `<name>_veh.xtbl`
// document, in file order.
std::vector<VehicleEntry> ParseAllVehicles(const Document& doc);

// Convenience: parses every `<Vehicle_Group>` row of a whole
// vehicle_groups.xtbl document, in file order (spec 7.2's "defaults mode" -
// see the SCOPE banner above for what this reader does and does not
// reproduce about that mode).
std::vector<VehicleEntry> ParseAllVehicleGroups(const Document& doc);

// ---------------------------------------------------------------------------
// Vehicle camera parameters: `vehicle_cameras.xtbl` / `vehicle_group_cameras.
// xtbl` (spec-vehicle-data.md 7.3's `+0x628`-`+0x72B` field table, added
// 2026-10-02, itself sourced from and partly spot-checked against
// spec-tables-ui-controls.md section 8.1 "Family A").
//
// DISCOVERY (found while implementing this, not anticipated going in):
// despite describing bytes `+0x628..+0x72B` of the SAME 0xB80-byte
// vehicle-info-table entry VehicleEntry/ParseVehicleEntry already covers
// most of, this region is NOT populated from the `<name>_veh.xtbl`/
// `vehicle_groups.xtbl` `<Vehicle>`/`<Vehicle_Group>` row ParseVehicleEntry
// receives. Per spec-tables-ui-controls.md 8.1 directly: it is filled by a
// SEPARATE load pass over `vehicle_cameras.xtbl` (vehicle mode) /
// `vehicle_group_cameras.xtbl` (group mode). Those files' OWN `<Vehicle>`/
// `<Vehicle_Group>` rows are resolved by NAME against the already-populated
// vehicle-info table (`0x00ACB000`/`0x00ACE2D0` resolve `Name` via
// `0x00AC27A0` to a 16-bit row index, `0xFFFF` = unknown -> row skipped,
// then call the shared `0x00ACA960`/`0x00AC5CF0` helpers to write into that
// row). A `<name>_veh.xtbl` `<Vehicle>` element has no `Lookat_Offset`/
// `Camera_Follow_Aggression` children at all - reading for them on the SAME
// row ParseVehicleEntry already has would silently read "absent" for every
// vehicle, which is not a faithful reimplementation. Consequently these
// types/this reader are kept SEPARATE from VehicleEntry/ParseVehicleEntry,
// mirroring the real engine's own two-pass load; VehicleEntry itself gains
// NO new field for this region (same footing as entry +0x480's group-row
// POINTER and Weapon_Link: a full merge needs the caller to join two loaded
// documents by Name, out of scope for a single-row parser).
//
// Only the `+0x628..+0x72B` fields this task's spec update confirms are
// modeled below. Also written by this same file per spec-tables-ui-
// controls.md 8.1, but OUTSIDE this task's `+0x628..+0x72B` scope and so
// NOT modeled here: `+0x624`'s `Camera_Proximity_Lock_Y` override,
// `+0xAF0`/`+0xAF4`'s `Camera_Roll`, `+0x860`/`+0x864`/`+0x868`'s FOV/follow
// fields, and `skip_camera_transition`/`use_alt_freckle_cam`.

// One `0x00AC5CF0`-shaped 32-byte camera-angle set: Primary at destination
// entry `+0x628` (selector 0), Secondary `+0x648` (selector 1, +0x20), RC
// `+0x668` (selector 2, +0x40). The set's final 4 bytes ("+0x1C" within the
// set) are spec-documented as "copied only" with no named XML element read
// into them by `0x00AC5CF0` - nothing is modeled for that sub-field (same
// convention as every other element-less field this reader leaves out).
struct CameraAngleSet {
    Vec3Result lookatOffset;          // Lookat_Offset, set+0x00 (3xf32)
    Always<float> followDistance;      // Follow_Distance, set+0x0C
    Always<float> followHeight;         // Follow_Height, set+0x10
    Always<float> minPitchRadians;       // Min_Pitch, set+0x14, (r) when present
    Always<float> maxPitchRadians;        // Max_Pitch, set+0x18, (r) when present
};

// `Camera_Follow_Aggression > Position_Based` (spec-tables-ui-controls.md
// 8.1, +0x6EC/+0x6FC/+0x6F0/+0x6F4/+0x70C/+0x710). Mutually exclusive with
// Sphere_Based below - `Position_Based` wins when both exist (spec text).
// Swing_Rate_Fwd/Rev and From_Sticky_Fwd/Rev are documented as going through
// a further DETERMINISTIC (not RNG - 8.1 text corrects an earlier RNG
// reading) `1 - exp(K / value)` transform downstream of this reader, where K
// is a double constant the spec itself has not yet read
// (`0x01184050`, NEEDS-EXE) - that transform is NOT applied here (this
// reader's house convention: do not reproduce a transform this reader could
// not even complete correctly, same footing as the Helicopter rotor-ring
// derivation being left to vehicle_type.h's "not reproduced" note). The raw
// authored XML values are kept as read.
struct PositionBasedAggression {
    Always<float> swingRateFwd;           // Swing_Rate_Fwd, +0x6EC
    Always<float> swingRateRev;            // Swing_Rate_Rev, +0x6FC
    Always<float> headingRetentionNormal;   // Heading_Retention_Normal, +0x6F0; documented default 1.0, NOT auto-applied (matches Player_Damage_Multiplier precedent)
    Always<float> headingRetentionTurning;   // Heading_Retention_Turning, +0x6F4; documented default 1.0, NOT auto-applied
    Always<float> fromStickyFwd;              // From_Sticky_Fwd, +0x70C
    Always<float> fromStickyRev;               // From_Sticky_Rev, +0x710
};

// `Camera_Follow_Aggression > Sphere_Based` (+0x700/+0x704/+0x708).
struct SphereBasedAggression {
    Always<float> slerpValue;              // Slerp_Value, +0x700
    Always<float> fromStickySlerpValue;     // From_Sticky_Slerp_Value, +0x704; documented default = Slerp_Value, NOT computed here (matches AI_Wheel_Friction precedent)
    Always<float> minAngleRadians;           // Min_Angle, +0x708, (r) when present; presence also sets destination-entry +0x870 bit 0x80000000 in the real engine - NOT applied here (this reader has no +0x870 flag word of its own; see flags0's bit-31 comment in this header)
};

// `Camera_Follow_Aggression` (+0x6EC..+0x710): a child element of a
// vehicle_cameras.xtbl/vehicle_group_cameras.xtbl row (NOT of the
// `<name>_veh.xtbl` row - see the SCOPE note above). `present` tracks
// whether the wrapper itself exists (same convention as AirControlBlock /
// MotorcycleVariant::Wheelie's own `present` flag, vehicle_type.h). Both
// Position_Based and Sphere_Based are read unconditionally when the wrapper
// is present (each sub-block's own fields already read as absent/default
// when ITS OWN wrapper is additionally missing) - which one the real engine
// prefers when both are supplied is documented ("Position_Based wins") but
// not resolved here, left to the caller, matching this reader's precedent
// of keeping mutually-exclusive raw inputs rather than resolving them (same
// footing as ExhaustTypes' keyed list).
//
// NOT modeled here: the destination `+0x870` bit `0x40000000` mode selector
// and its group-row inheritance path (needs the whole loaded group table
// PLUS this camera file, doubly out of scope for a single-row parser - same
// footing as the +0x480 group-row-pointer precedent), and the four still-
// untabulated sub-readers that run after the three angle sets
// (`0x00AC6020`, `0x00AC6210`, `0x00AC6120`, `0x00AC9910`).
struct CameraFollowAggression {
    bool present = false;
    PositionBasedAggression positionBased;    // Position_Based
    SphereBasedAggression sphereBased;         // Sphere_Based
};

// One `vehicle_cameras.xtbl` `<Vehicle>` row (vehicle mode) or
// `vehicle_group_cameras.xtbl` `<Vehicle_Group>` row (group mode) - the SAME
// shape serves both, mirroring VehicleEntry/ParseVehicleEntry's own
// "same reader, two root element kinds" precedent (spec 7.2). `name` is kept
// as raw text, NOT resolved to the vehicle-info-table row index the real
// `0x00AC27A0` lookup would compute (same footing as VehicleEntry::group -
// resolving it needs the whole loaded vehicle-info table, out of scope for
// a single-row parser). `primary`/`secondary`/`rc` are read from XML wrapper
// elements named `Primary_Camera_Angle`/`Secondary_Camera_Angle`/
// `RC_Camera_Angle` per spec-tables-ui-controls.md 8.1's own field table.
// CONFIRMED against real data (clean-room legitimate - real game data, not
// TEAM A\tools or disassembly): `misc_tables.vpp_pc`'s own
// `vehicle_cameras.xtbl` carries BOTH real `<Vehicle>` rows using exactly
// this nesting (`Primary_Camera_Angle`/`Secondary_Camera_Angle`/
// `RC_Camera_Angle` > `Lookat_Offset`/`Follow_Distance`/`Follow_Height`,
// `Camera_Follow_Aggression` > `Position_Based`/`Sphere_Based` with every
// field name used below appearing verbatim) AND the file's own embedded
// `<TableDescription>` schema block (every real `.xtbl` carries one, an
// authoring-tool field catalog, not disassembly) naming every field here
// identically, resolving 8.1's own "NEEDS-DATA: element-vs-attribute check
// in the real file" note. That same schema block also names the four
// sub-readers 8.1 could not identify from disassembly alone, most likely:
// `Fine_Aim_Camera_Angle` (same Lookat_Offset shape as the three angle sets,
// seen between Secondary_Camera_Angle and Camera_Follow_Aggression in real
// rows), `Camera_Swings` (`Powerslide`/`Nitrous`/`Burnout`, each an
// offset-vec3/slerp/Duration/Roll/flags record), `Camera_Sticky`
// (`Default_Time`/`Stopped_Time`/`Speed_Threshhold`), and
// `Helicopter_Specific`. This is a real, useful finding (reported, not
// guessed) but NOT implemented: the schema block gives element names and
// authoring-tool defaults only, never the runtime byte offsets spec-
// vehicle-data.md 7.3/spec-tables-ui-controls.md 8.1 still mark NEEDS-EXE
// for `0x00AC6020`/`0x00AC6210`/`0x00AC6120`/`0x00AC9910` - implementing a
// destination offset from this alone would be inventing an engine value,
// against this project's hard rule. Left genuinely OPEN, as instructed.
//
// Also surfaced by this same real-data check, worth flagging rather than
// silently resolving: the real file's own schema states
// `Heading_Retention_Normal`/`Heading_Retention_Turning`'s authoring-tool
// `<Default>` as `0.0`, not the `1.0` spec-tables-ui-controls.md 8.1's prose
// states - these may be two different concepts (an XML-authoring-tool
// fallback vs. the compiled reader's own absent-value default) rather than
// a real contradiction, and this reader does not auto-apply either number
// for these two fields regardless (see PositionBasedAggression above), so
// no code depends on which is right - flagged here for the spec's own
// maintainers to reconcile, not resolved by guessing.
struct VehicleCameraRow {
    std::optional<std::string> name;          // Name (join key - see the note above)
    CameraAngleSet primary;                    // Primary_Camera_Angle, selector 0, destination +0x628
    CameraAngleSet secondary;                   // Secondary_Camera_Angle, selector 1, destination +0x648
    CameraAngleSet rc;                           // RC_Camera_Angle, selector 2, destination +0x668
    CameraFollowAggression cameraFollowAggression;  // Camera_Follow_Aggression, destination +0x6EC..+0x710
};

// Parses one `<Vehicle>` (vehicle_cameras.xtbl) or `<Vehicle_Group>`
// (vehicle_group_cameras.xtbl) row into a VehicleCameraRow. `row` must be
// that element (case-insensitive), directly under `<root><Table>`.
VehicleCameraRow ParseVehicleCameraRow(const Node* row);

// Convenience: parses every `<Vehicle>` row of a whole vehicle_cameras.xtbl
// document, in file order.
std::vector<VehicleCameraRow> ParseAllVehicleCameraRows(const Document& doc);

// Convenience: parses every `<Vehicle_Group>` row of a whole
// vehicle_group_cameras.xtbl document, in file order.
std::vector<VehicleCameraRow> ParseAllVehicleGroupCameraRows(const Document& doc);

}  // namespace sr3vehicleinfo
