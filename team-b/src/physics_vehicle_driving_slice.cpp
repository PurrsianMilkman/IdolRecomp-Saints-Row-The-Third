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

// D-20 (spec-differential-results.md, vectors E3): the minimum-time limiter with the reversal seed. A target of exactly 0 uses the
// return pair with seed 0; otherwise the turn-in pair, and when the wheel is still turning away from the new target the segment
// that stops it uses the RETURN pair's acceleration (Vr^2 / (2 dr)) instead of A.
double SteeringProfileLimiter::Update(double targetRad, double dt) {
    constexpr double tol = 1e-6;
    const bool centring = (targetRad == 0.0);
    const double V = centring ? p_.returnSpeed : p_.turnInSpeed;
    const double A = centring ? p_.returnAccel : p_.turnInAccel;
    if (!(V > 0.0) || !(A > 0.0)) {  // no profile data: snap (callers only construct this with data)
        angle_ = targetRad;
        vel_ = 0.0;
        return angle_;
    }
    const double d = V * V / (2.0 * A);  // damp angle of the chosen pair
    double seed = 0.0;
    if (!centring && (targetRad - angle_) * vel_ < 0.0) seed = p_.returnAccel;  // Vr^2 / (2 dr)
    double rem = dt;
    for (int seg = 0; seg < 8 && rem > 0.0; ++seg) {
        const double e = targetRad - angle_;
        if (std::abs(e) <= tol) {
            angle_ = targetRad;
            vel_ = 0.0;
            break;
        }
        const double s = (e >= 0.0) ? 1.0 : -1.0;
        const double ae = std::abs(e);
        double w = s * vel_;  // speed toward the target (negative = moving away)
        double accel, tEvent;
        if (w * ae < -tol) {  // moving away: brake with the seed (reversal) or A
            accel = seed > 0.0 ? seed : A;
            tEvent = -w / accel;
        } else if (ae <= d + tol) {  // inside the braking zone
            const double vt = std::sqrt(w * w / 2.0 + A * ae);
            if (std::abs(vt - w) <= tol) {  // on the curve
                w = vt;
                accel = -A;
                tEvent = 2.0 * ae / vt;
            } else if (w < vt) {  // slower than the curve: accelerate until reaching it
                accel = A;
                const double dd = (2.0 * A * ae - w * w) / (4.0 * A);
                tEvent = (-w + std::sqrt(std::max(0.0, w * w + 2.0 * A * dd))) / A;
            } else {  // faster: stop exactly at the target
                accel = -w * w / (2.0 * ae);
                tEvent = 2.0 * ae / w;
            }
        } else {  // outside the zone
            const double z = ae - d;
            if (std::abs(w - V) <= tol) {  // at cruise speed
                w = V;
                accel = 0.0;
                tEvent = z / V;
            } else {
                accel = (w > V) ? -A : A;
                const double tSpeed = (V - w) / accel;
                const double tZone = (-w + std::sqrt(std::max(0.0, w * w + 2.0 * accel * z))) / accel;
                tEvent = std::min(tSpeed, tZone);
            }
        }
        const double t = std::max(0.0, std::min(rem, tEvent));
        angle_ += s * (w * t + 0.5 * accel * t * t);
        w += accel * t;
        vel_ = s * w;
        rem -= t;
        if (t <= 0.0 && tEvent <= 0.0 && seg > 0 && rem <= 1e-12) break;
    }
    return angle_;
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

double EngineRpmUpdate(const EngineCurveTuning& t, double currentRpm, double throttle, double transmissionRpm, double dt) {
    const double target = t.minRpm + (t.clutchSlipRpm - t.minRpm) * throttle;
    if (transmissionRpm >= target) return transmissionRpm;
    return EngineRampRpm(currentRpm, target, dt);
}

// D-17 (spec-differential-results.md, vectors E1; 5,400/5,400 random cases bit-exact in the report): the table field names are NOT
// crossed. Products are rounded to single precision as written. Positive throttle:
//   target = Min_RPM + (Clutch_Slip_RPM - Min_RPM) * throttle; r = max(engine rpm, target); d = r - Opt_RPM
//   d < 0:  k = 1/(Min_RPM - Opt_RPM); T = Torque*(1 + (MinTF - 1) k^2 d^2); R = Torque*(OptRes + (MinRes - OptRes) k d);
//           transmission rpm < Min_RPM -> R *= transmission rpm / Min_RPM
//   r < Max_RPM: the same with Max_RPM, MaxTF, MaxRes;   else T = 0, R = Torque*MaxRes.
//   torque = T*throttle - R, then x0.85 (attached-object condition), x finalScale.
// Zero throttle (coasting): torque = -(mass * 0.35 * engine rpm / Max_RPM), x (transmission rpm / Min_RPM) when below Min_RPM.
double EngineNetTorque(const EngineCurveTuning& t, double rpmIn, double throttleIn, bool attachedObjectPenalty,
                       double finalScale, double transmissionRpmIn, double vehicleMass) {
    const float rpm = static_cast<float>(rpmIn);
    const float throttle = static_cast<float>(throttleIn);
    const float transRpm = static_cast<float>(transmissionRpmIn < 0.0 ? rpmIn : transmissionRpmIn);
    const float minRpm = static_cast<float>(t.minRpm), optRpm = static_cast<float>(t.optRpm), maxRpm = static_cast<float>(t.maxRpm);
    const float torque = static_cast<float>(t.torque);
    float net;
    if (throttle > 0.0f) {
        const float target = minRpm + (static_cast<float>(t.clutchSlipRpm) - minRpm) * throttle;
        const float r = std::max(rpm, target);
        const float d = r - optRpm;
        float T = 0.0f, R;
        if (d < 0.0f) {
            const float k = 1.0f / (minRpm - optRpm);
            const float kd = k * d;
            T = torque * (1.0f + (static_cast<float>(t.minTorqueFactor) - 1.0f) * (kd * kd));
            R = torque * (static_cast<float>(t.optResistance) + (static_cast<float>(t.minResistance) - static_cast<float>(t.optResistance)) * kd);
            if (transRpm < minRpm) R = R * transRpm / minRpm;
        } else if (r < maxRpm) {
            const float k = 1.0f / (maxRpm - optRpm);
            const float kd = k * d;
            T = torque * (1.0f + (static_cast<float>(t.maxTorqueFactor) - 1.0f) * (kd * kd));
            R = torque * (static_cast<float>(t.optResistance) + (static_cast<float>(t.maxResistance) - static_cast<float>(t.optResistance)) * kd);
        } else {
            R = torque * static_cast<float>(t.maxResistance);  // the stored engine rpm is also set to Max_RPM by the caller
        }
        net = T * throttle - R;
    } else {
        net = -(static_cast<float>(vehicleMass) * 0.35f * rpm / maxRpm);
        if (transRpm < minRpm) net = net * (transRpm / minRpm);
    }
    double out = net;
    if (attachedObjectPenalty) out *= 0.85;
    return out * finalScale;
}


// ===========================================================================
// 10.3 Transmission
// ===========================================================================

// D-19 (spec-differential-results.md): the original's per-step rule. The timer and the shifting flag never block a decision, there
// are no cooldowns, comparisons are strict, and the upshift is checked in the same step after the downshift.
void GearShiftStateMachine::Update(double rpm, double dt, bool shiftWindowClosed, bool upshiftInhibited) {
    shiftTimer_ -= dt;
    if (shiftTimer_ <= 0.0) shiftTimer_ = 0.0;  // the "shifting" flag (midShift) clears at timer <= 0
    if (shiftWindowClosed) return;
    if (gear_ > 1) {
        const size_t idx = static_cast<size_t>(gear_ - 2);  // downshiftRpmByGear[gear-2] = threshold for downshifting OUT OF `gear_`
        if (idx < tuning_.downshiftRpmByGear.size() && tuning_.downshiftRpmByGear[idx] > rpm) {
            --gear_;
            shiftTimer_ = tuning_.shiftDurationSec;
        }
    }
    if (!upshiftInhibited && rpm > tuning_.upshiftRpmThreshold && gear_ < tuning_.maxGear) {
        ++gear_;
        shiftTimer_ = tuning_.shiftDurationSec;
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
                                                  double closingSpeed, bool touching) {
    // 0x00c59630 FINAL (D-25): float32 steps, damper = compression when v < 0 else relaxation, times chassis mass.
    if (!touching) return 0.0;
    const float diff = static_cast<float>(t.restLength - currentLength);
    const float spring = static_cast<float>(static_cast<double>(diff) * t.spring * t.loadFactor);
    const double damping = (closingSpeed < 0.0) ? t.damperCompression : t.damperRelaxation;
    const float inner = static_cast<float>(static_cast<double>(spring) - damping * closingSpeed);
    return static_cast<double>(static_cast<float>(static_cast<double>(inner) * t.chassisMass));
}

double ChassisMassFromInverseMass(double inverseMass) {
    const float inv = static_cast<float>(inverseMass);
    if (inv == 0.0f) return 0.0;
    return static_cast<double>(1.0f / inv);
}

SuspensionWheelTuning WithChassisInverseMass(SuspensionWheelTuning base, double inverseMass) {
    base.chassisMass = ChassisMassFromInverseMass(inverseMass);
    return base;
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
        const double force = SuspensionSpringDamperForceNoXDenominator(tuning, newLength, -compressionVelocity);  // spec sign: compression is negative

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

FrictionSlipLookup LookupFrictionSlipTable(const FrictionSlipTable& table, double A, double B) {
    // spec-physics-runtime.md 10.11.6 PORT RULE (FINAL): search, do not index.
    FrictionSlipLookup out;
    if (A * A + B * B <= 1.0) return out;  // inside the friction circle: no correction
    out.belowCircle = false;
    double prevU = 0.0, prevV = 0.0, prevS = 0.0;
    double ladder = A * table.start;  // u_0
    for (int i = 0; i < 16; ++i) {
        const double u = ladder;
        const double v = B * table.entries[static_cast<size_t>(i)];
        const double S = u * u + v * v;
        if (S > 1.0 || i == 15) {
            const double r = std::sqrt(S);
            const double pr = std::sqrt(prevS);
            const double t = (1.0 - pr) / std::max(r - pr, 1.19e-7);
            out.stopIndex = i;
            out.t = t;
            out.u = std::clamp((1.0 - t) * prevU + t * u, -1.1, 1.1);
            out.v = (1.0 - t) * prevV + t * v;
            return out;
        }
        prevU = u;
        prevV = v;
        prevS = S;
        ladder *= table.q;
    }
    return out;  // unreachable: entry 15 always stops the search
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
