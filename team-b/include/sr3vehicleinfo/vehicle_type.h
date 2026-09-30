#pragma once

// sr3vehicleinfo - Vehicle_Type variant sub-structs (spec-vehicle-data.md
// section 7.5: "the class enum and the variant union").
//
// Split out of vehicle_entry.h because the six class-specific blocks are
// large enough to make one header unwieldy. See vehicle_entry.h for the
// overall conventions (Always<T> vs optional<T>, unit conversions, what is
// deliberately left OPEN) - they apply here too.
//
// Every struct below is READ-ONLY schema: it mirrors the entry-relative
// byte layout spec section 7.5 tabulates for one Vehicle_Type variant, nothing
// else. Fields the spec marks OPEN/untabulated are omitted, not guessed -
// see the per-struct comments and the master OPEN list in vehicle_entry.h.

#include <cstdint>
#include <optional>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3vehicleinfo {

using sr3xtbl::Always;
using sr3xtbl::Node;

// ---------------------------------------------------------------------------
// Shared sub-blocks
// ---------------------------------------------------------------------------

// `Air_Control` (spec 7.5: Automobile +0x58, Motorcycle +0x68 - same shape).
// The block's own +0x00 "flag byte" has NO named XML source anywhere in the
// spec text (only "(flag byte, Max_Roll_Speed ...)" - the byte itself is
// unlabelled). This reader's INTERPRETATION (not a spec-confirmed fact):
// `present` mirrors whether an <Air_Control> element exists at all. Flagged
// here so a caller can see this is a judgement call, not a cited fact.
struct AirControlBlock {
    bool present = false;                  // INTERPRETED: <Air_Control> element presence (see banner above)
    Always<float> maxRollSpeedRadians;      // Max_Roll_Speed, +0x5C / +0x6C, (r)
    Always<float> maxRollAccelRadians;      // Max_Roll_Accel, +0x60 / +0x70, (r)
    Always<float> maxPitchSpeedRadians;     // Max_Pitch_Speed, +0x64 / +0x74, (r)
    Always<float> maxPitchAccelRadians;     // Max_Pitch_Accel, +0x68 / +0x78, (r)
};

// One `Surfaces > Surface` record (spec 7.5 "Surface record", 0x28 bytes),
// shared verbatim by Airplane and Helicopter (and therefore Vtol, which
// reuses both). "Type" tokens are exhaustive in the real reader (an
// unrecognised Type fails the whole vehicle load there); this reader does
// not throw - `type` is -1 for absent/unrecognised so a validation harness
// can gate on it instead.
struct SurfaceRecord {
    int32_t type = -1;                // Type: Wing 0, Aeleron 1 (sic, spec's own spelling), Elevator 2, Rudder 3; -1 if absent/unrecognised
    Always<float> xOffset;            // X_Offset, +0x04
    Always<float> yOffset;            // Y_Offset, +0x08
    Always<float> zOffset;            // Z_Offset, +0x0C
    // +0x10..+0x18: the real entry stores a DERIVED direction vector computed
    // from Primary_Angle/Secondary_Angle - not reproduced here (same
    // "raw inputs, not the derived geometry" convention sr3tables_weapons
    // uses for Fire_Cone_Metrics' ring points). The raw angle inputs are
    // kept instead; a caller needing the direction vector must compute it.
    Always<float> primaryAngleDegrees;    // Primary_Angle (raw degrees, NOT converted - the spec marks only Max_Deflect (r))
    Always<float> secondaryAngleDegrees;  // Secondary_Angle (raw degrees, NOT converted)
    Always<float> liftFactor;             // Lift_Factor, +0x1C
    Always<float> maxDeflectRadians;      // Max_Deflect, +0x20, (r)
    std::optional<bool> invertInput;      // Invert_Input, +0x24 (u8)
};

// One engine offset triple of an Airplane's `Engines` list (spec 7.5:
// "up to four X/Y/Z_Offset triples, each 0x0C"). Element names X_Offset/
// Y_Offset/Z_Offset are literal (spec's own shorthand); the wrapper/item
// element names ("Engines" / one item per engine) are NOT given verbatim by
// the spec (no shipped Airplane sample exists to check against, per 7.9)-
// this reader's choice of "Engines" (plural wrapper) / "Engine" (item) is an
// INTERPRETATION, following the `<Plural> > <Singular>` convention the spec
// itself confirms elsewhere (Axles > Axle, section 7.3; Surfaces > Surface,
// section 7.5).
struct EngineOffset {
    Always<float> xOffset;  // X_Offset
    Always<float> yOffset;  // Y_Offset
    Always<float> zOffset;  // Z_Offset
};

// `Pilot_Assist_Params` (spec 7.5: ten f32 at Airplane block +0x18C). The
// underscore-continuation field names ("_Accel +0x10, _Decel +0x14" etc.)
// are expanded here per the spec's own established shorthand convention
// (seen throughout section 7.3, e.g. the Steering_Wheel_* run) - each
// continues the immediately preceding field's prefix.
struct PilotAssistParams {
    Always<float> rollMaxAngularSpeedRadians;        // Roll_Max_Angular_Speed, +0x00, (r)
    Always<float> rollDampAngleRadians;               // Roll_Damp_Angle, +0x04, (r)
    Always<float> barrelRollMaxAngularSpeedRadians;    // Barrel_Roll_Max_Angular_Speed, +0x08, (r)
    Always<float> horizontalMaxAngularSpeedRadians;     // Horizontal_Max_Angular_Speed, +0x0C, (r)
    Always<float> horizontalMaxAngularAccelRadians;      // Horizontal_Max_Angular_Accel, +0x10, (r) [shorthand expansion]
    Always<float> horizontalMaxAngularDecelRadians;       // Horizontal_Max_Angular_Decel, +0x14, (r) [shorthand expansion]
    Always<float> verticalMaxAngularSpeedRadians;          // Vertical_Max_Angular_Speed, +0x18, (r)
    Always<float> verticalMaxAngularAccelRadians;           // Vertical_Max_Angular_Accel, +0x1C, (r) [shorthand expansion]
    Always<float> liftMinSpeedMs;                             // Lift_Min_Speed, +0x20, (v) mph->m/s
    Always<float> turnLinearAccel;                             // Turn_Linear_Accel, +0x24, NOT converted (neither (r) nor (v))
};

// `Engine_Params` (spec 7.5, part of the Airplane block).
struct AirplaneEngineParams {
    Always<float> engineAccel;             // Engine_Accel, +0x34
    Always<float> engineReverseAccel;       // Engine_Reverse_Accel, +0x38
    Always<float> engineRampUpTime;          // Engine_Ramp_Up_Time, +0x3C
    Always<float> engineRampDownTime;         // Engine_Ramp_Down_Time, +0x40
};

// ---------------------------------------------------------------------------
// 0. Automobile (spec 7.5 table row 0)
// ---------------------------------------------------------------------------
struct AutomobileVariant {
    // Hydraulics_Capable and Drifting_Possible land in entry flag word
    // +0x870 (bits 3 and 6, see vehicle_entry.h's flags0 constants) even
    // though they are read from WITHIN this variant's XML block - so they
    // are computed by ParseVehicleEntry's flag pass, not stored here, to
    // avoid two copies of the same bit disagreeing.
    Always<float> driftingYawTorqueFactor;          // Drifting_Yaw_Torque_Factor, +0x30
    Always<float> driftingForwardForce;              // Drifting_Forward_Force, +0x34
    Always<float> driftingForwardForceSpeedCapMs;     // _Forward_Force_Speed_Cap, +0x38, (v) [shorthand expansion of Drifting_Forward_Force_Speed_Cap]
    Always<float> driftingBoostStrength;               // Drifting_Boost_Strength, +0x3C
    Always<float> peeloutFrictionModifier;              // Peelout_Friction_Modifier, +0x40
    Always<float> peeloutIdealAcceleratorInput;          // Peelout_Ideal_Accelerator_Input, +0x44
    Always<float> peeloutEndSpeedMs;                      // Peelout_End_Speed, +0x48, (v)
    Always<float> crushHeight;                             // Crush_Height, +0x4C
    Always<float> crushFactor;                              // Crush_Factor, +0x50

    // AutomobileFlags -> u8 at +0x54, exactly six accepted Flag strings.
    uint8_t automobileFlags = 0;

    AirControlBlock airControl;  // Air_Control, +0x58
};

inline constexpr std::string_view kAutomobileFlagNames[6] = {
    "Has Security Alarm", "Has Beater Backfire", "Has Exotic Backfire",
    "Always Open Topped", "Is Tank", "No Treads",
};

// ---------------------------------------------------------------------------
// 1. Motorcycle (spec 7.5 table row 1, "closes section 6 item 3")
// ---------------------------------------------------------------------------
struct LeaningBlock {
    Always<float> maxLeanAngleRadians;     // Max_Lean_Angle, +0x30, (r)
    Always<float> maxLeanSpeedRadians;      // Max_Lean_Speed, +0x34, (r)
    Always<float> maxReturnSpeedRadians;     // Max_Return_Speed, +0x38, (r)
    Always<float> leanDampAngleRadians;       // Lean_Damp_Angle, +0x3C, (r)
    Always<float> returnDampAngleRadians;      // Return_Damp_Angle, +0x40, (r)
};

// `Wheelie` block. Same "+0x00 flag byte has no named XML source" caveat as
// AirControlBlock - `present` is this reader's element-presence stand-in.
struct WheelieBlock {
    bool present = false;                  // INTERPRETED: <Wheelie> element presence
    Always<float> balanceAngleRadians;      // Balance_Angle, +0x50, (r)
    Always<float> balanceRange;              // Balance_Range, +0x54
    // Range_Decay is "stored as a rounded integer" (spec 7.5) - this reader
    // parses the text as the engine float grammar, then rounds to the
    // nearest int32, matching that documented storage rule exactly.
    Always<int32_t> rangeDecay;               // Range_Decay, +0x58 (rounded)
    Always<float> maxWheelieSpeedRadians;      // Max_Wheelie_Speed, +0x5C, (r)
    Always<float> wheelieDampAngleRadians;      // Wheelie_Damp_Angle, +0x60, (r)
    Always<float> unbalancingAccelRadians;       // Unbalancing_Accel, +0x64, (r)
};

struct MotorcycleVariant {
    LeaningBlock leaning;                    // Leaning, +0x30
    Always<float> maxSteeringAngleRadians;    // Max_Steering_Angle, +0x44, (r)
    Always<float> turnSpeedMultiplier;         // Turn_Speed_Multiplier, +0x48
    WheelieBlock wheelie;                       // Wheelie, +0x4C
    AirControlBlock airControl;                  // Air_Control, +0x68

    // Drifting_Possible -> flags +0x870 bit 6 (computed by ParseVehicleEntry,
    // not stored here - see AutomobileVariant's note).
    Always<float> driftingYawTorqueFactor;          // Drifting_Yaw_Torque_Factor, +0x7C
    Always<float> driftingForwardForce;              // Drifting_Forward_Force, +0x80
    Always<float> driftingForwardForceSpeedCapMs;     // _Speed_Cap, +0x84, (v) [shorthand expansion]
    Always<float> driftingBoostStrength;               // Drifting_Boost_Strength, +0x88
    Always<float> peeloutFrictionModifier;              // Peelout_Friction_Modifier, +0x8C
    Always<float> peeloutIdealAcceleratorInput;          // Peelout_Ideal_Accelerator_Input, +0x90
    Always<float> peeloutEndSpeedMs;                      // Peelout_End_Speed, +0x94, (v)
};

// ---------------------------------------------------------------------------
// 2. Airplane (spec 7.5 table row 2)
// ---------------------------------------------------------------------------
struct AirplaneVariant {
    std::vector<EngineOffset> engines;       // Engines (capacity 4), blk+0x00, count blk+0x30
    AirplaneEngineParams engineParams;        // Engine_Params, blk+0x34..+0x40
    Always<float> reverseSpeedMs;              // Reverse_Speed, +0x44, (v)
    std::vector<SurfaceRecord> surfaces;        // Surfaces (capacity 8), +0x48, count +0x188
    PilotAssistParams pilotAssistParams;         // Pilot_Assist_Params, +0x18C
};

// ---------------------------------------------------------------------------
// 3. Helicopter (spec 7.5 table row 3)
// ---------------------------------------------------------------------------
struct HelicopterVariant {
    Always<float> elevationSpeedMs;         // Elevation_Speed, +0x30, (v)
    Always<float> elevationAccel;            // Elevation_Accel, +0x34
    Always<float> elevationDamp;              // Elevation_Damp, +0x38

    // +0x3C..+0xD7: twelve DERIVED rotor-ring points (vec3 each), generated
    // from Rotor_Center / Rotor_Radius / Rotor_Tilt - consumed-but-not-
    // stored-verbatim per spec section 7.4 item C. Not reproduced here (the
    // derivation formula for the twelve points is not given); the three raw
    // inputs are kept instead.
    sr3xtbl::Vec3Result rotorCenter;         // Rotor_Center (X/Y/Z children)
    Always<float> rotorRadius;                // Rotor_Radius
    Always<float> rotorTilt;                   // Rotor_Tilt

    Always<float> steeringAccel;             // Steering_Accel, +0xD8
    Always<float> pitchBias;                  // Pitch_Bias, +0xDC
    sr3xtbl::Vec3Result tailRotorCenter;       // Tail_Rotor_Center, +0xE0 (vec3)
    Always<float> tailAccel;                    // Tail_Accel, +0xEC
    Always<float> autolevelAccelRadians;         // Autolevel_Accel, +0xF0, (r)
    Always<float> aiAutolevelAccelRadians;        // AI_Autolevel_Accel, +0xF4, (r); documented default = Autolevel_Accel, NOT computed here

    std::vector<SurfaceRecord> surfaces;    // Surfaces (capacity 8), +0xF8, count +0x238

    Always<float> extraSpeedAccel;             // Extra_Speed_Accel, +0x23C
    Always<float> aiExtraSpeedAccel;            // AI_Extra_Speed_Accel, +0x240; documented default = Extra_Speed_Accel, NOT computed here
    Always<float> aiChaseExtraSpeedAccel;        // AI_Chase_Extra_Speed_Accel, +0x244
    Always<float> tiltLiftLoss;                   // Tilt_Lift_Loss, +0x248
    Always<float> dropOffHeight;                   // Drop_Off_Height, +0x24C; documented default -1, NOT auto-applied here

    // Drop_off_copilot -> flags +0x874 bit 17, Is_Attack_Aircraft -> +0x874
    // bit 6 (computed by ParseVehicleEntry, not stored here).
};

// ---------------------------------------------------------------------------
// 4. Vtol (spec 7.5 table row 4): an Airplane block (from Airplane_Mode) AND
// a Helicopter block (from Helicopter_Mode) as siblings, reusing both
// variant structs above verbatim.
// ---------------------------------------------------------------------------
struct VtolVariant {
    AirplaneVariant airplaneMode;      // Airplane_Mode, entry +0x30 (reuses AirplaneVariant)
    HelicopterVariant helicopterMode;   // Helicopter_Mode, entry +0x1E4 (reuses HelicopterVariant)
    // entry +0x410: an 8-dword camera-override block (Camera_Overrides) -
    // OPEN, "untabulated" per spec section 7.5 / open item 2. NOT modeled.
};

// ---------------------------------------------------------------------------
// 5. Watercraft (spec 7.5 table row 5) - ONLY the tabulated LBS_* fields are
// implemented, per the spec's own explicit statement that the other child
// element names are "not tabulated (OPEN)". No shipped sample exists
// either (base game blocked until recently, no DLC watercraft shipped).
// ---------------------------------------------------------------------------
struct WatercraftVariant {
    // +0x30 vec3 (reader-local X/Y/Z children), +0x3C..+0x54 and +0x6C..
    // +0x80 further f32s: child element names NOT given by the spec - OPEN,
    // not implemented (see banner above and vehicle_entry.h's OPEN list).
    // Real base-game data DOES reveal plausible names for this OPEN region
    // (Engine_Thrust_Position, Maximum_Forward/Reverse_Acceleration,
    // Engine_Thrust_Pitch_Angle, Audio_T_Derivative_Max/Damp_Distance,
    // Engine_Thrust_Ramp_Up/Down_Rate, Steering_Input_Rate,
    // Steering_While_Coasting, Spawn_Height_Offset - see
    // tools/validation/validate_vehicleinfo_population.cpp's G5 output) but
    // this reader does NOT implement them, per the task's explicit
    // instruction to only implement the tabulated LBS_* fields; reported to
    // the orchestrator instead.
    //
    // The LBS_* fields below ARE tabulated - and real data (see
    // src/vehicle_entry.cpp's file banner) confirms they sit inside their
    // own <Lean_Based_Steering> wrapper, not directly under <Watercraft>.
    Always<float> lbsMinRadius;               // Lean_Based_Steering/LBS_Min_Radius, +0x88
    Always<float> lbsMaxLeanAngleRadians;       // Lean_Based_Steering/LBS_Max_Lean_Angle, +0x8C, (r)
    Always<float> lbsCorrectionAccelRadians;     // Lean_Based_Steering/LBS_Correction_Acceleration, +0x90, (r)
    Always<float> lbsTurnRadiusModifier;          // Lean_Based_Steering/LBS_Turn_Radius_Modifier, +0x94
};

// Tagged union of the six Vehicle_Type variants (spec 7.5: class enum at
// entry +0x2C selects one of these; the reader tests them in this exact
// order, first child present wins).
enum class VehicleClass : int32_t {
    Automobile = 0,
    Motorcycle = 1,
    Airplane = 2,
    Helicopter = 3,
    Vtol = 4,
    Watercraft = 5,
    Unknown = -1,  // no recognised Vehicle_Type child present (the real loader fails the vehicle; this reader just leaves the union empty)
};

struct VehicleTypeUnion {
    VehicleClass vehicleClass = VehicleClass::Unknown;
    AutomobileVariant automobile;
    MotorcycleVariant motorcycle;
    AirplaneVariant airplane;
    HelicopterVariant helicopter;
    VtolVariant vtol;
    WatercraftVariant watercraft;
    // Only the member matching `vehicleClass` was actually populated from
    // XML; the others are left default-constructed. Kept as plain members
    // (not std::variant) to match this project's house style of exposing
    // every sub-block by name (see sr3tables_weapons::Weapon).
};

}  // namespace sr3vehicleinfo
