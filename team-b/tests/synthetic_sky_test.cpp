// sr3sky against spec-tables-environment.md 19.2's vectors and spec-differential-results.md D-23's
// table (the original game's output, emulated by Team D).
#include <cmath>
#include <iostream>

#include "sr3sky/sun.h"
#include "sr3sky/tod_curves.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK_TRUE(c) do { ++g_checks; if (!(c)) { ++g_failures; std::cerr << "CHECK FAILED: " #c "\n"; } } while (0)

void Near3(const std::array<float, 3>& got, float x, float y, float z, const char* what) {
    ++g_checks;
    if (std::fabs(got[0] - x) > 2e-4f || std::fabs(got[1] - y) > 2e-4f || std::fabs(got[2] - z) > 2e-4f) {
        ++g_failures;
        std::cerr << "MISMATCH " << what << ": got (" << got[0] << ", " << got[1] << ", " << got[2] << ") expected ("
                  << x << ", " << y << ", " << z << ")\n";
    }
}


void Near(float got, float want, const char* what) {
    ++g_checks;
    if (std::fabs(got - want) > 1e-4f) {
        ++g_failures;
        std::cerr << "MISMATCH " << what << ": got " << got << " expected " << want << "\n";
    }
}

}  // namespace

int main() {
    using sr3sky::TodLightDirection;
    // 19.2 table: E = 15 / 40 / 1.
    Near3(TodLightDirection(0.0f, 15), -0.34369f, -0.82975f, -0.43977f, "H0 E15");
    Near3(TodLightDirection(6.0f, 15), 0.90544f, -0.37505f, -0.19877f, "H6 E15");
    Near3(TodLightDirection(6.0f, 40), 0.80786f, -0.56206f, -0.17735f, "H6 E40");
    Near3(TodLightDirection(6.0f, 1), 0.90544f, -0.37505f, -0.19877f, "H6 E1");
    Near3(TodLightDirection(7.5f, 15), 0.96810f, -0.25056f, 0.0f, "H7.5 E15");
    Near3(TodLightDirection(7.5f, 40), 0.84121f, -0.54071f, 0.0f, "H7.5 E40");
    Near3(TodLightDirection(7.5f, 1), 0.99985f, -0.01745f, 0.0f, "H7.5 E1");
    Near3(TodLightDirection(12.0f, 15), 0.34369f, -0.82975f, 0.43977f, "H12");
    Near3(TodLightDirection(13.5f, 40), 0.0f, -0.88357f, 0.46829f, "H13.5");
    Near3(TodLightDirection(18.0f, 15), -0.90544f, -0.37505f, 0.19877f, "H18 E15");
    Near3(TodLightDirection(18.0f, 40), -0.80786f, -0.56206f, 0.17735f, "H18 E40");
    // D-23 (sun light, E = 40).
    const struct {
        float h, x, y, z;
    } d23[] = {{0, -0.34369f, -0.82975f, -0.43977f},  {3, 0.34369f, -0.82975f, -0.43977f},
               {6, 0.80786f, -0.56206f, -0.17735f},   {7.5f, 0.84121f, -0.54071f, 0.0f},
               {9, 0.80786f, -0.56206f, 0.17735f},    {12, 0.34369f, -0.82975f, 0.43977f},
               {13.5f, 0.0f, -0.88357f, 0.46829f},    {15, -0.34369f, -0.82975f, 0.43977f},
               {18, -0.80786f, -0.56206f, 0.17735f},  {19.5f, -0.84121f, -0.54071f, 0.0f},
               {21, -0.80786f, -0.56206f, -0.17735f}};
    for (const auto& v : d23) Near3(TodLightDirection(v.h, 40), v.x, v.y, v.z, "D-23");
    // Toward the sun is -out; Team A's emulation vector 1 with yaw 0, H 12, E 40.
    Near3(sr3sky::SunTowardDirection(12.0f), -0.34369f, 0.82975f, -0.43977f, "toward H12");
    // 19.1 resample (0x00BA36E0) and per-frame read (0x00BA2E30) vectors: keys 10 = 0, 19 = 0.6, 29 = 0, 42 = 0.
    {
        std::map<int, sr3sky::Value4> keys = {{10, {0}}, {19, {0.6f}}, {29, {0}}, {42, {0}}};
        auto s = sr3sky::ResampleTodKeys(keys);
        Near(s[24][0], 0.3f, "slot 24");
        Near(s[45][0], 0.0f, "slot 45 (0 -> 0)");
        keys[42] = {1.0f};
        s = sr3sky::ResampleTodKeys(keys);
        Near(s[45][0], 0.8f, "slot 45 w 0.2");
        Near(s[47][0], 0.66667f, "slot 47 w 5/15");
        Near(s[0][0], 0.66667f, "slot 0 w 5/15");
        sr3sky::TodCurve c;
        c.components = 1;
        c.slots = s;
        Near(sr3sky::SampleTodCurve(c, 12.25f)[0], 0.27f, "H 12.25");
        Near(sr3sky::SampleTodCurve(c, 23.9f)[0], 0.66667f, "H 23.9");
        Near(sr3sky::ResampleTodKeys({{7, {0.5f}}})[30][0], 0.5f, "one key");
        Near(sr3sky::ResampleTodKeys({}, {0.25f})[3][0], 0.25f, "no key: default");
    }
    {
        const auto doc = sr3xtbl::ParseDocument(
            "<root><ambient_color><key><time>19</time><value>0.5 0.5 1.0 2.0</value></key>"
            "<key><time>29</time><value>1.0 1.0 1.0 2.0</value></key></ambient_color>"
            "<fog_density_new><key><time>10</time><value>0.001</value></key></fog_density_new></root>");
        const auto curves = sr3sky::TodBaseCurves::Parse(doc);
        CHECK_TRUE(curves.size() == 2 && curves.has("AMBIENT_COLOR"));
        const auto a = curves.colour("ambient_color", 12.0f);  // slot 24: midpoint of 19 and 29
        CHECK_TRUE(a.has_value());
        if (a) Near((*a)[0], 1.5f, "ambient r x I at 12:00");
        Near(curves.sample("fog_density_new", 3.0f).value_or(sr3sky::Value4{})[0], 0.001f, "single-key float");
        CHECK_TRUE(!curves.sample("missing", 1.0f));
    }
    {
        // D-24 table: base (9,9,9,9), from (1,2,3,4), to (5,6,7,8), t 0.25.
        const sr3sky::Value4 base{9, 9, 9, 9}, from{1, 2, 3, 4}, to{5, 6, 7, 8};
        auto n4 = [&](const sr3sky::Value4& v, float a, float b, float c, float d, const char* w) {
            Near3({v[0], v[1], v[2]}, a, b, c, w);
            Near(v[3], d, w);
        };
        n4(sr3sky::MergeWeatherField(base, &from, &to, 0.25f), 2, 3, 4, 5, "D-24 from+to");
        n4(sr3sky::MergeWeatherField(base, &from, nullptr, 0.25f), 3, 3.75f, 4.5f, 5.25f, "D-24 from only");
        n4(sr3sky::MergeWeatherField(base, nullptr, &to, 0.25f), 8, 8.25f, 8.5f, 8.75f, "D-24 to only");
        n4(sr3sky::MergeWeatherField(base, nullptr, nullptr, 0.25f), 9, 9, 9, 9, "D-24 neither");
        n4(sr3sky::MergeWeatherField(base, &from, &to, 0.25f, false), 9, 9, 9, 9, "D-24 script weather");
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
