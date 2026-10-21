#pragma once

// sr3physics/vehicle_driving_slice.h - the 7 named per-component `hkpVehicle`
// override classes spec-physics-runtime.md Sec10 opens, as a standalone,
// deterministic, headless library of their CONFIRMED formulas.
//
// Source: spec-physics-runtime.md Sec10 ("Driving-slice component overrides -
// the 7 named hkpVehicle component classes"), SPEC TEAM (cleanroom reverse-
// engineering process) - the ONLY source for every CONFIRMED/HIGH/HYPOTHESIS
// claim tagged below. Addresses and field offsets in comments are copied from
// that text so a reader can cross-check the section; nothing here was derived
// from the executable, and no decompiled code or pseudocode appears below.
// Every CHOSEN formula/constant filling a gap the spec leaves open is
// commented individually at its point of use, same convention as this
// project's existing vehicle_substeps.h.
//
// ---------------------------------------------------------------------------
// RELATIONSHIP TO vehicle_substeps.h - read this before assuming these wire
// together automatically.
// ---------------------------------------------------------------------------
// vehicle_substeps.h/.cpp models spec Sec6 (D-U0): the 13 confirmed Havok
// vehicle sub-steps' own CALL ORDER and dispatch SHAPE, with CHOSEN stand-in
// per-wheel tire/force physics (a linear-tire model + friction-circle clamp)
// standing in for what Sec6 itself says is unconfirmed (no formula given for
// any of the 13 sub-steps' own internal arithmetic - see that header's own
// SCOPE DISCIPLINE banner). THIS file instead models Sec10 (the direct
// follow-up unit): the 7 named COMPONENT classes those sub-steps dispatch
// INTO, several of which Sec10 gives real, CONFIRMED formulas for (steering's
// inverse-square speed falloff, engine's 9-field torque curve, suspension's
// asymmetric spring-damper, wheel-collide's AABB-only raycast).
//
// WIRED-IN UPDATE (2026-10-08, driving-slice wire-in task): these two files
// ARE now wired together, sub-step by sub-step, wherever a component's own
// CONFIRMED formula genuinely covers what vehicle_substeps.cpp's own
// placeholder was standing in for - steering (`SteeringBaseAngle`/
// `SteeringRateLimiter`, replacing Step03's old ad hoc two-point rate
// clamp), transmission (`GearShiftStateMachine`, replacing Step06's old
// fixed single gear), the engine/transmission RPM-matching loop
// (`ComputeTargetEngineRpm`->`EngineRampRpm`, replacing Step08's old instant
// RPM snap), brakes (`ComputeBrakeTorque`, replacing ProcessAxle's old ad
// hoc per-axle brake force), driver input's quadratic throttle curve
// (`ApplyThrottleResponseCurve`, applied in Step01), and the §10.8 friction-
// ellipse saturation test (`SolveFrictionEllipse`, replacing the old plain-
// circle `ApplyFrictionCircle` clamp in Steps 10/11/13, with a CHOSEN
// decoupled/no-real-curve default that is numerically IDENTICAL to the old
// clamp - see physics_vehicle_substeps.cpp's own doc comments for the
// equivalence argument). See vehicle_substeps.cpp's Step0N_* doc comments
// for the sub-step-by-sub-step CONFIRMED-vs-CHOSEN account of exactly what
// changed and why.
//
// SECOND WIRED-IN UPDATE (2026-10-08, engine-curve/slip-table follow-up task,
// authority spec-physics-runtime.md §10.2/§10.8 "Construction site and the
// real xtbl field mapping" + the real D-U1 slip-velocity/16-entry-table
// finding, both synced commit 4c7d3f2): the engine's own 9-field torque
// CURVE - previously left unwired here because "its Havok-internal field
// schema has no established correspondence to the 6-field xtbl Engine
// schema `ComputeEngineTorqueNm()` already consumes" - now IS wired, because
// that correspondence is now confirmed: `0x00aba4c0`-`0x00aba599`'s own
// bare field-copy constructor, cross-checked against `0x00ae3330`'s own
// consuming formula, shows the authored xtbl field NAMES are CROSSED from
// their runtime ROLES for 5 of the 9 fields (every `*_Torque_Factor`-named
// field drives a resistance-shaped runtime field and vice versa). See
// `VehicleTuning::ToEngineCurveTuning()` (vehicle_substeps.h/.cpp) for the
// exact mapping and `EngineNetTorque()`'s own new call site in
// vehicle_substeps.cpp's Step08. The real tyre slip-velocity computation
// (`ComputeTyreSlipVelocity()` below, `0x00c64252`-`0x00c64306`, confirmed
// to be a genuinely SEPARATE computation from §10.5's
// `ComputeContactRelativeVelocity()`) is now also implemented and wired into
// `ProcessAxle()`'s own wheel-frame velocity projection (physically a
// behaviour-preserving reformulation under this harness's own static-ground
// assumption - see that function's doc comment for the exact equivalence),
// and the §10.8 16-entry friction/slip table now has a real, injectable
// (caller-settable, per-axle) home - `FrictionSlipTable`/
// `FrictionSlipTableCurve()` below, `VehicleTuning::AxleAccelTuning::
// frictionSlipTable` - defaulting to an honestly-flagged pass-through
// placeholder, never fabricated sampled values.
//
// What is STILL NOT wired (suspension's spring-damper - this model carries
// no vertical/compression state at all; wheel-collide's AABB/raycast
// dispatch - this model has no real collision world/vertical wheel travel
// to feed it; `ComputeTangentEffectiveMass()`'s own soft/rigid effective-
// mass terms - composing those would require carrying real iterative-
// sequential-impulse-solver state this harness's per-step-force model does
// not have, see this file's own §10.8 banner below) is flagged individually
// at each unchanged site, not silently dropped.
//
// ---------------------------------------------------------------------------
// SCOPE DISCIPLINE, per component (Sec10.7's own cross-cutting table) -
// Engine/Transmission = full replacement (no Havok base call found either);
// Suspension/Brakes = unconditional wrap-and-call (always calls Havok's base
// too - NOT reproduced here, out of scope, Sec1's Havok-internals boundary);
// Steering/Wheel-collide AABB = conditional delegation (one mode bypasses
// Havok, the other calls Havok's base - the base-method formula IS given for
// steering, Sec10.1, and IS reproduced below).
//
// OPEN items this file does NOT model, flagged individually below too (Sec
// 10.7's own list): vehicle-instance mode flags +0x164 (both bits)/+0x19c/
// +0x201/+0x160/+0x6d bit1/+0x59 bit2 exact meaning (modelled only as opaque
// bool knobs this library's caller sets, not tied to any real offset); the
// writer/runtime value of `_DAT_02845af4` (engine's final torque multiplier,
// zero on disk - CHOSEN default 1.0 here, see EngineComponent); manual-
// transmission shift-input handling (not modelled, automatic-only);
// wheel-collide's round-robin counter's purpose (not modelled); the struck-
// shape-type taxonomy (0x00aeac00's 0/1/2/3 - irrelevant here, this library
// has no real collision world); the remaining 6 of 7 component-array slot
// mappings (irrelevant here - this file implements component FORMULAS, not
// the dispatch shell's slot array, which is vehicle_substeps.h's own concern
// and its own still-OPEN item).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace sr3physics {

// ===========================================================================
// 10.1 Steering (`hk_custom_vehicle_steering`, row 31)
// ===========================================================================

// `0x00aeb0c0`'s own fields (CONFIRMED offsets, Sec10.1): "a per-vehicle
// threshold" at `+0xc` and an input-to-angle scale at `+0x8`. Field names are
// this file's own (not given by the spec beyond their offset/role).
struct SteeringBaseTuning {
    double inputToAngleScale = 1.0;  // steeringComponent.+0x8 ("baseAngle = input * steeringComponent.+0x8")
    double speedThreshold = 1.0;     // steeringComponent.+0xc (CONFIRMED role: "above a per-vehicle threshold")
    bool fastPathNoSpeedTerm = false;  // vehicle-instance +0x19c flag (OPEN exact meaning, Sec10.7) - CHOSEN: caller-set bool, not tied to any real offset read
};

// `0x00aeb0c0` (CONFIRMED formula, Sec10.1): fast path (instance `+0x19c` set)
// is a flat multiply with no speed term; the normal path calls Havok's own
// base method `0x00c5a830` (CONFIRMED formula): baseAngle = input *
// steeringComponent.+0x8, then ABOVE the per-vehicle threshold (+0xc),
// finalAngle = baseAngle * (threshold/speedTerm)^2 - "a clean inverse-square
// speed-sensitive falloff, living inside Havok's own default implementation."
// `speedTerm` is CHOSEN here as the caller-supplied scalar (Sec10.1 itself
// only says "projects a transformed velocity-shaped vector onto a reference
// row for a speedTerm" - HIGH, the exact vector/row is not given - a caller
// passes whatever scalar speed measure it has, e.g. chassis forward speed).
double SteeringBaseAngle(const SteeringBaseTuning& tuning, double inputSteerFraction, double speedTerm);

// `0x00aeb110` (CONFIRMED structure, Sec10.1): rate-limits the angle using
// two rate-constant pairs (normal-turn vs. centring) off the steering
// component; on a direction reversal, seeds the limiter with
// `rate1^2/(2*rate2)` (the textbook v^2/(2a) stopping-distance term) before
// delegating to a generic shared easing primitive (`0x00dae970`, NOT
// vehicle-specific, and NOT itself reproduced here - HIGH/OPEN, out of
// scope: it is a generic primitive, not part of this unit's own 7 component
// classes). CHOSEN concrete behaviour implementing the CONFIRMED pieces
// (the two named rate constants' roles, and the reversal-seed formula): see
// this class's own .cpp doc comment for the exact selection/seeding logic.
class SteeringRateLimiter {
public:
    SteeringRateLimiter(double normalTurnRateRadPerSec, double centeringRateRadPerSec)
        : normalRate_(normalTurnRateRadPerSec), centeringRate_(centeringRateRadPerSec) {}

    double Update(double targetRad, double dt);
    double current() const { return current_; }

private:
    double normalRate_;
    double centeringRate_;
    double current_ = 0.0;
    double lastDiffSign_ = 0.0;
};

// spec-physics-runtime.md 10.11.7 steering limiter profile (`0x00dae970`):
// minimum-time profile toward the target angle with maximum angular speed V
// and acceleration/deceleration A = V^2 / (2 * dampAngle), so stopping from V
// uses exactly the damp angle. When the target is 0 (centring) the return
// pair is used, otherwise the turn-in pair. Evaluated as at most 8 analytic
// segments per call (accelerate, cruise at V, decelerate), time tolerance
// 1e-6, as the spec describes. The reversal-seed role inside `0x00dae970`
// is OPEN (10.11.11 item 9) and is not modelled. Replaces SteeringRateLimiter
// in the simulator whenever the vehicle data supplies both damp angles.
struct SteeringProfileParams {
    double turnInSpeed = 0.0, turnInAccel = 0.0;  // V, A = V^2/(2*damp)
    double returnSpeed = 0.0, returnAccel = 0.0;
};

class SteeringProfileLimiter {
public:
    explicit SteeringProfileLimiter(SteeringProfileParams p) : p_(p) {}
    double Update(double targetRad, double dt);
    double current() const { return angle_; }
    double velocity() const { return vel_; }

private:
    SteeringProfileParams p_;
    double angle_ = 0.0;
    double vel_ = 0.0;
};

// ===========================================================================
// 10.2 Engine (`hk_custom_vehicle_engine`, row 30)
// ===========================================================================

// `0x00ae3330`'s real 9-field piecewise torque/resistance curve (CONFIRMED
// offsets and roles, Sec10.2 table). Field names are this file's own.
struct EngineCurveTuning {
    double idleRpm = 800.0;             // +0x8
    double peakTorqueRpm = 4500.0;      // +0xc
    double redlineRpm = 6500.0;         // +0x10
    double torqueScale = 1.0;           // +0x14, "overall torque scale"
    double resistanceLowBandCoeff = 1.0;   // +0x18, "quadratic resistance-shape coefficient, low-RPM band"
    double resistanceHighBandCoeff = 1.0;  // +0x1c, "...high-RPM band"
    double torqueFactorIdle = 0.5;      // +0x20
    double torqueFactorPeak = 1.0;      // +0x24
    double torqueFactorRedline = 0.5;   // +0x28
    // D-17 (spec-differential-results.md): the TABLE field roles (not crossed) used by the original curve.
    double torque = 0.0, minRpm = 0.0, optRpm = 0.0, maxRpm = 0.0;
    double minTorqueFactor = 1.0, maxTorqueFactor = 1.0;                   // Min/Max_RPM_Torque_Factor
    double minResistance = 0.0, optResistance = 0.0, maxResistance = 0.0;  // Min/Opt/Max_RPM_Resistance
    double clutchSlipRpm = 0.0;                                            // Clutch_Slip_RPM
};

// D-18 (vectors E2): the original's engine-RPM update. target = Min_RPM + (Clutch_Slip_RPM - Min_RPM) * throttle; if the
// transmission RPM is at or above target the new engine RPM IS the transmission RPM; otherwise the (two-way) ramp of EngineRampRpm.
double EngineRpmUpdate(const EngineCurveTuning& tuning, double currentRpm, double throttle, double transmissionRpm, double dt);

// `0x00ae31f0` (CONFIRMED formula+constants, Sec10.2): ramps RPM UP-ONLY
// toward a throttle-lerped target - "no explicit fall-rate path (falling RPM
// presumably comes from the torque curve's own rotational dynamics)", so
// this function never itself reduces rpm below its input when target<rpm
// (the caller's own rotational dynamics, e.g. engine braking via negative net
// torque feeding back through the drivetrain, is what is expected to bring
// rpm down - out of scope for this single function, matching the spec's own
// reading).
//   rate = clamp((target-current)/dt, -10000.0, 10000.0)   [confirmed literal]
//   ease = clamp(|target-current|/750.0, 0, 1)              [confirmed literal]
//   new  = current + ease*rate*dt
double EngineRampRpm(double currentRpm, double targetRpm, double dt);

// `0x00ae3330` (CONFIRMED structure + CHOSEN closed form, Sec10.2): "At/above
// peak-torque RPM the curve lerps torque/resistance factors toward redline
// (clamping at redline); below peak it lerps toward idle, additionally
// scaling by a clutch/load ratio near idle." CHOSEN concrete closed form (the
// spec gives the 9 fields' roles and this qualitative shape, not a formula
// combining them - see the .cpp's own doc comment for the exact lerp/
// quadratic-band split chosen): netTorque = resistanceFactor*throttle -
// torqueFactor (CONFIRMED formula, quoted literally), scaled by torqueScale,
// gated by the water/attached-object ×0.85 penalty (CONFIRMED exact literal,
// `attachedObjectPenalty` models the "a specific bit is set on the attached
// object" condition as a caller-supplied bool rather than reproducing the
// liveness-check call chain itself - Sec1's Havok/game-object boundary), and
// finally scaled by `finalScale` (CHOSEN default 1.0 - models the OPEN
// `_DAT_02845af4` global, "reads zero on disk... something must set it at
// runtime or the engine never produces nonzero torque"; defaulting to 1.0
// rather than the on-disk 0.0 is the only reading that does not make this
// function always return zero, which the spec's own words say cannot be
// the shipped behaviour).
double EngineNetTorque(const EngineCurveTuning& tuning, double rpm, double throttle, bool attachedObjectPenalty,
                       double finalScale = 1.0, double transmissionRpm = -1.0, double vehicleMass = 0.0);

// ===========================================================================
// 10.3 Transmission (`hk_custom_vehicle_transmission`, row 32)
// ===========================================================================

// `0x00aece20`'s real automatic gear-shift state machine fields (CONFIRMED
// roles, Sec10.3): a per-gear downshift-RPM table (`+0x34[gear-1]`), a single
// shared upshift threshold (`+0xc`), a max-gear clamp, and a shift-duration
// timer. `downshiftRpmByGear[g]` is the downshift RPM for gear `g+2` (there is
// no downshift FROM gear 1). Cooldown durations are CHOSEN (the spec confirms
// "independent cooldown gates per direction" exist, not their length).
struct GearShiftTuning {
    std::vector<double> downshiftRpmByGear;  // size = maxGear-1, CONFIRMED per-gear table (+0x34[gear-1])
    double upshiftRpmThreshold = 5500.0;      // +0xc, CONFIRMED single shared threshold
    int maxGear = 5;
    double shiftDurationSec = 0.3;             // CHOSEN
    double upshiftCooldownSec = 0.5;           // CHOSEN
    double downshiftCooldownSec = 0.5;         // CHOSEN
};

// `0x00aece20` (CONFIRMED structure, Sec10.3): the real automatic upshift/
// downshift state machine. Manual-shift input handling is explicitly NOT
// modelled here (Sec10.3: "gated out entirely at the mode byte and was not
// located, OPEN").
class GearShiftStateMachine {
public:
    explicit GearShiftStateMachine(GearShiftTuning tuning, int startGear = 1)
        : tuning_(std::move(tuning)), gear_(startGear) {}

    // Advances cooldown/shift timers by dt, then evaluates the single shared
    // upshift threshold and the per-gear downshift table against rpm,
    // clamped at [1, maxGear] and gated by shiftDurationSec/cooldowns.
    void Update(double rpm, double dt, bool shiftWindowClosed = false, bool upshiftInhibited = false);

    int gear() const { return gear_; }
    bool midShift() const { return shiftTimer_ > 0.0; }

private:
    GearShiftTuning tuning_;
    int gear_;
    double shiftTimer_ = 0.0;
    double upCooldown_ = 0.0;
    double downCooldown_ = 0.0;
};

// `0x00aecef0` (CONFIRMED structure + constants, Sec10.3): "computes a
// torque-split-weighted average wheel RPM using the exact 60/(2*PI)
// conversion (confirmed literals 60.0/0.15915493667...), subtracts the
// cached reaction-torque field, rate-limits, and double-clamps - the
// transmission's half of the engine/transmission RPM-matching loop that
// feeds the clutch-side correction term Sec10.2's torque curve subtracts."
// Composition note: a caller is expected to feed this function's return
// value into EngineComponent's EngineRampRpm() as `targetRpm` - that
// composition IS the "RPM-matching loop" the spec's own cross-reference
// names, without this file inventing a tighter internal coupling than the
// spec itself states. `reactionTorqueToRpmGain` and `maxRpmChangePerSec` are
// CHOSEN (the spec confirms "subtracts"/"rate-limits" happen, not their
// coefficients); clamping is to [idleRpm, redlineRpm] (CHOSEN concrete
// bounds for the CONFIRMED "double-clamps").
double ComputeTargetEngineRpm(const std::array<double, 4>& wheelAngularSpeedRadPerSec,
                               const std::array<double, 4>& torqueSplitWeights, double overallGearRatio,
                               double reactionTorqueNm, double reactionTorqueToRpmGain, double previousTargetRpm,
                               double maxRpmChangePerSec, double dt, double idleRpm, double redlineRpm);

// ===========================================================================
// 10.4 Suspension (`hk_custom_vehicle_suspension`, row 34)
// ===========================================================================

// Per-wheel `{spring, damperRelaxation, damperCompression}` record (CONFIRMED
// fields, stride `0xC`, Sec10.4). `restLength`/`loadFactor` are this file's
// own additions needed to evaluate the spring-damper formula (restLength is
// implied by "restLength-currentLength" in the CONFIRMED formula itself;
// loadFactor is the CONFIRMED "load factor scaling only the spring term",
// HIGH: anti-roll/weight-transfer weighting - CHOSEN default 1.0, i.e. no
// weighting, when a caller has no load-transfer estimate to feed in).
struct SuspensionWheelTuning {
    double spring = 30000.0;            // N/m, CHOSEN concrete magnitude
    double damperRelaxation = 2500.0;    // rebound-branch damper, CHOSEN concrete magnitude
    double damperCompression = 3500.0;   // compression-branch damper, CHOSEN concrete magnitude
    double restLength = 0.3;              // m, CHOSEN concrete magnitude
    double chassisMass = 1.0;              // M (FINAL D-25): the force is multiplied by the chassis mass
    double loadFactor = 1.0;               // HIGH role (anti-roll/weight-transfer), CHOSEN default (no weighting)
};

// `0x00c59630` (FINAL, D-25 emulation, 12,000 wheel cases bit-exact):
//   spring = float32(L - C) * S * k;   damping = (v < 0) ? damperCompression : damperRelaxation (v >= 0 incl. 0, -0);
//   force  = float32(float32(spring - damping * v) * M),  M = chassis mass (1 / inverse mass; 0 if inverse mass 0).
// `v` is the closing speed in the SPEC sign (d(length)/dt: NEGATIVE while compressing). A wheel not touching
// the ground gives 0 (touching = false). Strength and damping are per unit mass. (Replaces the CHOSEN
// "divide by unknown X" / compress-on-positive-velocity stand-in.)
double SuspensionSpringDamperForceNoXDenominator(const SuspensionWheelTuning& tuning, double currentLength,
                                                  double closingSpeed, bool touching = true);

// `0x00c14790` (FINAL, D-25, Sec10.4): M = 1 / inverse mass (motion +0xCC, the w of +0xC0), stored as f32; 0 when the
// inverse mass is 0. Feed it into SuspensionWheelTuning::chassisMass: the default 1.0 only stands in for callers with
// no chassis and drops the mass factor of the force.
double ChassisMassFromInverseMass(double inverseMass);
// Copy of `base` with chassisMass set from the chassis inverse mass (Sec10.4).
SuspensionWheelTuning WithChassisInverseMass(SuspensionWheelTuning base, double inverseMass);

struct SuspensionStepResult {
    double averageForce = 0.0;
    double finalCompressionLength = 0.0;
    double finalCompressionVelocity = 0.0;
};

// `0x00aebe30` (CONFIRMED structure, 992 insn, Sec10.4): internally re-slices
// the incoming dt into <= 1/60s (`kMaxMicroStepSec`, CONFIRMED literal
// 0.016666668) chunks and loops - "an internal sub-stepping layer independent
// of... the outer variable-dt physics step." Each internal micro-step
// predicts the wheel-anchor position and calls `0x00aeb760` - "a ray-vs-plane
// intersection against a cached ground-plane, not a fresh Havok query" - then
// calls Havok's base suspension method once per micro-step. This library has
// no real collision world (Scope discipline, matching this project's
// existing Havok-boundary abstractions, e.g. frame_contract.h's
// IPhysicsBackend): `currentLengthFn(timeIntoOuterStepSec)` stands in for the
// predicted-anchor-position-then-ray-vs-plane call, returning the
// compression length a caller's own (possibly trivial/flat-ground) geometry
// would have produced at that instant. Returns the micro-step-COUNT-weighted
// average force (CHOSEN aggregation - the spec confirms "two virtual calls...
// apply linear/angular impulse" commit an aggregated reaction after the loop,
// HIGH role, not the aggregation formula).
SuspensionStepResult RunSuspensionSubstep(const SuspensionWheelTuning& tuning, double outerDt,
                                           double initialCompressionLength,
                                           const std::function<double(double)>& currentLengthFn);
inline constexpr double kSuspensionMaxMicroStepSec = 0.016666668;  // CONFIRMED literal, Sec10.4

// ===========================================================================
// 10.5 Wheel collision (`hk_custom_vehicle_raycast_wheel_collide`, row 36 +
// collector, row 37)
// ===========================================================================

struct AABB3 {
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
};

// `0x00aea040` / Havok base `0x00c5a1e0` (CONFIRMED, both read in full,
// Sec10.5): "compute an AABB over each wheel's stored ray segment ONLY - no
// radius, no shape reference, no swept-volume term anywhere" - "settling
// raycast-vs-shapecast decisively in favor of a genuine point-sampled
// line-segment ray cast." Reproduced here exactly as that claim states: a
// plain two-point bounding box, nothing added for a wheel radius.
AABB3 ComputeWheelRayAABB(double startX, double startY, double startZ, double endX, double endY, double endZ);

// `0x00aeadb0`/`0x00aea850` (CONFIRMED structure, Sec10.5): "per-wheel ground
// friction comes from a custom Havok body property tagged onto the struck
// body (preferred key 5, fallback key 7, else a locally-computed default) -
// not a materials-table lookup." Modelled as a pluggable property source
// (this library has no real Havok body-property system, Scope discipline) -
// `hasKey5`/`key5Value`/`hasKey7`/`key7Value` stand in for a property lookup
// that either hits or misses.
double SelectWheelFriction(bool hasKey5, double key5Value, bool hasKey7, double key7Value, double localDefault);

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

// `0x00aea160` (CONFIRMED, "the standard two-body point-velocity formula",
// Sec10.5): v_rel = (velB + omegaB x (contactPoint-posB)) - (velA + omegaA x
// (contactPoint-posA)) - "feeds into suspension's 'compression velocity'
// field" (Sec10.4's own compressionVelocity input).
Vec3 ComputeContactRelativeVelocity(Vec3 velA, Vec3 omegaA, Vec3 posA, Vec3 velB, Vec3 omegaB, Vec3 posB,
                                    Vec3 contactPoint);

struct WheelRaycastResult {
    bool hit = false;
    double distanceFraction = 1.0;  // along the ray, [0,1]; 1.0 = no hit (ray end)
};

// `0x00ae9f10` (CONFIRMED structure, Sec10.5): "dispatches per wheel via a
// skip bitmask, a hit/miss branch, and a shared finalize call." The separate
// rotating counter it also advances once per call (not per wheel) is OPEN
// (Sec10.5/10.7: "unidentified round-robin purpose") - NOT modelled here.
void DispatchWheelRaycasts(int wheelCount, uint32_t skipMask, const std::function<WheelRaycastResult(int)>& raycastFn,
                            const std::function<void(int, const WheelRaycastResult&)>& finalizeFn);

// ===========================================================================
// 10.6 Brakes (row 33) and driver input (row 35)
// ===========================================================================

// `0x00ae2c80` (CONFIRMED formula, Sec10.6). SHARPENED (2026-10-08, synced
// commit a5a69cd, spec-physics-runtime.md Sec10.6/10.7's own "replace vs
// wrap" refinement): the override calls Havok's base (`0x00c59e40`, a fully
// self-contained 159-instruction leaf, NOT reproduced here - Scope
// discipline/Sec1 boundary) but does NOT merely "modify" its result -
// Havok's base computes its OWN per-wheel torque (a stall-timer-gated
// contact-speed-threshold computation) into the SAME output array the
// override then UNCONDITIONALLY OVERWRITES wholesale with its own
// independently-computed value (the formula below) - the base's own torque
// is fully computed, then discarded, not blended or modified in place. This
// function therefore reproduces the FULL and ONLY thing that ends up in that
// output array - there is no missing "base contribution" to add on top of
// it, because the override's own replace semantics mean the base's
// contribution to THIS SPECIFIC output never survives at all: torque =
// clamp(contactVelocityProjection * (-0.5/dt) * brakeInput * biasFraction,
// -maxTorqueNm, maxTorqueNm). (The base call's OTHER two outputs - a
// lock-flag array the override's own handbrake pass only ADDITIVELY EXTENDS,
// never replaces, and a stall-timer the override PASSES THROUGH untouched -
// are a different output each, see ApplyHandbrakeLock()'s own doc comment
// immediately below for why this harness's Scope discipline leaves both
// unmodeled rather than inventing the base's own missing lock/stall
// formula.)
double ComputeBrakeTorque(double contactVelocityProjection, double dt, double brakeInput, double biasFraction,
                          double maxTorqueNm);

// `0x00ae2c80`'s own "separate handbrake wheel-lock pass" (CONFIRMED to
// exist, Sec10.6 - exact formula not given beyond "wheel-lock"). SHARPENED
// (2026-10-08, synced commit a5a69cd): this pass is now confirmed to only
// ever SET the real lock-flag array (never clear it), additively extending
// whatever Havok's own base call already set there from its own independent
// per-wheel "below a stall speed threshold" eligibility test (Sec10.6's own
// item 1/2) - i.e. the real per-wheel lock state is base-eligibility OR
// override-handbrake, not override-handbrake alone. This harness does not
// model Havok's base brakes computation at all (Scope discipline, Sec1
// boundary - same reason ComputeBrakeTorque() above never calls a base
// formula either), so there is no base-side "eligibility" signal for this
// function to OR together with the handbrake flag in the first place; the
// CHOSEN concrete model below (full lock when handbrake active, nothing
// otherwise) is therefore still the simplest reading of this function's own
// OWN contribution to that array - not a claim that the real combined
// lock-flag state has no other source, only that this harness's own Scope
// discipline leaves the other (base-side, stall-speed-eligibility) source
// unmodeled, same as it already leaves the stall-timer output (Sec10.6 item
// 4/5, a one-shot stall-entry edge detector the override passes through
// completely untouched) unmodeled - neither is read by anything else in this
// library, so there is nothing for either omission to silently break.
double ApplyHandbrakeLock(bool handbrakeActive, double wheelAngularSpeedRadPerSec);

// `0x00ae30c0` (CONFIRMED formula, Sec10.6): "builds Havok's expected
// analog-input struct straight off the vehicle instance, including a
// quadratic (sign-preserving-squared) throttle response curve by default
// (|sum|*sum), bypassable via instance flag +0x164 bit 1 - not linear."
// `linearOverride` models that instance flag as a caller-supplied bool (the
// flag's exact real-world meaning/trigger is OPEN, Sec10.7's own "+0x164
// (both bits)" entry - only its BYPASS EFFECT here is reproduced, per
// Sec10.6's own words).
double ApplyThrottleResponseCurve(double rawInputSum, bool linearOverride);

// ===========================================================================
// 10.8 The real D-U1 tangential-friction/slip solve (`0x00c63c70` + callees,
// synced 2026-10-08, commit f85467e) - direct follow-up to 10.5's friction-
// coefficient finding and this project's own §6 D-U1 citation.
// ===========================================================================
//
// Sec10.8 CORRECTS the earlier "decay-toward-limit terms and a final
// friction-impulse clamp" framing (D-U0 §6.4 items 10/13's own language, used
// loosely to describe what D-U1 was *believed* to do before it was opened):
// it is not one `min(limit, x)` clamp, it is a 3-stage combined-slip
// friction-ellipse pipeline - (1) per-tangent-axis effective-mass terms,
// (2)-(3) a friction-ellipse apportionment between the two tangent axes via
// a coupling scalar packed into a {1,X,X,1} 2x2 matrix, with that matrix's
// exact closed-form inverse, (4) the real saturation test - the literal unit
// friction circle `(A*B)^2+(C*D)^2 < 1.0` - guarded by a deadzone and a
// +-1.1 overshoot clamp, then a bounded 16-entry table-driven piecewise
// response, (5) a final write of the friction-circle-corrected impulse into
// the SAME chassis force/torque accumulators §6.5 already names.
//
// What is CONFIRMED (Sec10.8) and reproduced EXACTLY below: the two
// reciprocal effective-mass formulas (step 2); the {1,X,X,1} coupling
// matrix's closed-form inverse `invDet*[[1,-X],[-X,1]]`, `det=1-X^2` (step
// 3-4); the literal unit-circle saturation test with comparand exactly 1.0,
// the confirmed 1e-4 deadzone constant, and the confirmed +-1.1 overshoot
// clamp (step 5).
//
// What is OPEN (Sec10.8's own words) and therefore NOT invented here, each
// modelled as an explicit, caller-supplied seam rather than a guess:
//  * the coupling scalar X itself - how the real game computes it is not
//    traced. CHOSEN default 0.0 at every call site in this project (see
//    physics_vehicle_substeps.cpp's ProcessAxle()/Step13): that value
//    degenerates the coupling-inverse to the identity matrix, i.e. the
//    plain *decoupled* unit-circle test - the honest "coupling unknown"
//    case, not a claim that real coupling is actually zero;
//  * the 16-entry table's real sampled values ("would need a live vehicle
//    tuning asset") AND its own indexing/interpolation rule (Sec10.8 only
//    confirms "piecewise" and "16-entry", not how a caller walks it) -
//    modelled as an injectable `FrictionEllipseCurveFn` callback. Passing
//    none (the default, `nullptr`) falls back to the SAME proportional
//    rescale (`scale = limit/magnitude`) this project's own prior
//    `ApplyFrictionCircle()` already used - a graceful, previously-reviewed
//    default, not a guess at the real curve's shape. WIRED-IN UPDATE
//    (2026-10-08, engine-curve/slip-table follow-up task): the table's own
//    CONFIRMED location (16 floats + 2 companion scalars inside the
//    per-wheel tuning record, `a1[wheel]+0x8`..`+0x4C`) now has a concrete,
//    injectable home below - `FrictionSlipTable`/`FrictionSlipTableCurve()` -
//    wired per-axle via `VehicleTuning::AxleAccelTuning::frictionSlipTable`
//    (`std::nullopt` by default, so `ApplyFrictionCircle()`'s own call into
//    `SolveFrictionEllipse()` keeps passing plain `nullptr` and nothing
//    about this project's existing regression behaviour changes unless a
//    caller actively opts a vehicle into a supplied table). The table's
//    real sampled VALUES remain exactly as OPEN as stated above - only the
//    plumbing is new, see `FrictionSlipTable`'s own doc comment for the
//    honestly-flagged pass-through placeholder this defaults to;
//  * exactly where §10.5's per-wheel ground-friction coefficient (body
//    property key 5/fallback key 7/local default, `SelectWheelFriction()`
//    above) enters this specific arithmetic - Sec10.8 itself flags this as
//    "not conclusively traced". This file's own answer: it scales the
//    per-axis LIMIT terms (`axis1Limit`/`axis2Limit` below) - the only seam
//    this model has for a scalar friction coefficient, and consistent with
//    every other confirmed friction-circle-shaped clamp in this project
//    (Sec6.4 items 9/13) already doing exactly that - but this is this
//    file's OWN placement choice, not a confirmed one. Kept injectable
//    (plain function parameters, not a hardcoded internal read) so a future
//    pass that DOES trace the real entry point can change only the call
//    site, not this function's own body.
//
// STILL NOT composed into vehicle_substeps.cpp's per-step force pipeline
// (and flagged here rather than silently omitted): `ComputeTangentEffectiveMass()`
// below. It is a genuine, confirmed, independently-tested building block,
// but turning it into part of an actual impulse computation would require
// carrying real iterative-sequential-impulse-solver state (an accumulated
// relative velocity / running impulse per wheel-axis, re-used and refined
// across many solver iterations) that this project's harness does not have
// at all (vehicle_substeps.cpp computes one force value per sub-step call,
// not an iterative velocity-correction loop) - composing it anyway would be
// INVENTING the missing solver state, not wiring confirmed formulas
// together, so it is deliberately left standalone.
//
// NOW composed (2026-10-08, engine-curve/slip-table follow-up task):
// `ComputeTyreSlipVelocity()` below (the real `0x00c64252`-`0x00c64306`
// slip-velocity computation, CONFIRMED structure) IS wired into
// `ProcessAxle()` in physics_vehicle_substeps.cpp, replacing that function's
// own ad hoc wheel-frame velocity-projection arithmetic with a call into
// this CONFIRMED formula shape - unlike the effective-mass terms above,
// this one needs no persistent solver state at all (it is a stateless
// difference-then-project computation over values this harness already has
// in hand every step), so there was no "missing state" reason to leave it
// standalone. See that function's own doc comment in physics_vehicle_
// substeps.cpp for the exact behaviour-preserving-reformulation argument
// (this harness's own static/flat-ground assumption makes `struckVel`
// identically zero, so the CONFIRMED formula's output is provably identical
// to the old ad hoc arithmetic it replaces - not a silent behaviour change).

struct TangentEffectiveMass {
    double rigid = 0.0;  // 1/(sumSq+1.0)
    double soft = 0.0;   // 1/(sumSq+1.0+wheelExtra)
};

// `0x00c63c70` step 2 (CONFIRMED reciprocal formulas, exact constants
// verified by direct byte read, Sec10.8): `sumSq` is this file's own name
// for whatever sum-of-squares term the real solver feeds in (the spec
// confirms the reciprocal SHAPE, not sumSq's own composition - HIGH, not
// re-derived here); `wheelExtra` is the soft variant's additional per-wheel
// term (same caveat). See this header's own §10.8 banner for why this
// function is implemented+tested but not wired into the top-level force
// pipeline.
TangentEffectiveMass ComputeTangentEffectiveMass(double sumSq, double wheelExtra);

struct FrictionCouplingInverse2x2 {
    double m00 = 1.0, m01 = 0.0, m10 = 0.0, m11 = 1.0;  // identity = decoupled (X=0) default
    bool degenerate = false;  // |couplingX| within degenerateTolerance of 1 (det near 0)
};

// `0x00c63570` (CONFIRMED exact closed form, Sec10.8 step 3-4):
// `invDet*[[1,-X],[-X,1]]`, `det=1-X^2`, tolerance-gated degenerate signal
// when the two tangent axes are too strongly correlated (`|X|` near 1).
// `degenerateTolerance` is CHOSEN (the spec confirms a tolerance gate
// exists, not its value). On a degenerate input this returns the identity
// (fully decoupled) matrix rather than a numerically-exploding inverse -
// CHOSEN safety fallback, not a spec claim.
FrictionCouplingInverse2x2 InvertFrictionCouplingMatrix(double couplingX, double degenerateTolerance = 1e-3);

struct FrictionEllipseResult {
    double axis1Force = 0.0;
    double axis2Force = 0.0;
    bool saturated = false;
};

// A caller-supplied response curve standing in for the confirmed-to-exist,
// OPEN-content 16-entry table (Sec10.8 step 5) - see this header's own
// §10.8 banner. `overshootFraction` is already deadzone/clamp-guarded into
// [0,1] by SolveFrictionEllipse() itself before this callback is invoked;
// the return value is the (CHOSEN-by-the-table, in the real game) force
// rescale factor to apply. An empty/null function means "no real table
// supplied" - see SolveFrictionEllipse()'s own doc comment for its fallback.
using FrictionEllipseCurveFn = std::function<double(double overshootFraction)>;

// The per-axle 16-entry friction/slip table read by the saturation routine
// `0x00c63930` (spec-physics-runtime.md 10.11.6, FINAL for the lookup rule,
// 2026-10-14; supersedes the earlier 10.8 "OPEN" wording). Axle description
// layout: 16 floats at +0x00..+0x3C, step `q` at desc +0x40 (a1-relative
// +0x48), start `x/16` at desc +0x44 (a1-relative +0x4C). The earlier labels
// here ("boundary" at +0x48, "multiplier" at +0x4C) were swapped; the spec's
// correction is: +0x48 = q, +0x4C = start. Values are built from data by
// BuildFrictionSlipTableFromSpec() (vehicle_spec_data.h).
struct FrictionSlipTable {
    std::array<double, 16> entries{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
                                   1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};  // placeholder until built from data
    double q = 1.0;      // desc +0x40 (a1+0x48): ladder step, (16/x)^(1/15)
    double start = 0.0;  // desc +0x44 (a1+0x4C): ladder start, x/16
};

// Result of the 10.11.6 search. `belowCircle` = A^2 + B^2 <= 1 (wheel is
// inside the friction circle: nothing happens, wheel +0x98 is set to 0).
struct FrictionSlipLookup {
    bool belowCircle = true;
    int stopIndex = -1;  // first i with S_i > 1.0 (0..15)
    double t = 0.0;      // secant parameter (1 - pr) / max(r - pr, 1.19e-7)
    double u = 0.0;      // u*, clamped to [-1.1, 1.1]
    double v = 0.0;      // v*
};

// 10.11.6 PORT RULE, implemented exactly: sequential first-exceed search,
// u_i = A*start*q^i, v_i = B*table[i], S_i = u_i^2 + v_i^2 > 1; secant
// interpolation between sample i and i-1 (for i = 0 the previous sample is
// u = v = 0, S = 0), denominator floored at 1.19e-7, u* clamped to +-1.1.
// NOT precomputed from slip. Not decoded by the spec: which physical axis A
// and B are, and how (u*, v*) becomes a force change - so no caller applies
// the result to a force yet (SolveFrictionEllipse keeps its own fallback).
FrictionSlipLookup LookupFrictionSlipTable(const FrictionSlipTable& table, double A, double B);


constexpr double kFrictionEllipseDeadzone = 1e-4;        // CONFIRMED literal, Sec10.8 step 5
constexpr double kFrictionEllipseOvershootClamp = 1.1;   // CONFIRMED literal, Sec10.8 step 5
// CONFIRMED to exist as a guard constant (Sec10.8 step 5: "guarded by
// confirmed FLT_EPSILON... constants"); the standard IEEE-754 single-
// precision epsilon value, spelled out as a literal rather than relying on
// a platform FLT_EPSILON macro (this project's own established convention,
// e.g. physics_vehicle_substeps.cpp's local `kPi`).
constexpr double kFrictionEllipseEpsilonGuard = 1.1920929e-7;

// `0x00c63930` (CONFIRMED structure, Sec10.8 step 5): the real combined-slip
// saturation test against the unit friction circle/ellipse,
// `(A*B)^2+(C*D)^2 < 1.0` (comparand CONFIRMED exactly 1.0), preceded by the
// coupling-matrix transform (steps 3-4) applied to the raw per-axis force
// pair - CHOSEN ORDER (apply-coupling-then-test), the only reading
// consistent with the coupling matrix being described as how the test's own
// terms are built ("apportionment... via a coupling scalar"), not a separate
// later step. See this header's own §10.8 banner for the full CONFIRMED-vs-
// CHOSEN/OPEN breakdown of every argument below (`axis1Limit`/`axis2Limit`
// is this file's own placement of §10.5's OPEN friction-coefficient entry
// point; `curveFn` is the OPEN 16-entry table's own injectable seam).
FrictionEllipseResult SolveFrictionEllipse(double axis1ForceIn, double axis2ForceIn, double axis1Limit,
                                           double axis2Limit, const FrictionCouplingInverse2x2& couplingInverse,
                                           const FrictionEllipseCurveFn& curveFn = nullptr);

// `0x00c64252`-`0x00c64306` (CONFIRMED, address-pinned, inside this same
// solve's own "Loop 2" - Sec10.8, synced 2026-10-08): the real tyre
// slip-velocity computation this combined-slip friction solve itself
// consumes - confirmed to be a genuinely SEPARATE computation from §10.5's
// `ComputeContactRelativeVelocity()` above (that one feeds suspension's
// compression-velocity field instead; this settles the question §10.5 left
// hedged, "believed to live in the D-U1 cluster" - it does, specifically
// here, not there). Differences the chassis's own force-accumulator-shaped
// velocity field (`a2+0xe0..0xe8`, this file's own `chassisVel` name)
// against the struck/ground object's own cached linear velocity (a FIXED,
// non-wheel-looped pointer, `a2+0x30` - this file's own `struckVel` name),
// then projects the difference onto the wheel's two tangent-basis rows
// (longitudinal, lateral) to get the raw slip-velocity pair - all CONFIRMED
// structure. CHOSEN subtraction order (`chassisVel - struckVel`): the
// spec's own prose reads "differences [the chassis velocity] against [the
// struck velocity]", read here as the natural A-against-B = A-B parse; the
// spec gives no named-variable formula to check this against, so the order
// is flagged CHOSEN rather than claimed CONFIRMED (it only flips the
// overall sign of both slip components, not this function's own internal
// correctness).
struct TyreSlipVelocity {
    double longitudinal = 0.0;
    double lateral = 0.0;
};

// The raw projection is then clamped against a SECOND, previously-
// undocumented per-wheel limit (CONFIRMED to exist, Sec10.8: "`a2+0x38/
// +0x3c/+0x40`, read just ahead of the per-wheel record array" - three
// scalars for two slip components). CHOSEN apportionment of those 3 scalars
// (the spec itself does not say which scalar gates which operation):
// `longitudinalLimit`/`lateralLimit` each symmetrically clamp their own
// component first (the simplest reading of "a... limit" applied per-axis);
// `magnitudeLimit` then additionally clamps the combined vector's own
// magnitude, proportionally rescaling both components together - giving
// all 3 CONFIRMED-to-exist scalars a role instead of silently dropping the
// third. Any limit <=0.0 disables that specific clamp stage (this project's
// own established "0 means off" idiom, e.g. `SteeringBaseTuning::
// speedThreshold` above) - this library has no real source for these 3
// scalars' concrete magnitudes (OPEN, Sec10.8), so a caller that cannot
// supply them gets the un-clamped raw projection instead of an invented
// bound.
TyreSlipVelocity ComputeTyreSlipVelocity(Vec3 chassisVel, Vec3 struckVel, Vec3 tangentLongitudinal,
                                         Vec3 tangentLateral, double longitudinalLimit = 0.0,
                                         double lateralLimit = 0.0, double magnitudeLimit = 0.0);

}  // namespace sr3physics
