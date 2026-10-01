// Parse functions for sr3tables_environment (see include/sr3tables_environment/
// tables.h, weather_time_of_day.h, camera_free.h for the struct definitions
// and per-field spec citations). Built ONLY on sr3xtbl's accessors
// (include/sr3xtbl/xtbl.h); nothing here touches the game executable,
// disassembly or decompiled code. Source: spec-tables-environment.md.

#include "sr3tables_environment/tables.h"

#include <cctype>

namespace sr3tables_environment {

namespace {

using sr3xtbl::Always;
using sr3xtbl::Node;

std::optional<std::string> getText(const Node* node, std::string_view name = {}) {
    const std::string* t = sr3xtbl::ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string_view> splitWs(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isWs(s[i])) ++i;
        const size_t start = i;
        while (i < s.size() && !isWs(s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

// spec-tables-environment.md 9.3 / 11.5: "r g b [a]", C `sscanf`, unspecified
// trailing components stay 1.0. sr3xtbl exposes only the engine float
// grammar (ParseFloat), used here as the closest available substitute for
// the spec's stated `sscanf`/`atof` (see the ColorRGBAText banner in
// tables.h).
std::optional<ColorRGBAText> parseColorRGBAText(const std::string* text) {
    if (!text) return std::nullopt;
    const std::vector<std::string_view> toks = splitWs(*text);
    if (toks.empty()) return std::nullopt;
    ColorRGBAText c;  // defaults r=g=b=a=1.0
    if (toks.size() >= 1) c.r = sr3xtbl::ParseFloat(toks[0]);
    if (toks.size() >= 2) c.g = sr3xtbl::ParseFloat(toks[1]);
    if (toks.size() >= 3) c.b = sr3xtbl::ParseFloat(toks[2]);
    if (toks.size() >= 4) c.a = sr3xtbl::ParseFloat(toks[3]);
    return c;
}

// weather_time_of_day.xtbl's particle_ambient_color / particle_tod_light_color
// (spec 3.3): a vec4 read from one element's OWN text (0x00DE03E0, C
// `strtok`+`atof`). Same ParseFloat-as-substitute caveat as above.
std::optional<Vec4Text> readVec4TextChild(const Node* parent, std::string_view name) {
    const Node* n = sr3xtbl::FindChild(parent, name);
    if (!n || !n->text()) return std::nullopt;
    const std::vector<std::string_view> toks = splitWs(*n->text());
    if (toks.empty()) return std::nullopt;
    Vec4Text v;
    if (toks.size() >= 1) v.x = sr3xtbl::ParseFloat(toks[0]);
    if (toks.size() >= 2) v.y = sr3xtbl::ParseFloat(toks[1]);
    if (toks.size() >= 3) v.z = sr3xtbl::ParseFloat(toks[2]);
    if (toks.size() >= 4) v.w = sr3xtbl::ParseFloat(toks[3]);
    return v;
}

// 0x00DAD0A0-family: children R, G, B read raw (not divided by 255), always-write.
ColorRGBAlways ReadColorRGBAlways(const Node* node) {
    ColorRGBAlways c;
    c.r = sr3xtbl::ReadFloatAlways(node, "R");
    c.g = sr3xtbl::ReadFloatAlways(node, "G");
    c.b = sr3xtbl::ReadFloatAlways(node, "B");
    return c;
}

Always<float> divBy255(Always<float> raw) {
    return Always<float>{raw.present ? raw.value / 255.0f : 0.0f, raw.present};
}

// weather.xtbl / lightning.xtbl convention (spec 1.4, 0x00DAD0A0/0x00DAD130):
// named child's R/G/B divided by 255.0. `present` = whether the named colour
// element itself was found.
ColorDiv255 ReadColorDiv255(const Node* parent, std::string_view name) {
    ColorDiv255 c;
    const Node* n = sr3xtbl::FindChild(parent, name);
    if (!n) return c;
    c.present = true;
    const ColorRGBAlways raw = ReadColorRGBAlways(n);
    c.r = divBy255(raw.r);
    c.g = divBy255(raw.g);
    c.b = divBy255(raw.b);
    return c;
}

// weather_time_of_day.xtbl convention (spec 3.3, 0x00BA1510): named colour
// element's R/G/B read raw (NOT /255), plus a write-if-present sibling
// "<Name>_Intensity" with engine default 1.0. `present` = whether the colour
// element itself was found (the function's own documented return value).
ColorIntensity ReadColorIntensity(const Node* parent, std::string_view name) {
    ColorIntensity ci;
    const Node* n = sr3xtbl::FindChild(parent, name);
    if (!n) return ci;
    ci.present = true;
    ci.rgb = ReadColorRGBAlways(n);
    const std::string intensityName = std::string(name) + "_Intensity";
    if (auto v = sr3xtbl::GetFloat(parent, intensityName)) {
        ci.intensity = *v;
        ci.intensityPresent = true;
    }
    return ci;
}

}  // namespace

// ===========================================================================
// 2. weather.xtbl
// ===========================================================================
WeatherStage ParseWeatherStage(const Node* row) {
    WeatherStage w;
    w.name = getText(row, "Name");
    w.displayName = getText(row, "DisplayName");

    const Node* stageSettings = sr3xtbl::FindChild(row, "Stage_Settings");
    w.chance = sr3xtbl::ReadFloatAlways(stageSettings, "Chance");
    w.averageDuration = sr3xtbl::ReadFloatAlways(stageSettings, "Average_Duration");
    w.variance = sr3xtbl::ReadFloatAlways(stageSettings, "Variance");
    w.pedDensity = sr3xtbl::ReadFloatAlways(stageSettings, "PedDensity");

    const Node* rainSettings = sr3xtbl::FindChild(row, "Rain_Settings");
    w.rainDensity = sr3xtbl::ReadFloatAlways(rainSettings, "Rain_Density");
    const Node* lightningSettings = sr3xtbl::FindChild(rainSettings, "Lightning_Settings");
    w.lightningFrequency = sr3xtbl::ReadFloatAlways(lightningSettings, "Frequency");
    w.lightningVariance = sr3xtbl::ReadFloatAlways(lightningSettings, "Variance");

    const Node* rainFog = sr3xtbl::FindChild(row, "Rain_Fog_Settings");
    const Node* layer1 = sr3xtbl::FindChild(rainFog, "Layer_1");
    w.rainFogLayer1UScale = sr3xtbl::ReadFloatAlways(layer1, "U_Scale");
    w.rainFogLayer1VScale = sr3xtbl::ReadFloatAlways(layer1, "V_Scale");
    w.rainFogLayer1ScrollSpeed = sr3xtbl::ReadFloatAlways(layer1, "Scroll_Speed");
    w.rainFogLayer1Opacity = sr3xtbl::ReadFloatAlways(layer1, "Opacity");
    const Node* layer2 = sr3xtbl::FindChild(rainFog, "Layer_2");
    w.rainFogLayer2UScale = sr3xtbl::ReadFloatAlways(layer2, "U_Scale");
    w.rainFogLayer2VScale = sr3xtbl::ReadFloatAlways(layer2, "V_Scale");
    w.rainFogLayer2ScrollSpeed = sr3xtbl::ReadFloatAlways(layer2, "Scroll_Speed");
    w.rainFogLayer2Opacity = sr3xtbl::ReadFloatAlways(layer2, "Opacity");

    const Node* todSettings = sr3xtbl::FindChild(row, "TOD_Settings");
    w.todLightsPct = sr3xtbl::ReadFloatAlways(todSettings, "Tod_Lights_Pct");
    const Node* skydomeSettings = sr3xtbl::FindChild(todSettings, "Skydome_Settings");
    w.skydomeBlendFactor = sr3xtbl::ReadFloatAlways(skydomeSettings, "Blend_Factor");
    w.skydomeColor = ReadColorDiv255(skydomeSettings, "Color");

    const Node* sunSettings = sr3xtbl::FindChild(row, "Sun_Settings");
    w.sunColorMultiply = ReadColorDiv255(sunSettings, "Sun_Color_Multiply");
    w.sunGlowColorMultiply = ReadColorDiv255(sunSettings, "Sun_Glow_Color_Multiply");
    w.sunOpacity = sr3xtbl::ReadFloatAlways(sunSettings, "Sun_Opacity");

    const Node* moon = sr3xtbl::FindChild(row, "Moon");
    w.moonColorMultiply = ReadColorDiv255(moon, "Moon_Color_Multiply");

    const Node* windSettings = sr3xtbl::FindChild(row, "Wind_Settings");
    w.windAverageSpeed = sr3xtbl::ReadFloatAlways(windSettings, "Average_Speed");
    w.windGustSpeed = sr3xtbl::ReadFloatAlways(windSettings, "Gust_Speed");
    w.windFrequency = sr3xtbl::ReadFloatAlways(windSettings, "Frequency");
    w.windVariance = sr3xtbl::ReadFloatAlways(windSettings, "Variance");

    const Node* cloudSettings = sr3xtbl::FindChild(row, "Cloud_Settings");
    const Node* mapStrengths = sr3xtbl::FindChild(cloudSettings, "Map_Strengths");
    w.cloudHorizonCirrus = sr3xtbl::ReadFloatAlways(mapStrengths, "Horizon_Cirrus");
    w.cloudHorizonCumulus = sr3xtbl::ReadFloatAlways(mapStrengths, "Horizon_Cumulus");
    w.cloudHorizonStorm = sr3xtbl::ReadFloatAlways(mapStrengths, "Horizon_Storm");
    w.cloudOverheadCirrus = sr3xtbl::ReadFloatAlways(mapStrengths, "Overhead_Cirrus");
    w.cloudOverheadCumulus = sr3xtbl::ReadFloatAlways(mapStrengths, "Overhead_Cumulus");
    w.cloudOverheadStorm = sr3xtbl::ReadFloatAlways(mapStrengths, "Overhead_Storm");
    const Node* scrolling = sr3xtbl::FindChild(cloudSettings, "Scrolling");
    w.cloudHorizonScrollRate = sr3xtbl::ReadFloatAlways(scrolling, "Horizon_Scroll_Rate");
    w.cloudOverheadScrollRate = sr3xtbl::ReadFloatAlways(scrolling, "Overhead_Scroll_Rate");

    const Node* tempSettings = sr3xtbl::FindChild(row, "Temp_Settings");
    w.minTempDay = sr3xtbl::ReadFloatAlways(tempSettings, "Min_Temp_Day");
    w.maxTempDay = sr3xtbl::ReadFloatAlways(tempSettings, "Max_Temp_Day");
    w.minTempNight = sr3xtbl::ReadFloatAlways(tempSettings, "Min_Temp_Night");
    w.maxTempNight = sr3xtbl::ReadFloatAlways(tempSettings, "Max_Temp_Night");

    const Node* ambientWave = sr3xtbl::FindChild(row, "Ambient_Wave_Settings");
    w.ambientWaveSpeed = sr3xtbl::ReadFloatAlways(ambientWave, "Ambient_Wave_Speed");
    w.ambientWaveAmplitude = sr3xtbl::ReadFloatAlways(ambientWave, "Ambient_Wave_Amplitude");

    const Node* audio = sr3xtbl::FindChild(row, "Audio");
    w.audioIntensity = sr3xtbl::ReadFloatAlways(audio, "Audio_Intensity");

    const Node* nextStageList = sr3xtbl::FindChild(row, "Next_Stage_List");
    for (const Node* n : sr3xtbl::Children(nextStageList, "Next_Stage"))
        if (n->text()) w.nextStageNames.push_back(*n->text());

    return w;
}

std::vector<WeatherStage> ParseWeatherTable(const Document& doc) {
    std::vector<WeatherStage> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Weather_Stage")) out.push_back(ParseWeatherStage(row));
    return out;
}

// ===========================================================================
// 4. wind.xtbl
// ===========================================================================
WindStage ParseWindStage(const Node* row) {
    WindStage w;
    w.name = getText(row, "Name");
    w.displayName = getText(row, "Display_Name");
    const Node* stageSettings = sr3xtbl::FindChild(row, "Stage_Settings");
    w.chance = sr3xtbl::ReadFloatAlways(stageSettings, "Chance");
    w.averageDuration = sr3xtbl::ReadFloatAlways(stageSettings, "Average_Duration");
    w.variance = sr3xtbl::ReadFloatAlways(stageSettings, "Variance");
    const Node* windSettings = sr3xtbl::FindChild(row, "Wind_Settings");
    w.windAverageIntensity = sr3xtbl::ReadFloatAlways(windSettings, "Average_Intensity");
    const Node* nextStageList = sr3xtbl::FindChild(row, "Next_Stage_List");
    for (const Node* n : sr3xtbl::Children(nextStageList, "Next_Stage"))
        if (n->text()) w.nextStageNames.push_back(*n->text());
    return w;
}

std::vector<WindStage> ParseWindTable(const Document& doc) {
    std::vector<WindStage> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Wind_Stage")) out.push_back(ParseWindStage(row));
    return out;
}

// ===========================================================================
// 5.1 rain.xtbl
// ===========================================================================
RainLevel ParseRainLevel(const Node* row) {
    RainLevel r;
    const Node* params = sr3xtbl::FindChild(row, "Parameters");
    // [OPEN - spec-tables-environment.md 1.4 (engine integer grammar): "this document does not say which accessor
    // (signed or unsigned) each integer field below uses (`Density`, `Start_Time`, `On_Time`, ...), so whether a
    // negative value survives is not pinned; to be settled against the executable". Unsigned read kept.]
    r.densityRaw = sr3xtbl::ReadUInt32Always(params, "Density");
    r.viewRadius = sr3xtbl::ReadFloatAlways(params, "View_Radius");
    r.speed = sr3xtbl::ReadFloatAlways(params, "Speed");
    r.opacity = sr3xtbl::ReadFloatAlways(params, "Opacity");
    r.lengthNear = sr3xtbl::ReadFloatAlways(params, "Length_Near");
    r.lengthFar = sr3xtbl::ReadFloatAlways(params, "Length_Far");
    r.widthNear = sr3xtbl::ReadFloatAlways(params, "Width_Near");
    r.widthFar = sr3xtbl::ReadFloatAlways(params, "Width_Far");
    r.splashLifetime = sr3xtbl::ReadFloatAlways(params, "Splash_Lifetime");
    r.splashSizeNear = sr3xtbl::ReadFloatAlways(params, "Splash_Size_Near");
    r.splashSizeFar = sr3xtbl::ReadFloatAlways(params, "Splash_Size_Far");
    r.windAmount = sr3xtbl::ReadFloatAlways(params, "Wind_Amount");
    r.effect = getText(params, "Effect");
    r.cameraDropEffect = getText(params, "Camera_drop_effect");
    return r;
}

std::vector<RainLevel> ParseRainTable(const Document& doc) {
    std::vector<RainLevel> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Level")) out.push_back(ParseRainLevel(row));
    return out;
}

// ===========================================================================
// 5.2 lightning.xtbl
// ===========================================================================
LightningType ParseLightningType(const Node* row) {
    LightningType l;
    l.probability = sr3xtbl::ReadFloatAlways(row, "Probability");
    const Node* vfxList = sr3xtbl::FindChild(row, "VFX_List");
    for (const Node* v : sr3xtbl::Children(vfxList, "VFX"))
        if (v->text()) l.vfxNames.push_back(*v->text());
    const Node* todOverrides = sr3xtbl::FindChild(row, "TOD_Overrides");
    l.fogColorOverride = ReadColorDiv255(todOverrides, "Fog_Color_Override");
    l.fogStrengthOverride = sr3xtbl::GetFloat(todOverrides, "Fog_Strength_Override");
    l.ambientOverride = ReadColorDiv255(todOverrides, "Ambient_Override");
    l.cloudBrightnessOverride = sr3xtbl::GetFloat(todOverrides, "Cloud_Brightness_Override");
    l.cloudContrastOverride = sr3xtbl::GetFloat(todOverrides, "Cloud_Contrast_Override");
    l.skyBrightnessOverride = sr3xtbl::GetFloat(todOverrides, "Sky_Brightness_Override");
    l.todLightOverride = ReadColorDiv255(todOverrides, "TOD_Light_Override");
    return l;
}

std::vector<LightningType> ParseLightningTable(const Document& doc) {
    std::vector<LightningType> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Lightning_Type")) out.push_back(ParseLightningType(row));
    return out;
}

// ===========================================================================
// 6.1 lens_flares.xtbl
// ===========================================================================
LensFlare ParseLensFlare(const Node* row) {
    LensFlare f;
    f.imageFilename = getText(sr3xtbl::FindChild(row, "ImageFilename"), "Filename");
    f.radius = sr3xtbl::ReadFloatAlways(row, "Radius");
    f.scale = sr3xtbl::ReadFloatAlways(row, "Scale");
    f.baseAlpha = sr3xtbl::ReadFloatAlways(row, "BaseAlpha");
    return f;
}

std::vector<LensFlare> ParseLensFlaresTable(const Document& doc) {
    std::vector<LensFlare> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Flare")) out.push_back(ParseLensFlare(row));
    return out;
}

// ===========================================================================
// 6.2 motion_blur.xtbl
// ===========================================================================
std::optional<MotionBlurSettings> ParseMotionBlurSettings(const Document& doc) {
    const Node* mb = sr3xtbl::FindChild(doc.table(), "Motion_Blur_Settings");
    if (!mb) return std::nullopt;
    MotionBlurSettings s;

    const Node* walkRun = sr3xtbl::FindChild(mb, "On_Foot_Walk_Run");
    s.onFootWalkRunStrength = sr3xtbl::ReadFloatAlways(walkRun, "Strength");
    s.onFootWalkRunMaxOffset = sr3xtbl::ReadFloatAlways(walkRun, "Max_Offset");

    const Node* sprint = sr3xtbl::FindChild(mb, "On_Foot_Sprint");
    s.onFootSprintStrength = sr3xtbl::ReadFloatAlways(sprint, "Strength");
    s.onFootSprintMaxOffset = sr3xtbl::ReadFloatAlways(sprint, "Max_Offset");

    const Node* freefall = sr3xtbl::FindChild(mb, "On_Foot_Freefall");
    s.onFootFreefallStrength = sr3xtbl::ReadFloatAlways(freefall, "Strength");
    s.onFootFreefallMaxOffset = sr3xtbl::ReadFloatAlways(freefall, "Max_Offset");
    s.onFootFreefallMinFallVelocity = sr3xtbl::ReadFloatAlways(freefall, "Min_Fall_Velocity");
    s.onFootFreefallMaxFallVelocity = sr3xtbl::ReadFloatAlways(freefall, "Max_Fall_Velocity");

    const Node* explosion = sr3xtbl::FindChild(mb, "On_Foot_Explosion");
    s.onFootExplosionWorldStrength = sr3xtbl::ReadFloatAlways(explosion, "World_Strength");
    s.onFootExplosionTargetStrength = sr3xtbl::ReadFloatAlways(explosion, "Target_Strength");
    s.onFootExplosionMaxOffset = sr3xtbl::ReadFloatAlways(explosion, "Max_Offset");

    auto readBase = [&](const char* name) {
        MotionBlurSettings::BaseGroup g;
        const Node* n = sr3xtbl::FindChild(mb, name);
        g.worldStrength = sr3xtbl::ReadFloatAlways(n, "World_Strength");
        g.targetStrength = sr3xtbl::ReadFloatAlways(n, "Target_Strength");
        g.maxOffset = sr3xtbl::ReadFloatAlways(n, "Max_Offset");
        g.fakeVelocityScale = sr3xtbl::ReadFloatAlways(n, "Fake_Velocity_Scale");
        return g;
    };
    s.vehicleBase = readBase("Vehicle_Base");
    s.airplaneBase = readBase("Airplane_Base");
    s.helicopterBase = readBase("Helicopter_Base");

    auto readBurst = [&](const char* name) {
        MotionBlurSettings::BurstGroup g;
        const Node* n = sr3xtbl::FindChild(mb, name);
        g.worldStrength = sr3xtbl::ReadFloatAlways(n, "World_Strength");
        g.targetStrength = sr3xtbl::ReadFloatAlways(n, "Target_Strength");
        g.maxOffset = sr3xtbl::ReadFloatAlways(n, "Max_Offset");
        g.duration = sr3xtbl::ReadFloatAlways(n, "Duration");
        g.decayTime = sr3xtbl::ReadFloatAlways(n, "Decay_Time");
        g.fakeVelocityScale = sr3xtbl::ReadFloatAlways(n, "Fake_Velocity_Scale");
        return g;
    };
    s.vehicleNitrous = readBurst("Vehicle_Nitrous");
    s.vehiclePeelout = readBurst("Vehicle_Peelout");

    return s;
}

// ===========================================================================
// 6.3 radial_blur.xtbl
// ===========================================================================
RadialBlur ParseRadialBlur(const Node* row) {
    RadialBlur r;
    r.name = sr3xtbl::CopyText(row, "Name", 0x40);
    r.strength = sr3xtbl::ReadFloatAlways(row, "Strength");
    r.duration = sr3xtbl::ReadFloatAlways(row, "Duration");
    r.radius = sr3xtbl::ReadFloatAlways(row, "Radius");
    r.distanceFade = sr3xtbl::ReadFloatAlways(row, "Distance_fade");
    r.priority = sr3xtbl::ReadInt32Always(row, "Priority");
    return r;
}

std::vector<RadialBlur> ParseRadialBlurTable(const Document& doc) {
    std::vector<RadialBlur> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Radial_blur")) out.push_back(ParseRadialBlur(row));
    return out;
}

// ===========================================================================
// 7.1 dof_situations.xtbl
// ===========================================================================
DofSituation ParseDofSituation(const Node* row) {
    DofSituation d;
    d.name = getText(row, "name");
    d.startMultiplierA = sr3xtbl::ReadFloatAlways(row, "Start_Multiplier_A");
    d.startMultiplierB = sr3xtbl::ReadFloatAlways(row, "Start_Multiplier_B");
    d.endMultiplierA = sr3xtbl::ReadFloatAlways(row, "End_Multiplier_A");
    d.endMultiplierB = sr3xtbl::ReadFloatAlways(row, "End_Multiplier_B");
    d.blurRadius = sr3xtbl::ReadFloatAlways(row, "Blur_Radius");
    d.transitionSpeed = sr3xtbl::ReadFloatAlways(row, "Transition_Speed");
    d.humanSpherecastRadius = sr3xtbl::ReadFloatAlways(row, "Human_Spherecast_Radius");
    return d;
}

std::vector<DofSituation> ParseDofSituationsTable(const Document& doc) {
    std::vector<DofSituation> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "DOF_situation")) out.push_back(ParseDofSituation(row));
    return out;
}

// ===========================================================================
// 8. refraction_situations.xtbl
// ===========================================================================
RefractionSituation ParseRefractionSituation(const Node* row) {
    RefractionSituation r;
    r.name = getText(row, "name");
    r.scale = sr3xtbl::ReadFloatAlways(row, "Scale");
    r.frequency = sr3xtbl::ReadFloatAlways(row, "Frequency");
    r.offsetDelta = sr3xtbl::ReadFloatAlways(row, "Offset_Delta");
    r.fadeInTime = sr3xtbl::ReadFloatAlways(row, "Fade_In_Time");
    r.fadeOutTime = sr3xtbl::ReadFloatAlways(row, "Fade_Out_Time");
    r.duration = sr3xtbl::GetFloat(row, "Duration");

    const Node* spasm = sr3xtbl::FindChild(row, "Spasm");
    r.spasmPresent = spasm != nullptr;
    r.spasmTimeMin = sr3xtbl::ReadFloatAlways(spasm, "Spasm_Time_Min");
    r.spasmTimeMax = sr3xtbl::ReadFloatAlways(spasm, "Spasm_Time_Max");
    r.spasmFrequencyMin = sr3xtbl::ReadFloatAlways(spasm, "Frequency_Min");
    r.spasmFrequencyMax = sr3xtbl::ReadFloatAlways(spasm, "Frequency_Max");
    r.spasmScaleMin = sr3xtbl::ReadFloatAlways(spasm, "Scale_Min");
    r.spasmScaleMax = sr3xtbl::ReadFloatAlways(spasm, "Scale_Max");
    r.spasmDurationMin = sr3xtbl::ReadFloatAlways(spasm, "Duration_Min");
    r.spasmDurationMax = sr3xtbl::ReadFloatAlways(spasm, "Duration_Max");

    r.framework = getText(row, "Framework");
    return r;
}

std::vector<RefractionSituation> ParseRefractionSituationsTable(const Document& doc) {
    std::vector<RefractionSituation> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "refraction_situation")) out.push_back(ParseRefractionSituation(row));
    return out;
}

// ===========================================================================
// 9.1 skybox_effects.xtbl
// ===========================================================================
SkyboxEffect ParseSkyboxEffect(const Node* row) {
    SkyboxEffect s;
    s.name = getText(row, "Name");
    s.effect = getText(row, "Effect");
    const Node* autoSpawn = sr3xtbl::FindChild(row, "Auto_Spawn");
    s.autoSpawnPresent = autoSpawn != nullptr;
    s.autoSpawnMinTimeSpacing = sr3xtbl::ReadFloatAlways(autoSpawn, "Min_Time_Spacing");
    s.autoSpawnMaxTimeSpacing = sr3xtbl::ReadFloatAlways(autoSpawn, "Max_Time_Spacing");
    s.randomOrientation = sr3xtbl::ReadBoolAlways(row, "Random_Orientation");
    const Node* todRange = sr3xtbl::FindChild(row, "TODRange");
    s.todRangeStartTime = sr3xtbl::GetUInt16(todRange, "Start_Time");
    s.todRangeEndTime = sr3xtbl::GetUInt16(todRange, "End_Time");
    s.weatherStage = getText(row, "Weather_Stage");
    s.skyboxLayer = sr3xtbl::ReadUInt8Always(row, "Skybox_Layer");
    return s;
}

std::vector<SkyboxEffect> ParseSkyboxEffectsTable(const Document& doc) {
    std::vector<SkyboxEffect> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Skybox_Effect")) out.push_back(ParseSkyboxEffect(row));
    return out;
}

// ===========================================================================
// 9.2 time_of_day_objects.xtbl
// ===========================================================================
TimeOfDayObjectType ParseTimeOfDayObjectType(const Node* row) {
    TimeOfDayObjectType t;
    t.name = getText(row, "Name");
    // [OPEN - spec-tables-environment.md 1.4: accessor signedness of On_Time/Off_Time/Variation not pinned
    // (ReadUInt32Always kept; a negative text value would not survive).]
    t.onTimeHHMM = sr3xtbl::ReadUInt32Always(row, "On_Time");
    t.offTimeHHMM = sr3xtbl::ReadUInt32Always(row, "Off_Time");
    t.variationMinutes = sr3xtbl::ReadUInt32Always(row, "Variation");
    return t;
}

std::vector<TimeOfDayObjectType> ParseTimeOfDayObjectsTable(const Document& doc) {
    std::vector<TimeOfDayObjectType> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Object_Type")) out.push_back(ParseTimeOfDayObjectType(row));
    return out;
}

// ===========================================================================
// 9.3 external_light_override.xtbl
// ===========================================================================
LightOverride ParseLightOverride(const Node* row) {
    LightOverride l;
    l.name = getText(row, "Name");
    l.frontColor = parseColorRGBAText(sr3xtbl::ChildText(row, "front_color"));
    l.backColor = parseColorRGBAText(sr3xtbl::ChildText(row, "back_color"));
    l.frontIntensity = sr3xtbl::GetFloat(row, "front_intensity");
    l.backIntensity = sr3xtbl::GetFloat(row, "back_intensity");
    return l;
}

std::vector<LightOverride> ParseExternalLightOverrideTable(const Document& doc) {
    std::vector<LightOverride> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "light_override")) out.push_back(ParseLightOverride(row));
    return out;
}

// ===========================================================================
// 10.1 effects.xtbl
// ===========================================================================
Effect ParseEffect(const Node* row) {
    Effect e;
    e.name = sr3xtbl::CopyText(row, "Name", 0x40);
    e.framework = getText(row, "Framework");
    e.infoSlotIndex = sr3xtbl::GetInt32(row, "Info_Slot_Index");
    e.visual = getText(row, "visual");
    e.sound = getText(row, "sound");
    e.soundParentSwitchName = getText(row, "sound_parent_switch_name");
    e.soundSwitchName = getText(row, "sound_switch_name");
    e.vfxKillParticlesFadeTime = sr3xtbl::GetFloat(row, "vfx_kill_particles_fade_time");
    e.scaleFactor = sr3xtbl::GetFloat(row, "scale_factor");

    const Node* dmg = sr3xtbl::FindChild(row, "Damage_Region");
    e.damageRegionPresent = dmg != nullptr;
    e.damageRegionType = getText(dmg, "Region_Type");
    if (auto shape = getText(dmg, "Region_Shape")) e.damageRegionIsBox = sr3xtbl::NameEquals(*shape, "Box");
    e.damageRegionOffset = sr3xtbl::ReadVec3Child(dmg, "Region_Offset");
    e.damageRegionSize = sr3xtbl::ReadVec3Child(dmg, "Region_Size");

    e.restartSoundAtLoop = sr3xtbl::GetBool(row, "restart_sound_at_loop");
    e.killSoundWhenDone = sr3xtbl::GetBool(row, "kill_sound_when_done");
    e.soundFollowsEffect = sr3xtbl::GetBool(row, "sound_follows_effect");
    e.vfxKillParticles = sr3xtbl::GetBool(row, "vfx_kill_particles");
    e.stopWhenHostDestroyed = sr3xtbl::GetBool(row, "stop_when_host_destroyed");
    e.stopUnderCar = sr3xtbl::GetBool(row, "stop_under_car");
    e.stopUnderPlayer = sr3xtbl::GetBool(row, "stop_under_player");
    e.onlyAtNight = sr3xtbl::GetBool(row, "only_at_night");
    e.useMissionSrid = sr3xtbl::GetBool(row, "use_mission_srid");

    const Node* prox = sr3xtbl::FindChild(row, "Proximity_Refraction");
    e.proximityRefractionPresent = prox != nullptr;
    e.proximityRefractionSituation = getText(prox, "Situation");
    e.proximityRefractionNearRadius = sr3xtbl::ReadFloatAlways(prox, "Near_Radius");
    e.proximityRefractionFarRadius = sr3xtbl::ReadFloatAlways(prox, "Far_Radius");

    return e;
}

std::vector<Effect> ParseEffectsTable(const Document& doc) {
    std::vector<Effect> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Effect")) out.push_back(ParseEffect(row));
    return out;
}

// ===========================================================================
// 10.2 vfx.xtbl
// ===========================================================================
VfxEffect ParseVfxEffect(const Node* row) {
    VfxEffect v;
    v.name = sr3xtbl::CopyText(row, "Name", 0x40);
    v.framework = getText(row, "Framework");
    v.vfxFilename = sr3xtbl::CopyText(sr3xtbl::FindChild(row, "VFX"), "Filename", 0x40);
    v.streamingCategory = getText(row, "Streaming_Category");
    v.radius = sr3xtbl::GetFloat(row, "Radius");
    v.radiusExpands = sr3xtbl::GetBool(row, "Radius_expands");

    const Node* blocker = sr3xtbl::FindChild(row, "Blocker");
    v.blockerPresent = blocker != nullptr;
    v.blockerOpacity = sr3xtbl::GetFloat(blocker, "Opacity");
    v.blockerLifeSpan = sr3xtbl::ReadInt32Always(blocker, "LifeSpan");

    v.radialBlurEntry = getText(sr3xtbl::FindChild(row, "Radial_blur"), "Radial_blur_entry");

    const Node* lod = sr3xtbl::FindChild(row, "LOD");
    v.lodPresent = lod != nullptr;
    const Node* spawning = sr3xtbl::FindChild(lod, "Spawning");
    v.lodSpawningDistance = sr3xtbl::GetFloat(spawning, "Distance");
    v.lodSpawningView = sr3xtbl::GetBool(spawning, "View");
    const Node* distance = sr3xtbl::FindChild(lod, "Distance");
    v.lodDistanceFadingStart = sr3xtbl::GetFloat(distance, "Fading_start");
    v.lodDistanceFadingEnd = sr3xtbl::GetFloat(distance, "Fading_end");
    v.lodDistanceRestore = sr3xtbl::GetBool(distance, "Restore");
    const Node* update = sr3xtbl::FindChild(lod, "Update");
    v.lodUpdatePresent = update != nullptr;
    v.lodUpdateMinimumTime = sr3xtbl::GetFloat(update, "Minimum_time");

    v.startTime = sr3xtbl::GetFloat(row, "Start_time");
    return v;
}

std::vector<VfxEffect> ParseVfxTable(const Document& doc) {
    std::vector<VfxEffect> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Effect")) out.push_back(ParseVfxEffect(row));
    return out;
}

// ===========================================================================
// 11.1 interface_effects.xtbl
// ===========================================================================
InterfaceEffect ParseInterfaceEffect(const Node* row) {
    InterfaceEffect ie;
    ie.name = sr3xtbl::CopyText(row, "Name", 0x20);
    const Node* camera = sr3xtbl::FindChild(row, "Camera");
    ie.cameraFov = sr3xtbl::ReadFloatAlways(camera, "FOV");
    ie.cameraBlur = sr3xtbl::GetFloat(row, "Camera_Blur");  // direct child of the row, not under Camera
    const Node* lut = sr3xtbl::FindChild(row, "LUT");
    ie.lutPresent = lut != nullptr;
    ie.lutName = sr3xtbl::CopyText(lut, "LutName", 0x20);
    ie.lutStrength = sr3xtbl::ReadFloatAlways(lut, "LutStrength");
    return ie;
}

std::vector<InterfaceEffect> ParseInterfaceEffectsTable(const Document& doc) {
    std::vector<InterfaceEffect> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "InterfaceEffect")) out.push_back(ParseInterfaceEffect(row));
    return out;
}

// ===========================================================================
// 11.2 decal_info.xtbl
// ===========================================================================
DecalInfo ParseDecalInfo(const Node* row) {
    DecalInfo d;
    d.name = sr3xtbl::CopyText(row, "Name", 0x40);
    d.materialFilename = sr3xtbl::CopyText(sr3xtbl::FindChild(row, "Material"), "Filename", 0x40);
    d.preload = sr3xtbl::ReadBoolAlways(row, "Preload");
    d.doubleSided = sr3xtbl::ReadBoolAlways(row, "Double_sided");
    d.lifeTime = sr3xtbl::ReadInt32Always(row, "Life_Time");
    d.fadeTime = sr3xtbl::ReadInt32Always(row, "Fade_Time");
    d.width = sr3xtbl::ReadFloatAlways(row, "Width");
    d.length = sr3xtbl::ReadFloatAlways(row, "Length");
    d.depth = sr3xtbl::ReadFloatAlways(row, "Depth");
    d.depthFadeStart = sr3xtbl::ReadFloatAlways(row, "Depth_Fade_Start");
    d.depthFadeEnd = sr3xtbl::ReadFloatAlways(row, "Depth_Fade_End");
    d.slopeFadeStart = sr3xtbl::ReadFloatAlways(row, "Slope_Fade_Start");
    d.slopeFadeEnd = sr3xtbl::ReadFloatAlways(row, "Slope_Fade_End");
    d.hasNormal = sr3xtbl::ReadBoolAlways(row, "has_normal");
    d.alphaTest = sr3xtbl::ReadFloatAlways(row, "alpha_test");
    return d;
}

std::vector<DecalInfo> ParseDecalInfoTable(const Document& doc) {
    std::vector<DecalInfo> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Decal_Info")) out.push_back(ParseDecalInfo(row));
    return out;
}

// ===========================================================================
// 11.3 groundfires.xtbl
// ===========================================================================
Groundfire ParseGroundfire(const Node* row) {
    Groundfire g;
    g.name = getText(row, "Name");
    g.damageRadius = sr3xtbl::ReadFloatAlways(row, "Damage_Radius");
    g.damageRegion = getText(row, "Damage_Region");
    const Node* effects = sr3xtbl::FindChild(row, "effects");
    for (const Node* e : sr3xtbl::Children(effects, "effect"))
        if (e->text()) g.effectNames.push_back(*e->text());
    g.durationMinSeconds = sr3xtbl::ReadFloatAlways(row, "Duration_Min");
    g.durationMaxSeconds = sr3xtbl::ReadFloatAlways(row, "Duration_Max");
    return g;
}

std::vector<Groundfire> ParseGroundfiresTable(const Document& doc) {
    std::vector<Groundfire> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Groundfire")) out.push_back(ParseGroundfire(row));
    return out;
}

// ===========================================================================
// 11.4 shells.xtbl
// ===========================================================================
Shell ParseShell(const Node* row) {
    Shell s;
    s.name = getText(row, "Name");
    s.staticMesh = getText(row, "Static_Mesh");
    s.collisionFoley = getText(row, "Collision_Foley");
    return s;
}

std::vector<Shell> ParseShellsTable(const Document& doc) {
    std::vector<Shell> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Shell")) out.push_back(ParseShell(row));
    return out;
}

// ===========================================================================
// 11.5 material_color_variants.xtbl
// ===========================================================================
ColorEntry ParseColorEntry(const Node* row) {
    ColorEntry c;
    c.entryId = sr3xtbl::GetInt32(row, "_Entry_ID");  // spec: sscanf("%d"); ParseInt32 is the closest
                                                       // available substitute (see ColorRGBAText banner)
    c.color = parseColorRGBAText(sr3xtbl::ChildText(row, "Color"));
    return c;
}

std::vector<ColorEntry> ParseMaterialColorVariantsTable(const Document& doc) {
    std::vector<ColorEntry> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "color_entry")) out.push_back(ParseColorEntry(row));
    return out;
}

// ===========================================================================
// 11.6 bitmap_materials.xtbl
// ===========================================================================
BitmapMaterial ParseBitmapMaterial(const Node* row) {
    BitmapMaterial b;
    b.name = getText(row, "Name");
    b.suffix = sr3xtbl::CopyText(sr3xtbl::FindChild(row, "MaterialProperties"), "Suffix", 3);
    const Node* bulletDecals = sr3xtbl::FindChild(row, "Bullet_Decals");
    for (const Node* d : sr3xtbl::Children(bulletDecals, "Bullet_Decal"))
        if (d->text()) b.bulletDecals.push_back(*d->text());
    b.blastDecal = getText(row, "Blast_Decal");
    b.crashDecal = getText(row, "Crash_Decal");
    b.audioOcclusion = sr3xtbl::GetInt32(row, "Audio_Occlusion");
    return b;
}

std::vector<BitmapMaterial> ParseBitmapMaterialsTable(const Document& doc) {
    std::vector<BitmapMaterial> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Bitmap_Material")) out.push_back(ParseBitmapMaterial(row));
    return out;
}

// ===========================================================================
// 11.7 bitmap_sheets.xtbl
// ===========================================================================
BitmapSheet ParseBitmapSheet(const Node* row) {
    BitmapSheet s;
    s.name = getText(row, "Name");
    return s;
}

std::vector<BitmapSheet> ParseBitmapSheetsTable(const Document& doc) {
    std::vector<BitmapSheet> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "BitmapSheets")) out.push_back(ParseBitmapSheet(row));
    return out;
}

// ===========================================================================
// 11.8 map_districts.xtbl
// ===========================================================================
MapDistrict ParseMapDistrict(const Node* row) {
    MapDistrict m;
    m.name = sr3xtbl::CopyText(row, "Name", 0x20);
    m.dataItemName = sr3xtbl::CopyText(row, "data_item_name", 0x20);
    m.teamName = sr3xtbl::CopyText(row, "team_name", 0x20);
    m.contactIcon = sr3xtbl::CopyText(row, "contact_icon", 0x20);
    m.contactName = sr3xtbl::CopyText(row, "contact_name", 0x28);
    const sr3xtbl::Vec3Result loc = sr3xtbl::ReadVec3Child(row, "text_location");
    m.textLocationX = loc.x;
    m.textLocationY = loc.y;
    m.textLocationZ = loc.z;
    return m;
}

std::vector<MapDistrict> ParseMapDistrictsTable(const Document& doc) {
    std::vector<MapDistrict> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Map_district")) out.push_back(ParseMapDistrict(row));
    return out;
}

// ===========================================================================
// 11.9 fade_categories.xtbl
// ===========================================================================
FadeCategory ParseFadeCategory(const Node* row) {
    FadeCategory f;
    f.name = getText(row, "Name");
    f.mediumLodDistance = sr3xtbl::ReadFloatAlways(row, "medium_lod_distance");
    f.lowLodDistance = sr3xtbl::ReadFloatAlways(row, "low_lod_distance");
    f.distance = sr3xtbl::ReadFloatAlways(row, "Distance");
    return f;
}

std::vector<FadeCategory> ParseFadeCategoriesTable(const Document& doc) {
    std::vector<FadeCategory> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Category")) out.push_back(ParseFadeCategory(row));
    return out;
}

// ===========================================================================
// 12.1 camera_shake.xtbl
// ===========================================================================
CameraShake ParseCameraShake(const Node* row) {
    CameraShake c;
    c.name = getText(row, "Name");

    const Node* wobble = sr3xtbl::FindChild(row, "Wobble");
    c.wobblePitch = sr3xtbl::ReadFloatAlways(wobble, "Pitch");
    c.wobbleRoll = sr3xtbl::ReadFloatAlways(wobble, "Roll");
    c.wobbleYaw = sr3xtbl::ReadFloatAlways(wobble, "Yaw");
    c.wobbleVariation = sr3xtbl::ReadFloatAlways(wobble, "Variation");

    const Node* destab = sr3xtbl::FindChild(row, "Destabilization");
    c.destabilizationPitch = sr3xtbl::ReadFloatAlways(destab, "Pitch");
    c.destabilizationRoll = sr3xtbl::ReadFloatAlways(destab, "Roll");
    c.destabilizationYaw = sr3xtbl::ReadFloatAlways(destab, "Yaw");
    c.destabilizationFrequency = sr3xtbl::ReadFloatAlways(destab, "Frequency");

    const Node* wander = sr3xtbl::FindChild(row, "Wander");
    c.wanderPitch = sr3xtbl::ReadFloatAlways(wander, "Pitch");
    c.wanderYaw = sr3xtbl::ReadFloatAlways(wander, "Yaw");
    c.wanderFrequency = sr3xtbl::ReadFloatAlways(wander, "Frequency");

    const Node* jitter = sr3xtbl::FindChild(row, "Jitter");
    c.jitterPitch = sr3xtbl::ReadFloatAlways(jitter, "Pitch");
    c.jitterRoll = sr3xtbl::ReadFloatAlways(jitter, "Roll");
    c.jitterYaw = sr3xtbl::ReadFloatAlways(jitter, "Yaw");

    const Node* osc1 = sr3xtbl::FindChild(row, "Oscillation1");
    c.oscillation1Pitch = sr3xtbl::ReadFloatAlways(osc1, "Pitch");
    c.oscillation1Roll = sr3xtbl::ReadFloatAlways(osc1, "Roll");
    c.oscillation1Yaw = sr3xtbl::ReadFloatAlways(osc1, "Yaw");
    c.oscillation1Frequency = sr3xtbl::ReadFloatAlways(osc1, "Frequency");

    const Node* osc2 = sr3xtbl::FindChild(row, "Oscillation2");
    c.oscillation2Pitch = sr3xtbl::ReadFloatAlways(osc2, "Pitch");
    c.oscillation2Roll = sr3xtbl::ReadFloatAlways(osc2, "Roll");
    c.oscillation2Yaw = sr3xtbl::ReadFloatAlways(osc2, "Yaw");
    c.oscillation2Frequency = sr3xtbl::ReadFloatAlways(osc2, "Frequency");

    const Node* direct = sr3xtbl::FindChild(row, "Direct");
    c.directPitch = sr3xtbl::ReadFloatAlways(direct, "Pitch");
    c.directRoll = sr3xtbl::ReadFloatAlways(direct, "Roll");
    c.directYaw = sr3xtbl::ReadFloatAlways(direct, "Yaw");

    const Node* wanderDirect = sr3xtbl::FindChild(row, "Wander_Direct");
    c.wanderDirectPitch = sr3xtbl::ReadFloatAlways(wanderDirect, "Pitch");
    c.wanderDirectYaw = sr3xtbl::ReadFloatAlways(wanderDirect, "Yaw");

    const Node* strengthGraph = sr3xtbl::FindChild(row, "Strength_Graph");
    for (const Node* el : sr3xtbl::Children(strengthGraph, "Strength_Element")) {
        CameraShakeStrengthElement se;
        se.time = sr3xtbl::ReadFloatAlways(el, "Time");
        se.wobble = sr3xtbl::ReadFloatAlways(el, "Wobble");
        se.destable = sr3xtbl::ReadFloatAlways(el, "Destable");
        se.wander = sr3xtbl::ReadFloatAlways(el, "Wander");
        se.jitter = sr3xtbl::ReadFloatAlways(el, "Jitter");
        se.oscillation1 = sr3xtbl::ReadFloatAlways(el, "Oscillation1");
        se.oscillation2 = sr3xtbl::ReadFloatAlways(el, "Oscillation2");
        se.direct = sr3xtbl::ReadFloatAlways(el, "Direct");
        se.wanderDirect = sr3xtbl::ReadFloatAlways(el, "Wander_Direct");
        se.blur = sr3xtbl::ReadFloatAlways(el, "Blur");
        se.strongVibration = sr3xtbl::ReadFloatAlways(el, "Strong_Vibration");
        se.weakVibration = sr3xtbl::ReadFloatAlways(el, "Weak_Vibration");
        c.strengthGraph.push_back(se);
    }
    return c;
}

std::vector<CameraShake> ParseCameraShakeTable(const Document& doc) {
    std::vector<CameraShake> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Camera_Shake")) out.push_back(ParseCameraShake(row));
    return out;
}

// ===========================================================================
// 3. weather_time_of_day.xtbl (see weather_time_of_day.h for the full field
// list and the "?" cloud-layer-slot omission rationale)
// ===========================================================================
WeatherTimeOfDayCell ParseWeatherTimeOfDayCell(const Node* stageRow) {
    WeatherTimeOfDayCell cell;
    cell.stageName = getText(stageRow, "Stage_Name");

    const Node* districtLighting = sr3xtbl::FindChild(stageRow, "District_Lighting");
    const Node* lightingParams = sr3xtbl::FindChild(districtLighting, "Lighting_Parameters");
    cell.lighting.todLightColor = ReadColorIntensity(lightingParams, "TOD_Light_Color");
    cell.lighting.ambientColor = ReadColorIntensity(lightingParams, "Ambient_Color");
    cell.lighting.backAmbientColor = ReadColorIntensity(lightingParams, "Back_Ambient_Color");
    cell.lighting.fogColor = ReadColorIntensity(lightingParams, "Fog_Color");
    cell.lighting.fogGround = sr3xtbl::GetFloat(lightingParams, "Fog_Ground");
    cell.lighting.fogAtmosphereScale = sr3xtbl::GetFloat(lightingParams, "Fog_Atmosphere_Scale");
    cell.lighting.fogDensity = sr3xtbl::GetFloat(lightingParams, "Fog_Density");
    cell.lighting.fogDensityOffset = sr3xtbl::GetFloat(lightingParams, "Fog_Density_Offset");
    cell.lighting.groundReflectionGloss = sr3xtbl::GetFloat(lightingParams, "Ground_Reflection_Gloss");
    cell.lighting.groundReflectionBrightness = sr3xtbl::GetFloat(lightingParams, "Ground_Reflection_Brightness");
    cell.lighting.groundReflectionSpecBrightness =
        sr3xtbl::GetFloat(lightingParams, "Ground_Reflection_Spec_Brightness");
    cell.lighting.starStrength = sr3xtbl::GetFloat(lightingParams, "Star_Strength");
    cell.lighting.meteorStrength = sr3xtbl::GetFloat(lightingParams, "Meteor_Strength");

    const Node* skyboxParams = sr3xtbl::FindChild(districtLighting, "Skybox_Parameters");
    auto readBand = [&](const Node* bandParent) {
        WeatherTimeOfDayCell::SkyboxBand b;
        b.cloudFrontColor = ReadColorIntensity(bandParent, "Cloud_Front_Color");
        b.cloudBackColor = ReadColorIntensity(bandParent, "Cloud_Back_Color");
        b.cloudLayerStrength = sr3xtbl::GetFloat(bandParent, "Cloud_Layer_Strength");
        b.cloudNormalMapHeight = sr3xtbl::GetFloat(bandParent, "Cloud_Normal_Map_Height");
        b.cloudHighlight = sr3xtbl::GetFloat(bandParent, "Cloud_Highlight");
        b.stormStrength = sr3xtbl::GetFloat(bandParent, "Storm_Strength");
        b.cloudSpeed = sr3xtbl::GetFloat(bandParent, "Cloud_Speed");
        return b;
    };
    cell.skyboxHorizon = readBand(sr3xtbl::FindChild(skyboxParams, "Horizon"));
    cell.skyboxOverhead = readBand(sr3xtbl::FindChild(skyboxParams, "Overhead"));

    cell.skyboxDirect.backlightStrength = sr3xtbl::GetFloat(skyboxParams, "Backlight_Strength");
    cell.skyboxDirect.backlightPower = sr3xtbl::GetFloat(skyboxParams, "Backlight_Power");
    cell.skyboxDirect.mountainFrontColor = ReadColorIntensity(skyboxParams, "Mountain_Front_Color");
    cell.skyboxDirect.mountainBackColor = ReadColorIntensity(skyboxParams, "Mountain_Back_Color");
    cell.skyboxDirect.mountainFogColor = ReadColorIntensity(skyboxParams, "Mountain_Fog_Color");
    cell.skyboxDirect.mountainFogDensity = sr3xtbl::GetFloat(skyboxParams, "Mountain_Fog_Density");
    cell.skyboxDirect.mountainNormalMapHeight = sr3xtbl::GetFloat(skyboxParams, "Mountain_Normal_Map_Height");

    const Node* water = sr3xtbl::FindChild(districtLighting, "Water");
    cell.water.ambientColor = ReadColorIntensity(water, "Ambient_Color");
    cell.water.diffuseColor1 = ReadColorIntensity(water, "Diffuse_Color1");
    cell.water.diffuseColor2 = ReadColorIntensity(water, "Diffuse_Color2");
    cell.water.specularColor = ReadColorIntensity(water, "Specular_Color");
    cell.water.specularAlpha = sr3xtbl::GetFloat(water, "Specular_Alpha");
    cell.water.specularPower = sr3xtbl::GetFloat(water, "Specular_Power");
    cell.water.falloffColor = ReadColorIntensity(water, "Falloff_Color");
    cell.water.fogColor = ReadColorIntensity(water, "Fog_Color");
    cell.water.crestColor = ReadColorIntensity(water, "Crest_Color");
    cell.water.crestThreshold = sr3xtbl::GetFloat(water, "Crest_Threshold");

    const Node* colorCorrection = sr3xtbl::FindChild(districtLighting, "Color_Correction");
    cell.colorCorrection.windowTint = ReadColorIntensity(colorCorrection, "Window_Tint");
    cell.colorCorrection.lutFilename =
        sr3xtbl::CopyText(sr3xtbl::FindChild(colorCorrection, "LUT_Filename"), "Filename", 0x40);

    const Node* exposure = sr3xtbl::FindChild(districtLighting, "Exposure");
    cell.exposure.desiredBrightness = sr3xtbl::GetFloat(exposure, "Desired_Brightness");
    cell.exposure.exposureMin = sr3xtbl::GetFloat(exposure, "Exposure_Min");
    cell.exposure.exposureMax = sr3xtbl::GetFloat(exposure, "Exposure_Max");

    const Node* todAudio = sr3xtbl::FindChild(districtLighting, "TOD_Audio");
    cell.todAudioAmbientRtpc = sr3xtbl::GetFloat(todAudio, "Ambient_RTPC");

    const Node* districtSkybox = sr3xtbl::FindChild(stageRow, "District_Skybox");
    cell.west1 = ReadColorIntensity(districtSkybox, "West1");
    cell.west2 = ReadColorIntensity(districtSkybox, "West2");
    cell.west3 = ReadColorIntensity(districtSkybox, "West3");
    cell.west4 = ReadColorIntensity(districtSkybox, "West4");
    cell.east1 = ReadColorIntensity(districtSkybox, "East1");
    cell.east2 = ReadColorIntensity(districtSkybox, "East2");
    cell.east3 = ReadColorIntensity(districtSkybox, "East3");
    cell.east4 = ReadColorIntensity(districtSkybox, "East4");
    cell.westZenith = ReadColorIntensity(districtSkybox, "West_Zenith");

    cell.particleAmbientColor = readVec4TextChild(stageRow, "particle_ambient_color");
    cell.particleTodLightColor = readVec4TextChild(stageRow, "particle_tod_light_color");
    cell.ldrMin = sr3xtbl::GetFloat(stageRow, "ldr_min");
    cell.ldrMax = sr3xtbl::GetFloat(stageRow, "ldr_max");
    cell.bloomExposure = sr3xtbl::GetFloat(stageRow, "bloom_exposure");
    cell.irisRate = sr3xtbl::GetFloat(stageRow, "iris_rate");
    cell.luminanceMax = sr3xtbl::GetFloat(stageRow, "luminance_max");
    cell.luminanceMin = sr3xtbl::GetFloat(stageRow, "luminance_min");
    cell.luminanceMaskMax = sr3xtbl::GetFloat(stageRow, "luminance_mask_max");
    cell.eyeAdaptionBase = sr3xtbl::GetFloat(stageRow, "eye_adaption_base");
    cell.eyeAdaptionAmount = sr3xtbl::GetFloat(stageRow, "eye_adaption_amount");
    cell.eyeFadeMin = sr3xtbl::GetFloat(stageRow, "eye_fade_min");
    cell.eyeFadeMax = sr3xtbl::GetFloat(stageRow, "eye_fade_max");
    cell.brightpassThresholdNew = sr3xtbl::GetFloat(stageRow, "brightpass_threshold_new");
    cell.brightpassOffsetNew = sr3xtbl::GetFloat(stageRow, "brightpass_offset_new");
    cell.bloomAmount = sr3xtbl::GetFloat(stageRow, "bloom_amount");
    cell.bloomTheta = sr3xtbl::GetFloat(stageRow, "bloom_theta");
    cell.bloomSlopeA = sr3xtbl::GetFloat(stageRow, "bloom_slope_A");
    cell.bloomSlopeB = sr3xtbl::GetFloat(stageRow, "bloom_slope_B");
    cell.tonemapLumRange = sr3xtbl::GetFloat(stageRow, "tonemap_lum_range");
    cell.tonemapLumOffset = sr3xtbl::GetFloat(stageRow, "tonemap_lum_offset");

    const Node* orbitalObjects = sr3xtbl::FindChild(stageRow, "Orbital_Objects");
    for (const Node* obj : sr3xtbl::Children(orbitalObjects, "Object")) {
        OrbitalObjectOverride o;
        o.objectName = getText(obj, "Object_Name");
        const Node* tint = sr3xtbl::FindChild(obj, "Tint");
        o.tintPresent = tint != nullptr;
        o.tint = ReadColorRGBAlways(tint);
        o.scale = sr3xtbl::GetFloat(obj, "Scale");
        o.opacity = sr3xtbl::GetFloat(obj, "Opacity");
        cell.orbitalObjects.push_back(o);
    }

    return cell;
}

WeatherTimeSegment ParseWeatherTimeSegment(const Node* row) {
    WeatherTimeSegment seg;
    seg.name = getText(row, "Name");
    // [OPEN - spec-tables-environment.md 1.4 and the Start_Time row: "which accessor (signed/unsigned) reads `t`";
    // ReadUInt32Always kept.]
    seg.startTimeHHMM = sr3xtbl::ReadUInt32Always(row, "Start_Time");
    seg.rampOutTimeMinutes = sr3xtbl::ReadUInt32Always(row, "Ramp_Out_Time");
    const Node* weatherStages = sr3xtbl::FindChild(row, "Weather_Stages");
    for (const Node* stage : sr3xtbl::Children(weatherStages, "Stage")) seg.stages.push_back(ParseWeatherTimeOfDayCell(stage));
    return seg;
}

std::vector<WeatherTimeSegment> ParseWeatherTimeOfDayTable(const Document& doc) {
    std::vector<WeatherTimeSegment> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Weather_Time_Segment")) out.push_back(ParseWeatherTimeSegment(row));
    return out;
}

// ===========================================================================
// 12.2 camera_free.xtbl (see camera_free.h for the modelling-choice caveat
// on the panning_group accessor identities)
// ===========================================================================
std::optional<CameraFree> ParseCameraFree(const Document& doc) {
    const Node* camera = sr3xtbl::FindChild(doc.table(), "Camera");
    if (!camera) return std::nullopt;
    CameraFree cf;

    const Node* panning = sr3xtbl::FindChild(camera, "panning_group");
    CameraFreePanningGroup& p = cf.panning;
    p.fastPanHorizontalMultiplier = sr3xtbl::ReadFloatAlways(panning, "fast_pan_horizontal_multiplier");
    p.fastPanVerticalMultiplier = sr3xtbl::ReadFloatAlways(panning, "fast_pan_vertical_multiplier");
    p.slowPanHorizontalMultiplier = sr3xtbl::ReadFloatAlways(panning, "slow_pan_horizontal_multiplier");
    p.slowPanVerticalMultiplier = sr3xtbl::ReadFloatAlways(panning, "slow_pan_vertical_multiplier");
    p.pegAccelPanHorizontalMin = sr3xtbl::ReadFloatAlways(panning, "peg_accel_pan_horizontal_min");
    p.pegAccelPanHorizontalMax = sr3xtbl::ReadFloatAlways(panning, "peg_accel_pan_horizontal_max");
    p.pegAccelPanVerticalMin = sr3xtbl::ReadFloatAlways(panning, "peg_accel_pan_vertical_min");
    p.pegAccelPanVerticalMax = sr3xtbl::ReadFloatAlways(panning, "peg_accel_pan_vertical_max");
    p.zoomScaleMin = sr3xtbl::ReadFloatAlways(panning, "zoom_scale_min");
    p.zoomScaleMax = sr3xtbl::ReadFloatAlways(panning, "zoom_scale_max");
    p.fastPanInputThreshold = sr3xtbl::ReadFloatAlways(panning, "fast_pan_input_threshold");
    p.accelScale = sr3xtbl::ReadFloatAlways(panning, "accel_scale");
    p.decelScale = sr3xtbl::ReadFloatAlways(panning, "decel_scale");
    p.fineAimAccelScale = sr3xtbl::ReadFloatAlways(panning, "fine_aim_accel_scale");
    p.fineAimDecelScale = sr3xtbl::ReadFloatAlways(panning, "fine_aim_decel_scale");
    p.hpanThreshold = sr3xtbl::ReadFloatAlways(panning, "hpan_threshold");
    p.vpanThreshold = sr3xtbl::ReadFloatAlways(panning, "vpan_threshold");

    // Dampening element-name reconstruction (spec 12.2): CONFIRMED literal
    // examples are "tank_horiz_dampening_genki_mouse" / "tank_vert_dampening_
    // genki[_mouse]" and "interior_v_dampening_mouse" / "interior_h_dampening
    // _mouse". The general pattern used below (<prefix>_horiz_dampening[_genki]
    // [_mouse] / <prefix>_vert_dampening[_genki][_mouse] for the {fine_aim,
    // heavy_weapon, human_shield, tank[, tank_..._genki]} set) is INFERRED by
    // analogy from those two confirmed examples, not independently confirmed
    // per prefix - flagged in the report for Team A to verify against the
    // executable directly.
    auto dampening = [&](const std::string& prefix, const std::string& suffix = "") {
        CameraFreePanningGroup::Dampening d;
        d.horiz = sr3xtbl::ReadFloatAlways(panning, prefix + "_horiz_dampening" + suffix);
        d.vert = sr3xtbl::ReadFloatAlways(panning, prefix + "_vert_dampening" + suffix);
        return d;
    };
    p.fineAimDampening = dampening("fine_aim");
    p.heavyWeaponDampening = dampening("heavy_weapon");
    p.humanShieldDampening = dampening("human_shield");
    p.tankDampening = dampening("tank");
    p.tankGenkiDampening = dampening("tank", "_genki");
    p.fineAimDampeningMouse = dampening("fine_aim", "_mouse");
    p.heavyWeaponDampeningMouse = dampening("heavy_weapon", "_mouse");
    p.humanShieldDampeningMouse = dampening("human_shield", "_mouse");
    p.tankDampeningMouse = dampening("tank", "_mouse");
    p.tankGenkiDampeningMouse = dampening("tank", "_genki_mouse");  // CONFIRMED (spec 12.2)

    p.interiorVDampening = sr3xtbl::ReadFloatAlways(panning, "interior_v_dampening");
    p.interiorHDampening = sr3xtbl::ReadFloatAlways(panning, "interior_h_dampening");
    p.skydiveHDampening = sr3xtbl::ReadFloatAlways(panning, "skydive_h_dampening");
    p.skydiveVDampening = sr3xtbl::ReadFloatAlways(panning, "skydive_v_dampening");
    p.freefallHDampening = sr3xtbl::ReadFloatAlways(panning, "freefall_h_dampening");
    p.parachuteHDampening = sr3xtbl::ReadFloatAlways(panning, "parachute_h_dampening");
    p.helicopterHDampening = sr3xtbl::ReadFloatAlways(panning, "helicopter_h_dampening");
    p.interiorVDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "interior_v_dampening_mouse");  // CONFIRMED
    p.interiorHDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "interior_h_dampening_mouse");  // CONFIRMED
    p.skydiveHDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "skydive_h_dampening_mouse");
    p.skydiveVDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "skydive_v_dampening_mouse");
    p.freefallHDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "freefall_h_dampening_mouse");
    p.parachuteHDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "parachute_h_dampening_mouse");
    p.helicopterHDampeningMouse = sr3xtbl::ReadFloatAlways(panning, "helicopter_h_dampening_mouse");

    const Node* asvct = sr3xtbl::FindChild(camera, "asvct_group");
    cf.asvct.defaultTime = sr3xtbl::ReadFloatAlways(asvct, "default_time");
    cf.asvct.stationaryTime = sr3xtbl::ReadFloatAlways(asvct, "stationary_time");
    cf.asvct.stationaryThreshold = sr3xtbl::ReadFloatAlways(asvct, "stationary_threshold");

    const Node* followAgg = sr3xtbl::FindChild(camera, "follow_aggression_group");
    cf.followAggression.defaultSwingRate = sr3xtbl::ReadFloatAlways(followAgg, "default_swing_rate");

    const Node* hillTracking = sr3xtbl::FindChild(camera, "hill_tracking_group");
    cf.hillTracking.defaultAggression = sr3xtbl::ReadFloatAlways(hillTracking, "default_aggression");
    cf.hillTracking.genkiAggression = sr3xtbl::ReadFloatAlways(hillTracking, "genki_aggression");

    const Node* misc = sr3xtbl::FindChild(camera, "miscellany_group");
    cf.miscellany.pitchResetTime = sr3xtbl::ReadFloatAlways(misc, "pitch_reset_time");
    cf.miscellany.backawaySpeed = sr3xtbl::ReadFloatAlways(misc, "backaway_speed");
    cf.miscellany.manualAimElevationAngle = sr3xtbl::ReadFloatAlways(misc, "Manual_Aim_Elevation_Angle");
    cf.miscellany.manualVehicleAimElevationAngle =
        sr3xtbl::ReadFloatAlways(misc, "Manual_Vehicle_Aim_Elevation_Angle");
    cf.miscellany.helicopterLandingPitch = sr3xtbl::ReadFloatAlways(misc, "Helicopter_Landing_Pitch");
    cf.miscellany.helicopterLandingMaxSpeed = sr3xtbl::ReadFloatAlways(misc, "Helicopter_Landing_Max_Speed");
    cf.miscellany.helicopterLandingMaxAltitude = sr3xtbl::ReadFloatAlways(misc, "Helicopter_Landing_Max_Altitude");
    cf.miscellany.exteriorInteriorBlendTime = sr3xtbl::ReadFloatAlways(misc, "Exterior_Interior_Blend_Time");
    cf.miscellany.ragdollBlendTime = sr3xtbl::ReadFloatAlways(misc, "Ragdoll_Blend_Time");

    const Node* submodes = sr3xtbl::FindChild(camera, "submodes");
    for (const Node* sm : sr3xtbl::Children(submodes, "submode")) {
        CameraFreeSubmode s;
        s.name = getText(sm, "name");
        s.aspect = getText(sm, "Aspect");
        s.lookatOffset = sr3xtbl::ReadVec3Child(sm, "lookat_offset");
        s.zDist = sr3xtbl::GetFloat(sm, "z_dist");
        s.yDist = sr3xtbl::GetFloat(sm, "y_dist");
        s.minElevation = sr3xtbl::ReadFloatAlways(sm, "min_elevation");
        s.maxElevation = sr3xtbl::ReadFloatAlways(sm, "max_elevation");
        s.defaultElevation = sr3xtbl::GetFloat(sm, "default_elevation");
        s.baseFov = sr3xtbl::ReadFloatAlways(sm, "base_fov");
        s.blendTime = sr3xtbl::ReadFloatAlways(sm, "blend_time");
        s.xShift = sr3xtbl::ReadFloatAlways(sm, "x_shift");
        s.overrideExitBlendTime = sr3xtbl::GetBool(sm, "override_exit_blend_time");
        cf.submodes.push_back(s);
    }

    const Node* vehicleFallbacks = sr3xtbl::FindChild(camera, "vehicle_fallbacks");
    for (const Node* vf : sr3xtbl::Children(vehicleFallbacks, "vehicle_fallback")) {
        CameraFreeVehicleFallback f;
        f.name = getText(vf, "name");
        f.distance = sr3xtbl::ReadFloatAlways(vf, "distance");
        f.rampin = sr3xtbl::ReadInt32Always(vf, "rampin");
        f.duration = sr3xtbl::ReadInt32Always(vf, "duration");
        f.returnValue = sr3xtbl::ReadInt32Always(vf, "return");
        cf.vehicleFallbacks.push_back(f);
    }

    const Node* vehicleAims = sr3xtbl::FindChild(camera, "Vehicle_Aims");
    for (const Node* va : sr3xtbl::Children(vehicleAims, "Vehicle_Aim")) {
        CameraFreeVehicleAim a;
        a.name = getText(va, "name");
        a.aspect = getText(va, "Aspect");
        a.lookatOffset = sr3xtbl::ReadVec3Child(va, "Lookat_Offset");
        a.minPitch = sr3xtbl::ReadFloatAlways(va, "Min_Pitch");
        a.maxPitch = sr3xtbl::ReadFloatAlways(va, "Max_pitch");
        a.xShift = sr3xtbl::ReadFloatAlways(va, "X_Shift");
        a.headingRange = sr3xtbl::ReadFloatAlways(va, "Heading_Range");
        a.headingCenter = sr3xtbl::ReadFloatAlways(va, "Heading_Center");
        a.baseFov = sr3xtbl::ReadFloatAlways(va, "base_fov");
        a.zDist = sr3xtbl::ReadFloatAlways(va, "z_dist");
        a.yDist = sr3xtbl::ReadFloatAlways(va, "y_dist");
        const Node* flags = sr3xtbl::FindChild(va, "flags");
        for (const Node* fl : sr3xtbl::Children(flags, "Flag"))
            if (fl->text()) a.flagNames.push_back(*fl->text());
        cf.vehicleAims.push_back(a);
    }

    return cf;
}

}  // namespace sr3tables_environment
