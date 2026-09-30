#pragma once

// weather_time_of_day.xtbl - time segments x weather stages, and the
// per-cell lighting record (spec-tables-environment.md 3). Included from
// sr3tables_environment/tables.h; split into its own file purely because of
// size (~150 named parameters per cell, spec 3.3).
//
// STRUCTURE: Table -> repeated Weather_Time_Segment (3.1), each owning one
// "cell" per weather.xtbl stage; a cell's data comes from a Weather_Stages/
// Stage row matched to a weather.xtbl stage by name (Stage_Name, 3.1/13).
// The cell itself (3.2/3.3) is a sparse override layer: nearly every field
// has its own presence bit, "set when the element is present in the row ...
// absent fields are 'inherit'" (3.2). Reading each field with sr3xtbl's
// "write only if present" accessors (GetFloat / ColorIntensity.present /
// ColorDiv255.present below) reproduces that presence bit exactly: XML
// element found == presence bit set.
//
// TWO FIELDS THIS STRUCT DOES NOT AND CANNOT HAVE: the two cloud-layer slots
// at Horizon +0x1A8 and Overhead +0x1BC are read, per the disassembly, from
// an element literally named "?" (spec 3.3). No XML element can be named
// "?" through ordinary authoring, AND this project's own parser (sr3xtbl,
// see its banner) treats a bare "<?" as the start of a processing
// instruction / XML declaration and skips to the next '>' - so `<?>x</?>`
// does not even produce an element named "?" here (the close tag "</?>"
// then has nothing open to match and is ignored as a StrayCloseTag). These
// two slots are therefore unreachable from any real or synthetic XML this
// table could ever contain, through this engine's own authoring rules AND
// through this project's parser; they are left out of the struct rather
// than guessed at. (Spec 15.3 records that a DIFFERENT, flattened per-
// mission-override vocabulary names the equivalent slots
// horizon_layer3_strength / overhead_layer3_strength - that is a different
// table, not modelled here since it has no filename literal and is not one
// of the 27 named tables this group covers.)
//
// The four "cloud layer" slots per band (Horizon +0x19C.."?", Overhead
// +0x1B0.."?") are also the subject of the spec's own §15.3 naming
// disagreement between this table's vocabulary (Cloud_Layer_Strength /
// Cloud_Normal_Map_Height / Cloud_Highlight for slots 0/1/2) and the
// mission-override vocabulary (horizon_layer0/1/2/3_strength). This struct
// uses THIS table's own element names (Cloud_Layer_Strength etc.), which is
// what a row of weather_time_of_day.xtbl actually contains.

#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_environment {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// A generic 4-component vector read from one element's OWN text as
// whitespace-separated numbers (spec 3.3: particle_ambient_color /
// particle_tod_light_color, accessor 0x00DE03E0, C `strtok`+`atof`). Same
// caveat as ColorRGBAText in tables.h: this project's foundation exposes
// only the engine float grammar (ParseFloat), used here as the closest
// available approximation to `atof`.
struct Vec4Text {
    float x = 0, y = 0, z = 0, w = 0;
};

// One Orbital_Objects/Object row, matched by Object_Name against the cell's
// pre-created per-object records (spec 3.2/3.3). Each of Tint/Scale/Opacity
// has its own presence bit (P(rec+0xBC,5/6/7)); modelled as independent
// optionals/flags rather than one aggregate ColorIntensity, since the spec
// does not describe an "_Intensity" sibling for this Tint (unlike every
// other colour in this table).
struct OrbitalObjectOverride {
    std::optional<std::string> objectName; // Object_Name (matched case-insensitively; unmatched -> ignored)
    bool tintPresent = false;
    ColorRGBAlways tint;          // Tint -> +0x5C (raw colour, no stated _Intensity sibling)
    std::optional<float> scale;   // Scale -> +0x6C
    std::optional<float> opacity; // Opacity -> +0x70
};

// The per-cell lighting-parameter block (spec 3.3): everything read from one
// Weather_Stages/Stage row. Field grouping mirrors the XML nesting exactly.
struct WeatherTimeOfDayCell {
    std::optional<std::string> stageName; // Stage_Name -> matched against weather.xtbl Name (13)

    struct DistrictLightingParams {
        ColorIntensity todLightColor;       // TOD_Light_Color -> +0x90
        ColorIntensity ambientColor;        // Ambient_Color -> +0xA0
        ColorIntensity backAmbientColor;    // Back_Ambient_Color -> +0xB0
        ColorIntensity fogColor;            // Fog_Color -> +0xD0
        std::optional<float> fogGround;              // Fog_Ground -> +0x100
        std::optional<float> fogAtmosphereScale;     // Fog_Atmosphere_Scale -> +0x104
        std::optional<float> fogDensity;             // Fog_Density -> +0x108
        std::optional<float> fogDensityOffset;       // Fog_Density_Offset -> +0x10C
        std::optional<float> groundReflectionGloss;  // Ground_Reflection_Gloss -> +0x20C
        std::optional<float> groundReflectionBrightness; // Ground_Reflection_Brightness -> +0x210
        std::optional<float> groundReflectionSpecBrightness; // Ground_Reflection_Spec_Brightness -> +0x214
        std::optional<float> starStrength;    // Star_Strength -> +0x218
        std::optional<float> meteorStrength;  // Meteor_Strength -> +0x21C
    } lighting; // District_Lighting/Lighting_Parameters/*

    struct SkyboxBand {
        ColorIntensity cloudFrontColor;    // Cloud_Front_Color
        ColorIntensity cloudBackColor;     // Cloud_Back_Color
        std::optional<float> cloudLayerStrength;   // Cloud_Layer_Strength (slot 0 of 4)
        std::optional<float> cloudNormalMapHeight; // Cloud_Normal_Map_Height (slot 1 of 4; spec 15.3:
                                                    // an independent reader names this slot
                                                    // "horizon/overhead_layer1_strength" instead)
        std::optional<float> cloudHighlight;       // Cloud_Highlight (slot 2 of 4; spec 15.3: alt name
                                                    // "..._layer2_strength")
        // slot 3 of 4 ("?") is unreachable - see file banner.
        std::optional<float> stormStrength;        // Storm_Strength
        std::optional<float> cloudSpeed;           // Cloud_Speed (spec 15.3: cloud_layer01_speed for
                                                    // Horizon, cloud_layer23_speed for Overhead)
    };
    SkyboxBand skyboxHorizon; // District_Lighting/Skybox_Parameters/Horizon/* -> +0x17C.."+0x1AC, +0x204
    SkyboxBand skyboxOverhead; // District_Lighting/Skybox_Parameters/Overhead/* -> +0x15C.."+0x1C0, +0x208

    struct SkyboxDirect {
        std::optional<float> backlightStrength;   // Backlight_Strength -> +0x200
        std::optional<float> backlightPower;      // Backlight_Power -> +0x1FC
        ColorIntensity mountainFrontColor;         // Mountain_Front_Color -> +0x1C4
        ColorIntensity mountainBackColor;          // Mountain_Back_Color -> +0x1D4
        ColorIntensity mountainFogColor;           // Mountain_Fog_Color -> +0x1E4
        std::optional<float> mountainFogDensity;   // Mountain_Fog_Density -> +0x1F4
        std::optional<float> mountainNormalMapHeight; // Mountain_Normal_Map_Height -> +0x1F8
    } skyboxDirect; // District_Lighting/Skybox_Parameters/* (direct children)

    struct Water {
        ColorIntensity ambientColor;   // Ambient_Color -> +0x260
        ColorIntensity diffuseColor1;  // Diffuse_Color1 -> +0x270
        ColorIntensity diffuseColor2;  // Diffuse_Color2 -> +0x280
        ColorIntensity specularColor;  // Specular_Color -> +0x290
        std::optional<float> specularAlpha; // Specular_Alpha -> +0x2A0
        std::optional<float> specularPower; // Specular_Power -> +0x2A4
        ColorIntensity falloffColor;   // Falloff_Color -> +0x2A8
        ColorIntensity fogColor;       // Fog_Color -> +0x2B8
        ColorIntensity crestColor;     // Crest_Color -> +0x2C8
        std::optional<float> crestThreshold; // Crest_Threshold -> +0x2D8
    } water; // District_Lighting/Water/*

    struct ColorCorrection {
        ColorIntensity windowTint;             // Window_Tint -> +0xC0
        std::optional<std::string> lutFilename; // LUT_Filename/Filename -> char[0x40] at +0x220
    } colorCorrection; // District_Lighting/Color_Correction/*

    struct Exposure {
        std::optional<float> desiredBrightness; // Desired_Brightness -> +0x2E0
        std::optional<float> exposureMin;       // Exposure_Min -> +0x2E4
        std::optional<float> exposureMax;       // Exposure_Max -> +0x2E8
    } exposure; // District_Lighting/Exposure/*

    std::optional<float> todAudioAmbientRtpc; // District_Lighting/TOD_Audio/Ambient_RTPC -> +0x2DC

    // District_Skybox/* - nine 4-float colours (spec 3.3 QUIRK: the four
    // West* colours share ONE presence bit (only West1's presence is
    // actually observable through it, since West4..West1 are assigned in
    // that order and the last write wins) and likewise the four East*
    // share one bit reflecting only East1. Each ColorIntensity below still
    // reports its OWN element's presence independently - the shared-bit
    // quirk is a runtime-storage fact, not an XML-reading one, so it does
    // not collapse what this struct can observe.
    ColorIntensity west1, west2, west3, west4; // +0x00, +0x10, +0x20, +0x30
    ColorIntensity east1, east2, east3, east4; // +0x40, +0x50, +0x60, +0x70
    ColorIntensity westZenith;                  // West_Zenith -> +0x80

    // Direct children of Stage, read from the element's OWN text (spec 3.3:
    // accessor 0x00DE0200, C `atof`-style; the caveat in tables.h's
    // ColorRGBAText banner applies equally here - this reader uses
    // sr3xtbl::ParseFloat, the engine float grammar, as the closest
    // available substitute for the spec's stated C `atof`).
    std::optional<Vec4Text> particleAmbientColor;    // particle_ambient_color -> +0xE0
    std::optional<Vec4Text> particleTodLightColor;   // particle_tod_light_color -> +0xF0
    std::optional<float> ldrMin, ldrMax;                       // +0x110, +0x114
    std::optional<float> bloomExposure;                        // +0x118
    std::optional<float> irisRate;                              // +0x11C
    std::optional<float> luminanceMax, luminanceMin;            // +0x120, +0x124
    std::optional<float> luminanceMaskMax;                      // +0x128
    std::optional<float> eyeAdaptionBase, eyeAdaptionAmount;    // +0x12C, +0x130 (spelling "eye_adaption",
                                                                 // not "adaptation" - the shipped literal)
    std::optional<float> eyeFadeMin, eyeFadeMax;                // +0x134, +0x138
    std::optional<float> brightpassThresholdNew;                // +0x13C
    std::optional<float> brightpassOffsetNew;                   // +0x140
    std::optional<float> bloomAmount;                            // +0x144
    std::optional<float> bloomTheta;                             // +0x148
    std::optional<float> bloomSlopeA, bloomSlopeB;               // +0x14C, +0x150
    std::optional<float> tonemapLumRange;                        // +0x154
    std::optional<float> tonemapLumOffset;                       // +0x158

    std::vector<OrbitalObjectOverride> orbitalObjects; // Orbital_Objects/Object (repeated)
};

WeatherTimeOfDayCell ParseWeatherTimeOfDayCell(const Node* stageRow);

struct WeatherTimeSegment {
    std::optional<std::string> name;              // Name
    Always<uint32_t> startTimeHHMM;                 // Start_Time - RAW integer HHMM; the engine converts
                                                     // this to a fraction-of-day float (load-time transform,
                                                     // spec 3.1)
    Always<uint32_t> rampOutTimeMinutes;             // Ramp_Out_Time - RAW integer minutes
    std::vector<WeatherTimeOfDayCell> stages;       // Weather_Stages/Stage (repeated)
};

WeatherTimeSegment ParseWeatherTimeSegment(const Node* row);
std::vector<WeatherTimeSegment> ParseWeatherTimeOfDayTable(const Document& doc);

}  // namespace sr3tables_environment
