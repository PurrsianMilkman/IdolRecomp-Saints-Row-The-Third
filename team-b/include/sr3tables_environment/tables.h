#pragma once

// sr3tables_environment - typed readers for the environment / weather /
// lighting / post-processing / VFX `.xtbl` table group.
//
// Source: spec-tables-environment.md (SPEC TEAM, agent AK, 2026-09-20), the
// ONLY source consulted for this file's schema (cleanroom boundary - see
// HARD RULES in the task that produced this file). Built entirely on top of
// include/sr3xtbl/xtbl.h's document model and accessors (Node, FindChild,
// ChildText, GetX/ReadXAlways, ReadVec3, FlagMask/EnumIndex, NameHash);
// nothing here was derived from the game executable, disassembly or
// decompiled code - every offset/address cited in a comment is copied
// verbatim from the spec's own prose, purely to let a reader cross-reference
// the spec section, never re-derived.
//
// CONVENTION (matches sr3xtbl.h exactly - see its banner, section 3):
//   * sr3xtbl::Always<T> (value + present)  = the spec's "always write"
//     elements (usually 0x00DACCB0/0x00DABC70/0x00DABDF0/0x00DABF20/0x00DAC480
//     in the spec's accessor catalogue, spec 1.4). When !present the ENGINE's
//     real behaviour is an indeterminate stack-residue value (spec 1.4's
//     "hazard" paragraph), NOT sr3xtbl::Always<T>'s 0 stand-in - see xtbl.h's
//     own warning. Shipped data always supplies these elements per the spec's
//     DLC validation (14.2); this reader does not attempt to reproduce the
//     residue.
//   * std::optional<T> = the spec's "write only if present" / "only if
//     present" / "write-if-present" elements (0x00DACD40, 0x00DABE80,
//     0x00DAC510, and any element the spec states is untouched when absent).
//   * A handful of elements have a spec-STATED CONCRETE default distinct from
//     both of the above (e.g. refraction_situations.xtbl Duration -> -1.0,
//     skybox_effects.xtbl TODRange -> 0 / 0x0937, vfx.xtbl Blocker/Opacity ->
//     -1.0, LOD -> 1e8/1e10). These are modelled as std::optional<T> (the
//     struct holds exactly what was read) plus a small `...OrDefault()`
//     helper that applies the spec's own documented constant - so a caller
//     never has to guess, and the "was it actually present in real data"
//     question the validation harness asks stays answerable.
//
// WHAT IS DELIBERATELY LEFT OUT (per the task's "don't guess" rule):
//   * Fields the spec marks OPEN/UNKNOWN at the level of what the ELEMENT
//     MEANS are still surfaced (raw text/number), since the element and its
//     type are CONFIRMED - only the runtime *consumer* is open (e.g.
//     external_light_override.xtbl's owner subsystem, camera_free.xtbl's
//     Vehicle_Aim flag bit meanings). What is left out is anything that is
//     not a readable XML element at all: derived/computed values (a row's
//     resolved CRC-32 hash, a cross-table pointer resolved at load time,
//     Chance-list normalisation, the DOF "Advanced" pairing, map_districts'
//     +0xB0 world-geometry lookup), and the two weather_time_of_day.xtbl
//     cloud-layer slots literally named "?" in the disassembly (spec 3.3),
//     which cannot be authored by any XML element name and so cannot be
//     populated by this table's data at all (see the report for detail).
//   * materials.xtbl is SKIPPED ENTIRELY: spec 1.1/1.2 - it has no standalone
//     filename literal in the executable (only as the tail of
//     weapon_tracer_materials.xtbl / bitmap_materials.xtbl /
//     customization_materials.xtbl), so no loader was ever found for it -
//     the same "no literal, no loader, skip" situation as traffic-ai's
//     traffic_types.xtbl. 26 of the 27 named tables are implemented here.
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> for every repeated-row table, and ParseXxx(const
// sr3xtbl::Document&) -> optional<Xxx> (or Xxx) for the few single-block
// tables (motion_blur.xtbl, camera_free.xtbl). All definitions are in
// src/tables_environment.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_environment {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3Result;

// ---------------------------------------------------------------------------
// Shared colour conventions (spec-tables-environment.md 1.4's accessor
// catalogue documents two, and weather_time_of_day.xtbl's own reader adds a
// third variant of the first - see 3.3).
// ---------------------------------------------------------------------------

// Raw R/G/B (0x00DAD0A0-family: children R, G, B read as-is, NOT divided by
// 255). `present` records whether the named colour element itself was found
// (the "only if present" gate 0x00DAD130 applies at the whole-element level;
// per-component absence, if it ever occurs, falls under the general
// Always-hazard of 1.4 since no per-component "write only if present"
// variant of this accessor is documented).
struct ColorRGBAlways {
    Always<float> r, g, b;
};

// A named colour element read raw (R/G/B, NOT divided by 255) plus a sibling
// "<Name>_Intensity" element read write-if-present with engine default 1.0
// (spec 3.3, accessor 0x00BA1510: "the function returns 'present'" = whether
// the colour element itself exists). Used throughout weather_time_of_day.xtbl
// and nowhere else in this group.
struct ColorIntensity {
    bool present = false;         // the colour element itself was found
    ColorRGBAlways rgb;           // valid only when present
    float intensity = 1.0f;       // spec-documented default when the
                                   // "<Name>_Intensity" sibling is absent
    bool intensityPresent = false;
};

// Raw R/G/B each divided by 255.0 (0x00DAD0A0), the convention weather.xtbl
// and lightning.xtbl's overrides use. `present` = whether the named colour
// element itself was found (models both the plain 0x00DAD0A0 call and its
// "only if present" wrapper 0x00DAD130 uniformly from a parsing point of
// view: in both cases "element missing" is externally observable as
// !present; the difference the spec draws - hazard vs "untouched" - is a
// destination-write question this struct does not need to take a position
// on, since it always reports exactly what text was found).
struct ColorDiv255 {
    bool present = false;
    Always<float> r, g, b;
};

// A generic "r g b [a]" whitespace-separated colour string, the format
// external_light_override.xtbl's front_color/back_color and
// material_color_variants.xtbl's Color use (spec 9.3, 11.5: C `sscanf`,
// unspecified trailing components stay 1.0). This project's foundation
// (sr3xtbl.h) exposes only the engine float grammar (ParseFloat), not C
// `atof`/`sscanf`; the parser in tables_environment.cpp tokenises on
// whitespace and applies sr3xtbl::ParseFloat to each token as the closest
// available approximation - see the .cpp for the caveat this implies.
struct ColorRGBAText {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// ===========================================================================
// 2. weather.xtbl - weather stages (spec-tables-environment.md 2)
// ===========================================================================
struct WeatherStage {
    std::optional<std::string> name;         // Name
    std::optional<std::string> displayName;  // DisplayName (falls back to Name when absent - a
                                              // display-time default, not modelled here)
    Always<float> chance;                    // Stage_Settings/Chance - RAW weight; normalising it
                                              // over the sum of every stage's Chance is a whole-
                                              // table load step, not a per-row concern (2)
    Always<float> averageDuration;           // Stage_Settings/Average_Duration
    Always<float> variance;                  // Stage_Settings/Variance
    Always<float> pedDensity;                // Stage_Settings/PedDensity
    Always<float> rainDensity;               // Rain_Settings/Rain_Density (NOT rain.xtbl's integer Density)
    Always<float> lightningFrequency;        // Rain_Settings/Lightning_Settings/Frequency
    Always<float> lightningVariance;         // Rain_Settings/Lightning_Settings/Variance
    Always<float> rainFogLayer1UScale, rainFogLayer1VScale, rainFogLayer1ScrollSpeed, rainFogLayer1Opacity;
    Always<float> rainFogLayer2UScale, rainFogLayer2VScale, rainFogLayer2ScrollSpeed, rainFogLayer2Opacity;
    Always<float> todLightsPct;               // TOD_Settings/Tod_Lights_Pct
    Always<float> skydomeBlendFactor;         // TOD_Settings/Skydome_Settings/Blend_Factor
    ColorDiv255 skydomeColor;                 // TOD_Settings/Skydome_Settings/Color
    ColorDiv255 sunColorMultiply;             // Sun_Settings/Sun_Color_Multiply
    ColorDiv255 sunGlowColorMultiply;         // Sun_Settings/Sun_Glow_Color_Multiply
    Always<float> sunOpacity;                 // Sun_Settings/Sun_Opacity
    ColorDiv255 moonColorMultiply;            // Moon/Moon_Color_Multiply - explicitly "only if present";
                                               // .present == false -> untouched (0) per the spec
    Always<float> windAverageSpeed, windGustSpeed, windFrequency, windVariance; // Wind_Settings/*
    Always<float> cloudHorizonCirrus, cloudHorizonCumulus, cloudHorizonStorm;   // Cloud_Settings/Map_Strengths/*
    Always<float> cloudOverheadCirrus, cloudOverheadCumulus, cloudOverheadStorm;
    Always<float> cloudHorizonScrollRate, cloudOverheadScrollRate;             // Cloud_Settings/Scrolling/*
    Always<float> minTempDay, maxTempDay, minTempNight, maxTempNight;          // Temp_Settings/*
    Always<float> ambientWaveSpeed, ambientWaveAmplitude;                      // Ambient_Wave_Settings/*
    Always<float> audioIntensity;             // Audio/Audio_Intensity
    std::vector<std::string> nextStageNames;  // Next_Stage_List/Next_Stage (repeated; text = a stage
                                               // name resolved to an entry pointer at load time, NULL
                                               // if unmatched - raw names kept here)
};
WeatherStage ParseWeatherStage(const Node* row);
std::vector<WeatherStage> ParseWeatherTable(const Document& doc);

// ===========================================================================
// 4. wind.xtbl - wind stages (spec-tables-environment.md 4)
// ===========================================================================
struct WindStage {
    std::optional<std::string> name;          // Name
    std::optional<std::string> displayName;   // Display_Name (note the spelling differs from
                                               // weather.xtbl's DisplayName - spec 4)
    Always<float> chance;                     // Stage_Settings/Chance (raw weight)
    Always<float> averageDuration;            // Stage_Settings/Average_Duration
    Always<float> variance;                   // Stage_Settings/Variance
    Always<float> windAverageIntensity;       // Wind_Settings/Average_Intensity
    std::vector<std::string> nextStageNames;  // Next_Stage_List/Next_Stage
};
WindStage ParseWindStage(const Node* row);
std::vector<WindStage> ParseWindTable(const Document& doc);

// ===========================================================================
// 5.1 rain.xtbl - rain intensity levels (spec-tables-environment.md 5.1)
// ===========================================================================
struct RainLevel {
    Always<uint32_t> densityRaw;     // Level/Parameters/Density - the ONLY integer element and the
                                      // sort key; stored by the engine as f32 = value / 100.0
    float densityFraction() const { return densityRaw.present ? static_cast<float>(densityRaw.value) / 100.0f : 0.0f; }
    Always<float> viewRadius;         // View_Radius
    Always<float> speed;              // Speed
    Always<float> opacity;            // Opacity
    Always<float> lengthNear, lengthFar;   // Length_Near, Length_Far
    Always<float> widthNear, widthFar;     // Width_Near, Width_Far
    Always<float> splashLifetime;          // Splash_Lifetime
    Always<float> splashSizeNear, splashSizeFar; // Splash_Size_Near, Splash_Size_Far
    Always<float> windAmount;              // Wind_Amount
    std::optional<std::string> effect;           // Effect -> effects.xtbl index (0xFFFFFFFF if absent/unknown)
    std::optional<std::string> cameraDropEffect;  // Camera_drop_effect (note the lower-case "drop")
};
RainLevel ParseRainLevel(const Node* row);
std::vector<RainLevel> ParseRainTable(const Document& doc);

// ===========================================================================
// 5.2 lightning.xtbl - lightning types (spec-tables-environment.md 5.2)
// ===========================================================================
struct LightningType {
    // `Name` is read by the row reader and explicitly discarded ("not
    // stored") - omitted here since it is not part of the runtime row.
    Always<float> probability;                  // Probability
    std::vector<std::string> vfxNames;           // VFX_List/VFX (repeated; text = a skybox_effects.xtbl
                                                  // Name, CRC-matched at load; unresolved names dropped
                                                  // there - raw names kept here)
    ColorDiv255 fogColorOverride;                 // TOD_Overrides/Fog_Color_Override (presence bit +0x44.0)
    std::optional<float> fogStrengthOverride;     // TOD_Overrides/Fog_Strength_Override (own text; +0x44.1)
    ColorDiv255 ambientOverride;                   // TOD_Overrides/Ambient_Override (+0x44.2)
    std::optional<float> cloudBrightnessOverride; // TOD_Overrides/Cloud_Brightness_Override (own text; +0x44.3)
    std::optional<float> cloudContrastOverride;   // TOD_Overrides/Cloud_Contrast_Override (own text; +0x44.4)
    std::optional<float> skyBrightnessOverride;   // TOD_Overrides/Sky_Brightness_Override (own text; +0x44.5)
    ColorDiv255 todLightOverride;                  // TOD_Overrides/TOD_Light_Override (+0x44.6)
};
LightningType ParseLightningType(const Node* row);
std::vector<LightningType> ParseLightningTable(const Document& doc);

// ===========================================================================
// 6.1 lens_flares.xtbl (spec-tables-environment.md 6.1)
// ===========================================================================
// NOTE: the whole table is ignored by the loader if it holds more than 8
// Flare rows (a table-level guard, checked before any row is read - the
// validation harness reports this, it is not a struct-level concern).
struct LensFlare {
    std::optional<std::string> imageFilename; // ImageFilename/Filename -> texture handle
    Always<float> radius;                      // Radius
    Always<float> scale;                       // Scale
    Always<float> baseAlpha;                   // BaseAlpha
};
LensFlare ParseLensFlare(const Node* row);
std::vector<LensFlare> ParseLensFlaresTable(const Document& doc);

// ===========================================================================
// 6.2 motion_blur.xtbl (spec-tables-environment.md 6.2)
// ===========================================================================
// Whole-file, single <Motion_Blur_Settings> element (not repeated); if
// absent the table is a documented no-op (ParseMotionBlurSettings returns
// std::nullopt). Every field uses the always-write accessor (0x00DACCB0)
// except Fake_Velocity_Scale, which uses 0x00DACE40 (one value replicated
// into 4 consecutive floats) - modelled as a single Always<float> since all
// four destination slots are, by construction, equal.
struct MotionBlurSettings {
    Always<float> onFootWalkRunStrength, onFootWalkRunMaxOffset;                       // On_Foot_Walk_Run
    Always<float> onFootSprintStrength, onFootSprintMaxOffset;                         // On_Foot_Sprint
    Always<float> onFootFreefallStrength, onFootFreefallMaxOffset;                     // On_Foot_Freefall
    Always<float> onFootFreefallMinFallVelocity, onFootFreefallMaxFallVelocity;
    Always<float> onFootExplosionWorldStrength, onFootExplosionTargetStrength, onFootExplosionMaxOffset; // On_Foot_Explosion

    struct BaseGroup {
        Always<float> worldStrength, targetStrength, maxOffset, fakeVelocityScale;
    };
    BaseGroup vehicleBase, airplaneBase, helicopterBase; // Vehicle_Base, Airplane_Base, Helicopter_Base

    struct BurstGroup {
        Always<float> worldStrength, targetStrength, maxOffset, duration, decayTime, fakeVelocityScale;
    };
    BurstGroup vehicleNitrous, vehiclePeelout; // Vehicle_Nitrous, Vehicle_Peelout
};
std::optional<MotionBlurSettings> ParseMotionBlurSettings(const Document& doc);

// ===========================================================================
// 6.3 radial_blur.xtbl (spec-tables-environment.md 6.3)
// ===========================================================================
struct RadialBlur {
    std::optional<std::string> name;  // Name (bounded char[0x40] copy)
    Always<float> strength;            // Strength
    Always<float> duration;            // Duration
    Always<float> radius;              // Radius
    Always<float> distanceFade;        // Distance_fade
    Always<int32_t> priority;          // Priority
};
RadialBlur ParseRadialBlur(const Node* row);
std::vector<RadialBlur> ParseRadialBlurTable(const Document& doc);

// ===========================================================================
// 7.1 dof_situations.xtbl (spec-tables-environment.md 7.1)
// ===========================================================================
// The " Advanced" pairing (7.1) is a cross-row, load-time-derived relation,
// not a field of any one row; not modelled here.
struct DofSituation {
    std::optional<std::string> name; // "name" (looked up case-insensitively, so "Name" also matches)
    Always<float> startMultiplierA, startMultiplierB;
    Always<float> endMultiplierA, endMultiplierB;
    Always<float> blurRadius;
    Always<float> transitionSpeed;
    Always<float> humanSpherecastRadius;
};
DofSituation ParseDofSituation(const Node* row);
std::vector<DofSituation> ParseDofSituationsTable(const Document& doc);

// ===========================================================================
// 8. refraction_situations.xtbl (spec-tables-environment.md 8)
// ===========================================================================
struct RefractionSituation {
    std::optional<std::string> name; // "name" (matched case-insensitively; "Name" in the data)
    Always<float> scale;              // Scale
    Always<float> frequency;          // Frequency
    Always<float> offsetDelta;        // Offset_Delta
    Always<float> fadeInTime;         // Fade_In_Time
    Always<float> fadeOutTime;        // Fade_Out_Time
    std::optional<float> duration;    // Duration - EXPLICITLY optional; spec-stated default -1.0 (the
                                       // constant at 0x012A2D54), NOT the general Always-hazard 0
    float DurationOrDefault() const { return duration.value_or(-1.0f); }

    // Spasm - the whole block is optional; spec-stated: absent -> every
    // sub-field below is explicitly set to 0 (a real coded default, which
    // happens to be the same value sr3xtbl::Always<T> stands in with, but
    // for a different, spec-confirmed reason - see 8).
    bool spasmPresent = false;
    Always<float> spasmTimeMin, spasmTimeMax;
    Always<float> spasmFrequencyMin, spasmFrequencyMax;
    Always<float> spasmScaleMin, spasmScaleMax;
    Always<float> spasmDurationMin, spasmDurationMax;

    std::optional<std::string> framework; // Framework - filter only, not stored by the runtime row;
                                           // default "main" when absent (8)
};
RefractionSituation ParseRefractionSituation(const Node* row);
std::vector<RefractionSituation> ParseRefractionSituationsTable(const Document& doc);

// ===========================================================================
// 9.1 skybox_effects.xtbl (spec-tables-environment.md 9.1)
// ===========================================================================
struct SkyboxEffect {
    std::optional<std::string> name;    // Name -> CRC-32 key (derived; not stored here)
    std::optional<std::string> effect;  // Effect -> effects.xtbl index (0xFFFFFFFF if unknown)
    bool autoSpawnPresent = false;      // Auto_Spawn - a presence flag; absent -> both fields below 0
    Always<float> autoSpawnMinTimeSpacing; // Auto_Spawn/Min_Time_Spacing
    Always<float> autoSpawnMaxTimeSpacing; // Auto_Spawn/Max_Time_Spacing
    Always<bool> randomOrientation;         // Random_Orientation (always-write bool - hazard applies)
    std::optional<uint16_t> todRangeStartTime; // TODRange/Start_Time; absent TODRange -> spec default 0
    std::optional<uint16_t> todRangeEndTime;   // TODRange/End_Time; absent TODRange -> spec default 0x0937 (2359)
    uint16_t TodRangeStartOrDefault() const { return todRangeStartTime.value_or(0); }
    uint16_t TodRangeEndOrDefault() const { return todRangeEndTime.value_or(0x0937); }
    std::optional<std::string> weatherStage;   // Weather_Stage -> weather.xtbl index; absent/unmatched -> 0xFFFF
    Always<uint8_t> skyboxLayer;                // Skybox_Layer (low byte kept)
};
SkyboxEffect ParseSkyboxEffect(const Node* row);
std::vector<SkyboxEffect> ParseSkyboxEffectsTable(const Document& doc);

// ===========================================================================
// 9.2 time_of_day_objects.xtbl (spec-tables-environment.md 9.2)
// ===========================================================================
// The 5-name enum table (spec 9.2 / 13): selects which of five fixed runtime
// records a row writes to.
inline constexpr std::array<std::string_view, 5> kTimeOfDayObjectNames = {
    "Street Lights", "Headlights", "Windows", "Distant Vehicle Headlights", "Searchlights",
};
struct TimeOfDayObjectType {
    std::optional<std::string> name;   // Name -> enum index via kTimeOfDayObjectNames (case-insensitive);
                                        // a row with no Name is skipped by the loader; an unrecognised
                                        // Name still selects a (mis-)index (spec: "-1", no range check)
    Always<uint32_t> onTimeHHMM;        // On_Time (raw integer HHMM; the loader turns this into a
                                         // fraction-of-day float, a load-time transform)
    Always<uint32_t> offTimeHHMM;       // Off_Time
    Always<uint32_t> variationMinutes;  // Variation (raw integer minutes)
};
TimeOfDayObjectType ParseTimeOfDayObjectType(const Node* row);
std::vector<TimeOfDayObjectType> ParseTimeOfDayObjectsTable(const Document& doc);

// ===========================================================================
// 9.3 external_light_override.xtbl (spec-tables-environment.md 9.3)
// ===========================================================================
struct LightOverride {
    std::optional<std::string> name;  // Name - the key prefix ("<Name>-<element>" record naming)
    std::optional<ColorRGBAText> frontColor; // front_color "r g b [a]"; unspecified trailing components stay 1.0
    std::optional<ColorRGBAText> backColor;  // back_color "r g b [a]"; RAW text form - NOT yet multiplied
                                              // by back_intensity (that multiply is a load-time transform)
    std::optional<float> frontIntensity;      // front_intensity (C atof-style; spec default 1.0)
    std::optional<float> backIntensity;       // back_intensity (also the back_color multiplier)
    float FrontIntensityOrDefault() const { return frontIntensity.value_or(1.0f); }
    float BackIntensityOrDefault() const { return backIntensity.value_or(1.0f); }
};
LightOverride ParseLightOverride(const Node* row);
std::vector<LightOverride> ParseExternalLightOverrideTable(const Document& doc);

// ===========================================================================
// 10.1 effects.xtbl (spec-tables-environment.md 10.1)
// ===========================================================================
struct Effect {
    std::optional<std::string> name;       // Name (bounded char[0x40] copy)
    std::optional<std::string> framework;  // Framework - filter only; default "main"
    std::optional<int32_t> infoSlotIndex;  // Info_Slot_Index - only meaningful (read) on the first
                                            // Effect row of a matching framework by the DLC driver;
                                            // surfaced raw per row regardless
    std::optional<std::string> visual;     // visual -> vfx.xtbl Name (linear _stricmp match)
    std::optional<std::string> sound;      // sound -> sound handle (0 if absent)
    std::optional<std::string> soundParentSwitchName; // sound_parent_switch_name "Group:State"
    std::optional<std::string> soundSwitchName;       // sound_switch_name "Group:State"
    std::optional<float> vfxKillParticlesFadeTime;    // vfx_kill_particles_fade_time (write-if-present;
                                                       // engine reads it only once `visual` resolved)
    std::optional<float> scaleFactor;                 // scale_factor (write-if-present; read for every row)

    bool damageRegionPresent = false;                 // Damage_Region - whole block optional
    std::optional<std::string> damageRegionType;      // Damage_Region/Region_Type (hashed at load; raw text kept)
    bool damageRegionIsBox = false;                    // Damage_Region/Region_Shape == "Box" (case-insensitive);
                                                        // any other text, or an absent block, -> false
    Vec3Result damageRegionOffset;                      // Damage_Region/Region_Offset; absent block -> (0,0,0)
    Vec3Result damageRegionSize;                        // Damage_Region/Region_Size; absent block -> (0,0,0)

    // bool elements - all write-if-present; the engine additionally gates
    // several of these on whether `sound`/`visual` resolved, a load-time
    // condition this struct does not evaluate (raw as-read values only).
    std::optional<bool> restartSoundAtLoop;   // read only when `sound` resolved
    std::optional<bool> killSoundWhenDone;    // read only when `sound` resolved
    std::optional<bool> soundFollowsEffect;   // read only when `sound` resolved
    std::optional<bool> vfxKillParticles;     // read only when `visual` resolved
    std::optional<bool> stopWhenHostDestroyed; // read only when `visual` resolved
    std::optional<bool> stopUnderCar;          // read only when `visual` resolved
    std::optional<bool> stopUnderPlayer;       // read only when `visual` resolved
    std::optional<bool> onlyAtNight;           // read only when `visual` resolved
    std::optional<bool> useMissionSrid;

    bool proximityRefractionPresent = false;                    // Proximity_Refraction - whole block optional
    std::optional<std::string> proximityRefractionSituation;    // Proximity_Refraction/Situation (hashed at load;
                                                                 // matches refraction_situations.xtbl "name")
    Always<float> proximityRefractionNearRadius;                 // Proximity_Refraction/Near_Radius; absent block -> 0
    Always<float> proximityRefractionFarRadius;                  // Proximity_Refraction/Far_Radius; absent block -> 0
};
Effect ParseEffect(const Node* row);
std::vector<Effect> ParseEffectsTable(const Document& doc);

// ===========================================================================
// 10.2 vfx.xtbl (spec-tables-environment.md 10.2)
// ===========================================================================
inline constexpr std::array<std::string_view, 6> kVfxStreamingCategoryNames = {
    "Preload", "Multiplayer", "Vehicle", "Environment", "Env Preload", "Cutscene",
};
struct VfxEffect {
    std::optional<std::string> name;         // Name (bounded char[0x40] copy)
    std::optional<std::string> framework;    // Framework - filter; default "main"
    std::optional<std::string> vfxFilename;  // VFX/Filename (raw authoring name; the extension-swap to
                                              // .cefct_pc is a load-time transform, see effects.xtbl notes)
    std::optional<std::string> streamingCategory; // Streaming_Category text; see kVfxStreamingCategoryNames
                                                    // via sr3xtbl::EnumIndex - unmatched text -> -1

    std::optional<float> radius;         // Radius (write-if-present; spec default 1.0)
    float RadiusOrDefault() const { return radius.value_or(1.0f); }
    std::optional<bool> radiusExpands;   // Radius_expands (write-if-present; spec default TRUE)
    bool RadiusExpandsOrDefault() const { return radiusExpands.value_or(true); }

    bool blockerPresent = false;                 // Blocker - presence flag
    std::optional<float> blockerOpacity;          // Blocker/Opacity; absent Blocker -> spec default -1.0
    float BlockerOpacityOrDefault() const { return blockerOpacity.value_or(-1.0f); }
    Always<int32_t> blockerLifeSpan;               // Blocker/LifeSpan

    std::optional<std::string> radialBlurEntry;   // Radial_blur/Radial_blur_entry -> radial_blur.xtbl index

    // [OPEN - spec-tables-environment.md 10.2 (vfx.xtbl) LOD rows (Review status: "NEEDS-EXE: `0x005C3E70` with no `LOD`"): the
    // 1.0e8 / 1.0e10 defaults are stated only for an absent `Spawning`/`Distance` block INSIDE a present `LOD`;
    // what the fields hold when the whole `LOD` element is absent (401 of 529 real rows, 17.1) is not stated. The
    // *OrDefault() helpers below apply the defaults in both cases; the whole-LOD-absent case is NOT confirmed.]
    bool lodPresent = false;                       // LOD - presence flag
    std::optional<float> lodSpawningDistance;      // LOD/Spawning/Distance; absent Spawning -> spec default 1.0e8
    float LodSpawningDistanceOrDefault() const { return lodSpawningDistance.value_or(1.0e8f); }
    std::optional<bool> lodSpawningView;           // LOD/Spawning/View
    std::optional<float> lodDistanceFadingStart;   // LOD/Distance/Fading_start; absent Distance -> spec default 1.0e10
    std::optional<float> lodDistanceFadingEnd;     // LOD/Distance/Fading_end; absent Distance -> spec default 1.0e10
    float LodFadingStartOrDefault() const { return lodDistanceFadingStart.value_or(1.0e10f); }
    float LodFadingEndOrDefault() const { return lodDistanceFadingEnd.value_or(1.0e10f); }
    std::optional<bool> lodDistanceRestore;        // LOD/Distance/Restore
    bool lodUpdatePresent = false;                  // LOD/Update - presence flag
    std::optional<float> lodUpdateMinimumTime;      // LOD/Update/Minimum_time

    std::optional<float> startTime;    // Start_time (write-if-present; spec default 0)
    float StartTimeOrDefault() const { return startTime.value_or(0.0f); }
};
VfxEffect ParseVfxEffect(const Node* row);
std::vector<VfxEffect> ParseVfxTable(const Document& doc);

// ===========================================================================
// 11.1 interface_effects.xtbl (spec-tables-environment.md 11.1)
// ===========================================================================
struct InterfaceEffect {
    std::optional<std::string> name;   // Name (bounded char[0x20] copy)
    Always<float> cameraFov;            // Camera/FOV (row zeroed first)
    std::optional<float> cameraBlur;    // Camera_Blur - a DIRECT child of the row, not under Camera (write-if-present)
    bool lutPresent = false;            // LUT - presence flag
    std::optional<std::string> lutName; // LUT/LutName (bounded char[0x20] copy)
    Always<float> lutStrength;           // LUT/LutStrength - read only when LUT exists
};
InterfaceEffect ParseInterfaceEffect(const Node* row);
std::vector<InterfaceEffect> ParseInterfaceEffectsTable(const Document& doc);

// ===========================================================================
// 11.2 decal_info.xtbl (spec-tables-environment.md 11.2)
// ===========================================================================
struct DecalInfo {
    std::optional<std::string> name;              // Name (bounded char[0x40] copy)
    std::optional<std::string> materialFilename;  // Material/Filename (extension-swap to matlib_pc is
                                                   // a load-time transform)
    Always<bool> preload;                          // Preload
    Always<bool> doubleSided;                      // Double_sided
    Always<int32_t> lifeTime;                       // Life_Time
    Always<int32_t> fadeTime;                       // Fade_Time
    Always<float> width, length, depth;             // Width, Length, Depth
    Always<float> depthFadeStart, depthFadeEnd;     // Depth_Fade_Start, Depth_Fade_End
    Always<float> slopeFadeStart, slopeFadeEnd;     // Slope_Fade_Start/End - RAW degrees; the
                                                     // degrees->radians->trig conversion is a load-time
                                                     // transform, CONFIRMED cosine (spec 17.3, disassembly:
                                                     // FCOS instruction + "cos" domain-error string -
                                                     // fixed from HYPOTHESIS 2026-09-29, rule 16)
    Always<bool> hasNormal;                         // has_normal
    Always<float> alphaTest;                        // alpha_test
};
DecalInfo ParseDecalInfo(const Node* row);
std::vector<DecalInfo> ParseDecalInfoTable(const Document& doc);

// ===========================================================================
// 11.3 groundfires.xtbl (spec-tables-environment.md 11.3)
// ===========================================================================
struct Groundfire {
    std::optional<std::string> name;         // Name
    Always<float> damageRadius;               // Damage_Radius
    std::optional<std::string> damageRegion;  // Damage_Region (a heap copy of the string; not a
                                               // sub-tree like effects.xtbl's Damage_Region)
    std::vector<std::string> effectNames;     // effects/effect (repeated; effect index at load, unknown
                                               // names -> 0xFFFFFFFF; raw names kept here)
    Always<float> durationMinSeconds;          // Duration_Min - RAW seconds; the engine stores
                                               // (int)(x * 1000.0) integer milliseconds (load-time)
    Always<float> durationMaxSeconds;          // Duration_Max
};
Groundfire ParseGroundfire(const Node* row);
std::vector<Groundfire> ParseGroundfiresTable(const Document& doc);

// ===========================================================================
// 11.4 shells.xtbl (spec-tables-environment.md 11.4)
// ===========================================================================
// NOTE: at most 8 rows are read (the loop stops at the ninth) - a table-level
// guard, reported by the validation harness rather than modelled here.
struct Shell {
    std::optional<std::string> name;           // Name (a heap copy)
    std::optional<std::string> staticMesh;      // Static_Mesh -> mesh handle
    std::optional<std::string> collisionFoley;  // Collision_Foley -> sound handle; 0 when absent
};
Shell ParseShell(const Node* row);
std::vector<Shell> ParseShellsTable(const Document& doc);

// ===========================================================================
// 11.5 material_color_variants.xtbl (spec-tables-environment.md 11.5)
// ===========================================================================
struct ColorEntry {
    std::optional<int32_t> entryId;  // _Entry_ID via a C `sscanf("%d")`-style read; must be <= 256
                                      // (< 0x101) or the row is skipped by the loader (table-level
                                      // policy, not enforced by this struct)
    std::optional<ColorRGBAText> color; // Color "r g b a"; unspecified trailing components stay 1.0;
                                         // a Color element with NO text leaves the record untouched
                                         // (modelled here as std::nullopt, same as "absent")
};
ColorEntry ParseColorEntry(const Node* row);
std::vector<ColorEntry> ParseMaterialColorVariantsTable(const Document& doc);

// ===========================================================================
// 11.6 bitmap_materials.xtbl (spec-tables-environment.md 11.6)
// ===========================================================================
// The 33-name slot list (spec 11.6): a row's Name is matched case-insensitively
// against this list; an unknown name falls into slot 0x1F ("not set").
inline constexpr std::array<std::string_view, 33> kBitmapMaterialSlotNames = {
    "concrete", "cardboard", "carpet", "ceramic", "dirt", "drywall", "flesh", "foliage",
    "glass - heavy", "glass - medium", "glass - light", "grass", "gravel", "marble",
    "metal - fence", "metal - solid", "metal - thin", "plastic", "rubber", "sand", "water",
    "wood", "corrugated brick", "corrugated metal", "cyberspace", "vibration",
    "water container", "flame", "electric", "steam", "concrete - reflectable", "not set",
    "wrestling mat",
};
struct BitmapMaterial {
    std::optional<std::string> name;         // Name -> slot selector (kBitmapMaterialSlotNames)
    std::optional<std::string> suffix;        // MaterialProperties/Suffix (bounded char[3] copy)
    std::vector<std::string> bulletDecals;    // Bullet_Decals/Bullet_Decal (repeated; decal name ->
                                               // decal_info.xtbl index via the multiply-33 hash pool)
    std::optional<std::string> blastDecal;    // Blast_Decal -> decal index
    std::optional<std::string> crashDecal;    // Crash_Decal -> decal index
    std::optional<int32_t> audioOcclusion;    // Audio_Occlusion (integer, low byte kept; write-if-present,
                                               // preset 0)
};
BitmapMaterial ParseBitmapMaterial(const Node* row);
std::vector<BitmapMaterial> ParseBitmapMaterialsTable(const Document& doc);

// ===========================================================================
// 11.7 bitmap_sheets.xtbl (spec-tables-environment.md 11.7)
// ===========================================================================
// Row shape only (loader-level; "OPEN for what happens after", 11.7). At
// most 0x180 = 384 sheets are read (table-level guard).
struct BitmapSheet {
    std::optional<std::string> name;  // Name; spec-stated default empty string when absent
    std::string NameOrDefault() const { return name.value_or(std::string()); }
};
BitmapSheet ParseBitmapSheet(const Node* row);
std::vector<BitmapSheet> ParseBitmapSheetsTable(const Document& doc);

// ===========================================================================
// 11.8 map_districts.xtbl (spec-tables-environment.md 11.8)
// ===========================================================================
struct MapDistrict {
    std::optional<std::string> name;          // Name (bounded char[0x20] copy)
    std::optional<std::string> dataItemName;  // data_item_name (bounded char[0x20] copy)
    std::optional<std::string> teamName;      // team_name (bounded char[0x20] copy)
    std::optional<std::string> contactIcon;   // contact_icon (bounded char[0x20] copy)
    std::optional<std::string> contactName;   // contact_name (bounded char[0x28] copy)
    Always<float> textLocationX;               // text_location/X -> +0xA8
    Always<float> textLocationY;               // text_location/Y - read by the engine's vec3 reader but
                                                // explicitly NOT stored anywhere (spec 11.8); kept here
                                                // only because it is real, present XML data
    Always<float> textLocationZ;               // text_location/Z -> +0xAC
    // The derived +0xB0 field (a world-geometry containment lookup at the
    // point (X,Y,Z), performed at load time) is not an XML element and is
    // not modelled here.
};
MapDistrict ParseMapDistrict(const Node* row);
std::vector<MapDistrict> ParseMapDistrictsTable(const Document& doc);

// ===========================================================================
// 11.9 fade_categories.xtbl (spec-tables-environment.md 11.9)
// ===========================================================================
struct FadeCategory {
    std::optional<std::string> name;      // Name -> CRC-32 key (derived; not stored here)
    // NOTE (spec 11.9, verbatim): "the always-write accessor is used, so an
    // absent element is subject to the [Always-]hazard ... rather than to
    // the default - the default (60.0 / 200.0 / 500.0) only applies to the
    // destination's INITIAL value before the row is even read." Do not
    // treat Always<float>::value as 60.0/200.0/500.0 when !present.
    Always<float> mediumLodDistance;   // medium_lod_distance - RAW value (the engine squares it before storing)
    Always<float> lowLodDistance;      // low_lod_distance - RAW value (squared before storing)
    Always<float> distance;            // Distance - RAW value (squared before storing)
};
FadeCategory ParseFadeCategory(const Node* row);
std::vector<FadeCategory> ParseFadeCategoriesTable(const Document& doc);

// ===========================================================================
// 12.1 camera_shake.xtbl (spec-tables-environment.md 12.1)
// ===========================================================================
// "Records ... all f32, all read with the always-write accessor" (12.1,
// stated explicitly for this whole table). At most 0x80 = 128 rows; the
// Strength_Graph holds at most 6 elements in the real record layout, but the
// loader does not check the count when reading - all parsed elements are
// kept here.
struct CameraShakeStrengthElement {
    Always<float> time;            // Time
    Always<float> wobble;          // Wobble
    Always<float> destable;        // Destable
    Always<float> wander;          // Wander
    Always<float> jitter;          // Jitter
    Always<float> oscillation1;    // Oscillation1
    Always<float> oscillation2;    // Oscillation2
    Always<float> direct;          // Direct
    Always<float> wanderDirect;    // Wander_Direct
    Always<float> blur;            // Blur
    Always<float> strongVibration; // Strong_Vibration
    Always<float> weakVibration;   // Weak_Vibration
};
struct CameraShake {
    std::optional<std::string> name;  // Name (the key, hashed at load)
    Always<float> wobblePitch, wobbleRoll, wobbleYaw, wobbleVariation;                       // Wobble
    Always<float> destabilizationPitch, destabilizationRoll, destabilizationYaw, destabilizationFrequency; // Destabilization
    Always<float> wanderPitch, wanderYaw, wanderFrequency;                                    // Wander (no Roll)
    Always<float> jitterPitch, jitterRoll, jitterYaw;                                         // Jitter
    Always<float> oscillation1Pitch, oscillation1Roll, oscillation1Yaw, oscillation1Frequency; // Oscillation1
    Always<float> oscillation2Pitch, oscillation2Roll, oscillation2Yaw, oscillation2Frequency; // Oscillation2
    Always<float> directPitch, directRoll, directYaw;                                          // Direct
    Always<float> wanderDirectPitch, wanderDirectYaw;                                          // Wander_Direct (no Roll)
    std::vector<CameraShakeStrengthElement> strengthGraph;                                     // Strength_Graph/Strength_Element
};
CameraShake ParseCameraShake(const Node* row);
std::vector<CameraShake> ParseCameraShakeTable(const Document& doc);

}  // namespace sr3tables_environment

#include "sr3tables_environment/weather_time_of_day.h"
#include "sr3tables_environment/camera_free.h"
