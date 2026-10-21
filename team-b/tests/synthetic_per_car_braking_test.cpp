// Synthetic test for sr3airuntime::PerCarBrakingDecel (include/sr3airuntime/traffic_following.h): the per-car a_b pick
// (Sec20.6.3(b)) from an optional vehicle-row AI_Max_Braking_Decel with a CHOSEN fallback.
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>

#include "sr3airuntime/traffic_following.h"

static int g_fail = 0;
#define CHECK(c)                                                           \
    do {                                                                   \
        if (!(c)) {                                                        \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);               \
            ++g_fail;                                                      \
        }                                                                  \
    } while (0)

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    using sr3airuntime::PerCarBrakingDecel;
    bool used = false;
    CHECK(Near(PerCarBrakingDecel(9.5, 12.06, 0, &used), 9.5) && used);                 // row value wins
    CHECK(Near(PerCarBrakingDecel(9.5, 12.06, 1, &used), 9.5 * 0.8) && used);           // condition scale applies
    CHECK(Near(PerCarBrakingDecel(std::nullopt, 12.06, 0, &used), 12.06) && !used);     // no model / no row -> fallback
    CHECK(Near(PerCarBrakingDecel(0.0, 12.06, 0, &used), 12.06) && !used);              // zero is "missing"
    CHECK(Near(PerCarBrakingDecel(-3.0, 12.06, 0, &used), 12.06) && !used);             // negative is unusable
    CHECK(Near(PerCarBrakingDecel(std::numeric_limits<double>::quiet_NaN(), 12.06, 0, &used), 12.06) && !used);
    CHECK(Near(PerCarBrakingDecel(std::nullopt, 12.06, 4), 12.06 * 0.5));               // usedRow pointer optional
    CHECK(Near(PerCarBrakingDecel(12.06, 5.0, 0), sr3airuntime::ConditionScaledBrakingDecel(12.06, 0)));
    if (g_fail) return 1;
    std::printf("synthetic_per_car_braking_test: PASS\n");
    return 0;
}
