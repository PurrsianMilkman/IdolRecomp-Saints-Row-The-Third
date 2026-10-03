// Parse functions for sr3tables_customization/player_creation.h (spec-
// tables-customization.md §14-§17). Built ONLY on sr3xtbl's accessors;
// nothing here touches the game executable, disassembly or decompiled code.

#include "sr3tables_customization/player_creation.h"

namespace sr3tables_customization {

using namespace sr3xtbl;

namespace {

std::optional<std::string> OptText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

int MatchName(const std::optional<std::string>& text, const std::string_view* names, size_t count) {
    if (!text) return -1;
    for (size_t i = 0; i < count; ++i)
        if (NameEquals(*text, names[i])) return static_cast<int>(i);
    return -1;
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

// "r g b [a]" whitespace-separated colour text - same judgement call as
// color_pools.h's ColorText (see that file's banner).
std::optional<ColorText> ParseColorTextField(const Node* node, std::string_view name) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    const std::vector<std::string_view> toks = splitWs(*t);
    if (toks.empty()) return std::nullopt;
    ColorText c;
    if (toks.size() >= 1) c.r = ParseFloat(toks[0]);
    if (toks.size() >= 2) c.g = ParseFloat(toks[1]);
    if (toks.size() >= 3) c.b = ParseFloat(toks[2]);
    if (toks.size() >= 4) c.a = ParseFloat(toks[3]);
    return c;
}

}  // namespace

// ===========================================================================
// 14.1 player_creation.xtbl
// ===========================================================================
MorphSet ParseMorphSet(const Node* row) {
    MorphSet m;
    m.name = OptText(row, "Name");
    m.categoryIndex = MatchName(m.name, kMorphSetCategoryNames.data(), kMorphSetCategoryNames.size());
    m.displayName = OptText(row, "DisplayName");
    const Node* wrap = FindChild(row, "Morph_Infos");
    for (const Node* e = FindChild(wrap, "Morph_Info"); e; e = NextSibling(wrap, e, "Morph_Info"))
        m.morphInfoRows.push_back(e);
    return m;
}

std::vector<MorphSet> ParsePlayerCreationTable(const Document& doc) {
    std::vector<MorphSet> out;
    for (const Node* row : Children(doc.table(), "Morph_Set")) out.push_back(ParseMorphSet(row));
    return out;
}

// ===========================================================================
// 14.2 player_creation_morph_groups.xtbl
// ===========================================================================
MorphGroup ParseMorphGroup(const Node* row) {
    MorphGroup g;
    g.name = OptText(row, "Name");
    const Node* wrap = FindChild(row, "morph_list");
    for (const Node* e = FindChild(wrap, "morph_item"); e; e = NextSibling(wrap, e, "morph_item")) {
        MorphGroupItem item;
        item.morphName = OptText(e, "morph_name");
        const std::optional<std::string> dt = OptText(e, "display_type");
        item.isGenderType = dt.has_value() && NameEquals(*dt, "gender");
        g.morphList.push_back(item);
    }
    return g;
}

std::vector<MorphGroup> ParsePlayerCreationMorphGroupsTable(const Document& doc) {
    std::vector<MorphGroup> out;
    for (const Node* row : Children(doc.table(), "morph_group")) out.push_back(ParseMorphGroup(row));
    return out;
}

// ===========================================================================
// 15.1 player_creation_normal_maps.xtbl
// ===========================================================================
NormalMapSettings ParseNormalMapSettings(const Node* row) {
    NormalMapSettings n;
    n.name = OptText(row, "Name");
    n.bodyTypeIndex = MatchName(n.name, kBodyTypeNames.data(), kBodyTypeNames.size());
    const Node* slidersWrap = FindChild(row, "Morph_Sliders");
    for (const Node* s = FindChild(slidersWrap, "Morph_Slider"); s; s = NextSibling(slidersWrap, s, "Morph_Slider")) {
        // CONFIRMED 2026-10-02 (re-derived from the executable, `FUN_009fbf10` read in full): both caps are
        // pre-checked while-loop conditions (tested BEFORE processing each entry) - the slider loop runs
        // `while (nextElement != 0 && count < 4)`, keeping a true maximum of exactly 4, matching the `>= 4`
        // already coded here. Only the normals cap (materials.cpp, §9.2) can change a real result.
        if (n.morphSliders.size() >= 4) break;  // cap 4 sliders/type (§15.1)
        MorphSlider slider;
        slider.sliderName = OptText(s, "Slider_Name");
        const Node* keysWrap = FindChild(s, "Slider_Keys");
        for (const Node* k = FindChild(keysWrap, "Slider_Key"); k; k = NextSibling(keysWrap, k, "Slider_Key")) {
            // CONFIRMED 2026-10-02 (same dump as above): the key loop runs `while (nextElement != 0 && count < 8)`,
            // keeping a true maximum of exactly 8, matching the `>= 8` already coded here.
            if (slider.sliderKeys.size() >= 8) break;  // cap 8 keys/slider (§15.1)
            SliderKey key;
            key.sliderPosition = ReadFloatAlways(k, "Slider_Position");
            key.normalMapStrength = ReadFloatAlways(k, "Normal_Map_Strength");
            slider.sliderKeys.push_back(key);
        }
        n.morphSliders.push_back(slider);
    }
    return n;
}

std::vector<NormalMapSettings> ParsePlayerCreationNormalMapsTable(const Document& doc) {
    std::vector<NormalMapSettings> out;
    for (const Node* row : Children(doc.table(), "Normal_Map_Settings")) out.push_back(ParseNormalMapSettings(row));
    return out;
}

// ===========================================================================
// 15.2 player_creation_hair_color.xtbl
// ===========================================================================
HairColor ParseHairColor(const Node* row) {
    HairColor h;
    h.name = OptText(row, "Name");
    h.displayName = OptText(row, "Display_Name");
    h.shaderball = OptText(row, "Shaderball");
    h.swatchImage = OptText(row, "swatch_image");
    h.swatchLabel = OptText(row, "swatch_label");
    h.hue = GetFloat(row, "Hue");
    h.saturationLight = ReadFloatAlways(row, "Saturation_light");
    h.saturationDark = ReadFloatAlways(row, "Saturation_dark");
    h.specularAlpha = ReadFloatAlways(row, "Specular_alpha");
    h.brightness = ReadFloatAlways(row, "Brightness");
    h.brightnessBias = ReadFloatAlways(row, "Brightness_bias");
    h.specularBrightRaw = OptText(row, "Specular_bright");
    h.specularDarkRaw = OptText(row, "Specular_dark");
    return h;
}

std::vector<HairColor> ParsePlayerCreationHairColorTable(const Document& doc) {
    std::vector<HairColor> out;
    for (const Node* row : Children(doc.table(), "Hair_Color")) out.push_back(ParseHairColor(row));
    return out;
}

// ===========================================================================
// 15.3 player_creation_skin_colors.xtbl
// ===========================================================================
SkinColorEntry ParseSkinColorEntry(const Node* row) {
    SkinColorEntry e;
    e.name = OptText(row, "Name");
    e.displayName = OptText(row, "display_name");
    e.swatch = OptText(row, "swatch");
    e.shaderball = OptText(row, "Shaderball");
    e.unmaskedHue = ReadFloatAlways(row, "Unmasked_Hue");
    e.unmaskedSaturation = ReadFloatAlways(row, "Unmasked_Saturation");
    e.unmaskedBrightness = ReadFloatAlways(row, "Unmasked_Brightness");
    e.hue = ReadFloatAlways(row, "Hue");
    e.saturation = ReadFloatAlways(row, "Saturation");
    e.brightness = ReadFloatAlways(row, "Brightness");
    e.specularAlpha = ReadFloatAlways(row, "Specular_Alpha");
    e.fresnelAlpha = ReadFloatAlways(row, "Fresnel_Alpha");
    e.specularPower = ReadFloatAlways(row, "Specular_Power");
    return e;
}

std::vector<SkinColorEntry> ParsePlayerCreationSkinColorsTable(const Document& doc) {
    std::vector<SkinColorEntry> out;
    for (const Node* row : Children(doc.table(), "Entry")) out.push_back(ParseSkinColorEntry(row));
    return out;
}

// ===========================================================================
// 16.1 player_master_sliders.xtbl
// ===========================================================================
MasterSliderCategory ParseMasterSliderCategory(const Node* row) {
    MasterSliderCategory c;
    c.name = OptText(row, "Name");
    c.displayName = OptText(row, "DisplayName");
    for (const Node* s = FindChild(row, "Slider"); s; s = NextSibling(row, s, "Slider")) {
        MasterSlider slider;
        slider.name = OptText(s, "Name");
        slider.displayName = OptText(s, "DisplayName");
        slider.initialValue = GetFloat(s, "InitialValue");
        slider.morphListRaw = OptText(s, "MorphList");
        c.sliders.push_back(slider);
    }
    return c;
}

std::vector<MasterSliderCategory> ParsePlayerMasterSlidersTable(const Document& doc) {
    std::vector<MasterSliderCategory> out;
    for (const Node* row : Children(doc.table(), "MasterSliders")) out.push_back(ParseMasterSliderCategory(row));
    return out;
}

// ===========================================================================
// 16.2 player_presets.xtbl
// ===========================================================================
namespace {

PresetComposite ParsePresetComposite(const Node* row) {
    PresetComposite c;
    c.layer = OptText(row, "Layer");
    c.color = ParseColorTextField(row, "Color");
    return c;
}

}  // namespace

Preset ParsePreset(const Node* row) {
    Preset p;
    p.name = OptText(row, "Name");
    p.race = OptText(row, "Race");
    p.gender = OptText(row, "Gender");
    p.isDefault = ReadBoolAlways(row, "Default");
    p.displayName = OptText(row, "DisplayName");
    p.hair = OptText(row, "Hair");
    p.hairLength = ReadFloatAlways(row, "Hair_Length");
    p.hairColorPrimary = OptText(row, "Hair_Color_Primary");
    p.hairColorSecondary = OptText(row, "Hair_Color_Secondary");
    p.skinColor = OptText(row, "Skin_Color");

    const Node* compositesWrap = FindChild(row, "Composites");
    for (const Node* c = FindChild(compositesWrap, "Composite"); c; c = NextSibling(compositesWrap, c, "Composite")) {
        // Hard cap: 5 inline Composite entries per preset - previously undocumented, CONFIRMED 2026-10-02 from
        // the executable (the fixed 100-byte Preset record has room for no more: up to 5 inline Layer pointers
        // at +0x34-+0x47 plus 5 inline {R,G,B,0xff} colour cells at +0x48-+0x5B). Real data never approaches
        // this (§16.2 validation: 8 rows total), so this cannot change a real result, but it is now a CONFIRMED
        // fact, not an OPEN compare-operator guess like this file's other caps.
        if (p.composites.size() >= 5) break;
        p.composites.push_back(ParsePresetComposite(c));
    }

    const Node* presetGridWrap = FindChild(row, "Preset_Grid");
    for (const Node* e = FindChild(presetGridWrap, "Preset_Element"); e; e = NextSibling(presetGridWrap, e, "Preset_Element")) {
        PresetElement pe;
        pe.morphName = OptText(e, "Morph_Name");
        pe.value = ReadFloatAlways(e, "Value");
        p.presetGrid.push_back(pe);
    }

    const Node* regionalWrap = FindChild(row, "RegionalPresets");
    for (const Node* e = FindChild(regionalWrap, "RegionalPreset"); e; e = NextSibling(regionalWrap, e, "RegionalPreset")) {
        PresetRegionalPresetRef r;
        r.category = OptText(e, "Category");
        r.preset = OptText(e, "Preset");
        p.regionalPresets.push_back(r);
    }

    return p;
}

std::vector<Preset> ParsePlayerPresetsTable(const Document& doc) {
    std::vector<Preset> out;
    for (const Node* row : Children(doc.table(), "Preset")) out.push_back(ParsePreset(row));
    return out;
}

// ===========================================================================
// 16.3 player_regional_presets.xtbl
// ===========================================================================
namespace {

RegionalPresetEntry ParseRegionalPresetEntry(const Node* row) {
    RegionalPresetEntry e;
    e.name = OptText(row, "Name");
    e.displayName = OptText(row, "DisplayName");
    e.figure = OptText(row, "figure");
    const Node* wrap = FindChild(row, "MorphTargets");
    for (const Node* t = FindChild(wrap, "MorphTarget"); t; t = NextSibling(wrap, t, "MorphTarget")) {
        RegionalPresetMorphTarget mt;
        mt.morph = OptText(t, "Morph");
        mt.target = ReadFloatAlways(t, "Target");
        e.morphTargets.push_back(mt);
    }
    return e;
}

}  // namespace

RegionalPresetsGroup ParseRegionalPresetsGroup(const Node* row) {
    RegionalPresetsGroup g;
    g.name = OptText(row, "Name");
    g.displayName = OptText(row, "DisplayName");
    const Node* wrap = FindChild(row, "Presets");
    for (const Node* e = FindChild(wrap, "Preset"); e; e = NextSibling(wrap, e, "Preset"))
        g.presets.push_back(ParseRegionalPresetEntry(e));
    return g;
}

std::vector<RegionalPresetsGroup> ParsePlayerRegionalPresetsTable(const Document& doc) {
    std::vector<RegionalPresetsGroup> out;
    for (const Node* row : Children(doc.table(), "RegionalPresets")) out.push_back(ParseRegionalPresetsGroup(row));
    return out;
}

// ===========================================================================
// 17. player_cust_shot_map.xtbl
// ===========================================================================
NewEntity ParseNewEntity(const Node* row) {
    NewEntity e;
    e.name = OptText(row, "Name");
    e.animPos = OptText(row, "Anim_Pos");
    return e;
}

std::vector<NewEntity> ParsePlayerCustShotMapTable(const Document& doc) {
    std::vector<NewEntity> out;
    for (const Node* row : Children(doc.table(), "NewEntity")) out.push_back(ParseNewEntity(row));
    return out;
}

}  // namespace sr3tables_customization
