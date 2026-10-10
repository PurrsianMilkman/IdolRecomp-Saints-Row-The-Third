#pragma once

// Per-frame weather/time-of-day lighting blend - the RUNTIME CONSUMER side
// of weather.xtbl/weather_time_of_day.xtbl (the DATA side already exists:
// include/sr3tables_environment/{tables.h,weather_time_of_day.h},
// src/tables_environment.cpp - this file does not re-parse anything).
//
// Source: spec-tables-environment.md Sec18 ("Weather/TOD runtime: state
// machine, blend formulas, per-frame consumer chain", 2026-10-08) plus its
// cross-references in spec-render-pipeline.md Sec20.10/Sec20.12.7. Nothing
// here was derived from the game executable or disassembly directly - every
// address/offset cited in a comment is copied from the spec's own prose, as
// with every other file in this project's clean-room boundary.
//
// WHAT IS SPEC-CONFIRMED (Sec18), implemented faithfully here:
//   * The TOD-segment crossfade formula: given the bracketing segment pair
//     [low, high] by current TOD fraction t, timeToNextStart =
//     high.Start_Time - t (wrapped into [0,1)); if that falls inside
//     low.Ramp_Out_Time, blend = 1 - timeToNextStart/low.Ramp_Out_Time, else
//     0 -> ComputeTodSegmentBlend()/FindTodSegmentBracket() below.
//   * The mechanism is a 2x2 "bilinear corner cell" selection: four
//     weather_time_of_day cell pointers (segment-low x stage-A, segment-high
//     x stage-A, segment-low x stage-B, segment-high x stage-B) plus the two
//     blend scalars (TOD-segment blend above, and a separate weather-STAGE
//     blend) -> BlendTodCorners() below implements exactly this shape,
//     generically over whichever ColorIntensity field the caller selects.
//   * Each field is blended by ONE small generic primitive, confirmed to
//     operate on colour/Vec4 fields as a 4-float unit (RGB + intensity,
//     spec Sec3.3's own "Colours are therefore f32[4]... stride 0x10") -
//     modelled here as TodColor4 + LerpColor4(), a plain 4-component lerp.
//
// WHAT IS A CHOSEN SCOPE-REDUCTION (not spec-derived), stated honestly:
//   * The WEATHER-STAGE axis of the real 2x2 blend is driven, in this
//     project, by Sec18's own 0x005A6270 Chance-weighted stage picker / the
//     20-byte schedule array / the per-tick duration-jitter state machine -
//     NONE of that runtime state machine exists in this project (only the
//     xtbl READERS do). Porting it is a separate, substantially larger task
//     than this one. BlendTodCorners() below is fully generic and DOES
//     implement the real weather-stage blend term - exercised with two
//     distinct weather stages by this file's own unit tests
//     (tests/synthetic_tod_lighting_test.cpp) - but ComputeTodLighting()'s
//     own RUNTIME DRIVER (the one tools/sr3_viewer.cpp's runPlaytest()
//     actually calls) holds a single CHOSEN weather-stage name constant and
//     passes it as BOTH stage-A and stage-B corners, which makes the
//     weather axis a mathematical no-op regardless of stageBlend's value -
//     an honest "not wired up", not a silently-approximated schedule.
//   * Which three fields of the ~100+-field cell this consumer reads
//     (Ambient_Color / Fog_Color / TOD_Light_Color) is itself a CHOSEN
//     reading of spec Sec18's own render-pipeline cross-reference paragraph
//     ("plausible blend targets"), not a field-by-field-confirmed shader-
//     constant mapping (Sec18 states the full ~100+ field enumeration inside
//     the real blend function was not exhaustively walked).
//   * Mapping Fog_Color -> the screen clear/sky colour, Ambient_Color -> the
//     renderer's ambient term and TOD_Light_Color -> the renderer's sun/
//     directional term is this consumer's OWN choice of where each blended
//     value is used on screen - the spec does not state that the real
//     engine's clear colour literally equals its fog colour (a common real-
//     time-rendering convention, used here because it is the most direct
//     "sky colour" candidate this table actually has, not because it was
//     found in the engine).
//   * A real HDR tonemap (ldr_min/ldr_max/bloom_exposure/tonemap_lum_range,
//     spec Sec3.3's direct-Stage-child float list) is explicitly OPEN/not
//     modelled anywhere in this project. Real TOD_Light_Color intensities in
//     shipped data run as high as ~16x (confirmed by direct real-data read
//     against the real misc_tables.vpp_pc, 2026-10-08) - per-channel
//     clamping that high would wash every hue out to flat white, so
//     NormalizeForDisplay() below divides by the brightest channel (or 1,
//     whichever is larger) instead of clamping each channel independently -
//     a CHOSEN, hue-preserving stand-in for the real tonemap, not a port of
//     it.
//   * Sun/moon DIRECTION is a genuine, substantiated spec NEGATIVE (Sec18:
//     "No Sun_Angle/Sun_Direction-shaped field exists... the full consumer
//     chain was traced specifically looking for a TOD-fraction-to-angle
//     computation and found none"). ComputeChosenSunDirection() below is
//     entirely CHOSEN, labelled as such at every use.
//
// WHAT THIS FILE DOES NOT DO: simulate a weather SCHEDULE (see above);
// resolve the "absent field = inherit from a previous/default state" rule
// spec Sec3.2 describes for the cell's own sparse override layer (same "not
// modelled" choice weather_time_of_day.h's own banner already makes for
// this project's xtbl READER - this consumer inherits that choice rather
// than inventing a resolution: an absent ColorIntensity reads as that
// struct's own already-documented 0/1.0 defaults, see ToTodColor4()).

#include <cstdint>
#include <string>
#include <vector>

#include "sr3tables_environment/tables.h"
#include "sr3tables_environment/weather_time_of_day.h"

namespace sr3render {

// ---------------------------------------------------------------------------
// The generic 4-float blend unit (spec Sec3.3's own "RGB + intensity, stride
// 0x10" colour record) and the lerp primitive the spec's own generic
// per-field blend function (0x00BA38E0) is confirmed to use.
// ---------------------------------------------------------------------------
struct TodColor4 {
    float r = 0.0f, g = 0.0f, b = 0.0f, intensity = 1.0f;
};

// !ci.present (the spec's "inherit" layer, not modelled - see this file's
// banner) reads as ColorIntensity's OWN default-constructed values (rgb all
// 0, intensity 1.0) - not a new invented fallback, just taking the already-
// documented struct defaults at face value.
TodColor4 ToTodColor4(const sr3tables_environment::ColorIntensity& ci);

inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }
TodColor4 LerpColor4(const TodColor4& a, const TodColor4& b, float t);

// ---------------------------------------------------------------------------
// spec Sec3.1's HHMM/minutes -> day-fraction conversions, applied here (the
// consumer) rather than at parse time - weather_time_of_day.h's own struct
// deliberately keeps the RAW integers (see its "RAW value, conversion
// documented not performed" banner); this is that documented conversion,
// performed once, at the one place it is actually needed.
// ---------------------------------------------------------------------------

// (hh*60+mm)/1440 via the engine's own truncating-signed-division reading of
// Start_Time (spec Sec3.1, FOR TEAM B 2026-10-01 correction): hours = hhmm/100
// (truncating signed division), minutes = hhmm%100, frac = (minutes/60 +
// hours)/24, reduced by 1.0 while STRICTLY greater than 1.0 (so 2400 stays
// exactly 1.0, never wraps to 0).
float TodFractionFromHHMM(int32_t hhmm);
// Ramp_Out_Time (minutes) -> day-fraction: minutes/1440.
float TodFractionFromMinutes(int32_t minutes);

// ---------------------------------------------------------------------------
// The CONFIRMED TOD-segment crossfade formula (spec Sec18).
// ---------------------------------------------------------------------------
// `t`, `highStartFraction`, `lowRampOutFraction` are all day-fractions in
// [0,1) (see the conversions above). Guards lowRampOutFraction <= 0 by
// returning 0 (a zero-width ramp window can never contain timeToNextStart) -
// a defensive guard against a divide-by-zero, not a spec-stated rule.
float ComputeTodSegmentBlend(float t, float highStartFraction, float lowRampOutFraction);

// ---------------------------------------------------------------------------
// Segment bracketing (the consumer's own job - spec 3.1's load-time qsort-by-
// Start_Time is not reproduced by this project's table reader, same "derived
// value, not modelled" rule; this function sorts a local copy of pointers
// instead of mutating the caller's vector).
// ---------------------------------------------------------------------------
struct TodSegmentBracket {
    const sr3tables_environment::WeatherTimeSegment* low = nullptr;
    const sr3tables_environment::WeatherTimeSegment* high = nullptr;
    float segmentBlend = 0.0f;
};
// Circular: the last segment (by Start_Time) brackets forward to the first.
// Returns a default-constructed (all-null) TodSegmentBracket if `segments`
// is empty.
TodSegmentBracket FindTodSegmentBracket(
    const std::vector<sr3tables_environment::WeatherTimeSegment>& segments, float t);

// First cell of `segment` whose Stage_Name matches `stageName`
// (case-insensitive, spec Sec3.1's own Stage_Name<->weather.xtbl Name
// matching rule), or nullptr.
const sr3tables_environment::WeatherTimeOfDayCell* FindCellByStageName(
    const sr3tables_environment::WeatherTimeSegment& segment, const std::string& stageName);

// ---------------------------------------------------------------------------
// The full 2x2 bilinear corner blend (spec Sec18), generic over whichever
// ColorIntensity field `extract` selects.
// ---------------------------------------------------------------------------
using ColorIntensityExtractor =
    const sr3tables_environment::ColorIntensity& (*)(const sr3tables_environment::WeatherTimeOfDayCell&);

const sr3tables_environment::ColorIntensity& ExtractAmbientColor(
    const sr3tables_environment::WeatherTimeOfDayCell& cell);
const sr3tables_environment::ColorIntensity& ExtractFogColor(
    const sr3tables_environment::WeatherTimeOfDayCell& cell);
const sr3tables_environment::ColorIntensity& ExtractTodLightColor(
    const sr3tables_environment::WeatherTimeOfDayCell& cell);

// Blends [segLowStageA,segHighStageA] by segmentBlend, [segLowStageB,
// segHighStageB] by the SAME segmentBlend, then the two results by
// stageBlend - the 2x2 corner order spec Sec18 itself describes (segment-low
// x stage-A, segment-high x stage-A, segment-low x stage-B, segment-high x
// stage-B).
TodColor4 BlendTodCorners(const sr3tables_environment::WeatherTimeOfDayCell& segLowStageA,
                          const sr3tables_environment::WeatherTimeOfDayCell& segHighStageA,
                          const sr3tables_environment::WeatherTimeOfDayCell& segLowStageB,
                          const sr3tables_environment::WeatherTimeOfDayCell& segHighStageB,
                          float segmentBlend, float stageBlend, ColorIntensityExtractor extract);

// ---------------------------------------------------------------------------
// CHOSEN sun direction (NOT spec-derived - spec Sec18 confirms no direction
// field exists anywhere in this table group; see this file's banner).
// ---------------------------------------------------------------------------
// A simple, cosmetic day arc: fixed horizontal bias (0.3, -0.8) - the SAME
// horizontal component the playtest's own prior fixed "headlight" direction
// used (src/mesh_renderer.cpp's old CHOSEN (0.3,0.5,-0.8) literal) - with
// height = sin(t*2*pi - pi/2), so the sun sits highest at t=0.5 (noon,
// height=+1) and lowest at t=0.0/1.0 (midnight, height=-1). A "sun" below
// the horizon needs no special-casing: the shader's existing
// saturate(dot(normal, sunDirection)) already zeroes the direct term then,
// leaving only the ambient term - exactly the desired day/night behaviour,
// for free.
void ComputeChosenSunDirection(float t, float outDirection[3]);

// Hue-preserving tonemap stand-in (CHOSEN - see this file's banner): divides
// all three channels by max(1.0, the brightest channel), so a >1 intensity
// dims toward white evenly rather than clipping each channel independently
// (which would wash a saturated colour out to flat white).
void NormalizeForDisplay(float rgb[3]);

// ---------------------------------------------------------------------------
// One frame's fully-blended scene lighting values. Defaults equal the
// orchestrator's own PRIOR fixed CHOSEN placeholders (skyblue clear colour
// 0.53/0.81/0.92, ambient 0.35, sun colour 0.65, sun direction
// (0.3,0.5,-0.8)) byte-for-byte, so a caller that gets a default-constructed
// (or lookup-failed) result renders exactly as before this task.
// ---------------------------------------------------------------------------
struct TodLightingResult {
    float skyClearColor[3] = {0.53f, 0.81f, 0.92f};  // CHOSEN mapping: blended Fog_Color
    float ambientColor[3] = {0.35f, 0.35f, 0.35f};   // blended Ambient_Color
    float sunColor[3] = {0.65f, 0.65f, 0.65f};       // CHOSEN mapping: blended TOD_Light_Color
    float sunDirection[3] = {0.3f, 0.5f, -0.8f};     // CHOSEN, see ComputeChosenSunDirection
};

// Computes one frame's TodLightingResult from the real parsed tables, a TOD
// fraction `t` (0=midnight .. 1=next midnight) and a CHOSEN single weather-
// stage name (see this file's banner: the weather-STAGE axis of the real
// blend is not simulated by this project). Falls back to
// TodLightingResult's own old-behaviour defaults, field by field, whenever
// `segments` is empty or the chosen stage has no cell in one of the
// bracketing segments - never a guessed or fabricated value.
TodLightingResult ComputeTodLighting(
    const std::vector<sr3tables_environment::WeatherTimeSegment>& segments, float t,
    const std::string& chosenWeatherStageName);

}  // namespace sr3render
