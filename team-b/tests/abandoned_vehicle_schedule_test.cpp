// Tests for include/sr3airuntime/abandoned_vehicle_schedule.h: spec-ai-runtime.md Sec20.6.14(h) vectors 3-7.
#include <cstdio>

#include "sr3airuntime/abandoned_vehicle_schedule.h"

using namespace sr3airuntime;

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

static std::vector<AbandonedCandidate> Cars(int n, bool visible, int64_t age = 60000) {
    std::vector<AbandonedCandidate> v;
    for (int i = 0; i < n; ++i) {
        AbandonedCandidate c;
        c.id = static_cast<uint64_t>(i + 1);
        c.ageMs = age;
        c.visible = visible;
        c.distance = 10.0 + i;  // the last is the farthest
        v.push_back(c);
    }
    return v;
}

int main() {
    // #3: 4 cars: n < 5, timer reset, nothing removed.
    {
        AbandonedVehicleSchedule s;
        CHECK(!s.Run(0, Cars(4, false)).found);
        CHECK(!s.globalTimerRunning());
        CHECK(!s.Run(60000, Cars(4, false)).found);
    }
    // #4: 5 cars, none visible, all old: timer starts, deleted at E >= 10 s (the farthest), then n = 4 resets.
    {
        AbandonedVehicleSchedule s;
        CHECK(!s.Run(0, Cars(5, false)).found);
        CHECK(s.globalTimerRunning());
        CHECK(!s.Run(9999, Cars(5, false)).found);
        const auto p = s.Run(10000, Cars(5, false));
        CHECK(p.found && p.id == 5 && !p.inView);
        CHECK(!s.Run(10500, Cars(4, false)).found);
        CHECK(!s.globalTimerRunning());
    }
    // #5: 7 cars, none visible: row {7, 0 s} fires on the first pass (E = 0).
    {
        AbandonedVehicleSchedule s;
        const auto p = s.Run(1000, Cars(7, false));
        CHECK(p.found && p.id == 7 && !p.inView);
    }
    // #6: 6 cars, all visible: the out-of-view pick fails; in view at E >= 20 s the farthest fades; the next
    // in-view removal waits for the 10 s gate.
    {
        AbandonedVehicleSchedule s;
        CHECK(!s.Run(0, Cars(6, true)).found);
        CHECK(!s.Run(19999, Cars(6, true)).found);
        const auto p = s.Run(20000, Cars(6, true));
        CHECK(p.found && p.id == 6 && p.inView);
        CHECK(!s.Run(25000, Cars(6, true)).found);   // gate: 5 s < 10 s
        CHECK(s.Run(30000, Cars(6, true)).found);    // gate open, E still >= 20 s
    }
    // #7 / eligibility: an ineligible (sunk last vehicle) car counts toward n but is never picked.
    {
        AbandonedVehicleSchedule s;
        auto c = Cars(5, false);
        c.back().eligible = false;  // the farthest is the ineligible one
        CHECK(!s.Run(0, c).found);
        const auto p = s.Run(10000, c);
        CHECK(p.found && p.id == 4);
    }
    // The picker's own age gate: a car younger than abandon_despawn_delay never qualifies.
    {
        AbandonedVehicleSchedule s;
        auto c = Cars(7, false);
        for (auto& x : c) x.ageMs = 5000;
        CHECK(!s.Run(0, c).found);
    }
    if (g_fail == 0) std::printf("abandoned_vehicle_schedule_test: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
