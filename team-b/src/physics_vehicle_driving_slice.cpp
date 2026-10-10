#include "sr3physics/vehicle_driving_slice.h"

namespace sr3physics {

// ===========================================================================
// 10.1 Steering
// ===========================================================================

double SteeringBaseAngle(const SteeringBaseTuning& tuning, double inputSteerFraction, double speedTerm) {
    const double baseAngle = inputSteerFraction * tuning.inputToAngleScale;
    if (tuning.fastPathNoSpeedTerm) {
        // 0x00aeb0c0's fast path (CONFIRMED, Sec10.1): "a flat multiply with
        // no speed term at all."
        return baseAngle;
    }
    const double absSpeed = std::abs(speedTerm);
    if (absSpeed <= tuning.speedThreshold || tuning.speedThreshold <= 0.0) {
        // Below/at the threshold: the CONFIRMED formula only applies ABOVE
        // it ("above a per-vehicle threshold applies..."); at/below, baseAngle
        // itself is the result (CHOSEN reading of what happens on the other
        // side of the threshold - the spec's own quote only describes the
        // above-threshold branch).
        return baseAngle;
    }
    // 0x00c5a830 (CONFIRMED formula, Sec10.1): finalAngle = baseAngle *
    // (threshold/speedTerm)^2 - "a clean inverse-square speed-sensitive
    // falloff."
    const double ratio = tuning.speedThreshold / absSpeed;
    return baseAngle * ratio * ratio;
}

double SteeringRateLimiter::Update(double targetRad, double dt) {
    const double diff = targetRad - current_;
    const double diffSign = (diff > 0.0) ? 1.0 : ((diff < 0.0) ? -1.0 : 0.0);
    const bool reversed = (diffSign != 0.0 && lastDiffSign_ != 0.0 && diffSign != lastDiffSign_);

    // CHOSEN selection predicate for "two rate-constant pairs (normal-turn
    // vs. centring)" (Sec10.1 CONFIRMS the two named roles, not the exact
    // selection rule): moving the magnitude of the current angle UP uses the
    // normal-turn rate; moving it DOWN (back toward zero/centre) uses the
    // centring rate.
    const bool movingAwayFromCenter = std::abs(targetRad) > std::abs(current_);
    const double rate = movingAwayFromCenter ? normalRate_ : centeringRate_;
    double maxDelta = rate * dt;

    if (reversed) {
        // CONFIRMED formula (Sec10.1): "seeds the limiter with
        // rate1^2/(2*rate2) (the textbook v^2/(2a) stopping-distance term)
        // before delegating to a generic shared easing primitive." CHOSEN
        // application of that seed (the generic easing primitive 0x00dae970
        // itself is not reproduced, HIGH/OPEN, not vehicle-specific): use the
        // seed value as a one-off tighter cap on this first post-reversal
        // step, reverting to the plain constant-rate cap above on every
        // subsequent call.
        const double seedRate = (normalRate_ * normalRate_) / (2.0 * std::max(1e-9, centeringRate_));
        maxDelta = std::min(maxDelta, seedRate * dt);
    }

    const double clampedDiff = std::clamp(diff, -maxDelta, maxDelta);
    current_ += clampedDiff;
    if (diffSign != 0.0) lastDiffSign_ = diffSign;
    return current_;
}

// ===========================================================================
// 10.2 Engine
// ===========================================================================

double EngineRampRpm(double currentRpm, double targetRpm, double dt) {
    // 0x00ae31f0 (CONFIRMED formula+constants, Sec10.2).
    const double diff = targetRpm - currentRpm;
    double rate = diff / std::max(1e-9, dt);
    rate = std::clamp(rate, -10000.0, 10000.0);  // confirmed literal
    const double ease = std::clamp(std::abs(diff) / 750.0, 0.0, 1.0);  // confirmed literal
    return currentRpm + ease * rate * dt;
}

double EngineNetTorque(const EngineCurveTuning& t, double rpm, double throttle, bool attachedObjectPenalty,
                       double finalScale) {
    double torqueFactor;
    double resistanceFactor;

    // CHOSEN closed form for the CONFIRMED qualitative shape (Sec10.2): see
    // this header's own doc comment on EngineNetTorque for the full
    // CONFIRMED-vs-CHOSEN breakdown of this split.
    if (rpm >= t.peakTorqueRpm) {
        const double span = std::max(1e-6, t.redlineRpm - t.peakTorqueRpm);
        const double f = std::clamp((rpm - t.peakTorqueRpm) / span, 0.0, 1.0);  // clamped AT redline, confirmed behaviour
        torqueFactor = t.torqueFactorPeak + f * (t.torqueFactorRedline - t.torqueFactorPeak);
        resistanceFactor = t.resistanceHighBandCoeff * f * f;  // CHOSEN: quadratic in band-position, high-RPM coefficient
    } else {
        const double span = std::max(1e-6, t.peakTorqueRpm - t.idleRpm);
        const double f = std::clamp((rpm - t.idleRpm) / span, 0.0, 1.0);
        torqueFactor = t.torqueFactorIdle + f * (t.torqueFactorPeak - t.torqueFactorIdle);
        resistanceFactor = t.resistanceLowBandCoeff * f * f;  // CHOSEN: quadratic in band-position, low-RPM coefficient

        if (rpm < t.idleRpm) {
            // CHOSEN concrete reading of "additionally scaling by a
            // clutch/load ratio near idle" (Sec10.2, HIGH role only): a
            // simple ramp from 0 at rpm=0 to 1 at rpm=idleRpm.
            const double clutchLoadRatio = std::clamp(rpm / std::max(1e-6, t.idleRpm), 0.0, 1.0);
            torqueFactor *= clutchLoadRatio;
        }
    }

    // CONFIRMED formula, quoted literally (Sec10.2): "netTorque =
    // resistanceFactor*throttle - torqueFactor."
    double netTorque = resistanceFactor * throttle - torqueFactor;
    netTorque *= t.torqueScale;  // CHOSEN placement of the "+0x14, overall torque scale" field

    if (attachedObjectPenalty) {
        netTorque *= 0.85;  // CONFIRMED exact literal, Sec10.2
    }

    netTorque *= finalScale;  // models the OPEN `_DAT_02845af4` global, see header doc comment
    return netTorque;
}

// ===========================================================================
// 10.3 Transmission
// ===========================================================================

void GearShiftStateMachine::Update(double rpm, double dt) {
    if (shiftTimer_ > 0.0) shiftTimer_ = std::max(0.0, shiftTimer_ - dt);
    if (upCooldown_ > 0.0) upCooldown_ = std::max(0.0, upCooldown_ - dt);
    if (downCooldown_ > 0.0) downCooldown_ = std::max(0.0, downCooldown_ - dt);

    if (shiftTimer_ > 0.0) return;  // mid-shift: no new shift decision (CHOSEN gating, matches a timed shift's own intent)

    // CONFIRMED structure (Sec10.3): single shared upshift threshold,
    // independent per-gear downshift table, max-gear clamp, independent
    // cooldown gates per direction.
    if (gear_ < tuning_.maxGear && upCooldown_ <= 0.0 && rpm >= tuning_.upshiftRpmThreshold) {
        ++gear_;
        shiftTimer_ = tuning_.shiftDurationSec;
        upCooldown_ = tuning_.upshiftCooldownSec;
        return;
    }
    if (gear_ > 1) {
        const size_t idx = static_cast<size_t>(gear_ - 2);  // downshiftRpmByGear[gear-2] = threshold for downshifting OUT OF `gear_`
        if (idx < tuning_.downshiftRpmByGear.size() && downCooldown_ <= 0.0 && rpm <= tuning_.downshiftRpmByGear[idx]) {
            --gear_;
            shiftTimer_ = tuning_.shiftDurationSec;
            downCooldown_ = tuning_.downshiftCooldownSec;
        }
    }
}

double ComputeTargetEngineRpm(const std::array<double, 4>& wheelAngularSpeedRadPerSec,
                               const std::array<double, 4>& torqueSplitWeights, double overallGearRatio,
                               double reactionTorqueNm, double reactionTorqueToRpmGain, double previousTargetRpm,
                               double maxRpmChangePerSec, double dt, double idleRpm, double redlineRpm) {
    // CONFIRMED structure (Sec10.3): "a torque-split-weighted average wheel
    // RPM using the exact 60/(2*PI) conversion."
    double weightedWheelAngularSpeed = 0.0;
    double weightSum = 0.0;
    for (size_t i = 0; i < wheelAngularSpeedRadPerSec.size(); ++i) {
        weightedWheelAngularSpeed += wheelAngularSpeedRadPerSec[i] * torqueSplitWeights[i];
        weightSum += torqueSplitWeights[i];
    }
    if (weightSum > 1e-9) weightedWheelAngularSpeed /= weightSum;

    const double engineAngularSpeed = weightedWheelAngularSpeed * overallGearRatio;
    double rpm = engineAngularSpeed * 60.0 / (2.0 * 3.14159265358979323846);  // 60/(2*PI), confirmed literals

    // CONFIRMED: "subtracts the cached reaction-torque field" - CHOSEN
    // concrete coefficient (reactionTorqueToRpmGain) turning a torque into an
    // RPM correction, since the spec does not give one.
    rpm -= reactionTorqueNm * reactionTorqueToRpmGain;

    // CONFIRMED: "rate-limits" - CHOSEN concrete cap (maxRpmChangePerSec).
    const double maxDelta = maxRpmChangePerSec * dt;
    rpm = std::clamp(rpm, previousTargetRpm - maxDelta, previousTargetRpm + maxDelta);

    // CONFIRMED: "double-clamps" - CHOSEN concrete bounds (idle/redline, the
    // same two confirmed engine-curve bounds).
    rpm = std::clamp(rpm, idleRpm, redlineRpm);
    return rpm;
}

// ===========================================================================
// 10.4 Suspension
// ===========================================================================

double SuspensionSpringDamperForceNoXDenominator(const SuspensionWheelTuning& t, double currentLength,
                                                  double compressionVelocity) {
    // 0x00c59630 (CONFIRMED formula, Sec10.4): force = ((restLength-
    // currentLength)*spring*loadFactor - damper*compressionVelocity) *
    // (1/X). X is CHOSEN as 1.0 here (see header doc comment) - this
    // function's own name flags that choice rather than silently baking it
    // in.
    const double springTerm = (t.restLength - currentLength) * t.spring * t.loadFactor;
    // Damper branch selected by compression-velocity sign (CONFIRMED,
    // Sec10.4): compressing further (positive compressionVelocity, i.e.
    // currentLength shrinking) uses damperCompression; rebounding uses
    // damperRelaxation.
    const double damper = (compressionVelocity > 0.0) ? t.damperCompression : t.damperRelaxation;
    const double damperTerm = damper * compressionVelocity;
    return springTerm - damperTerm;
}

SuspensionStepResult RunSuspensionSubstep(const SuspensionWheelTuning& tuning, double outerDt,
                                           double initialCompressionLength,
                                           const std::function<double(double)>& currentLengthFn) {
    SuspensionStepResult result;
    double remaining = std::max(0.0, outerDt);
    double timeIntoStep = 0.0;
    double length = initialCompressionLength;
    double forceSum = 0.0;
    int microSteps = 0;

    while (remaining > 1e-12) {
        const double microDt = std::min(remaining, kSuspensionMaxMicroStepSec);  // CONFIRMED re-slicing, Sec10.4
        timeIntoStep += microDt;
        const double newLength = currentLengthFn(timeIntoStep);
        const double compressionVelocity = (length - newLength) / std::max(1e-9, microDt);  // shrinking length = positive compression velocity
        const double force = SuspensionSpringDamperForceNoXDenominator(tuning, newLength, compressionVelocity);

        forceSum += force;
        ++microSteps;
        length = newLength;
        result.finalCompressionVelocity = compressionVelocity;
        remaining -= microDt;
    }

    result.averageForce = (microSteps > 0) ? (forceSum / microSteps) : 0.0;
    result.finalCompressionLength = length;
    return result;
}

// ===========================================================================
// 10.5 Wheel collision
// ===========================================================================

AABB3 ComputeWheelRayAABB(double startX, double startY, double startZ, double endX, double endY, double endZ) {
    // CONFIRMED (Sec10.5): AABB over the ray segment ONLY, no radius added.
    return AABB3{std::min(startX, endX), std::min(startY, endY), std::min(startZ, endZ),
                 std::max(startX, endX), std::max(startY, endY), std::max(startZ, endZ)};
}

double SelectWheelFriction(bool hasKey5, double key5Value, bool hasKey7, double key7Value, double localDefault) {
    // CONFIRMED structure (Sec10.5): "preferred key 5, fallback key 7, else a
    // locally-computed default."
    if (hasKey5) return key5Value;
    if (hasKey7) return key7Value;
    return localDefault;
}

namespace {
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 Sub(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
}  // namespace

Vec3 ComputeContactRelativeVelocity(Vec3 velA, Vec3 omegaA, Vec3 posA, Vec3 velB, Vec3 omegaB, Vec3 posB,
                                    Vec3 contactPoint) {
    // CONFIRMED (Sec10.5): "the standard two-body point-velocity formula."
    const Vec3 pointVelA = Add(velA, Cross(omegaA, Sub(contactPoint, posA)));
    const Vec3 pointVelB = Add(velB, Cross(omegaB, Sub(contactPoint, posB)));
    return Sub(pointVelB, pointVelA);
}

void DispatchWheelRaycasts(int wheelCount, uint32_t skipMask, const std::function<WheelRaycastResult(int)>& raycastFn,
                            const std::function<void(int, const WheelRaycastResult&)>& finalizeFn) {
    // CONFIRMED structure (Sec10.5): skip bitmask, hit/miss branch, shared
    // finalize call for every wheel (hit or miss). The round-robin counter
    // 0x00ae9f10 also advances is OPEN/not modelled (see header doc comment).
    for (int i = 0; i < wheelCount; ++i) {
        const bool skipped = (skipMask & (1u << static_cast<uint32_t>(i))) != 0;
        WheelRaycastResult r = skipped ? WheelRaycastResult{} : raycastFn(i);
        finalizeFn(i, r);
    }
}

// ===========================================================================
// 10.6 Brakes and driver input
// ===========================================================================

double ComputeBrakeTorque(double contactVelocityProjection, double dt, double brakeInput, double biasFraction,
                          double maxTorqueNm) {
    // CONFIRMED formula (Sec10.6), quoted literally: "-0.5*(1/dt)*brakeInput*
    // bias," applied to the contact-velocity projection, then clamped to a
    // per-wheel max-torque table.
    const double torque = contactVelocityProjection * (-0.5 / std::max(1e-9, dt)) * brakeInput * biasFraction;
    return std::clamp(torque, -maxTorqueNm, maxTorqueNm);
}

double ApplyHandbrakeLock(bool handbrakeActive, double wheelAngularSpeedRadPerSec) {
    // CHOSEN concrete model of "a separate handbrake wheel-lock pass"
    // (CONFIRMED to exist, Sec10.6 - exact formula not given): full lock.
    return handbrakeActive ? 0.0 : wheelAngularSpeedRadPerSec;
}

double ApplyThrottleResponseCurve(double rawInputSum, bool linearOverride) {
    // CONFIRMED formula (Sec10.6): "a quadratic (sign-preserving-squared)
    // throttle response curve by default (|sum|*sum), bypassable via
    // instance flag +0x164 bit 1."
    if (linearOverride) return rawInputSum;
    return std::abs(rawInputSum) * rawInputSum;
}

// ===========================================================================
// 10.8 D-U1 tangential-friction/slip solve
// ===========================================================================

TangentEffectiveMass ComputeTangentEffectiveMass(double sumSq, double wheelExtra) {
    // `0x00c63c70` step 2 (CONFIRMED reciprocal formulas, Sec10.8).
    TangentEffectiveMass m;
    m.rigid = 1.0 / (sumSq + 1.0);
    m.soft = 1.0 / (sumSq + 1.0 + wheelExtra);
    return m;
}

FrictionCouplingInverse2x2 InvertFrictionCouplingMatrix(double couplingX, double degenerateTolerance) {
    // `0x00c63570` (CONFIRMED exact closed form, Sec10.8 step 3-4).
    FrictionCouplingInverse2x2 result;  // identity default, used on the degenerate path
    const double det = 1.0 - couplingX * couplingX;
    if (std::abs(1.0 - std::abs(couplingX)) <= degenerateTolerance || std::abs(det) < 1e-12) {
        result.degenerate = true;
        return result;
    }
    const double invDet = 1.0 / det;
    result.m00 = invDet * 1.0;
    result.m01 = invDet * -couplingX;
    result.m10 = invDet * -couplingX;
    result.m11 = invDet * 1.0;
    result.degenerate = false;
    return result;
}

FrictionEllipseCurveFn FrictionSlipTableCurve(const FrictionSlipTable& table) {
    // Captured by value - the table is small (16 doubles + 2 scalars) and a
    // caller may build this curve once per vehicle/axle, not per call.
    return [table](double overshootFraction) -> double {
        const double clampedFraction = std::clamp(overshootFraction, 0.0, 1.0);
        if (clampedFraction <= table.boundary) {
            // CHOSEN reading of "a pre-loop boundary value" (see header doc
            // comment): below it, the per-entry walk never starts at all.
            return table.entries.front() * table.entryMultiplier;
        }
        const double span = std::max(1e-9, 1.0 - table.boundary);
        const double t = std::clamp((clampedFraction - table.boundary) / span, 0.0, 1.0);
        const double scaledIndex = t * static_cast<double>(table.entries.size() - 1);
        const size_t lo = static_cast<size_t>(scaledIndex);
        const size_t hi = std::min(lo + 1, table.entries.size() - 1);
        const double frac = scaledIndex - static_cast<double>(lo);
        const double interp = table.entries[lo] + frac * (table.entries[hi] - table.entries[lo]);
        return interp * table.entryMultiplier;
    };
}

FrictionEllipseResult SolveFrictionEllipse(double axis1ForceIn, double axis2ForceIn, double axis1Limit,
                                           double axis2Limit, const FrictionCouplingInverse2x2& couplingInverse,
                                           const FrictionEllipseCurveFn& curveFn) {
    // Sec10.8 steps 3-4: apply the (already-inverted) axis-coupling matrix to
    // the raw per-axis force pair before testing it - see this function's
    // own header doc comment for why apply-then-test is the CHOSEN order.
    const double coupledAxis1 = couplingInverse.m00 * axis1ForceIn + couplingInverse.m01 * axis2ForceIn;
    const double coupledAxis2 = couplingInverse.m10 * axis1ForceIn + couplingInverse.m11 * axis2ForceIn;

    // §10.5's OPEN friction-coefficient entry point: CHOSEN here as scaling
    // the per-axis limit terms (see header banner). Guarded by the CONFIRMED
    // FLT_EPSILON-shaped constant rather than an arbitrary tolerance.
    const double invLimit1 = (std::abs(axis1Limit) > kFrictionEllipseEpsilonGuard) ? (1.0 / axis1Limit) : 0.0;
    const double invLimit2 = (std::abs(axis2Limit) > kFrictionEllipseEpsilonGuard) ? (1.0 / axis2Limit) : 0.0;
    const double a = coupledAxis1 * invLimit1;
    const double c = coupledAxis2 * invLimit2;
    const double sumSq = a * a + c * c;

    FrictionEllipseResult result;
    // CONFIRMED comparand exactly 1.0 (Sec10.8 step 5), deadzone-guarded
    // (kFrictionEllipseDeadzone, CONFIRMED to exist as a 1e-4 literal) so a
    // near-unity magnitude does not chatter in/out of saturation call to call.
    if (sumSq < 1.0 - kFrictionEllipseDeadzone) {
        result.axis1Force = coupledAxis1;
        result.axis2Force = coupledAxis2;
        result.saturated = false;
        return result;
    }

    result.saturated = true;
    const double overshoot = std::sqrt(std::max(sumSq, 0.0));
    // CONFIRMED +-1.1 exact literal clamp (Sec10.8 step 5).
    const double clampedOvershoot = std::min(overshoot, kFrictionEllipseOvershootClamp);
    const double overshootFraction =
        std::clamp((clampedOvershoot - 1.0) / (kFrictionEllipseOvershootClamp - 1.0), 0.0, 1.0);

    double scale;
    if (curveFn) {
        // Real 16-entry table (Sec10.8 step 5) - caller-supplied, see header
        // doc comment for why this file does not invent its values/indexing.
        scale = curveFn(overshootFraction);
    } else {
        // CHOSEN fallback (no real curve supplied): the same proportional
        // rescale this project's own prior ApplyFrictionCircle() used -
        // scale = limit/magnitude, i.e. exactly cancel the overshoot. Not a
        // claim about the real table's shape, only a safe, previously-
        // validated default (see header banner for the exact equivalence).
        scale = (overshoot > kFrictionEllipseEpsilonGuard) ? (1.0 / overshoot) : 0.0;
    }
    result.axis1Force = coupledAxis1 * scale;
    result.axis2Force = coupledAxis2 * scale;
    return result;
}

TyreSlipVelocity ComputeTyreSlipVelocity(Vec3 chassisVel, Vec3 struckVel, Vec3 tangentLongitudinal,
                                         Vec3 tangentLateral, double longitudinalLimit, double lateralLimit,
                                         double magnitudeLimit) {
    // `0x00c64252`-`0x00c64306` (CONFIRMED structure, Sec10.8 "Loop 2"): see
    // this function's own header doc comment for the full CONFIRMED-vs-
    // CHOSEN breakdown of the subtraction order and the 3-scalar clamp
    // apportionment below.
    const Vec3 rel = Sub(chassisVel, struckVel);
    double longitudinal = Dot(rel, tangentLongitudinal);
    double lateral = Dot(rel, tangentLateral);

    if (longitudinalLimit > 0.0) longitudinal = std::clamp(longitudinal, -longitudinalLimit, longitudinalLimit);
    if (lateralLimit > 0.0) lateral = std::clamp(lateral, -lateralLimit, lateralLimit);
    if (magnitudeLimit > 0.0) {
        const double mag = std::sqrt(longitudinal * longitudinal + lateral * lateral);
        if (mag > magnitudeLimit && mag > kFrictionEllipseEpsilonGuard) {
            const double scale = magnitudeLimit / mag;
            longitudinal *= scale;
            lateral *= scale;
        }
    }
    return TyreSlipVelocity{longitudinal, lateral};
}

}  // namespace sr3physics
