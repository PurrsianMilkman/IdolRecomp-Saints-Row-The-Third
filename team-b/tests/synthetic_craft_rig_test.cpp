// Tests for tools/sr3_viewer/viewer_craft_rig.h (AW-15): the 3-D rigid body that wires the Sec10.13 laws.
// Flies the rig on a flat ground plane / a water plane and checks the behaviours the playtest relies on.
#include <cmath>
#include <cstdio>

#include "viewer_craft_rig.h"

using namespace sr3viewer_craft;

static int g_fail = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);           \
            ++g_fail;                                                  \
        }                                                              \
    } while (0)

static const double dt = 1.0 / 30.0;

static CraftEnv FlatGround(double y) {
    CraftEnv e;
    e.ground = [y](double, double, double, double& out) {
        out = y;
        return true;
    };
    e.hasWater = false;
    return e;
}
static CraftEnv OpenSea(double waterY) {
    CraftEnv e;
    e.ground = [](double, double, double, double&) { return false; };
    e.hasWater = true;
    e.waterY = waterY;
    return e;
}

static CraftTypeRow HeliRow() {
    CraftTypeRow r;
    r.name = "heli_standard_02";
    r.cls = 3;
    r.mass = 1360;
    r.maxSpeed = 31.2928;
    r.heli.normalSpinDamping = r.heli.collisionSpinDamping = 3.0;  // authored per vehicle (entry +0x848)
    return r;
}
static CraftTypeRow PlaneRow() {
    CraftTypeRow r;
    r.name = "plane_standard01";
    r.cls = 2;
    r.mass = 1500;
    r.maxSpeed = 44.704;
    r.plane.enginePoints = {pc::V3{0, 0, -2}};
    return r;
}
static CraftTypeRow BoatRow() {
    CraftTypeRow r;
    r.name = "boat_speed01";
    r.cls = 5;
    r.mass = 8600;
    r.buoyancy = 0.8;
    r.maxSpeed = 29.0576;
    return r;
}

int main() {
    // orientation helpers
    {
        const Quat qh = FromSimHeading(0.7);
        const pc::Basis b = BasisOf(qh);
        CHECK(std::fabs(b.fwd.x - std::cos(0.7)) < 1e-9 && std::fabs(b.fwd.z - std::sin(0.7)) < 1e-9 && std::fabs(b.fwd.y) < 1e-9);
        CHECK(std::fabs(b.up.y - 1.0) < 1e-9);
        CHECK(std::fabs(pc::Dot(pc::Cross(b.right, b.up), b.fwd) - 1.0) < 1e-9);  // right x up = fwd
        const Quat qp = FromSimHeading(0.0, 0.3);
        CHECK(PitchOf(BasisOf(qp)) > 0.29 && PitchOf(BasisOf(qp)) < 0.31);  // positive pitch = nose up
        // integrating a yaw rate about +Y turns the nose toward +X from +Z (right)
        Quat q0;
        for (int i = 0; i < 30; ++i) q0 = Integrate(q0, {0, 1.0, 0}, 1.0 / 30.0);
        CHECK(std::fabs(BasisOf(q0).fwd.x - std::sin(1.0)) < 0.02);
    }
    // script parser
    {
        std::vector<ScriptSeg> segs;
        ParseCraftScript("10-20:up=1,fwd=0.5;30-30:chute=1", segs);
        CHECK(segs.size() == 2);
        bool ch = false;
        CraftControls c = ScriptControlsAt(segs, 15, ch);
        CHECK(c.up == 1.0 && c.fwd == 0.5 && !ch);
        c = ScriptControlsAt(segs, 30, ch);
        CHECK(ch);
        c = ScriptControlsAt(segs, 25, ch);
        CHECK(c.up == 0.0 && !ch);
    }
    // helicopter: take off, hover, descend and land
    {
        CraftRig r;
        r.Enter(Kind::Heli, HeliRow(), {0, 1.2, 0}, 0.0, {-1.5, -1.2, -4}, {1.5, 1.5, 4});
        const CraftEnv env = FlatGround(0.0);
        CraftControls idle;
        for (int i = 0; i < 30; ++i) r.Step(dt, idle, env);
        CHECK(r.onGround);
        CHECK(r.body.pos.y > 1.1 && r.body.pos.y < 1.3);  // rests on its skids with no input
        CraftControls up;
        up.up = 1.0;
        for (int i = 0; i < 120; ++i) r.Step(dt, up, env);  // 4 s of full collective
        CHECK(r.body.pos.y > 15.0);
        CHECK(r.airborne());
        for (int i = 0; i < 90; ++i) r.Step(dt, idle, env);  // collective released: altitude hold
        const double hoverY = r.body.pos.y;
        for (int i = 0; i < 60; ++i) r.Step(dt, idle, env);
        CHECK(std::fabs(r.body.pos.y - hoverY) < 1.0);
        CHECK(std::fabs(r.body.vel.y) < 0.5);
        CraftControls down;
        down.down = 0.25;
        for (int i = 0; i < 30 * 60 && !r.onGround; ++i) r.Step(dt, down, env);
        CHECK(r.onGround);
        for (int i = 0; i < 60; ++i) r.Step(dt, idle, env);
        CHECK(pc::Len(r.body.vel) < 0.5);
        CHECK(r.stats.takeoffs == 1 && r.stats.landings >= 1);
        CHECK(r.stats.maxImpact < 8.0);  // soft landing under the min-time limiter
    }
    // Team A 2026-10-20 (Sec10.13.7 note): an empty helicopter is not set down; the laws keep running with a = a_lim + 4.9
    // (half-cancelled gravity), so it ends up below an occupied twin that holds its altitude, and lands by physics.
    {
        CraftRig r, twin;
        r.Enter(Kind::Heli, HeliRow(), {0, 1.2, 0}, 0.0, {-1.5, -1.2, -4}, {1.5, 1.5, 4});
        twin.Enter(Kind::Heli, HeliRow(), {0, 1.2, 0}, 0.0, {-1.5, -1.2, -4}, {1.5, 1.5, 4});
        const CraftEnv env = FlatGround(0.0);
        CraftControls up;
        up.up = 1.0;
        for (int i = 0; i < 120; ++i) { r.Step(dt, up, env); twin.Step(dt, up, env); }
        CHECK(r.body.pos.y > 15.0);
        r.Vacate();
        CraftControls idle;
        for (int i = 0; i < 60; ++i) { r.Step(dt, idle, env); twin.Step(dt, idle, env); }
        std::printf("empty heli y=%.3f vy=%.3f, occupied twin y=%.3f vy=%.3f\n", r.body.pos.y, r.body.vel.y, twin.body.pos.y, twin.body.vel.y);
        CHECK(r.body.pos.y < twin.body.pos.y - 0.3 && !r.onGround);  // sinking under half-cancelled gravity, not snapped down
        for (int i = 0; i < 30 * 240 && !r.onGround; ++i) r.Step(dt, idle, env);
        CHECK(r.onGround);                                // lands by physics
    }
    // helicopter: cyclic forward pitches the nose down and flies the way the nose points
    {
        CraftRig r;
        r.Enter(Kind::Heli, HeliRow(), {0, 1.2, 0}, 0.0, {-1.5, -1.2, -4}, {1.5, 1.5, 4});  // heading 0: forward = +X
        const CraftEnv env = FlatGround(0.0);
        CraftControls up;
        up.up = 1.0;
        for (int i = 0; i < 90; ++i) r.Step(dt, up, env);
        CraftControls fwd;
        fwd.fwd = 1.0;
        for (int i = 0; i < 90; ++i) r.Step(dt, fwd, env);
        CHECK(PitchOf(r.body.basis) < -0.03 && PitchOf(r.body.basis) > -0.9);  // nose down, no flip while the stick is held
        CHECK(r.body.pos.x > 3.0);                    // moved along the initial forward (+X)
        CHECK(r.body.vel.x > 1.0);
    }
    // helicopter: tail rotor yaws (Space + D = turn right: heading changes)
    {
        CraftRig r;
        r.Enter(Kind::Heli, HeliRow(), {0, 1.2, 0}, 0.0, {-1.5, -1.2, -4}, {1.5, 1.5, 4});
        const CraftEnv env = FlatGround(0.0);
        CraftControls up;
        up.up = 1.0;
        for (int i = 0; i < 90; ++i) r.Step(dt, up, env);
        const double h0 = SimHeadingOf(r.body.basis);
        CraftControls turn;
        turn.turnSelect = true;
        turn.right = 1.0;
        for (int i = 0; i < 60; ++i) r.Step(dt, turn, env);
        CHECK(std::fabs(pc::WrapPi(SimHeadingOf(r.body.basis) - h0)) > 0.3);
    }
    // plane: taxi, take off, climb; stick signs
    {
        CraftRig r;
        r.Enter(Kind::Plane, PlaneRow(), {0, 1.5, 0}, 0.0, {-5, -1.5, -6}, {5, 1.5, 6});
        const CraftEnv env = FlatGround(0.0);
        CraftControls thr;
        thr.up = 1.0;
        for (int i = 0; i < 30 * 12; ++i) r.Step(dt, thr, env);
        CHECK(r.onGround);
        const double u = pc::Dot(r.body.vel, r.body.basis.fwd);
        CHECK(u > 20.0);                      // accelerating along the runway
        CraftControls pull = thr;
        pull.fwd = -1.0;                      // S = pull back = nose up
        for (int i = 0; i < 30 * 3; ++i) r.Step(dt, pull, env);
        CHECK(r.airborne());
        CHECK(r.body.pos.y > 5.0);
        CHECK(PitchOf(r.body.basis) > 0.0);
        CHECK(r.stats.takeoffs == 1);
    }
    // plane in the air: roll right (D) gives a positive bank (right wing down) and a right turn (heading decreases)
    {
        CraftRig r;
        r.Enter(Kind::Plane, PlaneRow(), {0, 500.0, 0}, 0.0, {-5, -1.5, -6}, {5, 1.5, 6});
        r.body.vel = {40.0, 0.0, 0.0};  // level flight along the initial forward (+X), lift ~ 1
        CraftEnv env = FlatGround(0.0);
        CraftControls thr;
        thr.up = 0.5;
        for (int i = 0; i < 30; ++i) r.Step(dt, thr, env);
        const double h0 = SimHeadingOf(r.body.basis);
        CraftControls roll = thr;
        roll.right = 1.0;
        for (int i = 0; i < 30 * 3; ++i) r.Step(dt, roll, env);
        CHECK(pc::PlaneBank(r.body.basis) > 0.5);
        CHECK(pc::WrapPi(SimHeadingOf(r.body.basis) - h0) < -0.1);
    }
    // boat: floats at about the rest volume of W1, then drives
    {
        CraftRig r;
        r.Enter(Kind::Boat, BoatRow(), {0, 0.4, 0}, 0.0, {-1.4, -0.6, -4.5}, {1.4, 0.9, 4.5});
        const CraftEnv env = OpenSea(0.0);
        CraftControls idle;
        for (int i = 0; i < 30 * 8; ++i) r.Step(dt, idle, env);
        CHECK(r.inWater);
        CHECK(std::fabs(r.body.vel.y) < 0.3);
        CHECK(r.body.basis.up.y > 0.95);
        CHECK(r.body.pos.y > -2.0 && r.body.pos.y < 1.5);
        CraftControls gas;
        gas.fwd = 1.0;
        for (int i = 0; i < 30 * 10; ++i) r.Step(dt, gas, env);
        const double u = pc::Dot(r.body.vel, r.body.basis.fwd);
        CHECK(u > 8.0);
        CHECK(r.body.basis.up.y > 0.8);
        // steering right = positive yaw rate about +Y; with forward = (cos h, sin h) and right = (sin h, -cos h) the sim
        // heading h = atan2(z, x) DEcreases on a right turn
        CraftControls turn = gas;
        turn.right = 1.0;
        const double h0 = SimHeadingOf(r.body.basis);
        for (int i = 0; i < 30 * 3; ++i) r.Step(dt, turn, env);
        CHECK(pc::WrapPi(SimHeadingOf(r.body.basis) - h0) < -0.2);
    }
    // boat: a hull on land does not float
    {
        CraftRig r;
        r.Enter(Kind::Boat, BoatRow(), {0, 5.0, 0}, 0.0, {-1.4, -0.6, -4.5}, {1.4, 0.9, 4.5});
        CraftEnv env = FlatGround(3.0);  // ground above the water plane at 0
        env.hasWater = true;
        env.waterY = 0.0;
        CraftControls idle;
        for (int i = 0; i < 30 * 4; ++i) r.Step(dt, idle, env);
        CHECK(r.onGround && !r.inWater);
    }
    // VTOL: request jet mode via the toggle
    {
        CraftTypeRow row = HeliRow();
        row.cls = 4;
        row.plane = PlaneRow().plane;
        CraftRig r;
        r.Enter(Kind::Vtol, row, {0, 1.2, 0}, 0.0, {-1.5, -1.2, -4}, {1.5, 1.5, 4});
        const CraftEnv env = FlatGround(0.0);
        CraftControls up;
        up.up = 1.0;
        for (int i = 0; i < 90; ++i) r.Step(dt, up, env);
        CHECK(r.vs.heliModel == 1);
        CraftControls tog;
        tog.vtolToggle = true;
        r.Step(dt, tog, env);
        CraftControls none;
        for (int i = 0; i < 60; ++i) r.Step(dt, none, env);
        CHECK(r.vs.heliModel == 0);  // jet mode after the wing animation
    }
    if (g_fail == 0) std::printf("synthetic_craft_rig_test: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
