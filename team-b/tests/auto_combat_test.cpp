// Decision-logic tests for the sr3_viewer --auto-combat harness assist (sr3luahost/auto_combat.h).
#include <iostream>

#include "sr3luahost/auto_combat.h"

using namespace sr3luahost::autocombat;

static int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

int main() {
    Config cfg;
    {   // far target: first frame teleports to the standoff point, never instant-kills
        State s;
        std::vector<Target> t{{"a", 500, 0}};
        Decision d = decide(s, cfg, 0, 0, t, 0);
        CHECK(d.action == Action::Teleport);
        CHECK(std::abs(d.tx - 475.0) < 1e-6);
        // does not re-teleport every frame
        d = decide(s, cfg, 0, 0, t, 0);
        CHECK(d.action == Action::None);
    }
    {   // in range: waits noticeFrames, then fires at the interval
        State s;
        std::vector<Target> t{{"a", 20, 0}};
        int fires = 0, firstFire = -1;
        for (int f = 0; f < 300; ++f) {
            Decision d = decide(s, cfg, 0, 0, t, 0);
            CHECK(d.action != Action::Teleport && d.action != Action::Fallback);
            if (d.action == Action::Fire) { if (firstFire < 0) firstFire = f; ++fires; CHECK(d.target == "a"); }
        }
        CHECK(firstFire > cfg.noticeFrames - 1);
        CHECK(fires > 5);
    }
    {   // nearest target chosen
        State s;
        std::vector<Target> t{{"far", 40, 0}, {"near", 10, 0}};
        Decision d;
        for (int f = 0; f < 300; ++f) { d = decide(s, cfg, 0, 0, t, 0); if (d.action == Action::Fire) break; }
        CHECK(d.action == Action::Fire && d.target == "near");
    }
    {   // fallback only after fallbackFrames without a kill; a kill resets the clock
        State s;
        std::vector<Target> t{{"a", 20, 0}};
        int fb = -1;
        for (int f = 0; f < cfg.fallbackFrames + 5; ++f)
            if (decide(s, cfg, 0, 0, t, 0).action == Action::Fallback) { fb = f; break; }
        CHECK(fb == cfg.fallbackFrames - 1);
        State s2;
        for (int f = 0; f < cfg.fallbackFrames - 10; ++f) decide(s2, cfg, 0, 0, t, 0);
        decide(s2, cfg, 0, 0, t, 1);  // a kill happened
        bool anyFb = false;
        for (int f = 0; f < cfg.fallbackFrames - 10; ++f) anyFb |= decide(s2, cfg, 0, 0, t, 1).action == Action::Fallback;
        CHECK(!anyFb);
    }
    {   // no targets: nothing, state reset
        State s;
        s.inRangeFrames = 99;
        Decision d = decide(s, cfg, 0, 0, {}, 0);
        CHECK(d.action == Action::None && s.inRangeFrames == 0);
    }
    if (g_failures == 0) std::cout << "auto_combat_test: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
