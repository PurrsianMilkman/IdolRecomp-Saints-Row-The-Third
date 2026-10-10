#pragma once

#include <cmath>

// Playtest TOD clock advance/jump math (v0.3 polish task, 2026-10-09: "slow
// the TOD cycle + add a time-skip key") - the pure per-frame/per-keypress
// arithmetic tools/sr3_viewer.cpp's runPlaytest() uses to drive its own
// CHOSEN todFraction clock (see sr3render/tod_lighting.h's own doc comment
// for what todFraction itself feeds into - ComputeTodLighting()). Pulled out
// to this header rather than left inline in runPlaytest() (a window/D3D-
// coupled function in a monolithic .cpp) so tests/synthetic_tod_clock_test.cpp
// can link and exercise the REAL formula directly, not a mirrored copy -
// same reasoning include/sr3luahost/minimap_live_transform.h's own top
// comment gives for why THAT transform got a header instead of staying
// inline.
//
// kTodSecondsPerDay (CHOSEN, NOT spec-derived): spec-tables-environment.md
// Sec18 ("Weather/TOD runtime: state machine, blend formulas, per-frame
// consumer chain") traces the real TOD-fraction advance/crossfade mechanism
// in full (0x005A6270 stage picker / 0x005A8320 per-frame tick / 0x005A23F0
// clock advance / 0x00BA8540 corner blend / ...) but never binds any of it
// to a real-world-seconds-per-day constant - re-checked directly against
// that section before choosing this value, not assumed. 1440.0 (24 REAL
// minutes per in-game day, i.e. "roughly one game hour per real minute") is
// this project's own CHOSEN value, replacing an earlier CHOSEN 20.0 (one
// in-game day per 20 REAL seconds, sky-lighting-tod task) that the
// orchestrator flagged as far too fast for actual play after reviewing the
// vehicle-world-collision playtest build.
//
// kTodHoursPerTKeyPress (CHOSEN, this task): the new `T` key's own per-press
// time-skip amount, runPlaytest()'s own manual fast-forward control. No
// spec-confirmed "skip time" input exists anywhere in this project's tables -
// CHOSEN purely so a human playtester can see a lighting change on demand
// without waiting out the slower kTodSecondsPerDay cycle above in real time.
namespace sr3render {

constexpr double kTodSecondsPerDay = 1440.0;
constexpr double kTodHoursPerTKeyPress = 3.0;

// Advances `todFraction` by one frame's worth of the per-frame TOD clock (dt
// real seconds / secondsPerDay) and wraps back into [0,1) - the exact
// formula runPlaytest()'s own frame loop applies every frame.
inline double AdvanceTodFraction(double todFraction, double dt, double secondsPerDay) {
    todFraction += dt / secondsPerDay;
    todFraction -= std::floor(todFraction);
    return todFraction;
}

// Jumps `todFraction` forward by `hours` in-game hours (the `T` key's own
// per-press skip) and wraps back into [0,1) - same "-= floor()" wrap
// convention AdvanceTodFraction() above already uses (this project's own
// existing idiom, not a new one invented for this task).
inline double JumpTodFractionHours(double todFraction, double hours) {
    todFraction += hours / 24.0;
    todFraction -= std::floor(todFraction);
    return todFraction;
}

// todFraction -> a plain 24-hour clock readout (0.5 -> 12:00), for the `T`
// key's own per-press console line (runPlaytest()'s own stdout, so a human
// playtester can see the new time without needing a capture). CHOSEN
// rounding: nearest minute, with a minute==60 carry into the next hour (mod
// 24) so e.g. todFraction's own floating-point rounding never prints a
// literal "23:60".
inline void TodFractionToHourMinute(double todFraction, int& hour, int& minute) {
    const double hoursFloat = todFraction * 24.0;
    hour = static_cast<int>(hoursFloat);
    minute = static_cast<int>((hoursFloat - static_cast<double>(hour)) * 60.0 + 0.5);
    if (minute >= 60) {
        minute = 0;
        hour = (hour + 1) % 24;
    }
}

} // namespace sr3render
