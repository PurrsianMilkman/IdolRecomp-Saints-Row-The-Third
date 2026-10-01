#pragma once

// sr3tables_customization/items.h - customization_items.xtbl (spec-tables-
// customization.md §4) and customization_stores.xtbl (§5.2). Included from
// sr3tables_customization/tables.h.
//
// ---------------------------------------------------------------------------
// customization_outfits.xtbl (§5.1) IS DELIBERATELY NOT REIMPLEMENTED HERE.
// ---------------------------------------------------------------------------
// Per the task brief's overlap rule: include/sr3customization/customization.h
// (built from spec-customization-data.md) already models `Outfit` /
// `ContentElement` / `StyleElement` for this exact file, and comparing that
// schema against spec-tables-customization.md §5.1's text shows it is
// materially the same - Name/Display_Name, Is_DLC, Content_Grid >
// Content_Element (Item, Default_Wear_Option, Primary/Secondary/Tertiary_
// Color), Styles_Grid > Style_Element > Variant_Grid > Variant_Element. The
// one thing §5.1 states that the older reader doesn't model, "a CRC field
// (&DAT_0115f594)", reads as a DERIVED value (a CRC-32 of the outfit Name,
// the same kind of load-time-computed key this project's convention already
// leaves unmodelled elsewhere - e.g. sr3tables_environment's SkyboxEffect.name
// "-> CRC-32 key (derived; not stored here)") - not a new authored XML
// element, so it is not a basis for a second reader. See
// include/sr3customization/customization.h for that type.
//
// customization_items.xtbl (§4) IS reimplemented here, under new type names
// (CustomizationItemEntry, not sr3customization::CustomizationItem), because
// §4's text is genuinely richer than spec-customization-data.md's: real,
// disassembly-confirmed fields the older reader has no equivalent for at all
// - Brand, Heel_Angle/Heel_Height, ShoeAudioSwitch/ClothingAudioSwitch (and
// the same-destination-field bug between them), Fat_Bone_Arm/Fat_Bone_Leg,
// the Style enum, the specific "not ready"/"big hat"/"variants are same
// item" Flags bits, the "npc only" Slot fallback, the 858-row hard cap, and
// the Comparison sub-flag on each Wear_Option flag reference. See the
// DISCREPANCIES banner below for the handful of specific fields where this
// header's reading of §4 differs from the older reader's reading of its own
// spec - flagged for the orchestrating session to reconcile, not resolved
// here.
//
// ---------------------------------------------------------------------------
// DISCREPANCIES vs sr3customization::CustomizationItem (for reconciliation)
// ---------------------------------------------------------------------------
//  1. Mesh_Information: §4.2 states only `Male_Mesh_Filename > Filename` is
//     read ("the exact mesh-string half of the custmesh_<N> ... recipe").
//     It does not mention a Female_Mesh_Filename counterpart at all, unlike
//     sr3customization::MeshInformation (which has one, from
//     spec-customization-data.md). Not modelled here since THIS spec's text
//     never confirms it - may simply be a gap in this pass's decompile
//     rather than a real absence.
//     LABEL: spec-tables-customization.md §4.2 marks this [OPEN - desk review 2026-09-30] (conflict with
//     spec-customization-data.md §2.1/§5.2, which documents Female_Mesh_Filename > Filename).
//  2. Shader_Type: §4.3's own sentence structure ("Material_List >
//     Material_Element[] (each: Material name ... ; Shader_Type, plus a
//     hardcoded ...)") reads Shader_Type as a PER-Material_Element field.
//     sr3customization::CustomizationVariant models Shader_Type once, at the
//     VARIANT level. This header follows §4.3's literal text (per
//     MaterialElementEntry, not per VariantEntry) - a documented judgement
//     call, not a certainty either way.
//     LABEL: spec-tables-customization.md §4.3 marks the Shader_Type parent [OPEN - desk review 2026-09-30]
//     (spec-customization-data.md §2.1 lists it at variant level).
//  3. Default_Colors_Grid: §4.4 describes this ONLY at the ITEM level
//     ("per-item preset colour slots"); a Variant's own Material_List is
//     described as supplying a colour-SLOT COUNT used to clamp the item's
//     grid, not a second colour list. sr3customization::CustomizationVariant
//     has its own defaultColorsGrid field (per spec-customization-data.md).
//     This header has no per-variant Default_Colors_Grid at all - matches
//     §4.4's text, differs from the older reader's schema.
//
// Source: spec-tables-customization.md §4-§5.2, the ONLY document consulted
// for this file's schema. Built entirely on include/sr3xtbl/xtbl.h.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3tables_customization/slots_categories.h"  // kCanonicalSlotNames, StateAnimation
#include "sr3xtbl/xtbl.h"

namespace sr3tables_customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// The 24 canonical slot names (§6.1, `PTR_DAT_0130b780`) are declared as
// kCanonicalSlotNames in slots_categories.h, included BEFORE this file from
// the umbrella tables.h (see that file's #include order) since §4, §6.1,
// §6.2 and §7.1 all resolve a `Slot` reference against the same array.

// ===========================================================================
// 4.2 Wear_Option (0x28 = 40 bytes each)
// ===========================================================================
// A flag name reference (Active_Flag / Required_Flag / Incompatible_Flag):
// "each also carrying its own Comparison (yes/no) sub-flag packed into the
// flag-index byte's top bit" (4.2). Modelled as the element's OWN text (the
// flag name, matched against customization_flags.xtbl's 39-entry registry,
// §7.2) plus its "Comparison" child - a judgement call on the XML shape
// (mixed content: leaf text + one child), consistent with this project's
// document model (sr3xtbl.h's TEXT rules) and with authored data elsewhere
// in this spec group using the same shape.
struct WearOptionFlagRef {
    std::optional<std::string> name;  // the flag's own text -> customization_flags.xtbl Flag.Name (§7.2)
    std::optional<bool> comparison;   // Comparison child (yes/no)
};

struct WearOptionEntry {
    std::optional<std::string> name;             // Name (hashed)
    std::optional<std::string> maleMeshFilename;  // Mesh_Information > Male_Mesh_Filename > Filename
                                                    // (the .cmeshx name, engine string-hash FUN_00da7930 -
                                                    // the mesh-string half of the custmesh_<N>.str2_pc bundle
                                                    // recipe, spec-customization-data.md §5 / this spec §4.2)
    std::vector<WearOptionFlagRef> activeFlags;        // Active_Flags > Active_Flag[]
    std::vector<WearOptionFlagRef> requiredFlags;      // Required_Flags > Required_Flag[]
    std::vector<WearOptionFlagRef> incompatibleFlags;  // Incompatible_Flags > Incompatible_Flag[]
    // `Disabled` itself is NOT a field here: "A Wear_Option with Disabled=yes
    // is dropped entirely ... disabled options simply don't exist in the
    // parsed output" (4.2) - ParseWearOptionsList applies that filter, so
    // every WearOptionEntry this reader produces was, by construction, not
    // disabled.
};

// ===========================================================================
// 4.3 Variant (0x70 = 112 bytes each)
// ===========================================================================
struct MaterialElementEntry {
    std::optional<std::string> material;    // Material -> customization_materials.xtbl Cust_Material.Name (§9.1)
    std::optional<std::string> shaderType;   // Shader_Type - see DISCREPANCY 2 above (modelled per-element, not per-Variant)
    // The hardcoded __stricmp(name,"cm_suit_deckersuit") special case (4.3)
    // is load-time control flow (feeds FUN_00828de0), not an XML field - not modelled.
};

struct VariantEntry {
    std::optional<std::string> name;             // Name (localized + hashed)
    std::optional<int32_t> price;                 // Price - "default to the item's own Base_Price ... when
                                                    // the authored value equals a sentinel" (4.3); the sentinel
                                                    // value itself is not given by the spec, so this field is
                                                    // kept as read (nullopt only when the element is truly
                                                    // absent) rather than guessing which authored integer means
                                                    // "use the item default"
    std::optional<int32_t> respectBonus;           // Respect_Bonus - same fallback convention as Price
    std::optional<std::string> meshVariantName;    // Mesh_Variant_Info > Variant_Name (hashed)
    Always<int32_t> variantId;                     // Mesh_Variant_Info > VariantID ("essentially always 1 in real data")
    std::vector<MaterialElementEntry> materialList; // Material_List > Material_Element[]
};

// ===========================================================================
// 4.4 Default_Colors_Grid (item-level only - see DISCREPANCY 3 above)
// ===========================================================================
// Each Default_Color is documented as ONE OF Clothing_Color / Tattoo_Color /
// Makeup_Color (pool index 0/3/2 respectively, resolved through the shared
// §2 colour-pool helper) - all three fields are kept per entry rather than
// enforced mutually-exclusive, since the spec does not state the parser
// itself rejects more than one being present.
struct DefaultColorEntry {
    std::optional<std::string> clothingColor;  // Default_Color > Clothing_Color (pool 0, character_color_pool.xtbl)
    std::optional<std::string> tattooColor;    // Default_Color > Tattoo_Color (pool 3, tattoo_color_pool.xtbl)
    std::optional<std::string> makeupColor;    // Default_Color > Makeup_Color (pool 2, makeup_color_pool.xtbl)
};

// ===========================================================================
// 4.1 Customization_Item row (0x7c = 124 bytes each; HARD CAP 858 rows -
// enforced by ParseCustomizationItemsTable per the note below, NOT by
// ParseCustomizationItem itself)
// ===========================================================================
struct CustomizationItemEntry {
    // --- Flags (3 bits recovered, §4.1; "npc only" is a 4th confirmed flag
    // NAME used only for the Slot fallback below, not counted among the 3
    // recovered bit positions) ---
    bool notReady = false;             // Flags > Flag == "not ready" (bit 0). Rows with this flag are DROPPED
                                        // entirely by the real loader; ParseCustomizationItemsTable applies that
                                        // filter, so this field is here only for callers of ParseCustomizationItem
                                        // directly on a single row.
    bool bigHat = false;               // bit 1
    bool variantsAreSameItem = false;  // bit 2
    bool npcOnly = false;              // "npc only" - not one of the 3 recovered bit positions; used only to
                                        // decide whether an unmatched Slot is still accepted (see `slot` below)

    std::optional<std::string> slot;   // Slot - text, hashed then resolved against the 24-name canonical slot
                                        // array (§6.1, CanonicalSlotNames()); unmatched -> the row is accepted
                                        // WITHOUT a slot binding only if `npcOnly` is set (4.1) - not enforced
                                        // as a drop here (see slotIndex, which is simply -1 when unmatched,
                                        // the same EnumIndex convention this project uses elsewhere, e.g.
                                        // sr3tables_environment::TimeOfDayObjectType)
    int slotIndex = -1;                // index into CanonicalSlotNames(), or -1 if `slot` didn't match

    Always<int32_t> basePrice;          // Base_Price
    Always<int32_t> baseRespectBonus;   // Base_Respect_Bonus

    std::optional<std::string> brand;   // Brand - matched against a 6-entry fixed array in the real loader; only
                                         // ONE entry ("Astro Gaming") was individually confirmed (§4.1, §22.8) -
                                         // NOT modelled as an enum index here (5 of 6 names are unconfirmed);
                                         // kept as raw text only

    std::optional<std::string> name;        // Name (hashed; also checked against "basicparachute" as a one-off
                                             // special case tying it to the parachute slot group - derived
                                             // control flow, not modelled)
    Always<bool> isDlc;                      // Is_DLC (yes/no -> 0xff base / 0 DLC sentinel byte)
    std::optional<std::string> displayName;  // DisplayName (localization key; falls back to a default string id
                                              // if unresolved - a load-time fallback, not modelled)

    std::vector<WearOptionEntry> wearOptions;   // Wear_Options > Wear_Option[] (Disabled=yes options already
                                                 // filtered out - see WearOptionEntry)
    std::vector<VariantEntry> variants;          // Variants > Variant[]
    std::vector<DefaultColorEntry> defaultColorsGrid;  // Default_Colors_Grid > Default_Color[] (item-level; see
                                                        // DISCREPANCY 3 - clamping/back-fill against a variant's
                                                        // own colour-slot count is a load-time transform, not
                                                        // modelled)

    // Heel_Angle / Heel_Height: "floats, default 0" - a SPEC-STATED CONCRETE
    // DEFAULT (distinct from the general Always-hazard; matches this
    // project's `...OrDefault()` convention, sr3tables_environment/tables.h).
    std::optional<float> heelAngle;
    std::optional<float> heelHeight;
    float HeelAngleOrDefault() const { return heelAngle.value_or(0.0f); }
    float HeelHeightOrDefault() const { return heelHeight.value_or(0.0f); }

    // ShoeAudioSwitch / ClothingAudioSwitch: "both write the same +0x6c
    // field ... if both are present, ClothingAudioSwitch overwrites
    // ShoeAudioSwitch (real code behaviour, not corrected here)" (4.1). Both
    // raw elements are kept, PLUS the resolved effective value the real
    // loader would have stored (Clothing wins over Shoe when both present -
    // this is a fixed check order in the real code, not a document-order
    // "last one wins").
    std::optional<std::string> shoeAudioSwitch;
    std::optional<std::string> clothingAudioSwitch;
    const std::optional<std::string>& AudioSwitchEffective() const {
        return clothingAudioSwitch.has_value() ? clothingAudioSwitch : shoeAudioSwitch;
    }

    // Fat_Bone_Arm / Fat_Bone_Leg: same "floats, default 0" convention as Heel_*.
    std::optional<float> fatBoneArm;
    std::optional<float> fatBoneLeg;
    float FatBoneArmOrDefault() const { return fatBoneArm.value_or(0.0f); }
    float FatBoneLegOrDefault() const { return fatBoneLeg.value_or(0.0f); }

    // Style: "text enum: Cool->bit 0x10, Costume->bit 0x8, NyteBlayde->bit
    // 0x20 (OR'd into the row's own flags dword)" (4.1). The OR-into-flags
    // step is a load-time transform over the row's OWN internal flags dword
    // (not one of the 3 recovered Flags>Flag bits above, a separate dword) -
    // not modelled; only the matched enum index is kept.
    std::optional<std::string> style;
    int styleIndex = -1;  // index into kStyleNames ("Cool","Costume","NyteBlayde"), or -1 if unmatched/absent
};
inline constexpr std::array<std::string_view, 3> kStyleNames = {"Cool", "Costume", "NyteBlayde"};

CustomizationItemEntry ParseCustomizationItem(const Node* row);
// Applies the real loader's two documented row-level filters: rows whose
// Flags include "not ready" are dropped (4.1), and the 858-row hard cap
// (0x35a) is enforced by simply not reading past the 858th accepted row -
// this reader does not reproduce the cap as an error, only as a stopping
// point, matching how sr3tables_environment treats its own hard-capped
// tables (e.g. lens_flares.xtbl, camera_shake.xtbl).
// LABEL: spec-tables-customization.md §4.1 (and §5.2 for the store caps 255/73): the compare operator is
// [OPEN - desk review 2026-09-30]; the `>= N` coding is our assumption (see LABEL comments in the .cpp). Only
// the normals cap (materials.h, §9.2) can change a result on real data.
std::vector<CustomizationItemEntry> ParseCustomizationItemsTable(const Document& doc);

// ===========================================================================
// 5.2 customization_stores.xtbl - in-game clothing store inventories.
// [CONFIRMED - disassembly + empirical, spec §5.2]
// ===========================================================================
struct StoreItemEntry {
    std::optional<std::string> itemName;  // Store_Item's own text -> customization_items.xtbl item Name
                                            // (5.2: "item lookup by name"; the item's own +0x78 viability/DLC
                                            // byte check is derived control flow, not modelled)
    bool variantsPresent = false;           // whether a Variants wrapper was authored at all - "if absent, EVERY
                                             // variant of the item is listed by default" (5.2), a load-time
                                             // default this reader does not compute (needs the whole item table)
    std::vector<std::string> itemVariants;   // Variants > Item_Variant[] (each's own text -> that item's own Variant name)
};

struct Store {
    std::optional<std::string> name;             // Name (hashed)
    bool allowWardrobe = false;                    // Flags > Flag == "allow_wardrobe" (single bit, 5.2)
    std::vector<StoreItemEntry> storeItems;         // Store_Item[] - capped at 255 (0xfe) in the real loader,
                                                     // not enforced here (never observed near the cap, §21)
    std::vector<std::string> outfitElements;         // Outfits > Outfit_Element[] (each's own text -> a
                                                       // customization_outfits.xtbl Outfit Name) - capped at 73
                                                       // (0x48+1) in the real loader, not enforced here
};

Store ParseStore(const Node* row);
std::vector<Store> ParseCustomizationStoresTable(const Document& doc);

}  // namespace sr3tables_customization
