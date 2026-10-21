// Synthetic test for sr3physics/vehicle_driving_slice.h: spec-physics-
// runtime.md Sec10's 7 named hkpVehicle component override classes, each
// exercised with known, deterministic inputs against hand-computed expected
// outputs (not golden data - there is no golden source for this unit, see
// that header's own doc comment on what is CONFIRMED vs CHOSEN).
//
// House style follows tests/synthetic_physics_vehicle_substeps_test.cpp: a
// local CHECK() macro, one test function per concern, plain doubles compared
// either exactly (where the arithmetic is binary-exact, e.g. integers/halves/
// quarters) or within a small stated tolerance (where it is not, e.g. a
// division by a decimal literal like 0.1 that is not exactly representable).

#include <cmath>
#include <iostream>

#include "sr3physics/vehicle_driving_slice.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) {                                                                     \
            ++g_passed;                                                                 \
        } else {                                                                        \
            ++g_failed;                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
        }                                                                               \
    } while (0)

constexpr double kEps = 1e-9;
bool Near(double a, double b, double eps = kEps) { return std::abs(a - b) < eps; }

using namespace sr3physics;

// ---------------------------------------------------------------------------
// 10.1 Steering
// ---------------------------------------------------------------------------
void testSteeringBaseAngleInverseSquareFalloff() {
    SteeringBaseTuning t;
    t.inputToAngleScale = 1.0;
    t.speedThreshold = 10.0;

    // At/below the threshold: baseAngle passes through unchanged.
    CHECK(Near(SteeringBaseAngle(t, 1.0, 5.0), 1.0));
    CHECK(Near(SteeringBaseAngle(t, 1.0, 10.0), 1.0));  // exactly at threshold: not ABOVE it

    // Above threshold: finalAngle = baseAngle * (threshold/speedTerm)^2.
    // speedTerm = 2*threshold -> ratio 0.5 -> factor 0.25 (CONFIRMED formula, Sec10.1).
    CHECK(Near(SteeringBaseAngle(t, 1.0, 20.0), 0.25));
    // speedTerm = 4*threshold -> ratio 0.25 -> factor 0.0625.
    CHECK(Near(SteeringBaseAngle(t, 1.0, 40.0), 0.0625));
    // Sign of speedTerm must not matter (|speedTerm| is what is compared/used).
    CHECK(Near(SteeringBaseAngle(t, 1.0, -20.0), 0.25));

    // inputToAngleScale scales baseAngle before the falloff is applied.
    SteeringBaseTuning t2 = t;
    t2.inputToAngleScale = 2.0;
    CHECK(Near(SteeringBaseAngle(t2, 1.0, 20.0), 0.5));  // baseAngle=2.0, *0.25 = 0.5

    // Fast path (instance +0x19c, CHOSEN bool): no speed term at all, ever.
    SteeringBaseTuning fast = t;
    fast.fastPathNoSpeedTerm = true;
    CHECK(Near(SteeringBaseAngle(fast, 1.0, 1000.0), 1.0));
}

void testSteeringRateLimiter() {
    // Plain constant-rate ramp toward a target with no reversal: normalRate
    // governs moving the magnitude UP.
    SteeringRateLimiter lim(/*normalTurnRateRadPerSec=*/2.0, /*centeringRateRadPerSec=*/1.0);
    CHECK(Near(lim.Update(/*targetRad=*/1.0, /*dt=*/0.1), 0.2));  // maxDelta = 2.0*0.1 = 0.2, diff=1.0 clamped to 0.2
    CHECK(Near(lim.Update(1.0, 0.1), 0.4));
    CHECK(Near(lim.Update(1.0, 0.1), 0.6));

    // Moving back toward centre (|target|<|current|) switches to centeringRate.
    CHECK(Near(lim.Update(0.0, 0.1), 0.5));  // maxDelta = 1.0*0.1 = 0.1, diff=-0.6 clamped to -0.1 -> 0.6-0.1=0.5

    // Direction reversal: current=0.5, target flips sign to -1.0 (diff sign
    // flips from the previous call's negative diff to... construct a fresh
    // limiter to isolate the reversal case cleanly.
    SteeringRateLimiter lim2(/*normalTurnRateRadPerSec=*/10.0, /*centeringRateRadPerSec=*/2.0);
    // First call establishes a positive diff (current 0 -> moving toward +5): diffSign=+1.
    const double afterFirst = lim2.Update(5.0, 0.1);
    CHECK(Near(afterFirst, 1.0));  // maxDelta = normalRate*dt = 10*0.1 = 1.0 (moving away from centre)
    // Second call: target flips to -5.0 -> diff is negative, previous diffSign
    // was positive -> reversal. Seed rate = normalRate^2/(2*centeringRate) =
    // 100/(2*2) = 25.0; seeded maxDelta = 25.0*0.1 = 2.5. Plain rate would be
    // centeringRate (moving back toward 0 from 1.0 toward -5.0 means |target|
    // > |current| actually (5>1), so movingAwayFromCenter is TRUE here ->
    // plain rate = normalRate=10 -> plain maxDelta=1.0, SMALLER than the seed
    // cap (2.5) -> std::min picks the plain rate's smaller cap, so the seed
    // has no additional effect in this particular configuration (seed only
    // binds when it is the TIGHTER of the two caps). This is itself a useful
    // thing to assert: the seed is a cap, never an expansion.
    const double afterReversal = lim2.Update(-5.0, 0.1);
    CHECK(Near(afterReversal, 0.0));  // 1.0 - 1.0 (clamped diff) = 0.0
}

// ---------------------------------------------------------------------------
// 10.2 Engine
// ---------------------------------------------------------------------------
void testEngineRampRpm() {
    // diff=100, rate=100/0.1=1000 (under the +-10000 clamp), ease=100/750.
    const double result = EngineRampRpm(/*currentRpm=*/800.0, /*targetRpm=*/900.0, /*dt=*/0.1);
    const double expectedEase = 100.0 / 750.0;
    const double expected = 800.0 + expectedEase * 1000.0 * 0.1;
    CHECK(Near(result, expected, 1e-6));
    CHECK(result > 800.0 && result < 900.0);

    // Large diff: rate clamps at 10000, ease clamps at 1.0 -> new = current + 1.0*10000*dt.
    const double big = EngineRampRpm(/*currentRpm=*/0.0, /*targetRpm=*/100000.0, /*dt=*/0.05);
    CHECK(Near(big, 0.0 + 1.0 * 10000.0 * 0.05, 1e-6));  // = 500.0

    // No change when target == current.
    CHECK(Near(EngineRampRpm(1500.0, 1500.0, 0.1), 1500.0));
}

void testEngineNetTorqueCurve() {
    // D-17 (spec-differential-results.md): table field names are not crossed. Engine: Torque 1000, Min/Opt/Max 1000/4500/6500,
    // Min/Max_Torque_Factor 0.9, resistances 0 / 0 / 0.2, Clutch_Slip 1750.
    EngineCurveTuning t;
    t.torque = 1000.0; t.minRpm = 1000.0; t.optRpm = 4500.0; t.maxRpm = 6500.0;
    t.minTorqueFactor = 0.9; t.maxTorqueFactor = 0.9;
    t.minResistance = 0.0; t.optResistance = 0.0; t.maxResistance = 0.2;
    t.clutchSlipRpm = 1750.0;

    // At Opt_RPM the torque bump peaks at exactly Torque and the resistance is Opt_RPM_Resistance (0): torque = T*throttle - R.
    CHECK(Near(EngineNetTorque(t, 4500.0, 1.0, false), 1000.0, 1e-3));
    CHECK(Near(EngineNetTorque(t, 4500.0, 0.5, false), 500.0, 1e-3));
    // Full throttle evaluates the curve at max(rpm, Min + (Slip - Min) x throttle) = 1750: T = 1000 x (1 + (0.9 - 1) (2750/3500)^2).
    CHECK(Near(EngineNetTorque(t, 1000.0, 1.0, false), 938.265, 1e-2));
    // At/above Max_RPM T = 0 and R = Torque x Max_RPM_Resistance.
    CHECK(Near(EngineNetTorque(t, 6500.0, 1.0, false), -200.0, 1e-3));
    // Attached-object penalty: exact x0.85; finalScale is a pure multiplier.
    const double base = EngineNetTorque(t, 4500.0, 1.0, false);
    CHECK(Near(EngineNetTorque(t, 4500.0, 1.0, true), base * 0.85, 1e-3));
    CHECK(Near(EngineNetTorque(t, 4500.0, 1.0, false, 0.5), base * 0.5, 1e-3));
    CHECK(Near(EngineNetTorque(t, 4500.0, 1.0, false, 0.0), 0.0, 1e-9));
    // Zero throttle (coasting): -(mass x 0.35 x rpm / Max_RPM) = -(1500 x 0.35 x 3000 / 6500) = -242.308.
    CHECK(Near(EngineNetTorque(t, 3000.0, 0.0, false, 1.0, -1.0, 1500.0), -242.308, 1e-2));
}

// ---------------------------------------------------------------------------
// 10.3 Transmission
// ---------------------------------------------------------------------------
void testGearShiftStateMachineUpshiftSequence() {
    // D-19 (spec-differential-results.md): no cooldowns, no mid-shift block, strict comparisons; one decision per step.
    GearShiftTuning tuning;
    tuning.downshiftRpmByGear = {2000.0, 2500.0, 3000.0, 3500.0};
    tuning.upshiftRpmThreshold = 5000.0;
    tuning.maxGear = 5;
    tuning.shiftDurationSec = 0.2;
    GearShiftStateMachine sm(tuning, /*startGear=*/1);
    sm.Update(6000.0, 0.1);
    CHECK(sm.gear() == 2);
    CHECK(sm.midShift());
    sm.Update(6000.0, 0.1);  // timer 0.1 left; it does not block the next upshift
    CHECK(sm.gear() == 3);
    CHECK(sm.midShift());
    sm.Update(5000.0, 0.1);  // exactly at the threshold: strict, no shift; timer expired -> not shifting
    CHECK(sm.gear() == 3);
    sm.Update(5000.0, 0.1);
    CHECK(!sm.midShift());
}

void testGearShiftStateMachineDownshiftAndClamps() {
    GearShiftTuning tuning;
    tuning.downshiftRpmByGear = {2000.0, 2500.0, 3000.0, 3500.0};
    tuning.upshiftRpmThreshold = 5000.0;
    tuning.maxGear = 5;
    tuning.shiftDurationSec = 0.2;
    tuning.upshiftCooldownSec = 0.3;
    tuning.downshiftCooldownSec = 0.3;

    // Downshift: start at gear 3, low rpm at/under downshiftRpmByGear[gear-2]=downshiftRpmByGear[1]=2500.
    GearShiftStateMachine sm(tuning, /*startGear=*/3);
    sm.Update(1500.0, 0.1);
    CHECK(sm.gear() == 2);
    CHECK(sm.midShift());

    // Max-gear clamp: a machine already at maxGear never upshifts further.
    GearShiftStateMachine atMax(tuning, /*startGear=*/5);
    atMax.Update(9000.0, 0.1);
    CHECK(atMax.gear() == 5);
    CHECK(!atMax.midShift());

    // Gear-1 floor: a machine at gear 1 never downshifts further, regardless of rpm.
    GearShiftStateMachine atMin(tuning, /*startGear=*/1);
    atMin.Update(0.0, 0.1);
    CHECK(atMin.gear() == 1);
}

void testComputeTargetEngineRpm() {
    // Uniform wheel speeds/weights: weighted average = the common speed.
    // Chosen so engineAngularSpeed = pi rad/s exactly (wheelSpeed=pi/10,
    // gearRatio=10), giving rpm = pi*60/(2*pi) = 30.0 exactly before any
    // subtraction/clamping - avoids an arbitrary non-round expected value.
    const double kPi = 3.14159265358979323846;
    std::array<double, 4> speeds = {kPi / 10.0, kPi / 10.0, kPi / 10.0, kPi / 10.0};
    std::array<double, 4> weights = {1.0, 1.0, 1.0, 1.0};

    const double rpmNoAdjust = ComputeTargetEngineRpm(speeds, weights, /*overallGearRatio=*/10.0,
                                                       /*reactionTorqueNm=*/0.0, /*reactionTorqueToRpmGain=*/0.01,
                                                       /*previousTargetRpm=*/30.0, /*maxRpmChangePerSec=*/1000.0,
                                                       /*dt=*/0.1, /*idleRpm=*/0.0, /*redlineRpm=*/10000.0);
    CHECK(Near(rpmNoAdjust, 30.0, 1e-9));

    // Reaction-torque subtraction: 500 Nm * 0.01 gain = 5.0 subtracted.
    const double rpmWithReaction = ComputeTargetEngineRpm(speeds, weights, 10.0, /*reactionTorqueNm=*/500.0, 0.01,
                                                           30.0, 1000.0, 0.1, 0.0, 10000.0);
    CHECK(Near(rpmWithReaction, 25.0, 1e-9));

    // Double-clamp: idleRpm floor of 28 catches the 25.0 result above.
    const double rpmClampedToIdle =
        ComputeTargetEngineRpm(speeds, weights, 10.0, 500.0, 0.01, 30.0, 1000.0, 0.1, /*idleRpm=*/28.0, 10000.0);
    CHECK(Near(rpmClampedToIdle, 28.0, 1e-9));

    // Rate limiting: a tight maxRpmChangePerSec caps the move from previousTargetRpm.
    const double rpmRateLimited = ComputeTargetEngineRpm(speeds, weights, 10.0, 0.0, 0.01, /*previousTargetRpm=*/0.0,
                                                          /*maxRpmChangePerSec=*/10.0, /*dt=*/0.1, 0.0, 10000.0);
    CHECK(Near(rpmRateLimited, 1.0, 1e-9));  // maxDelta = 10*0.1 = 1.0, uncapped target (~30) clamped to 0+1.0

    // Non-uniform torque-split weights (e.g. rear-only split, front wheels
    // weighted 0): weighted average depends only on the weighted wheels.
    std::array<double, 4> mixedSpeeds = {0.0, 0.0, kPi / 5.0, kPi / 5.0};
    std::array<double, 4> rearOnlyWeights = {0.0, 0.0, 1.0, 1.0};
    const double rpmRearOnly = ComputeTargetEngineRpm(mixedSpeeds, rearOnlyWeights, 10.0, 0.0, 0.01, 60.0, 1000.0, 0.1,
                                                       0.0, 10000.0);
    CHECK(Near(rpmRearOnly, 60.0, 1e-9));  // weighted avg = pi/5, *10 gearRatio = 2*pi rad/s -> 60 rpm exactly
}

// ---------------------------------------------------------------------------
// 10.4 Suspension
// ---------------------------------------------------------------------------
void testSuspensionSpringDamperForce() {
    SuspensionWheelTuning t;
    t.spring = 1000.0;
    t.damperCompression = 50.0;
    t.damperRelaxation = 20.0;
    t.restLength = 0.3;
    t.loadFactor = 1.0;

    // FINAL D-25: v is the spec closing speed; v < 0 (compressing) uses the COMPRESSION damper.
    // force = (0.3-0.25)*1000 - 50*(-2) = 150.
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(t, 0.25, -2.0), 150.0, 1e-3));

    // v >= 0: RELAXATION damper. force = 50 - 20*2 = 10; v = 0 / -0 also relaxation.
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(t, 0.25, 2.0), 10.0, 1e-3));
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(t, 0.25, -0.0), 50.0, 1e-3));
    // Not touching: 0. Chassis mass multiplies the force.
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(t, 0.25, 2.0, false), 0.0));
    SuspensionWheelTuning tm = t; tm.chassisMass = 1500.0;
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(tm, 0.25, 2.0), 15000.0, 1e-2));

    // TA-2: M = 1 / inverse mass (f32), 0 when the inverse mass is 0; the helper feeds the tuning.
    CHECK(Near(ChassisMassFromInverseMass(1.0 / 1500.0), 1500.0, 1e-2) && ChassisMassFromInverseMass(0.0) == 0.0);
    const SuspensionWheelTuning tw = WithChassisInverseMass(t, 1.0 / 1500.0);
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(tw, 0.25, 2.0), 15000.0, 1.0));
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(WithChassisInverseMass(t, 0.0), 0.25, 2.0), 0.0));

    // At rest length with zero velocity: zero force.
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(t, 0.3, 0.0), 0.0, 1e-6));

    // loadFactor scales only the spring term (CONFIRMED, Sec10.4).
    SuspensionWheelTuning t2 = t;
    t2.loadFactor = 2.0;
    CHECK(Near(SuspensionSpringDamperForceNoXDenominator(t2, 0.25, 0.0), 100.0, 1e-3));  // spring term doubled, no damper term to compare against
}

void testSuspensionSubstepReslicing() {
    SuspensionWheelTuning t;
    t.spring = 1000.0;
    t.damperCompression = 50.0;
    t.damperRelaxation = 20.0;
    t.restLength = 0.3;

    // Equilibrium: length stays at restLength for the whole outer step ->
    // zero compression velocity and zero force at every micro-step.
    auto constantLength = [](double) { return 0.3; };
    const SuspensionStepResult eq = RunSuspensionSubstep(t, /*outerDt=*/0.1, /*initialCompressionLength=*/0.3,
                                                          constantLength);
    CHECK(Near(eq.averageForce, 0.0));
    CHECK(Near(eq.finalCompressionLength, 0.3));
    CHECK(Near(eq.finalCompressionVelocity, 0.0));

    // Linearly-compressing wheel: length(timeIntoStep) = 0.3 - 0.6*timeIntoStep
    // (a steady 0.6 m/s compression rate, independent of the re-slicing
    // granularity - the finite-difference compressionVelocity this function
    // computes per micro-step should reproduce that exact derivative
    // regardless of how many <=1/60s chunks outerDt was sliced into, and the
    // final length should land exactly on the analytic function evaluated at
    // outerDt - both are direct tests of the CONFIRMED re-slicing behaviour).
    auto linearCompression = [](double timeIntoStep) { return 0.3 - 0.6 * timeIntoStep; };
    const SuspensionStepResult lin = RunSuspensionSubstep(t, /*outerDt=*/0.1, /*initialCompressionLength=*/0.3,
                                                           linearCompression);
    CHECK(Near(lin.finalCompressionVelocity, 0.6, 1e-6));
    CHECK(Near(lin.finalCompressionLength, 0.3 - 0.6 * 0.1, 1e-9));
}

// ---------------------------------------------------------------------------
// 10.5 Wheel collision
// ---------------------------------------------------------------------------
void testWheelRayAABBNoRadiusPadding() {
    const AABB3 box = ComputeWheelRayAABB(1.0, 5.0, 2.0, 3.0, 1.0, 2.0);
    CHECK(Near(box.minX, 1.0) && Near(box.maxX, 3.0));
    CHECK(Near(box.minY, 1.0) && Near(box.maxY, 5.0));
    CHECK(Near(box.minZ, 2.0) && Near(box.maxZ, 2.0));  // degenerate on Z: no radius/thickness added anywhere (CONFIRMED, Sec10.5)
}

void testSelectWheelFrictionKeyPreference() {
    // Preferred key 5 wins even when key 7 is also present.
    CHECK(Near(SelectWheelFriction(true, 0.8, true, 0.5, 0.3), 0.8));
    // Key 5 absent: falls back to key 7.
    CHECK(Near(SelectWheelFriction(false, 0.8, true, 0.5, 0.3), 0.5));
    // Neither present: locally-computed default.
    CHECK(Near(SelectWheelFriction(false, 0.8, false, 0.5, 0.3), 0.3));
}

void testContactRelativeVelocityStandardFormula() {
    // Body A: stationary, non-rotating (stand-in for a static struck body,
    // e.g. the ground). Body B: translating at (1,0,0), spinning at 2 rad/s
    // about +Z, contact point 1m along +Y from B's own centre.
    const Vec3 velA{0, 0, 0}, omegaA{0, 0, 0}, posA{0, 0, 0};
    const Vec3 velB{1, 0, 0}, omegaB{0, 0, 2}, posB{0, 0, 0};
    const Vec3 contact{0, 1, 0};
    const Vec3 rel = ComputeContactRelativeVelocity(velA, omegaA, posA, velB, omegaB, posB, contact);
    // pointVelB = velB + omegaB x (contact-posB) = (1,0,0) + (0,0,2)x(0,1,0) = (1,0,0)+(-2,0,0) = (-1,0,0).
    // pointVelA = (0,0,0). relative = pointVelB - pointVelA = (-1,0,0).
    CHECK(Near(rel.x, -1.0) && Near(rel.y, 0.0) && Near(rel.z, 0.0));
}

void testDispatchWheelRaycastsSkipMask() {
    std::array<WheelRaycastResult, 4> results;
    int raycastCalls = 0;
    const uint32_t skipWheel1 = 1u << 1;
    DispatchWheelRaycasts(
        4, skipWheel1,
        [&](int) -> WheelRaycastResult {
            ++raycastCalls;
            return WheelRaycastResult{true, 0.5};
        },
        [&](int i, const WheelRaycastResult& r) { results[static_cast<size_t>(i)] = r; });

    CHECK(raycastCalls == 3);  // wheel 1 skipped, never reaches raycastFn
    CHECK(results[0].hit && Near(results[0].distanceFraction, 0.5));
    CHECK(!results[1].hit && Near(results[1].distanceFraction, 1.0));  // default-constructed, finalize still called
    CHECK(results[2].hit && Near(results[2].distanceFraction, 0.5));
    CHECK(results[3].hit && Near(results[3].distanceFraction, 0.5));
}

// ---------------------------------------------------------------------------
// 10.6 Brakes and driver input
// ---------------------------------------------------------------------------
void testComputeBrakeTorque() {
    // torque = contactVelocityProjection * (-0.5/dt) * brakeInput * bias.
    // dt=0.5 chosen so -0.5/dt = -1.0 exactly (binary-exact arithmetic).
    const double torque = ComputeBrakeTorque(/*contactVelocityProjection=*/4.0, /*dt=*/0.5, /*brakeInput=*/0.5,
                                              /*biasFraction=*/0.6, /*maxTorqueNm=*/1000.0);
    CHECK(Near(torque, 4.0 * -1.0 * 0.5 * 0.6, 1e-9));  // = -1.2

    // Clamped to the per-wheel max-torque table.
    const double clamped = ComputeBrakeTorque(4.0, 0.5, 0.5, 0.6, /*maxTorqueNm=*/1.0);
    CHECK(Near(clamped, -1.0));
}

void testApplyHandbrakeLock() {
    CHECK(Near(ApplyHandbrakeLock(/*handbrakeActive=*/true, /*wheelAngularSpeedRadPerSec=*/5.0), 0.0));
    CHECK(Near(ApplyHandbrakeLock(/*handbrakeActive=*/false, 5.0), 5.0));
}

void testApplyThrottleResponseCurve() {
    // Sign-preserving-squared (CONFIRMED formula |sum|*sum, Sec10.6).
    CHECK(Near(ApplyThrottleResponseCurve(0.5, false), 0.25));
    CHECK(Near(ApplyThrottleResponseCurve(-0.5, false), -0.25));
    CHECK(Near(ApplyThrottleResponseCurve(0.0, false), 0.0));
    // Linear override (instance flag +0x164 bit 1 bypass, CONFIRMED to exist): passthrough.
    CHECK(Near(ApplyThrottleResponseCurve(0.5, true), 0.5));
    CHECK(Near(ApplyThrottleResponseCurve(-0.5, true), -0.5));
}

// ---------------------------------------------------------------------------
// 10.8 D-U1 tangential-friction/slip solve (added 2026-10-08, synced
// commit f85467e)
// ---------------------------------------------------------------------------
void testComputeTangentEffectiveMass() {
    // 1/(sumSq+1.0) and 1/(sumSq+1.0+wheelExtra) - both CONFIRMED reciprocal
    // formulas, exact arithmetic (every term here is binary-exact).
    const TangentEffectiveMass m1 = ComputeTangentEffectiveMass(/*sumSq=*/3.0, /*wheelExtra=*/0.0);
    CHECK(Near(m1.rigid, 0.25));
    CHECK(Near(m1.soft, 0.25));  // wheelExtra=0 -> soft==rigid

    const TangentEffectiveMass m2 = ComputeTangentEffectiveMass(3.0, /*wheelExtra=*/1.0);
    CHECK(Near(m2.rigid, 0.25));
    CHECK(Near(m2.soft, 0.2));  // 1/(3+1+1) = 1/5
}

void testInvertFrictionCouplingMatrixClosedForm() {
    // couplingX=0 -> identity (fully decoupled), matching SolveFrictionEllipse()'s own documented default.
    const FrictionCouplingInverse2x2 identity = InvertFrictionCouplingMatrix(0.0);
    CHECK(!identity.degenerate);
    CHECK(Near(identity.m00, 1.0) && Near(identity.m01, 0.0) && Near(identity.m10, 0.0) && Near(identity.m11, 1.0));

    // couplingX=0.5 -> det=1-0.25=0.75, invDet=4/3; invDet*[[1,-X],[-X,1]] (CONFIRMED exact closed form, Sec10.8).
    const FrictionCouplingInverse2x2 half = InvertFrictionCouplingMatrix(0.5);
    CHECK(!half.degenerate);
    const double invDet = 1.0 / 0.75;
    CHECK(Near(half.m00, invDet) && Near(half.m11, invDet));
    CHECK(Near(half.m01, invDet * -0.5) && Near(half.m10, invDet * -0.5));

    // |X| within the CHOSEN default tolerance (1e-3) of 1 -> degenerate signal, identity fallback (CHOSEN safety, not a spec claim).
    const FrictionCouplingInverse2x2 nearSingular = InvertFrictionCouplingMatrix(0.9995);
    CHECK(nearSingular.degenerate);
    CHECK(Near(nearSingular.m00, 1.0) && Near(nearSingular.m01, 0.0) && Near(nearSingular.m10, 0.0) &&
         Near(nearSingular.m11, 1.0));

    // Comfortably away from 1 -> not degenerate, even though det is getting small.
    const FrictionCouplingInverse2x2 notDegenerate = InvertFrictionCouplingMatrix(0.9);
    CHECK(!notDegenerate.degenerate);
}

void testSolveFrictionEllipseUnsaturatedPassthrough() {
    const FrictionCouplingInverse2x2 identity;  // couplingX=0 default
    const FrictionEllipseResult r = SolveFrictionEllipse(/*axis1ForceIn=*/0.3, /*axis2ForceIn=*/0.3, /*axis1Limit=*/1.0,
                                                         /*axis2Limit=*/1.0, identity);
    // sumSq = 0.09+0.09 = 0.18 < 1.0-deadzone -> well inside the circle, untouched.
    CHECK(!r.saturated);
    CHECK(Near(r.axis1Force, 0.3) && Near(r.axis2Force, 0.3));
}

void testSolveFrictionEllipseSaturatedDefaultFallbackMatchesOldFrictionCircle() {
    // The classic 3-4-5 triangle: magnitude 5, limit 1 on both axes ->
    // massively saturated. No curveFn supplied (nullptr) -> CHOSEN default
    // fallback is the SAME proportional rescale (scale=limit/magnitude) this
    // project's own prior ApplyFrictionCircle() used (see header's own
    // §10.8 banner for the full equivalence argument) - output should land
    // EXACTLY on the unit circle, same direction as the input.
    const FrictionCouplingInverse2x2 identity;
    const FrictionEllipseResult r =
        SolveFrictionEllipse(/*axis1ForceIn=*/3.0, /*axis2ForceIn=*/4.0, /*axis1Limit=*/1.0, /*axis2Limit=*/1.0, identity);
    CHECK(r.saturated);
    CHECK(Near(r.axis1Force, 0.6) && Near(r.axis2Force, 0.8, 1e-6));  // (3,4)/5 = (0.6,0.8), magnitude exactly 1
    const double outMag = std::sqrt(r.axis1Force * r.axis1Force + r.axis2Force * r.axis2Force);
    CHECK(Near(outMag, 1.0, 1e-6));
}

void testSolveFrictionEllipseCustomCurveFn() {
    // A trivial caller-supplied curve (always 0.5 regardless of overshoot
    // fraction) - exercises the injectable seam Sec10.8's own OPEN 16-entry
    // table stands in for (see header banner): the function must use the
    // CALLER's scale, not its own default fallback, once saturated.
    const FrictionCouplingInverse2x2 identity;
    const FrictionEllipseCurveFn alwaysHalf = [](double) { return 0.5; };
    const FrictionEllipseResult r =
        SolveFrictionEllipse(3.0, 4.0, 1.0, 1.0, identity, alwaysHalf);
    CHECK(r.saturated);
    CHECK(Near(r.axis1Force, 1.5) && Near(r.axis2Force, 2.0));
}

void testSolveFrictionEllipseCouplingTransformsBothAxes() {
    // couplingX=0.5, axis1ForceIn=1, axis2ForceIn=0: the coupling-inverse
    // matrix (off-diagonal nonzero) mixes the two axes BEFORE the
    // saturation test, so axis2's OUTPUT is nonzero even though axis2's own
    // INPUT was zero - a direct demonstration that SolveFrictionEllipse()
    // really applies the matrix, not just a per-axis independent clamp.
    const FrictionCouplingInverse2x2 coupled = InvertFrictionCouplingMatrix(0.5);
    const FrictionEllipseResult r = SolveFrictionEllipse(1.0, 0.0, /*axis1Limit=*/10.0, /*axis2Limit=*/10.0, coupled);
    const double invDet = 1.0 / 0.75;
    CHECK(!r.saturated);  // small relative to the generous limits used here
    CHECK(Near(r.axis1Force, invDet));        // m00*1 + m01*0
    CHECK(Near(r.axis2Force, invDet * -0.5));  // m10*1 + m11*0
    CHECK(std::abs(r.axis2Force) > 1e-9);      // genuinely nonzero, not a no-op
}

}  // namespace

int main() {
    testSteeringBaseAngleInverseSquareFalloff();
    testSteeringRateLimiter();

    testEngineRampRpm();
    testEngineNetTorqueCurve();

    testGearShiftStateMachineUpshiftSequence();
    testGearShiftStateMachineDownshiftAndClamps();
    testComputeTargetEngineRpm();

    testSuspensionSpringDamperForce();
    testSuspensionSubstepReslicing();

    testWheelRayAABBNoRadiusPadding();
    testSelectWheelFrictionKeyPreference();
    testContactRelativeVelocityStandardFormula();
    testDispatchWheelRaycastsSkipMask();

    testComputeBrakeTorque();
    testApplyHandbrakeLock();
    testApplyThrottleResponseCurve();

    testComputeTangentEffectiveMass();
    testInvertFrictionCouplingMatrixClosedForm();
    testSolveFrictionEllipseUnsaturatedPassthrough();
    testSolveFrictionEllipseSaturatedDefaultFallbackMatchesOldFrictionCircle();
    testSolveFrictionEllipseCustomCurveFn();
    testSolveFrictionEllipseCouplingTransformsBothAxes();

    std::cout << g_passed << " tests passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
