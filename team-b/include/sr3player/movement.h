#pragma once

// sr3player/movement.h - player runtime units U2 (normal-condition movement
// and the intent fields) and U3 (the locomotion bridge).
//
// Source: spec-player-runtime.md Sec3 (U2) and Sec5 (U3), SPEC TEAM
// 2026-10-03 - the ONLY source for every behaviour below. Addresses and
// offsets in comments are copied from that text so a reader can cross-check
// the section; nothing here was derived from the executable.
//
// WHAT IS IMPLEMENTED (per player, one PlayerMovement object):
//   U2  * the walk routine 0x005ab860 and the sprint routine 0x005ab930
//         (Sec3.6.2): walk bit/latch, sprint want/latch, the 300 ms keyboard
//         idle grace, the standstill burst, the stamina coupling (through an
//         injectable SprintStamina - the stamina machine itself is external);
//       * crouch hold/toggle (Sec3.6.3);
//       * the axis read and conditioning 0x005ad130 (Sec3.5.2): hard block,
//         axis modifiers (as OPEN gates), cover dominant-axis rule, release
//         detector, publication of A/B;
//       * the move slot 0x005b3e80 (Sec3.5.1): camera-flattened basis, the
//         stale-intent gate, the target point 0x005b31f0 (Sec3.5.3), the
//         speed scalar s, the writer, the locomotion-state update;
//       * the intent writer 0x005aee30 (Sec3.5.4) steps 1-11, 14, 15, 17,
//         18 and 19 - stance maximum speed table (first match wins), sprint
//         ramp, acceleration modifiers, the approach rule, the M clamp, the
//         facing +0xa50. Closed form: Sec3.5.5.
//   U3  * the locomotion-state update 0x00985df0 (Sec5.5): +0xccc, the
//         sprint ramp timer +0x2058, the +0xa64 rescale of +0xa60;
//       * the velocity composer for the player (Sec5.4.3, closed form
//         Sec5.4.4): +0x1cec = k.r + R.E/dt, or k|r|.facing + R.E/dt;
//       * a DIRECT INTEGRATOR in place of the Havok capsule - licensed by
//         Sec5.11 item 5 ("A port that moves its own capsule may replace
//         Sec5.6.4-Sec5.6.5 wholesale, provided it consumes the same
//         per-frame velocity and publishes the same fields"): the gather's
//         player air control (Sec5.6.3 (e)), position += velocity*dt, the
//         write-back's +0xd04/+0xd08/+0xd0c/+0xd1c (Sec5.6.6) and the
//         position sync threshold of Sec5.7;
//       * frame ordering (Sec5.1, Sec5.9): controls -> compose -> integrate
//         -> publish, each exactly once per frame (runFrame()).
//
// WHAT IS NOT IMPLEMENTED (each one is either an OPEN gate that throws
// sr3luahost::OpenStateError when reached, or a documented no-op):
//   * Animation requests (Sec3.5.4 step 13 and the sprint-style bits of
//     step 16): Sec3.8 item 4 licenses treating them as no-ops for a port
//     without the original animation network. Step 16's +0x1d97 bits
//     0x10/0x20 are therefore NOT maintained (they only feed animation).
//   * Turn angle +0xa68 (Sec3.5.4 step 20, HIGH only) - not computed; the
//     writer's step 3 zero is still applied. The step-19 fallbacks that
//     need it (q = old +0xa50 rotated by old +0xa68) throw OPEN.
//   * The look slot 0x005ab6a0 (Sec3.4) - implemented with the camera, U4:
//     CameraSystem::cameraHook()/lookSlot() in sr3player/camera.h - and
//     the classify 0x005ac620 (Sec3.2)
//     - the +0x2078 permission mask is an INPUT here - the dormant pivot
//     branch 0x005ad510 (Sec3.5.6, dead in the shipped build), jump/climb,
//     aim, melee (Sec3.6.4-Sec3.6.6).
//   * NPC shaping 0x009918b0 (Sec5.10 item 1): the player is routed through
//     it only when +0xccd is 2 or 3 (Sec5.4.3 step 5) - that throws OPEN
//     here (player-only pass; meaning of those values is spec OPEN 40).
//   * Stride matching / playback rate +0x1a90 (Sec5.4.3 step 7), the lean
//     (step 6), synced pairs (Sec5.4.6), the controller motion's support
//     query/slope removal (Sec5.6.5), landing, ground probe, knock-downs
//     (Sec5.6.6) and the contact replay (Sec5.8): this project has no
//     animation root-motion evaluation, collision or contact system.
//
// CONSTRUCTION VALUES. +0x2048 and +0x203c are zeroed by the player
// constructor (Sec3.5.7, CONFIRMED); +0xa64 = 1.0 at construction
// (Sec5.5 / OPEN 44, CONFIRMED). Every OTHER per-player field this unit
// owns (stance bytes +0x1d94..+0x1d97, +0xa50, +0xa60, +0xccc, the timers)
// starts zero/unset - CHOSEN: no spec gives their construction value; this
// follows the same pool-zero-fill reading spec-player-runtime.md Sec2.10
// gives (HIGH) for the neighbouring byte +0x1d99. Everything that belongs to
// a subsystem this project does not model is an OpenValue in MovementWorld.

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sr3luahost/open_state.h"
#include "sr3player/input.h"

namespace sr3player {

using sr3luahost::OpenStateError;
using sr3luahost::OpenValue;

// --- small vector type ------------------------------------------------------
// CHOSEN: double components throughout. The engine stores floats; the spec
// notes several products are taken in double precision (Sec3.5.1 step 7,
// Sec3.5.3 step 1). Double everywhere is the uniform reading.
struct Vec3 {
    double x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
// The 1e-7 zero test 0x00d9f9d0 (Sec3.5.3 step 2, Sec3.5.4 steps 2/7).
// CHOSEN: read per component (|c| <= 1e-7 for all three); the spec gives the
// tolerance, not whether it is a component or a length test.
bool IsZero7(Vec3 v);
// Normalise, or the zero vector when IsZero7 (Sec3.5.3 step 2's rule; also
// used where the spec says "normalise" of a vector that may be zero, CHOSEN
// there).
Vec3 NormaliseOrZero(Vec3 v);

// The human's orientation rows (Sec5.4.3 notation): +0x4c right, +0x58 up,
// +0x64 forward ("the facing axis"). Animation-driven in the real game
// (Sec5.4.5) - injected here.
struct Orientation {
    Vec3 right, up, forward;
    // R . v as the composer applies it (Sec5.4.3 step 9: "E <- R.E"):
    // a local vector expressed in the rows' frame, v.x along right, v.y
    // along up, v.z along forward. CHOSEN convention (x=right, y=up,
    // z=forward) - the spec names the rows, not the multiplication order.
    Vec3 apply(Vec3 local) const { return right * local.x + up * local.y + forward * local.z; }
};

// --- engine timers ------------------------------------------------------------
// A timer is "set" with an expiry time in engine ms; 0x00d9e210 is "ms
// remaining" (Sec3.5.4 step 5). "Expired" = set and remaining <= 0.
struct EngineTimer {
    bool set = false;
    int64_t expiryMs = 0;
    void arm(int64_t nowMs, int64_t durationMs) { set = true; expiryMs = nowMs + durationMs; }
    void unset() { set = false; }
    int64_t remaining(int64_t nowMs) const { return set ? expiryMs - nowMs : 0; }
    bool expired(int64_t nowMs) const { return set && expiryMs - nowMs <= 0; }
    bool running(int64_t nowMs) const { return set && expiryMs - nowMs > 0; }
    bool unsetOrExpired(int64_t nowMs) const { return !set || expired(nowMs); }
};

// --- stance bytes (Sec3.7) ------------------------------------------------------
namespace stance {
// +0x1d94
inline constexpr uint8_t kSprint = 0x10;        // sprint requested and granted
inline constexpr uint8_t kBusted = 0x02;        // busted flag (spec-player-runtime 18.6 R3 note step 16; set by the bust commit)
inline constexpr uint8_t kWalk = 0x20;          // walk (KB/M: s = 0.3)
inline constexpr uint8_t kBurst = 0x40;         // sprint-from-standstill burst pending
inline constexpr uint8_t kMoveWhileLocked = 0x80; // move attempted while animation-locked
// +0x1d95
inline constexpr uint8_t kStartTurnOwed = 0x01; // start/turn animation owed (momentum states)
inline constexpr uint8_t kPanting = 0x02;       // stamina at/below PantPercentage
inline constexpr uint8_t kScopeView = 0x10;     // weapon scope view active
// +0x1d96
inline constexpr uint8_t kSprintWanted = 0x02;  // sprint was wanted this frame
inline constexpr uint8_t kSprintLatch = 0x04;   // sprint-toggle latch
inline constexpr uint8_t kWalkLatch = 0x08;     // walk-toggle latch
inline constexpr uint8_t kCrouchLatch = 0x10;   // crouch-toggle latch
// +0x1d97
inline constexpr uint8_t kStopping = 0x08;      // intent just went to zero with residual velocity
}  // namespace stance

// +0xccc locomotion states (Sec5.0 item 8, Sec5.5 step 3).
inline constexpr int32_t kLocoIdle = 0;
inline constexpr int32_t kLocoWalk = 1;
inline constexpr int32_t kLocoRun = 2;
inline constexpr int32_t kLocoSprint = 3;

// --- the per-player state this unit owns ------------------------------------------
struct PlayerMovementState {
    // Stance bytes +0x1d94..+0x1d97 (Sec3.7).
    uint8_t b1d94 = 0, b1d95 = 0, b1d96 = 0, b1d97 = 0;
    // Crouched state (0x00985cd0) and fine aim (+0x1d98 bit 0x80). Owned
    // by stance/aim code outside U2; kept here because U2's crouch and
    // sprint routines change them (Sec3.6.2 step 5, Sec3.6.3).
    bool crouched = false;
    bool fineAim = false;

    Vec3 position;            // +0x40 (synced from the capsule, Sec5.7)
    Vec3 desiredMove;         // +0x2048 D (Sec3.7)
    Vec3 smoothedMove;        // +0x203c r (Sec3.7)
    Vec3 facingA50;           // +0xa50 desired facing = unit(r) (Sec5.4.4)
    double desiredSpeedA60 = 0;  // +0xa60
    double speedScaleA64 = 1.0;  // +0xa64 k, 1.0 at construction (Sec5.5, CONFIRMED)
    double turnAngleA68 = 0;     // +0xa68 (only the step-3 zero is modelled)
    Vec3 moveTargetCb0;       // +0xcb0 move target (Sec5.4.4)
    int32_t locomotionState = kLocoIdle;  // +0xccc

    Vec3 controllerVelocity1cec;  // +0x1cec controller desired velocity (Sec5.4)
    Vec3 velocityD04;             // +0xd04/+0xd08/+0xd0c published velocity (Sec5.6.6)
    double speedD1c = 0;          // +0xd1c published speed
    Vec3 capsulePosition;         // the integrator's own body (replaces the proxy)
    // CONFIRMED meaning of controller+0x08 bit 0x04 ("supported" -
    // spec-player-runtime.md Sec5.6.1), tracked when a CollisionHook is
    // wired (sr3player/character_collision.h); true at construction
    // (CHOSEN: matches SeedOnFootCommonCase()'s own on-foot/on-ground
    // common case) and left unchanged by the plain direct integrator below
    // when no CollisionHook is set.
    bool grounded = true;

    // Timers (Sec3.7 table, Sec5.5). +0x2068/+0x206c/+0x205c are not kept:
    // they feed only the animation requests / sprint style of Sec3.5.4
    // steps 13 and 16, which are no-ops here (Sec3.8 item 4).
    EngineTimer sprintRamp2058;      // sprint ramp
    EngineTimer sprintCooldown2060;  // sprint cool-down (writer not in U2/U3 - read only)
    EngineTimer sprintStart2054;     // 750 ms on sprint start (Sec5.5 step 3)
    EngineTimer leaveIdleAa0;        // 500 ms on leaving state 0
    // Not a field of the human - see updateLocomotionState(): the previous
    // frame's raw sprint request, used to detect "the request ends".
    bool prevSprintRequest = false;
};

// Controls-band globals of 0x005ad130 / 0x005ab930 (Sec3.5.2, Sec3.6.2). One
// set per local player in the real game (they are globals); kept per
// PlayerMovement here. Static values: zero (CHOSEN, as above).
struct ControlsGlobals {
    float publishedA = 0, publishedB = 0;  // 0x0140e6c0 / 0x0140e6bc
    float prevA = 0, prevB = 0;            // 0x0140e6a8 / 0x0140e6a4
    float releaseEnergy = 0;               // 0x0140e694
    bool stoppedLatch = false;             // 0x0140e6dd
    EngineTimer keyboardSprintGrace;       // 0x012e6df4 (300 ms)
    Vec3 targetPoint;                      // 0x02289b24..0x02289b2c
};

// --- tweak constants (Sec3.5.4 step 5/14, Sec3.6.3) ---------------------------------
//
// These are entries of the engine's tweak REGISTRY (spec-tables-progression.md
// Sec10.10: 586 tunables registered by static constructors with a name, type
// and destination). A tweak_table.xtbl row may override one by name; a
// tunable with no row keeps its STATIC IMAGE VALUE, which no spec in this
// repository publishes. So each is OPEN until seeded - by the caller
// directly, or from the real tweak_table.xtbl through the existing reader:
// SeedMoveTweaksFromTweakTable() in sr3player/movement_tweak_table.h.
struct MoveTweaks {
    OpenValue<double> maxRunSpeed{"Player_max_run_speed (0x0262e9e0)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxSprintSpeed{"Player_max_sprint_speed (0x0262e9e8)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxHeavyWeaponSprintSpeed{"Player_max_heavy_weapon_sprint_speed (0x0262e9c0)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxHeavyWeaponSpeed{"Player_max_heavy_weapon_speed (0x0262e9c4)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxAvatarMoveSpeed{"Player_max_avatar_move_speed (0x0262e9c8)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxSwimmingSpeed{"Player_max_swimming_speed (0x0262e9cc)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxFineAimSpeed{"Player_max_fine_aim_speed (0x0262e9e4)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> maxRiotMoveSpeed{"Player_max_riot_move_speed (0x0262e9dc)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> skydiveMoveSpeed{"Player_skydive_move_speed (0x0262e9d4)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> rappelMoveSpeed{"Player_rappel_move_speed (0x0262e9d8)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> crouchMoveSpeed{"Player_crouch_move_speed (0x0262e9d0)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<double> acceleration{"Player_acceleration (0x0140e64c)", "spec-player-runtime.md Sec3.5.4 step 14"};
    OpenValue<double> diveRollAcceleration{"Player_dive_roll_acceleration (0x0140e648)", "spec-player-runtime.md Sec3.5.4 step 14"};
    OpenValue<double> crouchClickSensitivity{"Crouch_click_sensitivity (0x0140e690)", "spec-player-runtime.md Sec3.6.3"};
    // The boosted sprint 0x0263ae34 (HIGH: set to 30.0 and decaying to
    // Player_max_sprint_speed by a per-player update outside U2). Only read
    // with the sprint upgrade bit +0x1c9c 0x80000 set.
    OpenValue<double> boostedSprintSpeed{"boosted sprint speed 0x0263ae34", "spec-player-runtime.md Sec3.5.4 step 5 (HIGH)"};

    // Seeds every tunable above whose registry NAME matches a row
    // (case-insensitive, as the tweak loader matches - spec-tables-
    // progression.md Sec10.10). Takes plain (name, value) pairs so this
    // library does not depend on sr3tables_progression; feed it the
    // name/value of each ParseTweakTable() row. Returns how many it seeded.
    int seedFromTweakRows(const std::vector<std::pair<std::string, double>>& rows);
};

// --- world predicates this project has no subsystem for ---------------------------
//
// Every one is OPEN until set. They are read LAZILY, in the spec's order,
// so an OPEN predicate only throws when the real code would actually have
// tested it. SeedOnFootCommonCase() is an explicit, CHOSEN opt-in that
// seeds the plain on-foot case (no vehicle, alive, not swimming, ...).
struct MovementWorld {
    // +0x1d99 bits 0x08/0x10 - the integration point with sr3luahost's
    // CharacterState (bindDisableFlags). Own fallbacks when not bound.
    OpenValue<bool> controlsDisabled{"character byte +0x1d99 bit 0x08 (controls disabled)", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> movementDisabled{"character byte +0x1d99 bit 0x10 (movement disabled)", "spec-player-runtime.md Sec3.5.2 step 1 / Sec3.6.2"};
    const OpenValue<bool>* controlsDisabledSource = nullptr;
    const OpenValue<bool>* movementDisabledSource = nullptr;
    // Binds the two disable bits to another owner's OpenValue (e.g.
    // sr3luahost::CharacterState::controlsDisabled/movementDisabled, so
    // player_controls_disable / player_movement_disable take effect here).
    // The referenced objects must outlive this MovementWorld.
    void bindDisableFlags(const OpenValue<bool>& controls, const OpenValue<bool>& movement) {
        controlsDisabledSource = &controls;
        movementDisabledSource = &movement;
    }
    bool readControlsDisabled() const { return (controlsDisabledSource ? *controlsDisabledSource : controlsDisabled).get(); }
    bool readMovementDisabled() const { return (movementDisabledSource ? *movementDisabledSource : movementDisabled).get(); }

    // +0x2078, the on-foot action permission mask written by the classify
    // 0x005ac620 (Sec3.2, not implemented here). Bits read by U2: 0x4000
    // (sprint and walk toggle), 0x2000 (crouch).
    OpenValue<uint32_t> permissions2078{"on-foot permission mask +0x2078 (classify 0x005ac620 not implemented)", "spec-player-runtime.md Sec3.2"};

    // Hard block 0x005ad050 (Sec3.5.2 step 1), in the spec's order.
    OpenValue<bool> objectReady33{"+0x33 bit 0x08", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> state346{"0x009570b0 = 0x346", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> enteringVehicle{"entering a vehicle 0x009b91e0", "spec-player-runtime.md Sec3.5.1 step 4 / Sec3.5.2 step 1"};
    OpenValue<bool> dead{"dead 0x0096f4f0", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> scriptControlled{"script-controlled +0x28ac bit 0x02", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> flagE8_100000{"+0xe8 bit 0x100000", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> missionType1a{"mission type 0x1a", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> inCover{"in cover 0x009aa260(player, 0) (HYPOTHESIS name)", "spec-player-runtime.md Sec3.5.2 step 1/5"};
    OpenValue<int32_t> coverMode18d4{"+0x18d4", "spec-player-runtime.md Sec3.5.2 steps 1/5"};
    OpenValue<bool> pred9a9550{"0x009a9550", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> controllerState285{"controller state 0x285 active", "spec-player-runtime.md Sec3.5.2 step 1"};
    OpenValue<bool> ragdolled{"0x009a1dc0", "spec-player-runtime.md Sec3.5.2 step 1 / Sec3.6.2 step 5"};
    OpenValue<int32_t> aiSubState500{"AI sub-state +0x500", "spec-player-runtime.md Sec3.5.1 step 4 / Sec3.5.2 step 1"};
    OpenValue<int32_t> aiMode501{"AI mode +0x501", "spec-player-runtime.md Sec3.5.1 step 4"};

    // Axis modifiers (Sec3.5.2 step 4). Each "inactive" predicate, when
    // false, throws: the effects' internals are spec OPEN 17 / OPEN 22.
    OpenValue<bool> drunkEffectInactive{"drunk/weed effect 0x009758b0 inactive (internals OPEN 17)", "spec-player-runtime.md Sec3.5.2 step 4"};
    OpenValue<bool> mission16InvertActive{"0x012ec730 = 0 (mission-16 axis inversion; image value 0, OPEN 22)", "spec-player-runtime.md Sec3.5.2 step 4"};
    OpenValue<bool> activityInvertActive{"0x006e62e0 inversion (0x014f3d34, activity 8, 0x006e5430(7))", "spec-player-runtime.md Sec3.5.2 step 4"};

    // Target point overrides (Sec3.5.3).
    OpenValue<bool> moveToObjectHandleSet{"move-to-object handle +0x1a28/+0x1a2c non-zero", "spec-player-runtime.md Sec3.5.3"};
    OpenValue<Vec3> forcedTargetPoint{"forced target point +0x1a30", "spec-player-runtime.md Sec3.5.3"};

    // Orientation rows (Sec5.4.3); +0x64 is "the facing axis".
    OpenValue<Orientation> orientation{"orientation rows +0x4c/+0x58/+0x64 (animation-driven)", "spec-player-runtime.md Sec5.4.3"};

    // Sub-state bytes owned by jump/dive/climb code.
    OpenValue<bool> diving{"diving 0x009db960 (+0xcce = 0x26)", "spec-player-runtime.md Sec3.5.1 step 7"};
    OpenValue<bool> cceIs3{"+0xcce = 3", "spec-player-runtime.md Sec3.6.2 step 5"};
    OpenValue<int32_t> ccd{"+0xccd sub-state", "spec-player-runtime.md Sec5.4.3 step 5 / Sec5.4.4"};
    OpenValue<bool> climbingOrVaulting{"climbing or vaulting 0x00989670", "spec-player-runtime.md Sec5.5 step 1 / Sec3.6.2 step 7"};
    OpenValue<bool> jumping{"jumping 0x0098b970", "spec-player-runtime.md Sec3.6.2 step 7 / Sec5.4.3 step 9"};

    // Animation-controller predicates.
    OpenValue<bool> controllerFlag10{"animation controller flag 0x10 (movement locked)", "spec-player-runtime.md Sec3.5.4 step 3 / Sec3.6.2"};
    OpenValue<bool> controllerFlag4{"animation controller flag 4", "spec-player-runtime.md Sec3.5.4 step 4 / Sec5.4.3 step 5"};
    OpenValue<bool> controllerFlag800{"animation controller flag 0x800", "spec-player-runtime.md Sec3.5.4 step 9"};
    OpenValue<bool> controllerFlag100{"animation controller flag 0x100 (root-motion driven)", "spec-player-runtime.md Sec5.4.3 step 5"};
    OpenValue<bool> sprintAnimStateActive{"animation state 0x305 or 0x1d8f active", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<bool> momentumState1d80{"controller state 0x1d80 active", "spec-player-runtime.md Sec3.5.4 step 6"};
    OpenValue<bool> momentumState1d8f{"controller state 0x1d8f active", "spec-player-runtime.md Sec3.5.4 step 6"};
    OpenValue<bool> state1d50Active{"animation state 0x1d50 active", "spec-player-runtime.md Sec3.6.2 step 5"};
    OpenValue<bool> startMoveState2f9Active{"start-to-move state 0x2f9 active", "spec-player-runtime.md Sec5.5 step 3"};
    OpenValue<double> startMoveState2f9Progress{"start-to-move state 0x2f9 progress 0x004b4600", "spec-player-runtime.md Sec5.5 step 3"};

    // Stance-table conditions (Sec3.5.4 step 5), in table order.
    OpenValue<bool> heavyWeapon{"heavy weapon 0x009ba860", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<bool> avatar{"avatar 0x005e6ce0", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<bool> swimming{"swimming 0x009b5690", "spec-player-runtime.md Sec3.5.4 step 5 / Sec5.5"};
    OpenValue<bool> sprintUpgrade{"+0x1c9c bit 0x80000 (sprint upgrade)", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<bool> riotMode{"controller mode +0x44 = 0xb", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<bool> skydiving{"skydiving 0x009adb00", "spec-player-runtime.md Sec3.5.4 step 5"};
    OpenValue<bool> rappelling{"rappelling 0x009a5d60", "spec-player-runtime.md Sec3.5.4 step 5"};
    // Movement flags +0xd40 (writers OPEN 38): bit 0x2000 backpedal.
    OpenValue<uint32_t> movementFlagsD40{"movement flags +0xd40 (writers OPEN 38)", "spec-player-runtime.md Sec3.5.4 steps 5/14"};

    // Writer (Sec3.5.4) remaining predicates.
    OpenValue<bool> flagE0_1000{"+0xe0 bit 0x1000", "spec-player-runtime.md Sec3.5.4 step 10"};
    OpenValue<bool> accelShaping9573d0{"0x009573d0 (facing override / acceleration shaping, OPEN 13)", "spec-player-runtime.md Sec3.5.4 step 14 / Sec5.4.3 step 5"};
    OpenValue<bool> supportedByGround{"supported by ground 0x0096e670", "spec-player-runtime.md Sec3.5.4 step 14 / Sec5.6.3"};
    OpenValue<bool> flagE8_20{"+0xe8 bit 0x20", "spec-player-runtime.md Sec3.5.4 step 14"};
    OpenValue<bool> combatReady{"combat-ready 0x00943a20", "spec-player-runtime.md Sec3.5.4 step 19"};
    OpenValue<bool> facingOverrideActive{"facing override +0xa6c != -1 and +0xa7c = 1", "spec-player-runtime.md Sec3.5.4 step 19"};

    // Sprint routine (Sec3.6.2).
    OpenValue<bool> sprintSystemEnabled{"sprint system switch byte 0x0268d2a0 (0x009fee70)", "spec-player-runtime.md Sec3.6.2 step 1"};
    OpenValue<bool> stateTest985d80{"0x00985d80(player, 5 or 6, 0)", "spec-player-runtime.md Sec3.6.2 step 5"};
    OpenValue<bool> canMoveOnGround{"can move on the ground 0x00941da0", "spec-player-runtime.md Sec3.6.2 step 5 / Sec5.5 step 3"};
    OpenValue<bool> flagE8_100{"+0xe8 bit 0x100 (0x0097cdd0)", "spec-player-runtime.md Sec3.6.2 step 5"};
    OpenValue<bool> pred9ba300{"0x009ba300", "spec-player-runtime.md Sec3.6.2 step 5"};
    OpenValue<bool> crouchPossible{"crouch possible 0x00986810", "spec-player-runtime.md Sec3.6.3 step 2"};
    // 0x012e6ced: 1 in the image, no writer in .text (Sec3.6.3, HIGH).
    // Not OPEN: the image value is spec-given.
    uint8_t crouchStickTestBypass = 1;

    // Locomotion-state update (Sec5.5).
    OpenValue<bool> riding{"riding (seated in a vehicle)", "spec-player-runtime.md Sec5.5 step 3"};
    OpenValue<bool> swimStop9b5830{"0x009b5830 / 0x009b5670", "spec-player-runtime.md Sec5.5 step 3"};
    OpenValue<bool> keepState9b5600{"0x009b5600", "spec-player-runtime.md Sec5.5 step 3"};

    // Composer (Sec5.4.3) and gather (Sec5.6.3).
    OpenValue<bool> onMovingSupport{"+0x1c9c bit 0x2000 (moving support; +0x1c60 not modelled)", "spec-player-runtime.md Sec5.4.3 step 8"};
    OpenValue<bool> jumpUpgrade{"+0x1c9c bit 0x100000 (jump upgrade)", "spec-player-runtime.md Sec5.4.3 step 9"};
    OpenValue<bool> flagEc800{"+0xec bit 0x800", "spec-player-runtime.md Sec5.4.3 step 9"};
    OpenValue<bool> cceIs27{"+0xcce = 0x27", "spec-player-runtime.md Sec5.4.3 step 9"};
    OpenValue<bool> animationDrivesVertical{"animation-controller +0x1a4 bit 0x80 / +0xec bit 0x01 / cutscene", "spec-player-runtime.md Sec5.6.3 (e)"};
    OpenValue<bool> freeFall{"free fall 0x00994f60", "spec-player-runtime.md Sec5.6.3 (e)"};
};

// CHOSEN common-case seeding for the plain on-foot player this project can
// actually run: alive, ready, not in a vehicle/cover/water/air/ragdoll, no
// script control, no special mission types, no animation locks or special
// states, no weapon/avatar/upgrade, on the ground, axis modifiers inactive,
// AI mode 43 (Player, Sec3.4) with no sub-state, +0xccd = 0, sprint system on.
// NONE of these is a recovered engine value; each is "this project has no X
// subsystem, so the player is never in X". Leaves OPEN: the disable bits
// (bind them or set them), permissions2078, orientation and every tweak
// constant - those have no common case to default to.
void SeedOnFootCommonCase(MovementWorld& w);

// --- input seam (U0) ----------------------------------------------------------------
enum class MoveAction { Sprint, SprintToggle, Walk, WalkToggle, Crouch, CrouchToggle };

// What U2 reads from the input layer. InputSystemMoveSource is the real
// adapter onto sr3player::InputSystem (U0); tests may supply their own.
class MoveInputSource {
public:
    virtual ~MoveInputSource() = default;
    virtual float walkAxisA() = 0;  // CAA_WALK_TURN_LEFT_RIGHT through 0x005bb530
    virtual float walkAxisB() = 0;  // CAA_WALK_FORWARD_BACKWARD through 0x005bb530
    virtual bool gamepadActive() = 0;  // 0x005bc5d0
    virtual bool held(MoveAction a, int* out) = 0;     // 0x005bd7b0
    virtual bool pressed(MoveAction a, int* out) = 0;  // 0x005bae10
    virtual void consume(MoveAction a) = 0;            // 0x005bbdd0
    virtual bool optionBit0() = 0;  // 0x01411b00 bit 0 (Sec3.6.3)
};

// Published action indices: CBA_OFC_SPRINT 0x12, CBA_OFC_SPRINT_TOGGLE 0x13,
// CBA_OFC_WALK 0x14, CBA_OFC_CROUCH 0x19 (spec-player-runtime.md Sec1.8.4);
// CAA_WALK_FORWARD_BACKWARD 2, CAA_WALK_TURN_LEFT_RIGHT 3
// (spec-lua-api-behaviour.md, game_get_key_name_for_action's axis indices).
inline constexpr int kCbaOfcSprint = 0x12;
inline constexpr int kCbaOfcSprintToggle = 0x13;
inline constexpr int kCbaOfcWalk = 0x14;
inline constexpr int kCbaOfcCrouch = 0x19;
inline constexpr int kCaaWalkForwardBackward = 2;
inline constexpr int kCaaWalkTurnLeftRight = 3;

class InputSystemMoveSource : public MoveInputSource {
public:
    explicit InputSystemMoveSource(InputSystem& in) : in_(in) {}
    // Not published anywhere in this repository (spec-tables-ui-controls.md
    // Sec4.1 lists only representative CBA values): OPEN until seeded.
    OpenValue<int32_t> walkToggleCba{"CBA_OFC_WALK_TOGGLE index", "spec-tables-ui-controls.md Sec4.1 (not published)"};
    OpenValue<int32_t> crouchToggleCba{"CBA_OFC_CROUCH_TOGGLE index", "spec-tables-ui-controls.md Sec4.1 (not published)"};

    float walkAxisA() override { return in_.axis(kCaaWalkTurnLeftRight); }
    float walkAxisB() override { return in_.axis(kCaaWalkForwardBackward); }
    bool gamepadActive() override { return in_.gamepadIsActiveInput(); }
    bool held(MoveAction a, int* out) override { return in_.held(cba(a), out); }
    bool pressed(MoveAction a, int* out) override { return in_.pressed(cba(a), out); }
    void consume(MoveAction a) override { in_.consume(cba(a)); }
    bool optionBit0() override;

private:
    int cba(MoveAction a) const;
    InputSystem& in_;
};

// --- the stamina machine (external) -------------------------------------------------
// 0x009fef60 / 0x009ffd00 / 0x009ff400 / 0x009fffc0 / 0x009fffa0 / 0x009ff270
// (Sec3.3, Sec3.6.2). Their CONFIRMED effects on the player's stance bits
// are applied by PlayerMovement itself (see sprintRoutine); everything
// else (states 0-4, the amount, timers, HUD) is the stamina system's.
class SprintStamina {
public:
    virtual ~SprintStamina() = default;
    // 0x009fef60 after its unconditional "+0x1d94 bit 0x10 clear": advance
    // the recharge machine; may set/clear +0x1d95 bit 0x02 (panting) and
    // clear +0x1d96 bit 0x04 when recharge starts.
    virtual void notSprintingStep(PlayerMovementState& st) = 0;
    // 0x009ffd00: true while stamina remains (the caller then sets +0x1d94
    // bit 0x10 and clears +0x1d95 bit 0x02).
    virtual bool sprintingStep(PlayerMovementState& st) = 0;
    virtual double available() = 0;  // 0x009ff400 (>= 0 = available)
    virtual void startPenalty() = 0; // 0x009fffc0
    virtual void jumpPenalty() = 0;  // 0x009fffa0
    virtual void step9ff270() = 0;   // 0x009ff270
};

// The default: every call throws OPEN. The metered_sprint.xtbl parameters
// are specified (spec-tables-progression.md Sec10.11) but the state machine
// 0x0268d2ac's transitions are not.
class OpenSprintStamina : public SprintStamina {
public:
    void notSprintingStep(PlayerMovementState&) override { fail("0x009fef60"); }
    bool sprintingStep(PlayerMovementState&) override { fail("0x009ffd00"); }
    double available() override { fail("0x009ff400"); }
    void startPenalty() override { fail("0x009fffc0"); }
    void jumpPenalty() override { fail("0x009fffa0"); }
    void step9ff270() override { fail("0x009ff270"); }

private:
    [[noreturn]] static void fail(const char* fn) {
        throw OpenStateError(std::string("stamina state machine 0x0268d2ac (") + fn + ")",
                             "spec-player-runtime.md Sec3.3 / Sec3.6.2");
    }
};

// --- per-frame inputs -----------------------------------------------------------------
struct CameraRows {
    Vec3 row0;  // 0x013c8790 R0 ("right", HIGH)
    Vec3 row2;  // 0x013c87b0 R2 ("forward", HIGH)
};

struct MoveFrameInput {
    int64_t nowMs = 0;       // engine time (0x029f4db0 domain)
    double dt = 0;           // 0x0132a0ac - writer approach, composer, integrator
    double axisDt = 0;       // 0x0132a0b4 - the release detector's own frame-time global
    CameraRows camera;       // injected: the camera update ran earlier in the frame
    // The "controls ran this frame" flag 0x0140e6f8 (Sec3.3): cleared by an
    // active QTE or a fired prompt, in which case the movement post-hook
    // (the move slot) does not run.
    bool controlsRanThisFrame = true;
    bool jumpedThisFrame = false;  // the jump handler's "jumped" result (Sec3.6.4)
    // E: the non-locomotion animation layers' translation for this step
    // (Sec5.4.3 step 3). No real node-graph evaluation exists in this
    // project, so this is an explicit input: zero = no root motion.
    Vec3 animationTranslationE;
};

// Trace of the frame phases, for ordering tests (Sec5.9).
// NoIntent = a gate/locked path's "writer with own position, s = 0".
enum class FramePhase { NoIntent, OnFootActions, MoveSlot, Compose, Integrate, Publish };

// --- the unit ----------------------------------------------------------------------------
class PlayerMovement {
public:
    PlayerMovement(MoveInputSource& input, SprintStamina& stamina) : in_(input), stamina_(stamina) {}

    PlayerMovementState st;
    ControlsGlobals g;
    MovementWorld world;
    MoveTweaks tweaks;

    // "player gave input" 0x00950b70(player, blend) (Sec3.4): its effects
    // are the idle timer, an animation end and the AI-mode Idle -> 43
    // transition - all owned elsewhere. Exposed as a hook + counter.
    std::function<void(double blend)> onPlayerGaveInput;
    uint64_t playerGaveInputCalls = 0;

    // One full frame of control state 2 ("normal play") in Sec5.1 order:
    // on-foot actions (crouch, then sprint/walk, Sec3.6.1 steps 6-7) ->
    // move slot (unless controlsRanThisFrame is false) -> composer ->
    // integrator -> publish. Each phase exactly once. The controls-disabled
    // gate of 0x005b7430 (Sec2.7: +0x1d99 bit 0x08 -> writer with own
    // position, early-out, so no action dispatch and no move slot) is
    // applied first; the other 0x005b7430 gates (menu, camera mode 3,
    // cinema editor, cutscene, script-controlled) are not modelled.
    void runFrame(const MoveFrameInput& f);
    // One frame of the locked rows 0/1 (0x005b2170, Sec2.7): writer with
    // own position and s = 0, then 0x00985df0(player, 0, 1, 1) and
    // 0x009fef60; then compose/integrate/publish as usual.
    void runLockedFrame(const MoveFrameInput& f);
    bool traceEnabled = false;
    std::vector<FramePhase> trace;

    // --- U2, individually callable -------------------------------------
    void crouchRoutine(const MoveFrameInput& f);            // Sec3.6.3
    void walkRoutine();                                     // 0x005ab860
    void sprintRoutine(const MoveFrameInput& f);            // 0x005ab930
    void moveSlot(const MoveFrameInput& f);                 // 0x005b3e80
    struct AxisResult {
        float A = 0, B = 0;
        bool centred = false, stopped = false;
    };
    AxisResult readAxes(const MoveFrameInput& f);           // 0x005ad130
    Vec3 targetPoint(float B, float A, Vec3 F, Vec3 Rt);    // 0x005b31f0 (flag = 1)
    void writeIntent(const MoveFrameInput& f, Vec3 target, double s);  // 0x005aee30
    // The locked/gated callers' "no intent": own position, s = 0 (Sec3.1).
    void writeNoIntent(const MoveFrameInput& f) { writeIntent(f, st.position, 0.0); }
    // 0x009dca30: stance maximum speed M and M0 (Sec3.5.4 step 5).
    void stanceMaxSpeed(int64_t nowMs, double& M, double& M0);

    // --- U3 ------------------------------------------------------------
    void updateLocomotionState(const MoveFrameInput& f, bool sprint, bool stopped);  // 0x00985df0
    void composeVelocity(const MoveFrameInput& f);          // 0x0099d1b0, player branch
    void integrate(const MoveFrameInput& f);                // replaces Sec5.6.3-Sec5.6.6
    void publish();                                         // Sec5.6.6 write-back + Sec5.7 sync

    // Optional real collision integrator (2026-10-07 character-proxy/
    // collision follow-up task; sr3player/character_collision.h's
    // CharacterProxyDriver is the real implementation, wired to the REAL
    // sr3physics::CharacterProxyStepper lock-step loop and
    // CharacterProxyListener queue-only contract). When set, integrate()
    // asks it to resolve the capsule's movement against real collision
    // geometry INSTEAD OF this class's own licensed-but-naive direct
    // integrator ("position += velocity*dt", spec-player-runtime.md
    // Sec5.11 item 5). Left unset (nullptr) by default - every existing
    // caller/test keeps the EXACT prior behaviour, byte for byte. Kept
    // header-only in movement.h (only Vec3, no other dependency) so this
    // file does not need to include character_collision.h, which itself
    // includes THIS header - CollisionHookResult is the minimal shape the
    // hook needs to return; CharacterProxyDriver::step()'s own richer
    // CapsuleStepResult (pass count, wall-hit records) is a tool-side
    // diagnostic concern, not part of this library's own contract.
    struct CollisionHookResult {
        Vec3 position;
        Vec3 velocity;
        bool grounded = true;
    };
    std::function<CollisionHookResult(Vec3 fromPosition, Vec3 desiredVelocity, double dt)> collisionHook;

    // Integrator output between integrate() and publish() (the "proxy").
    Vec3 capsuleVelocity;
    bool capsuleStepped = false;  // false when dt == 0 (0x0099e7e0 does nothing)
    // Moves the body and the capsule together (the gather's warp request
    // +0xec bit 0x08 -> 0x007a4a40 moves the proxy to +0x40, Sec5.6.3 (c)).
    void warp(Vec3 p) {
        st.position = p;
        st.capsulePosition = p;
    }

private:
    bool diving() const { return world.diving.get(); }
    bool sprintBit() const { return (st.b1d94 & stance::kSprint) != 0; }
    void playerGaveInput(double blend);
    void notSprintingStep();
    MoveInputSource& in_;
    SprintStamina& stamina_;
};

}  // namespace sr3player
