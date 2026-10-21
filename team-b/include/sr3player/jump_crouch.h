// sr3player/jump_crouch.h - on-foot jump gate, launch and airborne bookkeeping (free-roam task T2).
//
// SPEC SOURCES (spec-*.md only):
//   * Jump input gate: spec-player-runtime.md Sec3.6.4 (CONFIRMED). Only on CBA_OFC_JUMP_CLIMB pressed, with the
//     permission bit +0x2078 & 0x200 and no melee started this frame; "unless 0x0098b970" (already jumping) the ledge
//     probe runs; "unless 0x00989670" the jump is launched via 0x0098bea0(player, sprint held).
//     0x00989670 = "climbing or vaulting" (spec-player-runtime.md Sec5.5 step 1; JC-2) -> JumpGateInputs::blocked.
//     0x0098b970 ("already jumping") ONLY suppresses the ledge probe (not modelled here), NOT the jump itself.
//   * Launch formula: Sec5.6.5 "Jump launch" (CONFIRMED): gravityDir = normalize(world gravity);
//     speed = sqrt(2 * |gravity| * |heightArg|); velocity = -gravityDir * speed. Bit 0x04 (supported) is cleared.
//     World gravity = (0, -9.8, 0) (spec-physics-runtime.md Sec4.1 step 5, CONFIRMED).
//   * Airborne integration: Sec5.6.5 "Gravity integration" (quadratic terminal-velocity falloff) is ALREADY in
//     CharacterProxyDriver::step() (character_collision.h); this header only owns the vertical velocity that is fed in
//     and the landing/peak/airtime bookkeeping that is read back out.
//
// CHOSEN (not spec facts):
//   * kDefaultJumpHeightMetres = 1.0. The height argument that the default jump passes to 0x0098bea0 is NOT in any spec
//     (Sec5.6.5 says the two other callers pass "1.0/x of the cast sub-object" and a fixed ~0.016667, neither
//     identifies the default jump); the real clips (spec-anim-runtime.md jump_sm, 'force use velocity' window) drive
//     the take-off vertical. 1.0 m is a plain human standing-jump height. Team A question T2-Q1.
//   * JumpGateInputs::airborne (JC-2): NOT a spec gate (0x0098b970 only suppresses the ledge probe); kept as a CHOSEN
//     guard so a mid-air press cannot launch a second jump (the 0.1 m supported test below is the same guard).
//   * "grounded" for the gate: the controller's supported state (CapsuleStepResult::grounded of the previous frame).
//
// Pure, header-only, unit-tested by tests/synthetic_jump_crouch_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>

namespace sr3player {

inline constexpr double kDefaultJumpHeightMetres = 1.0;  // CHOSEN, see above

// Sec5.6.5: speed = sqrt(2 * |g| * h). Returns 0 for non-positive height (the launch is gated on heightArg > 0).
inline double JumpLaunchSpeed(double gravityMagnitude, double heightMetres) {
    if (!(heightMetres > 0.0)) return 0.0;
    return std::sqrt(2.0 * std::fabs(gravityMagnitude) * std::fabs(heightMetres));
}

struct JumpGateInputs {
    bool jumpPressed = false;       // CBA_OFC_JUMP_CLIMB pressed this frame (Sec3.6.4 head)
    bool permitted = true;          // +0x2078 bit 0x200 (on-foot jump/climb permission)
    bool meleeStarted = false;      // "no melee started this frame"
    bool controlsDisabled = false;  // Sec2.7: the controls-disabled gate skips the action dispatch entirely
    bool airborne = false;          // CHOSEN guard (not the spec: 0x0098b970 only suppresses the ledge probe)
    bool blocked = false;           // 0x00989670 "climbing or vaulting" (Sec3.6.4 step 2: no jump while set)
    double heightAboveGround = 0.0; // position.y - ground chain value; the "supported" stand-in (CHOSEN, below)
};

// CHOSEN: "supported" = within 0.1 m of the ground value (same tolerance as ground_follow.h's supported threshold).
// Without a Havok support query (Sec5.6.5 support resolution is out of scope) this is the grounded test; it also stops
// a mid-air press (e.g. while falling off a ledge or while the follower is still easing the ground target) from
// launching a second jump.
inline constexpr double kJumpSupportedThreshold = 0.1;

inline bool JumpGate(const JumpGateInputs& g) {
    return g.jumpPressed && g.permitted && !g.meleeStarted && !g.controlsDisabled && !g.airborne && !g.blocked &&
           g.heightAboveGround <= kJumpSupportedThreshold;
}

struct JumpStats {
    int jumps = 0;
    int landings = 0;
    double peakAboveTakeoff = 0.0;  // max over jumps
    double lastPeakAboveTakeoff = 0.0;
    double lastAirtimeMs = 0.0;
    double totalAirtimeMs = 0.0;
    int crouchFrames = 0;
    int airFrames = 0;
    double crouchMaxSpeed = 0.0;  // peak grounded horizontal speed while crouched
    double standMaxSpeed = 0.0;   // peak grounded horizontal speed while standing
};

// Owns the vertical velocity fed to the character controller across frames and tracks the flight.
class JumpController {
public:
    explicit JumpController(double heightMetres = kDefaultJumpHeightMetres, double gravityY = -9.8)
        : height_(heightMetres), gravityY_(gravityY) {}

    // Edge request (key press / script). Latched until the next beginFrame() consumes it.
    void requestJump() { requested_ = true; }

    bool airborne() const { return airborne_; }
    double verticalVelocity() const { return vy_; }
    const JumpStats& stats() const { return stats_; }

    // Called once per controller step BEFORE it runs. `controlsDisabled`/`permitted` etc. feed the gate. Returns the
    // vertical velocity to pass into the controller (0 while grounded and not launching; the carried velocity while
    // airborne - the controller itself applies gravity). `launchedOut` is true on the frame the launch happens.
    double beginFrame(JumpGateInputs gate, double posY, bool* launchedOut = nullptr) {
        gate.jumpPressed = requested_;
        gate.airborne = airborne_;
        requested_ = false;
        bool launched = false;
        if (JumpGate(gate)) {
            vy_ = JumpLaunchSpeed(gravityY_, height_);  // velocity = -gravityDir * speed, gravityDir = (0,-1,0)
            airborne_ = true;
            takeoffY_ = posY;
            peakY_ = posY;
            airtimeMs_ = 0.0;
            ++stats_.jumps;
            launched = true;
        }
        if (launchedOut) *launchedOut = launched;
        // Not jumping: carry the controller's own fall velocity (0 when it was grounded), so walking off a ledge /
        // down a slope the ground target stepped below falls under accumulating gravity instead of restarting from
        // vy = 0 every frame (that gave a constant ~|g|*dt^2 = 0.011 m/frame at 30 Hz "drift", never reaching terminal).
        return airborne_ ? vy_ : fallVy_;
    }

    // Called once per controller step AFTER it runs, with the controller's published result.
    void endFrame(double newY, double newVy, bool grounded, double dtSeconds) {
        if (!airborne_) {
            fallVy_ = (grounded || newVy > 0.0) ? 0.0 : newVy;
            return;
        }
        fallVy_ = 0.0;
        ++stats_.airFrames;
        airtimeMs_ += dtSeconds * 1000.0;
        peakY_ = std::max(peakY_, newY);
        if (grounded && !launchedThisFrameStillRising(newVy)) {
            airborne_ = false;
            vy_ = 0.0;
            ++stats_.landings;
            stats_.lastPeakAboveTakeoff = peakY_ - takeoffY_;
            stats_.peakAboveTakeoff = std::max(stats_.peakAboveTakeoff, stats_.lastPeakAboveTakeoff);
            stats_.lastAirtimeMs = airtimeMs_;
            stats_.totalAirtimeMs += airtimeMs_;
        } else {
            vy_ = newVy;
        }
    }

    void noteCrouchFrame() { ++stats_.crouchFrames; }
    // Diagnostics: peak horizontal speed seen on the ground while crouched / standing.
    void noteGroundSpeed(double horizontalSpeed, bool crouched) {
        if (airborne_) return;
        double& m = crouched ? stats_.crouchMaxSpeed : stats_.standMaxSpeed;
        m = std::max(m, horizontalSpeed);
    }

private:
    // The controller reports grounded=true only when it crossed the ground plane (newY <= groundY) so a rising launch
    // frame is never grounded; kept as a hook for clarity.
    static bool launchedThisFrameStillRising(double newVy) { return newVy > 0.0; }

    double height_;
    double gravityY_;
    bool requested_ = false;
    bool airborne_ = false;
    double vy_ = 0.0;
    double fallVy_ = 0.0;  // controller fall velocity carried while NOT jumping (0 when grounded)
    double takeoffY_ = 0.0, peakY_ = 0.0, airtimeMs_ = 0.0;
    JumpStats stats_;
};

}  // namespace sr3player
