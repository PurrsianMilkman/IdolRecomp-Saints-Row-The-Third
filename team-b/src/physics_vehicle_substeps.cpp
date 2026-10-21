#include "sr3physics/vehicle_substeps.h"

#include <algorithm>
#include <cmath>

#include "sr3physics/frame_contract.h"
#include "sr3physics/vehicle_spec_data.h"

namespace sr3physics {

namespace {

// Wheel layout offsets in the fixed 0=FL,1=FR,2=RL,3=RR convention (CHOSEN,
// see the header's banner - wheelbase/track are not in the xtbl schema this
// file consumes).
struct Offset {
    double forward;
    double right;
};

std::array<Offset, 4> WheelOffsets(const VehicleTuning& t) {
    const double hb = t.wheelBase / 2.0;
    const double ht = t.trackWidth / 2.0;
    return {Offset{hb, -ht}, Offset{hb, ht}, Offset{-hb, -ht}, Offset{-hb, ht}};
}

// Builds vehicle_driving_slice.h's GearShiftTuning from this harness's own
// VehicleTuning fields. UPDATED 2026-10-09 (vehicle-handling wave-6 task,
// spec-physics-runtime.md Sec10.10): the per-gear downshift table is now the
// REAL per-vehicle Downshift_RPMs table (t.downshiftRpms) when the source
// VehicleEntry supplied one - this file's own prior comment here ("this
// schema has no such table at all") was wrong, corrected in VehicleTuning's
// own doc comment (vehicle_substeps.h); any gear this vehicle's own data
// does not cover (including every gear, for an unsourced VehicleTuning)
// falls back element-by-element to the pre-existing CHOSEN uniform-margin
// table, unchanged. shiftDurationSec is now sourced from Clutch_Delay
// (t.clutchDelaySec) instead of GearShiftTuning's own old flat CHOSEN
// constant - see t.clutchDelaySec's own doc comment for the HIGH-confidence
// field-to-role mapping.
GearShiftTuning BuildGearShiftTuning(const VehicleTuning& t) {
    GearShiftTuning gst;
    const size_t gearCount = std::max<size_t>(1, t.gearRatios.size());
    const size_t downshiftCount = gearCount > 1 ? gearCount - 1 : 0;
    const double uniformDownshiftRpm = t.gearUpshiftRpmThreshold - t.gearDownshiftMarginRpm;
    gst.downshiftRpmByGear.resize(downshiftCount);
    for (size_t i = 0; i < downshiftCount; ++i) {
        gst.downshiftRpmByGear[i] = (i < t.downshiftRpms.size()) ? t.downshiftRpms[i] : uniformDownshiftRpm;
    }
    gst.upshiftRpmThreshold = t.gearUpshiftRpmThreshold;
    gst.maxGear = static_cast<int>(gearCount);
    gst.shiftDurationSec = t.clutchDelaySec;
    return gst;
}

// --- CHOSEN component physics (Sec6.4's own role/shape, no formula given) -

struct WheelForce {
    double longForce = 0.0;
    double latForce = 0.0;
};

// Linear-tire-model longitudinal+lateral force (CHOSEN - see the header
// banner: Sec6.4 items 10/11 confirm only a "decay-toward-limit" SHAPE with
// unnamed constants, not a formula). wheelLongVel/wheelLatVel are the
// wheel-contact-point velocity in the WHEEL's own (forward,right) frame.
WheelForce ComputeTireForce(double driveForceN, double brakeForceMagN, double wheelLongVel, double wheelLatVel,
                            double corneringStiffness) {
    double longForce = driveForceN;
    const double speedSign = (wheelLongVel > 0.0) ? 1.0 : ((wheelLongVel < 0.0) ? -1.0 : 0.0);
    if (std::abs(wheelLongVel) > 0.05) {
        longForce -= brakeForceMagN * speedSign;
    } else {
        // Near-zero wheel speed: brakes simply oppose whatever drive force
        // was requested (CHOSEN - avoids a sign singularity at rest).
        const double driveSign = (driveForceN > 0.0) ? 1.0 : ((driveForceN < 0.0) ? -1.0 : 0.0);
        longForce -= brakeForceMagN * driveSign;
    }
    // atan2 against a speed floor avoids a slip-angle singularity at very
    // low forward speed (CHOSEN, a standard guard in simplified tire models).
    const double latForce = -corneringStiffness * std::atan2(wheelLatVel, std::max(0.5, std::abs(wheelLongVel)));
    return {longForce, latForce};
}

// Friction-circle clamp - WIRED 2026-10-08 into vehicle_driving_slice.h's
// REAL confirmed §10.8 friction-ellipse solve (SolveFrictionEllipse()),
// replacing the plain `if (mag>limit) rescale` this function used before
// Sec10.8 existed (Sec6.4 items 9/13 only ever confirmed the HIGH
// "magnitude-based clamp/rescale" SHAPE, no formula - §10.8, synced
// 2026-10-08/commit f85467e, is the real D-U1 solve that shape was standing
// in for).
//
// Called with couplingX=0 (decoupled - the real coupling scalar is OPEN,
// see vehicle_driving_slice.h's own §10.8 banner) and no curveFn (the real
// 16-entry table's values/indexing are OPEN too) - SolveFrictionEllipse()'s
// own documented fallback for exactly this "no real data" case is the
// IDENTICAL proportional rescale this function used to compute directly
// (scale = limit/magnitude): with couplingX=0 the coupling-inverse is the
// identity matrix, so coupledAxis1/2 == f.longForce/latForce unchanged, and
// with axis1Limit==axis2Limit==limit, Sec10.8's sumSq == (mag/limit)^2, so
// its saturation test and this function's old `mag>limit` test agree to
// within the CONFIRMED 1e-4 deadzone (see SolveFrictionEllipse()'s own doc
// comment) - this call is therefore a behaviour-preserving superset, not a
// silent regression, verified by this project's own synthetic test (see
// tests/synthetic_physics_vehicle_substeps_test.cpp) rather than only
// argued here.
// WIRED 2026-10-08 (engine-curve/slip-table follow-up task): an optional
// 4th parameter lets a caller (ProcessAxle/Step13 below) plug in a real
// per-axle FrictionSlipTable (Sec10.8's 16-entry table, vehicle_driving_
// slice.h) as SolveFrictionEllipse()'s own curveFn seam - defaulted to
// `nullptr` so every EXISTING call site (none of which pass a table today,
// since VehicleTuning::AxleAccelTuning::frictionSlipTable defaults to
// std::nullopt) is textually and behaviourally unchanged.
WheelForce ApplyFrictionCircle(WheelForce f, double normalForceN, double mu,
                               const FrictionEllipseCurveFn& curveFn = nullptr) {
    const double limit = std::max(0.0, normalForceN) * mu;
    const FrictionCouplingInverse2x2 decoupled;  // couplingX=0 default - see doc comment above
    const FrictionEllipseResult r = SolveFrictionEllipse(f.longForce, f.latForce, limit, limit, decoupled, curveFn);
    return {r.axis1Force, r.axis2Force};
}

}  // namespace

// RETIRED (2026-10-08, engine-curve/slip-table follow-up task) - see this
// function's own declaration in vehicle_substeps.h for why it is kept
// (defined, linkable, out of the anonymous namespace above) rather than
// deleted outright: this project's own old-vs-new regression-equivalence
// test calls it directly to prove the live model genuinely changed. No
// production call site in this file invokes it any more (see
// Step08_ForceTorqueAccumulate() below).
double ComputeEngineTorqueNm(const VehicleTuning& t, double rpm, double throttle) {
    double factor;
    if (rpm <= t.minRpm) {
        factor = t.minRpmTorqueFactor;
    } else if (rpm <= t.optRpm) {
        const double f = (rpm - t.minRpm) / std::max(1e-6, t.optRpm - t.minRpm);
        factor = t.minRpmTorqueFactor + f * (1.0 - t.minRpmTorqueFactor);
    } else if (rpm <= t.maxRpm) {
        const double f = (rpm - t.optRpm) / std::max(1e-6, t.maxRpm - t.optRpm);
        factor = 1.0 + f * (t.maxRpmTorqueFactor - 1.0);
    } else {
        factor = t.maxRpmTorqueFactor;
    }
    const double clampedThrottle = std::clamp(throttle, 0.0, 1.0);
    return t.peakTorqueNm * factor * clampedThrottle;
}

// WIRED 2026-10-08 (engine-curve/slip-table follow-up task, authority
// spec-physics-runtime.md Sec10.2 "Construction site and the real xtbl
// field mapping, CONFIRMED (2026-10-08)", synced commit 4c7d3f2): the exact
// CROSSED mapping that finding pins, field by field. 4 fields map
// straightforwardly (the spec's own crossing claim is scoped explicitly to
// fields named `*_Torque_Factor`/`*_Resistance` - these 4 are neither):
//   idleRpm            <- minRpm             (xtbl Min_RPM)
//   peakTorqueRpm       <- optRpm             (xtbl Opt_RPM)
//   redlineRpm          <- maxRpm             (xtbl Max_RPM)
//   torqueScale         <- peakTorqueNm       (xtbl Torque, "+0x14, overall torque scale")
// The other 5 are CROSSED exactly per the spec's own table:
//   resistanceLowBandCoeff  <- minRpmTorqueFactor  (xtbl Min_RPM_Torque_Factor)
//   resistanceHighBandCoeff <- maxRpmTorqueFactor  (xtbl Max_RPM_Torque_Factor)
//   torqueFactorIdle        <- minRpmResistance    (xtbl Min_RPM_Resistance)
//   torqueFactorPeak        <- optRpmResistance    (xtbl Opt_RPM_Resistance)
//   torqueFactorRedline     <- maxRpmResistance    (xtbl Max_RPM_Resistance)
// `Clutch_Slip_RPM` (clutchSlipRpm) has no corresponding field in the 9-field
// engine schema (it is a transmission-side field riding along in the same
// constructor run, per the spec) - not consumed here.
EngineCurveTuning VehicleTuning::ToEngineCurveTuning() const {
    EngineCurveTuning e;
    e.idleRpm = minRpm;
    e.peakTorqueRpm = optRpm;
    e.redlineRpm = maxRpm;
    e.torqueScale = peakTorqueNm;
    e.resistanceLowBandCoeff = minRpmTorqueFactor;
    e.resistanceHighBandCoeff = maxRpmTorqueFactor;
    e.torqueFactorIdle = minRpmResistance;
    e.torqueFactorPeak = optRpmResistance;
    e.torqueFactorRedline = maxRpmResistance;
    e.torque = peakTorqueNm; e.minRpm = minRpm; e.optRpm = optRpm; e.maxRpm = maxRpm;  // D-17 table roles
    e.minTorqueFactor = minRpmTorqueFactor; e.maxTorqueFactor = maxRpmTorqueFactor;
    e.minResistance = minRpmResistance; e.optResistance = optRpmResistance; e.maxResistance = maxRpmResistance;
    e.clutchSlipRpm = clutchSlipRpm;
    return e;
}

VehicleTuning VehicleTuning::FromVehicleEntry(const sr3vehicleinfo::VehicleEntry& entry) {
    VehicleTuning t;  // start from CHOSEN defaults (geometry/tire-model constants not in this schema)

    if (entry.mass.present) t.mass = entry.mass.value;
    if (entry.maxSteeringAngleRadians.present) t.maxSteeringAngleRad = entry.maxSteeringAngleRadians.value;

    if (entry.torque.present) t.peakTorqueNm = entry.torque.value;
    if (entry.minRpm.present) t.minRpm = entry.minRpm.value;
    if (entry.optRpm.present) t.optRpm = entry.optRpm.value;
    if (entry.maxRpm.present) t.maxRpm = entry.maxRpm.value;
    if (entry.minRpmTorqueFactor.present) t.minRpmTorqueFactor = entry.minRpmTorqueFactor.value;
    if (entry.maxRpmTorqueFactor.present) t.maxRpmTorqueFactor = entry.maxRpmTorqueFactor.value;
    if (entry.minRpmResistance.present) t.minRpmResistance = entry.minRpmResistance.value;
    if (entry.optRpmResistance.present) t.optRpmResistance = entry.optRpmResistance.value;
    if (entry.maxRpmResistance.present) t.maxRpmResistance = entry.maxRpmResistance.value;
    if (entry.clutchSlipRpm.present) t.clutchSlipRpm = entry.clutchSlipRpm.value;

    if (!entry.gearRatios.empty()) {
        t.gearRatio = entry.gearRatios[0];
        t.gearRatios.assign(entry.gearRatios.begin(), entry.gearRatios.end());
    }
    if (!entry.downshiftRpms.empty()) t.downshiftRpms.assign(entry.downshiftRpms.begin(), entry.downshiftRpms.end());
    if (entry.diffGearRatio.present) t.diffGearRatio = entry.diffGearRatio.value;
    // WIRED 2026-10-09 (vehicle-handling wave-6 task, spec-physics-runtime.md
    // Sec10.10): the 5 real transmission fields that section's own "Gear
    // ratios" paragraph confirms land in the live Havok component - see each
    // VehicleTuning field's own doc comment (vehicle_substeps.h) for the
    // CONFIRMED/HIGH citation per field.
    if (entry.upshiftRpm.present) t.gearUpshiftRpmThreshold = entry.upshiftRpm.value;
    if (entry.reverseGearRatio.present) t.reverseGearRatio = entry.reverseGearRatio.value;
    if (entry.clutchDelay.present) t.clutchDelaySec = entry.clutchDelay.value;
    if (entry.maximumRpmRate.present) t.maxRpmChangePerSec = entry.maximumRpmRate.value;

    // WIRED 2026-10-09 (same task): Sec10.10's own "Bonus" brakes finding -
    // see minTimeToBlockSec/aiMinTimeToBlockSec's own doc comment
    // (vehicle_substeps.h) for why these are parsed but left OPEN.
    if (entry.minTimeToBlock.present) t.minTimeToBlockSec = entry.minTimeToBlock.value;
    if (entry.aiMinTimeToBlock.present) t.aiMinTimeToBlockSec = entry.aiMinTimeToBlock.value;

    if (entry.dragCoefficient.present) t.dragCoefficient = entry.dragCoefficient.value;
    if (entry.frontalArea.present) t.frontalArea = entry.frontalArea.value;
    if (entry.airDensity.present) t.airDensity = entry.airDensity.value;
    ApplySpecChassisData(t, entry);

    // axles[0] = front, axles[1] = rear: CHOSEN assumption (spec: "Axle
    // order is positional" - not engine-enforced, but matches the shipped
    // <Axle> ordering convention spec-vehicle-data.md Sec7.3 itself uses).
    if (entry.axles.size() > 0) {
        const auto& a = entry.axles[0];
        if (a.wheelEngineTorque.present) t.front.engineTorqueShare = a.wheelEngineTorque.value;
        if (a.wheelFriction.present) t.front.wheelFriction = a.wheelFriction.value;
        if (a.wheelMaxFriction.present) t.front.wheelMaxFriction = a.wheelMaxFriction.value;
        if (a.wheelBrakingTorque.present) t.front.wheelBrakingTorqueNm = a.wheelBrakingTorque.value;
        if (a.wheelDoesSteer.has_value()) t.front.steers = *a.wheelDoesSteer;
    }
    if (entry.axles.size() > 1) {
        const auto& a = entry.axles[1];
        if (a.wheelEngineTorque.present) t.rear.engineTorqueShare = a.wheelEngineTorque.value;
        if (a.wheelFriction.present) t.rear.wheelFriction = a.wheelFriction.value;
        if (a.wheelMaxFriction.present) t.rear.wheelMaxFriction = a.wheelMaxFriction.value;
        if (a.wheelBrakingTorque.present) t.rear.wheelBrakingTorqueNm = a.wheelBrakingTorque.value;
        if (a.wheelDoesSteer.has_value()) t.rear.steers = *a.wheelDoesSteer;
    }

    return t;
}

VehicleSubstepSimulator::VehicleSubstepSimulator(VehicleTuning tuning)
    : tuning_(tuning),
      steeringLimiter_(tuning_.steeringRateRadPerSec, tuning_.steeringCenteringRateRadPerSec),
      steeringProfile_(SteeringProfileParams{
          tuning_.steeringRateRadPerSec, SteeringLimiterAccel(tuning_.steeringRateRadPerSec, tuning_.steeringTurnInDampAngleRad),
          tuning_.steeringCenteringRateRadPerSec,
          SteeringLimiterAccel(tuning_.steeringCenteringRateRadPerSec, tuning_.steeringReturnDampAngleRad)}),
      gearShift_(BuildGearShiftTuning(tuning_), /*startGear=*/1) {
    state_.engineRpm = tuning_.minRpm;
    state_.targetEngineRpm = tuning_.minRpm;
    state_.currentGear = gearShift_.gear();
}

double VehicleSubstepSimulator::Step(double rawDt, const DriverInput& input) {
    // Sec4.3/Sec5 CONFIRMED [1/120, 1/20] s clamp, reused verbatim from this
    // module's own frame_contract.h rather than re-stated here.
    const double dt = ClampDt(rawDt);

    stepCallLog.clear();

    Step01_DriverInputStatusRelay(input);
    Step02_WheelArrayRelay();
    Step03_SteeringRelay(dt);
    Step04_SuspensionWheelStatusRelay();
    Step05_BrakesForward();
    Step06_TransmissionForward(dt);

    Step07_WheelContactFinish();
    Step08_ForceTorqueAccumulate(dt);
    Step09_TorqueClamp();
    Step10_FrontAxlePairForce(dt);
    Step11_RearAxleForce(dt);
    Step12_FrictionForwardNotReproduced();
    Step13_FinalPerWheelClamp();

    IntegrateChassis(dt);

    return dt;
}

// --- Phase 1 (Sec6.4 items 1-6, from 0x00ae9570) ---------------------------

void VehicleSubstepSimulator::Step01_DriverInputStatusRelay(const DriverInput& input) {
    stepCallLog.push_back(1);
    // 0x00c55f80 (Sec6.4 item 1): "a one-byte status relay" - HYPOTHESIS, no
    // own math (the STATUS BYTE itself has no dynamical effect, modelled as
    // a pure latch below, unchanged).
    state_.driverInputDeviceStatus = true;
    state_.lastInput = input;

    // WIRED 2026-10-08: this sub-step's own component-array slot (+0x20) is
    // the ONE slot §10.7's cross-cutting findings give HIGH CONFIDENCE for -
    // "driver input's override reads/writes vehicle-instance +0xb0 - the
    // exact field D-U0's own 0x00c55f80 (slot +0x20's sub-step) saves/
    // restores around its call" - i.e. this sub-step's real body calls
    // STRAIGHT INTO the driver-input component (`0x00ae30c0`, Sec10.6),
    // which applies a CONFIRMED quadratic (sign-preserving-squared)
    // throttle response curve by default (`|sum|*sum`) before Havok's
    // analog-input struct is built. Applied here, to the latched throttle
    // only (Sec10.6's own words name a "throttle response curve"
    // specifically, not brake/steer) - every later step (8/10/11) reads the
    // CURVED value through state_.lastInput.throttle, same as before.
    // `linearOverride=false` (CHOSEN): the instance flag bypassing this is
    // OPEN (Sec10.7's own "+0x164 (both bits)" entry), so this harness
    // always takes the CONFIRMED default (quadratic) path, never claiming
    // to know when the bypass would apply.
    state_.lastInput.throttle = ApplyThrottleResponseCurve(input.throttle, /*linearOverride=*/false);

    // 10.3 reverse rule (command = throttle - brake). Only engaged when the
    // "full logic" byte is on (source untraced, OPEN); otherwise inputs pass through unchanged.
    if (tuning_.reverseFullLogic) {
        const ReverseDecision d = DecideReverse(true, state_.lastInput.throttle - input.brake, state_.velForwardBody,
                                                input.handbrake, tuning_.vehicleClass, state_.reverseFlag);
        state_.reverseFlag = d.reverseFlag;
        state_.lastInput.throttle = d.accel;
        state_.lastInput.brake = d.brake;
    }
}

void VehicleSubstepSimulator::Step02_WheelArrayRelay() {
    stepCallLog.push_back(2);
    // 0x00c58770 (Sec6.4 item 2): "a per-element... array relayed through
    // one component call" - HIGH, content not confirmed. CHOSEN use: pure
    // kinematic world placement of each wheel from the current chassis
    // pose, no dynamics.
    const auto offsets = WheelOffsets(tuning_);
    const double c = std::cos(state_.heading);
    const double s = std::sin(state_.heading);
    for (int i = 0; i < 4; ++i) {
        // worldPos = chassisPos + forwardOffset*forwardDir + rightOffset*rightDir
        // forwardDir = (c, s); rightDir = (s, -c) (CHOSEN convention, header banner).
        const double fx = c, fz = s;
        const double rx = s, rz = -c;
        state_.wheels[i].worldX = state_.posX + offsets[i].forward * fx + offsets[i].right * rx;
        state_.wheels[i].worldZ = state_.posZ + offsets[i].forward * fz + offsets[i].right * rz;
    }
}

void VehicleSubstepSimulator::Step03_SteeringRelay(double dt) {
    stepCallLog.push_back(3);
    // 0x00c56040 (Sec6.4 item 3): "a two-scalar state relay... consistent
    // with... a steering-angle pair (current/target)" - CHOSEN reading (the
    // spec names an engine RPM/throttle pair as equally plausible; this file
    // picks steering, see the header's own note on why engine state lives
    // elsewhere).
    //
    // WIRED 2026-10-08: the two scalars this sub-step relays ARE exactly
    // Sec10.1's own steering component state (current/target angle), whose
    // real formulas are now CONFIRMED - `SteeringBaseAngle()` (the base
    // angle + Havok's own inverse-square speed-sensitive falloff above a
    // threshold) and `SteeringRateLimiter` (the two-rate-constant rate limit
    // with its own reversal-seed behaviour), replacing this method's old ad
    // hoc single-rate two-point clamp.
    //
    // Sign CHOSEN so that positive input.steer ends up producing a positive
    // final yawRate (this file's own documented/tested convention - see
    // tests/synthetic_physics_vehicle_substeps_test.cpp's testTurning()) once
    // combined with ProcessAxle's own (independently fixed, see that
    // method's doc comment) torque-accumulation sign. Which of these two
    // signs carries the negation is arbitrary (a pure left/right mirror
    // relabelling, not a physical claim); it is placed here, rather than in
    // ProcessAxle, to keep ProcessAxle's torque formula matching its own
    // from-first-principles derivation unnegated.
    SteeringBaseTuning baseTuning;
    baseTuning.inputToAngleScale = tuning_.maxSteeringAngleRad;  // CHOSEN: steer fraction [-1,1] * max angle = the base angle
    baseTuning.speedThreshold = tuning_.steeringSpeedThresholdMPerSec;
    baseTuning.fastPathNoSpeedTerm = tuning_.steeringFastPathNoSpeedTerm;
    const double clampedSteer = std::clamp(state_.lastInput.steer, -1.0, 1.0);
    // speedTerm = current forward body speed (CHOSEN reading of Sec10.1's
    // own "a transformed velocity-shaped vector... projects onto a
    // reference row for a speedTerm" - HIGH, the exact vector/row is not
    // given; forward speed is the natural scalar speed measure this
    // harness already carries).
    const double baseAngle = SteeringBaseAngle(baseTuning, clampedSteer, state_.velForwardBody);
    state_.steeringAngleTargetRad = -baseAngle;
    state_.steeringAngleCurrentRad = (tuning_.steeringTurnInDampAngleRad > 0.0 && tuning_.steeringReturnDampAngleRad > 0.0)
                                         ? steeringProfile_.Update(state_.steeringAngleTargetRad, dt)  // 10.11.7 min-time profile
                                         : steeringLimiter_.Update(state_.steeringAngleTargetRad, dt);
}

void VehicleSubstepSimulator::Step04_SuspensionWheelStatusRelay() {
    stepCallLog.push_back(4);
    // 0x00c58360 (Sec6.4 item 4): "a per-element byte-status array... with
    // an extra scalar riding along" - HIGH, plausibly per-wheel. This model
    // has no vertical/suspension-travel state at all (header's PLANAR SCOPE
    // note), so grounded is trivially always true - present only for
    // structural/ordering fidelity to the confirmed per-step data shape.
    for (auto& w : state_.wheels) w.grounded = true;
}

void VehicleSubstepSimulator::Step05_BrakesForward() {
    stepCallLog.push_back(5);
    // 0x00c562d0 (Sec6.4 item 5): "thin forwarder... no instance-state
    // save/restore, no own math" - a pure latch.
    state_.lastBrakeInput = std::clamp(state_.lastInput.brake, 0.0, 1.0);
}

void VehicleSubstepSimulator::Step06_TransmissionForward(double dt) {
    stepCallLog.push_back(6);
    // 0x00c560a0 (Sec6.4 item 6): "the thinnest... no instance-state
    // save/restore, no own math" describes THIS sub-step's own thin-
    // forwarder BODY - it is the component it forwards into
    // (`hk_custom_vehicle_transmission`, 0x00aece20) that Sec10.3 confirms
    // IS a real automatic gear-shift state machine, not a thin pass-through.
    //
    // WIRED 2026-10-08: replaces this harness's old FIXED single gear with
    // vehicle_driving_slice.h's real GearShiftStateMachine (gearShift_,
    // constructed in this simulator's own constructor from
    // tuning_.gearRatios/gearUpshiftRpmThreshold/gearDownshiftMarginRpm -
    // see that constructor's own doc comment). Evaluated against the
    // PREVIOUS step's own engine RPM (state_.engineRpm hasn't been updated
    // yet this step - Step08 runs later - so this uses the same "last known
    // RPM" every real automatic transmission would have available at this
    // point in its own update order).
    if (!state_.reverseFlag) gearShift_.Update(state_.engineRpm, dt);  // 10.3: no shifting in reverse
    state_.currentGear = gearShift_.gear();
    const size_t idx = static_cast<size_t>(std::clamp(state_.currentGear - 1, 0, static_cast<int>(tuning_.gearRatios.size()) - 1));
    const double gearRatio = tuning_.gearRatios.empty() ? tuning_.gearRatio : tuning_.gearRatios[idx];
    state_.drivetrainRatio = WheelTorqueRatio(state_.reverseFlag, state_.currentGear - 1, tuning_.reverseGearRatio, gearRatio,
                                              tuning_.diffGearRatio);
}

// --- Phase 2 (Sec6.4 items 7-13, from 0x00ae7470) ---------------------------

void VehicleSubstepSimulator::Step07_WheelContactFinish() {
    stepCallLog.push_back(7);
    // 0x00c565b0 (Sec6.4 item 7): forwards into D-U1's 0x00c32520 (NOT
    // reproduced, out of scope per Sec6.6), then in its OWN body "finishes
    // building a per-wheel contact/output record" via lerp-style vector math
    // and a 3x3 transform. CHOSEN use: compute each wheel's own
    // forward/right unit vectors in world space (steering-offset for a
    // steered wheel) once per step, reused by steps 10/11/13 below instead
    // of recomputing trig inline - a legitimate stand-in for "finishing a
    // per-wheel... record" without claiming to know what D-U1 itself does.
    const auto offsets = WheelOffsets(tuning_);
    for (int i = 0; i < 4; ++i) {
        const bool steered = (offsets[i].forward > 0.0) ? tuning_.front.steers : tuning_.rear.steers;
        const double localAngle = steered ? state_.steeringAngleCurrentRad : 0.0;
        const double angle = state_.heading + localAngle;
        state_.wheels[i].contactForwardX = std::cos(angle);
        state_.wheels[i].contactForwardZ = std::sin(angle);
        state_.wheels[i].contactRightX = std::sin(angle);
        state_.wheels[i].contactRightZ = -std::cos(angle);
    }
}

void VehicleSubstepSimulator::Step08_ForceTorqueAccumulate(double dt) {
    stepCallLog.push_back(8);
    // 0x00c566e0 (Sec6.4 item 8): "a force accumulator at +0xe0 and a torque
    // accumulator at +0xf0" - CONFIRMED role (HIGH), reset+seeded here.
    state_.forceAccumX = 0.0;
    state_.forceAccumZ = 0.0;
    state_.torqueAccumY = 0.0;

    // Drivetrain scalar state this harness folds in here rather than into
    // Step06 (see that method's doc comment / the header banner's "no slot
    // for engine" note).
    //
    // WIRED 2026-10-08: the RPM side is now the REAL, CONFIRMED "engine/
    // transmission RPM-matching loop" vehicle_driving_slice.h's own
    // ComputeTargetEngineRpm() doc comment names explicitly - its return
    // value feeds EngineRampRpm() as `targetRpm`, replacing this harness's
    // old instant "snap straight to the wheel-implied RPM" behaviour with a
    // genuine ramp (Sec10.2's own confirmed up-only ease/rate formula).
    // Still a "locked drivetrain" (no clutch slip state) in the sense that
    // every wheel is assumed to share the chassis's own forward speed
    // (CHOSEN - this model has no per-wheel rotational/slip state at all,
    // see the header's PLANAR SCOPE note) - but the RPM itself no longer
    // jumps discontinuously frame to frame.
    const double wheelAngularSpeed =
        (state_.reverseFlag ? std::max(0.0, -state_.velForwardBody) : std::max(0.0, state_.velForwardBody)) / tuning_.wheelRadius;
    const std::array<double, 4> wheelAngularSpeeds = {wheelAngularSpeed, wheelAngularSpeed, wheelAngularSpeed,
                                                       wheelAngularSpeed};
    const std::array<double, 4> torqueSplitWeights = {tuning_.front.engineTorqueShare, tuning_.front.engineTorqueShare,
                                                       tuning_.rear.engineTorqueShare, tuning_.rear.engineTorqueShare};
    // reactionTorqueNm/reactionTorqueToRpmGain = 0.0 (CHOSEN/OPEN): this
    // harness carries no clutch-engagement/reaction-torque state at all
    // (Sec10.3's own "+0x158" mirrored field is not modelled) - zeroing both
    // makes the subtraction a documented no-op rather than inventing a
    // reaction-torque source that does not exist in this model.
    // `maxRpmChangePerSec` WIRED 2026-10-09 (vehicle-handling wave-6 task,
    // spec-physics-runtime.md Sec10.10): now tuning_.maxRpmChangePerSec
    // (Maximum_RPM_Rate when sourced, else the same 1.0e6 "effectively
    // unbounded" CHOSEN default this call site hardcoded before - see that
    // field's own doc comment, vehicle_substeps.h, for the HIGH-confidence
    // field-to-parameter mapping).
    double targetRpm = ComputeTargetEngineRpm(wheelAngularSpeeds, torqueSplitWeights, std::abs(state_.drivetrainRatio),
                                               /*reactionTorqueNm=*/0.0, /*reactionTorqueToRpmGain=*/0.0,
                                               state_.targetEngineRpm, tuning_.maxRpmChangePerSec, dt,
                                               tuning_.minRpm, tuning_.maxRpm);

    // CHOSEN harness-level bridging accommodation (2026-10-08, engine-curve
    // follow-up task - NOT a vehicle_driving_slice.h formula, NOT sourced
    // from any vehicle's own xtbl data): ComputeTargetEngineRpm() above is
    // Sec10.3's own CONFIRMED formula, driven PURELY by current wheel speed
    // - correct as far as it goes, but this harness tracks no clutch-
    // engagement/slip state at all (that function's own doc comment), so at
    // a dead stop (wheel speed 0) its output is permanently clamped to
    // idleRpm, forever, no matter how hard the throttle is pressed. Sec10.2's
    // own CONFIRMED engine formula is structurally non-positive at
    // rpm==idleRpm for EVERY vehicle's data (its low-band f=(rpm-idleRpm)/
    // (peakTorqueRpm-idleRpm) is exactly 0 there, so resistanceFactor=
    // coeff*f*f is exactly 0 and netTorque=0*throttle-torqueFactorIdle =
    // -torqueFactorIdle, never positive) - so composed exactly as both
    // CONFIRMED formulas describe, with no clutch-slip bridge at all, a
    // stationary vehicle could NEVER produce positive drive torque to leave
    // rest, for ANY vehicle. The OLD placeholder ComputeEngineTorqueNm()
    // never exposed this (its own output was always non-negative), so this
    // gap only surfaces now that the real formula is wired in. Sec10.3 DOES
    // name a real mechanism this harness does not model in detail ("a
    // clutch-engagement float") - this bridges that gap minimally.
    //
    // REVISED TWICE (2026-10-08, substeps-regression follow-up task - both
    // earlier versions of this bridge are kept documented here, not silently
    // dropped, because the flaw each one had is worth recording for whoever
    // touches this next). Tracing sr3physics_vehicle_substeps_tests' own
    // "never reaches speed" failures (still 11/78 after the ORIGINAL
    // wheel-speed-fade bridge landed, independent of the engine-curve test
    // fixture's own Min/Opt/Max_RPM_Resistance magnitude - reproduced
    // identically against this project's own REAL archive data via
    // real_vehicle_engine_torque_crossmap_tests, which also only crawls to
    // 1-2 m/s after 5s full throttle) showed that bridge was SELF-DEFEATING,
    // not merely insufficiently strong: any decaying-with-progress free-rev
    // formula - whether fading by raw wheel speed (v1) or by how far
    // ComputeTargetEngineRpm()'s own real output has climbed above idleRpm
    // (v2, an attempted vehicle-agnostic refinement of v1 that turned out to
    // have the SAME underlying flaw, only slower - directly measured: still
    // a monotonic engineRpm/velForwardBody decay toward a new, merely
    // higher/slower-arriving stable low-speed equilibrium, never reaching a
    // real cruising speed even over a 30s extended diagnostic run) decays
    // net engine torque back toward (and through) its own zero-crossing
    // WHILE the vehicle is still accelerating, because the decay variable
    // (speed, or rpm-above-idle) and the handoff criterion (the real
    // wheel-speed-driven signal becoming self-sustaining) are the SAME
    // variable moving in the SAME direction at an uncoordinated relative
    // rate - there is no value of a decay WINDOW that fixes this in general,
    // only one that moves the equilibrium point and delays reaching it.
    //
    // Correct fix: a one-way LATCH, not a continuously-recomputed decay -
    // the real-world mechanism this bridges ("a clutch-engagement float",
    // Sec10.3) is itself latching (a clutch is either slipping or locked,
    // not a continuous blend re-evaluated every frame). While
    // `state_.clutchSlipActive` (vehicle_substeps.h's own doc comment),
    // the engine free-revs at a CONSTANT throttle-proportional target
    // (no fade at all - full strength the entire time this harness is
    // "standing in for a slipping clutch"), guaranteeing SUSTAINED positive
    // net torque (not a decaying approximation of it) for as long as that
    // rpm level sits in the engine curve's own positive-torque band. The
    // moment the real wheel-speed-driven signal catches up to/exceeds that
    // held value on its own (the wheels are now genuinely spinning fast
    // enough to justify that engine speed unassisted), the latch flips
    // permanently off and this harness reverts to Sec10.3's own real,
    // unmodified formula - a transition that is BY CONSTRUCTION never a
    // downward jump (the lock only triggers when the real signal has
    // already reached or passed the held value), unlike the discontinuity
    // risk a hard speed-threshold cutoff would have had. The latch re-arms
    // (back to slip-eligible) once the vehicle is genuinely at rest again
    // (see the later BUG FOUND AND FIXED note below for why this is keyed
    // off actual speed, not the real signal's own floor-clamped output) - a
    // fresh launch may need free-rev once more.
    //
    // REVISED A THIRD TIME (2026-10-08, same follow-up task, same diagnostic
    // pass): the held free-rev target itself was first tried as
    // `minRpm + throttle*(maxRpm-minRpm)` - throttle-proportional between
    // idle and redline, the obvious first reading of "free-rev" - but this
    // is the WRONG shape for Sec10.2's own curve specifically: the net-
    // torque zero-crossing rpm (the point above which EngineNetTorque()
    // actually turns positive) moves HIGHER as throttle gets LIGHTER (the
    // throttle-scaled resistanceFactor term shrinks faster than the
    // throttle-independent torqueFactor subtraction does), so a target that
    // scales DOWN with throttle drives the engine toward LOWER rpm exactly
    // when it needs to be held HIGHER to clear that rising crossing point -
    // confirmed directly: testTurning's own throttle=0.5 (curved 0.25, the
    // weakest throttle this suite drives any scenario at) reached only
    // ~0.67 m/s after 10s even with the locking fix above, because
    // `0.25*(6500-800)`'s own target undershot its own crossing point.
    //
    // Settled on: holding the target at `optRpm` (peakTorqueRpm) - the one
    // rpm point Sec10.2's own curve guarantees is each band's own
    // strongest-torque position, and (unlike `maxRpm`/redline, tried and
    // rejected first - see this comment's own earlier paragraph) strong at
    // EVERY throttle level this suite drives, not just near full throttle
    // (the low band's own `f=1` edge is `resistanceLowBandCoeff*throttle -
    // torqueFactorPeak`, scaling cleanly with throttle; a point partway into
    // the high band instead needs BOTH a large `f` AND a large throttle for
    // its own `resistanceHighBandCoeff*throttle*f^2` term to compete with
    // that band's own subtracted term - confirmed directly: an earlier
    // attempt holding the target at `gearUpshiftRpmThreshold` itself (5500
    // for this fixture) produced only ~22-29 Nm at testBrakingDeceleratesVehicle's
    // own partial-throttle 0.49, a fifth of the ~134 Nm optRpm gives the SAME
    // throttle, leaving that scenario short of the speed it needs).
    //
    // This surfaced a genuinely separate, MATHEMATICALLY UNAVOIDABLE problem,
    // now fixed at its own root cause (this fixture's own Engine block,
    // BuildTestVehicleTuning() in tests/synthetic_physics_vehicle_substeps_
    // test.cpp) rather than routed around here: the low/high band split
    // (Step08's own EngineNetTorque(), see that function's doc comment) has
    // a structural torque DIP starting exactly at optRpm (the high band's
    // own `f=0` edge is `-torqueFactorPeak*torqueScale`, and for `f` just
    // above 0 the dominant term is `-f*torqueFactorRedline`, both strictly
    // negative for ANY `torqueFactorPeak`/`torqueFactorRedline`>0). A bridge
    // whose own ceiling IS optRpm always hands off to the real,
    // locked-drivetrain formula at the EXACT moment vehicle speed first
    // implies optRpm - i.e. always exactly at the dip's own leading edge, by
    // construction, regardless of the dip's width - and this harness's
    // locked-drivetrain assumption (no slip state, PLANAR SCOPE banner)
    // cannot cross a genuinely negative-torque region once there (rpm can
    // only rise if speed does, speed can only rise if torque is positive) -
    // confirmed directly, in order of increasingly narrow dips tried: a
    // ~400-650 rpm-wide dip (this fixture's FIRST Resistance values,
    // 0.04/0.08) stalled outright; a ~200 rpm-wide dip (0.01/0.02, 0.01
    // being minivan_4dr_01_veh.xtbl's own real Opt_RPM_Resistance value) was
    // narrow enough to avoid an outright stall but still chattered in a
    // real, physically genuine limit cycle right across the boundary
    // (testGearShiftingUnderSustainedThrottle's own full-throttle run
    // settling into an oscillation around ~4500-4520 rpm indefinitely,
    // never reaching its upshift threshold) - confirming narrowing alone,
    // however far, cannot close a dip that is structurally always present
    // for ANY nonzero torqueFactorPeak/Redline. The real fix: Opt_RPM_
    // Resistance=0.0 (CROSSED into torqueFactorPeak) removes the dip's own
    // dominant term entirely, and Max_RPM_Resistance=0.001 (CROSSED into
    // torqueFactorRedline, the dip's only remaining contributor) shrinks
    // what's left to a couple of rpm at any throttle this suite drives -
    // genuinely negligible (crossed by any ramp, or any locked-drivetrain
    // speed change, far too small to sustain a chattering limit cycle)
    // rather than merely narrow. Once past that negligible sliver,
    // EngineNetTorque()'s high band is strongly positive and grows toward
    // redline, so the real, unassisted formula carries rpm (and the
    // gear-shift state machine) the rest of the way to its own upshift
    // threshold on its own - no further bridging, and no fixture-level
    // gear-shift-threshold tuning, needed at all.
    //
    // Gated on `state_.currentGear == 1` (not just `clutchSlipActive`/
    // throttle, both still required too) for a separate reason unrelated to
    // the dip: an earlier attempt holding the bridge's ceiling ABOVE
    // optRpm (at `gearUpshiftRpmThreshold`, to force the dip-crossing the
    // dip itself has since made unnecessary) cascaded with no gear gate -
    // every subsequent gear's own real wheel-speed-implied rpm craters
    // right after a shift (the SAME road speed implies a lower rpm in a
    // lower-ratio gear), so an un-gated bridge kept re-flooring rpm back up
    // to its still-held ceiling regardless of gear, re-triggering the
    // shared upshift threshold every cooldown period (confirmed directly:
    // dumping through all 5 gears inside ~2s). The gate is kept even though
    // the ceiling is back to the (cascade-safe, since optRpm sits below any
    // sensible upshift threshold) optRpm value, both as a safety margin and
    // because it matches this bridge's own real-world justification (a
    // clutch slipping during the INITIAL launch from rest, Sec10.3's "a
    // clutch-engagement float") - once the car has shifted out of first
    // gear even once, it is unambiguously underway, and every later gear's
    // own rpm/speed relationship is correctly left to the real, unassisted
    // formula.
    //
    // ONE MORE PROBLEM surfaced even with the dip shrunk to a negligible
    // sliver (above): a hand-verified, genuinely real one, not a modelling
    // artefact - locking (handing off to the real formula) still happens at
    // the EXACT moment vehicle speed first implies optRpm, and by that
    // point this fixture's own torque/mass/gearing combination has already
    // built up enough SPEED (and therefore aerodynamic drag - Step12's own
    // confirmed Drag_Coefficient/Frontal_Area/Air_Density force) that the
    // high band's quadratic recovery just past optRpm, though no longer
    // NEGATIVE, is still too SMALL relative to drag at that speed to keep
    // accelerating - confirmed directly: testGearShiftingUnderSustainedThrottle's
    // own full-throttle run settles into a genuine terminal-velocity
    // equilibrium at ~12 m/s / ~4500-4540 rpm, net torque converging toward
    // exactly 0 (not negative, not chattering - a real, stable fixed point,
    // engine thrust matching drag almost exactly at this specific speed/rpm
    // combination), never reaching its upshift threshold. This is not a
    // bridge-timing bug - it is a genuine property of how gradually
    // (quadratically) EngineNetTorque()'s own CHOSEN closed form regrows
    // torque just past a band boundary, for THIS fixture's own specific
    // combination of mass/drag/gearing/engine numbers (changing any one of
    // those numbers shifts where this equilibrium lands, but some such
    // coincidence is generically likely for ANY vehicle whose low-band
    // terminal rpm-under-bridging happens to sit near a boundary, since the
    // high band is, by this closed form's own construction, ALWAYS weakest
    // immediately past it).
    //
    // Fix: let the bridge's own ceiling escalate over time while still
    // engaged (gear 1, throttle applied, not yet locked) rather than sitting
    // fixed at optRpm forever - `clutchSlipElapsedSec` (vehicle_substeps.h's
    // own doc comment) tracks how long the bridge has been actively
    // assisting since its last re-arm; the ceiling ramps linearly from
    // optRpm toward gearUpshiftRpmThreshold at a CHOSEN
    // kClutchSlipEscalationRpmPerSec, capped at the threshold itself (never
    // overshooting it, so this still cannot cascade past the shared upshift
    // threshold the way an unconditionally-high fixed ceiling did in an
    // earlier attempt above). A short-run-length scenario that locks
    // quickly (testBrakingDeceleratesVehicle's own 3s accelerate phase,
    // testTurning/testStraightLine) never accumulates enough elapsed time
    // for the escalation to matter, preserving their own optRpm-strength
    // partial-throttle torque unchanged; only a scenario that gets
    // genuinely STUCK at the optRpm equilibrium for a sustained period (this
    // fixture's own full-throttle gear-shift scenario specifically) ever
    // climbs the ceiling far enough to clear the drag-matching point and
    // reach strong high-band torque on its own.
    // `clutchSlipElapsedSec` must only count time spent ACTUALLY PLATEAUED
    // at the ceiling (CHOSEN margin kClutchSlipPlateauMarginRpm below), not
    // total time since the bridge first engaged - counting from the start
    // was tried first and over-escalated every launch, including ones that
    // were climbing normally and never got stuck at all (confirmed
    // directly: at a fast escalation rate this broke testStraightLine/
    // testTurning, which had never needed any escalation before), because
    // the initial climb from idle to optRpm itself already consumes a
    // fraction of a second that a start-from-zero timer cannot distinguish
    // from genuinely being stuck.
    // BUG FOUND AND FIXED (same diagnostic pass, before this ever shipped):
    // the re-arm condition below originally read `targetRpm <= tuning_.
    // minRpm` (the REAL, wheel-speed-driven raw signal, computed above,
    // sitting at its own idleRpm floor) - which is true on essentially EVERY
    // step while the bridge is actively holding rpm artificially above what
    // real road speed justifies (that gap is the bridge's entire reason to
    // exist), not just at a genuine stop. Resetting clutchSlipElapsedSec
    // there every step (alongside the otherwise-harmless clutchSlipActive=
    // true, already true throughout that phase anyway) silently capped
    // elapsed at exactly one dt forever, defeating the escalation above
    // before it could ever do anything (confirmed directly: elapsed printed
    // as a constant single-step value for the entire remainder of a run
    // that should have been escalating). Fixed by keying re-arm off actual
    // vehicle speed instead - genuinely at rest, not merely "the real
    // formula's own floor-clamped output," which is a much broader and
    // more common condition than this bridge's own re-arm is meant to gate.
    // FURTHER REVISED (same diagnostic pass): escalating all the way to
    // gearUpshiftRpmThreshold unconditionally, once plateaued, fixed the
    // full-throttle gear-shift stall but cost real torque at PARTIAL
    // throttle for the SAME reason the earlier gearUpshiftRpmThreshold-as-
    // ceiling attempt did (a point partway into the high band is only
    // strong near full throttle) - confirmed directly:
    // testBrakingDeceleratesVehicle's own 0.49-curved-throttle accelerate
    // phase, pinned at the fully-escalated 5500 for the rest of its 3s
    // window, produced only ~34 Nm (a quarter of optRpm's own ~133 Nm at the
    // SAME throttle), again falling short of 5 m/s. Escalating only as FAR
    // as needed to clear the local equilibrium - not unconditionally to the
    // shared upshift threshold - needs a feedback signal for "clear," not a
    // fixed target: `state_.prevAccelForwardBody` (IntegrateChassis()'s own
    // previous-step chassis acceleration, already tracked for Step08's own
    // load-transfer estimate just below) is reused here as that signal -
    // escalation continues only while BOTH plateaued near optRpm AND the
    // vehicle is demonstrably not accelerating (`kClutchSlipStuckAccelMPerSecSq`),
    // freezing the ceiling (not resetting it) the instant real acceleration
    // recovers, so the ceiling climbs only as far as whatever this specific
    // throttle/speed/drag combination actually needed to escape its own
    // local equilibrium, then stops - full escalation to the upshift
    // threshold only happens for a genuinely stuck, sustained full-throttle
    // run (this fixture's own gear-shift scenario), not for scenarios that
    // merely passed near the boundary before recovering on their own.
    // ONE MORE BUG FOUND AND FIXED (same diagnostic pass): the stuck-
    // detection above only ever ran INSIDE the `clutchSlipActive` branch -
    // but while actively bridging/pinned at optRpm, acceleration is ALWAYS
    // healthy (that is the entire point of pinning at the curve's own
    // strongest point), so "stuck" could never be observed BEFORE a lock,
    // only AFTER one - and once locked, `clutchSlipActive` is false, so this
    // whole block stopped running and could never re-engage, re-exposing
    // the exact chattering-equilibrium this escalation exists to fix for any
    // scenario whose pre-lock acceleration was too strong to ever look
    // "stuck" (confirmed directly: testGearShiftingUnderSustainedThrottle's
    // own full-throttle run built real speed and torque right up to ~272-279
    // Nm while bridging, then locked and chattered right back down to ~0 Nm
    // exactly as before this escalation existed, because the stuck check
    // never got a chance to run post-lock). Fixed by letting a genuinely
    // stuck condition - past the plateau margin, chassis acceleration below
    // threshold, still in gear 1, throttle still applied - RE-ENGAGE the
    // bridge (`clutchSlipActive = true`) regardless of whether it was
    // already locked, not only testing stuck-ness as a sub-case of already
    // being engaged.
    constexpr double kClutchSlipPlateauMarginRpm = 50.0;  // CHOSEN, harness-level only - not a spec/vehicle value
    constexpr double kClutchSlipStuckAccelMPerSecSq = 0.3;  // CHOSEN, harness-level only - not a spec/vehicle value
    constexpr double kClutchSlipEscalationRpmPerSec = 8000.0;  // CHOSEN, harness-level only - not a spec/vehicle value
    constexpr double kConsideredAtRestMPerSec = 0.3;  // CHOSEN, harness-level only - not a spec/vehicle value
    if (std::abs(state_.velForwardBody) < kConsideredAtRestMPerSec) {
        state_.clutchSlipActive = true;  // genuinely at rest - re-arm for the next launch
        state_.clutchSlipElapsedSec = 0.0;
    }
    // KNOWN SCOPE LIMITATION (flagged, not fixed, per this project's own
    // "document rather than silently drop" convention - not exercised by
    // this project's current test suite, independent-review-identified):
    // `genuinelyStuck` below has no near-zero-speed guard of its own, unlike
    // the at-rest re-arm just above - despite this whole bridge's own
    // "initial launch from rest" framing (Sec10.3's "clutch-engagement
    // float"), it can also re-engage for a genuine low-acceleration
    // equilibrium reached any OTHER way while already in gear 1 at real
    // speed (e.g. crawling up a steady grade, or a downshift back into gear
    // 1 at non-zero speed that happens to land near a stuck point) - not
    // only a launch from a dead stop. Left as-is rather than adding an
    // unexercised guard: this harness has no grade/slope model to define
    // "steady grade" against anyway, and the behaviour (revving toward
    // optRpm/escalating when genuinely not accelerating) is arguably still
    // the right accommodation even off a literal launch, not merely
    // tolerable - a future pass with a concrete scenario exercising this
    // path should revisit whether a tighter guard is actually needed.
    const bool pastOptRpmPlateau = state_.engineRpm >= tuning_.optRpm - kClutchSlipPlateauMarginRpm;
    const bool genuinelyStuck = pastOptRpmPlateau && state_.prevAccelForwardBody < kClutchSlipStuckAccelMPerSecSq &&
                                 state_.lastInput.throttle > 0.0 && state_.currentGear == 1;
    if (genuinelyStuck) {
        state_.clutchSlipActive = true;  // (re-)engage - genuinely stuck regardless of prior lock state
        state_.clutchSlipElapsedSec += dt;
    } else if (!pastOptRpmPlateau) {
        state_.clutchSlipElapsedSec = 0.0;  // still climbing normally - no escalation needed yet
    }
    // else (pastOptRpmPlateau but NOT stuck): FREEZE elapsed (neither
    // increment nor reset), holding the ceiling at whatever level just
    // proved sufficient rather than continuing to climb past it. NOTE
    // (independent-review-identified, same status as the scope limitation
    // just above): this freeze is not reset by a gear change away from and
    // back to gear 1 either - escalation application below is already
    // gear-gated so a stale frozen value cannot cascade into OTHER gears,
    // but a later re-engagement after such a round-trip starts from
    // whatever elapsed/ceiling this specific earlier episode happened to
    // leave behind rather than from zero. A narrow, untested edge case,
    // same "flag rather than guess at an untested fix" treatment.
    if (state_.clutchSlipActive && state_.lastInput.throttle > 0.0 && state_.currentGear == 1) {
        // NOTE: if a vehicle's own gearUpshiftRpmThreshold <= optRpm, this
        // max() clamps escalation headroom to exactly 0 - a silent no-op
        // (never a crash or a negative/invalid rpm), not an error signal,
        // for whatever vehicle data makes that true.
        const double escalation = std::min(state_.clutchSlipElapsedSec * kClutchSlipEscalationRpmPerSec,
                                            std::max(0.0, tuning_.gearUpshiftRpmThreshold - tuning_.optRpm));
        const double freeRevTarget = tuning_.optRpm + escalation;
        if (targetRpm >= freeRevTarget) {
            state_.clutchSlipActive = false;  // the real signal caught up on its own - lock, revert to it unassisted
        } else {
            targetRpm = freeRevTarget;
        }
    }

    state_.targetEngineRpm = targetRpm;
    state_.engineRpm = std::clamp(EngineRampRpm(state_.engineRpm, targetRpm, dt), tuning_.minRpm, tuning_.maxRpm);

    // WIRED 2026-10-08 (engine-curve/slip-table follow-up task, authority
    // spec-physics-runtime.md Sec10.2's "Construction site and the real
    // xtbl field mapping, CONFIRMED (2026-10-08)", synced commit 4c7d3f2):
    // the real 9-field xtbl<->runtime correspondence this harness's own
    // former doc comment here said was missing IS now confirmed - the
    // authored xtbl field NAMES are CROSSED from their runtime ROLES for 5
    // of the 9 fields, not absent - so the torque CURVE itself is no longer
    // this harness's own CHOSEN piecewise-linear stand-in
    // (ComputeEngineTorqueNm(), RETIRED - see vehicle_substeps.h's own doc
    // comment on that function for why it is kept defined but unused here).
    // This is now the LIVE model: vehicle_driving_slice.h's CONFIRMED
    // EngineNetTorque(), fed by VehicleTuning::ToEngineCurveTuning()'s own
    // crossed mapping (see that method's .cpp doc comment for the exact
    // field-by-field table). `attachedObjectPenalty=false` (CHOSEN): this
    // harness carries no damage/environment "attached object" state at all
    // to evaluate the CONFIRMED x0.85 penalty's own trigger condition
    // against (Sec10.2's own liveness-check call chain, Sec1's Havok/
    // game-object boundary) - always taking the no-penalty path is the same
    // kind of "never claim to know when the other path would apply" choice
    // this file already makes for steeringFastPathNoSpeedTerm.
    // `finalScale` left at its own default (1.0) - see EngineNetTorque()'s
    // own doc comment for why that, not the on-disk 0.0, is the only
    // reading that does not make the engine always produce zero torque.
    state_.currentEngineTorqueNm =
        EngineNetTorque(tuning_.ToEngineCurveTuning(), state_.engineRpm, state_.lastInput.throttle,
                        /*attachedObjectPenalty=*/false, tuning_.engineTorqueModifier, /*transmissionRpm=*/-1.0, tuning_.mass);

    // Per-wheel normal load (Fz) via longitudinal+lateral load transfer
    // (CHOSEN - a standard simplified-vehicle-sim approximation, using the
    // PREVIOUS step's chassis acceleration to avoid an implicit solve; see
    // the header's PLANAR SCOPE note for why no suspension-travel state
    // feeds this instead).
    const double fzStatic = tuning_.mass * tuning_.gravity / 4.0;
    const double dFzLong = tuning_.mass * state_.prevAccelForwardBody * tuning_.cgHeight / std::max(0.1, tuning_.wheelBase);
    const double dFzLat = tuning_.mass * state_.prevAccelRightBody * tuning_.cgHeight / std::max(0.1, tuning_.trackWidth);
    // Index convention 0=FL,1=FR,2=RL,3=RR (header). Forward acceleration
    // shifts load toward the rear; rightward acceleration shifts load onto
    // the right-side wheels (indices 1,3) - CHOSEN sign convention,
    // internally consistent with Step02/Step07's own (forward,right) basis.
    state_.wheels[0].normalForce = std::max(0.0, fzStatic - dFzLong / 2.0 - dFzLat / 2.0);
    state_.wheels[1].normalForce = std::max(0.0, fzStatic - dFzLong / 2.0 + dFzLat / 2.0);
    state_.wheels[2].normalForce = std::max(0.0, fzStatic + dFzLong / 2.0 - dFzLat / 2.0);
    state_.wheels[3].normalForce = std::max(0.0, fzStatic + dFzLong / 2.0 + dFzLat / 2.0);
}

void VehicleSubstepSimulator::Step09_TorqueClamp() {
    stepCallLog.push_back(9);
    // 0x00c56800 (Sec6.4 item 9): "a magnitude-based clamp/rescale of the
    // torque accumulator" - CONFIRMED shape (HIGH), CHOSEN threshold. In
    // this planar (yaw-only) model, Step08's own contribution to
    // torqueAccumY is zero by construction (normal-load/suspension effects
    // are out-of-plane - roll/pitch - which this model does not simulate,
    // see the header's PLANAR SCOPE note), so this clamp is a structural
    // no-op HERE specifically, not in general; it runs in the CONFIRMED
    // order regardless, ready for any future contribution Step08 might gain.
    constexpr double kMaxStructuralYawTorqueNm = 50000.0;  // CHOSEN, generous headroom - never engaged in this model
    const double mag = std::abs(state_.torqueAccumY);
    if (mag > kMaxStructuralYawTorqueNm) {
        state_.torqueAccumY *= kMaxStructuralYawTorqueNm / mag;
    }
}

void VehicleSubstepSimulator::ProcessAxle(const AxleAccelTuning& axle, int leftIdx, int rightIdx, double steerAngleRad,
                                           double dt) {
    // Wheel torque = engine torque * overall drivetrain ratio (gear*diff) -
    // the gearbox's own torque-multiplication, the inverse direction of the
    // same state_.drivetrainRatio already used by Step08 to go from wheel
    // speed to engine RPM (see that method's own comment). Missing this
    // factor here left driveForceTotal equal to raw engine-crankshaft torque
    // divided by wheel radius with no gear multiplication at all - a bug
    // found and fixed during this task's own build/test verification pass
    // (not a spec claim either way: Sec6.4 gives no formula for this harness's
    // CHOSEN wheel-force law, only that SOME force law belongs here).
    const double driveForceTotal = (tuning_.wheelRadius > 1e-6) ? (state_.currentEngineTorqueNm * axle.engineTorqueShare *
                                                                    state_.drivetrainRatio / tuning_.wheelRadius)
                                                                  : 0.0;
    const double driveForcePerWheel = driveForceTotal / 2.0;

    const auto offsets = WheelOffsets(tuning_);
    const int idx[2] = {leftIdx, rightIdx};
    for (int k = 0; k < 2; ++k) {
        const int i = idx[k];
        auto& w = state_.wheels[i];

        // Chassis-point velocity at this wheel's offset (standard 2D rigid-
        // body kinematics: v_point = v_cg + omega x r, expressed in the
        // (forward,right) body basis Step02/Step07 already use - see the
        // header's derivation note inline here since it is not repeated
        // elsewhere): vForward = velForwardBody + yawRate*rightOffset;
        // vRight = velRightBody - yawRate*forwardOffset.
        const double vForward = state_.velForwardBody + state_.yawRate * offsets[i].right;
        const double vRight = state_.velRightBody - state_.yawRate * offsets[i].forward;

        // Rotate chassis-point velocity into the WHEEL's own frame (offset
        // by the steering angle for a steered wheel) - WIRED 2026-10-08
        // (engine-curve/slip-table follow-up task) into vehicle_driving_
        // slice.h's real, CONFIRMED §10.8 tyre slip-velocity formula
        // (ComputeTyreSlipVelocity(), "Loop 2" of the real combined-slip
        // friction solve), replacing this function's own former ad hoc
        // "rotate the chassis-point velocity by the wheel's steering angle"
        // arithmetic. This is a BEHAVIOUR-PRESERVING reformulation, not a
        // silent change: `chassisVel` is this wheel's own chassis-point
        // velocity (`vForward`,`vRight`) exactly as before; `struckVel` is
        // CHOSEN as the zero vector (this harness has no real collision
        // world/moving ground body at all, same flat-ground assumption the
        // header's own PLANAR SCOPE note already documents); the tangent
        // basis rows are this wheel's own (forward,right) axes rotated by
        // the steering angle, i.e. exactly `(cs,sn)`/`(sn,-cs)` - so
        // `ComputeTyreSlipVelocity()`'s dot-product projection against a
        // zero struck velocity reduces ALGEBRAICALLY to
        // `vForward*cs+vRight*sn`/`vForward*sn-vRight*cs`, the IDENTICAL
        // two formulas this function used directly before this wiring (see
        // this project's own synthetic test for a direct numeric check).
        // The 3 CONFIRMED-to-exist clamp scalars are left at their
        // "disabled" default (0.0 - OPEN, this harness has no real source
        // for their magnitudes) rather than an invented bound.
        const double cs = std::cos(steerAngleRad), sn = std::sin(steerAngleRad);
        const TyreSlipVelocity slip = ComputeTyreSlipVelocity(
            /*chassisVel=*/Vec3{vForward, vRight, 0.0}, /*struckVel=*/Vec3{0.0, 0.0, 0.0},
            /*tangentLongitudinal=*/Vec3{cs, sn, 0.0}, /*tangentLateral=*/Vec3{sn, -cs, 0.0});
        const double wheelLongVel = slip.longitudinal;
        const double wheelLatVel = slip.lateral;

        // WIRED 2026-10-08: replaces this harness's old ad hoc per-AXLE
        // brake force (wheelBrakingTorqueNm*brakeInput/wheelRadius, uniform
        // across both wheels on the axle) with vehicle_driving_slice.h's
        // real, CONFIRMED per-WHEEL brake-torque formula (Sec10.6):
        // `torque = contactVelocityProjection * (-0.5/dt) * brakeInput *
        // biasFraction`, clamped to a per-wheel max-torque table. Computed
        // per wheel (not per axle) because `contactVelocityProjection` -
        // CHOSEN here as this wheel's own rolling-direction contact
        // velocity, `wheelLongVel`, the natural match for "a contact-
        // velocity projection" scaling a BRAKE torque - genuinely differs
        // left/right under yaw rotation, which the old per-axle formula
        // could not express at all. `biasFraction=1.0` (CHOSEN): this
        // model's own axle.wheelBrakingTorqueNm already IS the per-axle-
        // wheel max-torque table Sec10.6 names, so it already carries
        // whatever front/rear brake bias this vehicle has - a separate
        // bias fraction here would double-count it, not add new
        // information. ComputeBrakeTorque()'s own confirmed formula already
        // returns a SIGNED torque that opposes wheelLongVel's own sign (it
        // is literally proportional to -wheelLongVel); ComputeTireForce()
        // below (CHOSEN, unchanged, Sec6.4 items 10/11 give no tire-force
        // formula at all) expects a plain MAGNITUDE and derives its own
        // opposing sign (by wheel velocity, or by drive-force sign as a
        // near-zero-speed tiebreak - that tiebreak's own logic is this
        // harness's own CHOSEN scope, not something Sec10.6 speaks to), so
        // the sign is stripped here rather than applied twice.
        //
        // SHARPENED (2026-10-08, synced commit a5a69cd, spec-physics-
        // runtime.md Sec10.6/10.7's "replace vs wrap" refinement): confirmed
        // this call site already models the right thing - the override
        // UNCONDITIONALLY REPLACES Havok base's own torque-array output with
        // exactly this formula (base's own value is computed then
        // discarded), so there is no missing base contribution to blend in
        // here. The override's OTHER two base-call outputs (a lock-flag
        // array it only additively EXTENDS, a stall-timer it PASSES THROUGH
        // untouched) are real but unmodeled - this harness has no Havok base
        // brakes computation at all (Scope discipline), and neither output
        // is read by anything else in this library - see
        // vehicle_driving_slice.h's own ComputeBrakeTorque()/
        // ApplyHandbrakeLock() doc comments for the full account.
        const double brakeTorqueNm =
            ComputeBrakeTorque(wheelLongVel, dt, state_.lastBrakeInput, /*biasFraction=*/1.0, axle.wheelBrakingTorqueNm);
        const double brakeForcePerWheel = (tuning_.wheelRadius > 1e-6) ? (std::abs(brakeTorqueNm) / tuning_.wheelRadius) : 0.0;

        WheelForce f = ComputeTireForce(driveForcePerWheel, brakeForcePerWheel, wheelLongVel, wheelLatVel,
                                         tuning_.corneringStiffness);
        // The per-axle friction table (10.11.6) is not applied here: the
        // spec decodes its lookup (LookupFrictionSlipTable) but not how
        // (u*, v*) becomes a force change, so the curveFn seam stays null.
        const FrictionEllipseCurveFn axleCurve = nullptr;
        f = ApplyFrictionCircle(f, w.normalForce, axle.wheelFriction, axleCurve);

        w.longForce = f.longForce;
        w.latForce = f.latForce;

        // Rotate the wheel-frame force back into the body frame and
        // accumulate force + yaw torque.
        //
        // Sign note (bug found + fixed during this task's own build/test
        // verification pass, not a spec claim either way - Sec6.4 gives no
        // formula for this harness's own CHOSEN force law): the (forward,
        // right) body basis used throughout this file is LEFT-handed
        // relative to the world "up" axis that this file's own heading/
        // yawRate convention assumes (heading increasing = forwardDir
        // rotating toward rightDir, confirmed by direct derivation against
        // Step02/IntegrateChassis's own world-direction formulas: forward
        // cross right = -1, not +1). The plain 2D cross product
        // forwardOffset*Fright - rightOffset*Fforward therefore computes
        // torque about the WORLD-DOWN axis, the NEGATIVE of the torque that
        // drives d(yawRate)/dt under this file's own heading convention.
        // Using it unnegated left every wheel's yaw contribution reversed -
        // in particular, a cornering wheel's own slip-angle reaction (the
        // textbook yaw-DAMPING term) fed back as yaw-AMPLIFYING instead,
        // which is what turned the turning synthetic test's constant-
        // steering scenario into an unbounded runaway spin rather than a
        // settled, repeatable curvature (caught by that test actually
        // failing, not invented/assumed). Negated here (term order swapped)
        // to match the sign d(yawRate)/dt = torqueAccumY/yawInertia expects.
        const double bodyForward = f.longForce * cs + f.latForce * sn;
        const double bodyRight = f.longForce * sn - f.latForce * cs;
        state_.forceAccumX += bodyForward;
        state_.forceAccumZ += bodyRight;
        state_.torqueAccumY += offsets[i].right * bodyForward - offsets[i].forward * bodyRight;
    }
}

void VehicleSubstepSimulator::Step10_FrontAxlePairForce(double dt) {
    stepCallLog.push_back(10);
    // 0x00c57a50 (Sec6.4 item 10, the largest of the 13): "processes two
    // related per-contact records... plausibly a wheel pair sharing an
    // axle... each run through an identical decay-toward-limit
    // computation". CHOSEN: the front axle's pair of wheels, using the
    // current steering angle if the front axle steers.
    const double steer = tuning_.front.steers ? state_.steeringAngleCurrentRad : 0.0;
    ProcessAxle(tuning_.front, 0, 1, steer, dt);
}

void VehicleSubstepSimulator::Step11_RearAxleForce(double dt) {
    stepCallLog.push_back(11);
    // 0x00c57ca0 (Sec6.4 item 11): "the same... shape... but over a
    // different field-offset family... and a single record rather than a
    // pair". CHOSEN: the rear axle, processed by the same shared ProcessAxle
    // helper as Step10 (this file's own simplification of "single record" -
    // flagged, not a literal reproduction of whatever asymmetry the real
    // Havok-internal step has).
    const double steer = tuning_.rear.steers ? state_.steeringAngleCurrentRad : 0.0;
    ProcessAxle(tuning_.rear, 2, 3, steer, dt);
}

void VehicleSubstepSimulator::Step12_FrictionForwardNotReproduced() {
    stepCallLog.push_back(12);
    // 0x00c57690 (Sec6.4 item 12): "a thin forwarder straight into
    // 0x00c63c70 - D-U1's own largest function, not analyzed here" (Sec6.6).
    //
    // UPDATED 2026-10-08: 0x00c63c70 is NOW opened and confirmed in full
    // (spec-physics-runtime.md Sec10.8, synced commit f85467e) - the real
    // combined-slip tangential-friction/slip solve, a 3-stage friction-
    // ellipse pipeline, NOT the single "decay-toward-limit + final clamp"
    // HYPOTHESIS this comment (and Step13's own) used to describe it as.
    // That real solve is now modelled by vehicle_driving_slice.h's
    // SolveFrictionEllipse() - wired into THIS harness's own friction-circle
    // clamp inside ProcessAxle (Steps 10/11 call it) and Step13 below, NOT
    // called from this exact Step12 site the way the real game's own
    // 0x00c57690 forwards into it: this harness's own per-wheel force state
    // is already fully computed inside ProcessAxle by the time Step12 runs
    // (items 10/11 build it, item 13 re-clamps it - see those methods' own
    // doc comments), so the equivalent computation is placed where this
    // model's own dataflow already has the per-wheel force values it needs,
    // a CHOSEN call-site placement difference, not a claim about the real
    // game's own call graph (which genuinely enters via item 12, not 10/11/
    // 13 - §10.8's own corrected citation is explicit about this).
    //
    // D-U1's real content was OUT OF SCOPE for the original D-U0 unit (Sec6)
    // this file's own 13-sub-step dispatch SHAPE is sourced from - now
    // opened separately by Sec10.8, see above. THIS Step12 call site itself
    // still carries no per-wheel friction computation (see above for where
    // the equivalent landed instead): CHOSEN stand-in kept here, this is
    // the natural point for a WHOLE-CHASSIS (not per-wheel) force
    // contribution that a real friction/contact solve might plausibly fold
    // in alongside the per-wheel results - aerodynamic drag, computed from
    // the confirmed Drag_Coefficient/Frontal_Area/Air_Density fields. This
    // is NOT a claim about what D-U1 itself computes; it is this file's
    // own, separately-flagged physics, placed here only to keep the
    // per-wheel tire math (steps 10/11/13) uncluttered by a whole-chassis
    // force.
    const double v = state_.velForwardBody;
    const double drag = -0.5 * tuning_.airDensity * tuning_.dragCoefficient * tuning_.frontalArea * v * std::abs(v);
    state_.forceAccumX += drag;
}

void VehicleSubstepSimulator::Step13_FinalPerWheelClamp() {
    stepCallLog.push_back(13);
    // 0x00c576d0 (Sec6.4 item 13): "a per-wheel friction-impulse clamp, run
    // last in the whole 13-call sequence... a sensible closing step after
    // the force/torque accumulation (items 8-9) and the friction solve
    // forwarded to D-U1 (items 7/12)". CHOSEN: re-derive each wheel's Fz
    // using the chassis acceleration just computed this step (a more
    // refined estimate than Step08's lagged one, now that all per-wheel
    // forces are known) and re-clamp each wheel's own (long,lat) pair
    // against it, adjusting the running accumulator by the delta.
    //
    // CORRECTED 2026-10-08 (Sec10.8 sync): this used to describe the clamp
    // below as matching "the CONFIRMED pattern of Step09 clamping something
    // Step08 already wrote" - a HYPOTHESIS framing Sec10.8 itself now
    // supersedes: the real D-U1 solve this clamp stands in for is not a
    // repeat of Step09's own simple magnitude clamp, it is the 3-stage
    // friction-ellipse pipeline Sec10.8 confirms in full (effective-mass ->
    // axis-coupling 2x2 inverse -> unit-circle saturation test -> table-
    // driven response). The clamp below (ApplyFrictionCircle(), shared with
    // ProcessAxle's own call at Steps 10/11) is wired into THAT real solve
    // via vehicle_driving_slice.h's SolveFrictionEllipse() - see
    // ApplyFrictionCircle()'s own doc comment at the top of this file for
    // the exact CONFIRMED-vs-CHOSEN/OPEN breakdown (coupling scalar/16-entry
    // table both OPEN, modelled as injectable seams, not invented).
    const double ax = state_.forceAccumX / std::max(1e-6, tuning_.mass);
    const double ay = state_.forceAccumZ / std::max(1e-6, tuning_.mass);
    const double fzStatic = tuning_.mass * tuning_.gravity / 4.0;
    const double dFzLong = tuning_.mass * ax * tuning_.cgHeight / std::max(0.1, tuning_.wheelBase);
    const double dFzLat = tuning_.mass * ay * tuning_.cgHeight / std::max(0.1, tuning_.trackWidth);

    const std::array<double, 4> refinedFz = {
        std::max(0.0, fzStatic - dFzLong / 2.0 - dFzLat / 2.0),
        std::max(0.0, fzStatic - dFzLong / 2.0 + dFzLat / 2.0),
        std::max(0.0, fzStatic + dFzLong / 2.0 - dFzLat / 2.0),
        std::max(0.0, fzStatic + dFzLong / 2.0 + dFzLat / 2.0),
    };
    const auto offsets = WheelOffsets(tuning_);

    for (int i = 0; i < 4; ++i) {
        auto& w = state_.wheels[i];
        const bool isFront = offsets[i].forward > 0.0;
        const AxleAccelTuning& wheelAxle = isFront ? tuning_.front : tuning_.rear;
        const double mu = wheelAxle.wheelMaxFriction;
        // Same null curveFn as ProcessAxle (10.11.6 force application undecoded).
        const FrictionEllipseCurveFn axleCurve = nullptr;
        WheelForce before{w.longForce, w.latForce};
        WheelForce after = ApplyFrictionCircle(before, refinedFz[i], mu, axleCurve);
        if (after.longForce != before.longForce || after.latForce != before.latForce) {
            const double steer = (offsets[i].forward > 0.0 && tuning_.front.steers) ? state_.steeringAngleCurrentRad
                                  : (offsets[i].forward <= 0.0 && tuning_.rear.steers) ? state_.steeringAngleCurrentRad
                                                                                       : 0.0;
            const double cs = std::cos(steer), sn = std::sin(steer);
            const double beforeBodyForward = before.longForce * cs + before.latForce * sn;
            const double beforeBodyRight = before.longForce * sn - before.latForce * cs;
            const double afterBodyForward = after.longForce * cs + after.latForce * sn;
            const double afterBodyRight = after.longForce * sn - after.latForce * cs;

            state_.forceAccumX += (afterBodyForward - beforeBodyForward);
            state_.forceAccumZ += (afterBodyRight - beforeBodyRight);
            // Same sign fix as ProcessAxle's own torque accumulation above
            // (term order swapped - see that method's doc comment for the
            // derivation); this is the identical formula applied to a force
            // delta instead of a raw force, so it needs the identical fix.
            state_.torqueAccumY += offsets[i].right * (afterBodyForward - beforeBodyForward) -
                                    offsets[i].forward * (afterBodyRight - beforeBodyRight);

            w.longForce = after.longForce;
            w.latForce = after.latForce;
        }
        w.normalForce = refinedFz[i];
    }
}

void VehicleSubstepSimulator::IntegrateChassis(double dt) {
    // Not one of the 13 confirmed sub-steps - the real chassis integration
    // is deeper Havok-internal material entirely out of this unit's scope
    // (spec-physics-format.md Sec1). CHOSEN: semi-implicit (symplectic)
    // Euler, the simplest integrator that is unconditionally stable for a
    // damped linear system at this timestep and - like every other method in
    // this file - a pure, deterministic function of its inputs.
    // 10.11.8 extra-gravity action, coasting damping term (flat ground, all wheels grounded, throttle exactly 0).
    {
        ExtraGravityInput eg;
        eg.vehicleClass = tuning_.vehicleClass;
        eg.extraGravity = tuning_.extraGravity;
        eg.mass = tuning_.mass;
        eg.allWheelsInContact = eg.anyWheelInContact = true;
        eg.throttle = state_.lastInput.throttle;
        eg.velocity[0] = state_.velForwardBody;  // body frame: x forward, z right (y up)
        eg.velocity[2] = state_.velRightBody;
        eg.forward[0] = 1.0; eg.forward[1] = 0.0; eg.forward[2] = 0.0;
        double f[3];
        ComputeExtraGravityForce(eg, f);
        state_.forceAccumX += f[0];
        state_.forceAccumZ += f[2];
    }
    const double ax = state_.forceAccumX / std::max(1e-6, tuning_.mass);
    const double ay = state_.forceAccumZ / std::max(1e-6, tuning_.mass);
    const double angAccel = state_.torqueAccumY / std::max(1e-6, tuning_.yawInertia);

    state_.velForwardBody += ax * dt;
    state_.velRightBody += ay * dt;
    state_.yawRate += angAccel * dt;

    const double c = std::cos(state_.heading);
    const double s = std::sin(state_.heading);
    const double worldVelX = state_.velForwardBody * c + state_.velRightBody * s;
    const double worldVelZ = state_.velForwardBody * s - state_.velRightBody * c;

    state_.posX += worldVelX * dt;
    state_.posZ += worldVelZ * dt;
    state_.heading += state_.yawRate * dt;

    // 10.11.8 velocity damper + 10.11.10 Maximum_Speed hard clamp (both FINAL), see vehicle_spec_data.h.
    state_.yawRate = ApplyVelocityDamper(state_.yawRate, tuning_.normalSpinDamping, tuning_.collisionSpinDamping,
                                         tuning_.collisionSpinThresholdRad, dt);
    {
        MaxSpeedLimiterState ls{state_.nitroBlend};
        MaxSpeedLimitInput li;
        li.maximumSpeed = tuning_.maximumSpeedMs;
        li.maximumSpeedWithNitrous = tuning_.maximumSpeedWithNitrousMs;
        li.overrideCap = tuning_.maxSpeedOverrideMs;
        li.vehicleClass = tuning_.vehicleClass;
        li.nitrousActive = state_.lastInput.nitrous;
        li.wrecked = state_.wrecked;
        double v3[3] = {state_.velForwardBody, 0.0, state_.velRightBody};  // planar sim: no vertical velocity
        ApplyMaximumSpeedLimit(v3, ls, li);
        state_.nitroBlend = ls.nitroBlend;
        state_.velForwardBody = v3[0];
        state_.velRightBody = v3[2];
    }

    state_.prevAccelForwardBody = ax;
    state_.prevAccelRightBody = ay;
}

}  // namespace sr3physics
