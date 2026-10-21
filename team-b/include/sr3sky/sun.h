// Sun / time-of-day light direction - spec-tables-environment.md 19.2 (0x00B9E670), the rule
// spec-differential-results.md D-23 checks: from the game-clock hour H and a minimum elevation.
#pragma once

#include <array>

namespace sr3sky {

// `out` as the game writes it: the direction the light TRAVELS (pointing down). The direction toward
// the sun is -out. minElevDeg: 40 for the sun light, 15 for the sky shader, 1 and 90 for two other
// users. yawDeg is 0 in shipped play; `reverse` is the shipped switch (on).
std::array<float, 3> TodLightDirection(float hour, float minElevDeg, float yawDeg = 0.0f, bool reverse = true);

// Toward-the-sun unit vector for the sun light (minimum elevation 40).
std::array<float, 3> SunTowardDirection(float hour);

// The game-clock hour from a 0..1 day fraction.
inline float HourFromDayFraction(double fraction) { return static_cast<float>(fraction * 24.0); }

}  // namespace sr3sky
