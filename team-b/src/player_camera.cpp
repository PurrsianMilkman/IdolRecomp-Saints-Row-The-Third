// sr3player - player runtime unit U4, part 1: the camera mode machine, the
// generic pre-update/update, the outputs, the math helpers and the look-input
// adapter. spec-player-runtime.md Sec4 is the ONLY source; see
// include/sr3player/camera.h for scope, OPEN gates and CHOSEN readings.
#include "sr3player/camera.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sr3player {

namespace {

const char* const kSecTable = "spec-player-runtime.md Sec4.1";
const char* const kSecSelect = "spec-player-runtime.md Sec4.2";

constexpr double kPi = 3.14159265358979323846;

void checkMode(int mode) {
    // The engine does not range-check a mode value; CHOSEN: refuse instead
    // of reading past the 18-row table (as input.h does for its indices).
    if (mode < 0 || mode >= kNumCameraModes) throw std::out_of_range("camera mode out of range 0..17");
}

}  // namespace

// --- math helpers ---------------------------------------------------------------------
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

double WrapAngle(double a) {
    if (!std::isfinite(a)) return a;
    const double twoPi = 2.0 * kPi;
    double r = std::fmod(a + kPi, twoPi);
    if (r < 0) r += twoPi;
    return r - kPi;  // [-pi, pi)
}

double EaseInOut(double t) {
    if (t <= 0.5) return 2.0 * t * t;
    const double u = 1.0 - t;
    return 1.0 - 2.0 * u * u;
}

CameraRotation RotationFromAngles(double pitch, double roll, double heading) {
    const double sp = std::sin(pitch), cp = std::cos(pitch);
    const double sh = std::sin(heading), ch = std::cos(heading);
    CameraRotation m;
    m.forward = {sh * cp, -sp, ch * cp};
    const Vec3 right0{ch, 0, -sh};
    const Vec3 up0 = Cross(m.forward, right0);
    const double sr = std::sin(roll), cr = std::cos(roll);
    m.right = right0 * cr + up0 * sr;
    m.up = up0 * cr - right0 * sr;
    return m;
}

double HeadingFromForward(Vec3 forward) { return std::atan2(forward.x, forward.z); }

bool IsZoomSubmode(int s) { return s == 9 || s == 12 || s == 13 || s == 55; }

bool IsFineAimSubmode(int s) {
    switch (s) {
        case 4: case 8: case 20: case 21: case 22: case 24: case 28: case 30:
        case 45: case 47: case 49: case 57: case 60:
            return true;
        default:
            return false;
    }
}

bool IsScopeAutoEntrySubmode(int s) {
    switch (s) {
        case 20: case 8: case 21: case 22: case 54: case 57: case 59: case 60:
            return true;
        default:
            return false;
    }
}

bool IsSkydivingSubmode(int s) { return s >= submode::kSkydivingFirst && s <= submode::kSkydivingLast; }

void SetPolarFromAuthored(SubmodeRecord& r, double zDist, double yDist) {
    if (std::fabs(zDist) <= 0.001) {
        r.baseElevation = 0;
        r.followDistance = 0;
        return;
    }
    r.baseElevation = std::atan(yDist / zDist);
    r.followDistance = std::sqrt(zDist * zDist + yDist * yDist);
}

SubmodeRecord LerpSubmode(const SubmodeRecord& from, const SubmodeRecord& to, double t, bool flipXShift) {
    const auto lerp = [t](double a, double b) { return a + (b - a) * t; };
    SubmodeRecord r = to;  // default elevation + flag outright; CHOSEN: blend time / override too
    r.lookatOffset = from.lookatOffset + (to.lookatOffset - from.lookatOffset) * t;
    r.xShift = lerp(from.xShift, flipXShift ? -to.xShift : to.xShift);
    r.minElevation = lerp(from.minElevation, to.minElevation);
    r.maxElevation = lerp(from.maxElevation, to.maxElevation);
    r.baseFov = lerp(from.baseFov, to.baseFov);
    r.baseElevation = lerp(from.baseElevation, to.baseElevation);
    r.followDistance = lerp(from.followDistance, to.followDistance);
    return r;
}

// --- look-input adapter (Sec1.12.2 / Sec3.4) ----------------------------------------
bool InputSystemLookSource::invertHorizontal() {
    if (in_.cameraDrivenByKbm()) return mouseInvertH.get();
    if (!in_.optionFlagBits0to3) throw OpenStateError("option byte 0x01411b00 bit 1 (no rebuild yet)", "spec-player-runtime.md Sec1.12.2");
    return (*in_.optionFlagBits0to3 & 0x02) != 0;
}

bool InputSystemLookSource::invertVertical() {
    if (in_.cameraDrivenByKbm()) return mouseInvertV.get();
    if (!in_.optionFlagBits0to3) throw OpenStateError("option byte 0x01411b00 bit 2 (no rebuild yet)", "spec-player-runtime.md Sec1.12.2");
    return (*in_.optionFlagBits0to3 & 0x04) != 0;
}

// --- the mode table (Sec4.1) ------------------------------------------------------------
CameraSystem::CameraSystem(LookInputSource& look) : look_(look) {
    // Slot presence per the Sec4.1 static table (a "stub" slot - the single-
    // RET 0x00754410 - counts as present). Bodies of every row but 0 are
    // U8 territory (Sec7 OPEN 24): implemented = false.
    struct Def {
        const char* id;
        bool i, r, e, p, u;
    };
    static const Def defs[kNumCameraModes] = {
        {"gameplay follow camera", true, true, true, true, true},
        {"unreachable (FOV 60 enter)", false, false, true, false, true},
        {"unreachable (sub-state 0x013cb7b0)", false, false, true, true, true},
        {"debug free camera", false, false, true, false, true},
        {"satellite-weapon view (empty row)", false, false, false, false, false},
        {"empty", false, false, false, false, false},
        {"scripted look-through", false, false, false, false, true},
        {"cutscene camera", false, false, false, false, true},
        {"empty", false, false, false, false, false},
        {"death camera", false, false, true, false, true},
        {"fixed stored transform (unreachable)", false, false, false, false, true},
        {"object-relative hold camera", false, false, true, false, true},
        {"UI-band fixed camera", true, true, false, false, true},
        {"gang customization", true, true, false, false, true},
        {"unreachable (0x00577e20 only with false)", true, true, false, false, true},
        {"showcase camera", true, true, false, false, true},
        {"store / customization camera", true, false, false, true, true},
        {"cinema-editor camera", false, false, false, false, true},
    };
    for (int m = 0; m < kNumCameraModes; ++m) {
        CameraModeRow& row = rows_[static_cast<size_t>(m)];
        row.identity = defs[m].id;
        row.hasInit = defs[m].i;
        row.hasReset = defs[m].r;
        row.hasEnter = defs[m].e;
        row.hasPre = defs[m].p;
        row.hasUpdate = defs[m].u;
    }
    CameraModeRow& r0 = rows_[0];
    r0.implemented = true;
    // Row 0's init is the camera_free.xtbl loader 0x0056e4c0. The table
    // data itself is seeded separately (camera_free_seed.h); the loader's
    // CONFIRMED scalar effects on the angle block are the four 1.0 values.
    r0.init = [this] {
        angles.yawGain = angles.pitchGain = 1.0;
        angles.padAccelYaw = angles.padAccelPitch = 1.0;
    };
    r0.reset = [this] {
        if (!hooks.row0Reset) throw OpenStateError("row 0 reset slot 0x005749e0 (body not read)", kSecTable);
        hooks.row0Reset();
    };
    r0.enter = [this] { followEnter(); };
    r0.pre = [this](double s) { followPreUpdate(s); };
    r0.update = [this](double s) { followUpdate(s); };
}

const CameraModeRow& CameraSystem::row(int mode) const {
    checkMode(mode);
    return rows_[static_cast<size_t>(mode)];
}

void CameraSystem::installRow(int mode, CameraModeRow r) {
    checkMode(mode);
    rows_[static_cast<size_t>(mode)] = std::move(r);
}

void CameraSystem::requireSelectable(int mode) const {
    const CameraModeRow& r = row(mode);
    if (!r.selectable())
        throw OpenStateError(std::string("camera mode ") + std::to_string(mode) + " (" + r.identity +
                                 "): row bodies not implemented (U8, OPEN 24)",
                             kSecTable);
}

void CameraSystem::callEnter(int mode) {
    const CameraModeRow& r = rows_[static_cast<size_t>(mode)];
    if (r.enter) r.enter();
}

// --- selection (Sec4.2) -------------------------------------------------------------------
void CameraSystem::push(int mode) {
    requireSelectable(mode);
    // Search top-down; a found entry is removed (later entries shift down).
    int found = -1;
    for (int i = count_ - 1; i >= 0; --i) {
        if (stack_[static_cast<size_t>(i)] == mode) {
            found = i;
            break;
        }
    }
    // No capacity check in the engine (unreachable overflow, HIGH). CHOSEN:
    // refuse instead of overwriting the count, before anything changes.
    if (found < 0 && count_ >= kCameraStackSlots) throw std::length_error("camera mode stack overflow (unreachable in the shipped build)");
    if (found >= 0) {
        for (int i = found; i + 1 < count_; ++i) stack_[static_cast<size_t>(i)] = stack_[static_cast<size_t>(i + 1)];
        --count_;
    }
    stack_[static_cast<size_t>(count_)] = mode;
    ++count_;
    if (current_ != mode) {
        flags_ &= ~camflag::kTransitionBlend;
        current_ = mode;
        callEnter(mode);
    }
}

void CameraSystem::pop(int mode) {
    checkMode(mode);
    if (count_ == 0) return;
    if (stack_[static_cast<size_t>(count_ - 1)] == mode) {
        --count_;
    } else {
        for (int i = count_ - 2; i >= 0; --i) {
            if (stack_[static_cast<size_t>(i)] == mode) {
                for (int j = i; j + 1 < count_; ++j) stack_[static_cast<size_t>(j)] = stack_[static_cast<size_t>(j + 1)];
                --count_;
                break;
            }
        }
    }
    // Even when nothing was removed.
    const int next = count_ > 0 ? stack_[static_cast<size_t>(count_ - 1)] : base_;
    flags_ &= ~camflag::kTransitionBlend;
    if (next != current_) {
        current_ = next;
        callEnter(next);
    }
}

void CameraSystem::setBase(int mode, bool blendIn, double time) {
    requireSelectable(mode);
    base_ = mode;
    if (count_ == 0 && current_ != mode) {
        flags_ &= ~camflag::kTransitionBlend;
        current_ = mode;
        if (blendIn) startTransitionBlend(time, 7);
        callEnter(mode);
    }
}

void CameraSystem::setSmoothFov(bool on) {
    if (on)
        flags_ |= camflag::kSmoothFov;
    else
        flags_ &= ~camflag::kSmoothFov;
}

void CameraSystem::startTransitionBlend(double time, uint32_t parts) {
    // CHOSEN: "unless 0x013cbb34 is 1" read as skipping the whole start.
    if (world.transitionSnapshotSuppressed.get()) return;
    transitionSnapshot_ = committed_;  // rotation kept as rows (the quaternion 0x00533d90 is only an encoding)
    transitionElapsed_ = 0;
    transitionDuration_ = time;
    transitionParts_ = parts;
    transitionType_ = 0;
    flags_ |= camflag::kTransitionBlend;
}

void CameraSystem::cancelTransitionBlend() {
    transitionType_ = -1;
    flags_ &= ~camflag::kTransitionBlend;
}

void CameraSystem::setPositionDirect(Vec3 p) {
    // CHOSEN: the jump is the Euclidean distance, and the largest is kept
    // ("the largest direct jump recorded in 0x013c8738", Sec4.10).
    directJump8738_ = std::max(directJump8738_, Length(p - committed_.position));
    working_.position = p;
    committed_.position = p;
}

// --- init / reset (Sec4.2) -----------------------------------------------------------------
void CameraSystem::init() {
    // Every row's init slot. Rows other than 0 are not modelled: their init
    // bodies only touch their own (unmodelled) state, so they are skipped.
    for (auto& r : rows_)
        if (r.init) r.init();
    count_ = 0;
    working_.fov = committed_.fov = previous_.fov = kCameraInitFov;
    committed_.right = {1, 0, 0};
    committed_.up = {0, 1, 0};
    committed_.forward = {0, 0, 1};
}

void CameraSystem::reset() {
    // The caller sets `target` to the local player first (the reset's own
    // first act, 0x013c8848/0x013c884c <- local player).
    current_ = kCameraModeFollow;  // no enter, base untouched
    count_ = 0;
    flags_ = 0;
    // The snap/fade timer 0x012e3cf4: nothing in this project arms it.
    if (target.valid) {
        targetSnapPos_ = target.referencePoint;
        targetSnapRows_ = target.orientation;
    }
    for (auto& r : rows_)
        if (r.reset) r.reset();
    cancelTransitionBlend();  // 0x00580410 - CHOSEN: the transition-blend family's reset
    if (target.valid) followSnap();
    // DOF defaults: not modelled.
}

// --- cadence (Sec4.3) ------------------------------------------------------------------------
void CameraSystem::preUpdate(double frameTime) {
    double step = frameTime;
    if (target.valid && world.targetPhysicsDriven.get() && (flags_ & camflag::kTransitionBlend) == 0)
        step = world.physicsQuantisedStep.get();
    const CameraModeRow& r = rows_[static_cast<size_t>(current_)];
    if (r.pre) r.pre(step);
    step88b4_ = step;
}

void CameraSystem::update(const CameraFrameTimes& t) {
    frameTimes_ = t;
    // Step 1 (timer 0x012e3cf4 / 0x00a93320): never armed here.
    // Step 2: without a target, skip everything.
    if (!target.valid) return;
    // Step 3 (the OPEN step is checked first so a refusal changes nothing).
    if (!step88b4_) throw OpenStateError("camera step 0x013c88b4 (no pre-update has run)", "spec-player-runtime.md Sec4.3");
    const double step = *step88b4_;
    previous_ = committed_;
    const CameraModeRow& r = rows_[static_cast<size_t>(current_)];
    if (step > 0 && r.update) r.update(step);
    if (flags_ & camflag::kTransitionBlend)
        throw OpenStateError("transition-blend apply 0x00580b20 (easing 0x00dadb00 and elapsed advance not given)", kSecSelect);
    // Step 4.
    if (!hooks.cameraShake) throw OpenStateError("camera shake 0x0057c930 (OPEN 34)", "spec-player-runtime.md Sec4.11 step 4");
    hooks.cameraShake();
    // Step 5. CHOSEN: the request bit is consumed (cleared) by the snap; the
    // spec does not name its clearer and a sticky bit would snap every frame.
    if (flags_ & camflag::kSnapRequested) {
        flags_ &= ~camflag::kSnapRequested;
        followSnap();  // mode 0 only; the 225-unit notify 0x0090fa60 is not modelled
    }
    // Step 6: commit.
    committed_.position = working_.position;
    committed_.right = working_.right;
    committed_.up = working_.up;
    committed_.forward = working_.forward;
    if (flags_ & camflag::kSmoothFov) {
        const double k = std::clamp(kFovSmoothRate * t.realTimeB4, 0.0, 1.0);
        committed_.fov += (working_.fov - committed_.fov) * k;
    } else {
        committed_.fov = working_.fov;
    }
    // Step 7: per-update outputs (Sec4.10). The hide-humans sweep, zone word
    // and DOF are not modelled.
    targetSnapPos_ = target.referencePoint;
    targetSnapRows_ = target.orientation;
    const double moved = std::max(Length(committed_.position - previous_.position), directJump8738_);
    cutByte_ = moved >= kCameraCutDistance;
    directJump8738_ = 0;
}

// --- common case ----------------------------------------------------------------------------
void SeedFollowCameraOnFootCommonCase(CameraSystem& cam) {
    FollowCameraWorld& w = cam.world;
    w.pivotModeA20.set(false);
    w.drunkEffectInactive.set(true);
    w.swingPredicate6c4990.set(false);
    w.targetPhysicsDriven.set(false);
    w.animationDrivenCameraTest.set(false);
    w.playerScopeView.set(false);
    w.playerFreeFall.set(false);
    w.fn614d00Is5.set(false);
    w.fn614d00Is2.set(false);
    w.flag13c9363.set(false);
    w.pendingRequest8a48.set(0);
    w.playerVehicleSubState5.set(false);
    w.humanSubmode.set(submode::kExteriorClose);  // CHOSEN single on-foot submode (chooser OPEN 30)
    w.submodeFlags[submode::kExteriorClose] = 0u;  // CHOSEN: plain heading-rotated pivot
    w.playerRagdolling.set(false);
    w.player9a1de0.set(false);
    w.instantBlend13c9362.set(false);
    w.heavyWeapon.set(false);
    w.xShiftFlip.set(false);
    w.transitionSnapshotSuppressed.set(false);
    w.yawFactor.set(1.0);
    w.pitchFactor.set(1.0);
    w.fovFactor.set(1.0);
    FollowCameraHooks& h = cam.hooks;
    h.row0Reset = [] {};
    h.pitchReturn = [](FollowAngles&, double) {};
    h.hillTracking = [](FollowAngles&, Vec3, double) {};
    h.coreHead5723e0 = [] {};
    h.coreHead920680 = [] {};
    h.subState0Handler = [](double) {};
    h.after0056ef10 = [] {};
    h.cameraShake = [] {};
    h.surfaceHeights = [](Vec3) { return std::optional<std::pair<double, double>>{}; };
    // CHOSEN: 0x0056c780 always reports "changed", so the blend step always
    // takes its documented branch (advance while progress < 1; a completed
    // blend holds the target record). 0x00574070's body is not given.
    h.blendRequestUnchanged = [] { return false; };
}

}  // namespace sr3player
