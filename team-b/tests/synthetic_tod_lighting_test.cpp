// Unit tests for sr3render::{TodFractionFromHHMM, ComputeTodSegmentBlend,
// FindTodSegmentBracket, BlendTodCorners, ComputeChosenSunDirection,
// NormalizeForDisplay, ComputeTodLighting} - the weather/TOD runtime blend
// consumer added this task (2026-10-08, sky/lighting/TOD), implementing
// spec-tables-environment.md Sec18's confirmed blend formulas on top of
// sr3tables_environment's EXISTING, already-tested xtbl readers.
//
// Same "synthetic" convention as tests/synthetic_tables_environment_test.cpp:
// hand-rolled XML fixtures built from the spec's own text / this project's
// own already-confirmed reader schema, never derived from real game data
// (that is read directly, at runtime, by tools/sr3_viewer.cpp's
// runPlaytest() - no fixture here claims to reproduce it). A pass here
// proves this file's OWN blend math matches hand-computed expected values,
// not that the spec is right.
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3render/tod_lighting.h"
#include "sr3tables_environment/weather_time_of_day.h"
#include "sr3xtbl/xtbl.h"

namespace {

using namespace sr3render;
using namespace sr3tables_environment;
using sr3xtbl::Document;
using sr3xtbl::ParseDocument;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ \
                      << "\n";                                                 \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

bool near(float a, float b, float tol = 1e-5f) { return (a > b ? a - b : b - a) <= tol; }

Document P(std::string_view s) { return ParseDocument(s); }

// ===========================================================================
// spec 3.1's HHMM/minutes -> day-fraction conversions.
// ===========================================================================
void testTodFractionFromHHMM() {
    CHECK(near(TodFractionFromHHMM(0), 0.0f));
    CHECK(near(TodFractionFromHHMM(800), 8.0f / 24.0f));
    CHECK(near(TodFractionFromHHMM(1200), 0.5f));
    // 1430 -> hours=14 (truncating signed division), minutes=30 -> (30/60+14)/24.
    CHECK(near(TodFractionFromHHMM(1430), (30.0f / 60.0f + 14.0f) / 24.0f));
    // spec FOR TEAM B 2026-10-01 correction: 2400 stays exactly 1.0, never wraps to 0.
    CHECK(near(TodFractionFromHHMM(2400), 1.0f));

    CHECK(near(TodFractionFromMinutes(0), 0.0f));
    CHECK(near(TodFractionFromMinutes(60), 60.0f / 1440.0f));
    CHECK(near(TodFractionFromMinutes(1440), 1.0f));
}

// ===========================================================================
// The CONFIRMED TOD-segment crossfade formula (spec Sec18).
// ===========================================================================
void testComputeTodSegmentBlend() {
    // Far from the next segment's start: no crossfade yet.
    CHECK(near(ComputeTodSegmentBlend(0.3f, 0.5f, 1.0f / 24.0f), 0.0f));
    // Inside the ramp window, no wraparound needed: t=0.49, high.start=0.5,
    // low.rampOut=1/24 (~0.041667) -> timeToNextStart=0.01 -> blend=1-0.01/(1/24).
    {
        const float rampOut = 1.0f / 24.0f;
        const float expected = 1.0f - 0.01f / rampOut;
        CHECK(near(ComputeTodSegmentBlend(0.49f, 0.5f, rampOut), expected, 1e-4f));
    }
    // Exactly at the next segment's start: timeToNextStart=0 -> blend=1 (fully "high").
    CHECK(near(ComputeTodSegmentBlend(0.5f, 0.5f, 1.0f / 24.0f), 1.0f));
    // Wraparound branch: high.start(0.05) < t(0.97), so the raw difference is
    // negative and must be wrapped by +1.0 before the ramp-window test.
    {
        const float t = 0.97f, highStart = 0.05f, rampOut = 0.1f;
        // raw diff = 0.05-0.97 = -0.92 -> +1.0 = 0.08 -> inside the 0.1 window.
        const float expected = 1.0f - 0.08f / rampOut;
        CHECK(near(ComputeTodSegmentBlend(t, highStart, rampOut), expected, 1e-4f));
    }
    // A zero-width ramp window can never contain a (non-negative) timeToNextStart.
    CHECK(near(ComputeTodSegmentBlend(0.5f, 0.5f, 0.0f), 0.0f));
}

// ===========================================================================
// The full 2x2 bilinear corner blend (spec Sec18) - BlendTodCorners(),
// exercised with 2 DISTINCT weather-stage corners (A colours vs B colours)
// and 2 DISTINCT TOD-segment corners (low vs high), at 2 different blend-
// scalar inputs, each with a hand-computed expected output.
// ===========================================================================
WeatherTimeOfDayCell MakeCellWithAmbient(float r, float g, float b, float intensity) {
    WeatherTimeOfDayCell c;
    c.lighting.ambientColor.present = true;
    c.lighting.ambientColor.rgb.r = {r, true};
    c.lighting.ambientColor.rgb.g = {g, true};
    c.lighting.ambientColor.rgb.b = {b, true};
    c.lighting.ambientColor.intensity = intensity;
    c.lighting.ambientColor.intensityPresent = true;
    return c;
}

void testBlendTodCorners() {
    // Stage A: low=black(i=1), high=red(i=1). Stage B: low=green(i=2), high=blue(i=3).
    const WeatherTimeOfDayCell segLowStageA = MakeCellWithAmbient(0.0f, 0.0f, 0.0f, 1.0f);
    const WeatherTimeOfDayCell segHighStageA = MakeCellWithAmbient(1.0f, 0.0f, 0.0f, 1.0f);
    const WeatherTimeOfDayCell segLowStageB = MakeCellWithAmbient(0.0f, 1.0f, 0.0f, 2.0f);
    const WeatherTimeOfDayCell segHighStageB = MakeCellWithAmbient(0.0f, 0.0f, 1.0f, 3.0f);

    // Input set 1: segmentBlend=0.5, stageBlend=0.25.
    // stageA = lerp(black,red,0.5) = (0.5,0,0,i=1); stageB = lerp(green,blue,0.5) = (0,0.5,0.5,i=2.5)
    // result = lerp(stageA,stageB,0.25) = (0.375,0.125,0.125,i=1.375)
    {
        const TodColor4 r = BlendTodCorners(segLowStageA, segHighStageA, segLowStageB, segHighStageB, 0.5f, 0.25f,
                                            ExtractAmbientColor);
        CHECK(near(r.r, 0.375f));
        CHECK(near(r.g, 0.125f));
        CHECK(near(r.b, 0.125f));
        CHECK(near(r.intensity, 1.375f));
    }
    // Input set 2: segmentBlend=0.0, stageBlend=1.0 -> exactly segLowStageB
    // (green, i=2): a bilinear blend at an exact corner must reproduce that
    // corner exactly, not merely approximately.
    {
        const TodColor4 r = BlendTodCorners(segLowStageA, segHighStageA, segLowStageB, segHighStageB, 0.0f, 1.0f,
                                            ExtractAmbientColor);
        CHECK(near(r.r, 0.0f));
        CHECK(near(r.g, 1.0f));
        CHECK(near(r.b, 0.0f));
        CHECK(near(r.intensity, 2.0f));
    }
    // Input set 3: segmentBlend=1.0, stageBlend=0.0 -> exactly segHighStageA (red, i=1).
    {
        const TodColor4 r = BlendTodCorners(segLowStageA, segHighStageA, segLowStageB, segHighStageB, 1.0f, 0.0f,
                                            ExtractAmbientColor);
        CHECK(near(r.r, 1.0f));
        CHECK(near(r.g, 0.0f));
        CHECK(near(r.b, 0.0f));
        CHECK(near(r.intensity, 1.0f));
    }
}

// ===========================================================================
// Segment bracketing (circular, sorts a local copy - the caller's vector
// order must not matter).
// ===========================================================================
void testFindTodSegmentBracket() {
    std::vector<WeatherTimeSegment> segs(3);
    // Deliberately out of Start_Time order, to prove the sort is this
    // consumer's own job, not an assumption about caller order.
    segs[0].startTimeHHMM = {1900, true};  // sunset
    segs[0].rampOutTimeMinutes = {60, true};
    segs[1].startTimeHHMM = {500, true};  // night
    segs[1].rampOutTimeMinutes = {60, true};
    segs[2].startTimeHHMM = {1200, true};  // noon
    segs[2].rampOutTimeMinutes = {60, true};

    // t=0.3 (~07:12) falls between "night" (500 -> 5/24=0.2083) and "noon" (1200 -> 0.5).
    {
        const TodSegmentBracket b = FindTodSegmentBracket(segs, 0.3f);
        CHECK(b.low != nullptr && b.low->startTimeHHMM.value == 500);
        CHECK(b.high != nullptr && b.high->startTimeHHMM.value == 1200);
    }
    // t=0.05 (before "night"'s own 0.2083 start) must wrap to the LAST
    // segment ("sunset", 1900) bracketing forward to the FIRST ("night", 500).
    {
        const TodSegmentBracket b = FindTodSegmentBracket(segs, 0.05f);
        CHECK(b.low != nullptr && b.low->startTimeHHMM.value == 1900);
        CHECK(b.high != nullptr && b.high->startTimeHHMM.value == 500);
    }
    // Empty input: both null, blend 0 - never a crash or a guessed bracket.
    {
        std::vector<WeatherTimeSegment> empty;
        const TodSegmentBracket b = FindTodSegmentBracket(empty, 0.5f);
        CHECK(b.low == nullptr && b.high == nullptr);
        CHECK(b.segmentBlend == 0.0f);
    }
}

// ===========================================================================
// CHOSEN sun direction (NOT spec-derived - see tod_lighting.h's banner).
// ===========================================================================
void testComputeChosenSunDirection() {
    float dir[3];
    ComputeChosenSunDirection(0.5f, dir);  // noon: raw height = sin(pi/2) = +1 (highest point)
    const float lenNoon = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    CHECK(near(lenNoon, 1.0f, 1e-4f));  // always a unit vector
    // Normalised y = 1/sqrt(0.3^2+1^2+0.8^2) = 1/sqrt(1.73) ~= 0.7605 - near-
    // maximal, not near 1.0, since the fixed x/z components still contribute
    // to the vector's length after normalisation.
    CHECK(near(dir[1], 1.0f / std::sqrt(1.73f), 1e-3f));
    CHECK(dir[1] > 0.7f);

    ComputeChosenSunDirection(0.0f, dir);  // midnight: raw height = sin(-pi/2) = -1 (lowest point)
    CHECK(near(dir[1], -1.0f / std::sqrt(1.73f), 1e-3f));
    CHECK(dir[1] < -0.7f);

    // t wraps the same way at 1.0 as at 0.0 (both "midnight").
    float dirAtOne[3];
    ComputeChosenSunDirection(1.0f, dirAtOne);
    CHECK(near(dirAtOne[1], dir[1], 1e-3f));
}

void testNormalizeForDisplay() {
    float rgb1[3] = {0.5f, 0.2f, 0.1f};
    NormalizeForDisplay(rgb1);  // already <= 1: unchanged
    CHECK(near(rgb1[0], 0.5f) && near(rgb1[1], 0.2f) && near(rgb1[2], 0.1f));

    float rgb2[3] = {2.0f, 1.0f, 0.5f};
    NormalizeForDisplay(rgb2);  // divides by the brightest channel (2.0), preserving hue ratio
    CHECK(near(rgb2[0], 1.0f) && near(rgb2[1], 0.5f) && near(rgb2[2], 0.25f));
}

// ===========================================================================
// End-to-end ComputeTodLighting(): a tiny 2-segment, 1-weather-stage real-
// shaped fixture (same element paths as
// tests/synthetic_tables_environment_test.cpp's own testWeatherTimeOfDay).
// ===========================================================================
void testComputeTodLightingEndToEnd() {
    Document d = P(R"(<root><Table>
      <Weather_Time_Segment><Name>Night</Name><Start_Time>0</Start_Time><Ramp_Out_Time>60</Ramp_Out_Time>
        <Weather_Stages>
          <Stage><Stage_Name>Clear</Stage_Name>
            <District_Lighting><Lighting_Parameters>
              <Ambient_Color><R>0.1</R><G>0.1</G><B>0.2</B></Ambient_Color>
              <Fog_Color><R>0.05</R><G>0.05</G><B>0.1</B></Fog_Color>
              <TOD_Light_Color><R>0.2</R><G>0.2</G><B>0.3</B></TOD_Light_Color>
            </Lighting_Parameters></District_Lighting>
          </Stage>
        </Weather_Stages>
      </Weather_Time_Segment>
      <Weather_Time_Segment><Name>Noon</Name><Start_Time>1200</Start_Time><Ramp_Out_Time>60</Ramp_Out_Time>
        <Weather_Stages>
          <Stage><Stage_Name>Clear</Stage_Name>
            <District_Lighting><Lighting_Parameters>
              <Ambient_Color><R>0.8</R><G>0.8</G><B>0.9</B></Ambient_Color>
              <Fog_Color><R>0.5</R><G>0.7</G><B>0.9</B></Fog_Color>
              <TOD_Light_Color><R>1.0</R><G>1.0</G><B>0.9</B></TOD_Light_Color>
            </Lighting_Parameters></District_Lighting>
          </Stage>
        </Weather_Stages>
      </Weather_Time_Segment>
    </Table></root>)");
    std::vector<WeatherTimeSegment> segs = ParseWeatherTimeOfDayTable(d);
    CHECK(segs.size() == 2);

    // t=0.48 is inside "Night"'s 60-minute (1/24 fraction) ramp window before
    // "Noon"'s own 0.5 start: timeToNextStart=0.02 -> blend=1-0.02/(60/1440)=0.52.
    {
        const TodLightingResult r = ComputeTodLighting(segs, 0.48f, "Clear");
        const float blend = 0.52f;
        CHECK(near(r.ambientColor[0], 0.1f + (0.8f - 0.1f) * blend, 1e-4f));
        CHECK(near(r.ambientColor[2], 0.2f + (0.9f - 0.2f) * blend, 1e-4f));
        CHECK(near(r.skyClearColor[1], 0.05f + (0.7f - 0.05f) * blend, 1e-4f));
        CHECK(near(r.sunColor[0], 0.2f + (1.0f - 0.2f) * blend, 1e-4f));
        // Every channel stayed <= 1.0, so NormalizeForDisplay was a no-op here.
        CHECK(r.ambientColor[0] <= 1.0f && r.skyClearColor[0] <= 1.0f && r.sunColor[0] <= 1.0f);
    }
    // A chosen stage name with no matching cell in these segments: honest
    // fallback to TodLightingResult's own old-behaviour defaults (NOT a
    // guessed/fabricated colour) - only sunDirection (which never depends on
    // table data) is still computed.
    {
        const TodLightingResult def;  // default-constructed, for comparison
        const TodLightingResult r = ComputeTodLighting(segs, 0.48f, "NoSuchStage");
        CHECK(near(r.ambientColor[0], def.ambientColor[0]));
        CHECK(near(r.skyClearColor[0], def.skyClearColor[0]));
        CHECK(near(r.sunColor[0], def.sunColor[0]));
    }
    // Empty segment list: same honest fallback.
    {
        const TodLightingResult def;
        std::vector<WeatherTimeSegment> empty;
        const TodLightingResult r = ComputeTodLighting(empty, 0.48f, "Clear");
        CHECK(near(r.ambientColor[0], def.ambientColor[0]));
    }
}

}  // namespace

int main() {
    testTodFractionFromHHMM();
    testComputeTodSegmentBlend();
    testBlendTodCorners();
    testFindTodSegmentBracket();
    testComputeChosenSunDirection();
    testNormalizeForDisplay();
    testComputeTodLightingEndToEnd();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
