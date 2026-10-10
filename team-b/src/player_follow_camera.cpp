// sr3player - player runtime unit U4, part 2: the gameplay follow camera
// (camera mode 0). spec-player-runtime.md Sec4.4-Sec4.9 (and Sec3.4 for the
// look slot) is the ONLY source; see include/sr3player/camera.h for scope,
// OPEN gates and CHOSEN readings.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "sr3player/camera.h"

namespace sr3player {

namespace {

const char* const kSecLook = "spec-player-runtime.md Sec3.4";
const char* const kSecPre = "spec-player-runtime.md Sec4.5";
const char* const kSecRate = "spec-player-runtime.md Sec4.6";
const char* const kSecCore = "spec-player-runtime.md Sec4.7";
const char* const kSecBlend = "spec-player-runtime.md Sec4.8";
const char* const kSecColl = "spec-player-runtime.md Sec4.9";

[[noreturn]] void open(const std::string& what, const char* sec) { throw OpenStateError(what, sec); }

// "Within 1e-5 on every axis" (Sec4.7 step 13, Sec4.9). CHOSEN: <=.
bool withinEps(Vec3 a, Vec3 b) {
    return std::fabs(a.x - b.x) <= kPlacementEpsilon && std::fabs(a.y - b.y) <= kPlacementEpsilon &&
           std::fabs(a.z - b.z) <= kPlacementEpsilon;
}

// The target's heading as rows (the camera convention with pitch = roll = 0).
Vec3 rotateByHeading(Vec3 v, double h) {
    const double s = std::sin(h), c = std::cos(h);
    return Vec3{c, 0, -s} * v.x + Vec3{0, 1, 0} * v.y + Vec3{s, 0, c} * v.z;
}

}  // namespace

const SubmodeRecord& CameraSystem::submodeRecord(int index) const {
    if (index < 0 || index >= kNumSubmodes)
        open("submode record " + std::to_string(index) + " (out of the 61-slot table)", kSecBlend);
    const auto& r = submodes[static_cast<size_t>(index)];
    if (!r) open("camera_free.xtbl submode record " + std::to_string(index) + " (not seeded)", "spec-tables-environment.md Sec12.2");
    return *r;
}

const SubmodeRecord& CameraSystem::blendedRecord() const {
    if (!blend.blended) open("blended submode record 0x012e4100 (no submode installed yet)", kSecBlend);
    return *blend.blended;
}

double CameraSystem::recomputePitch() {
    angles.pitch = angles.pitchTerm20 + angles.userPitch + angles.heliPitch + angles.pitchTerm2c +
                   blendedRecord().baseElevation;
    return angles.pitch;
}

// --- enter 0x005732b0 (Sec4.4) ---------------------------------------------------------------
void CameraSystem::followEnter() {
    angles.heading = target.valid ? target.heading : 0.0;
    // 0x0056bc60: zero every term (CHOSEN: the loader-set gains +0x40..+0x4c
    // are constants, not terms, and the sub-state is not a term).
    angles.pitch = angles.rollBase = angles.roll = angles.swayRoll = angles.swayPhase = 0;
    angles.pitchTerm20 = angles.userPitch = angles.heliPitch = angles.pitchTerm2c = 0;
    angles.scriptPitchRate = angles.scriptYawRate = angles.term38 = angles.term3c = 0;
    angles.lookYaw = angles.lookPitch = angles.yawRate = angles.pitchRate = 0;
    angles.swingRequest = 1.0;
    angles.swingRate = constants.defaultSwingRate.get();
    angles.baseFov = blendedRecord().baseFov;
    // Follow timers 0x012e41a4 / 0x013c8a20: not modelled (vehicle-only).
}

// --- camera hook 0x005ab5b0 and look slot 0x005ab6a0 (Sec4.6, Sec3.4) -------------------
void CameraSystem::cameraHook() {
    if (!look_.cameraDrivenByKbm() && world.sprintPadDescriptorByte5.get())
        angles.swingRequest = 1.0;
    else if (world.swingPredicate6c4990.get())
        angles.swingRequest = 0.3;
    else
        angles.swingRequest = -1.0;
    angles.swingRate = 1.0;
    lookSlot();
}

void CameraSystem::lookSlot() {
    // Step 1.
    if (world.pivotModeA20.get() || world.readControlsDisabled() || (flags_ & camflag::kTransitionBlend)) return;
    // Step 2.
    const float rot = look_.rotate();
    const float elev = look_.elevate();
    // Step 3 (mode 2 is unreachable, Sec4.1).
    if (current_ == kCameraModeFollow) {
        if (!world.drunkEffectInactive.get()) open("drunk camera modifier 0x00975ce0 (OPEN 17)", kSecLook);
        // 0x0056b100(-rot): stores x if 0x005be940() else -x.
        const double x = -static_cast<double>(rot);
        angles.lookYaw = look_.invertHorizontal() ? x : -x;
        // 0x0056b120(elev): stores -y if 0x005be920() else y.
        const double y = static_cast<double>(elev);
        angles.lookPitch = look_.invertVertical() ? -y : y;
    }
    // Step 4.
    if ((rot != 0.0f || elev != 0.0f) && onPlayerGaveInput) onPlayerGaveInput(0.3);
}

// --- pan rates 0x0056f8b0 / 0x0056fdb0 (Sec4.6) ---------------------------------------------
double CameraSystem::dampening(bool horizontal, bool mouse) const {
    const DampeningSet& d = mouse ? constants.mouse : constants.pad;
    const auto pick = [horizontal](const DampeningPair& p) { return horizontal ? p.h.get() : p.v.get(); };
    const int s = blend.current;
    if (s == submode::kTankDriver || s == submode::kRcTank)
        open("tank dampening (needs the vehicle's name hash; vehicles not modelled)", kSecRate);
    if (s == submode::kHumanShield || s == submode::kHumanShieldFineAim) return pick(d.humanShield);
    if (world.heavyWeapon.get()) return pick(d.heavyWeapon);
    if (IsFineAimSubmode(s)) return pick(d.fineAim);
    if (s == submode::kInteriorClose || s == submode::kInteriorSprint) return pick(d.interior);
    if (IsSkydivingSubmode(s)) return pick(d.skydive);
    // Freefall, parachute and helicopter have no vertical value and are not dampened vertically.
    if (s == submode::kFreefall) return horizontal ? d.freefallH.get() : 1.0;
    if (s == submode::kParachute) return horizontal ? d.parachuteH.get() : 1.0;
    if (s == submode::kHelicopterDriver) return horizontal ? d.helicopterH.get() : 1.0;
    return 1.0;
}

double CameraSystem::panRateYaw(double step, bool fine) {
    // Step 1: zoom scale (weapon zoom level 0x0056a880) - weapon system not modelled.
    if (fine) open("zoom scale (weapon zoom level 0x0056a880)", kSecRate);
    const bool mouse = look_.cameraDrivenByKbm();
    const double s = 1.0 * dampening(true, mouse);
    const double yaw = angles.lookYaw, pitch = angles.lookPitch;
    double rate;
    if (mouse) {
        rate = yaw * s;
    } else if (std::sqrt(yaw * yaw + pitch * pitch) <= constants.fastPanInputThreshold.get()) {
        rate = yaw * s * constants.slowPanH.get();
    } else if (std::fabs(yaw) <= constants.hpanThreshold.get()) {
        rate = 0;
    } else {
        if (!hooks.panAccelFactor) open("pan acceleration 0x00dade20 (body not given)", kSecRate);
        if (!hooks.panRateEase) open("pan-rate easing 0x0056bde0 (body not given)", kSecRate);
        const double a = hooks.panAccelFactor(angles.padAccelYaw, 0.1, 10.0, constants.pegAccelHMin.get(),
                                              constants.pegAccelHMax.get());
        const double lo = yaw * s * constants.slowPanH.get(), hi = yaw * s * constants.fastPanH.get();
        const double cand = std::clamp(angles.yawRate + a * yaw * s, std::min(lo, hi), std::max(lo, hi));
        rate = hooks.panRateEase(angles.yawRate, cand, step);
    }
    angles.yawRate = rate;
    return rate;
}

double CameraSystem::panRatePitch(double step, bool fine) {
    if (fine) open("zoom scale (weapon zoom level 0x0056a880)", kSecRate);
    const bool mouse = look_.cameraDrivenByKbm();
    const double s = 1.0 * dampening(false, mouse);
    const double yaw = angles.lookYaw, pitch = angles.lookPitch;
    double rate;
    if (mouse) {
        rate = pitch * s;
    } else if (std::sqrt(yaw * yaw + pitch * pitch) <= constants.fastPanInputThreshold.get()) {
        rate = pitch * s * constants.slowPanV.get();
    } else if (std::fabs(pitch) <= constants.vpanThreshold.get()) {
        rate = 0;
    } else {
        if (!hooks.panAccelFactor) open("pan acceleration 0x00dade20 (body not given)", kSecRate);
        if (!hooks.panRateEase) open("pan-rate easing 0x0056bde0 (body not given)", kSecRate);
        // CHOSEN: the vertical twin passes +0x4c (the spec names only +0x48).
        const double a = hooks.panAccelFactor(angles.padAccelPitch, 0.1, 10.0, constants.pegAccelVMin.get(),
                                              constants.pegAccelVMax.get());
        const double lo = pitch * s * constants.slowPanV.get(), hi = pitch * s * constants.fastPanV.get();
        const double cand = std::clamp(angles.pitchRate + a * pitch * s, std::min(lo, hi), std::max(lo, hi));
        rate = hooks.panRateEase(angles.pitchRate, cand, step);
    }
    angles.pitchRate = rate;
    return rate;
}

// --- yaw / pitch steps (Sec4.6) ------------------------------------------------------------------
void CameraSystem::yawStep(double step, bool fine) {
    const double rate = panRateYaw(step, fine);
    const double f = world.yawFactor.get();
    if (look_.cameraDrivenByKbm())
        angles.heading += rate * f;  // the mouse: no time step (its input is a per-frame delta)
    else
        angles.heading += angles.yawGain * rate * f * step;
    angles.heading = WrapAngle(angles.heading);
    angles.heading = WrapAngle(angles.heading + angles.scriptYawRate * step);
}

void CameraSystem::pitchStep(double step, bool fine) {
    const double rate = panRatePitch(step, fine);
    const double f = world.pitchFactor.get();
    if (look_.cameraDrivenByKbm())
        angles.userPitch += rate * f;
    else
        angles.userPitch += angles.pitchGain * rate * f * step;
    if (!hooks.pitchReturn) open("pitch return 0x0056bfe0 (OPEN 34)", kSecRate);
    hooks.pitchReturn(angles, step);
    angles.userPitch += angles.scriptPitchRate * step;
    const SubmodeRecord& rec = blendedRecord();
    // Reset to the default elevation. CHOSEN order: after the integration,
    // before the clamp (the spec lists them in that order).
    if (rec.hasDefaultElevation &&
        ((blend.progress < 1.0 && !blend.fineAimClassChanged) || blend.defaultElevationRequest)) {
        angles.userPitch = rec.defaultElevation - rec.baseElevation;
        blend.defaultElevationRequest = false;  // "once after a request"
    }
    angles.userPitch = std::clamp(angles.userPitch, rec.minElevation - rec.baseElevation,
                                  rec.maxElevation - rec.baseElevation);
}

// --- pre-update 0x005779b0 (Sec4.5) --------------------------------------------------------------
void CameraSystem::followPreUpdate(double step) {
    // Step 1.
    if (angles.subState == 0 || angles.subState == 5) {
        const bool fine = IsZoomSubmode(blend.current);
        yawStep(step, fine);
        pitchStep(step, fine);
        if (!hooks.hillTracking) open("hill tracking 0x0057ad60 (OPEN 34)", kSecPre);
        hooks.hillTracking(angles, committed_.position, blendedRecord().baseElevation);
        recomputePitch();
        const CameraRotation m = RotationFromAngles(angles.pitch, angles.roll, angles.heading);
        if (angles.subState == 5) open("sub-state 5 rotation adjust 0x00571f90 (OPEN 30)", kSecPre);
        working_.right = m.right;
        working_.up = m.up;
        working_.forward = m.forward;
    }
    // Step 2: animation-driven camera episodes. When the test fails, the
    // latch 0x013c9383 is cleared and an active episode ended - none exist here.
    if (world.animationDrivenCameraTest.get()) open("animation-driven camera 0x00575790", kSecPre);
    // Step 3.
    angles.scriptPitchRate = 0;
    angles.scriptYawRate = 0;
}

// --- update 0x00577cb0 (Sec4.7) -------------------------------------------------------------------
void CameraSystem::followUpdate(double step) {
    if (!(world.playerScopeView.get() || world.playerFreeFall.get() || world.fn614d00Is5.get() ||
          world.flag13c9363.get()))
        angles.baseFov = blendedRecord().baseFov;
    if (skipCoreOnce)
        skipCoreOnce = false;
    else
        followCore(step);
    // Surface clamp (HYPOTHESIS: water surface).
    if (!hooks.surfaceHeights) open("surface heights 0x0079a420 (OPEN 34)", kSecCore);
    if (const auto h = hooks.surfaceHeights(working_.position)) {
        const double level = h->first + h->second + kSurfaceClampLift;
        if (working_.position.y < level && level - working_.position.y < kSurfaceClampRange) working_.position.y = level;
    }
    if (!hooks.after0056ef10) open("0x0056ef10 (OPEN 34)", kSecCore);
    hooks.after0056ef10();
    // Consume the look input: one frame's input feeds exactly one update.
    angles.lookYaw = angles.lookPitch = 0;
    angles.swingRequestLast = angles.swingRequest;
    angles.swingRequest = 1.0;
    angles.swingRate = constants.defaultSwingRate.get();
}

// --- the core 0x00575e30 (Sec4.7) -----------------------------------------------------------------
void CameraSystem::followCore(double step) {
    // Step 1.
    if (!target.valid) return;
    working_.position = working_.position + lateralOffset;  // OPEN 29: purpose unknown
    lateralOffset = Vec3{};
    if (!hooks.coreHead5723e0) open("0x005723e0", kSecCore);
    hooks.coreHead5723e0();
    if (!hooks.coreHead920680) open("0x00920680(T)", kSecCore);
    hooks.coreHead920680();
    if (target.isVehicle) open("vehicle camera target (V); vehicles are not modelled", kSecCore);
    // Step 2: submode choice and blend, then the FOV (0x0056ec70, no V).
    blendUpdate(step);
    if (!angles.baseFov) open("base FOV 0x013c8a44 (not yet written)", kSecCore);
    working_.fov = *angles.baseFov * world.fovFactor.get();
    // Step 3: scope-view auto-entry (weapon system).
    if (IsScopeAutoEntrySubmode(blend.current) && blend.progress >= 1.0)
        open("scope-view auto-entry 0x009e6510 (weapon records not modelled)", kSecCore);
    // Step 4: the distance ramp (0x012e3d31, started only from vehicle
    // fallbacks) is never armed; without V the manual-look hold timer is stopped.
    // Step 5 (no V).
    const int32_t req = world.pendingRequest8a48.get();
    if (req == 2) open("sub-state request 2: re-target 0x005735b0", kSecCore);
    if (req == 1) open("sub-state request 1: 0x013c8a40", kSecCore);
    if (req == 0 && world.playerVehicleSubState5.get()) angles.subState = 5;
    // Step 6.
    if (angles.subState != 0)
        open("follow sub-state handler " + std::to_string(angles.subState) + " (OPEN 30)", kSecCore);
    if (!hooks.subState0Handler) open("follow swing 0x005710b0 (OPEN 30)", kSecCore);
    hooks.subState0Handler(step);
    // Step 7: vehicle-aim submodes.
    if (blend.current >= submode::kVehicleAim && blend.current <= submode::kVehicleZoom)
        open("vehicle-aim heading limits (Vehicle_Aim records)", kSecCore);
    // Step 8: no V, so no helicopter target: +0x28 decays 4 % per update toward 0.
    angles.heliPitch += kHeliPitchFollow * (0.0 - angles.heliPitch);
    // Step 9: pitch sum and sway.
    recomputePitch();
    const double phase = angles.swayPhase;
    const double effHeading = angles.heading + std::sin(phase) * swayHeadingAmp;
    const double rollTarget = angles.rollBase + std::sin(0.9 * phase) * swayRollAmp + angles.swayRoll;
    if (hooks.rollSmoother)
        angles.roll = hooks.rollSmoother(angles.roll, rollTarget, step);
    else if (rollTarget != angles.roll)
        open("roll smoothing per-step limit 0x00dadf70", kSecCore);
    swayHeadingAmp *= kSwayDecay;
    swayRollAmp *= kSwayDecay;
    angles.swayPhase += frameTimes_.realTimeB0 * 1.0;
    // Step 10.
    const CameraRotation m = RotationFromAngles(angles.pitch, angles.roll, effHeading);
    // Step 11: the blended follow distance as of the previous blend step (+0x90).
    if (!blend.previous) open("previous blended record +0x90", kSecCore);
    double d = blend.previous->followDistance;
    // Step 12: the look-at assist timer 0x012e41a0 has no starter here (OPEN 32).
    // Step 13: placement.
    const Vec3 g = blend.pivot;
    if (IsZoomSubmode(blend.current)) open("zoom-submode scope placement 0x009ba2e0", kSecCore);
    if (world.fn614d00Is2.get()) open("distance cap 0x00624070()", kSecCore);
    if (blend.current == submode::kShootingQte) d = Length(g - previous_.position);  // CHOSEN: the previous set
    if (IsSkydivingSubmode(blend.current)) open("skydiving back-vector rotation 0x004d6160", kSecCore);
    const Vec3 back = m.forward * -1.0;
    const Vec3 desired = g + back * d;
    Vec3 pos;
    if (withinEps(desired, g))
        pos = g;
    else
        pos = target.noCollision.get() ? desired : resolveCollision(g, desired);
    working_.position = pos;
    working_.right = m.right;
    working_.up = m.up;
    working_.forward = m.forward;
    // Step 14: shoulder offset.
    const double xs = blendedRecord().xShift;
    if (xs != 0.0) {
        const Vec3 off = m.right * xs;
        lateralOffset = off;
        const Vec3 moved = pos + off;
        // CHOSEN: the sweep runs from the step-13 position to the shifted one.
        working_.position = target.noCollision.get() ? moved : resolveCollision(pos, moved);
    }
}

// --- collision 0x00565d70 (Sec4.9) -------------------------------------------------------------------
Vec3 CameraSystem::resolveCollision(Vec3 from, Vec3 to) {
    if (withinEps(to, from)) return to;
    if (!collision) open("camera sphere cast 0x0075c880 / filter 0x00566070 (no collision world, OPEN 33)", kSecColl);
    if (const auto hit = collision->sweepSphere(from, to, kCameraCollisionRadius)) {
        cutFlag_ = true;
        return hit->position;
    }
    return to;
}

// --- submode choice and blend (Sec4.8) ----------------------------------------------------------------
int CameraSystem::chooseSubmode() const {
    if (!target.valid) return kNoSubmode;
    if (target.isHuman) {
        const int s = world.humanSubmode.get();
        if (s < 0 || s >= kNumSubmodes) throw std::out_of_range("human submode chooser result out of range");
        return s;
    }
    if (target.isVehicle) open("vehicle submode chooser 0x00573110 (vehicles not modelled)", kSecBlend);
    return submode::kSpectator;
}

void CameraSystem::blendSnapTo(int submodeIndex) {
    // 0x0056c860: copy the record, progress = 1, cancel any transition
    // blend, flag a camera cut.
    blend.blended = submodeRecord(submodeIndex);
    blend.progress = 1.0;
    cancelTransitionBlend();
    cutFlag_ = true;
}

void CameraSystem::blendUpdate(double step) {
    if (blend.holdTimerRunning) {  // never armed here (0x00573890 is OPEN)
        blendStep(step);
        return;
    }
    const int desired = chooseSubmode();
    const bool targetChanged = !blend.hasLastTarget || blend.lastTarget != target.id;
    if (desired != blend.current || targetChanged) {
        const int from = blend.current;
        const SubmodeRecord& to = submodeRecord(desired);
        const bool fromValid = from >= 0 && from < kNumSubmodes;
        // Duration (CHOSEN: the rules applied in the order listed, later ones overriding).
        double dur = (fromValid && submodeRecord(from).overrideExitBlendTime) ? submodeRecord(from).blendTime : to.blendTime;
        if ((from == submode::kExteriorClose && desired == submode::kInteriorClose) ||
            (from == submode::kInteriorClose && desired == submode::kExteriorClose))
            dur = constants.exteriorInteriorBlendTime.get();
        if (world.playerRagdolling.get())
            dur = constants.ragdollBlendTime.get();
        else if (desired == submode::kRagdoll && world.player9a1de0.get())
            dur = constants.ragdollEntryBlendTime.get();
        if (!targetChanged && world.instantBlend13c9362.get()) dur = 0;
        if (!world.blendKindFor) open("blend kind +0x14 for a new blend (its source is not given)", kSecBlend);
        const BlendKind kind = world.blendKindFor(from, desired);
        const auto& fl = world.submodeFlags[static_cast<size_t>(desired)];
        if (!fl) open("per-submode flag bits of 0x0056e370 for submode " + std::to_string(desired), kSecBlend);
        const bool first = !fromValid;
        if (dur > 0 && (desired == submode::kZoom || desired == submode::kZoomCrouch || first))
            open("blend hold timer 0x00573890", kSecBlend);
        // 0x0056e370: restart the blend (CHOSEN: "from" = the record the
        // camera is showing now, so a mid-blend restart is continuous).
        blend.from = blend.blended ? *blend.blended : to;
        blend.fromSubmode = from;
        blend.kind = kind;
        blend.duration = dur;
        blend.progress = 0.0;
        blend.flags = *fl;
        // 0x005728a0: install the new target and submode.
        blend.current = desired;
        blend.hasLastTarget = true;
        blend.lastTarget = target.id;
        angles.pitchTerm2c = 0;
        angles.term38 = angles.term3c = 0;  // CHOSEN: "reset" read as zero
        blend.fineAimClassChanged = (fromValid && IsFineAimSubmode(from)) != IsFineAimSubmode(desired);
    }
    // CHOSEN: the blend is stepped in the same update after a restart.
    blendStep(step);
}

void CameraSystem::blendStep(double step) {
    blend.previous = blend.blended;
    if (!hooks.blendRequestUnchanged) open("blend request test 0x0056c780", kSecBlend);
    if (hooks.blendRequestUnchanged()) {
        if (!hooks.onBlendRequestUnchanged) open("0x00574070 (unchanged-request branch)", kSecBlend);
        hooks.onBlendRequestUnchanged();
    } else if (blend.progress < 1.0) {
        const SubmodeRecord& to = submodeRecord(blend.current);
        if (!blend.from) open("blend 'from' record +0x50", kSecBlend);
        const SubmodeRecord from = *blend.from;
        const Vec3 dl = from.lookatOffset - to.lookatOffset;
        if (Dot(dl, dl) > kSubmodeSnapDistanceSq) {
            blendSnapTo(blend.current);
        } else {
            // CHOSEN: a zero duration completes at once (step / 0 clamped to 1).
            blend.progress = blend.duration > 0 ? std::min(1.0, blend.progress + step / blend.duration) : 1.0;
            switch (blend.kind) {
                case BlendKind::Snap:
                    blendSnapTo(blend.current);
                    break;
                case BlendKind::Hold:
                    blend.blended = blend.progress >= 1.0 ? to : from;
                    break;
                case BlendKind::Linear:
                    blend.blended = LerpSubmode(from, to, blend.progress, world.xShiftFlip.get());
                    break;
                case BlendKind::Eased:
                    blend.blended = LerpSubmode(from, to, EaseInOut(blend.progress), world.xShiftFlip.get());
                    break;
            }
        }
    }
    recomputePivot();
}

void CameraSystem::recomputePivot() {
    // 0x00573c10 (Sec4.8). Precedence CHOSEN: flag 0x08 (no rotation) first.
    const SubmodeRecord& rec = blendedRecord();
    Vec3 off = rec.lookatOffset;
    off.y *= target.sizeFactor.get();
    Vec3 rotated;
    if (blend.flags & 0x08)
        rotated = off;
    else if (blend.flags & 0x10)
        rotated = target.orientation.apply(off);
    else
        rotated = rotateByHeading(off, target.heading);
    if (blend.flags & 0x02) open("pivot shape query 0x0075df80 (flag 0x02; human states not modelled)", kSecBlend);
    if (blend.flags & 0x04) open("pivot vertical probe 0x0075d7e0 (flag 0x04)", kSecBlend);
    blend.pivot = rotated + target.referencePoint;
}

// --- the snap 0x00566620 (Sec4.11 step 5) -------------------------------------------------------------
void CameraSystem::placeBehindTarget() {
    // 0x005733b0, body not given beyond "place the camera behind the target
    // (collision-resolved)". CHOSEN reading: heading <- the target's heading
    // (as the enter does), then the core's step-13 placement with the
    // current pitch/roll and the blended follow distance.
    angles.heading = WrapAngle(target.heading);
    recomputePitch();
    const CameraRotation m = RotationFromAngles(angles.pitch, angles.roll, angles.heading);
    const Vec3 g = blend.pivot;
    const Vec3 desired = g + m.forward * -blendedRecord().followDistance;
    Vec3 pos;
    if (withinEps(desired, g))
        pos = g;
    else
        pos = target.noCollision.get() ? desired : resolveCollision(g, desired);
    working_.position = pos;
    working_.right = m.right;
    working_.up = m.up;
    working_.forward = m.forward;
}

void CameraSystem::followSnap() {
    if (current_ != kCameraModeFollow) return;  // "mode 0 only"
    angles.subState = 0;
    // 0x00574b10, "snap the submode blend" (CHOSEN reading: choose the
    // desired submode and install it completed - same copy as 0x0056c860
    // without the camera-cut flag, which the snap sets nowhere in the spec).
    const int desired = chooseSubmode();
    const SubmodeRecord& rec = submodeRecord(desired);
    const auto& fl = world.submodeFlags[static_cast<size_t>(desired)];
    if (!fl) open("per-submode flag bits of 0x0056e370 for submode " + std::to_string(desired), kSecBlend);
    blend.flags = *fl;
    blend.fromSubmode = blend.current;
    blend.current = desired;
    blend.hasLastTarget = true;
    blend.lastTarget = target.id;
    blend.from = rec;
    blend.blended = rec;
    blend.previous = rec;
    blend.progress = 1.0;
    blend.holdTimerRunning = false;
    recomputePivot();
    placeBehindTarget();
    committed_.position = previous_.position = working_.position;
    committed_.right = previous_.right = working_.right;
    committed_.up = previous_.up = working_.up;
    committed_.forward = previous_.forward = working_.forward;
}

}  // namespace sr3player
