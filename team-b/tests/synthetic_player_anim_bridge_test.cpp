// Synthetic tests for sr3playeranim: the player<->animation coupling ("a
// character walks"). Every expected value is either computed from a rule
// quoted from spec-player-runtime.md/spec-anim-runtime.md (cited per block)
// or, for the playback-rate formula, independently re-derived step by step
// in the test itself (not just compared against the production code's own
// output) so the two serve as a cross-check. Inputs (tweak values, the
// synthetic registries/graphs, the fake root-motion rates) are SYNTHETIC
// test numbers chosen for easy arithmetic - NOT engine values.
#include "sr3playeranim/bridge.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3animruntime/node_graph.h"
#include "sr3animruntime/node_graph_runtime.h"
#include "sr3luahost/open_state.h"
#include "sr3player/camera.h"
#include "sr3player/movement.h"
#include "sr3xtbl/xtbl.h"

using namespace sr3playeranim;
using namespace sr3animruntime;
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
        if (needle != nullptr && e.global().find(needle) == std::string::npos) {
            std::cerr << "  (OPEN thrown but named: " << e.global() << ")\n";
            return false;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Minimal fakes (same shape as tests/synthetic_player_movement_test.cpp /
// synthetic_player_camera_test.cpp's own ScriptedSource/ScriptedLook).
// ---------------------------------------------------------------------------
struct DummyMoveSource : MoveInputSource {
    float walkAxisA() override { return 0; }
    float walkAxisB() override { return 0; }
    bool gamepadActive() override { return true; }
    bool held(MoveAction, int*) override { return false; }
    bool pressed(MoveAction, int*) override { return false; }
    void consume(MoveAction) override {}
    bool optionBit0() override { return true; }
};
struct DummyStamina : SprintStamina {
    void notSprintingStep(PlayerMovementState&) override {}
    bool sprintingStep(PlayerMovementState&) override { return true; }
    double available() override { return 1.0; }
    void startPenalty() override {}
    void jumpPenalty() override {}
    void step9ff270() override {}
};
// Forward-held scripted source, for the multi-frame graph-driving test:
// gamepad held straight forward (B axis = 1) the whole run.
struct ForwardSource : MoveInputSource {
    float walkAxisA() override { return 0; }
    float walkAxisB() override { return 1.0f; }
    bool gamepadActive() override { return true; }
    bool held(MoveAction, int*) override { return false; }
    bool pressed(MoveAction, int*) override { return false; }
    void consume(MoveAction) override {}
    bool optionBit0() override { return true; }
};
struct ScriptedLook : LookInputSource {
    float rotate() override { return 0; }
    float elevate() override { return 0; }
    bool cameraDrivenByKbm() override { return false; }
    bool invertHorizontal() override { return false; }
    bool invertVertical() override { return false; }
};

const Orientation kIdentity{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // right=+x, up=+y, forward=+z

MoveFrameInput frameAt(int64_t nowMs, double dt = 1.0 / 30.0) {
    MoveFrameInput f;
    f.nowMs = nowMs;
    f.dt = dt;
    f.axisDt = 1.0 / 30.0;
    f.camera.row0 = {1, 0, 0};  // Rt
    f.camera.row2 = {0, 0, 1};  // F
    return f;
}

// ===========================================================================
// 1. The field -> control-parameter writer (spec-anim-runtime.md Sec4.2)
// ===========================================================================
void testParameterWriter() {
    const char* kParamsXml = R"(<root><control_parameters>
<control_parameter><name>desired speed</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>Vertical Speed</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>move forward bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>move left bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>move right bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>move back bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>backpedal bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>alternate strafe bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>walk bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>run bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>sprint bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>crouch bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>fine aim bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>current speed</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>direction</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>Speed Along rvec</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>turn angle</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>dead</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>combat ready</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>Supported By Ground</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>locomotion</name><data_type>integer</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>Weapon Sprint Bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>not aliased float</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
</control_parameters></root>)";
    const ControlParameterRegistry registry = ControlParameterRegistry::loadXml(kParamsXml);
    CHECK(registry.loaded() && registry.count() == 23);

    const std::vector<std::string> aliasedNames = {
        "desired speed", "Vertical Speed", "move forward bool", "move left bool", "move right bool",
        "move back bool", "backpedal bool", "alternate strafe bool", "walk bool", "run bool", "sprint bool",
        "crouch bool", "fine aim bool", "current speed", "direction", "Speed Along rvec", "turn angle", "dead",
        "combat ready", "Supported By Ground", "locomotion", "Weapon Sprint Bool",
        // "not aliased float" is deliberately NOT in this list.
    };
    GraphAliasTable aliases;
    aliases.present = true;
    for (const auto& n : aliasedNames) aliases.globalIds.push_back(registry.resolve(n));

    auto aliasOf = [&](const char* name) -> int16_t { return aliases.globalToAlias(registry.resolve(name)); };
    auto makeRow = [&] {
        GraphInstance row;
        row.values.assign(aliases.globalIds.size(), ParamSlot{});
        return row;
    };
    auto readFloat = [&](const GraphInstance& row, const char* name) -> float {
        return BitsFloat(row.values[static_cast<size_t>(aliasOf(name))].dword0);
    };
    auto readBool = [&](const GraphInstance& row, const char* name) -> bool {
        return (row.values[static_cast<size_t>(aliasOf(name))].dword0 & 0xFFu) != 0;
    };
    auto readInt = [&](const GraphInstance& row, const char* name) -> int32_t {
        return static_cast<int32_t>(row.values[static_cast<size_t>(aliasOf(name))].dword0);
    };

    // Not aliased at all - resolving it is a safe no-op (control_parameters.h:
    // writable only if the target graph aliases it).
    CHECK(aliasOf("not aliased float") == -1);

    DummyMoveSource src;
    DummyStamina stamina;
    PlayerMovement pm(src, stamina);
    SeedOnFootCommonCase(pm.world);
    pm.world.orientation.set(kIdentity);
    AnimParamWorld animWorld;
    SeedPlayerAnimCommonCase(animWorld);

    // --- standing still, walking latched, crouched, fine-aiming ------------
    {
        GraphInstance row = makeRow();
        pm.st.desiredMove = {0, 0, 0};
        pm.g.publishedA = 0;
        pm.g.publishedB = 0;
        pm.st.b1d94 = stance::kWalk;
        pm.st.b1d97 = 0;
        pm.st.crouched = true;
        pm.st.fineAim = true;
        pm.st.locomotionState = kLocoIdle;
        pm.st.turnAngleA68 = 0;
        AnimParamSnapshot prev;
        prev.velocity1cec = {1, 0, 2};
        prev.velocityD04 = {0, 0.5, 0};
        prev.speedD1c = 3.0;

        WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev);

        CHECK(readBool(row, "walk bool"));
        CHECK(!readBool(row, "sprint bool"));
        CHECK(readBool(row, "crouch bool"));
        CHECK(readBool(row, "fine aim bool"));
        CHECK(readBool(row, "move forward bool"));   // row 8: none of left/right/back -> forward
        CHECK(!readBool(row, "move left bool") && !readBool(row, "move right bool") && !readBool(row, "move back bool"));
        CHECK(deq(readFloat(row, "desired speed"), 0.0));           // row 6: |D| = 0
        CHECK(deq(readFloat(row, "Vertical Speed"), 0.5));          // row 7: ONE FRAME OLD, prev.velocityD04.y
        CHECK(deq(readFloat(row, "current speed"), 3.0));           // row 20: ONE FRAME OLD, prev.speedD1c
        CHECK(deq(readFloat(row, "Speed Along rvec"), 1.0));        // row 24: dot((1,0,0),(1,0,2)) = 1, ONE FRAME OLD
        CHECK(!readBool(row, "dead") && !readBool(row, "combat ready") && readBool(row, "Supported By Ground"));
        CHECK(readInt(row, "locomotion") == kLocoIdle);
        CHECK(deq(readFloat(row, "direction"), 0.0, 1e-9));          // facing == forward, D == 0 -> 0
        CHECK(!readBool(row, "Weapon Sprint Bool"));
    }

    // --- strafing right, backpedalling (axes), run, +0xd40 proxy bits ------
    {
        GraphInstance row = makeRow();
        pm.st.b1d94 = 0;
        pm.st.b1d97 = 0;
        pm.st.crouched = false;
        pm.st.fineAim = false;
        pm.g.publishedA = 0.5f;   // right
        pm.g.publishedB = -0.5f;  // back
        pm.st.locomotionState = kLocoRun;
        pm.world.movementFlagsD40.set(0x2000u | 0x0200u);  // backpedal + alt-strafe bits (row 12/13's real source)
        AnimParamSnapshot prev;

        WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev);

        CHECK(!readBool(row, "move forward bool"));
        CHECK(readBool(row, "move right bool") && !readBool(row, "move left bool"));
        CHECK(readBool(row, "move back bool"));
        CHECK(readBool(row, "backpedal bool"));          // row 12: read from world.movementFlagsD40 directly
        CHECK(readBool(row, "alternate strafe bool"));   // row 13
        CHECK(readBool(row, "run bool") && !readBool(row, "sprint bool"));  // row 15: locomotionState == kLocoRun
        pm.world.movementFlagsD40.set(0u);  // restore for later scenarios
    }

    // --- desired speed (unsmoothed D) and direction (row 6 / row 21c) -------
    {
        GraphInstance row = makeRow();
        pm.st.desiredMove = {0, 0, 3.0};  // facing +z too -> direction 0; |D| = 3
        AnimParamSnapshot prev;
        WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev);
        CHECK(deq(readFloat(row, "desired speed"), 3.0));
        CHECK(deq(readFloat(row, "direction"), 0.0, 1e-6));

        pm.st.desiredMove = {1.0, 0, 0};  // D along +x, facing +z -> heading(D) - heading(F) = +90 deg = pi/2
        WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev);
        CHECK(deq(readFloat(row, "direction"), HeadingFromForward({1, 0, 0}) - HeadingFromForward({0, 0, 1}), 1e-5));
        pm.st.desiredMove = {0, 0, 0};
    }

    // --- OPEN gates: genuinely unimplemented subsystems throw, not guess ---
    {
        GraphInstance row = makeRow();
        AnimParamSnapshot prev;
        pm.world.ccd.set(4);
        CHECK(throwsOpen([&] { WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev); }, "0x009896d0"));
        pm.world.ccd.set(0);

        animWorld.combatReady.set(true);
        CHECK(throwsOpen([&] { WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev); },
                         "0x009ba300"));
        animWorld.combatReady.set(false);

        pm.world.freeFall.set(true);
        CHECK(throwsOpen([&] { WriteHumanWriterParams(row, aliases, registry, pm, animWorld, prev); }, "0xaf4"));
        pm.world.freeFall.set(false);
    }
}

// ===========================================================================
// 2. The post step: T/E split for looping vs. one-shot entries
// ===========================================================================
void testPostOutputList() {
    struct FixedDurationServices : GraphRuntimeServices {
        float recordDuration(int32_t) override { return 2.0f; }
    } svc;

    // --- clean case: one looping + one non-looping entry --------------------
    {
        struct Motion : ClipRootMotionSource {
            Vec3 translationPerSecond(int32_t record, bool loop) override {
                return loop ? Vec3{0, 0, 4.0} : Vec3{1.0, 0, 0};
            }
        } motion;
        GraphOutputList out;
        OutputEntry a;
        a.record = 1;
        a.weight = 0.6f;
        a.flags = kOutLoop;
        out.append(a);
        OutputEntry b;
        b.record = 2;
        b.weight = 0.4f;
        b.flags = 0;
        b.clipTime = 0.3f;
        out.append(b);

        const double dt = 0.5;
        const PostStepResult r = PostOutputList(out, svc, motion, dt);
        CHECK(r.hadAnyOutput);
        CHECK(r.clips.size() == 2);
        // T: un-normalised weighted sum of LOOPING entries only: 0.6 * (0,0,4) * 0.5 = (0,0,1.2).
        CHECK(veq(r.T, {0, 0, 1.2}));
        // E: weight-NORMALISED sum of non-looping entries; one contributor ->
        // its own per-step delta regardless of its own weight: (1,0,0)*0.5 = (0.5,0,0).
        CHECK(veq(r.E, {0.5, 0, 0}));
        for (const PostedClip& c : r.clips)
            if (c.record == 2) {
                CHECK(!c.loop && deq(c.clipTime, 0.3f) && deq(c.duration, 2.0));
            } else {
                CHECK(c.loop && deq(c.clipTime, 0.0));  // looping clips report clipTime 0
            }
    }

    // --- normalisation really divides: two non-looping entries, dt = 1 ------
    {
        struct Motion : ClipRootMotionSource {
            Vec3 translationPerSecond(int32_t record, bool) override {
                return record == 10 ? Vec3{2, 0, 0} : Vec3{0, 0, 6};
            }
        } motion;
        GraphOutputList out;
        OutputEntry a;
        a.record = 10;
        a.weight = 0.3f;
        out.append(a);
        OutputEntry b;
        b.record = 11;
        b.weight = 0.7f;
        out.append(b);
        const PostStepResult r = PostOutputList(out, svc, motion, 1.0);
        // eSum = (2,0,0)*0.3 + (0,0,6)*0.7 = (0.6,0,4.2); eWeight = 1.0.
        CHECK(veq(r.E, {0.6, 0, 4.2}));
        CHECK(veq(r.T, {0, 0, 0}));  // no looping entries at all
    }

    // --- the 0.0001 drop threshold, and bit 1's exception (Sec8.9 item 6) --
    {
        struct ZeroMotion : ClipRootMotionSource {
            Vec3 translationPerSecond(int32_t, bool) override { return {0, 0, 0}; }
        } motion;
        GraphOutputList out;
        OutputEntry dropped;
        dropped.record = 20;
        dropped.weight = 0.00005f;  // below 0.0001, no exception bit -> dropped
        out.append(dropped);
        OutputEntry kept;
        kept.record = 21;
        kept.weight = 0.00002f;  // also below 0.0001, but bit 1 set -> kept
        kept.flags = kOutZeroedInTransition;
        out.append(kept);
        const PostStepResult r = PostOutputList(out, svc, motion, 0.1);
        CHECK(r.clips.size() == 1 && r.clips[0].record == 21);
        CHECK(r.hadAnyOutput);
    }

    // --- an empty list produces no output ------------------------------------
    {
        struct Motion : ClipRootMotionSource {
            Vec3 translationPerSecond(int32_t, bool) override { return {1, 1, 1}; }
        } motion;
        GraphOutputList out;
        const PostStepResult r = PostOutputList(out, svc, motion, 0.1);
        CHECK(!r.hadAnyOutput && r.clips.empty());
        CHECK(veq(r.T, {0, 0, 0}) && veq(r.E, {0, 0, 0}));
    }
}

// ===========================================================================
// 3. The playback-rate formula (spec-player-runtime.md Sec5.4.3 step 7),
//    independently re-derived step by step as a cross-check.
// ===========================================================================
double ExpectedRateStep(double rate, double s, double tMag, double dt) {
    if (!(s > 1e-4) || !(dt > 0.0)) return rate;
    const double q = s / (tMag / dt + 1e-4);
    const bool below1 = rate < 1.0;
    if (rate < q) {
        const double rise = below1 ? 2.0 * dt : dt;
        rate = std::min(q, rate + rise);
    } else if (rate > q) {
        double fall = below1 ? 2.8 * dt : 1.4 * dt;
        if (rate > 2.0 * q) fall += dt;
        rate = std::max(q, rate - fall);
    }
    return std::clamp(rate, 0.1, 1.5);
}

void testPlaybackRate() {
    PlaybackRateTracker tracker;
    double expected = 1.0;  // CHOSEN construction value (bridge.h); both sides start here.
    CHECK(deq(tracker.rate(), expected));

    const double dt = 0.01;
    // Rise toward a huge q (T == 0): starting at rate == 1.0 (not below 1),
    // rise step is 1*dt per frame - climbs toward the clamp ceiling.
    for (int i = 0; i < 60; ++i) {
        tracker.update(/*desiredSpeed=*/1.0, /*tMagnitude=*/0.0, dt);
        expected = ExpectedRateStep(expected, 1.0, 0.0, dt);
        CHECK(deq(tracker.rate(), expected, 1e-9));
    }
    CHECK(deq(tracker.rate(), 1.5, 1e-9));  // clamped at the ceiling

    // Fall toward a near-zero q (T huge): rate(1.5) > q, and rate > 2q, so the
    // extra +dt applies (1.4dt + dt while rate stays >= 1.0).
    tracker.update(1.0, 100.0, dt);
    expected = ExpectedRateStep(expected, 1.0, 100.0, dt);
    CHECK(deq(tracker.rate(), expected, 1e-9));
    CHECK(deq(tracker.rate(), 1.5 - 0.024, 1e-9));  // 1.4*0.01 + 0.01 = 0.024

    // Keep falling with the same (huge-T, near-zero-q) script until the rate
    // crosses below 1.0, to exercise the "2.8dt below 1.0" doubling.
    for (int i = 0; i < 200 && tracker.rate() >= 1.0; ++i) {
        tracker.update(1.0, 100.0, dt);
        expected = ExpectedRateStep(expected, 1.0, 100.0, dt);
        CHECK(deq(tracker.rate(), expected, 1e-9));
    }
    CHECK(tracker.rate() < 1.0);
    const double beforeBelow1 = tracker.rate();
    tracker.update(1.0, 100.0, dt);
    expected = ExpectedRateStep(expected, 1.0, 100.0, dt);
    CHECK(deq(tracker.rate(), expected, 1e-9));
    // Below 1.0 the fall step is at least 2.8*dt (plus the >2q extra dt while
    // q stays tiny) - strictly larger than the >=1.0 branch's 1.4dt+dt.
    CHECK(beforeBelow1 - tracker.rate() >= 2.8 * dt - 1e-9);

    // Keep falling toward the clamp floor.
    for (int i = 0; i < 500; ++i) {
        tracker.update(1.0, 1000.0, dt);
        expected = ExpectedRateStep(expected, 1.0, 1000.0, dt);
    }
    CHECK(deq(tracker.rate(), 0.1, 1e-9));
    CHECK(deq(tracker.rate(), expected, 1e-9));

    // Holding: desiredSpeed <= 1e-4 makes no write at all (CHOSEN, bridge.h).
    const double held = tracker.rate();
    tracker.update(0.0, 5.0, dt);
    CHECK(deq(tracker.rate(), held, 1e-12));
    tracker.update(1.0, 5.0, 0.0);  // dt == 0 also holds
    CHECK(deq(tracker.rate(), held, 1e-12));
}

// ===========================================================================
// 4. Camera hookup: CameraSystem::target tracks the character (do this last,
//    per the task - it's small since the camera already exists).
// ===========================================================================
void testCameraTracking() {
    DummyMoveSource src;
    DummyStamina stamina;
    PlayerMovement pm(src, stamina);
    SeedOnFootCommonCase(pm.world);
    pm.world.orientation.set(kIdentity);
    pm.st.position = {5, 0, -3};

    ScriptedLook look;
    CameraSystem cam(look);

    DriveCameraTarget(cam, pm);
    CHECK(cam.target.valid && cam.target.isHuman && !cam.target.isVehicle);
    CHECK(veq(cam.target.referencePoint, pm.st.position));
    CHECK(deq(cam.target.heading, HeadingFromForward({0, 0, 1})));

    pm.st.position = {5, 0, 10};  // the character moved
    DriveCameraTarget(cam, pm);
    CHECK(veq(cam.target.referencePoint, {5, 0, 10}));  // the camera's target tracks it

    // OrientationFromFacing: reuses camera.h's own RotationFromAngles/
    // HeadingFromForward convention - round-trips a flat facing vector back
    // to (approximately) itself as the forward row, and produces an
    // orthonormal right/up/forward triple.
    for (const Vec3 f : {Vec3{0, 0, 1}, Vec3{1, 0, 0}, Vec3{1, 0, 1}, Vec3{-1, 0, 1}}) {
        const Orientation o = OrientationFromFacing(f);
        CHECK(veq(o.forward, NormaliseOrZero(f), 1e-9));
        CHECK(deq(Length(o.right), 1.0, 1e-9) && deq(Length(o.up), 1.0, 1e-9) && deq(Length(o.forward), 1.0, 1e-9));
        CHECK(deq(Dot(o.right, o.forward), 0.0, 1e-9) && deq(Dot(o.right, o.up), 0.0, 1e-9));
    }
}

// ===========================================================================
// 5. Graph evaluation across multiple frames, driven by a real player: the
//    full PlayerAnimBridge, a small REAL loaded graph using the same
//    `locomotion`-parameter gate the real player_state_machine graph uses.
// ===========================================================================
std::string IntCond(const char* param, int value, const char* cmp) {
    return std::string("<condition><control_parameter><name>local</name><data_type>integer</data_type>"
                       "<default_value>") +
          std::to_string(value) +
          "</default_value></control_parameter><comparison>" + cmp +
          "</comparison><parameter1><type>control parameter</type><source_name>" + param +
          "</source_name></parameter1><parameter2><type>local control parameter</type>"
          "<source_name>local</source_name></parameter2></condition>";
}
std::string ClipStateXml(const char* name, const char* tag, const std::string& transitions, const char* loop) {
    return std::string("<node><name>") + name + "</name><tag_name>" + tag + "</tag_name><loop_flag>" + loop +
          "</loop_flag>" + (transitions.empty() ? "" : "<transitions>" + transitions + "</transitions>") + "</node>";
}
std::string TransitionXml(const char* name, const char* to, float length, const std::string& conditions) {
    return std::string("<transition><name>") + name + "</name><to_node>" + to + "</to_node><length>" +
          std::to_string(length) + "</length><conditions>" + conditions + "</conditions></transition>";
}
std::string StateMachineXml(const char* name, const char* deflt, const std::string& nodes) {
    return std::string("<root><state_machine><validated>true</validated><name>") + name + "</name><default_node>" +
          deflt + "</default_node><nodes>" + nodes + "</nodes></state_machine></root>";
}

void testMultiFrameGraphDrive() {
    const char* kParamsXml =
        R"(<root><control_parameters><control_parameter><name>locomotion</name><data_type>integer</data_type><default_value>0</default_value></control_parameter></control_parameters></root>)";
    const ControlParameterRegistry registry = ControlParameterRegistry::loadXml(kParamsXml);
    const sr3xtbl::Document filtersDoc = sr3xtbl::ParseDocument(std::string_view("<root><control_filters></control_filters></root>"));
    const ControlFilterTable filters = ControlFilterTable::load(filtersDoc, registry);
    const StateActionRegistry stateActions = StateActionRegistry::build({}, {"idle_tag", "move_tag"}, {});
    const TriggerRegistry triggers = TriggerRegistry::build({});

    const std::string graphXml = StateMachineXml(
        "Player SM Test", "Idle",
        ClipStateXml("Idle", "idle_tag", TransitionXml("Idle to Moving", "Moving", 0.2f, IntCond("locomotion", 0, "not equal")),
                    "false") +
            ClipStateXml("Moving", "move_tag",
                        TransitionXml("Moving to Idle", "Idle", 0.2f, IntCond("locomotion", 0, "equal")), "true"));

    const sr3xtbl::Document listDoc =
        sr3xtbl::ParseDocument(std::string_view("<root><node_graph_files><node_graph_file><name>player_sm_test</name>"
                                                "</node_graph_file></node_graph_files></root>"));
    NodeGraphLoadInputs in;
    in.registry = &registry;
    in.filters = &filters;
    in.stateActions = &stateActions;
    in.triggers = &triggers;
    in.openFile = [&](const std::string& f) -> std::optional<std::string> {
        if (f == "player_sm_test.xtbl") return graphXml;
        return std::nullopt;
    };
    const NodeGraphLibrary lib = LoadNodeGraphs(listDoc, in);
    CHECK(!lib.aborted);
    const int32_t gi = lib.findGraph("Player SM Test");
    CHECK(gi >= 0);

    GraphInstancePool pool;
    PlayerGraphServices svc;
    svc.tagToRecord = [](int32_t tagIndex) { return tagIndex + 100; };
    svc.duration = [](int32_t) { return 1.0f; };
    struct Motion : ClipRootMotionSource {
        Vec3 translationPerSecond(int32_t, bool loop) override { return loop ? Vec3{0, 0, 2.0} : Vec3{}; }
    } motion;

    PlayerAnimBridge bridge(lib, gi, registry, pool, svc, motion, filters);

    ForwardSource src;
    DummyStamina stamina;
    PlayerMovement pm(src, stamina);
    SeedOnFootCommonCase(pm.world);
    pm.world.orientation.set(kIdentity);
    pm.world.controlsDisabled.set(false);
    pm.world.movementDisabled.set(false);
    pm.world.permissions2078.set(0x4000u | 0x2000u);
    pm.tweaks.maxRunSpeed.set(10.0);
    pm.tweaks.maxSprintSpeed.set(15.0);
    pm.tweaks.crouchMoveSpeed.set(4.0);
    pm.tweaks.maxFineAimSpeed.set(3.0);
    pm.tweaks.acceleration.set(20.0);
    pm.tweaks.diveRollAcceleration.set(40.0);
    pm.tweaks.crouchClickSensitivity.set(0.5);

    const GraphDef* def = lib.graph(gi);
    const int32_t movingNode = FindNodeIndex(*def, "Moving");
    bool reachedMovingFullWeight = false;
    bool threw = false;
    try {
        for (int i = 0; i < 90; ++i) {
            bridge.stepFrame(pm, frameAt(1000 + i * 33));
            for (const OutputEntry& e : bridge.lastOutput().entries())
                if (e.graph == gi && e.node == movingNode && e.weight == 1.0f) reachedMovingFullWeight = true;
            CHECK(bridge.playbackRate() >= 0.1 && bridge.playbackRate() <= 1.5);
        }
    } catch (const std::exception& e) {
        threw = true;
        std::cerr << "unexpected throw driving the graph: " << e.what() << "\n";
    }
    CHECK(!threw);
    CHECK(pm.st.locomotionState != kLocoIdle);  // forward input eventually leaves idle
    CHECK(reachedMovingFullWeight);             // the graph really transitioned via `locomotion`
    // Once Moving holds full weight with no other entries, T is exactly this
    // step's rate*dt (the FakeMotion's constant 2 units/s), un-normalised.
    bool sawPureMovingT = false;
    for (const PostedClip& c : bridge.lastPost().clips)
        if (c.loop && c.weight == 1.0f) sawPureMovingT = true;
    CHECK(sawPureMovingT);
    CHECK(bridge.lastPost().T.z > 0.0);  // real root motion flowing from the real graph's output
}

}  // namespace

int main() {
    try {
        testParameterWriter();
        testPostOutputList();
        testPlaybackRate();
        testCameraTracking();
        testMultiFrameGraphDrive();
    } catch (const std::exception& e) {
        std::cerr << "uncaught exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures == 0) {
        std::cout << "All sr3playeranim bridge tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
