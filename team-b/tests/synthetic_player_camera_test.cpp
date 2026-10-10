// Synthetic tests for player runtime unit U4 - the camera mode machine and
// the gameplay follow camera (spec-player-runtime.md Sec4). Every expected
// value is computed from a rule quoted from that spec (cited per block).
// Inputs are SYNTHETIC: the scripted look source, the submode records
// (distance 4, FOV 50, ...), the panning constants (slow 0.5, threshold
// 0.9, ...) and the fake collision are test numbers chosen for easy
// arithmetic - NOT engine values; the real ones are OPEN until seeded from
// camera_free.xtbl (camera_free_seed.h).
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3luahost/open_state.h"
#include "sr3player/camera.h"
#include "sr3player/camera_free_seed.h"
#include "sr3player/input.h"
#include "sr3player/movement.h"
#include "sr3tables_environment/camera_free.h"
#include "sr3xtbl/xtbl.h"

using namespace sr3player;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool deq(double a, double b, double tol = 1e-9) { return std::fabs(a - b) < tol; }
bool veq(Vec3 a, Vec3 b, double tol = 1e-9) { return deq(a.x, b.x, tol) && deq(a.y, b.y, tol) && deq(a.z, b.z, tol); }

template <typename F>
bool throwsOpen(F&& f, const char* needle = nullptr) {
    try {
        f();
    } catch (const sr3luahost::OpenStateError& e) {
        if (needle != nullptr && e.global().find(needle) == std::string::npos) {
            std::cerr << "  (OPEN thrown but named: " << e.global() << ")\n";
            return false;
        }
        return true;
    }
    return false;
}

struct ScriptedLook : LookInputSource {
    float rot = 0, elev = 0;
    bool kbm = false, invH = false, invV = false;
    int reads = 0;
    float rotate() override { ++reads; return rot; }
    float elevate() override { ++reads; return elev; }
    bool cameraDrivenByKbm() override { return kbm; }
    bool invertHorizontal() override { return invH; }
    bool invertVertical() override { return invV; }
};

struct ScriptedCollision : CameraCollision {
    std::optional<CameraSweepHit> answer;
    int calls = 0;
    Vec3 lastFrom, lastTo;
    double lastRadius = 0;
    std::optional<CameraSweepHit> sweepSphere(Vec3 from, Vec3 to, double radius) override {
        ++calls;
        lastFrom = from;
        lastTo = to;
        lastRadius = radius;
        return answer;
    }
};

// A SYNTHETIC submode record built through 0x0056ad50.
SubmodeRecord makeRecord(Vec3 lookat, double z, double y, double minE, double maxE, double fov, double blendTime,
                         double xShift = 0) {
    SubmodeRecord r;
    r.lookatOffset = lookat;
    r.minElevation = minE;
    r.maxElevation = maxE;
    r.baseFov = fov;
    r.blendTime = blendTime;
    r.xShift = xShift;
    SetPolarFromAuthored(r, z, y);
    return r;
}

const Vec3 kPivot0{10, 1.5, 20};  // target (10, 0, 20) + look-at (0, 1.5, 0), any heading

struct Rig {
    ScriptedLook look;
    ScriptedCollision coll;
    CameraSystem cam{look};
    explicit Rig(bool doReset = true) {
        SeedFollowCameraOnFootCommonCase(cam);
        cam.world.controlsDisabled.set(false);
        cam.world.sprintPadDescriptorByte5.set(false);
        CameraFreeConstants& c = cam.constants;
        c.slowPanH.set(0.5);
        c.slowPanV.set(0.25);
        c.fastPanH.set(2.0);
        c.fastPanV.set(1.0);
        c.fastPanInputThreshold.set(0.9);
        c.hpanThreshold.set(0.1);
        c.vpanThreshold.set(0.1);
        c.pegAccelHMin.set(1);
        c.pegAccelHMax.set(3);
        c.pegAccelVMin.set(1);
        c.pegAccelVMax.set(3);
        c.defaultSwingRate.set(0.7);
        // Submode 0: pivot 1.5 up, camera 4 behind, level (z 4, y 0).
        cam.submodes[0] = makeRecord({0, 1.5, 0}, 4, 0, -1.0, 1.0, 50, 0.5);
        cam.target.valid = true;
        cam.target.id = 1;
        cam.target.referencePoint = {10, 0, 20};
        cam.target.heading = 0;
        cam.target.sizeFactor.set(1.0);
        cam.target.noCollision.set(false);
        cam.collision = &coll;
        cam.init();
        if (doReset) cam.reset();
    }
    static CameraFrameTimes times(double dt = 0.1) { return CameraFrameTimes{dt, dt, dt}; }
    void frame(double dt = 0.1) {
        cam.cameraHook();
        cam.preUpdate(dt);
        cam.update(times(dt));
    }
};

// ===================================================================
// Sec4.2 - mode stack semantics
// ===================================================================
void testModeStack() {
    ScriptedLook look;
    CameraSystem cam(look);
    std::vector<int> enters;
    const auto counting = [&enters](int m) {
        CameraModeRow r;
        r.identity = "test row";
        r.implemented = true;
        r.hasEnter = true;
        r.enter = [&enters, m] { enters.push_back(m); };
        return r;
    };
    for (int m : {0, 3, 6, 9}) cam.installRow(m, counting(m));
    SeedFollowCameraOnFootCommonCase(cam);
    // Statics (Sec4.2 table): current 0, count 0, base 0, flags 0.
    CHECK(cam.currentMode() == 0 && cam.stackCount() == 0 && cam.baseMode() == 0 && cam.flags() == 0);

    cam.push(3);
    CHECK(cam.currentMode() == 3 && cam.stackCount() == 1 && enters == std::vector<int>{3});
    cam.push(6);
    CHECK(cam.currentMode() == 6 && cam.stackCount() == 2 && cam.stackAt(0) == 3 && cam.stackAt(1) == 6);
    // Re-push moves an already-stacked mode to the top - no duplicate. Only
    // the INCOMING row's enter runs (there is no exit slot at all).
    cam.push(3);
    CHECK(cam.stackCount() == 2 && cam.stackAt(0) == 6 && cam.stackAt(1) == 3 && cam.currentMode() == 3);
    CHECK((enters == std::vector<int>{3, 6, 3}));
    // Re-push of the current top: same stack, no enter.
    cam.push(3);
    CHECK(cam.stackCount() == 2 && enters.size() == 3);
    // Pop by value, out of order: removes 6 from below; current stays 3, no enter.
    cam.pop(6);
    CHECK(cam.stackCount() == 1 && cam.stackAt(0) == 3 && cam.currentMode() == 3 && enters.size() == 3);
    // Pop of an absent mode: nothing removed, current recomputed (unchanged).
    cam.pop(9);
    CHECK(cam.stackCount() == 1 && cam.currentMode() == 3 && enters.size() == 3);
    // Set-base with a non-empty stack only records the base.
    cam.setBase(9, true, 0.4);
    CHECK(cam.baseMode() == 9 && cam.currentMode() == 3 && enters.size() == 3);
    CHECK((cam.flags() & camflag::kTransitionBlend) == 0);
    // The pop that empties the stack selects the base - WITHOUT a blend.
    cam.pop(3);
    CHECK(cam.stackCount() == 0 && cam.currentMode() == 9 && enters.back() == 9);
    CHECK((cam.flags() & camflag::kTransitionBlend) == 0);
    // Pop with an empty stack: nothing at all.
    cam.pop(9);
    CHECK(cam.currentMode() == 9 && enters.size() == 4);
    // Set-base with an empty stack and blend: current changes, the transition
    // blend starts (bit 0x02), then the enter.
    cam.setBase(0, true, 0.4);
    CHECK(cam.currentMode() == 0 && enters.back() == 0 && (cam.flags() & camflag::kTransitionBlend) != 0);
    // A push that changes the mode clears bit 0x02.
    cam.push(6);
    CHECK((cam.flags() & camflag::kTransitionBlend) == 0 && cam.currentMode() == 6);
    // A pop clears bit 0x02 even when the mode does not change.
    cam.startTransitionBlend(0.4, 7);
    cam.pop(9);
    CHECK((cam.flags() & camflag::kTransitionBlend) == 0 && cam.currentMode() == 6);
    // Set-base without blend: no bit.
    cam.pop(6);
    cam.setBase(3, false, 0);
    CHECK(cam.currentMode() == 3 && (cam.flags() & camflag::kTransitionBlend) == 0);

    // Reset 0x00566830: current <- 0 and count <- 0 WITHOUT an enter, base
    // untouched, flags cleared.
    cam.push(6);
    cam.requestSnap();
    const size_t before = enters.size();
    cam.target.valid = false;  // no target: the snap's placement is skipped
    cam.reset();
    CHECK(cam.currentMode() == 0 && cam.stackCount() == 0 && cam.baseMode() == 3 && cam.flags() == 0);
    CHECK(enters.size() == before);

    // Unimplemented rows: selecting throws OPEN before the stack changes.
    ScriptedLook look2;
    CameraSystem fresh(look2);
    CHECK(throwsOpen([&] { fresh.push(3); }, "camera mode 3"));
    CHECK(throwsOpen([&] { fresh.push(9); }, "death camera"));
    CHECK(throwsOpen([&] { fresh.setBase(11, true, 0.4); }, "camera mode 11"));
    CHECK(fresh.stackCount() == 0 && fresh.currentMode() == 0 && fresh.baseMode() == 0);
    // Rows 4, 5, 8 are empty in the real table (Sec4.1): selectable, nothing runs.
    fresh.push(4);
    CHECK(fresh.currentMode() == 4 && fresh.stackCount() == 1);
    fresh.push(8);
    fresh.pop(4);
    CHECK(fresh.currentMode() == 8 && fresh.stackCount() == 1);
    // Emptying the stack returns to base 0 and runs the REAL follow enter
    // 0x005732b0, which reads default_swing_rate (OPEN on this unseeded system).
    CHECK(throwsOpen([&] { fresh.pop(8); }, "default_swing_rate"));
    CHECK(fresh.currentMode() == 0 && fresh.stackCount() == 0);
    bool threw = false;
    try {
        fresh.push(18);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
    // CHOSEN: a ninth distinct push refuses (the engine has no check).
    for (int m = 1; m < kNumCameraModes; ++m) {
        CameraModeRow r;
        r.implemented = true;
        fresh.installRow(m, r);
    }
    for (int m = 1; m <= 8; ++m) fresh.push(m);
    threw = false;
    try {
        fresh.push(9);
    } catch (const std::length_error&) {
        threw = true;
    }
    CHECK(threw && fresh.stackCount() == 8 && fresh.currentMode() == 8);
}

// ===================================================================
// Sec4.2 init and reset, Sec4.11 step 5 snap
// ===================================================================
void testInitResetSnap() {
    Rig r(false);
    // Init: three FOVs 60, committed rows identity, loader gains 1.0.
    CHECK(r.cam.committed().fov == 60 && r.cam.working().fov == 60 && r.cam.previous().fov == 60);
    CHECK(veq(r.cam.committed().right, {1, 0, 0}) && veq(r.cam.committed().forward, {0, 0, 1}));
    CHECK(r.cam.angles.yawGain == 1.0 && r.cam.angles.padAccelPitch == 1.0);
    // Row 0's reset slot is OPEN until a hook is given.
    r.cam.hooks.row0Reset = nullptr;
    CHECK(throwsOpen([&] { r.cam.reset(); }, "0x005749e0"));
    r.cam.hooks.row0Reset = [] {};
    r.cam.target.heading = 0.5;
    const int callsBefore = r.coll.calls;
    r.cam.reset();
    // Snap: submode 0 installed complete, heading = target heading, the
    // camera behind the target; the same transform in all three sets.
    CHECK(r.cam.blend.current == 0 && r.cam.blend.progress == 1.0);
    CHECK(deq(r.cam.angles.heading, 0.5));
    CHECK(veq(r.cam.blend.pivot, kPivot0));
    const CameraRotation m = RotationFromAngles(0, 0, 0.5);
    const Vec3 expect = kPivot0 + m.forward * -4.0;
    CHECK(veq(r.cam.working().position, expect) && veq(r.cam.committed().position, expect) &&
          veq(r.cam.previous().position, expect));
    CHECK(veq(r.cam.committed().forward, m.forward) && veq(r.cam.previous().right, m.right));
    // The sweep ran pivot -> desired with radius 0.12 (Sec4.9).
    CHECK(r.coll.calls == callsBefore + 1 && veq(r.coll.lastFrom, kPivot0) && veq(r.coll.lastTo, expect));
    CHECK(r.coll.lastRadius == 0.12);
    CHECK(veq(r.cam.targetSnapshotPosition(), {10, 0, 20}));

    // Snap request (bit 0x04) is honoured by the next update and consumed.
    r.cam.preUpdate(0.1);
    r.cam.target.heading = -1.0;
    r.cam.requestSnap();
    r.cam.update(Rig::times());
    CHECK((r.cam.flags() & camflag::kSnapRequested) == 0);
    CHECK(deq(r.cam.angles.heading, -1.0));
    const Vec3 e2 = kPivot0 + RotationFromAngles(0, 0, -1.0).forward * -4.0;
    CHECK(veq(r.cam.committed().position, e2) && veq(r.cam.previous().position, e2));
}

// ===================================================================
// Sec3.4 look slot, Sec4.6 yaw/pitch steps (pad vs mouse)
// ===================================================================
void testLookAndAngles() {
    // Pad, inside fast_pan_input_threshold: rate = yaw * s * slow (s = 1,
    // no dampening for submode 0); heading += gain * rate * factor * step.
    {
        Rig r;
        std::vector<double> gave;
        r.cam.onPlayerGaveInput = [&gave](double b) { gave.push_back(b); };
        r.look.rot = 0.5f;
        r.look.elev = 0.4f;
        r.cam.cameraHook();
        CHECK(r.cam.angles.lookYaw == 0.5 && deq(r.cam.angles.lookPitch, 0.4f));  // no inversion
        CHECK(gave.size() == 1 && gave[0] == 0.3);
        CHECK(r.cam.angles.swingRequest == -1.0 && r.cam.angles.swingRate == 1.0);  // camera hook
        r.cam.preUpdate(0.1);
        CHECK(deq(r.cam.angles.yawRate, 0.25));
        CHECK(deq(r.cam.angles.heading, 0.025));                    // 1 * 0.25 * 1 * 0.1
        CHECK(deq(r.cam.angles.pitchRate, 0.4f * 0.25, 1e-7));      // 0.4 * 1 * 0.25
        CHECK(deq(r.cam.angles.userPitch, 0.4f * 0.25 * 0.1, 1e-8));
        CHECK(deq(r.cam.angles.pitch, r.cam.angles.userPitch));      // base elevation 0
        const CameraRotation m = RotationFromAngles(r.cam.angles.pitch, 0, 0.025);
        CHECK(veq(r.cam.working().forward, m.forward) && veq(r.cam.working().right, m.right));
        CHECK(veq(r.cam.committed().forward, {0, 0, 1}));  // not committed yet
        // Scripted yaw/pitch rates add * step and are zeroed by every pre-update.
        r.cam.update(Rig::times());
        CHECK(r.cam.angles.lookYaw == 0 && r.cam.angles.lookPitch == 0);  // consumed by the update
        CHECK(r.cam.angles.swingRequestLast == -1.0 && r.cam.angles.swingRequest == 1.0 && r.cam.angles.swingRate == 0.7);
        r.look.rot = r.look.elev = 0;
        r.cam.angles.scriptYawRate = 2.0;
        r.cam.cameraHook();
        r.cam.preUpdate(0.1);
        CHECK(deq(r.cam.angles.heading, 0.025 + 0.2));
        CHECK(r.cam.angles.scriptYawRate == 0 && r.cam.angles.scriptPitchRate == 0);
    }
    // Mouse: rate = yaw * s, and NO time step (per-frame delta).
    {
        Rig r;
        r.look.kbm = true;
        r.look.rot = 0.5f;
        r.look.elev = 0.4f;
        r.cam.cameraHook();
        r.cam.preUpdate(0.1);
        CHECK(deq(r.cam.angles.heading, 0.5));
        CHECK(deq(r.cam.angles.userPitch, 0.4f, 1e-7));
        // Same input with dt 0.02: identical deltas (no step dependence).
        r.cam.update(Rig::times());
        r.cam.cameraHook();
        r.cam.preUpdate(0.02);
        CHECK(deq(r.cam.angles.heading, 1.0));
    }
    // Inversion (0x0056b100 / 0x0056b120): yaw = -rot, pitch = -elev.
    {
        Rig r;
        r.look.rot = 0.5f;
        r.look.elev = 0.4f;
        r.look.invH = r.look.invV = true;
        r.cam.cameraHook();
        CHECK(r.cam.angles.lookYaw == -0.5 && deq(r.cam.angles.lookPitch, -0.4f));
    }
    // Look slot gates (Sec3.4 step 1): controls disabled / transition blend.
    {
        Rig r;
        r.look.rot = 0.5f;
        r.cam.world.controlsDisabled.set(true);
        r.cam.cameraHook();
        CHECK(r.look.reads == 0 && r.cam.angles.lookYaw == 0);
        r.cam.world.controlsDisabled.set(false);
        r.cam.startTransitionBlend(0.4, 7);
        r.cam.cameraHook();
        CHECK(r.look.reads == 0 && r.cam.angles.lookYaw == 0);
        r.cam.cancelTransitionBlend();
        r.cam.cameraHook();
        CHECK(r.look.reads == 2 && r.cam.angles.lookYaw == 0.5);
        // Bound to another owner's controls-disabled bit.
        OpenValue<bool> other{"other +0x1d99", "test"};
        other.set(true);
        r.cam.world.bindControlsDisabled(other);
        r.cam.angles.lookYaw = 0;
        r.cam.cameraHook();
        CHECK(r.cam.angles.lookYaw == 0);
    }
    // Heading wrap into [-pi, pi).
    CHECK(deq(WrapAngle(3.5), 3.5 - 2 * 3.14159265358979323846));
    CHECK(deq(WrapAngle(-3.14159265358979323846), -3.14159265358979323846));
}

// ===================================================================
// Sec4.6 pitch clamp to [min - base, max - base]; default-elevation reset
// ===================================================================
void testPitchClamp() {
    Rig r(false);
    // z 4, y 3: base elevation atan(0.75), distance 5 (0x0056ad50).
    r.cam.submodes[0] = makeRecord({0, 1.5, 0}, 4, 3, -0.2, 0.9, 50, 0.5);
    r.cam.reset();
    const double base = std::atan(0.75);
    CHECK(deq(r.cam.blend.blended->baseElevation, base) && deq(r.cam.blend.blended->followDistance, 5));
    r.look.kbm = true;
    r.look.elev = 5.0f;
    r.cam.cameraHook();
    r.cam.preUpdate(0.1);
    CHECK(deq(r.cam.angles.userPitch, 0.9 - base));
    CHECK(deq(r.cam.angles.pitch, 0.9));
    r.cam.update(Rig::times());
    r.look.elev = -5.0f;
    r.cam.cameraHook();
    r.cam.preUpdate(0.1);
    CHECK(deq(r.cam.angles.userPitch, -0.2 - base));
    CHECK(deq(r.cam.angles.pitch, -0.2));
    r.cam.update(Rig::times());
    // Default elevation: with the blend incomplete and no fine-aim class
    // change, +0x24 = default - base.
    r.look.elev = 0;
    r.cam.blend.blended->hasDefaultElevation = true;
    r.cam.blend.blended->defaultElevation = 0.5;
    r.cam.blend.progress = 0.5;
    r.cam.angles.lookPitch = 0;
    r.cam.pitchStep(0.1, false);
    CHECK(deq(r.cam.angles.userPitch, 0.5 - base));
    // ... not when the fine-aim class changed ...
    r.cam.angles.userPitch = 0;
    r.cam.blend.fineAimClassChanged = true;
    r.cam.pitchStep(0.1, false);
    CHECK(deq(r.cam.angles.userPitch, 0));
    // ... but once after a request.
    r.cam.blend.defaultElevationRequest = true;
    r.cam.pitchStep(0.1, false);
    CHECK(deq(r.cam.angles.userPitch, 0.5 - base) && !r.cam.blend.defaultElevationRequest);
    r.cam.angles.userPitch = 0;
    r.cam.pitchStep(0.1, false);
    CHECK(deq(r.cam.angles.userPitch, 0));
}

// ===================================================================
// Sec4.6 pan rate: pad fast path, thresholds, dampening
// ===================================================================
void testPanRates() {
    // Beyond fast_pan_input_threshold the two helpers are OPEN.
    {
        Rig r;
        r.cam.angles.lookYaw = 1.0;
        CHECK(throwsOpen([&] { r.cam.panRateYaw(0.1, false); }, "0x00dade20"));
    }
    // With injected helpers: candidate = prev + a * yaw * s, kept between
    // yaw*s*slow and yaw*s*fast, then eased (here: taken as is).
    {
        Rig r;
        std::vector<double> args;
        r.cam.hooks.panAccelFactor = [&args](double p, double lo, double hi, double mn, double mx) {
            args = {p, lo, hi, mn, mx};
            return 0.5;
        };
        r.cam.hooks.panRateEase = [](double, double cand, double) { return cand; };
        r.cam.angles.lookYaw = 1.0;
        r.cam.angles.yawRate = 1.0;
        CHECK(deq(r.cam.panRateYaw(0.1, false), 1.5));
        CHECK((args == std::vector<double>{1.0, 0.1, 10.0, 1.0, 3.0}));
        r.cam.angles.yawRate = 1.9;
        CHECK(deq(r.cam.panRateYaw(0.1, false), 2.0));  // capped at yaw * s * fast
        r.cam.angles.yawRate = -5;
        CHECK(deq(r.cam.panRateYaw(0.1, false), 0.5));  // floored at yaw * s * slow
        // |yaw| <= hpan_threshold with a large pitch: the yaw rate is 0.
        r.cam.angles.lookYaw = 0.05;
        r.cam.angles.lookPitch = 1.0;
        CHECK(r.cam.panRateYaw(0.1, false) == 0 && r.cam.angles.yawRate == 0);
        // Fine (zoom) scale needs the weapon system.
        CHECK(throwsOpen([&] { r.cam.panRateYaw(0.1, true); }, "zoom scale"));
    }
    // Dampening by submode, first match; mouse uses the _mouse copies.
    {
        Rig r;
        r.look.kbm = true;
        r.cam.angles.lookYaw = 1.0;
        r.cam.blend.current = submode::kInteriorClose;
        CHECK(throwsOpen([&] { r.cam.panRateYaw(0.1, false); }, "interior_h_dampening"));
        r.cam.constants.mouse.interior.h.set(0.3);
        r.cam.constants.pad.interior.h.set(0.6);
        CHECK(deq(r.cam.panRateYaw(0.1, false), 0.3));
        r.look.kbm = false;
        r.cam.angles.lookYaw = 0.5;
        CHECK(deq(r.cam.panRateYaw(0.1, false), 0.5 * 0.6 * 0.5));
        // Heavy weapon comes before the interior rule.
        r.cam.world.heavyWeapon.set(true);
        r.cam.constants.pad.heavyWeapon.h.set(0.2);
        CHECK(deq(r.cam.panRateYaw(0.1, false), 0.5 * 0.2 * 0.5));
        // Freefall: horizontal only; vertical not dampened.
        r.cam.world.heavyWeapon.set(false);
        r.cam.blend.current = submode::kFreefall;
        r.cam.constants.pad.freefallH.set(0.1);
        r.cam.angles.lookPitch = 0.4;
        CHECK(deq(r.cam.panRatePitch(0.1, false), 0.4 * 1.0 * 0.25));
        // Tank needs the vehicle's name hash.
        r.cam.blend.current = submode::kTankDriver;
        CHECK(throwsOpen([&] { r.cam.panRateYaw(0.1, false); }, "tank"));
    }
}

// ===================================================================
// Sec4.8 blend: four kinds, 15-unit snap, durations
// ===================================================================
struct BlendRig : Rig {
    BlendKind kind = BlendKind::Linear;
    BlendRig() {
        cam.submodes[31] = makeRecord({0, 1.5, 0}, 6, 0, -1, 1, 70, 0.5, 0.4);  // "sprint"
        cam.world.submodeFlags[31] = 0u;
        cam.world.blendKindFor = [this](int, int) { return kind; };
        cam.world.humanSubmode.set(31);
    }
};

void testBlendKinds() {
    const double st = 0.125;  // 0.125 / 0.5 = 0.25 per step, exact in binary
    // Linear: t = progress.
    {
        BlendRig r;
        r.cam.blendUpdate(st);
        CHECK(r.cam.blend.current == 31 && r.cam.blend.fromSubmode == 0 && deq(r.cam.blend.duration, 0.5));
        CHECK(deq(r.cam.blend.progress, 0.25));
        CHECK(deq(r.cam.blend.blended->followDistance, 4.5) && deq(r.cam.blend.blended->baseFov, 55));
        CHECK(deq(r.cam.blend.blended->xShift, 0.1));
        CHECK(deq(r.cam.blend.previous->followDistance, 4));  // +0x90: before this step
        r.cam.blendUpdate(st);
        CHECK(deq(r.cam.blend.blended->followDistance, 5) && deq(r.cam.blend.previous->followDistance, 4.5));
        r.cam.blendUpdate(st);
        r.cam.blendUpdate(st);
        CHECK(r.cam.blend.progress == 1.0 && deq(r.cam.blend.blended->followDistance, 6));
        r.cam.blendUpdate(st);  // complete: holds the target record
        CHECK(deq(r.cam.blend.blended->followDistance, 6) && deq(r.cam.blend.previous->followDistance, 6));
    }
    // Eased: t = 2p^2 (p <= 1/2), 1 - 2(1-p)^2 (p > 1/2) - constant
    // acceleration to the midpoint, then constant deceleration.
    {
        BlendRig r;
        r.kind = BlendKind::Eased;
        const double expect[] = {4 + 2 * 0.125, 4 + 2 * 0.5, 4 + 2 * 0.875, 6};
        for (double e : expect) {
            r.cam.blendUpdate(st);
            CHECK(deq(r.cam.blend.blended->followDistance, e));
        }
        CHECK(deq(EaseInOut(0.25), 0.125) && deq(EaseInOut(0.5), 0.5) && deq(EaseInOut(0.75), 0.875));
    }
    // Hold: the "from" record until progress reaches 1, then switch.
    {
        BlendRig r;
        r.kind = BlendKind::Hold;
        for (int i = 0; i < 3; ++i) {
            r.cam.blendUpdate(st);
            CHECK(deq(r.cam.blend.blended->followDistance, 4) && r.cam.blend.blended->baseFov == 50);
        }
        r.cam.blendUpdate(st);
        CHECK(deq(r.cam.blend.blended->followDistance, 6) && r.cam.blend.blended->baseFov == 70);
    }
    // Snap: the target record at once, progress 1, camera cut flagged.
    {
        BlendRig r;
        r.kind = BlendKind::Snap;
        r.cam.clearCameraCutFlag();
        r.cam.blendUpdate(st);
        CHECK(r.cam.blend.progress == 1.0 && deq(r.cam.blend.blended->followDistance, 6) && r.cam.cameraCutFlag());
    }
    // 15-unit rule: look-at offsets more than 15 apart (squared > 225) snap
    // whatever the kind; exactly 15 apart does not.
    {
        BlendRig r;
        r.cam.submodes[31]->lookatOffset = {16, 1.5, 0};
        r.cam.clearCameraCutFlag();
        r.cam.blendUpdate(st);
        CHECK(r.cam.blend.progress == 1.0 && r.cam.cameraCutFlag());
        CHECK(veq(r.cam.blend.blended->lookatOffset, {16, 1.5, 0}));
    }
    {
        BlendRig r;
        r.cam.submodes[31]->lookatOffset = {15, 1.5, 0};
        r.cam.clearCameraCutFlag();
        r.cam.blendUpdate(st);
        CHECK(deq(r.cam.blend.progress, 0.25) && !r.cam.cameraCutFlag());
        CHECK(veq(r.cam.blend.blended->lookatOffset, {3.75, 1.5, 0}));
        // The pivot follows the blended look-at offset (heading 0).
        CHECK(veq(r.cam.blend.pivot, {13.75, 1.5, 20}));
    }
    // x-shift sign flip (B +0x20).
    {
        BlendRig r;
        r.cam.world.xShiftFlip.set(true);
        r.cam.blendUpdate(st);
        CHECK(deq(r.cam.blend.blended->xShift, -0.1));
    }
    // A restart zeroes +0x2c and records the fine-aim class change.
    {
        BlendRig r;
        r.cam.angles.pitchTerm2c = 0.3;
        r.cam.submodes[20] = makeRecord({0, 1.5, 0}, 2, 0, -1, 1, 40, 0.5);
        r.cam.world.submodeFlags[20] = 0u;
        r.cam.world.humanSubmode.set(20);  // "fine aim"
        r.cam.blendUpdate(st);
        CHECK(r.cam.angles.pitchTerm2c == 0 && r.cam.blend.fineAimClassChanged);
    }
}

void testBlendDurations() {
    // override_exit_blend_time on the "from" record: its blend_time wins.
    {
        BlendRig r;
        r.cam.submodes[0]->overrideExitBlendTime = true;
        r.cam.submodes[0]->blendTime = 0.25;
        r.cam.blendUpdate(0.125);
        CHECK(deq(r.cam.blend.duration, 0.25) && deq(r.cam.blend.progress, 0.5));
    }
    // exterior close <-> interior close: Exterior_Interior_Blend_Time.
    {
        BlendRig r;
        r.cam.submodes[1] = makeRecord({0, 1.5, 0}, 3, 0, -1, 1, 60, 0.5);
        r.cam.world.submodeFlags[1] = 0u;
        r.cam.world.humanSubmode.set(1);
        CHECK(throwsOpen([&] { r.cam.blendUpdate(0.125); }, "Exterior_Interior_Blend_Time"));
        r.cam.constants.exteriorInteriorBlendTime.set(1.0);
        BlendRig r2;
        r2.cam.submodes[1] = r.cam.submodes[1];
        r2.cam.world.submodeFlags[1] = 0u;
        r2.cam.world.humanSubmode.set(1);
        r2.cam.constants.exteriorInteriorBlendTime.set(1.0);
        r2.cam.blendUpdate(0.125);
        CHECK(deq(r2.cam.blend.duration, 1.0));
    }
    // Ragdolling player: Ragdoll_Blend_Time.
    {
        BlendRig r;
        r.cam.world.playerRagdolling.set(true);
        r.cam.constants.ragdollBlendTime.set(2.0);
        r.cam.blendUpdate(0.125);
        CHECK(deq(r.cam.blend.duration, 2.0));
    }
    // Same target and 0x013c9362: instant (duration 0 -> complete at once).
    {
        BlendRig r;
        r.cam.world.instantBlend13c9362.set(true);
        r.cam.blendUpdate(0.125);
        CHECK(r.cam.blend.duration == 0 && r.cam.blend.progress == 1.0 && deq(r.cam.blend.blended->followDistance, 6));
    }
    // A changed target restarts the blend even with the same submode.
    {
        Rig r;
        r.cam.world.blendKindFor = [](int, int) { return BlendKind::Linear; };
        r.cam.target.id = 2;
        r.cam.blendUpdate(0.125);
        CHECK(r.cam.blend.lastTarget == 2 && deq(r.cam.blend.progress, 0.25));
    }
    // zoom with a positive duration starts the hold timer 0x00573890 (OPEN).
    {
        BlendRig r;
        r.cam.submodes[12] = makeRecord({0, 1.5, 0}, 1, 0, -1, 1, 30, 0.5);
        r.cam.world.submodeFlags[12] = 0u;
        r.cam.world.humanSubmode.set(12);
        CHECK(throwsOpen([&] { r.cam.blendUpdate(0.125); }, "0x00573890"));
    }
    // The kind source is not given: OPEN until supplied.
    {
        Rig r;
        r.cam.submodes[31] = makeRecord({0, 1.5, 0}, 6, 0, -1, 1, 70, 0.5);
        r.cam.world.submodeFlags[31] = 0u;
        r.cam.world.humanSubmode.set(31);
        CHECK(throwsOpen([&] { r.cam.blendUpdate(0.125); }, "blend kind"));
    }
}

// ===================================================================
// Sec4.7 step 9 sway; step 8 helicopter-pitch decay
// ===================================================================
void testSway() {
    Rig r;
    r.cam.hooks.rollSmoother = [](double, double target, double) { return target; };
    r.cam.swayHeadingAmp = 0.2;
    r.cam.swayRollAmp = 0.1;
    r.cam.angles.swayPhase = 1.0;
    r.cam.angles.heliPitch = 1.0;
    r.cam.preUpdate(0.1);
    // heliPitch is part of the pitch sum the pre-update builds; capture
    // after the core's decay instead.
    r.cam.update(CameraFrameTimes{0.1, 0.05, 0.1});
    const double effH = 0 + std::sin(1.0) * 0.2;
    const double rollT = 0 + std::sin(0.9) * 0.1 + 0;
    CHECK(deq(r.cam.angles.roll, rollT));
    CHECK(deq(r.cam.angles.heliPitch, 0.96));  // 4 % toward 0 per update
    CHECK(deq(r.cam.swayHeadingAmp, 0.18) && deq(r.cam.swayRollAmp, 0.09));  // decay 10 % per update
    CHECK(deq(r.cam.angles.swayPhase, 1.05));  // += real frame time 0x0132a0b0
    CHECK(deq(r.cam.angles.heading, 0));       // the stored heading carries no sway
    const CameraRotation m = RotationFromAngles(0.96, rollT, effH);
    CHECK(veq(r.cam.committed().forward, m.forward) && veq(r.cam.committed().up, m.up));
    CHECK(veq(r.cam.committed().position, kPivot0 + m.forward * -4.0));
    // Without a roll smoother, a changing roll target is OPEN.
    r.cam.hooks.rollSmoother = nullptr;
    r.cam.preUpdate(0.1);
    CHECK(throwsOpen([&] { r.cam.update(Rig::times()); }, "0x00dadf70"));
}

// ===================================================================
// Sec4.7 step 13 placement, Sec4.9 collision, step 14 shoulder offset
// ===================================================================
void testPlacement() {
    // Collision-resolved: the camera sits at the hit's sweep position.
    {
        Rig r;
        r.coll.answer = CameraSweepHit{{10, 1.5, 18}, {0, 0, -1}};
        r.cam.clearCameraCutFlag();
        r.frame();
        CHECK(veq(r.cam.committed().position, {10, 1.5, 18}) && r.cam.cameraCutFlag());
        CHECK(veq(r.coll.lastFrom, kPivot0) && veq(r.coll.lastTo, {10, 1.5, 16}));
        // The path clears: straight back to the full distance (no easing).
        r.coll.answer.reset();
        r.frame();
        CHECK(veq(r.cam.committed().position, {10, 1.5, 16}));
    }
    // No-collision flag: desired as is, no sweep.
    {
        Rig r;
        r.cam.target.noCollision.set(true);
        const int calls = r.coll.calls;
        r.frame();
        CHECK(r.coll.calls == calls && veq(r.cam.committed().position, {10, 1.5, 16}));
    }
    // Within 1e-5 of the pivot: the camera sits at the pivot, no sweep.
    {
        Rig r(false);
        r.cam.submodes[0] = makeRecord({0, 1.5, 0}, 0, 0, -1, 1, 50, 0.5);  // z 0 -> distance 0
        r.cam.reset();
        r.frame();
        CHECK(r.coll.calls == 0 && veq(r.cam.committed().position, kPivot0));
    }
    // No collision world: OPEN.
    {
        Rig r(false);
        r.cam.collision = nullptr;
        CHECK(throwsOpen([&] { r.cam.reset(); }, "0x0075c880"));
    }
    // Pivot: the look-at height scales with the size factor; the offset
    // rotates with the target heading (CHOSEN convention).
    {
        Rig r(false);
        r.cam.submodes[0] = makeRecord({1, 2, 0}, 4, 0, -1, 1, 50, 0.5);
        r.cam.target.sizeFactor.set(0.5);
        r.cam.target.heading = 3.14159265358979323846 / 2;  // +x forward: right = (0, 0, -1)
        r.cam.reset();
        CHECK(veq(r.cam.blend.pivot, {10, 1, 19}));
    }
    // Shoulder offset: x_shift * row 1, kept in 0x013c8a60, swept again.
    {
        Rig r(false);
        r.cam.submodes[0] = makeRecord({0, 1.5, 0}, 4, 0, -1, 1, 50, 0.5, 0.5);
        r.cam.reset();
        r.cam.preUpdate(0.1);
        const int calls = r.coll.calls;
        r.cam.update(Rig::times());
        CHECK(r.coll.calls == calls + 2);
        CHECK(veq(r.cam.lateralOffset, {0.5, 0, 0}));
        CHECK(veq(r.coll.lastFrom, {10, 1.5, 16}) && veq(r.coll.lastTo, {10.5, 1.5, 16}));
        CHECK(veq(r.cam.committed().position, {10.5, 1.5, 16}));
        // Next frame: the offset is re-added then recomputed - same result.
        r.frame();
        CHECK(veq(r.cam.committed().position, {10.5, 1.5, 16}));
    }
}

// ===================================================================
// Sec4.7 FOV, Sec4.11 step 6 FOV smoothing
// ===================================================================
void testFov() {
    Rig r;
    CHECK(r.cam.committed().fov == 60);  // init; the snap leaves FOV alone
    r.cam.setSmoothFov(true);
    r.cam.preUpdate(0.1);
    r.cam.update(Rig::times(0.1));  // k = clamp01(5 * 0.1) = 0.5
    CHECK(r.cam.working().fov == 50 && deq(r.cam.committed().fov, 55));
    CHECK(deq(r.cam.previous().fov, 60));
    r.frame(0.1);
    CHECK(deq(r.cam.committed().fov, 52.5));
    r.cam.update(CameraFrameTimes{0.1, 0.1, 1.0});  // k clamps to 1
    CHECK(deq(r.cam.committed().fov, 50));
    r.cam.setSmoothFov(false);
    r.cam.world.fovFactor.set(1.2);  // 0x00975bd0
    r.frame();
    CHECK(deq(r.cam.committed().fov, 60));
    // Base FOV +0x104 is not refreshed while the player is in scope view.
    r.cam.world.fovFactor.set(1.0);
    r.cam.blend.blended->baseFov = 80;
    r.cam.world.playerScopeView.set(true);
    r.frame();
    CHECK(deq(r.cam.committed().fov, 50));
    r.cam.world.playerScopeView.set(false);
    r.frame();
    CHECK(deq(r.cam.committed().fov, 80));
}

// ===================================================================
// Sec4.10 / Sec4.11: working -> committed -> previous; consumers see committed
// ===================================================================
void testCommitOrdering() {
    Rig r;
    // No pre-update yet: the step 0x013c88b4 is OPEN.
    CHECK(throwsOpen([&] { r.cam.update(Rig::times()); }, "0x013c88b4"));
    const CameraTransform c0 = r.cam.committed();
    r.look.kbm = true;
    r.look.rot = 0.3f;
    r.cam.cameraHook();
    r.cam.preUpdate(0.1);
    // The pre-update moved the WORKING rows only.
    CHECK(!veq(r.cam.working().forward, c0.forward) && veq(r.cam.committed().forward, c0.forward));
    CHECK(veq(r.cam.movementRows().row2, r.cam.working().forward) && veq(r.cam.movementRows().row0, r.cam.working().right));
    r.cam.update(Rig::times());
    // previous <- the old committed set; committed <- working.
    CHECK(veq(r.cam.previous().position, c0.position) && veq(r.cam.previous().forward, c0.forward));
    CHECK(veq(r.cam.committed().position, r.cam.working().position) && veq(r.cam.committed().forward, r.cam.working().forward));
    CHECK(!r.cam.cameraCutByte());
    // Without a target the update does nothing at all.
    const CameraTransform c1 = r.cam.committed(), p1 = r.cam.previous();
    r.cam.target.valid = false;
    r.cam.preUpdate(0.1);
    r.cam.update(Rig::times());
    CHECK(veq(r.cam.committed().position, c1.position) && veq(r.cam.previous().position, p1.position));
    r.cam.target.valid = true;
    // Camera-cut byte: the committed position moved 75 units or more.
    r.cam.target.referencePoint = {10, 0, 120};
    r.frame();
    CHECK(r.cam.cameraCutByte());
    r.frame();
    CHECK(!r.cam.cameraCutByte());
    // ... counting the largest direct jump 0x013c8738 (the core skipped once).
    r.cam.setPositionDirect(r.cam.committed().position + Vec3{0, 80, 0});
    r.cam.skipCoreOnce = true;
    r.frame();
    CHECK(r.cam.cameraCutByte() && !r.cam.skipCoreOnce);
    // Target snapshot (0x013c8850).
    CHECK(veq(r.cam.targetSnapshotPosition(), {10, 0, 120}));
}

// ===================================================================
// Sec4.7 surface clamp (HYPOTHESIS: water) and other OPEN gates
// ===================================================================
void testSurfaceAndOpenGates() {
    {
        Rig r;
        r.cam.hooks.surfaceHeights = [](Vec3) { return std::optional<std::pair<double, double>>({1.0, 2.0}); };
        r.frame();
        CHECK(deq(r.cam.committed().position.y, 3.75));  // 1 + 2 + 0.75
        r.cam.hooks.surfaceHeights = [](Vec3) { return std::optional<std::pair<double, double>>({30.0, 0.0}); };
        r.frame();
        CHECK(deq(r.cam.committed().position.y, 1.5));  // 29.25 below: not within 25
    }
    {
        Rig r;
        r.cam.target.isVehicle = true;
        r.cam.preUpdate(0.1);
        CHECK(throwsOpen([&] { r.cam.update(Rig::times()); }, "vehicle"));
    }
    {
        Rig r;
        r.cam.target.isHuman = false;  // another object -> spectator (15), unseeded
        r.cam.preUpdate(0.1);
        CHECK(throwsOpen([&] { r.cam.update(Rig::times()); }, "submode record 15"));
    }
    {
        Rig r;
        r.cam.world.animationDrivenCameraTest.set(true);
        CHECK(throwsOpen([&] { r.cam.preUpdate(0.1); }, "0x00575790"));
    }
    {
        Rig r;
        r.cam.world.targetPhysicsDriven.set(true);
        CHECK(throwsOpen([&] { r.cam.preUpdate(0.1); }, "0x00756790"));
        r.cam.world.physicsQuantisedStep.set(1.0 / 60);
        r.cam.preUpdate(0.1);
        CHECK(deq(*r.cam.lastStep(), 1.0 / 60));
        // ... but not while a transition blend is active.
        r.cam.startTransitionBlend(0.4, 7);
        r.cam.preUpdate(0.1);
        CHECK(deq(*r.cam.lastStep(), 0.1));
        CHECK(throwsOpen([&] { r.cam.update(Rig::times()); }, "0x00580b20"));
    }
    {
        Rig r;
        r.cam.world.pendingRequest8a48.set(2);
        r.cam.preUpdate(0.1);
        CHECK(throwsOpen([&] { r.cam.update(Rig::times()); }, "0x005735b0"));
    }
    {
        Rig r;
        r.cam.world.drunkEffectInactive.set(false);
        CHECK(throwsOpen([&] { r.cam.cameraHook(); }, "0x00975ce0"));
    }
    {
        Rig r;
        r.cam.world.playerVehicleSubState5.set(true);
        CHECK(throwsOpen([&] { r.frame(); }, "sub-state handler 5"));
    }
    // InputSystemLookSource: the pad family reads 0x01411b00 bits 1/2
    // (OPEN before a rebuild); the mouse family the two option bytes.
    {
        InputSystem in;
        InputSystemLookSource src(in);
        CHECK(throwsOpen([&] { src.invertHorizontal(); }, "0x01411b00"));
        in.optionFlagBits0to3 = static_cast<uint8_t>(0x02);
        CHECK(src.invertHorizontal() && !src.invertVertical());
        in.mutableContext().cameraFromKbm = 1;
        CHECK(throwsOpen([&] { src.invertVertical(); }, "0x01411b14"));
        src.mouseInvertV.set(true);
        CHECK(src.invertVertical());
        CHECK(src.rotate() == 0.0f);  // headless: no input
    }
}

// ===================================================================
// Sec4.3 cadence with the real U2/U3 movement (frame N / N+1)
// ===================================================================
struct MoveSrc : MoveInputSource {
    float A = 0, B = 0;
    float walkAxisA() override { return A; }
    float walkAxisB() override { return B; }
    bool gamepadActive() override { return true; }
    bool held(MoveAction, int* out) override {
        if (out) *out = kOutNone;
        return false;
    }
    bool pressed(MoveAction, int* out) override {
        if (out) *out = kOutNone;
        return false;
    }
    void consume(MoveAction) override {}
    bool optionBit0() override { return true; }
};
struct MoveStamina : SprintStamina {
    void notSprintingStep(PlayerMovementState&) override {}
    bool sprintingStep(PlayerMovementState&) override { return true; }
    double available() override { return 1.0; }
    void startPenalty() override {}
    void jumpPenalty() override {}
    void step9ff270() override {}
};

void testFrameCadenceWithMovement() {
    MoveSrc src;
    MoveStamina stamina;
    PlayerMovement pm(src, stamina);
    SeedOnFootCommonCase(pm.world);
    pm.world.controlsDisabled.set(false);
    pm.world.movementDisabled.set(false);
    pm.world.permissions2078.set(0x6000);
    pm.world.orientation.set(Orientation{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}});
    pm.tweaks.maxRunSpeed.set(10);
    pm.tweaks.maxSprintSpeed.set(15);
    pm.tweaks.acceleration.set(20);
    pm.tweaks.crouchClickSensitivity.set(0.5);

    Rig r(false);
    r.cam.target.referencePoint = pm.st.position;  // (0, 0, 0)
    r.cam.reset();
    const double dt = 1.0 / 30.0;
    int64_t now = 1000;
    const auto moveFrame = [&](CameraRows rows) {
        MoveFrameInput f;
        f.nowMs = now;
        f.dt = f.axisDt = dt;
        f.camera = rows;
        pm.runFrame(f);
        now += 33;
    };

    // ---- frame N: look right while pushing forward.
    src.B = 1;
    r.look.rot = 0.5f;  // pad, slow band: rate 0.25 rad/s
    const CameraTransform committedBefore = r.cam.committed();
    // (1) state 2's tail: camera hook -> look slot, then the camera pre-update.
    r.cam.cameraHook();
    r.cam.preUpdate(dt);
    const double h = 0.25 * dt;
    CHECK(deq(r.cam.angles.heading, h));
    // (2) the move slot (post-hook) reads the WORKING rows the pre-update
    // just wrote (Sec2.7 order + Sec3.5.1 step 3) - this frame's heading,
    // not the previous frame's committed rows (Sec4.3 "Consequence" says
    // otherwise; flagged in camera.h).
    const CameraRows rowsN = r.cam.movementRows();
    CHECK(!veq(rowsN.row2, committedBefore.forward));
    moveFrame(rowsN);
    CHECK(veq(pm.st.desiredMove, Vec3{std::sin(h), 0, std::cos(h)} * 10.0, 1e-9));
    const Vec3 playerAfterN = pm.st.position;
    CHECK(Length(playerAfterN) > 0);  // the integrator moved the body this frame
    // (3) after character movement: the camera update follows the CURRENT position.
    r.cam.target.referencePoint = pm.st.position;
    r.cam.update(Rig::times(dt));
    const CameraRotation mN = RotationFromAngles(0, 0, h);
    CHECK(veq(r.cam.committed().position, playerAfterN + Vec3{0, 1.5, 0} + mN.forward * -4.0));
    CHECK(veq(r.cam.committed().forward, mN.forward));

    // ---- frame N+1, no look input: the pre-update rebuilds the same rows,
    // so the move slot sees exactly what frame N committed.
    r.look.rot = 0;
    r.cam.cameraHook();
    r.cam.preUpdate(dt);
    CHECK(veq(r.cam.movementRows().row2, r.cam.committed().forward));
    moveFrame(r.cam.movementRows());
    r.cam.target.referencePoint = pm.st.position;
    r.cam.update(Rig::times(dt));
    CHECK(veq(r.cam.committed().position, pm.st.position + Vec3{0, 1.5, 0} + mN.forward * -4.0));
    CHECK(veq(r.cam.previous().position, playerAfterN + Vec3{0, 1.5, 0} + mN.forward * -4.0));

    // ---- frame N+2, look again: the move slot leads the committed rows by
    // this frame's yaw step.
    r.look.rot = 0.5f;
    r.cam.cameraHook();
    r.cam.preUpdate(dt);
    CHECK(deq(r.cam.angles.heading, 2 * h));
    CHECK(veq(r.cam.movementRows().row2, RotationFromAngles(0, 0, 2 * h).forward));
    CHECK(veq(r.cam.committed().forward, mN.forward));
}

// ===================================================================
// camera_free.xtbl seeding through the existing reader
// ===================================================================
void testCameraFreeSeeding() {
    const sr3xtbl::Document d = sr3xtbl::ParseDocument(R"(<root><Table><Camera>
      <panning_group>
        <fast_pan_horizontal_multiplier>1.5</fast_pan_horizontal_multiplier>
        <slow_pan_horizontal_multiplier>0.5</slow_pan_horizontal_multiplier>
        <fast_pan_input_threshold>0.8</fast_pan_input_threshold>
        <interior_h_dampening>0.6</interior_h_dampening>
        <interior_h_dampening_mouse>0.3</interior_h_dampening_mouse>
      </panning_group>
      <follow_aggression_group><default_swing_rate>0.9</default_swing_rate></follow_aggression_group>
      <miscellany_group><Exterior_Interior_Blend_Time>1.25</Exterior_Interior_Blend_Time></miscellany_group>
      <submodes>
        <submode><name>Exterior Close</name><lookat_offset><X>0</X><Y>1.5</Y><Z>0</Z></lookat_offset>
          <z_dist>4</z_dist><y_dist>3</y_dist><min_elevation>-30</min_elevation><max_elevation>60</max_elevation>
          <base_fov>55</base_fov><blend_time>0.5</blend_time><x_shift>0</x_shift></submode>
        <submode><name>exterior close</name><Aspect>widescreen</Aspect><lookat_offset><X>0</X><Y>9</Y><Z>0</Z></lookat_offset>
          <z_dist>4</z_dist><y_dist>3</y_dist><min_elevation>-30</min_elevation><max_elevation>60</max_elevation>
          <base_fov>70</base_fov><blend_time>0.5</blend_time><x_shift>0</x_shift></submode>
        <submode><name>sprint</name><lookat_offset><X>0</X><Y>1</Y><Z>0</Z></lookat_offset>
          <z_dist>4</z_dist><y_dist>0</y_dist><min_elevation>-30</min_elevation><max_elevation>60</max_elevation>
          <base_fov>55</base_fov><blend_time>0.5</blend_time></submode>
        <submode><name>no such mode</name></submode>
        <submode><name>zoom</name><lookat_offset><X>0.5</X><Y>1.6</Y><Z>0</Z></lookat_offset>
          <z_dist>0.0005</z_dist><y_dist>3</y_dist><min_elevation>-80</min_elevation><max_elevation>80</max_elevation>
          <default_elevation>10</default_elevation><base_fov>30</base_fov><blend_time>0.2</blend_time>
          <x_shift>0.3</x_shift><override_exit_blend_time>True</override_exit_blend_time></submode>
      </submodes>
    </Camera></Table></root>)");
    const auto cf = sr3tables_environment::ParseCameraFree(d);
    CHECK(cf.has_value());
    if (!cf) return;
    ScriptedLook look;
    CameraSystem cam(look);
    const CameraFreeSeedReport rep = SeedCameraFromCameraFree(cam, *cf, /*widescreen=*/false);
    CHECK(rep.submodesSeeded == 2 && rep.submodesIncomplete == 1 && rep.submodesUnknownName == 1 &&
          rep.submodesAspectFiltered == 1);
    CHECK(rep.scalarsSeeded == 7);
    CHECK(deq(cam.constants.fastPanH.get(), 1.5) && deq(cam.constants.slowPanH.get(), 0.5));
    CHECK(deq(cam.constants.fastPanInputThreshold.get(), 0.8f, 1e-7));
    CHECK(deq(cam.constants.pad.interior.h.get(), 0.6f, 1e-7) && deq(cam.constants.mouse.interior.h.get(), 0.3f, 1e-7));
    CHECK(deq(cam.constants.defaultSwingRate.get(), 0.9f, 1e-7));
    CHECK(deq(cam.constants.exteriorInteriorBlendTime.get(), 1.25));
    CHECK(!cam.constants.slowPanV.known());  // absent always-write: stays OPEN
    // Slot 0 (case-insensitive name), polar fields via 0x0056ad50, radians.
    const SubmodeRecord& r0 = cam.submodeRecord(0);
    CHECK(deq(r0.baseElevation, std::atan(0.75), 1e-7) && deq(r0.followDistance, 5, 1e-6));
    CHECK(deq(r0.minElevation, -30 * 0.017453f, 1e-6) && deq(r0.baseFov, 55) && !r0.hasDefaultElevation);
    CHECK(deq(r0.lookatOffset.y, 1.5));
    // "sprint" (31) lacked x_shift: its slot stays OPEN.
    CHECK(throwsOpen([&] { cam.submodeRecord(31); }, "31"));
    // "zoom" (12): |z_dist| <= 0.001 -> both polar fields 0; write-if-present flags.
    const SubmodeRecord& r12 = cam.submodeRecord(12);
    CHECK(r12.baseElevation == 0 && r12.followDistance == 0);
    CHECK(r12.hasDefaultElevation && deq(r12.defaultElevation, 10 * 0.017453f, 1e-6) && r12.overrideExitBlendTime);
    // Widescreen display: the "widescreen" row now passes and wins (last wins).
    CameraSystem cam2(look);
    const CameraFreeSeedReport rep2 = SeedCameraFromCameraFree(cam2, *cf, /*widescreen=*/true);
    CHECK(rep2.submodesAspectFiltered == 0 && rep2.submodesSeeded == 3);
    CHECK(deq(cam2.submodeRecord(0).baseFov, 70) && deq(cam2.submodeRecord(0).lookatOffset.y, 9));
}

// ===================================================================
// helpers: rotation convention, polar form
// ===================================================================
void testHelpers() {
    const CameraRotation m = RotationFromAngles(0.3, 0.2, 1.1);
    CHECK(deq(Dot(m.right, m.up), 0) && deq(Dot(m.right, m.forward), 0) && deq(Dot(m.up, m.forward), 0));
    CHECK(deq(Length(m.right), 1) && deq(Length(m.up), 1) && deq(Length(m.forward), 1));
    CHECK(veq(Cross(m.right, m.up), m.forward));
    // Positive pitch looks down, so pivot - d*forward is above the pivot.
    CHECK(RotationFromAngles(0.4, 0, 0).forward.y < 0);
    CHECK(deq(HeadingFromForward(RotationFromAngles(0, 0, 1.1).forward), 1.1));
    SubmodeRecord s;
    SetPolarFromAuthored(s, -4, 3);
    CHECK(deq(s.baseElevation, std::atan(3.0 / -4.0)) && deq(s.followDistance, 5));
    CHECK(IsZoomSubmode(55) && !IsZoomSubmode(20) && IsFineAimSubmode(4) && IsScopeAutoEntrySubmode(59));
}

}  // namespace

int main() {
    const auto run = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << name << ": unexpected exception: " << e.what() << "\n";
            ++g_failures;
        }
    };
    run("testModeStack", testModeStack);
    run("testInitResetSnap", testInitResetSnap);
    run("testLookAndAngles", testLookAndAngles);
    run("testPitchClamp", testPitchClamp);
    run("testPanRates", testPanRates);
    run("testBlendKinds", testBlendKinds);
    run("testBlendDurations", testBlendDurations);
    run("testSway", testSway);
    run("testPlacement", testPlacement);
    run("testFov", testFov);
    run("testCommitOrdering", testCommitOrdering);
    run("testSurfaceAndOpenGates", testSurfaceAndOpenGates);
    run("testFrameCadenceWithMovement", testFrameCadenceWithMovement);
    run("testCameraFreeSeeding", testCameraFreeSeeding);
    run("testHelpers", testHelpers);
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_player_camera_test: all checks passed\n";
    return 0;
}
