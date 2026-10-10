#pragma once

// sr3player/camera.h - player runtime unit U4: the camera mode machine and
// the gameplay follow camera (mode 0).
//
// Source: spec-player-runtime.md Sec4 (U4), SPEC TEAM - the ONLY source for
// every behaviour below. Addresses and offsets in comments are copied from
// that text so a reader can cross-check the section; nothing here was
// derived from the executable. Where this file needs a fact from another
// spec it cites it (the ease 0x00dad960: spec-anim-runtime.md Sec6; the angle
// wrap 0x00dadeb0: spec-tables-traffic-ai.md; the camera_free.xtbl reader:
// spec-tables-environment.md Sec12.2/Sec17.5).
//
// WHAT IS IMPLEMENTED
//   * The 18-row mode table with its five slot roles (init/reset/enter/pre/
//     update, Sec4.1) and the selection machinery of Sec4.2: move-to-top
//     push, by-value pop, deferred set-base, enter-only mode changes (no exit
//     callback exists), the getter, init 0x00565a40 and reset 0x00566830.
//     The transition-blend START (snapshot + flag bit 0x02, 0x00580700) and
//     cancel 0x00580420 are implemented; its APPLY is OPEN (see below).
//   * The generic pre-update 0x00565c60 and update 0x00566a40 (Sec4.3,
//     Sec4.11): step selection, previous <- committed, mode update, snap,
//     commit working -> committed with optional FOV smoothing (flag 0x08),
//     the camera-cut byte and the target snapshot.
//   * Mode 0, the gameplay follow camera: enter 0x005732b0 (+0x0056bc60),
//     the camera hook 0x005ab5b0 and look slot 0x005ab6a0 (Sec3.4, the
//     producer of the look input), the pre-update 0x005779b0 (Sec4.5) with
//     the yaw/pitch steps 0x00570560/0x00570650 and the pan-rate functions
//     0x0056f8b0/0x0056fdb0 (Sec4.6), the update 0x00577cb0 and the core
//     0x00575e30 (Sec4.7) for a HUMAN target with no vehicle: submode choice
//     and blend (Sec4.8: all four blend kinds, the 15-unit snap, the
//     duration rules, the pivot), sway, orientation, distance, placement,
//     collision (Sec4.9, through an injected query), the shoulder offset,
//     the FOV rule.
//
// WHAT IS NOT IMPLEMENTED (an OpenStateError when reached, never a guess)
//   * Every mode row other than 0. Rows 4, 5 and 8 are EMPTY in the real
//     table (all five slots null, Sec4.1, CONFIRMED) so selecting them is
//     faithful and does nothing; every other row's bodies are U8 territory
//     (Sec7 OPEN 24) and selecting one throws before the stack is touched.
//     Tests and future units can install a row (installRow).
//   * Frame cadence branches other than ordinary play (Sec4.3): the early
//     update during cutscene playback (0x00702a98) and the paused path with
//     HUD screens 0x35..0x39 (0x00702eee, OPEN 26). The caller owns the
//     world update and calls preUpdate()/update() at the ordinary-play
//     sites; this project has no cutscene playback or pause screens.
//   * Vehicle targets and every vehicle-only step of the core: the vehicle
//     chooser 0x00573110, the distance ramp and manual-look hold (step 4),
//     sub-states 1..5 and the vehicle presets (steps 5-6), vehicle-aim
//     heading limits (step 7), the helicopter pitch TARGET (step 8 - the 4%
//     decay toward 0 without a vehicle IS implemented), vehicle distance and
//     FOV scaling (step 11, the FOV paragraph), the proximity lock (step 13).
//   * Weapon/combat-only steps: zoom scale (Sec4.6 step 1), the scope-view
//     auto-entry (step 3), the zoom-submode scope placement (step 13), the
//     look-at assist (step 12), skydiving back-vector rotation (step 13).
//   * The animation-driven camera 0x00575790 (Sec4.5 step 2).
//   * The transition-blend apply 0x00580b20 (its easing 0x00dadb00 and the
//     elapsed-time advance are not given), the shake 0x0057c930's outputs,
//     DOF defaults, the zone word, the hide-humans sweep, the snap notify.
//   * Callees the spec names but did not read (Sec7 OPEN 27, 30, 31, 33, 34):
//     each is a std::function in FollowCameraHooks that throws OPEN while
//     unset. SeedFollowCameraOnFootCommonCase() installs CHOSEN no-op /
//     "absent" versions for the ones on the plain on-foot path, each named.
//
// SPEC CONFLICT FLAGGED (not resolved here, both readings documented):
// Sec4.3's "Consequence" paragraph says camera-relative movement reads the
// committed rows of the PREVIOUS frame. But Sec2.7 (CONFIRMED by full
// listing) orders state 2's tail as camera hook -> camera pre-update
// 0x00565c60 -> post-hook (the move slot), Sec4.5 step 1 says the
// pre-update WRITES the working rows 0x013c8790/0x013c87b0, and Sec3.5.1
// step 3 says the move slot reads exactly those working rows. So the move
// slot reads rows rebuilt THIS frame from this frame's angles (heading
// without sway; no look-at assist). This file follows the CONFIRMED
// listings: movementRows() returns the working rows, valid between
// preUpdate() and update(). Position: the camera does follow the current
// frame's player position (update runs after character movement), as
// Sec4.3 says.
//
// ROTATION CONVENTION (CHOSEN - no spec gives 0x004cd740's axis order or
// handedness). Y is up (gravity (0, -9.8, 0), spec-physics-runtime.md).
// R(pitch, roll, heading): forward = (sin h cos p, -sin p, cos h cos p);
// with no roll right = (cos h, 0, -sin h), up = forward x right; roll turns
// right toward up about forward. So heading 0 looks along +z, positive
// heading turns toward +x, positive pitch looks DOWN - chosen so that the
// polar base elevation atan(y_dist / z_dist) of a positive y_dist puts the
// camera ABOVE the pivot (desired = pivot - d * forward, Sec4.7 step 13).
// Rows: right x up = forward (the usual cross product with x right, y up,
// z forward). A target's heading uses the same convention
// (HeadingFromForward).

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "sr3luahost/open_state.h"
#include "sr3player/input.h"
#include "sr3player/movement.h"

namespace sr3player {

// --- constants (each from the spec text) --------------------------------------
inline constexpr int kNumCameraModes = 18;    // Sec4.1
inline constexpr int kCameraStackSlots = 8;   // 0x013c87cc..0x013c87e8 (Sec4.2)
inline constexpr int kCameraModeFollow = 0;
namespace camflag {  // 0x013c88a0 (Sec4.2)
inline constexpr uint32_t kTransitionBlend = 0x02;
inline constexpr uint32_t kSnapRequested = 0x04;
inline constexpr uint32_t kSmoothFov = 0x08;
}  // namespace camflag
inline constexpr double kCameraInitFov = 60.0;            // 0x01173de4 (Sec4.2 init)
inline constexpr double kCameraCollisionRadius = 0.12;    // 0x012e3cec (Sec4.9)
inline constexpr double kSubmodeSnapDistanceSq = 225.0;   // 15 units (Sec4.8)
inline constexpr double kCameraCutDistance = 75.0;        // 0x012e59f8 rule (Sec4.10)
inline constexpr double kPlacementEpsilon = 1e-5;         // Sec4.7 step 13, Sec4.9
inline constexpr double kSwayDecay = 0.9;                 // "decay by 10 % per update"
inline constexpr double kHeliPitchFollow = 0.04;          // "4 % ... per update"
inline constexpr double kFovSmoothRate = 5.0;             // clamp01(5 * real frame time)
inline constexpr double kSurfaceClampLift = 0.75;         // Sec4.7 (0x00577cb0)
inline constexpr double kSurfaceClampRange = 25.0;
inline constexpr double kTransitionBlendDefaultTime = 0.4;  // 0x011192f8 (Sec4.2)

// Submode indices = slots of the 61-name table (spec-tables-environment.md
// Sec12.2; sr3tables_environment::kCameraFreeSubmodeNames).
inline constexpr int kNumSubmodes = 61;
inline constexpr int kNoSubmode = 62;  // "out of range, no submode" (Sec4.8)
namespace submode {
inline constexpr int kExteriorClose = 0, kInteriorClose = 1, kInteriorSprint = 2;
inline constexpr int kHelicopterDriver = 6, kVehicleAim = 7, kVehicleFineAim = 8, kVehicleZoom = 9;
inline constexpr int kTankDriver = 11, kZoom = 12, kZoomCrouch = 13, kSpectator = 15, kRagdoll = 17;
inline constexpr int kFreefall = 25, kParachute = 26, kHumanShield = 29, kHumanShieldFineAim = 30;
inline constexpr int kShootingQte = 35, kRcTank = 42, kSkydivingFirst = 44, kSkydivingLast = 49;
}  // namespace submode
bool IsZoomSubmode(int s);              // 0x0056a690: 9, 12, 13, 55 (Sec4.5 step 1)
bool IsFineAimSubmode(int s);           // 0x0056a630 list (Sec4.6 step 2)
bool IsScopeAutoEntrySubmode(int s);    // Sec4.7 step 3 list
bool IsSkydivingSubmode(int s);         // 44..49

// --- math helpers ----------------------------------------------------------------
Vec3 Cross(Vec3 a, Vec3 b);
// 0x00dadeb0 with the -pi lower bound: wrap into [-pi, pi)
// (spec-tables-traffic-ai.md; spec-anim-runtime.md Sec4.2 reads the range as
// 0..2pi for its own call - the camera call's bound is not given, CHOSEN
// [-pi, pi). The rows built from the angle are identical either way.)
double WrapAngle(double a);
// 0x00dad960(t, 1.0, 0.5): 2t^2 for t <= 1/2, 1 - 2(1-t)^2 above
// (spec-anim-runtime.md Sec6, CONFIRMED by evaluating the listing; the same
// arguments as Sec4.8 kind 3).
double EaseInOut(double t);
// The camera rows R(pitch, roll, heading) - see ROTATION CONVENTION above.
struct CameraRotation {
    Vec3 right, up, forward;
};
CameraRotation RotationFromAngles(double pitch, double roll, double heading);  // 0x004cd740 (CHOSEN axes)
// Inverse of the heading part of the convention: the heading whose forward
// is `forward` flattened (for feeding a target's heading).
double HeadingFromForward(Vec3 forward);

// --- the three output sets (Sec4.10) ----------------------------------------------
struct CameraTransform {
    Vec3 position;
    Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};  // rows 1/2/3
    double fov = 0;  // degrees
};

// --- submode records (Sec4.4, Sec4.8; spec-tables-environment.md Sec12.2) --------
struct SubmodeRecord {
    Vec3 lookatOffset;               // +0x00
    double minElevation = 0;         // +0x10 radians
    double maxElevation = 0;         // +0x14 radians
    bool hasDefaultElevation = false;  // +0x18
    double defaultElevation = 0;     // +0x1c radians
    double baseFov = 0;              // +0x20 degrees
    double baseElevation = 0;        // +0x24 = atan(y_dist / z_dist)
    double followDistance = 0;       // +0x28 = sqrt(z_dist^2 + y_dist^2)
    double blendTime = 0;            // +0x2c seconds
    bool overrideExitBlendTime = false;  // +0x30
    double xShift = 0;               // +0x34
};
// 0x0056ad50(z_dist, y_dist) (Sec4.4, CONFIRMED): |z| <= 0.001 -> both 0.
// CHOSEN: "within 0.001" read as <=.
void SetPolarFromAuthored(SubmodeRecord& r, double zDist, double yDist);
// 0x0056c5b0 (Sec4.8): lerp of look-at offset, x_shift (to's sign flipped
// when flipXShift), min/max elevation, base FOV, base elevation, follow
// distance; default elevation + flag taken from `to` outright. CHOSEN: the
// fields the spec does not list (blend time, override flag) are taken from
// `to` as well.
SubmodeRecord LerpSubmode(const SubmodeRecord& from, const SubmodeRecord& to, double t, bool flipXShift);

enum class BlendKind : int { Snap = 0, Hold = 1, Linear = 2, Eased = 3 };  // table 0x00574568

// The submode blend object B 0x012e4030 (Sec4.4).
struct SubmodeBlend {
    uint32_t flags = 0;                         // +0x00 per-submode flag bits
    int current = kNoSubmode;                   // +0x10 0x012e4040
    BlendKind kind = BlendKind::Snap;           // +0x14
    double duration = 0;                        // +0x18
    double progress = 1;                        // +0x1c 0x012e404c (1 = done)
    std::optional<SubmodeRecord> from;          // +0x50
    std::optional<SubmodeRecord> previous;      // +0x90 (the previous step's blended record)
    std::optional<SubmodeRecord> blended;       // +0xd0 0x012e4100
    Vec3 pivot;                                 // +0x110 0x012e4140
    int fromSubmode = kNoSubmode;               // +0x130
    bool holdTimerRunning = false;              // +0x140 (0x00573890 is OPEN; never armed here)
    bool hasLastTarget = false;                 // +0x158/+0x15c
    uint64_t lastTarget = 0;
    bool fineAimClassChanged = false;           // 0x012e4169
    bool defaultElevationRequest = false;       // 0x012e4168 ("once after a request"; writer not traced)
};

// --- the follow camera's angle block 0x013c8940 (Sec4.4) ------------------------------
// CHOSEN construction: zero (the static image values are not given; the
// reset 0x00566830 runs at start-up, Sec4.2, and its snap rewrites what the
// first frame reads).
struct FollowAngles {
    int32_t subState = 0;          // +0x00 0..5
    double heading = 0;            // +0x04
    double pitch = 0;              // +0x08 = +0x20 + +0x24 + +0x28 + +0x2c + base elevation
    double rollBase = 0;           // +0x0c
    double roll = 0;               // +0x10 = base + sway
    double swayRoll = 0;           // +0x14 (added to roll)
    double swayPhase = 0;          // +0x18
    double pitchTerm20 = 0;        // +0x20
    double userPitch = 0;          // +0x24 look input
    double heliPitch = 0;          // +0x28 vehicle/helicopter pitch
    double pitchTerm2c = 0;        // +0x2c zeroed on a submode change
    double scriptPitchRate = 0;    // +0x30 (0x0056b190), zeroed by every pre-update
    double scriptYawRate = 0;      // +0x34
    double term38 = 0, term3c = 0;  // 0x013c8978/0x013c897c ("reset" on a submode change)
    double yawGain = 1.0;          // +0x40 (1.0, set by the loader)
    double pitchGain = 1.0;        // +0x44
    double padAccelYaw = 1.0;      // +0x48 (1.0, set by the loader; passed to 0x00dade20)
    double padAccelPitch = 1.0;    // +0x4c
    double lookYaw = 0;            // +0x50 0x013c8990
    double lookPitch = 0;          // +0x54 0x013c8994
    double yawRate = 0;            // +0x58
    double pitchRate = 0;          // +0x5c
    double swingRequest = 0;       // +0x60
    double swingRequestLast = 0;   // +0x64
    double swingRate = 0;          // +0x68
    std::optional<double> baseFov;  // +0x104 0x013c8a44 (unset until enter/update writes it)
};

// --- panning_group and the other camera_free.xtbl scalars U4 reads ----------------
struct DampeningPair {
    OpenValue<double> h, v;
};
struct DampeningSet {  // one per device family (the "_mouse" copies, Sec4.6 step 2)
    DampeningPair fineAim{{"fine_aim horiz dampening", "spec-player-runtime.md Sec4.6"}, {"fine_aim vert dampening", "spec-player-runtime.md Sec4.6"}};
    DampeningPair heavyWeapon{{"heavy_weapon horiz dampening", "spec-player-runtime.md Sec4.6"}, {"heavy_weapon vert dampening", "spec-player-runtime.md Sec4.6"}};
    DampeningPair humanShield{{"human_shield horiz dampening", "spec-player-runtime.md Sec4.6"}, {"human_shield vert dampening", "spec-player-runtime.md Sec4.6"}};
    DampeningPair interior{{"interior_h_dampening", "spec-player-runtime.md Sec4.6"}, {"interior_v_dampening", "spec-player-runtime.md Sec4.6"}};
    DampeningPair skydive{{"skydive_h_dampening", "spec-player-runtime.md Sec4.6"}, {"skydive_v_dampening", "spec-player-runtime.md Sec4.6"}};
    OpenValue<double> freefallH{"freefall_h_dampening", "spec-player-runtime.md Sec4.6"};
    OpenValue<double> parachuteH{"parachute_h_dampening", "spec-player-runtime.md Sec4.6"};
    OpenValue<double> helicopterH{"helicopter_h_dampening", "spec-player-runtime.md Sec4.6"};
};
struct CameraFreeConstants {
    static constexpr const char* kSec = "spec-tables-environment.md Sec12.2 / spec-player-runtime.md Sec4.6";
    OpenValue<double> fastPanH{"fast_pan_horizontal_multiplier 0x012e3e58", kSec};
    OpenValue<double> fastPanV{"fast_pan_vertical_multiplier", kSec};
    OpenValue<double> slowPanH{"slow_pan_horizontal_multiplier", kSec};
    OpenValue<double> slowPanV{"slow_pan_vertical_multiplier 0x012e3e64", kSec};
    OpenValue<double> pegAccelHMin{"peg_accel_pan_horizontal_min", kSec};
    OpenValue<double> pegAccelHMax{"peg_accel_pan_horizontal_max", kSec};
    OpenValue<double> pegAccelVMin{"peg_accel_pan_vertical_min", kSec};
    OpenValue<double> pegAccelVMax{"peg_accel_pan_vertical_max", kSec};
    OpenValue<double> fastPanInputThreshold{"fast_pan_input_threshold 0x012e3e80", kSec};
    OpenValue<double> hpanThreshold{"hpan_threshold", kSec};
    OpenValue<double> vpanThreshold{"vpan_threshold", kSec};
    DampeningSet pad, mouse;
    OpenValue<double> defaultSwingRate{"follow_aggression_group default_swing_rate 0x012e3f50", kSec};
    OpenValue<double> exteriorInteriorBlendTime{"Exterior_Interior_Blend_Time 0x012e3f38", kSec};
    OpenValue<double> ragdollBlendTime{"Ragdoll_Blend_Time 0x012e3f3c", kSec};
    // Not a named camera_free field (Sec4.8: "the value at 0x012e3f40").
    OpenValue<double> ragdollEntryBlendTime{"blend time 0x012e3f40 (unnamed)", "spec-player-runtime.md Sec4.8"};
};

// --- the camera target (Sec4.2 "Camera target", Sec4.8 pivot) -------------------------
// Supplied by the caller every frame (the handle pair 0x013c8848/0x013c884c
// resolved through 0x00458230 - no object system here).
struct CameraTarget {
    bool valid = false;            // the handle resolves
    uint64_t id = 0;               // handle identity ("target changed", Sec4.8)
    bool isVehicle = false;        // descriptor +6 bit 0x80 (vehicles are OPEN here)
    bool isHuman = true;           // descriptor +0xa bit 1 / +0x33 bit 8
    Vec3 referencePoint;           // 0x0056e530 (the pivot's base point)
    double heading = 0;            // the target's own heading (same convention as the camera)
    Orientation orientation{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // rows (pivot flag 0x10; reset snapshot)
    OpenValue<double> sizeFactor{"human size factor +0xd24 -> +0x18", "spec-player-runtime.md Sec4.8 (pivot)"};
    OpenValue<bool> noCollision{"no-collision flag +0xd24 -> +0x10 bit 0x1000", "spec-player-runtime.md Sec4.0 item 8 / Sec4.7 step 13"};
};

// --- the collision query (Sec4.9) -------------------------------------------------
// The swept-sphere world query 0x0075c880 with the filter 0x00566070 (OPEN
// 33). This project has no collision world, so the query is injected.
struct CameraSweepHit {
    Vec3 position;  // the hit's sweep position (+0x10)
    Vec3 normal;    // +0x20
};
class CameraCollision {
public:
    virtual ~CameraCollision() = default;
    // First blocking contact of a sphere of `radius` swept from `from` to `to`.
    virtual std::optional<CameraSweepHit> sweepSphere(Vec3 from, Vec3 to, double radius) = 0;
};
// CHOSEN opt-in for a world with nothing that blocks the camera.
class NoHitCameraCollision : public CameraCollision {
public:
    std::optional<CameraSweepHit> sweepSphere(Vec3, Vec3, double) override { return std::nullopt; }
};

// --- look input seam (Sec3.4) ------------------------------------------------------
class LookInputSource {
public:
    virtual ~LookInputSource() = default;
    virtual float rotate() = 0;              // 0x005bb530(CAA_CAMERA_ROTATE)
    virtual float elevate() = 0;             // 0x005bb530(CAA_CAMERA_ELEVATE)
    virtual bool cameraDrivenByKbm() = 0;    // 0x005bc5e0 (+0x9f6, Sec1.5)
    virtual bool invertHorizontal() = 0;     // 0x005be940
    virtual bool invertVertical() = 0;       // 0x005be920
};
// The real adapter onto sr3player::InputSystem (U0).
class InputSystemLookSource : public LookInputSource {
public:
    explicit InputSystemLookSource(InputSystem& in) : in_(in) {}
    // Bytes 0x01411b15 / 0x01411b14 (Sec1.12.2, Sec7 OPEN 2): user options
    // whose values no spec gives.
    OpenValue<bool> mouseInvertH{"look option byte 0x01411b15", "spec-player-runtime.md Sec1.12.2 / Sec3.4"};
    OpenValue<bool> mouseInvertV{"look option byte 0x01411b14", "spec-player-runtime.md Sec1.12.2 / Sec3.4"};
    float rotate() override { return in_.axis(kCaaCameraRotate); }
    float elevate() override { return in_.axis(kCaaCameraElevate); }
    bool cameraDrivenByKbm() override { return in_.cameraDrivenByKbm(); }
    bool invertHorizontal() override;  // mouse: 0x01411b15, pad: 0x01411b00 bit 1
    bool invertVertical() override;    // mouse: 0x01411b14, pad: 0x01411b00 bit 2

private:
    InputSystem& in_;
};

// --- predicates this project has no subsystem for --------------------------------------
// OPEN until set; read lazily in the spec's order. SeedFollowCameraOnFootCommonCase
// is the CHOSEN explicit opt-in for the plain on-foot player.
struct FollowCameraWorld {
    static constexpr const char* kSec = "spec-player-runtime.md Sec4";
    // Look slot (Sec3.4) - +0x1d99 bit 0x08, bindable like MovementWorld's.
    OpenValue<bool> controlsDisabled{"character byte +0x1d99 bit 0x08 (controls disabled)", "spec-player-runtime.md Sec3.4 step 1"};
    const OpenValue<bool>* controlsDisabledSource = nullptr;
    void bindControlsDisabled(const OpenValue<bool>& v) { controlsDisabledSource = &v; }
    bool readControlsDisabled() const { return (controlsDisabledSource ? *controlsDisabledSource : controlsDisabled).get(); }
    OpenValue<bool> pivotModeA20{"+0xa20 dormant pivot mode", "spec-player-runtime.md Sec3.4 step 1"};
    OpenValue<bool> drunkEffectInactive{"drunk camera modifier 0x00975ce0 inactive (OPEN 17)", "spec-player-runtime.md Sec3.4 step 3"};
    // Camera hook 0x005ab5b0 (Sec4.6).
    OpenValue<bool> sprintPadDescriptorByte5{"descriptor byte +5 of CBA_OFC_SPRINT's pad button", "spec-player-runtime.md Sec4.6"};
    OpenValue<bool> swingPredicate6c4990{"0x006c4990(player)", "spec-player-runtime.md Sec4.6"};
    // Generic pre-update 0x00565c60 (Sec4.3).
    OpenValue<bool> targetPhysicsDriven{"0x00853b30(target, 7) / ragdolled human with +0xcd2 != 3", "spec-player-runtime.md Sec4.3"};
    OpenValue<double> physicsQuantisedStep{"0x00756790() physics-quantised step", "spec-player-runtime.md Sec4.3"};
    // Follow pre-update (Sec4.5 step 2).
    OpenValue<bool> animationDrivenCameraTest{"0x0056c510 (animation-driven camera episode)", "spec-player-runtime.md Sec4.5 step 2"};
    // Follow update 0x00577cb0 base-FOV gate.
    OpenValue<bool> playerScopeView{"local player +0x1d95 bit 0x10 (scope view)", "spec-player-runtime.md Sec4.7"};
    OpenValue<bool> playerFreeFall{"0x00994f60(player)", "spec-player-runtime.md Sec4.7"};
    OpenValue<bool> fn614d00Is5{"0x00614d00() = 5", "spec-player-runtime.md Sec4.7"};
    OpenValue<bool> fn614d00Is2{"0x00614d00() = 2", "spec-player-runtime.md Sec4.7 step 13"};
    OpenValue<bool> flag13c9363{"0x013c9363", "spec-player-runtime.md Sec4.7"};
    // Core sub-state selection without a vehicle (step 5).
    OpenValue<int32_t> pendingRequest8a48{"sub-state request 0x013c8a48", "spec-player-runtime.md Sec4.7 step 5"};
    OpenValue<bool> playerVehicleSubState5{"0x00ac13d0 on P's vehicle handle 0x009b9130", "spec-player-runtime.md Sec4.7 step 5"};
    // Submode choice and blend (Sec4.8).
    OpenValue<int32_t> humanSubmode{"human submode chooser 0x005745c0 (OPEN 30)", "spec-player-runtime.md Sec4.8"};
    OpenValue<bool> playerRagdolling{"0x009a1dc0(P)", "spec-player-runtime.md Sec4.8"};
    OpenValue<bool> player9a1de0{"0x009a1de0(P)", "spec-player-runtime.md Sec4.8"};
    OpenValue<bool> instantBlend13c9362{"0x013c9362", "spec-player-runtime.md Sec4.8"};
    OpenValue<bool> heavyWeapon{"heavy weapon 0x009ba860(player)", "spec-player-runtime.md Sec4.6 step 2"};
    OpenValue<bool> xShiftFlip{"B +0x20 x-shift sign byte (writer not traced)", "spec-player-runtime.md Sec4.4 / Sec4.8"};
    std::array<std::optional<uint32_t>, kNumSubmodes> submodeFlags{};  // 0x0056e370's per-submode switch (not tabulated)
    std::function<BlendKind(int from, int to)> blendKindFor;           // how +0x14 is chosen is not given
    // Transition blend (Sec4.2).
    OpenValue<bool> transitionSnapshotSuppressed{"0x013cbb34 = 1", "spec-player-runtime.md Sec4.2"};
    // Unread scalars (Sec7 OPEN 27).
    OpenValue<double> yawFactor{"player-side yaw factor 0x00975770 (OPEN 27)", "spec-player-runtime.md Sec4.6"};
    OpenValue<double> pitchFactor{"player-side pitch factor 0x00975810 (OPEN 27)", "spec-player-runtime.md Sec4.6"};
    OpenValue<double> fovFactor{"FOV factor 0x00975bd0 (OPEN 27)", "spec-player-runtime.md Sec4.7"};
};

// Callees named by the spec whose bodies were not read. Unset = OPEN (the
// call throws naming the address). SeedFollowCameraOnFootCommonCase installs
// the ones on the plain on-foot path as CHOSEN no-ops / "absent" answers.
struct FollowCameraHooks {
    std::function<void()> row0Reset;                                   // 0x005749e0 (row 0 reset slot)
    std::function<void(FollowAngles&, double step)> pitchReturn;       // 0x0056bfe0 (OPEN 34)
    std::function<void(FollowAngles&, Vec3 committedPos, double baseElevation)> hillTracking;  // 0x0057ad60 (OPEN 34)
    std::function<void()> coreHead5723e0;                              // 0x005723e0 (core step 1)
    std::function<void()> coreHead920680;                              // 0x00920680(T) (core step 1)
    std::function<void(double step)> subState0Handler;                 // 0x005710b0 follow swing (OPEN 30)
    std::function<void()> after0056ef10;                               // 0x0056ef10 (OPEN 34)
    std::function<void()> cameraShake;                                 // 0x0057c930 (OPEN 34)
    // 0x0079a420 on the working position: two heights, or nullopt = no surface.
    std::function<std::optional<std::pair<double, double>>(Vec3)> surfaceHeights;  // (OPEN 34)
    std::function<bool()> blendRequestUnchanged;                       // 0x0056c780
    std::function<void()> onBlendRequestUnchanged;                     // 0x00574070
    // Pad pan acceleration beyond fast_pan_input_threshold (Sec4.6 step 3):
    // 0x00dade20(param, 0.1, 10, peg_min, peg_max) and the easing 0x0056bde0
    // of the stored rate toward the clamped candidate. Neither body is given.
    std::function<double(double param, double lo, double hi, double outMin, double outMax)> panAccelFactor;
    std::function<double(double stored, double candidate, double step)> panRateEase;
    // Roll smoothing with a per-step limit 0x00dadf70 (Sec4.7 step 9). Unset:
    // identity when the target equals the current roll (any per-step-limited
    // approach is the identity at its target), OPEN otherwise.
    std::function<double(double current, double target, double step)> rollSmoother;
};

// Per-frame time inputs (three distinct engine globals, Sec4.3/Sec4.7/Sec4.11).
struct CameraFrameTimes {
    double frameTime = 0;     // 0x0132a0ac - the pre-update step source
    double realTimeB0 = 0;    // 0x0132a0b0 - sway phase advance
    double realTimeB4 = 0;    // 0x0132a0b4 - FOV smoothing
};

// --- the mode table row (Sec4.1) ---------------------------------------------------
struct CameraModeRow {
    const char* identity = "";
    bool implemented = false;   // false: the real row has bodies this project does not model
    bool hasInit = false, hasReset = false, hasEnter = false, hasPre = false, hasUpdate = false;
    std::function<void()> init, reset, enter;
    std::function<void(double)> pre, update;
    bool empty() const { return !hasInit && !hasReset && !hasEnter && !hasPre && !hasUpdate; }
    bool selectable() const { return implemented || empty(); }
};

// --- the unit ----------------------------------------------------------------------
class CameraSystem {
public:
    explicit CameraSystem(LookInputSource& look);
    CameraSystem(const CameraSystem&) = delete;
    CameraSystem& operator=(const CameraSystem&) = delete;

    FollowCameraWorld world;
    FollowCameraHooks hooks;
    CameraFreeConstants constants;
    // The 61 submode slots 0x013c93b0 + 0x40*i. nullopt = OPEN (no row seeded).
    std::array<std::optional<SubmodeRecord>, kNumSubmodes> submodes{};
    const SubmodeRecord& submodeRecord(int index) const;  // throws OPEN when unseeded
    CameraCollision* collision = nullptr;  // OPEN when null and a sweep is needed
    CameraTarget target;
    // "player gave input" 0x00950b70(player, 0.3) from the look slot (Sec3.4 step 4).
    std::function<void(double blend)> onPlayerGaveInput;

    // --- Sec4.1/Sec4.2: the table and selection ---
    const CameraModeRow& row(int mode) const;
    void installRow(int mode, CameraModeRow r);  // tests / future units (replaces a row)
    void init();                                  // 0x00565a40
    void reset();                                 // 0x00566830 (target must be set to the local player)
    void push(int mode);                          // 0x00564880
    void pop(int mode);                           // 0x00564910
    void setBase(int mode, bool blend = true, double time = kTransitionBlendDefaultTime);  // 0x005649d0
    int currentMode() const { return current_; }  // 0x005649c0
    int baseMode() const { return base_; }
    int stackCount() const { return count_; }
    int stackAt(int i) const { return stack_.at(static_cast<size_t>(i)); }
    uint32_t flags() const { return flags_; }
    void requestSnap() { flags_ |= camflag::kSnapRequested; }     // 0x00567280
    void setSmoothFov(bool on);  // bit 0x08: set by 0x006bbff0, cleared by 0x00569640/0x006bc8e0/...
    void startTransitionBlend(double time, uint32_t parts);       // 0x00580700 (type 0)
    void cancelTransitionBlend();                                 // 0x00580420
    // 0x00564c20(position): writes working and committed directly, records the jump.
    void setPositionDirect(Vec3 p);

    // --- Sec4.3 cadence (ordinary play) ---
    // 1. cameraHook()   - state 2's tail: camera hook 0x005ab5b0 -> look slot 0x005ab6a0
    // 2. preUpdate()    - state 2's tail: 0x00565c60 (angles -> working rows)
    // 3. the move slot  - reads movementRows() (the working rows, Sec3.5.1 step 3)
    // 4. physics and character movement
    // 5. update()       - 0x00702d18, after character movement
    void cameraHook();
    void preUpdate(double frameTime);
    void update(const CameraFrameTimes& t);
    CameraRows movementRows() const { return CameraRows{working_.right, working_.forward}; }

    // --- outputs (Sec4.10). Consumers read COMMITTED. ---
    const CameraTransform& committed() const { return committed_; }
    const CameraTransform& previous() const { return previous_; }
    const CameraTransform& working() const { return working_; }  // private to the camera in the engine; tests only
    bool cameraCutByte() const { return cutByte_; }        // 0x012e59f8
    bool cameraCutFlag() const { return cutFlag_; }        // 0x013c872c (consumer 0x009ed510 clears it)
    void clearCameraCutFlag() { cutFlag_ = false; }
    Vec3 targetSnapshotPosition() const { return targetSnapPos_; }  // 0x013c8850
    const Orientation& targetSnapshotRows() const { return targetSnapRows_; }  // 0x013c8860..
    std::optional<double> lastStep() const { return step88b4_; }  // 0x013c88b4

    // --- mode 0 state, public for inspection and tests ---
    FollowAngles angles;
    SubmodeBlend blend;
    Vec3 lateralOffset;          // 0x013c8a60
    double swayHeadingAmp = 0;   // 0x013c936c (writers not traced, OPEN 31)
    double swayRollAmp = 0;      // 0x013c9370
    bool skipCoreOnce = false;   // 0x013c9388

    // --- mode 0 pieces, individually callable ---
    void followEnter();                           // 0x005732b0
    void followPreUpdate(double step);            // 0x005779b0
    void followUpdate(double step);               // 0x00577cb0
    void followCore(double step);                 // 0x00575e30
    void followSnap();                            // 0x00566620 (mode 0 part)
    void lookSlot();                              // 0x005ab6a0
    void yawStep(double step, bool fine);         // 0x00570560
    void pitchStep(double step, bool fine);       // 0x00570650
    double panRateYaw(double step, bool fine);    // 0x0056f8b0
    double panRatePitch(double step, bool fine);  // 0x0056fdb0
    void blendUpdate(double step);                // 0x005750f0
    void blendStep(double step);                  // 0x00574350
    void blendSnapTo(int submodeIndex);           // 0x0056c860 copy semantics
    void recomputePivot();                        // 0x00573c10
    int chooseSubmode() const;                    // 0x00574fe0
    double recomputePitch();                      // the +0x08 sum
    // 0x00565d70: collision-resolved camera position.
    Vec3 resolveCollision(Vec3 from, Vec3 to);

private:
    void requireSelectable(int mode) const;
    void callEnter(int mode);
    const SubmodeRecord& blendedRecord() const;
    double dampening(bool horizontal, bool mouse) const;
    void placeBehindTarget();  // 0x005733b0 (CHOSEN reading, see .cpp)

    LookInputSource& look_;
    std::array<CameraModeRow, kNumCameraModes> rows_;
    int current_ = 0;                 // 0x013c87c8 (static 0)
    std::array<int, kCameraStackSlots> stack_{};  // 0x013c87cc (static 0)
    int count_ = 0;                   // 0x013c87ec (static 0)
    int base_ = 0;                    // 0x013c87f0 (static 0)
    uint32_t flags_ = 0;              // 0x013c88a0 (static 0)
    std::optional<double> step88b4_;  // 0x013c88b4 (stored by the most recent pre-update)
    CameraFrameTimes frameTimes_;     // the current update's time globals (sway reads 0x0132a0b0)
    CameraTransform working_, committed_, previous_;
    bool cutByte_ = false, cutFlag_ = false;
    double directJump8738_ = 0;
    Vec3 targetSnapPos_;
    Orientation targetSnapRows_{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    // Transition blend snapshot (0x013cbb30..0x013cbb6c).
    int transitionType_ = -1;
    CameraTransform transitionSnapshot_;
    double transitionElapsed_ = 0, transitionDuration_ = 0;
    uint32_t transitionParts_ = 0;
};

// CHOSEN common-case seeding for the plain on-foot player this project can
// run: human target, no vehicle, no weapon zoom/heavy weapon/ragdoll/scope/
// free fall, no animation-driven camera, no pending sub-state request, no
// pivot mode, drunk modifier inactive, not physics-driven; the human chooser
// fixed to submode 0 ("exterior close") with flag bits 0; the OPEN-27 look/FOV
// factors 1.0 (no aim slow-down, default sensitivity); the transition
// snapshot not suppressed; and these hooks: row-0 reset, pitch return, hill
// tracking, the two core-head calls, the sub-state-0 follow swing, 0x0056ef10
// and the shake as no-ops; no water surface; "blend request changed" (so the
// blend machine always advances, see .cpp). NONE of these is a recovered
// engine value - each is "this project has no X subsystem" or a documented
// scope cut. Leaves OPEN: controls disabled (bind it), the look-invert
// options, the sprint pad descriptor byte, every camera_free constant and
// submode record (seed from the real file: camera_free_seed.h), the target's
// size factor and no-collision flag, the collision query, the pad
// pan-acceleration pair, the roll smoother, blend kinds.
void SeedFollowCameraOnFootCommonCase(CameraSystem& cam);

}  // namespace sr3player
