// Sun / TOD light direction. See include/sr3sky/sun.h.

#include "sr3sky/sun.h"

#include <algorithm>
#include <cmath>

namespace sr3sky {

std::array<float, 3> TodLightDirection(float hour, float minElevDeg, float yawDeg, bool reverse) {
    constexpr float kDeg = 0.017453f;
    constexpr float kPi = 3.14159265358979f;
    const float e = minElevDeg * kDeg;
    float t = hour - 7.5f;
    if (t < 0.0f) t += 24.0f;
    t = t / 12.0f;
    if (reverse) t = 1.0f - t;
    const float a = (t - 1.0f) * kPi;
    const float y = std::min(std::max(std::fabs(std::sin(a)), std::sin(e)), 1.0f);
    float v[3] = {-std::cos(a), y, 0.53f * std::sin(a)};
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (float& c : v) c /= len;
    const float yaw = yawDeg * kDeg, s = std::sin(yaw), c = std::cos(yaw);
    const float x = v[2] * s + v[0] * c;
    const float z = v[2] * c - v[0] * s;
    return {-x, -v[1], -z};
}

std::array<float, 3> SunTowardDirection(float hour) {
    const auto out = TodLightDirection(hour, 40.0f);
    return {-out[0], -out[1], -out[2]};
}

}  // namespace sr3sky
