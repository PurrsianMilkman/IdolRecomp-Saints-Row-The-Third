// Synthetic tests for the player runtime units U2 (movement and intent) and
// U3 (the locomotion bridge), spec-player-runtime.md Sec3/Sec5. Every
// expected value is computed from a rule quoted from that spec (cited per
// block). Inputs are SYNTHETIC: the scripted input source, the fake stamina
// machine and the tweak values below (run 10, sprint 15, acceleration 20,
// ...) are test numbers chosen for easy arithmetic - NOT engine values; the
// real tweak values are OPEN (see MoveTweaks).
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/open_state.h"
#include "sr3player/input.h"
#include "sr3player/movement.h"
#include "sr3player/movement_tweak_table.h"
#include "sr3tables_progression/tables_rules.h"
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

bool deq(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }
bool veq(Vec3 a, Vec3 b, double tol = 1e-6) { return deq(a.x, b.x, tol) && deq(a.y, b.y, tol) && deq(a.z, b.z, tol); }

template <typename F>
bool throwsOpen(F&& f, const char* needle = nullptr) {
    try {
        f();
    } catch (const sr3luahost::OpenStateError& e) {
        return needle == nullptr || e.global().find(needle) != std::string::npos;
    }
    return false;
}

constexpr int idx(MoveAction a) { return static_cast<int>(a); }

// Scripted MoveInputSource. Records the frame phase of every read, so the
// ordering test can prove all input is read inside the controls phases.
struct ScriptedSource : MoveInputSource {
    float A = 0, B = 0;
    bool pad = true;  // gamepad is the active input
    bool opt = true;  // 0x01411b00 bit 0
    bool heldA[6] = {}, pressedA[6] = {};
    int outA[6] = {0, 0, 0, 0, 0, 0};  // out code reported when true (0 = pad button)
    std::vector<MoveAction> consumed;
    const PlayerMovement* pm = nullptr;
    std::vector<FramePhase> readPhases;
    int reads = 0;

    void note() {
        ++reads;
        if (pm && pm->traceEnabled && !pm->trace.empty()) readPhases.push_back(pm->trace.back());
    }
    void kb() {
        for (int& o : outA) o = kOutKeyOrMouse;
    }
    void clearPressed() {
        for (bool& p : pressedA) p = false;
    }
    float walkAxisA() override { note(); return A; }
    float walkAxisB() override { note(); return B; }
    bool gamepadActive() override { note(); return pad; }
    bool held(MoveAction a, int* out) override {
        note();
        const bool v = heldA[idx(a)];
        if (out) *out = v ? outA[idx(a)] : kOutNone;
        return v;
    }
    bool pressed(MoveAction a, int* out) override {
        note();
        const bool v = pressedA[idx(a)];
        if (out) *out = v ? outA[idx(a)] : kOutNone;
        return v;
    }
    void consume(MoveAction a) override { consumed.push_back(a); }
    bool optionBit0() override { note(); return opt; }
};

struct FakeStamina : SprintStamina {
    bool remains = true;
    double avail = 1.0;
    int notSprinting = 0, sprinting = 0, start = 0, jump = 0, s9ff270 = 0;
    void notSprintingStep(PlayerMovementState&) override { ++notSprinting; }
    bool sprintingStep(PlayerMovementState&) override { ++sprinting; return remains; }
    double available() override { return avail; }
    void startPenalty() override { ++start; }
    void jumpPenalty() override { ++jump; }
    void step9ff270() override { ++s9ff270; }
};

const Orientation kIdentity{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

struct Rig {
    ScriptedSource src;
    FakeStamina stamina;
    PlayerMovement pm{src, stamina};
    Rig() {
        src.pm = &pm;
        SeedOnFootCommonCase(pm.world);
        pm.world.controlsDisabled.set(false);
        pm.world.movementDisabled.set(false);
        pm.world.permissions2078.set(0x4000u | 0x2000u);
        pm.world.orientation.set(kIdentity);
        // SYNTHETIC tweak values (test arithmetic only).
        pm.tweaks.maxRunSpeed.set(10);
        pm.tweaks.maxSprintSpeed.set(15);
        pm.tweaks.crouchMoveSpeed.set(4);
        pm.tweaks.maxFineAimSpeed.set(3);
        pm.tweaks.acceleration.set(20);
        pm.tweaks.diveRollAcceleration.set(40);
        pm.tweaks.crouchClickSensitivity.set(0.5);
    }
    MoveFrameInput frame(int64_t now, double dt = 1.0 / 30.0) const {
        MoveFrameInput f;
        f.nowMs = now;
        f.dt = dt;
        f.axisDt = 1.0 / 30.0;
        f.camera.row0 = {1, 0, 0};
        f.camera.row2 = {0, 0, 1};
        return f;
    }
};

// ===================================================================
// Sec3.5.5 - the move-intent closed form D = u . min(s,1) . M
// ===================================================================
void testClosedForm() {
    // Pad, half tilt right: s = A^2 + B^2 = 0.25 (quadratic), u = Rt.
    {
        Rig r;
        r.src.A = 0.5f;
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {2.5, 0, 0}));
        // Approach (Sec3.5.4 step 15): step = 20 * (1 + 0.5*2.5) / 30 = 1.5.
        CHECK(veq(r.pm.st.smoothedMove, {1.5, 0, 0}));
        CHECK(deq(r.pm.st.desiredSpeedA60, 1.5));
        CHECK(veq(r.pm.st.facingA50, {1, 0, 0}));  // unit(r), Sec5.4.4
        CHECK(r.pm.st.desiredMove.y == 0);         // the intent has no vertical component
    }
    // Pad, half tilt forward: +B moves along flattened row 2.
    {
        Rig r;
        r.src.B = 0.5f;
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 2.5}));
    }
    // Full diagonal: s = 2 clamps to 1 -> |D| = M.
    {
        Rig r;
        r.src.A = 1;
        r.src.B = 1;
        r.pm.runFrame(r.frame(1000));
        const double c = 10.0 / std::sqrt(2.0);
        CHECK(veq(r.pm.st.desiredMove, {c, 0, c}));
    }
    // Camera-relative in the horizontal plane: a pitched, yawed camera's
    // row 2 (0.6, -0.5, 0.8) flattens to (0.6, 0, 0.8) (Sec3.5.1 step 3).
    {
        Rig r;
        r.src.B = 1;
        MoveFrameInput f = r.frame(1000);
        f.camera.row2 = {0.6, -0.5, 0.8};
        f.camera.row0 = {0.8, 0.0, -0.6};
        r.pm.runFrame(f);
        CHECK(veq(r.pm.st.desiredMove, {6, 0, 8}));
    }
    // A degenerate camera row (all within 1e-5) normalises to (1, 0, 0).
    {
        Rig r;
        r.src.B = 1;
        MoveFrameInput f = r.frame(1000);
        f.camera.row2 = {0, 1, 0};  // looking straight down: flattened row is zero
        r.pm.runFrame(f);
        CHECK(veq(r.pm.st.desiredMove, {10, 0, 0}));
    }
    // Keyboard + walk: s = 0.3 (Sec3.5.1 step 7), any direction.
    {
        Rig r;
        r.src.pad = false;
        r.src.kb();
        r.src.A = 1;
        r.src.heldA[idx(MoveAction::Walk)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kWalk) != 0);
        CHECK(veq(r.pm.st.desiredMove, {3, 0, 0}));
    }
    // Keyboard without walk: keys give +-1, s = A^2+B^2 >= 1 -> M.
    {
        Rig r;
        r.src.pad = false;
        r.src.kb();
        r.src.A = -1;
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {-10, 0, 0}));
    }
    // Pad + walk bit: the bit has no effect on a pad (Sec3.6.2): s stays quadratic.
    {
        Rig r;
        r.src.A = 0.5f;
        r.src.heldA[idx(MoveAction::Walk)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kWalk) != 0);
        CHECK(veq(r.pm.st.desiredMove, {2.5, 0, 0}));
    }
    // Sprint: s = 1 regardless of deflection, M = Player_max_sprint_speed.
    {
        Rig r;
        r.src.A = 0.5f;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.src.pressedA[idx(MoveAction::Sprint)] = true;
        r.pm.st.speedD1c = 5;  // > 4.0: the stamina "sprinting" step, not the burst
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        CHECK(veq(r.pm.st.desiredMove, {15, 0, 0}));
        CHECK(r.pm.st.locomotionState == kLocoSprint);
    }
    // Diving: s = 1, M = run speed (sp is the sprint BIT), and the
    // acceleration is replaced by Player_dive_roll_acceleration:
    // step = 40 * (1 + 0.5*10) / 30 = 8.
    {
        Rig r;
        r.src.A = 0.5f;
        r.pm.world.diving.set(true);
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {10, 0, 0}));
        CHECK(veq(r.pm.st.smoothedMove, {8, 0, 0}));
        CHECK(r.pm.st.locomotionState == kLocoIdle);  // 0x00985df0 returns at once while diving
    }
    // Edge case (Sec3.5.5): 1e-4 < |A| but s <= 1e-4 -> no rescale, D is the
    // raw 5.0-unit offset to the target point; r and +0xa60 stay bounded by M.
    {
        Rig r;
        r.src.A = 0.005f;  // s = 2.5e-5
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {5, 0, 0}));
        CHECK(r.pm.st.desiredSpeedA60 <= 10.0);
    }
}

// ===================================================================
// Sec3.5.4 steps 14-15/18 - approach, modifiers and the M clamp
// ===================================================================
void testApproach() {
    // Convergence toward D = (10,0,0) at a*(1 + 0.5*delta)*dt, no overshoot.
    {
        Rig r;
        r.src.A = 1;
        const double expect[] = {4.0, 20.0 / 3.0, 8.0 + 4.0 / 9.0};
        for (int i = 0; i < 3; ++i) {
            r.pm.runFrame(r.frame(1000 + 33 * i));
            CHECK(deq(r.pm.st.smoothedMove.x, expect[i], 1e-9));
        }
        double prev = r.pm.st.smoothedMove.x;
        for (int i = 3; i < 10; ++i) {
            r.pm.runFrame(r.frame(1000 + 33 * i));
            CHECK(r.pm.st.smoothedMove.x >= prev && r.pm.st.smoothedMove.x <= 10.0);
            prev = r.pm.st.smoothedMove.x;
        }
        CHECK(deq(r.pm.st.smoothedMove.x, 10.0, 1e-12));  // reached exactly, never above
        CHECK(deq(r.pm.st.desiredSpeedA60, 10.0, 1e-12));
    }
    // Diagonal: a straight-line approach, |r| never exceeds M.
    {
        Rig r;
        r.src.A = 1;
        r.src.B = 1;
        for (int i = 0; i < 12; ++i) {
            r.pm.runFrame(r.frame(1000 + 33 * i));
            CHECK(r.pm.st.desiredSpeedA60 <= 10.0 + 1e-9);
            CHECK(deq(r.pm.st.smoothedMove.x, r.pm.st.smoothedMove.z, 1e-9));
        }
        CHECK(deq(r.pm.st.desiredSpeedA60, 10.0, 1e-9));
    }
    // The M clamp (step 18): r left over from a sprint (15) toward a run D
    // of 10: approach gives 15 - 20*3.5/30 = 12.667, clamped to M = 10.
    {
        Rig r;
        r.src.A = 1;
        r.pm.st.smoothedMove = {15, 0, 0};
        r.pm.runFrame(r.frame(1000));
        CHECK(deq(r.pm.st.desiredSpeedA60, 10.0));
        CHECK(veq(r.pm.st.smoothedMove, {10, 0, 0}));
    }
    // Not supported by ground: a x 0.5 -> step = 10 * 6 / 30 = 2.
    {
        Rig r;
        r.src.A = 1;
        r.pm.world.supportedByGround.set(false);
        r.pm.runFrame(r.frame(1000));
        CHECK(deq(r.pm.st.smoothedMove.x, 2.0));
    }
    // +0xe8 bit 0x20 on the ground: a x 2 -> step = 40 * 6 / 30 = 8.
    {
        Rig r;
        r.src.A = 1;
        r.pm.world.flagE8_20.set(true);
        r.pm.runFrame(r.frame(1000));
        CHECK(deq(r.pm.st.smoothedMove.x, 8.0));
    }
    // Backpedal (+0xd40 bit 0x2000): M x 0.8 = 8 and a x 0.8 = 16:
    // step = 16 * (1 + 4) / 30 = 8/3.
    {
        Rig r;
        r.src.A = 1;
        r.pm.world.movementFlagsD40.set(0x2000u);
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {8, 0, 0}));
        CHECK(deq(r.pm.st.smoothedMove.x, 8.0 / 3.0));
    }
    // Reversal against +0x1cec (step 9): p is zeroed, so the approach
    // starts from 0 instead of braking through the old vector.
    {
        Rig r;
        r.src.A = 1;
        r.pm.st.smoothedMove = {-6, 0, 0};
        r.pm.st.controllerVelocity1cec = {-6, 0, 0};
        r.pm.runFrame(r.frame(1000));
        CHECK(deq(r.pm.st.smoothedMove.x, 4.0));  // from 0, as in the first convergence frame
    }
    // Stopping bit (step 10): D goes to zero with a non-zero p.
    {
        Rig r;
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        r.src.A = 0;
        r.pm.runFrame(r.frame(1033));
        CHECK((r.pm.st.b1d97 & stance::kStopping) != 0);
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 0}));
    }
}

// ===================================================================
// Sec3.6.2 - sprint want/latch/burst, keyboard grace, walk
// ===================================================================
void testSprint() {
    // Standstill burst: sprint held, no stick, speed <= 4.0 -> bit 0x40;
    // the writer turns it into one full-speed step along the facing.
    {
        Rig r;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.src.pressedA[idx(MoveAction::Sprint)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kBurst) == 0);   // consumed by the writer (step 2)
        CHECK((r.pm.st.b1d94 & stance::kSprint) == 0);  // no stamina step on the burst path
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 10}));    // facing (0,0,1) * 1 * run M
        CHECK(r.stamina.sprinting == 0 && r.stamina.start == 0);
        CHECK((r.pm.st.b1d96 & stance::kSprintWanted) != 0);
    }
    // Granted sprint: the stamina step sets bit 0x10 and clears panting;
    // a fresh press applies the start penalty once.
    {
        Rig r;
        r.src.A = 1;
        r.pm.st.b1d95 |= stance::kPanting;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.src.pressedA[idx(MoveAction::Sprint)] = true;
        r.pm.st.speedD1c = 5;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        CHECK((r.pm.st.b1d95 & stance::kPanting) == 0);
        CHECK(r.stamina.sprinting == 1 && r.stamina.start == 1);
        r.src.clearPressed();
        r.pm.runFrame(r.frame(1033));
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        CHECK(r.stamina.sprinting == 2 && r.stamina.start == 1);  // already sprinting, no new press
    }
    // Stamina exhausted: the step runs but the bit stays clear.
    {
        Rig r;
        r.src.A = 1;
        r.stamina.remains = false;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.st.speedD1c = 5;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kSprint) == 0);
        CHECK(r.stamina.sprinting == 1);
    }
    // No stamina available / cool-down running: nothing happens this frame.
    {
        Rig r;
        r.src.A = 1;
        r.stamina.avail = -1;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.st.speedD1c = 5;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & (stance::kSprint | stance::kBurst)) == 0);
        CHECK(r.stamina.sprinting == 0 && r.stamina.notSprinting == 0);
    }
    // Toggle (pad): a press flips the latch, which holds sprint on later
    // frames; a second press clears it.
    {
        Rig r;
        r.src.A = 1;
        r.pm.st.speedD1c = 5;
        r.src.pressedA[idx(MoveAction::SprintToggle)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d96 & stance::kSprintLatch) != 0);
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        r.src.clearPressed();
        r.pm.runFrame(r.frame(1033));
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        r.src.pressedA[idx(MoveAction::SprintToggle)] = true;
        r.pm.runFrame(r.frame(1066));
        CHECK((r.pm.st.b1d96 & stance::kSprintLatch) == 0);
        CHECK((r.pm.st.b1d94 & stance::kSprint) == 0);
    }
    // Walk held cancels the sprint latch and keeps walk.
    {
        Rig r;
        r.pm.st.b1d96 |= stance::kSprintLatch;
        r.src.heldA[idx(MoveAction::Walk)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d96 & stance::kSprintLatch) == 0);
        CHECK((r.pm.st.b1d94 & stance::kWalk) != 0);
        CHECK((r.pm.st.b1d94 & stance::kSprint) == 0);
    }
    // Keyboard idle grace (step 3): sprint survives a release of the
    // movement keys shorter than 0.3 s, and is forgotten after it.
    {
        Rig r;
        r.src.pad = false;
        r.src.kb();
        r.src.A = 1;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.st.speedD1c = 5;
        r.pm.runFrame(r.frame(1000));  // sprinting, keys moving -> grace armed to 1300
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        CHECK(r.pm.g.keyboardSprintGrace.set && r.pm.g.keyboardSprintGrace.expiryMs == 1300);
        r.src.A = 0;  // keys released, sprint key still held
        r.pm.runFrame(r.frame(1100));
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        CHECK(r.pm.g.keyboardSprintGrace.expiryMs == 1300);  // not re-armed while idle
        const int before = r.stamina.notSprinting;
        r.pm.runFrame(r.frame(1350));  // grace expired: all three sprint inputs forgotten
        CHECK((r.pm.st.b1d94 & stance::kSprint) == 0);
        CHECK(r.stamina.notSprinting == before + 1);
    }
    // ... and with the toggle latch the expiry clears the latch itself.
    {
        Rig r;
        r.src.pad = false;
        r.src.kb();
        r.src.A = 1;
        r.pm.st.speedD1c = 5;
        r.src.pressedA[idx(MoveAction::SprintToggle)] = true;
        r.pm.runFrame(r.frame(1000));
        r.src.clearPressed();
        r.src.A = 0;
        r.pm.runFrame(r.frame(1200));
        CHECK((r.pm.st.b1d96 & stance::kSprintLatch) != 0);
        r.pm.runFrame(r.frame(1300));  // remaining 0 = expired
        CHECK((r.pm.st.b1d96 & stance::kSprintLatch) == 0);
    }
    // On a pad the idle test does not apply.
    {
        Rig r;
        r.src.A = 1;
        r.pm.st.speedD1c = 5;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.runFrame(r.frame(1000));
        r.src.A = 0;
        r.pm.runFrame(r.frame(5000));
        CHECK((r.pm.st.b1d94 & stance::kSprint) != 0);
        CHECK(!r.pm.g.keyboardSprintGrace.set);
    }
    // Movement disabled forces want = false (and fails the walk gate).
    {
        Rig r;
        r.pm.world.movementDisabled.set(true);
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.st.speedD1c = 5;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kSprint) == 0);
        CHECK((r.pm.st.b1d96 & stance::kSprintWanted) == 0);
        CHECK(r.stamina.notSprinting == 1);
    }
    // Sprint system switched off (0x0268d2a0): recharge step and return.
    {
        Rig r;
        r.pm.world.sprintSystemEnabled.set(false);
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(r.stamina.notSprinting == 1 && r.stamina.sprinting == 0);
        CHECK((r.pm.st.b1d96 & stance::kSprintWanted) == 0);
    }
    // Want + scope view: sprint leaves scope view; otherwise stands a
    // crouched player up; fine aim is always left.
    {
        Rig r;
        r.pm.st.b1d95 |= stance::kScopeView;
        r.pm.st.fineAim = true;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d95 & stance::kScopeView) == 0);
        CHECK(!r.pm.st.fineAim);
    }
    // Jump penalty (step 7).
    {
        Rig r;
        r.pm.world.jumping.set(true);
        MoveFrameInput f = r.frame(1000);
        f.jumpedThisFrame = true;
        r.pm.runFrame(f);
        CHECK(r.stamina.jump == 1);
    }
    // Walk toggle: the latch holds bit 0x20 on later frames.
    {
        Rig r;
        r.src.pressedA[idx(MoveAction::WalkToggle)] = true;
        r.pm.runFrame(r.frame(1000));
        r.src.clearPressed();
        r.pm.runFrame(r.frame(1033));
        CHECK((r.pm.st.b1d96 & stance::kWalkLatch) != 0);
        CHECK((r.pm.st.b1d94 & stance::kWalk) != 0);
    }
}

// ===================================================================
// Sec3.6.3 - crouch hold vs toggle
// ===================================================================
void testCrouch() {
    // Hold mode on keyboard: crouched while held, consumed, stands on release.
    {
        Rig r;
        r.src.pad = false;
        r.src.kb();
        r.src.heldA[idx(MoveAction::Crouch)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(r.pm.st.crouched);
        CHECK((r.pm.st.b1d96 & stance::kCrouchLatch) == 0);
        CHECK(r.src.consumed.size() == 1 && r.src.consumed[0] == MoveAction::Crouch);
        r.src.heldA[idx(MoveAction::Crouch)] = false;
        r.pm.runFrame(r.frame(1033));
        CHECK(!r.pm.st.crouched);
    }
    // Crouched stance speed: M = Player_crouch_move_speed.
    {
        Rig r;
        r.src.heldA[idx(MoveAction::Crouch)] = true;
        r.src.opt = false;  // pad with option bit 0 clear -> hold mode
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        CHECK(r.pm.st.crouched);
        CHECK(veq(r.pm.st.desiredMove, {4, 0, 0}));
    }
    // Toggle mode on a pad (option bit set): a press flips the latch; the
    // player stays crouched with the button up; a second press stands.
    {
        Rig r;
        r.src.heldA[idx(MoveAction::Crouch)] = true;
        r.src.pressedA[idx(MoveAction::Crouch)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(r.pm.st.crouched && (r.pm.st.b1d96 & stance::kCrouchLatch) != 0);
        CHECK(r.src.consumed.size() == 2);  // both crouch actions consumed
        r.src.clearPressed();
        r.src.heldA[idx(MoveAction::Crouch)] = false;
        r.pm.runFrame(r.frame(1033));
        CHECK(r.pm.st.crouched);
        r.src.pressedA[idx(MoveAction::Crouch)] = true;
        r.pm.runFrame(r.frame(1066));
        CHECK(!r.pm.st.crouched && (r.pm.st.b1d96 & stance::kCrouchLatch) == 0);
    }
    // Toggle on keyboard is CBA_OFC_CROUCH_TOGGLE.
    {
        Rig r;
        r.src.pad = false;
        r.src.kb();
        r.src.pressedA[idx(MoveAction::CrouchToggle)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(r.pm.st.crouched);
    }
    // The stick test, normally bypassed by 0x012e6ced = 1: with the byte 0
    // a deflection >= Crouch_click_sensitivity refuses the toggle, and the
    // latch re-syncs to the (standing) state.
    {
        Rig r;
        r.pm.world.crouchStickTestBypass = 0;
        r.src.A = 1;  // A^2+B^2 = 1 >= 0.5
        r.src.pressedA[idx(MoveAction::Crouch)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(!r.pm.st.crouched && (r.pm.st.b1d96 & stance::kCrouchLatch) == 0);
        r.src.A = 0.5f;  // 0.25 < 0.5
        r.pm.runFrame(r.frame(1033));
        CHECK(r.pm.st.crouched);
    }
    // Sprint held: a crouched player stands up.
    {
        Rig r;
        r.pm.st.crouched = true;
        r.pm.st.b1d96 |= stance::kCrouchLatch;
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(!r.pm.st.crouched);
    }
    // Crouch impossible: hold mode does nothing.
    {
        Rig r;
        r.pm.world.crouchPossible.set(false);
        r.src.opt = false;
        r.src.heldA[idx(MoveAction::Crouch)] = true;
        r.pm.runFrame(r.frame(1000));
        CHECK(!r.pm.st.crouched);
    }
}

// ===================================================================
// Sec5.4.3/Sec5.4.4 - the velocity composer
// ===================================================================
void testComposer() {
    // Not sprinting: +0x1cec = +0xa50 * +0xa60 = k.r.
    {
        Rig r;
        r.pm.st.facingA50 = {0.6, 0, 0.8};
        r.pm.st.desiredSpeedA60 = 5;
        r.pm.st.locomotionState = kLocoRun;
        r.pm.composeVelocity(r.frame(1000));
        CHECK(veq(r.pm.st.controllerVelocity1cec, {3, 0, 4}));
        // + R.E/dt: E = 0.3 along "up" (local y), dt = 0.1 -> (0, 3, 0).
        MoveFrameInput f = r.frame(1000, 0.1);
        f.animationTranslationE = {0, 0.3, 0};
        r.pm.composeVelocity(f);
        CHECK(veq(r.pm.st.controllerVelocity1cec, {3, 3, 4}));
        // E is rotated by the orientation rows: local forward -> world +x.
        r.pm.world.orientation.set(Orientation{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}});
        f.animationTranslationE = {0, 0, 0.2};
        r.pm.composeVelocity(f);
        CHECK(veq(r.pm.st.controllerVelocity1cec, {5, 0, 4}));
        // Controller flag 4 zeroes E on the player branch.
        r.pm.world.controllerFlag4.set(true);
        r.pm.composeVelocity(f);
        CHECK(veq(r.pm.st.controllerVelocity1cec, {3, 0, 4}));
    }
    // Sprinting (+0xccc = 3): k|r| along the facing axis +0x64, not unit(r).
    {
        Rig r;
        r.pm.world.orientation.set(Orientation{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}});
        r.pm.st.facingA50 = {0.6, 0, 0.8};
        r.pm.st.desiredSpeedA60 = 5;
        r.pm.st.locomotionState = kLocoSprint;
        r.pm.composeVelocity(r.frame(1000));
        CHECK(veq(r.pm.st.controllerVelocity1cec, {5, 0, 0}));
        // Seated: 0x00941e60 is false -> back to +0xa50.
        r.pm.world.riding.set(true);
        r.pm.composeVelocity(r.frame(1000));
        CHECK(veq(r.pm.st.controllerVelocity1cec, {3, 0, 4}));
    }
    // r = 0: only R.E/dt remains.
    {
        Rig r;
        MoveFrameInput f = r.frame(1000, 0.5);
        f.animationTranslationE = {1, 0, 0};
        r.pm.composeVelocity(f);
        CHECK(veq(r.pm.st.controllerVelocity1cec, {2, 0, 0}));
    }
    // 0x009573d0: v = 0 (E still added).
    {
        Rig r;
        r.pm.st.facingA50 = {1, 0, 0};
        r.pm.st.desiredSpeedA60 = 5;
        r.pm.world.accelShaping9573d0.set(true);
        r.pm.composeVelocity(r.frame(1000));
        CHECK(veq(r.pm.st.controllerVelocity1cec, {0, 0, 0}));
    }
    // Upgraded jump: E x 1.5 with all conditions and CBA_OFC_SPRINT held.
    {
        Rig r;
        r.pm.world.sprintUpgrade.set(true);
        r.pm.world.jumpUpgrade.set(true);
        r.pm.world.supportedByGround.set(false);
        r.pm.world.jumping.set(true);
        r.src.heldA[idx(MoveAction::Sprint)] = true;
        MoveFrameInput f = r.frame(1000, 0.5);
        f.animationTranslationE = {0, 1, 0};
        r.pm.composeVelocity(f);
        CHECK(veq(r.pm.st.controllerVelocity1cec, {0, 3, 0}));
    }
    // OPEN paths throw rather than invent.
    {
        Rig r;
        r.pm.world.controllerFlag100.set(true);
        CHECK(throwsOpen([&] { r.pm.composeVelocity(r.frame(1000)); }, "root-motion"));
    }
    {
        Rig r;
        r.pm.world.ccd.set(2);
        CHECK(throwsOpen([&] { r.pm.composeVelocity(r.frame(1000)); }, "0x009918b0"));
    }
    {
        Rig r;
        r.pm.st.desiredSpeedA60 = -1;
        CHECK(throwsOpen([&] { r.pm.composeVelocity(r.frame(1000)); }, "sentinel"));
    }
    // The full closed form with k = +0xa64 = 0.5: frame 1 r = (4,0,0),
    // +0xa60 = k|r| = 2, +0x1cec = k.r = (2,0,0).
    {
        Rig r;
        r.pm.st.speedScaleA64 = 0.5;
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.smoothedMove, {4, 0, 0}));
        CHECK(deq(r.pm.st.desiredSpeedA60, 2.0));
        CHECK(veq(r.pm.st.controllerVelocity1cec, {2, 0, 0}));
    }
}

// ===================================================================
// Sec5.5 - the locomotion-state update and the sprint ramp
// ===================================================================
void testLocomotionState() {
    // Run / walk / fine aim / cover / sprint / stopped.
    {
        Rig r;
        r.pm.st.speedD1c = 3;
        r.pm.updateLocomotionState(r.frame(1000), false, false);
        CHECK(r.pm.st.locomotionState == kLocoRun);
        CHECK(r.pm.st.leaveIdleAa0.set && r.pm.st.leaveIdleAa0.expiryMs == 1500);
        r.pm.st.speedD1c = 2.5;  // not > 2.5
        r.pm.updateLocomotionState(r.frame(1033), false, false);
        CHECK(r.pm.st.locomotionState == kLocoWalk);
        r.pm.st.speedD1c = 3;
        r.pm.st.fineAim = true;  // a run in fine aim becomes 1
        r.pm.updateLocomotionState(r.frame(1066), false, false);
        CHECK(r.pm.st.locomotionState == kLocoWalk);
        r.pm.st.fineAim = false;
        r.pm.updateLocomotionState(r.frame(1100), true, false);
        CHECK(r.pm.st.locomotionState == kLocoSprint);
        CHECK(r.pm.st.sprintStart2054.set && r.pm.st.sprintStart2054.expiryMs == 1850);
    }
    // The state-0 transition requires `stopped` AND able to move on the ground.
    {
        Rig r;
        r.pm.st.locomotionState = kLocoRun;
        r.pm.world.canMoveOnGround.set(false);
        r.pm.updateLocomotionState(r.frame(1000), false, true);
        CHECK(r.pm.st.locomotionState == kLocoWalk);
        r.pm.world.canMoveOnGround.set(true);
        r.pm.updateLocomotionState(r.frame(1033), false, true);
        CHECK(r.pm.st.locomotionState == kLocoIdle);
        r.pm.updateLocomotionState(r.frame(1066), false, false);
        CHECK(r.pm.st.locomotionState == kLocoWalk);  // speed 0 -> walk, not run
    }
    // The +0xa64 rescale of a positive +0xa60.
    {
        Rig r;
        r.pm.st.speedScaleA64 = 0.5;
        r.pm.st.desiredSpeedA60 = 4;
        r.pm.updateLocomotionState(r.frame(1000), false, false);
        CHECK(deq(r.pm.st.desiredSpeedA60, 2));
        r.pm.st.desiredSpeedA60 = 0;
        r.pm.updateLocomotionState(r.frame(1033), false, false);
        CHECK(r.pm.st.desiredSpeedA60 == 0);
    }
    // The sprint ramp: 300*min(1, (8-5)/3) = 300 ms, re-armed while held and
    // once more when the request ends; the tail keeps state 3 for 300 ms.
    {
        Rig r;
        r.pm.st.locomotionState = kLocoSprint;
        r.pm.st.speedD1c = 8;
        r.pm.updateLocomotionState(r.frame(1000), true, false);
        CHECK(r.pm.st.sprintRamp2058.expiryMs == 1300);
        r.pm.updateLocomotionState(r.frame(1100), false, false);  // request ends
        CHECK(r.pm.st.sprintRamp2058.expiryMs == 1400);
        CHECK(r.pm.st.locomotionState == kLocoSprint);
        r.pm.updateLocomotionState(r.frame(1399), false, false);
        CHECK(r.pm.st.locomotionState == kLocoSprint);
        CHECK(r.pm.st.sprintRamp2058.expiryMs == 1400);  // not re-armed after the end frame
        r.pm.updateLocomotionState(r.frame(1400), false, false);
        CHECK(r.pm.st.locomotionState == kLocoRun);
        CHECK(!r.pm.st.sprintRamp2058.set);  // unset on expiry
    }
    // Partial ramp: speed 6.5 -> 150 ms.
    {
        Rig r;
        r.pm.st.locomotionState = kLocoSprint;
        r.pm.st.speedD1c = 6.5;
        r.pm.updateLocomotionState(r.frame(1000), true, false);
        CHECK(r.pm.st.sprintRamp2058.expiryMs == 1150);
    }
    // Speed <= 5: the arm is non-positive (already expired) -> no tail.
    {
        Rig r;
        r.pm.st.locomotionState = kLocoSprint;
        r.pm.st.speedD1c = 5;
        r.pm.updateLocomotionState(r.frame(1000), true, false);
        r.pm.updateLocomotionState(r.frame(1033), false, false);
        CHECK(r.pm.st.locomotionState == kLocoRun);  // no sprint tail: straight to run (5 > 2.5)
        CHECK(!r.pm.st.sprintRamp2058.set);
    }
    // A running ramp keeps the request FALSE for a non-sprinting human.
    {
        Rig r;
        r.pm.st.locomotionState = kLocoRun;
        r.pm.st.speedD1c = 3;
        r.pm.st.sprintRamp2058.arm(1000, 200);
        r.pm.updateLocomotionState(r.frame(1050), true, false);
        CHECK(r.pm.st.locomotionState == kLocoRun);
        r.pm.updateLocomotionState(r.frame(1200), true, false);  // expired -> request passes
        CHECK(r.pm.st.locomotionState == kLocoSprint);
    }
    // Start-to-move state 0x2f9 forces 0 until its progress exceeds 0.55,
    // unless a run start with "sprint wanted" ends it.
    {
        Rig r;
        r.pm.st.speedD1c = 3;
        r.pm.world.startMoveState2f9Active.set(true);
        r.pm.world.startMoveState2f9Progress.set(0.3);
        r.pm.updateLocomotionState(r.frame(1000), false, false);
        CHECK(r.pm.st.locomotionState == kLocoIdle);
        r.pm.st.b1d96 |= stance::kSprintWanted;
        r.pm.updateLocomotionState(r.frame(1033), false, false);
        CHECK(r.pm.st.locomotionState == kLocoRun);
        r.pm.st.b1d96 = 0;
        r.pm.world.startMoveState2f9Progress.set(0.6);
        r.pm.updateLocomotionState(r.frame(1066), false, false);
        CHECK(r.pm.st.locomotionState == kLocoRun);
    }
    // Climbing/vaulting: returns at once (no "player gave input").
    {
        Rig r;
        r.pm.world.climbingOrVaulting.set(true);
        r.pm.st.speedD1c = 3;
        r.pm.updateLocomotionState(r.frame(1000), true, false);
        CHECK(r.pm.st.locomotionState == kLocoIdle && r.pm.playerGaveInputCalls == 0);
    }
    // The ramp blend of M (Sec3.5.4 step 5): 150 of 300 ms left ->
    // M = 0.5*10 + 0.5*15 = 12.5, M0 = 10; with the upgrade S = 30.
    {
        Rig r;
        r.pm.st.sprintRamp2058.arm(1000, 150);
        double M = 0, M0 = 0;
        r.pm.stanceMaxSpeed(1000, M, M0);
        CHECK(deq(M, 12.5) && deq(M0, 10));
        r.pm.world.sprintUpgrade.set(true);
        r.pm.stanceMaxSpeed(1000, M, M0);
        CHECK(deq(M, 20.0));
        r.pm.stanceMaxSpeed(1150, M, M0);  // no time left -> M = M0
        CHECK(deq(M, 10.0));
    }
    // Stance table first match: heavy weapon beats everything.
    {
        Rig r;
        r.pm.world.heavyWeapon.set(true);
        r.pm.tweaks.maxHeavyWeaponSpeed.set(6);
        r.pm.st.crouched = true;
        double M = 0, M0 = 0;
        r.pm.stanceMaxSpeed(0, M, M0);
        CHECK(deq(M, 6));
        // Fine aim beats crouch; the unseeded tweak is never read.
        r.pm.world.heavyWeapon.set(false);
        r.pm.st.fineAim = true;
        r.pm.stanceMaxSpeed(0, M, M0);
        CHECK(deq(M, 3));
    }
}

// ===================================================================
// Sec5.1 / Sec5.9 - frame ordering and the direct integrator
// ===================================================================
void testFrameOrderAndIntegrator() {
    // One frame: each phase exactly once, in order; every input read happens
    // inside the controls phases (the bridge never re-reads intent).
    {
        Rig r;
        r.pm.traceEnabled = true;
        r.src.pad = false;  // keyboard, so the sprint idle test reads the axes too
        r.src.kb();
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        const std::vector<FramePhase> want = {FramePhase::OnFootActions, FramePhase::MoveSlot, FramePhase::Compose,
                                              FramePhase::Integrate, FramePhase::Publish};
        CHECK(r.pm.trace == want);
        CHECK(!r.src.readPhases.empty());
        for (FramePhase p : r.src.readPhases) CHECK(p == FramePhase::OnFootActions || p == FramePhase::MoveSlot);
        // Integrate once with this frame's composed velocity; publish it.
        const Vec3 v = r.pm.st.controllerVelocity1cec;
        CHECK(veq(v, {4, 0, 0}));
        CHECK(veq(r.pm.st.capsulePosition, {4.0 / 30.0, 0, 0}));
        CHECK(veq(r.pm.st.position, r.pm.st.capsulePosition));
        CHECK(veq(r.pm.st.velocityD04, v));
        CHECK(deq(r.pm.st.speedD1c, 4.0));
        // Frame 2 integrates frame 2's velocity (one step each frame).
        r.pm.runFrame(r.frame(1033));
        CHECK(veq(r.pm.st.capsulePosition, {(4.0 + 20.0 / 3.0) / 30.0, 0, 0}));
    }
    // A suppressed movement post-hook (QTE / fired prompt): no move slot,
    // the intent is stale, the bridge still composes and steps it.
    {
        Rig r;
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        r.pm.traceEnabled = true;
        r.src.A = 0;
        MoveFrameInput f = r.frame(1033);
        f.controlsRanThisFrame = false;
        r.pm.runFrame(f);
        const std::vector<FramePhase> want = {FramePhase::OnFootActions, FramePhase::Compose, FramePhase::Integrate,
                                              FramePhase::Publish};
        CHECK(r.pm.trace == want);
        CHECK(veq(r.pm.st.smoothedMove, {4, 0, 0}));
        CHECK(veq(r.pm.st.controllerVelocity1cec, {4, 0, 0}));
    }
    // dt == 0: 0x0099e7e0 does nothing - no step, no write-back.
    {
        Rig r;
        r.pm.st.facingA50 = {1, 0, 0};
        r.pm.st.desiredSpeedA60 = 3;
        r.pm.st.velocityD04 = {9, 9, 9};
        const MoveFrameInput f = r.frame(1000, 0.0);
        r.pm.composeVelocity(f);
        r.pm.integrate(f);
        r.pm.publish();
        CHECK(!r.pm.capsuleStepped);
        CHECK(veq(r.pm.st.velocityD04, {9, 9, 9}));
        CHECK(veq(r.pm.st.capsulePosition, {0, 0, 0}));
    }
    // Air control (Sec5.6.3 (e)): airborne player velocity x clamp(|A|+|B|, 0, 1).
    {
        Rig r;
        r.pm.world.supportedByGround.set(false);
        r.src.A = 0.3f;
        r.src.B = 0.2f;
        r.pm.runFrame(r.frame(1000));
        const Vec3 v = r.pm.st.controllerVelocity1cec;
        CHECK(veq(r.pm.st.velocityD04, v * 0.5));
        // Free fall: no scaling.
        Rig q;
        q.pm.world.supportedByGround.set(false);
        q.pm.world.freeFall.set(true);
        q.src.A = 0.3f;
        q.pm.runFrame(q.frame(1000));
        CHECK(veq(q.pm.st.velocityD04, q.pm.st.controllerVelocity1cec));
    }
    // Position sync (Sec5.7): the body follows the capsule only when it is
    // more than 0.0008 away on some axis.
    {
        Rig r;
        r.pm.st.facingA50 = {1, 0, 0};
        r.pm.st.desiredSpeedA60 = 0.01;  // 0.01/30 = 0.000333 per frame
        for (int i = 1; i <= 3; ++i) {
            const MoveFrameInput f = r.frame(1000 + 33 * i);
            r.pm.composeVelocity(f);
            r.pm.integrate(f);
            r.pm.publish();
            if (i < 3) CHECK(r.pm.st.position.x == 0);
        }
        CHECK(deq(r.pm.st.position.x, 0.001, 1e-12));
    }
    // Controls disabled (Sec2.7 gate): writer with own position, no
    // action dispatch, no move slot.
    {
        Rig r;
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        r.pm.world.controlsDisabled.set(true);
        r.pm.traceEnabled = true;
        r.pm.runFrame(r.frame(1033));
        const std::vector<FramePhase> want = {FramePhase::NoIntent, FramePhase::Compose, FramePhase::Integrate,
                                              FramePhase::Publish};
        CHECK(r.pm.trace == want);
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 0}));
        // r brakes toward 0: p = 4, delta = 4, step = 20*3/30 = 2.
        CHECK(veq(r.pm.st.smoothedMove, {2, 0, 0}));
    }
    // Locked rows 0/1: no intent, stopped -> state 0, recharge step.
    {
        Rig r;
        r.pm.st.locomotionState = kLocoRun;
        r.pm.runLockedFrame(r.frame(1000));
        CHECK(r.pm.st.locomotionState == kLocoIdle);
        CHECK(r.stamina.notSprinting == 1);
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 0}));
    }
}

// ===================================================================
// Gates, the CharacterState binding and OPEN discipline
// ===================================================================
void testGatesAndOpen() {
    // +0x1d99 bound to sr3luahost::CharacterState (player_movement_disable).
    {
        Rig r;
        sr3luahost::CharacterState cs;
        cs.controlsDisabled.set(false);
        cs.movementDisabled.set(true);
        r.pm.world.bindDisableFlags(cs.controlsDisabled, cs.movementDisabled);
        r.pm.world.movementDisabled.set(false);  // the own fallback is ignored once bound
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 0}));  // hard block: A = B = 0
        CHECK(r.pm.g.stoppedLatch);
        CHECK(r.pm.g.publishedA == 0);  // publication skipped on the block path
        cs.movementDisabled.set(false);
        r.pm.runFrame(r.frame(1033));
        CHECK(veq(r.pm.st.desiredMove, {10, 0, 0}));
        cs.controlsDisabled.set(true);  // player_controls_disable -> the no-intent gate
        r.pm.runFrame(r.frame(1066));
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 0}));
        cs.controlsDisabled.forget();
        CHECK(throwsOpen([&] { r.pm.runFrame(r.frame(1100)); }, "+0x1d99"));
    }
    // Stale-intent gate (Sec3.5.1 step 4): AI mode Idle keeps last frame's intent.
    {
        Rig r;
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        const uint64_t calls = r.pm.playerGaveInputCalls;
        r.pm.world.aiMode501.set(0);
        r.src.A = -1;
        r.pm.runFrame(r.frame(1033));
        CHECK(veq(r.pm.st.desiredMove, {10, 0, 0}));
        CHECK(veq(r.pm.st.smoothedMove, {4, 0, 0}));
        CHECK(r.pm.playerGaveInputCalls == calls);  // nothing below the gate ran
        r.pm.world.aiMode501.set(43);
        r.pm.world.aiSubState500.set(0x8b);  // `exiting node`
        r.pm.runFrame(r.frame(1066));
        CHECK(veq(r.pm.st.desiredMove, {10, 0, 0}));
    }
    // Release detector (Sec3.5.2 step 6): a snapped stick is "stopped" one
    // frame later than an eased one.
    {
        Rig r;
        r.src.A = 1;
        r.pm.readAxes(r.frame(1000));
        r.src.A = 0;
        auto ax = r.pm.readAxes(r.frame(1033));  // energy (1*30)^2 = 900 >= 1
        CHECK(ax.centred && !ax.stopped);
        ax = r.pm.readAxes(r.frame(1066));
        CHECK(ax.stopped);
        Rig e;
        e.src.A = 0.02f;
        e.pm.readAxes(e.frame(1000));
        e.src.A = 0;
        ax = e.pm.readAxes(e.frame(1033));  // energy (0.02*30)^2 = 0.36 < 1
        CHECK(ax.centred && ax.stopped);
    }
    // Cover (Sec3.5.2 step 5): dominant axis only, a tie keeps A.
    {
        Rig r;
        r.pm.world.inCover.set(true);
        r.pm.world.coverMode18d4.set(1);
        r.src.A = 0.3f;
        r.src.B = 0.6f;
        auto ax = r.pm.readAxes(r.frame(1000));
        CHECK(ax.A == 0 && ax.B == 0.6f);
        r.src.A = 0.5f;
        r.src.B = 0.5f;
        ax = r.pm.readAxes(r.frame(1033));
        CHECK(ax.A == 0.5f && ax.B == 0);
        // Cover with +0x18d4 != 1 is a hard block.
        r.pm.world.coverMode18d4.set(0);
        ax = r.pm.readAxes(r.frame(1066));
        CHECK(ax.stopped && ax.A == 0 && ax.B == 0);
    }
    // Mission-16 inversion negates both axes.
    {
        Rig r;
        r.pm.world.mission16InvertActive.set(true);
        r.src.A = 0.5f;
        r.pm.runFrame(r.frame(1000));
        CHECK(veq(r.pm.st.desiredMove, {-2.5, 0, 0}));
    }
    // Animation lock (Sec3.5.4 step 3): bit 0x80, no intent.
    {
        Rig r;
        r.pm.world.controllerFlag10.set(true);
        r.src.A = 1;
        r.pm.runFrame(r.frame(1000));
        CHECK((r.pm.st.b1d94 & stance::kMoveWhileLocked) != 0);
        CHECK(veq(r.pm.st.desiredMove, {0, 0, 0}));
    }
    // OPEN discipline: subsystems with no model throw instead of guessing.
    {
        Rig r;
        r.pm.world.drunkEffectInactive.set(false);
        r.src.A = 1;
        CHECK(throwsOpen([&] { r.pm.runFrame(r.frame(1000)); }, "0x009758b0"));
    }
    {
        Rig r;
        r.pm.tweaks.maxRunSpeed.forget();
        r.src.A = 1;
        CHECK(throwsOpen([&] { r.pm.runFrame(r.frame(1000)); }, "Player_max_run_speed"));
    }
    {
        ScriptedSource src;
        OpenSprintStamina open;
        PlayerMovement pm(src, open);
        SeedOnFootCommonCase(pm.world);
        pm.world.controlsDisabled.set(false);
        pm.world.movementDisabled.set(false);
        pm.world.permissions2078.set(0x6000);
        CHECK(throwsOpen([&] { pm.sprintRoutine(MoveFrameInput{}); }, "0x009fef60"));
    }
    {
        // A fresh world is entirely OPEN: the first read throws.
        ScriptedSource src;
        FakeStamina st;
        PlayerMovement pm(src, st);
        CHECK(throwsOpen([&] { pm.crouchRoutine(MoveFrameInput{}); }, "+0x2078"));
    }
    // Tweak seeding from tweak_table rows: case-insensitive, later rows win.
    {
        MoveTweaks t;
        const int n = t.seedFromTweakRows(
            {{"player_MAX_run_speed", 7}, {"Player_acceleration", 3}, {"Unrelated_name", 1}, {"Player_max_run_speed", 8}});
        CHECK(n == 3);
        CHECK(t.maxRunSpeed.get() == 8 && t.acceleration.get() == 3);
        CHECK(!t.maxSprintSpeed.known());
    }
}

// ===================================================================
// tweak_table.xtbl -> MoveTweaks through the EXISTING reader
// (spec-tables-progression.md Sec10.10 / Sec1.4). Synthetic XML rows.
// ===================================================================
void testTweakTableSeeding() {
    const sr3xtbl::Document base = sr3xtbl::ParseDocument(
        "<root><Table>"
        "<Tweak_Table_Entry><Name>PLAYER_MAX_RUN_SPEED</Name><Value>9.5</Value></Tweak_Table_Entry>"
        "<Tweak_Table_Entry><Name>Player_acceleration</Name><Value>18</Value><Framework>MAIN</Framework></Tweak_Table_Entry>"
        "<Tweak_Table_Entry><Name>Player_max_sprint_speed</Name><Value>14</Value><Framework>dlc1</Framework></Tweak_Table_Entry>"
        "<Tweak_Table_Entry><Name>Player_crouch_move_speed</Name></Tweak_Table_Entry>"
        "<Tweak_Table_Entry><Name>Genki_ball_bounce_timer_ms</Name><Value>3000</Value></Tweak_Table_Entry>"
        "</Table></root>");
    const auto rows = sr3tables_progression::ParseTweakTable(base);
    MoveTweaks t;
    // Base load ("main"): the dlc1 row is filtered out, the Value-less row
    // is skipped, the unrelated tunable is ignored.
    CHECK(SeedMoveTweaksFromTweakTable(t, rows) == 2);
    CHECK(deq(t.maxRunSpeed.get(), 9.5) && deq(t.acceleration.get(), 18));
    CHECK(!t.maxSprintSpeed.known() && !t.crouchMoveSpeed.known());
    // The dlc1 overlay load accepts exactly its own row, later wins.
    CHECK(SeedMoveTweaksFromTweakTable(t, rows, "dlc1") == 1);
    CHECK(deq(t.maxSprintSpeed.get(), 14));
    // A tunable no row names stays OPEN - no compiled-in default is invented.
    CHECK(throwsOpen([&] { t.diveRollAcceleration.get(); }, "Player_dive_roll_acceleration"));
}

// ===================================================================
// The real U0 adapter (InputSystemMoveSource)
// ===================================================================
void testInputSystemAdapter() {
    constexpr int kScanD = 0x20, kScanW = 0x11, kScanShift = 0x2a;
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    InputSystemMoveSource src(in);
    // Unpublished action indices and the option byte are OPEN.
    CHECK(throwsOpen([&] { src.held(MoveAction::WalkToggle, nullptr); }, "CBA_OFC_WALK_TOGGLE"));
    CHECK(throwsOpen([&] { src.pressed(MoveAction::CrouchToggle, nullptr); }, "CBA_OFC_CROUCH_TOGGLE"));
    CHECK(throwsOpen([&] { src.optionBit0(); }, "0x01411b00"));
    in.optionFlagBits0to3 = static_cast<uint8_t>(1);
    CHECK(src.optionBit0());

    // CAA 3 (turn left/right) and CAA 2 (forward/back) through 0x005bb530;
    // CBA_OFC_SPRINT (0x12) through the held query.
    in.axisBindings()[kCaaWalkTurnLeftRight].keyPos = kScanD;
    in.axisBindings()[kCaaWalkForwardBackward].keyPos = kScanW;
    in.buttonBindings()[kCbaOfcSprint].key1 = kScanShift;
    dev.keyDown[kScanD] = true;
    dev.keyDown[kScanShift] = true;
    in.latchFrame();
    CHECK(src.walkAxisA() == 1.0f && src.walkAxisB() == 0.0f);
    int out = -9;
    CHECK(src.held(MoveAction::Sprint, &out) && out == kOutKeyOrMouse);

    // End to end: the real input layer drives the closed form. The two
    // toggle indices are seeded with indices that have no binding here (a
    // TEST choice - the real values are unpublished).
    src.walkToggleCba.set(0x15);
    src.crouchToggleCba.set(0x1a);
    dev.keyDown[kScanShift] = false;
    in.latchFrame();
    FakeStamina stamina;
    PlayerMovement pm(src, stamina);
    SeedOnFootCommonCase(pm.world);
    pm.world.controlsDisabled.set(false);
    pm.world.movementDisabled.set(false);
    pm.world.permissions2078.set(0x6000);
    pm.world.orientation.set(kIdentity);
    pm.tweaks.maxRunSpeed.set(10);
    pm.tweaks.maxSprintSpeed.set(15);
    pm.tweaks.acceleration.set(20);
    MoveFrameInput f;
    f.nowMs = 1000;
    f.dt = f.axisDt = 1.0 / 30.0;
    f.camera.row0 = {1, 0, 0};
    f.camera.row2 = {0, 0, 1};
    pm.runFrame(f);
    CHECK(veq(pm.st.desiredMove, {10, 0, 0}));  // keyboard, A = 1 -> s = 1
    CHECK(veq(pm.st.velocityD04, {4, 0, 0}));
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
    run("testClosedForm", testClosedForm);
    run("testApproach", testApproach);
    run("testSprint", testSprint);
    run("testCrouch", testCrouch);
    run("testComposer", testComposer);
    run("testLocomotionState", testLocomotionState);
    run("testFrameOrderAndIntegrator", testFrameOrderAndIntegrator);
    run("testGatesAndOpen", testGatesAndOpen);
    run("testTweakTableSeeding", testTweakTableSeeding);
    run("testInputSystemAdapter", testInputSystemAdapter);
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_player_movement_test: all checks passed\n";
    return 0;
}
