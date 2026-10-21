#include "sr3render/tod_lighting.h"

#include <algorithm>
#include <cmath>

#include "sr3xtbl/xtbl.h"

namespace sr3render {

using sr3tables_environment::ColorIntensity;
using sr3tables_environment::WeatherTimeOfDayCell;
using sr3tables_environment::WeatherTimeSegment;

TodColor4 ToTodColor4(const ColorIntensity& ci) {
    TodColor4 c;
    c.r = ci.rgb.r.value;
    c.g = ci.rgb.g.value;
    c.b = ci.rgb.b.value;
    c.intensity = ci.intensity;
    return c;
}

TodColor4 LerpColor4(const TodColor4& a, const TodColor4& b, float t) {
    TodColor4 r;
    r.r = LerpF(a.r, b.r, t);
    r.g = LerpF(a.g, b.g, t);
    r.b = LerpF(a.b, b.b, t);
    r.intensity = LerpF(a.intensity, b.intensity, t);
    return r;
}

float TodFractionFromHHMM(int32_t hhmm) {
    const int32_t hours = hhmm / 100;    // truncating signed division (spec 3.1 CORRECTED)
    const int32_t minutes = hhmm % 100;  // sign follows hhmm's, matching the engine's reading
    float frac = (static_cast<float>(minutes) / 60.0f + static_cast<float>(hours)) / 24.0f;
    // "reduced by 1.0 while strictly greater than 1.0" (spec 3.1): 2400 ->
    // exactly 1.0, kept, never wrapped to 0.
    while (frac > 1.0f) frac -= 1.0f;
    return frac;
}

float TodFractionFromMinutes(int32_t minutes) { return static_cast<float>(minutes) / 1440.0f; }

float ComputeTodSegmentBlend(float t, float highStartFraction, float lowRampOutFraction) {
    float timeToNextStart = highStartFraction - t;
    if (timeToNextStart < 0.0f) timeToNextStart += 1.0f;  // wrap into [0,1)
    if (lowRampOutFraction > 0.0f && timeToNextStart < lowRampOutFraction) {
        return 1.0f - timeToNextStart / lowRampOutFraction;
    }
    return 0.0f;
}

TodSegmentBracket FindTodSegmentBracket(const std::vector<WeatherTimeSegment>& segments, float t) {
    TodSegmentBracket result;
    if (segments.empty()) return result;

    std::vector<const WeatherTimeSegment*> sorted;
    sorted.reserve(segments.size());
    for (const WeatherTimeSegment& s : segments) sorted.push_back(&s);
    std::sort(sorted.begin(), sorted.end(), [](const WeatherTimeSegment* a, const WeatherTimeSegment* b) {
        return TodFractionFromHHMM(a->startTimeHHMM.value) < TodFractionFromHHMM(b->startTimeHHMM.value);
    });

    t = t - std::floor(t);  // wrap into [0,1)
    const size_t n = sorted.size();
    size_t lowIndex = n - 1;  // before the first segment's own start: still inside the LAST
                              // segment's span, which wraps across midnight
    for (size_t i = 0; i < n; ++i) {
        if (TodFractionFromHHMM(sorted[i]->startTimeHHMM.value) <= t) {
            lowIndex = i;
        } else {
            break;  // sorted ascending - the first one exceeding t ends the search
        }
    }
    const size_t highIndex = (lowIndex + 1) % n;
    result.low = sorted[lowIndex];
    result.high = sorted[highIndex];
    const float highStart = TodFractionFromHHMM(result.high->startTimeHHMM.value);
    const float lowRampOut = TodFractionFromMinutes(result.low->rampOutTimeMinutes.value);
    result.segmentBlend = ComputeTodSegmentBlend(t, highStart, lowRampOut);
    return result;
}

const WeatherTimeOfDayCell* FindCellByStageName(const WeatherTimeSegment& segment,
                                                 const std::string& stageName) {
    for (const WeatherTimeOfDayCell& cell : segment.stages) {
        if (cell.stageName.has_value() && sr3xtbl::NameEquals(*cell.stageName, stageName)) {
            return &cell;
        }
    }
    return nullptr;
}

const ColorIntensity& ExtractAmbientColor(const WeatherTimeOfDayCell& cell) {
    return cell.lighting.ambientColor;
}
const ColorIntensity& ExtractFogColor(const WeatherTimeOfDayCell& cell) { return cell.lighting.fogColor; }
const ColorIntensity& ExtractTodLightColor(const WeatherTimeOfDayCell& cell) {
    return cell.lighting.todLightColor;
}

TodColor4 BlendTodCorners(const WeatherTimeOfDayCell& segLowStageA, const WeatherTimeOfDayCell& segHighStageA,
                          const WeatherTimeOfDayCell& segLowStageB, const WeatherTimeOfDayCell& segHighStageB,
                          float segmentBlend, float stageBlend, ColorIntensityExtractor extract) {
    const TodColor4 stageA =
        LerpColor4(ToTodColor4(extract(segLowStageA)), ToTodColor4(extract(segHighStageA)), segmentBlend);
    const TodColor4 stageB =
        LerpColor4(ToTodColor4(extract(segLowStageB)), ToTodColor4(extract(segHighStageB)), segmentBlend);
    return LerpColor4(stageA, stageB, stageBlend);
}

void ComputeChosenSunDirection(float t, float outDirection[3]) {
    t = t - std::floor(t);
    constexpr float kPi = 3.14159265358979323846f;
    const float angle = t * 2.0f * kPi - kPi * 0.5f;
    const float height = std::sin(angle);  // -1 at midnight, +1 at noon (t=0.5)
    float dir[3] = {0.3f, height, -0.8f};
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 0.0f) len = 1.0f;
    outDirection[0] = dir[0] / len;
    outDirection[1] = dir[1] / len;
    outDirection[2] = dir[2] / len;
}

void NormalizeForDisplay(float rgb[3]) {
    float m = 1.0f;
    for (int i = 0; i < 3; ++i) m = std::max(m, rgb[i]);
    rgb[0] /= m;
    rgb[1] /= m;
    rgb[2] /= m;
}

TodLightingResult ComputeTodLighting(const std::vector<WeatherTimeSegment>& segments, float t,
                                     const std::string& chosenWeatherStageName) {
    // Single-stage form: stage A == stage B, so the weather axis is a mathematical no-op.
    return ComputeTodLightingBlended(segments, t, chosenWeatherStageName, chosenWeatherStageName, 0.0f);
}

TodLightingResult ComputeTodLightingBlended(const std::vector<WeatherTimeSegment>& segments, float t,
                                            const std::string& stageAName, const std::string& stageBName,
                                            float stageBlend) {
    TodLightingResult result;  // old-behaviour defaults; overwritten field-group by field-group below
    ComputeChosenSunDirection(t, result.sunDirection);

    const TodSegmentBracket bracket = FindTodSegmentBracket(segments, t);
    if (bracket.low == nullptr || bracket.high == nullptr) return result;  // no segment data - old defaults

    const WeatherTimeOfDayCell* lowA = FindCellByStageName(*bracket.low, stageAName);
    const WeatherTimeOfDayCell* highA = FindCellByStageName(*bracket.high, stageAName);
    const WeatherTimeOfDayCell* lowB = FindCellByStageName(*bracket.low, stageBName);
    const WeatherTimeOfDayCell* highB = FindCellByStageName(*bracket.high, stageBName);
    if (!lowA || !highA || !lowB || !highB) return result;  // a stage is missing here - old defaults

    const TodColor4 ambient =
        BlendTodCorners(*lowA, *highA, *lowB, *highB, bracket.segmentBlend, stageBlend, ExtractAmbientColor);
    const TodColor4 fog =
        BlendTodCorners(*lowA, *highA, *lowB, *highB, bracket.segmentBlend, stageBlend, ExtractFogColor);
    const TodColor4 sun =
        BlendTodCorners(*lowA, *highA, *lowB, *highB, bracket.segmentBlend, stageBlend, ExtractTodLightColor);

    result.ambientColor[0] = ambient.r * ambient.intensity;
    result.ambientColor[1] = ambient.g * ambient.intensity;
    result.ambientColor[2] = ambient.b * ambient.intensity;
    result.skyClearColor[0] = fog.r * fog.intensity;
    result.skyClearColor[1] = fog.g * fog.intensity;
    result.skyClearColor[2] = fog.b * fog.intensity;
    result.sunColor[0] = sun.r * sun.intensity;
    result.sunColor[1] = sun.g * sun.intensity;
    result.sunColor[2] = sun.b * sun.intensity;

    // CHOSEN hue-preserving tonemap stand-in (see header banner) - real
    // shipped TOD_Light_Color intensities run as high as ~16x.
    NormalizeForDisplay(result.ambientColor);
    NormalizeForDisplay(result.skyClearColor);
    NormalizeForDisplay(result.sunColor);
    return result;
}

}  // namespace sr3render
