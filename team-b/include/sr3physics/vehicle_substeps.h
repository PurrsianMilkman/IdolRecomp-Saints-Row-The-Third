#pragma once

// sr3physics/vehicle_substeps.h - a deterministic, headless, "behind the
// boundary" model of the game's 13 named Havok vehicle sub-steps.
//
// Source: spec-physics-runtime.md Sec6 ("Driving slice, unit D-U0 - the 13
// named Havok vehicle sub-steps' own bodies"), SPEC TEAM (cleanroom
// reverse-engineering process) - the ONLY source for every ORDER/SHAPE claim
// tagged CONFIRMED/HIGH/HYPOTHESIS below. Vehicle TUNING field semantics
// (Mass, Max_Steering_Angle, the engine RPM-torque curve, Gear_Ratios,
// Diff_Gear_Ratio, per-axle Wheel_Friction/Wheel_Max_Friction/
// Wheel_Braking_Torque/Wheel_Engine_Torque, Drag_Coefficient/Frontal_Area/
// Air_Density) are spec-vehicle-data.md Sec4/Sec7, consumed here ONLY through
// this project's own existing sr3vehicleinfo::VehicleEntry reader (built on
// sr3xtbl) - see FromVehicleEntry() below. Nothing here was derived from the
// executable, decompiled code or pseudocode directly by this file.
//
// ---------------------------------------------------------------------------
// SCOPE DISCIPLINE - read this before trusting any number below as "real".
// ---------------------------------------------------------------------------
// Sec6 confirms the 13 sub-steps' own SHAPE and CALL ORDER (down to the
// instruction level for 11 of the 13 - items 7/12 forward into a separate,
// explicitly out-of-scope unit D-U1, Sec6.6) but explicitly does NOT confirm:
//   * which of the vehicle instance's 7 component-pointer slots is which
//     named Havok component (engine/steering/transmission/brakes/suspension/
//     driver-input/raycast-wheel-collide) - Sec6.3's own HYPOTHESIS, left
//     OPEN at Sec6.8 item 1;
//   * any concrete tire-force law, suspension spring/damper law, or
//     force/torque-accumulator threshold - Sec6.4 reads every one of these
//     as HIGH/HYPOTHESIS *shapes* ("a force accumulator", "a magnitude-based
//     clamp/rescale", "a decay-toward-limit computation") with NO formula,
//     because the actual arithmetic lives inside Havok's own component
//     objects (Sec1's "Havok's own internals are not investigated further"
//     boundary) or inside D-U1 (out of scope per this unit's own brief).
// So: the 13 Step0N_* methods below reproduce the CONFIRMED call ORDER and
// the CONFIRMED/HIGH *role* of each sub-step (status relay, per-wheel array
// relay, two-float relay, richest per-wheel relay, two thin forwarders,
// contact-record finish, force/torque accumulate, torque clamp, two
// decay-toward-limit per-wheel passes, friction forward, final per-wheel
// clamp) - this is the "dispatch shell" spec Sec6.3 confirms exists. The
// actual PHYSICS inside each component call (the engine torque curve's
// interpolation shape, the linear-tire cornering-force law, the
// friction-circle clamp, the longitudinal/lateral load-transfer formula, the
// semi-implicit-Euler chassis integrator) is this file's own CHOSEN,
// explicitly-flagged stand-in for what Havok's own component objects and
// D-U1 actually compute - picked to be physically sane and ENTIRELY
// deterministic, never to match Havok bit-for-bit (no spec gives that
// formula, so claiming a match would be inventing one, against this
// project's own "no invented fixes" rule). Every CHOSEN formula/constant
// below is commented at its point of use.
//
// WIRED-IN UPDATE (2026-10-08, driving-slice wire-in task): several of those
// CHOSEN stand-ins above have since been replaced by real calls into
// sr3physics/vehicle_driving_slice.h's §10 component formulas, wherever a
// component's own CONFIRMED formula genuinely covers what the stand-in was
// for - see that header's own updated banner for the full list, and each
// Step0N_* method's own doc comment in physics_vehicle_substeps.cpp for the
// sub-step-by-sub-step account. What is NOT wired (the tire cornering-force
// law itself, the friction-circle clamp's own "which axis-coupling value"
// input, the longitudinal/lateral load-transfer formula, the chassis
// integrator, the engine torque CURVE's interpolation shape) remains this
// file's own CHOSEN stand-in, individually flagged at its own point of use -
// §10 either gives no formula for it either, or gives one for a Havok-
// internal field schema with no established correspondence to this file's
// own xtbl-sourced fields (see Step08's doc comment for that specific case).
//
// WIRED-IN UPDATE (2026-10-09, vehicle-handling wave-6 task, authority
// spec-physics-runtime.md Sec10.10/spec-vehicle-data.md Sec7.3 changelog,
// synced commit 4c146a9): wave 6 item 5's own vehicle-info-table-to-Havok
// construction-site trace newly confirms 5 real xtbl transmission fields
// this file previously left as CHOSEN placeholders or did not model at all -
// `gearUpshiftRpmThreshold` (Upshift_RPM, was CHOSEN-magnitude-only),
// `downshiftRpms` (Downshift_RPMs, the per-gear downshift table this file's
// own prior banner incorrectly said "has NO xtbl counterpart at all"),
// `reverseGearRatio` (Reverse_Gear_Ratio, not modelled before at all),
// `clutchDelaySec` (Clutch_Delay, mapped to GearShiftTuning::shiftDurationSec,
// was a flat CHOSEN constant), `maxRpmChangePerSec` (Maximum_RPM_Rate, mapped
// to ComputeTargetEngineRpm's own rate-limit parameter, was a flat CHOSEN
// 1.0e6 "effectively unbounded" literal) - plus the "Bonus" brakes finding,
// `minTimeToBlockSec`/`aiMinTimeToBlockSec` (Min_Time_To_Block/
// AI_Min_Time_To_Block), parsed for completeness but left OPEN (no stall-
// timer/lock-flag model exists in this harness to feed them into - see each
// field's own doc comment below for the individual CONFIRMED/HIGH/OPEN
// citation). Diff_Gear_Ratio/Gear_Ratios[] were ALREADY wired before this
// task (Sec10.10 only re-confirms the same field/destination, no change
// needed); the engine torque curve, steering and tire/wheel grip (the other
// 3 of wave 6's "5 target parameters") were likewise already wired and are
// untouched by this pass. See BuildGearShiftTuning() and
// Step08_ForceTorqueAccumulate() (physics_vehicle_substeps.cpp) for the
// consuming side.
//
// Component-slot map (spec-physics-runtime.md 10.11.0, FINAL, confirmed from the
// exe constructors; replaces this file's earlier guessed map, which was wrong for every
// slot but +0x20): +0x20 driver input, +0x24 steering, +0x28 engine, +0x2c
// transmission, +0x30 brakes, +0x34 suspension, +0x38 aerodynamics (Havok
// default), +0x3c wheel collide, +0x40 tyre marks (never built), +0x44
// velocity damper (Havok default). See VehicleComponentSlot below. This
// file's sub-step order is unrelated to slot order; engine torque is still
// computed inside the per-axle wheel-force steps (10/11).
//
// ---------------------------------------------------------------------------
// PLANAR SCOPE (CHOSEN, stated up front rather than per-field)
// ---------------------------------------------------------------------------
// This model simulates GROUND-PLANE (yaw/lateral/longitudinal) rigid-body
// dynamics only - no vertical chassis motion, no roll/pitch, no explicit
// wheel rotational inertia/slip-ratio state. Per-wheel normal load (Fz) is
// computed directly from a standard longitudinal+lateral load-transfer
// approximation (CHOSEN, a common simplified-vehicle-sim technique, not a
// Havok formula) rather than from simulated spring compression. This is
// sufficient, and only intended, to produce the two qualitative behaviours
// this unit's test asserts (straight-line driving stays straight; constant
// steering turns with consistent curvature) - it is not a claim of general
// vehicle-dynamics fidelity. Full suspension travel, roll/pitch and
// side-dependent vertical dynamics are out of scope.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "sr3physics/vehicle_driving_slice.h"
#include "sr3vehicleinfo/vehicle_entry.h"

namespace sr3physics {

// spec-physics-runtime.md 10.11.0 component-slot offsets on the vehicle instance.
enum class VehicleComponentSlot : int {
    DriverInput = 0x20,
    Steering = 0x24,
    Engine = 0x28,
    Transmission = 0x2c,
    Brakes = 0x30,
    Suspension = 0x34,
    Aerodynamics = 0x38,
    WheelCollide = 0x3c,
    TyreMarks = 0x40,  // never built
    VelocityDamper = 0x44,
};

// --- vehicle tuning -----------------------------------------------------

// Per-axle tuning (spec-vehicle-data.md Sec7.3 "Axles" - CONFIRMED field
// semantics; concrete numbers are CHOSEN unless sourced via
// VehicleTuning::FromVehicleEntry()).
struct AxleAccelTuning {
    double engineTorqueShare = 0.5;        // Wheel_Engine_Torque (axle's share of engine torque, spec: "renormalised... axles values sum to 1")
    double wheelFriction = 1.0;             // Wheel_Friction
    double wheelMaxFriction = 1.0;           // Wheel_Max_Friction (spec default = Wheel_Friction)
    double wheelBrakingTorqueNm = 900.0;      // Wheel_Braking_Torque
    bool steers = false;                       // Wheel_Does_Steer


};

// Vehicle-wide tuning. Fields marked (confirmed field) have their SEMANTICS
// confirmed by spec-vehicle-data.md (consumed only through
// sr3vehicleinfo::VehicleEntry, never read directly from any archive by this
// file); their concrete NUMBERS are whatever FromVehicleEntry() was given, or
// the CHOSEN defaults below if left default-constructed. Fields with no
// "(confirmed field)" note (wheelBase, trackWidth, cgHeight, yawInertia,
// wheelRadius, steeringRateRadPerSec, corneringStiffness) have NO xtbl
// element at this schema layer at all (wheelbase/track are geometry derived
// from .ccar_pc part transforms, spec-vehicle-geometry.md, not reproduced
// here; a tire cornering-stiffness constant is not a Havok value any spec
// gives) - CHOSEN outright, flagged individually below.
struct VehicleTuning {
    double mass = 1500.0;                  // Mass (confirmed field; units OPEN per spec-vehicle-data.md Sec6 item 2 - treated as kg, CHOSEN)
    double wheelBase = 2.6;                 // CHOSEN (m) - not in this schema layer, see banner
    double trackWidth = 1.6;                 // CHOSEN (m)
    double cgHeight = 0.5;                    // CHOSEN (m) - Center_Of_Mass_Z_Offset exists in the real schema but no formula ties it to a load-transfer law
    double yawInertia = 2200.0;                // CHOSEN (kg*m^2) - Yaw_Unit_Inertia exists in the real schema but no formula gives its engineering units
    double wheelRadius = 0.32;                  // CHOSEN (m)

    double maxSteeringAngleRad = 0.6108652;       // Max_Steering_Angle (confirmed field, deg->rad conversion already applied by VehicleEntry); CHOSEN concrete magnitude (~35 deg) when not sourced
    double steeringRateRadPerSec = 3.0;            // CHOSEN - feeds vehicle_driving_slice.h's SteeringRateLimiter as its "normal-turn" rate (Sec10.1 CONFIRMS this named role exists, not its magnitude)
    double steeringCenteringRateRadPerSec = 6.0;    // CHOSEN - the SAME limiter's "centring" rate (Sec10.1 CONFIRMS this second named role exists too); faster than the turn-in rate, a common self-centring-steering assumption, not a spec value
    double steeringSpeedThresholdMPerSec = 8.0;      // CHOSEN (m/s) - SteeringBaseTuning::speedThreshold (Sec10.1's own "+0xc, a per-vehicle threshold" field - CONFIRMED to exist, CHOSEN magnitude)
    bool steeringFastPathNoSpeedTerm = false;         // CHOSEN - always takes Sec10.1's normal (speed-sensitive, CONFIRMED-formula) path; the real +0x19c flag's trigger condition is OPEN (Sec10.7), so this harness never claims to know when the OTHER (flat-multiply, no-formula) path would apply

    double peakTorqueNm = 320.0;                    // Torque (confirmed field)
    double minRpm = 900.0;                           // Min_RPM (confirmed field)
    double optRpm = 4500.0;                           // Opt_RPM (confirmed field)
    double maxRpm = 6500.0;                            // Max_RPM (confirmed field)
    double minRpmTorqueFactor = 0.6;                    // Min_RPM_Torque_Factor (confirmed field) - consumed by this file's own retired ComputeEngineTorqueNm() placeholder ONLY; see ToEngineCurveTuning() below for where this field's real runtime ROLE (CROSSED, spec-physics-runtime.md Sec10.2) actually lands
    double maxRpmTorqueFactor = 0.5;                     // Max_RPM_Torque_Factor (confirmed field) - same note

    // The 3 further real xtbl Engine fields spec-physics-runtime.md Sec10.2's
    // "Construction site" finding (synced 2026-10-08, commit 4c7d3f2) pins
    // as REAL and CROSSED (authored name vs. runtime role backwards) -
    // parsed by FromVehicleEntry() below exactly like the two fields above,
    // but ONLY consumed (via ToEngineCurveTuning()) by the live
    // vehicle_driving_slice.h EngineNetTorque() model, never by the retired
    // placeholder. CHOSEN defaults match EngineCurveTuning's OWN existing
    // CHOSEN defaults for the runtime fields these cross into
    // (torqueFactorIdle/Peak/Redline = 0.5/1.0/0.5), so an unsourced
    // VehicleTuning produces the SAME engine-curve defaults either way.
    double minRpmResistance = 0.5;                       // Min_RPM_Resistance (confirmed field) - CROSSED, drives EngineCurveTuning::torqueFactorIdle
    double optRpmResistance = 1.0;                        // Opt_RPM_Resistance (confirmed field) - CROSSED, drives EngineCurveTuning::torqueFactorPeak
    double maxRpmResistance = 0.5;                         // Max_RPM_Resistance (confirmed field) - CROSSED, drives EngineCurveTuning::torqueFactorRedline
    double clutchSlipRpm = 0.0;                              // Clutch_Slip_RPM (confirmed field, transmission-side per spec - "a transmission field stored inside the engine run") - NOT consumed by EngineCurveTuning (no corresponding runtime field in the 9-field engine schema); kept for completeness/future transmission wiring, CHOSEN default 0.0 (OPEN - this harness has no transmission-side clutch-slip model to feed it into)

    double gearRatio = 3.4;                               // Gear_Ratios[0] (confirmed field) - kept for any existing caller still reading a single ratio; Step06 itself now reads `gearRatios` (plural) below, see that method's own doc comment
    std::vector<double> gearRatios = {3.4};                // Gear_Ratios (confirmed field LIST, spec-vehicle-data.md Sec7.3) - every element FromVehicleEntry() was given; drives GearShiftTuning::maxGear = gearRatios.size() below
    std::vector<double> downshiftRpms;                      // Downshift_RPMs (confirmed field LIST, spec-vehicle-data.md Sec7.3, entry+0x594) - runtime destination WIRED 2026-10-09 (spec-physics-runtime.md Sec10.10, wave 6 item 5): "Not independently re-verified: whether Downshift_RPMs is copied into component +0x34 ... structurally very likely, HIGH not CONFIRMED" - HIGH, not instruction-level CONFIRMED like the other 5 fields in this block, but consumed anyway (see BuildGearShiftTuning(), physics_vehicle_substeps.cpp): every element FromVehicleEntry() was given becomes the REAL per-gear downshift-RPM table GearShiftTuning::downshiftRpmByGear uses; any gear this specific vehicle's own data does not cover (including every gear, when this vector is left empty, CHOSEN default) falls back element-by-element to the pre-existing CHOSEN uniform-margin table (gearUpshiftRpmThreshold - gearDownshiftMarginRpm, unchanged below) - an unsourced vehicle's behaviour is therefore identical to before this wiring.
    double diffGearRatio = 3.7;                            // Diff_Gear_Ratio (confirmed field; re-confirmed 2026-10-09, spec-physics-runtime.md Sec10.10 - transmission constructor 0x00abd400 copies entry+0x5AC -> component +0x10, same field/destination this file already wired, no change needed)
    double reverseGearRatio = 0.0;                          // Reverse_Gear_Ratio (xtbl entry+0x5B0 -> transmission +0x18). 10.3 (2026-10-14) resolves trigger and ratio: wheel ratio = -(Reverse_Gear_Ratio x Diff_Gear_Ratio) while the reverse flag is set (flag per DecideReverse(), vehicle_spec_data.h); the shift machine is suspended in reverse. Files that omit it take an untraced base value, so the default stays 0 (no reverse drive) until sourced. Engagement needs VehicleTuning::reverseFullLogic (the "full logic" byte source is untraced).
    double clutchDelaySec = 0.3;                            // Clutch_Delay (confirmed field, WIRED 2026-10-09, spec-physics-runtime.md Sec10.10 wave 6 item 5: xtbl entry+0x5B4 -> transmission component +0x14, "all consumed by the already-documented gear-shift state machine 0x00aece20") - this file's own HIGH-confidence (not instruction-level CONFIRMED) reading of WHICH of that state machine's own fields +0x14 is: Sec10.3's own independent read of 0x00aece20 names exactly one timer-shaped field it owns ("a shift-duration timer") and no other, so this is mapped to GearShiftTuning::shiftDurationSec (see BuildGearShiftTuning(), physics_vehicle_substeps.cpp). CHOSEN default 0.3s matches GearShiftTuning's own prior CHOSEN default, so an unsourced vehicle's behaviour is unchanged.
    double maxRpmChangePerSec = 1.0e6;                      // Maximum_RPM_Rate (confirmed field, WIRED 2026-10-09, spec-physics-runtime.md Sec10.10 wave 6 item 5: xtbl entry+0x5B8 -> transmission component +0x40) - this file's own HIGH-confidence reading of its destination: vehicle_driving_slice.h's ComputeTargetEngineRpm() (Sec10.3's own 0x00aecef0, "the transmission's half of the engine/transmission RPM-matching loop") is the one function in this model with an otherwise-CHOSEN "maxRpmChangePerSec" parameter ("the spec confirms 'rate-limits' happen, not their coefficients") - the xtbl field's own name matches that role closely enough not to leave it unused. CHOSEN default 1.0e6 (effectively unbounded) matches Step08_ForceTorqueAccumulate()'s own prior hardcoded literal, so an unsourced vehicle's behaviour is unchanged.

    // GearShiftStateMachine tuning (vehicle_driving_slice.h Sec10.3).
    // CORRECTED 2026-10-09 (spec-physics-runtime.md Sec10.10, wave 6 item 5):
    // this block's own prior framing - "the per-gear downshift table has NO
    // xtbl counterpart at all" - no longer holds: Downshift_RPMs (above) IS a
    // real per-gear xtbl table (spec-vehicle-data.md Sec7.3, entry+0x594),
    // and Sec10.10 now gives HIGH (not CONFIRMED) confidence it round-trips
    // into the live transmission component too (+0x34). This harness still
    // falls back to the pre-existing CHOSEN UNIFORM table (every gear
    // downshifts at the same margin below the single shared upshift
    // threshold) for any gear downshiftRpms did not cover - see
    // BuildGearShiftTuning() (physics_vehicle_substeps.cpp) for where these
    // feed GearShiftTuning::downshiftRpmByGear/upshiftRpmThreshold/
    // shiftDurationSec.
    double gearUpshiftRpmThreshold = 5500.0;               // Upshift_RPM (confirmed field, WIRED 2026-10-09, spec-physics-runtime.md Sec10.10 wave 6 item 5: xtbl entry+0x5A8 -> transmission component +0xc, consumed by the gear-shift state machine 0x00aece20) when FromVehicleEntry() was given one; CHOSEN concrete default 5500.0 for an unsourced vehicle unchanged (Sec10.3 only ever confirmed this threshold's EXISTENCE/role, not a magnitude - Sec10.10 is what newly confirms the real per-vehicle SOURCE)
                                                              // NOTE (added 2026-10-08, substeps-regression follow-up task):
                                                              // Step08_ForceTorqueAccumulate()'s own free-rev bridging accommodation
                                                              // (physics_vehicle_substeps.cpp) revs the engine toward optRpm while
                                                              // leaving rest (gated to gear 1 only, see that method's own doc
                                                              // comment), which is the UNIVERSAL best-torque rpm for this curve at
                                                              // ANY throttle (EngineNetTorque()'s low band, Sec10.2) - but
                                                              // EngineNetTorque()'s own high band (rpm > optRpm) dips negative right
                                                              // at its own f=0 edge for any vehicle whose Opt_RPM_Resistance/
                                                              // Max_RPM_Resistance (CROSSED into torqueFactorPeak/Redline) are
                                                              // NOT negligibly small (see that function's own doc comment), which
                                                              // this harness's locked-drivetrain assumption (no slip state, PLANAR
                                                              // SCOPE banner) cannot cross once the bridge has handed off - a
                                                              // vehicle whose own data leaves that dip wide/deep can settle into a
                                                              // real chattering limit cycle right past optRpm rather than ever
                                                              // reaching this threshold (THIS default value plays no part in that
                                                              // failure mode either way - it is purely a property of where the
                                                              // dip sits relative to optRpm, not of this threshold's own
                                                              // magnitude). See tests/synthetic_physics_vehicle_substeps_test.cpp's
                                                              // BuildTestVehicleTuning() for a worked example of choosing
                                                              // Opt_RPM_Resistance/Max_RPM_Resistance small enough to make that
                                                              // dip negligible instead.
    double gearDownshiftMarginRpm = 2000.0;                 // CHOSEN - downshift fires at (upshiftRpmThreshold - this), uniformly across every gear (see note above)

    AxleAccelTuning front;                                   // axles[0] (positional - "Front" by shipped-data convention, spec Sec7.3; CHOSEN assumption, not engine-enforced)
    AxleAccelTuning rear;                                     // axles[1]

    // Brakes - WIRED 2026-10-09, spec-physics-runtime.md Sec10.10's own
    // "Bonus, not one of the 5 target parameters" finding: `0x00ab9ea0`
    // (the same per-vehicle constructor that writes steering/grip, Sec10.10)
    // also writes brakes component slot `+0x30`'s own `+0x14` from these two
    // xtbl fields - HIGH, "not independently confirmed by a vtable/
    // construction-site read the way other slots were" (same confidence tier
    // §10.7's own brakes-slot mapping already carries). Parsed for
    // completeness, same OPEN treatment as reverseGearRatio above: this
    // harness's own Scope discipline already declines to model Havok's BASE
    // brakes stall-timer/lock-flag computation at all
    // (vehicle_driving_slice.h's ComputeBrakeTorque()/ApplyHandbrakeLock()
    // own doc comments - "this harness has no Havok base brakes computation
    // at all"), so there is no stall-timer state machine here for a dwell-
    // time threshold to gate; inventing one now would be fabricating an
    // unmodeled mechanism, not wiring a confirmed one. CHOSEN default 0.0
    // (OPEN) for both.
    double minTimeToBlockSec = 0.0;      // Min_Time_To_Block (confirmed field, entry+0x5E0)
    double aiMinTimeToBlockSec = 0.0;     // AI_Min_Time_To_Block (confirmed field, entry+0x5E4)

    double corneringStiffness = 9000.0;                        // CHOSEN (N/rad) - linear-tire-model constant; Havok's own tire-friction law is not given by any spec (Sec6.4 items 10/11/13 are HIGH/HYPOTHESIS *shape* only)

    double dragCoefficient = 0.33;                              // Drag_Coefficient (confirmed field)
    // spec-physics-runtime.md 10.11.8/10.11.10 (FINAL, 2026-10-14) - pure
    // formulas in vehicle_spec_data.h. 0 = feature off.
    double maximumSpeedMs = 0.0;              // Maximum_Speed (m/s): per-frame hard clamp of chassis speed (10.11.10)
    double maximumSpeedWithNitrousMs = 0.0;    // Maximum_Speed_With_Nitrous (10.11.10 step 2 blend target)
    double maxSpeedOverrideMs = -1.0;           // vehicle +0x1d84 script override (`vehicle_max_speed`); negative = none
    int vehicleClass = 0;                        // Vehicle_Type class: 0 Automobile, 1 Motorcycle, 2 Airplane, 3 Helicopter, 4 Vtol, 5 Watercraft
    double engineTorqueModifier = 1.25;           // Vehicle_engine_torque_modifier (tweak_table.xtbl), 10.11.10: final multiply on engine torque
    double extraGravity = 0.0;                     // Extra_Gravity (10.11.8); 0 = action off. The planar sim only uses its coasting damping term
    double steeringTurnInDampAngleRad = 0.0;        // Steering_Wheel_Damp_Angle (10.11.7); both damp angles > 0 selects the min-time profile limiter
    double steeringReturnDampAngleRad = 0.0;         // Steering_Wheel_Return_Damp_Angle
    bool reverseFullLogic = false;                    // 10.3 "full logic" byte (source untraced, OPEN): false = plain mode, reverse never engages
    double normalSpinDamping = 0.0;            // Normal_Spin_Damping (10.11.8 velocity damper)
    double collisionSpinDamping = 0.0;          // Collision_Spin_Damping
    double collisionSpinThresholdRad = 0.0;      // Collision_Spin_Threshold (rad/s)
    double liftCoefficient = 0.0;                 // Lift_Coefficient (negative = downforce); stored; planar model has no vertical use yet
    double frontalArea = 2.2;                                   // Frontal_Area (confirmed field, m^2)
    double airDensity = 1.2;                                      // Air_Density (confirmed field, kg/m^3)

    double gravity = 9.8;                                          // CONFIRMED, spec-physics-runtime.md Sec4.1 step 5 / Sec4.6 ("gravity (0,-9.8,0)")

    // Builds tuning from a real sr3vehicleinfo::VehicleEntry (itself parsed
    // from a `<name>_veh.xtbl` `<Vehicle>` element by sr3vehicleinfo's own,
    // already-existing reader - spec-vehicle-data.md Sec7). Only fields this
    // schema actually carries are overridden; everything the schema does not
    // carry at all (wheelBase, trackWidth, cgHeight, yawInertia, wheelRadius,
    // steeringRateRadPerSec, corneringStiffness) keeps its CHOSEN default
    // above regardless of what entry contains.
    static VehicleTuning FromVehicleEntry(const sr3vehicleinfo::VehicleEntry& entry);

    // Builds vehicle_driving_slice.h's EngineCurveTuning (the LIVE engine-
    // torque model, Sec10.2) from this struct's own already-real-xtbl-
    // sourced fields, applying the CONFIRMED CROSSED mapping spec-physics-
    // runtime.md Sec10.2's "Construction site and the real xtbl field
    // mapping" finding pins (synced 2026-10-08, commit 4c7d3f2) - NOT a
    // naive name-to-name match. 4 of the 9 fields map straightforwardly (no
    // naming collision: `Torque`/`Min_RPM`/`Opt_RPM`/`Max_RPM` are not named
    // `*_Torque_Factor`/`*_Resistance`, the only two suffixes the spec's own
    // crossing applies to); the other 5 are CROSSED exactly per that
    // finding's own table - see this method's .cpp definition for the
    // field-by-field mapping, cited there rather than only here.
    sr3physics::EngineCurveTuning ToEngineCurveTuning() const;
};

// --- driver input ---------------------------------------------------------

struct DriverInput {
    double throttle = 0.0;  // [0,1]
    double brake = 0.0;     // [0,1]
    double steer = 0.0;     // [-1,1], fraction of maxSteeringAngleRad
    bool handbrake = false; // 10.3 reverse rule input (+0x14f)
    bool nitrous = false;   // 10.11.10 nitrous byte (+0x10c)
};

// --- per-wheel runtime record (sub-steps 2/4/7/8/10/11/13's own scratch) --

// Wheel index convention (CHOSEN, fixed): 0=front-left, 1=front-right,
// 2=rear-left, 3=rear-right.
struct WheelRuntimeRecord {
    // Sub-step 2 ("wheel array relay") - kinematic world placement only, no
    // dynamics (spec: "a per-element... array relayed through one component
    // call" - HIGH, content not confirmed; used here to hold position).
    double worldX = 0.0, worldZ = 0.0;

    // Sub-step 7 ("finishes building a per-wheel contact/output record") -
    // CHOSEN use: the wheel's own forward/right unit vectors in world space
    // (steering-offset for a steered wheel), computed once per step and
    // reused by steps 10/11/13 instead of recomputing trig inline.
    double contactForwardX = 1.0, contactForwardZ = 0.0;
    double contactRightX = 0.0, contactRightZ = -1.0;

    // Sub-step 4 ("byte-status array... extra scalar") - CHOSEN: always
    // grounded in this flat-ground, no-vertical-travel model (see the file
    // banner's PLANAR SCOPE note) - present for structural/ordering fidelity
    // only, not because real suspension travel is simulated.
    bool grounded = true;

    // Sub-steps 8/13 ("a force accumulator"/clamp input) - normal load (N),
    // from the longitudinal+lateral load-transfer approximation (CHOSEN).
    double normalForce = 0.0;

    // Sub-steps 10/11 ("decay-toward-limit"), re-clamped by 13 - the wheel's
    // own final (longitudinal, lateral) force pair in the WHEEL frame (N).
    double longForce = 0.0, latForce = 0.0;
};

// --- the vehicle instance's own per-step state ----------------------------

struct VehicleInstanceState {
    // Chassis planar pose (ground plane = world X/Z, Y up per spec-physics-
    // runtime.md's confirmed gravity direction). heading is radians,
    // measured so that (cos(heading), sin(heading)) is the chassis forward
    // direction in (X,Z) - a CHOSEN convention, consistent throughout this
    // file, not itself a spec value.
    double posX = 0.0, posZ = 0.0;
    double heading = 0.0;

    // Body-frame chassis velocity: x = forward, y = right (lateral/slip).
    double velForwardBody = 0.0, velRightBody = 0.0;
    double yawRate = 0.0;  // rad/s

    // Sub-step 1 relay target (+0xb0 in the real instance) - HYPOTHESIS "a
    // one-byte status relay... consistent with a boolean device/driver-input
    // status flag, not with a force or torque computation" (Sec6.4 item 1).
    // Modelled as pass-through state with NO dynamical effect, matching that
    // reading exactly.
    bool driverInputDeviceStatus = true;
    DriverInput lastInput{};  // latched by Step01, read by every later step

    // Sub-step 3 relay (+0xb4/+0xb8) - CHOSEN reading: steering current/
    // target angle (spec's own HYPOTHESIS names this OR an engine RPM/
    // throttle pair as equally plausible, Sec6.4 item 3; this file picks
    // steering because the per-frame driver input needs exactly this
    // current/target rate-limit relationship, and engine state is tracked
    // separately below since it is consumed inside steps 10/11 instead, see
    // the file banner's "no slot for engine" note).
    double steeringAngleCurrentRad = 0.0;
    double steeringAngleTargetRad = 0.0;

    // Engine/drivetrain running state (consumed by steps 10/11, not any
    // single named sub-step - see the file banner).
    double engineRpm = 900.0;
    double currentEngineTorqueNm = 0.0;
    double drivetrainRatio = 0.0;  // Sub-step 6: gearRatios[currentGear-1] * diffGearRatio (now a REAL per-gear lookup, see Step06's own doc comment - previously a fixed single-gear constant)
    int currentGear = 1;             // Wired 2026-10-08: driven by vehicle_driving_slice.h's GearShiftStateMachine (Sec10.3), mirrored here from VehicleSubstepSimulator's own gearShift_ member purely for external visibility/testing
    double targetEngineRpm = 900.0;   // Wired 2026-10-08: Sec10.3's ComputeTargetEngineRpm() output, fed into Sec10.2's EngineRampRpm() as `targetRpm` - the "RPM-matching loop" vehicle_driving_slice.h's own ComputeTargetEngineRpm() doc comment names explicitly

    // Added 2026-10-08 (substeps-regression follow-up task) - CHOSEN
    // harness-level latch, NOT a vehicle_driving_slice.h formula: true while
    // this harness's own free-rev bridging accommodation (Step08's own doc
    // comment, physics_vehicle_substeps.cpp) is standing in for Sec10.3's
    // real, unmodelled clutch-engagement float - i.e. while the engine is
    // allowed to free-rev AHEAD of what ComputeTargetEngineRpm()'s own real
    // wheel-speed-driven signal alone would produce. Latches to false (the
    // real wheel-speed-driven signal alone takes over from then on,
    // unassisted) the first time that real signal catches up to/exceeds the
    // free-rev target on its own - a one-way clutch-lock transition, never
    // reconsidered while driving away - and re-arms back to true once the
    // vehicle is genuinely at rest again (CORRECTED 2026-10-08, same pass:
    // originally keyed off the real signal returning to its own idleRpm
    // floor, which is true on essentially every step while this bridge is
    // actively holding rpm above what real road speed justifies - that gap
    // is this bridge's entire reason to exist, not a rare condition - so
    // that reading re-armed on almost every bridged step rather than only
    // at a genuine stop; see Step08_ForceTorqueAccumulate()'s own "BUG FOUND
    // AND FIXED" doc comment, physics_vehicle_substeps.cpp, for the full
    // account, including why `std::abs(state_.velForwardBody)` near zero is
    // the correct re-arm signal instead). Can also be forced back to true
    // mid-drive by that same method's separate "genuinely stuck" escalation
    // path (low chassis acceleration while plateaued near optRpm in gear 1)
    // - not only by a fresh launch from a dead stop.
    bool clutchSlipActive = true;

    // Added 2026-10-08 (same follow-up task, same free-rev bridge): seconds
    // spent PLATEAUED at (or past) optRpm while the bridge is engaged (gear
    // 1, throttle>0, not yet locked) - NOT total time since the bridge first
    // engaged (tried first, over-escalated every launch including ones
    // climbing normally and never actually stuck, since the initial idle-
    // to-optRpm climb itself already consumes a fraction of a second a
    // start-from-zero timer cannot tell apart from genuinely being stuck).
    // Reset to 0 both on re-arm and on any step where engineRpm is still
    // below optRpm (see Step08_ForceTorqueAccumulate()'s own doc comment for
    // the exact margin/reset logic). Feeds that method's own time-escalated
    // ceiling: holding exactly at optRpm can leave the vehicle at a genuine,
    // physically real equilibrium (engine thrust == aerodynamic drag)
    // sitting almost exactly at EngineNetTorque()'s own low/high-band
    // boundary, where the high band's quadratic recovery is too gradual to
    // out-accelerate drag before the real (locked) formula takes over - not
    // a bug in the bridge's own lock timing, a property of how slowly
    // torque regrows just past that specific boundary for this fixture's
    // own torque/mass/gearing/drag combination.
    double clutchSlipElapsedSec = 0.0;

    // Sub-step 5's own latch (brake input, 0-1) - thin forwarder, no math of
    // its own (matches Sec6.4 item 5's "no instance-state save/restore, no
    // own math").
    double lastBrakeInput = 0.0;

    std::array<WheelRuntimeRecord, 4> wheels{};

    // Sub-steps 8/9 ("a force accumulator at +0xe0 and a torque accumulator
    // at +0xf0"; "a magnitude-based clamp/rescale of the torque
    // accumulator") - the running per-step total, reset at the start of
    // Step08 and applied to the chassis by IntegrateChassis() after Step13.
    double forceAccumX = 0.0, forceAccumZ = 0.0;  // world-independent, BODY frame (x=forward, z=right - reusing the (x,z)-pair naming for the body axes, not world axes)
    double torqueAccumY = 0.0;                     // yaw torque (about the up axis)

    // Lagged chassis acceleration, used by Step08/Step13's load-transfer
    // estimate (CHOSEN - a standard explicit approximation avoiding an
    // implicit force/load solve; see Step08's own doc comment).
    double prevAccelForwardBody = 0.0, prevAccelRightBody = 0.0;

    // 10.11.10 / 10.3 persistent limiter state.
    double nitroBlend = 0.0;       // vehicle +0x1a34
    bool wrecked = false;          // skips the Maximum_Speed clamp
    bool reverseFlag = false;      // physics-object +0xb0
};

// RETIRED (2026-10-08, engine-curve/slip-table follow-up task): this
// harness's own original CHOSEN engine-torque-curve stand-in (Sec6.4's own
// torque-curve gap - "a concrete tire-force law... is this file's own
// CHOSEN, explicitly-flagged stand-in"), a simple piecewise-linear
// evaluation over only 2 of the real schema's 9 Engine fields
// (`minRpmTorqueFactor`/`maxRpmTorqueFactor`). NO LONGER CALLED by
// VehicleSubstepSimulator (see Step08_ForceTorqueAccumulate() in
// physics_vehicle_substeps.cpp, which now calls vehicle_driving_slice.h's
// EngineNetTorque() via VehicleTuning::ToEngineCurveTuning() instead, now
// that the real 9-field xtbl<->runtime crossed mapping is confirmed, Sec10.2)
// - kept defined, out of the anonymous namespace, SOLELY so this project's
// own old-vs-new regression-equivalence test
// (tests/synthetic_physics_vehicle_substeps_test.cpp's
// testEngineTorqueModelSwitchedFromPlaceholder()) can call both the old and
// new models side-by-side on identical inputs and assert they genuinely
// differ, proving the switch actually happened rather than merely being
// plumbed in without effect.
double ComputeEngineTorqueNm(const VehicleTuning& t, double rpm, double throttle);

// --- the simulator ----------------------------------------------------

// Runs the 13 confirmed sub-steps, in the exact call order spec-physics-
// runtime.md Sec6.1/Sec6.7 gives (phase 1: 0x00c55f80, 0x00c58770,
// 0x00c56040, 0x00c58360, 0x00c562d0, 0x00c560a0; phase 2: 0x00c565b0,
// 0x00c566e0, 0x00c56800, 0x00c57a50, 0x00c57ca0, 0x00c57690, 0x00c576d0),
// once per call to Step(), followed by a CHOSEN semi-implicit-Euler chassis
// integration (no spec names an integrator - the 13 sub-steps are Havok's
// own per-step FORCE/TORQUE BUILD-UP, not the integrator itself, which is
// deeper Havok-internal material entirely out of this unit's scope).
//
// Determinism: every method below is a pure function of (tuning, state,
// input, dt) - no randomness, no I/O, no threading, no uninitialized reads.
// Running the same (tuning, initial state, input sequence, dt sequence)
// through two separate instances therefore produces bit-identical floating-
// point results (IEEE 754 arithmetic in a fixed instruction order is
// reproducible on one machine/build) - this is what the synthetic test
// asserts directly, rather than merely assuming it.
class VehicleSubstepSimulator {
public:
    // Defined out-of-line (physics_vehicle_substeps.cpp) because it must
    // build this simulator's two NEW stateful driving-slice components
    // (gearShift_/steeringLimiter_ below, wired in 2026-10-08) from `tuning`,
    // which `GearShiftStateMachine`'s own constructor (no default
    // constructor exists - it is genuinely stateful, not a stand-in) needs
    // up front rather than via a default-then-assign pattern.
    explicit VehicleSubstepSimulator(VehicleTuning tuning);

    VehicleTuning tuning_;
    VehicleInstanceState state_;

    // Wired 2026-10-08: vehicle_driving_slice.h's own real, stateful Sec10.1/
    // Sec10.3 component objects (NOT CHOSEN stand-ins themselves - their own
    // formulas are CONFIRMED; see that header's own doc comments) -
    // constructed once here (not inside VehicleInstanceState, which is
    // plain data reset/rebuilt by the Step0N_* methods) because both carry
    // internal state that must persist and evolve call-to-call: the
    // steering limiter's own reversal-direction memory (Sec10.1), and the
    // gear-shift machine's own timers/current gear (Sec10.3).
    SteeringRateLimiter steeringLimiter_;
    SteeringProfileLimiter steeringProfile_;  // used instead when both damp angles are data-sourced (10.11.7)
    GearShiftStateMachine gearShift_;

    // Call-order log (sub-step index 1..13, in execution order this call),
    // cleared and refilled by every Step() - lets a test assert the
    // CONFIRMED call order directly rather than only its effects.
    std::vector<int> stepCallLog;

    // Runs one frame: Delta-t clamp (reusing frame_contract.h's own
    // CONFIRMED [1/120, 1/20] s clamp, Sec4.3/Sec5), the 13 sub-steps in
    // order, then chassis integration. Returns the clamped dt actually used.
    double Step(double rawDt, const DriverInput& input);

private:
    // Phase 1 (from the game override 0x00ae9570, Sec6.4 items 1-6).
    void Step01_DriverInputStatusRelay(const DriverInput& input);
    void Step02_WheelArrayRelay();
    void Step03_SteeringRelay(double dt);
    void Step04_SuspensionWheelStatusRelay();
    void Step05_BrakesForward();
    void Step06_TransmissionForward(double dt);

    // Phase 2 (from the game override 0x00ae7470, Sec6.4 items 7-13).
    void Step07_WheelContactFinish();
    void Step08_ForceTorqueAccumulate(double dt);
    void Step09_TorqueClamp();
    void Step10_FrontAxlePairForce(double dt);
    void Step11_RearAxleForce(double dt);
    void Step12_FrictionForwardNotReproduced();
    void Step13_FinalPerWheelClamp();

    void IntegrateChassis(double dt);

    // Shared per-axle wheel-force computation (steps 10/11 share this
    // shape, "the same ... shape ... but over a different field-offset
    // family", Sec6.4 item 11) - leftIdx/rightIdx select the axle's two
    // wheels in the fixed 0=FL,1=FR,2=RL,3=RR convention (each wheel's own
    // body-frame offset is read back out of WheelOffsets() internally).
    // `dt` added 2026-10-08 (driving-slice wire-in): needed by
    // vehicle_driving_slice.h's ComputeBrakeTorque(), which has its own
    // CONFIRMED 1/dt term (Sec10.6) - see this method's own .cpp doc comment.
    void ProcessAxle(const AxleAccelTuning& axle, int leftIdx, int rightIdx, double steerAngleRad, double dt);
};

}  // namespace sr3physics
