// Unit tests for sr3player/freefall.h - reproduces spec-player-runtime.md Sec19.6 vectors 1-12 (dt = 1/30).
#include <cmath>
#include <cstdio>

#include "sr3player/freefall.h"

using namespace sr3player::freefall;

static int g_fail = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                      \
            ++g_fail;                                                             \
        }                                                                         \
    } while (0)
static bool Near(double a, double b, double e) { return std::fabs(a - b) <= e; }

static const double dt = 1.0 / 30.0;

static MoveInputs Stick(double A, double B) {
    MoveInputs m;
    m.A = A;
    m.B = B;
    m.fwdX = 0;
    m.fwdZ = 1;
    m.rtX = 1;
    m.rtZ = 0;
    m.faceX = 0;
    m.faceZ = 1;
    return m;
}

int main() {
    // 1: freefall, no stick, settled: T = 45, horizontal 0, steady fall 45 m/s.
    {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        for (int i = 0; i < 300; ++i) UpdateMovement(s, Stick(0, 0), dt);
        CHECK(Near(s.terminalT, 45.0, 1e-9));
        CHECK(s.speed == 0.0 && s.rx == 0.0 && s.rz == 0.0);
        double vy = -45.0;
        for (int i = 0; i < 10; ++i) vy = StepVertical(vy, s.terminalT, dt);
        CHECK(Near(vy, -45.0, 1e-9));  // factor (1 - 1) = 0: steady
    }
    // 2: B = +0.3 from b = 0, 15 frames: b = 1 - (14/15)^15 = 0.645; T = 59.2.
    {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        for (int i = 0; i < 15; ++i) UpdateMovement(s, Stick(0, 0.3), dt);
        CHECK(Near(s.b, 1.0 - std::pow(14.0 / 15.0, 15), 1e-12));
        CHECK(Near(s.b, 0.645, 0.001));
        CHECK(Near(s.terminalT, 45.0 + 22.0 * s.b, 1e-9));
        CHECK(Near(s.terminalT, 59.2, 0.05));
    }
    // 3: B = -1 settled: b = -1, T = 36, D = -3 F.
    {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        double Dx = 0, Dz = 0;
        for (int i = 0; i < 600; ++i) UpdateMovement(s, Stick(0, -1), dt, &Dx, &Dz);
        CHECK(Near(s.b, -1.0, 1e-6));
        CHECK(Near(s.terminalT, 36.0, 1e-5));
        CHECK(Near(Dx, 0.0, 1e-12) && Near(Dz, -3.0, 1e-12));
    }
    // 4: A = +1, B = 0 from r = 0: D = 20 Rt, |r| = 0.333 after 1 frame, 20 after 2.0 s, bank -> 0.6.
    {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        double Dx = 0, Dz = 0;
        UpdateMovement(s, Stick(1, 0), dt, &Dx, &Dz);
        CHECK(Near(Dx, 20.0, 1e-12) && Near(Dz, 0.0, 1e-12));
        CHECK(Near(s.speed, 1.0 / 3.0, 1e-9));
        for (int i = 1; i < 60; ++i) UpdateMovement(s, Stick(1, 0), dt);  // 60 frames = 2.0 s
        CHECK(Near(s.speed, 20.0, 1e-6));
        for (int i = 0; i < 600; ++i) UpdateMovement(s, Stick(1, 0), dt);
        CHECK(Near(s.bank, 0.6, 1e-3));
        CHECK(s.bank < 0.6);
    }
    // 5: chute, no stick, lock expired: s = 8, T = 9.
    auto OpenedChute = [&]() {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        for (int i = 0; i < 60; ++i) TickTimers(s, dt);  // lock expired
        ButtonInputs bi;
        bi.fallSpeed = 20;
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::Deployed);
        return s;
    };
    {
        FreefallState s = OpenedChute();
        for (int i = 0; i < 60; ++i) TickTimers(s, dt);  // let the post-deploy lock run out too
        double Dx = 0, Dz = 0;
        for (int i = 0; i < 300; ++i) UpdateMovement(s, Stick(0, 0), dt, &Dx, &Dz);
        CHECK(Near(std::sqrt(Dx * Dx + Dz * Dz), 8.0, 1e-9));
        CHECK(Near(s.terminalT, 9.0, 1e-9));
    }
    // 6: chute B = +1 / +0.5 / -1: s = 20 / 14 / 2; T = 11.5 / 10.25 / 6.7.
    {
        const double Bs[3] = {1.0, 0.5, -1.0};
        const double ss[3] = {20.0, 14.0, 2.0};
        const double Ts[3] = {11.5, 10.25, 6.7};
        for (int k = 0; k < 3; ++k) {
            FreefallState s = OpenedChute();
            for (int i = 0; i < 60; ++i) TickTimers(s, dt);
            double Dx = 0, Dz = 0;
            for (int i = 0; i < 900; ++i) UpdateMovement(s, Stick(0, Bs[k]), dt, &Dx, &Dz);
            CHECK(Near(std::sqrt(Dx * Dx + Dz * Dz), ss[k], 1e-9));
            CHECK(Near(s.terminalT, Ts[k], 1e-4));
        }
    }
    // 7: chute just opened, B = +1: s = 8 for 1.5 s, then 20.
    {
        FreefallState s = OpenedChute();
        double Dx = 0, Dz = 0;
        for (int i = 0; i < 44; ++i) {  // 44 frames = 1.4667 s, the lock still runs
            TickTimers(s, dt);
            UpdateMovement(s, Stick(0, 1), dt, &Dx, &Dz);
            CHECK(Near(Dz, 8.0, 1e-9));
        }
        for (int i = 0; i < 3; ++i) {
            TickTimers(s, dt);
            UpdateMovement(s, Stick(0, 1), dt, &Dx, &Dz);
        }
        CHECK(Near(Dz, 20.0, 1e-9));
    }
    // 8: opening at sink 45 m/s, T = 9: step 1 sink 37.16; step 2: 31.92.
    {
        double vy = -45.0;
        // gravity x (1 - 25) = +235.2 up
        const double ratio = 45.0 / 9.0;
        CHECK(Near(-9.8 * (1.0 - ratio * ratio), 235.2, 1e-9));
        vy = StepVertical(vy, 9.0, dt);
        CHECK(Near(-vy, 37.16, 0.005));
        vy = StepVertical(vy, 9.0, dt);
        CHECK(Near(-vy, 31.92, 0.005));
    }
    // 9: fall starts at t = 0; button at 1.0 s and at 1.6 s.
    {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        ButtonInputs bi;
        bi.fallSpeed = 30;
        int f = 0;
        for (; f < 30; ++f) TickTimers(s, dt);
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::None);  // 1.0 s
        CHECK(!s.chuteOpen);
        for (; f < 48; ++f) TickTimers(s, dt);
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::Deployed);  // 1.6 s
        CHECK(s.chuteOpen);
    }
    // 10: chute open at t = 0; button at 1.2 s (nothing) and 1.6 s (cut away); re-deploy from 3.1 s.
    {
        FreefallState s;
        StartFreefall(s, 0, 0, true);
        for (int i = 0; i < 60; ++i) TickTimers(s, dt);
        ButtonInputs bi;
        bi.fallSpeed = 30;
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::Deployed);  // t = 0
        int f = 0;
        for (; f < 36; ++f) TickTimers(s, dt);
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::None);  // 1.2 s
        CHECK(s.chuteOpen);
        for (; f < 48; ++f) TickTimers(s, dt);
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::CutAway);  // 1.6 s
        CHECK(!s.chuteOpen && s.inFreefall);
        for (int i = 0; i < 100; ++i) UpdateMovement(s, Stick(0, 0), dt);
        CHECK(Near(s.terminalT, 45.0, 1e-6));
        // re-deploy not before 3.1 s (cut at 1.6 s + 1.5 s)
        for (f = 48; f < 90; ++f) TickTimers(s, dt);  // 3.0 s
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::None);
        for (; f < 96; ++f) TickTimers(s, dt);  // 3.2 s
        CHECK(PressActionButton(s, bi, 0, 0) == ButtonResult::Deployed);
    }
    // 11: falling at 9 m/s, or 8 m above ground: no freefall.
    {
        StartInputs si;
        si.fallSpeed = 20;
        CHECK(CanStartFreefall(si));
        si.fallSpeed = 9;
        CHECK(!CanStartFreefall(si));
        si.fallSpeed = 20;
        si.groundWithinClearDistance = true;  // 8 m: the 10 m ray hits
        CHECK(!CanStartFreefall(si));
        si.groundWithinClearDistance = false;
        si.onMovingSupport = true;
        CHECK(!CanStartFreefall(si));
        si.onMovingSupport = false;
        si.cc8 = 2;
        CHECK(!CanStartFreefall(si));
        si.cc8 = 0;
        si.locomotion = 5;
        CHECK(!CanStartFreefall(si));
    }
    // 12: landing under the chute: landing animation, chute released and faded after 1000 ms, T = 44.704.
    {
        FreefallState s = OpenedChute();
        EndInputs ei;
        ei.landed = true;
        bool anim = false;
        CHECK(CheckEnd(s, ei, anim));
        CHECK(anim && !s.inFreefall && !s.chuteOpen);
        CHECK(Near(s.terminalT, 44.704, 1e-12));
        CHECK(Near(s.chuteTimerMs, 1000.0, 1e-9));
        for (int i = 0; i < 30; ++i) TickTimers(s, dt);
        CHECK(s.chuteTimerMs <= 1e-6);
    }
    // extras: ground-near skips steering; non-landing end conditions; moving-support deploy rule
    {
        FreefallState s;
        StartFreefall(s, 3, 4, true);
        MoveInputs m = Stick(1, 1);
        m.groundNear = true;
        UpdateMovement(s, m, dt);
        CHECK(s.rx == 3.0 && s.rz == 4.0);
        EndInputs ei;
        ei.locomotion = 6;
        bool anim = true;
        CHECK(CheckEnd(s, ei, anim) && !anim && !s.inFreefall);
        FreefallState t;
        t.inFreefall = true;
        ButtonInputs bi;
        bi.onMovingSupport = true;
        bi.fallSpeed = 1.0;
        CHECK(MayDeploy(t, bi));
        bi.fallSpeed = 0.0;
        CHECK(!MayDeploy(t, bi));
        bi.isPlayer = false;
        bi.fallSpeed = 5.0;
        CHECK(!MayDeploy(t, bi));
    }
    if (g_fail) {
        std::printf("freefall tests FAILED: %d\n", g_fail);
        return 1;
    }
    std::printf("freefall tests passed\n");
    return 0;
}
