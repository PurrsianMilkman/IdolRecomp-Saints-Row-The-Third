// Unit tests for sr3render::{kTodSecondsPerDay, kTodHoursPerTKeyPress,
// AdvanceTodFraction, JumpTodFractionHours, TodFractionToHourMinute}
// (include/sr3render/tod_clock.h, v0.3 polish task, 2026-10-09: "slow the
// TOD cycle + add a time-skip key"). These are the exact pure-math pieces
// tools/sr3_viewer.cpp's runPlaytest() calls every frame (the per-frame TOD
// advance) and on every edge-triggered `T` keypress (the manual time-skip) -
// extracted to this header specifically so they have real test coverage,
// not a mirrored copy (same reasoning tests/synthetic_minimap_live_transform_test.cpp's
// own top comment gives for why include/sr3luahost/minimap_live_transform.h
// got a header instead of staying inline in a demo function). No game data,
// no D3D11 device - pure double arithmetic.
#include <cmath>
#include <iostream>

#include "sr3render/tod_clock.h"

namespace {

using namespace sr3render;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ \
                      << "\n";                                                 \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// --- the CHOSEN day-length constant itself: "roughly one game hour per real
// minute" (the orchestrator's own framing, after flagging the ORIGINAL 20.0
// - a full in-game day every 20 REAL seconds - as far too fast for actual
// human play) means 24 game-hours over 24 real-MINUTES, i.e. 1440 real
// seconds per in-game day. -------------------------------------------------
void testDayLengthConstant() {
    CHECK(near(kTodSecondsPerDay, 1440.0));
    CHECK(near(kTodSecondsPerDay / 60.0, 24.0));             // 24 REAL minutes per in-game day
    CHECK(near(kTodSecondsPerDay / 24.0, 60.0));             // -> exactly one REAL minute per in-game hour
}

// --- AdvanceTodFraction(): the exact per-frame formula runPlaytest()'s own
// frame loop applies (todFraction += dt/secondsPerDay; wrap into [0,1)). ---
void testAdvanceTodFraction() {
    // One in-game hour of real time (kTodSecondsPerDay/24 seconds) advances
    // the fraction by exactly 1/24.
    const double oneHourSeconds = kTodSecondsPerDay / 24.0;
    CHECK(near(AdvanceTodFraction(0.5, oneHourSeconds, kTodSecondsPerDay), 0.5 + 1.0 / 24.0));
    // A full in-game day's worth of real seconds returns to the SAME
    // fraction (wrap), not 1.0 or beyond.
    CHECK(near(AdvanceTodFraction(0.5, kTodSecondsPerDay, kTodSecondsPerDay), 0.5));
    // Starting near the wrap boundary and advancing past midnight wraps
    // into [0,1), never negative or >=1.
    const double justBeforeMidnight = 1.0 - 1e-6;
    const double advanced = AdvanceTodFraction(justBeforeMidnight, oneHourSeconds, kTodSecondsPerDay);
    CHECK(advanced >= 0.0 && advanced < 1.0);
    CHECK(near(advanced, (justBeforeMidnight + 1.0 / 24.0) - 1.0));
    // dt=0 (the very first frame, or a paused/degenerate frame this file's
    // own "if (dt > 0.25 || dt < 0.0) dt = 0.0" guard can produce) is a
    // true no-op.
    CHECK(near(AdvanceTodFraction(0.37, 0.0, kTodSecondsPerDay), 0.37));
}

// --- JumpTodFractionHours(): the `T` key's own per-press skip. ------------
void testJumpTodFractionHours() {
    // Default per-press amount (kTodHoursPerTKeyPress) from noon lands at
    // 15:00 (noon + 3h).
    CHECK(near(kTodHoursPerTKeyPress, 3.0));
    double t = JumpTodFractionHours(0.5, kTodHoursPerTKeyPress);
    int h = -1, m = -1;
    TodFractionToHourMinute(t, h, m);
    CHECK(h == 15 && m == 0);
    // Repeated presses accumulate (same wrap convention as the per-frame
    // advance above) - 8 presses of 3h = 24h = back to the SAME fraction.
    double t2 = 0.5;
    for (int i = 0; i < 8; ++i) t2 = JumpTodFractionHours(t2, kTodHoursPerTKeyPress);
    CHECK(near(t2, 0.5));
    // A single press that crosses midnight wraps correctly: 23:00 + 3h ->
    // 02:00 the next day, not 26:00.
    double t3 = JumpTodFractionHours(23.0 / 24.0, kTodHoursPerTKeyPress);
    int h3 = -1, m3 = -1;
    TodFractionToHourMinute(t3, h3, m3);
    CHECK(h3 == 2 && m3 == 0);
}

// --- TodFractionToHourMinute(): the console-line readout the `T` key's own
// handler prints every press. ----------------------------------------------
void testTodFractionToHourMinute() {
    struct Case { double fraction; int wantHour, wantMinute; };
    const Case cases[] = {
        {0.0, 0, 0},
        {0.5, 12, 0},
        {0.25, 6, 0},
        {0.75, 18, 0},
        {0.5 + 1.0 / 24.0, 13, 0},       // noon + 1h
        {14.0 / 24.0, 14, 0},            // exact hour, no rounding needed
        {(14.0 * 60.0 + 30.0) / (24.0 * 60.0), 14, 30},  // 14:30
    };
    for (const Case& c : cases) {
        int h = -1, m = -1;
        TodFractionToHourMinute(c.fraction, h, m);
        CHECK(h == c.wantHour && m == c.wantMinute);
    }
    // The minute==60 carry: a fraction that rounds to the NEXT hour's :00
    // must carry hour forward (mod 24), never print "HH:60".
    int hCarry = -1, mCarry = -1;
    TodFractionToHourMinute(23.0 / 24.0 + 0.0099 / 24.0, hCarry, mCarry);  // 23:00 + ~0.6min, rounds up to 23:01 (sanity: no carry here)
    CHECK(mCarry != 60);
    // Construct a fraction deliberately just under the next hour so the
    // nearest-minute rounding pushes minute to exactly 60 pre-carry.
    const double almostNextHour = (23.0 * 60.0 + 59.99) / (24.0 * 60.0);
    int hWrap = -1, mWrap = -1;
    TodFractionToHourMinute(almostNextHour, hWrap, mWrap);
    CHECK(mWrap != 60);
    CHECK(hWrap == 0 && mWrap == 0);  // 23:59.99 rounds to 24:00 -> carries to 00:00 the next day
}

}  // namespace

int main() {
    testDayLengthConstant();
    testAdvanceTodFraction();
    testJumpTodFractionHours();
    testTodFractionToHourMinute();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
