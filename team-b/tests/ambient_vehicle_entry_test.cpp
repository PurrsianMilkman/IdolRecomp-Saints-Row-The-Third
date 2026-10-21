// Tests for include/sr3player/ambient_vehicle_entry.h (spec-player-runtime.md Sec13.3/13.4 selection rules).
#include <cstdio>

#include "sr3player/ambient_vehicle_entry.h"

using namespace sr3player;

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

static AmbientEntryCandidate C(uint64_t id, double x, double z, bool occ = false, bool ok = true) {
    AmbientEntryCandidate c;
    c.id = id; c.x = x; c.z = z; c.occupied = occ; c.eligible = ok;
    return c;
}

int main() {
    // EC-2: the fast flag buys ONE retry across both candidates (the engine clears the request bit on the retry).
    {
        int calls = 0;
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 1, 0), C(2, 2, 0)}, 3.0, 0, true,
                                        [&](const AmbientEntryCandidate&) { ++calls; return false; });
        CHECK(p.index == -1);
        CHECK(calls == 3);  // cand 1, its single retry, cand 2 (no retry)
        calls = 0;
        PickAmbientEntryTarget(0, 0, {C(1, 1, 0), C(2, 2, 0)}, 3.0, 0, false,
                               [&](const AmbientEntryCandidate&) { ++calls; return false; });
        CHECK(calls == 2);
    }
    // empty / out of range
    CHECK(PickAmbientEntryTarget(0, 0, {}, 3.0).index == -1);
    CHECK(PickAmbientEntryTarget(0, 0, {C(1, 10, 0)}, 3.0).index == -1);
    CHECK(PickAmbientEntryTarget(0, 0, {C(1, 1, 0)}, 0.0).index == -1);  // no range -> nothing
    // exactly at max range qualifies (squared compare, > rejects)
    CHECK(PickAmbientEntryTarget(0, 0, {C(1, 3, 0)}, 3.0).index == 0);

    // nearest wins, regardless of vector order
    {
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 2.5, 0), C(2, 1.0, 1.0), C(3, 0, 2.0)}, 3.0);
        CHECK(p.index == 1);
        CHECK(p.dist > 1.41 && p.dist < 1.42);
        CHECK(!p.carjack);
    }
    // carjack flag follows the pick's occupant
    CHECK(PickAmbientEntryTarget(0, 0, {C(1, 1, 0, true)}, 3.0).carjack);
    CHECK(!PickAmbientEntryTarget(0, 0, {C(1, 1, 0, false)}, 3.0).carjack);

    // only the nearest TWO are validated: nearest + 2nd ineligible, 3rd eligible -> nothing
    {
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 1, 0, false, false), C(2, 2, 0, false, false), C(3, 2.5, 0)}, 3.0);
        CHECK(p.index == -1);
    }
    // nearest ineligible, 2nd eligible -> 2nd
    {
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 1, 0, false, false), C(2, 2, 0)}, 3.0);
        CHECK(p.index == 1);
    }
    // sticky candidate sorts first even when farther
    {
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 0.5, 0), C(2, 2.5, 0)}, 3.0, /*sticky=*/2);
        CHECK(p.index == 1);
    }
    // sticky candidate that is out of range is not resurrected
    {
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 0.5, 0), C(2, 9, 0)}, 3.0, /*sticky=*/2);
        CHECK(p.index == 0);
    }
    // sticky but ineligible falls through to the next slot
    {
        auto p = PickAmbientEntryTarget(0, 0, {C(1, 0.5, 0), C(2, 2.5, 0, false, false)}, 3.0, 2);
        CHECK(p.index == 0);
    }
    // fast flag retries the same candidate once (validator called twice for a refused one), no flag = once
    {
        int calls = 0;
        std::vector<AmbientEntryCandidate> v{C(1, 1, 0)};
        auto p = PickAmbientEntryTarget(0, 0, v, 3.0, 0, true, [&](const AmbientEntryCandidate&) { return ++calls >= 2; });
        CHECK(p.index == 0 && calls == 2);
        calls = 0;
        p = PickAmbientEntryTarget(0, 0, v, 3.0, 0, false, [&](const AmbientEntryCandidate&) { return ++calls >= 2; });
        CHECK(p.index == -1 && calls == 1);
    }
    // distance is ground-plane and symmetric around the player
    {
        auto p = PickAmbientEntryTarget(100, -50, {C(1, 101, -50), C(2, 100, -52.5)}, 3.0);
        CHECK(p.index == 0 && p.dist > 0.99 && p.dist < 1.01);
    }
    // stats
    {
        AmbientEntryStats s;
        s.AddDrive(3, 4);
        s.AddDrive(0, 5);
        CHECK(s.distanceDriven > 9.99 && s.distanceDriven < 10.01);
    }
    if (g_fail == 0) std::printf("ambient_vehicle_entry_test: PASS\n");
    return g_fail == 0 ? 0 : 1;
}
