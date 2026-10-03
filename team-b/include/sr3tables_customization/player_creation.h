#pragma once

// sr3tables_customization/player_creation.h - the player face/body creation
// subsystem (spec-tables-customization.md §14-§17): player_creation.xtbl
// (§14.1), player_creation_morph_groups.xtbl (§14.2),
// player_creation_normal_maps.xtbl (§15.1), player_creation_hair_color.xtbl
// (§15.2), player_creation_skin_colors.xtbl (§15.3),
// player_master_sliders.xtbl (§16.1), player_presets.xtbl (§16.2),
// player_regional_presets.xtbl (§16.3) and player_cust_shot_map.xtbl (§17).
// Included from sr3tables_customization/tables.h.
//
// This subsystem is named throughout spec-tables-customization.md's own
// overview (§1.1, §1.3's "player face/body creation" chain row) as part of
// the SAME document this whole library implements - it is not a separate
// spec, so it is implemented here alongside every other section.
//
// A recurring gap across this whole subsystem: the "shared player-creation
// morph namespace" (§14.1's Morph_Info entries, resolved via FUN_009f5c30
// wherever a MorphList/Morph_Name/Morph/Preset_Element references it) has
// its OWN per-entry field reader (FUN_009f6570) undecompiled this pass -
// nothing about what a single Morph_Info actually stores is confirmed
// beyond its existence and its owning category. Every table below that
// references "a morph name" (morph_groups, master_sliders, presets,
// regional_presets) therefore keeps that reference as RAW TEXT only - this
// reader has no model of the namespace to resolve names against, and several
// spec-documented loader behaviours that depend on that resolution (e.g.
// §14.2's "a row with an unresolved name is silently dropped, not stored")
// are consequently NOT reproduced here; every authored row/entry is kept.
//
// Source: spec-tables-customization.md, the ONLY document consulted for this
// file's schema. Built entirely on include/sr3xtbl/xtbl.h.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3tables_customization/color_pools.h"  // ColorText
#include "sr3xtbl/xtbl.h"

namespace sr3tables_customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// 14.1 player_creation.xtbl - the 14-category face/body Morph_Set root.
// [CONFIRMED - disassembly + empirical]
// ===========================================================================
// The 14 canonical Morph_Set category names (`0x0130b5ac`, §14.1) - a clean
// literal comma list in the spec text, unlike §9.3's abbreviated 34-slot
// list, so an enum-index array IS modelled here.
inline constexpr std::array<std::string_view, 14> kMorphSetCategoryNames = {
    "Body", "Face Global", "Crown", "Forehead", "Brow", "Eyes", "Nose", "Cheekbones",
    "Ears", "Chin", "Mouth", "Jaw", "Hair", "Skin",
};

struct MorphSet {
    std::optional<std::string> name;         // Name -> kMorphSetCategoryNames (real data casing "Morph_Set"; the
                                               // filename literal's own casing is "Morph_set" - matched
                                               // case-insensitively per the usual convention, §14.1)
    int categoryIndex = -1;
    std::optional<std::string> displayName;   // DisplayName (localized)

    // Morph_Infos > Morph_Info[]: per-entry reader (FUN_009f6570) NOT
    // decompiled this pass - nothing about a Morph_Info's OWN fields is
    // confirmed (see the file banner). Only the raw rows are located here,
    // matching this project's treatment of characters.h §13.1/§13.3.
    // Pointers alias the parsed Document - keep it alive while using this.
    // Shared, cross-category pool capped at 128 (0x80) TOTAL entries across
    // ALL 14 categories combined, not per-category (§14.1) - not enforced
    // here (a whole-table concern, not a single row's).
    std::vector<const Node*> morphInfoRows;
};
MorphSet ParseMorphSet(const Node* row);
std::vector<MorphSet> ParsePlayerCreationTable(const Document& doc);

// ===========================================================================
// 14.2 player_creation_morph_groups.xtbl - named UI-facing morph groupings.
// [CONFIRMED - disassembly + empirical] Each morph_group is a 12-byte (0xc)
// record; each morph_item is 8 bytes.
// ===========================================================================
struct MorphGroupItem {
    std::optional<std::string> morphName;  // morph_name -> §14.1's morph namespace (FUN_009f5c30) - see the file
                                             // banner: the real loader silently drops an entry whose name doesn't
                                             // resolve; this reader cannot reproduce that filter and keeps every
                                             // authored morph_item
    bool isGenderType = false;  // display_type == "gender" (the only recognised value, §14.2); any other
                                  // value or an absent element leaves this false
};
struct MorphGroup {
    std::optional<std::string> name;      // Name - "both hashed AND kept as a display string - the same text is
                                            // used for both, re-copied into a second buffer for the string form"
                                            // (§14.2); modelled as one field since both copies are identical
    std::vector<MorphGroupItem> morphList;  // morph_list > morph_item[]
};
MorphGroup ParseMorphGroup(const Node* row);
std::vector<MorphGroup> ParsePlayerCreationMorphGroupsTable(const Document& doc);

// ===========================================================================
// 15.1 player_creation_normal_maps.xtbl - per-body-type morph-to-normal-map
// curve. [CONFIRMED - disassembly + empirical]
// ===========================================================================
// The 8-entry body-type array (`0x0130b670`, §15.1) - also the array
// player_master_sliders.xtbl/player_presets.xtbl index into "by category"
// per the spec's own cross-reference note; a clean literal comma list.
inline constexpr std::array<std::string_view, 8> kBodyTypeNames = {
    "Base", "Female", "Male", "Skinny", "Fat", "Muscle", "Body Age", "Face Age",
};

struct SliderKey {
    Always<float> sliderPosition;      // Slider_Position
    Always<float> normalMapStrength;    // Normal_Map_Strength
};
struct MorphSlider {
    std::optional<std::string> sliderName;  // Slider_Name (hashed)
    // Slider_Keys > Slider_Key[]: capped at 8 keys per slider in the real
    // loader (§15.1), not enforced here. The post-load sort/curve-normalise
    // step (FUN_008f4700) is derived, not modelled - raw doc-order keys kept.
    std::vector<SliderKey> sliderKeys;
};
struct NormalMapSettings {
    std::optional<std::string> name;   // Name -> kBodyTypeNames
    int bodyTypeIndex = -1;
    // Morph_Sliders > Morph_Slider[]: capped at 4 sliders per body type in
    // the real loader (§15.1), not enforced here.
    std::vector<MorphSlider> morphSliders;
};
NormalMapSettings ParseNormalMapSettings(const Node* row);
std::vector<NormalMapSettings> ParsePlayerCreationNormalMapsTable(const Document& doc);

// ===========================================================================
// 15.2 player_creation_hair_color.xtbl. [CONFIRMED - disassembly +
// empirical] Each Hair_Color is a 72-byte (0x48) record.
// ===========================================================================
struct HairColor {
    std::optional<std::string> name;          // Name (+ a string-form copy of the same text, §15.2 - one field kept)
    std::optional<std::string> displayName;    // Display_Name
    std::optional<std::string> shaderball;      // Shaderball (texture ref via FUN_009f9f20 - the SAME helper
                                                  // player_presets.xtbl's Composites>Layer field uses, §16.2,
                                                  // "strongly suggesting a shared 'shaderball preview' concept")
    std::optional<std::string> swatchImage;      // swatch_image (hashed texture name)
    std::optional<std::string> swatchLabel;      // swatch_label - spec: "defaults to a fixed placeholder string if
                                                   // absent"; the literal placeholder text is not given, so no
                                                   // OrDefault() helper is provided (unlike this project's usual
                                                   // convention for a spec-STATED concrete default) - nullopt here
                                                   // just means "absent, real default text unknown"
    std::optional<float> hue;                      // Hue - RAW authored value; "scaled by a fixed constant
                                                    // DAT_01154b78" at load time (§15.2) - the constant's value
                                                    // isn't given, so the scale is NOT applied here
    Always<float> saturationLight, saturationDark;   // Saturation_light, Saturation_dark
    Always<float> specularAlpha;                       // Specular_alpha
    Always<float> brightness, brightnessBias;           // Brightness, Brightness_bias
    std::optional<std::string> specularBrightRaw;         // Specular_bright - "parsed by a multi-component helper
                                                           // (FUN_00dad130) whose exact output arity (vec2 vs vec3)
                                                           // was not narrowed" (§15.2, open item §22.9) - raw text,
                                                           // not decomposed into components
    std::optional<std::string> specularDarkRaw;            // Specular_dark - same treatment
};
HairColor ParseHairColor(const Node* row);
std::vector<HairColor> ParsePlayerCreationHairColorTable(const Document& doc);

// ===========================================================================
// 15.3 player_creation_skin_colors.xtbl. [CONFIRMED - disassembly +
// empirical] Each Entry is a 56-byte (0x38) record.
// ===========================================================================
struct SkinColorEntry {
    std::optional<std::string> name;         // Name (+ string copy)
    std::optional<std::string> displayName;   // display_name - "falls back to a copy of Name if the localization
                                                // lookup misses" (§15.3), a load-time fallback not modelled here;
                                                // nullopt means the element itself was absent
    std::optional<std::string> swatch;         // swatch (hashed texture name)
    std::optional<std::string> shaderball;      // Shaderball (same FUN_009f9f20 helper as §15.2)
    Always<float> unmaskedHue, unmaskedSaturation, unmaskedBrightness;  // Unmasked_Hue/Saturation/Brightness -
                                                                          // "a distinct triple from the masked/
                                                                          // final ones" (§15.3)
    Always<float> hue;                          // Hue (reusing §15.2's field-name constant, per the spec's own note)
    Always<float> saturation, brightness;         // Saturation, Brightness
    Always<float> specularAlpha;                   // Specular_Alpha
    Always<float> fresnelAlpha;                      // Fresnel_Alpha - "a Fresnel term not present on
                                                       // player_creation_hair_color.xtbl's record" (§15.3)
    Always<float> specularPower;                       // Specular_Power
};
SkinColorEntry ParseSkinColorEntry(const Node* row);
std::vector<SkinColorEntry> ParsePlayerCreationSkinColorsTable(const Document& doc);

// ===========================================================================
// 16.1 player_master_sliders.xtbl - shipped <Table> is EMPTY, and (unlike
// customization_slot_defaults.xtbl, §6.2) its <TableTemplates> sibling is
// ALSO genuinely empty - "the whole 'master slider' mechanism, while real
// and fully wired into the executable, has zero authored content at
// retail" (§16.1). [CONFIRMED - disassembly + empirical] Each Slider is a
// 20-byte (0x14) record.
// ===========================================================================
struct MasterSlider {
    std::optional<std::string> name;          // Slider > Name
    std::optional<std::string> displayName;    // Slider > DisplayName
    std::optional<float> initialValue;          // Slider > InitialValue
    std::optional<std::string> morphListRaw;     // Slider > MorphList - "a text blob parsed via FUN_00dac5b0 into
                                                  // a name list" (§16.1); the delimiter/grammar is not given, so
                                                  // this is kept as raw, unsplit text. Each resolved name would map
                                                  // through §14's morph namespace into an {id, weight=0} pair - the
                                                  // weight-zero initialisation is a load-time default, not modelled.
};
struct MasterSliderCategory {
    std::optional<std::string> name;         // MasterSliders (row) > Name
    std::optional<std::string> displayName;   // DisplayName
    std::vector<MasterSlider> sliders;         // Slider[]
};
MasterSliderCategory ParseMasterSliderCategory(const Node* row);
// Always returns an empty vector against real shipped data (§16.1, §21);
// included for completeness and for any future/modded archive that does
// populate the table.
std::vector<MasterSliderCategory> ParsePlayerMasterSlidersTable(const Document& doc);

// ===========================================================================
// 16.2 player_presets.xtbl - full character-creator preset records.
// [CONFIRMED - disassembly + empirical] Each Preset is a 100-byte fixed record.
// ===========================================================================
struct PresetComposite {
    std::optional<std::string> layer;   // Layer -> customization_compositing.xtbl Composite_Layer.Name (§9.3, FUN_009f9f20)
    std::optional<ColorText> color;      // Color; "defaulting to opaque white if absent" (§16.2) - a spec-STATED
                                           // CONCRETE DEFAULT (distinct from the general Always-hazard)
    ColorText ColorOrDefault() const { return color.value_or(ColorText{1.0f, 1.0f, 1.0f, 1.0f}); }
};
struct PresetElement {
    std::optional<std::string> morphName;  // Morph_Name -> §14's morph namespace - "the actual per-preset sculpt data"
    Always<float> value;                     // Value
};
struct PresetRegionalPresetRef {
    std::optional<std::string> category;  // Category (via FUN_00838cc0) -> §16.3's registry
    std::optional<std::string> preset;     // Preset (via FUN_00838d10) -> §16.3's registry
};
struct Preset {
    std::optional<std::string> name;   // Name (hashed)
    std::optional<std::string> race;    // Race (via FUN_00832000; "presumably the same 4-name race vocabulary as
                                          // elsewhere in this group - not independently confirmed to be the
                                          // identical array", §16.2) - raw text only, no enum index modelled
    std::optional<std::string> gender;   // Gender (via FUN_00831fc0) - raw text only
    Always<bool> isDefault;               // Default - "marks the character creator's initial preset"
    std::optional<std::string> displayName;  // DisplayName
    std::optional<std::string> hair;          // Hair -> customization_items.xtbl item (FUN_008282c0, the SAME
                                               // helper customization_default_items.xtbl's Item field uses, §8 -
                                               // "confirming a preset's hair selection is a real
                                               // customization_items.xtbl row, not a separate hairstyle list")
    Always<float> hairLength;                  // Hair_Length
    std::optional<std::string> hairColorPrimary;    // Hair_Color_Primary -> §15.2's loaded Hair_Color array (by name)
    std::optional<std::string> hairColorSecondary;   // Hair_Color_Secondary
    std::optional<std::string> skinColor;              // Skin_Color -> §15.3's loaded Entry array (by name)
    std::vector<PresetComposite> composites;             // Composites > Composite[] - hard cap 5 entries per
                                                           // preset (previously undocumented, CONFIRMED
                                                           // 2026-10-02: the fixed 100-byte record has room for
                                                           // no more), enforced by ParsePreset
    std::vector<PresetElement> presetGrid;                // Preset_Grid > Preset_Element[]
    std::vector<PresetRegionalPresetRef> regionalPresets;  // RegionalPresets > RegionalPreset[]
};
Preset ParsePreset(const Node* row);
std::vector<Preset> ParsePlayerPresetsTable(const Document& doc);

// ===========================================================================
// 16.3 player_regional_presets.xtbl - named regional face templates, three
// levels deep. [CONFIRMED - disassembly + empirical]
// ===========================================================================
struct RegionalPresetMorphTarget {
    std::optional<std::string> morph;  // MorphTarget > Morph -> §14's morph namespace
    Always<float> target;                // MorphTarget > Target
};
struct RegionalPresetEntry {
    std::optional<std::string> name;          // Presets > Preset > Name
    std::optional<std::string> displayName;    // DisplayName
    std::optional<std::string> figure;          // figure (via FUN_00831f80 - raw text)
    std::vector<RegionalPresetMorphTarget> morphTargets;  // MorphTargets > MorphTarget[]
};
struct RegionalPresetsGroup {
    std::optional<std::string> name;          // RegionalPresets (the row tag; wrapper and row share the identical
                                                // tag name, "the same convention as gang_customization.xtbl's
                                                // gang_vehicles", §16.3) > Name
    std::optional<std::string> displayName;    // DisplayName
    std::vector<RegionalPresetEntry> presets;   // Presets > Preset[]
};
RegionalPresetsGroup ParseRegionalPresetsGroup(const Node* row);
std::vector<RegionalPresetsGroup> ParsePlayerRegionalPresetsTable(const Document& doc);

// ===========================================================================
// 17. player_cust_shot_map.xtbl. [CONFIRMED - disassembly + empirical]
// Structurally and address-wise unrelated to every other table in this
// document, and NOT part of the *_cameras*.xtbl family (out of this spec's
// scope per its own §1 scope note) despite the similar naming.
// ===========================================================================
struct NewEntity {
    std::optional<std::string> name;     // Name (hashed)
    std::optional<std::string> animPos;   // Anim_Pos (hashed) - together, matched as a pair against a small
                                            // fixed lookup array of known entity/anim-position combinations
                                            // (§17). CORRECTED 2026-10-02 (re-derived from the executable,
                                            // `func_0x0080c330.txt` read in full): the array is CONFIRMED
                                            // exactly 6 slots (not "~12" as previously estimated). The resolved
                                            // slot id and a second output dword are pushed into a bounded
                                            // output array (capacity/count/array-pointer identity also fully
                                            // resolved this pass: DAT_022cbf4c=capacity 46, DAT_022cbf50=count,
                                            // DAT_022cbf48=array base). The spec's earlier claim that the
                                            // second dword is "a copy of the entity's own local position (a
                                            // float triple)" is NOT supported by the full decompile - no
                                            // vector/position assembly appears anywhere in this function; that
                                            // second dword's true source is still OPEN (confirmed to be exactly
                                            // one dword, not a float triple). None of this is modelled here
                                            // either way - all of it is derived/runtime, not an XML field.
};
NewEntity ParseNewEntity(const Node* row);
std::vector<NewEntity> ParsePlayerCustShotMapTable(const Document& doc);

}  // namespace sr3tables_customization
