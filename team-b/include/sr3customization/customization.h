#pragma once

// sr3customization - typed reader for character/item customization data
// (spec-customization-data.md section 2): `customization_items.xtbl`'s
// <Customization_Item> rows (section 2.1) and `customization_outfits.xtbl`'s
// <Outfit> rows (section 2.2).
//
// Built ENTIRELY on sr3xtbl's accessors (include/sr3xtbl/xtbl.h): FindChild,
// NextSibling, ChildText, GetX/ReadXAlways, NameHash. Nothing here touches
// the XML parser itself, and nothing here was read from the game .exe or any
// disassembly - every field and structural note below is transcribed from
// spec-customization-data.md, per this project's hard rule 1 (implement ONLY
// from the spec text and xtbl.h).
//
// ---------------------------------------------------------------------------
// IMPORTANT CORRECTION TO THE SPEC'S OWN §1/§4 ITEM 1 (read this first)
// ---------------------------------------------------------------------------
// spec-customization-data.md section 1 and section 4 item 1 say the base-game
// `customization_items.xtbl` / `customization_outfits.xtbl` (and the shared
// `customization_slots.xtbl` / `customization_slot_defaults.xtbl` /
// `vehicle_cust_color_pool.xtbl` / `vehicle_cust_color_sets.xtbl` tables) are
// "currently inaccessible" because they sit at non-first positions inside the
// mode-(a) compressed container `misc_tables.vpp_pc`. That limitation is
// RESOLVED as of this project's own HANDOFF.md section 9.78: vpp::Container
// now decodes every compressed entry of every container, mode (a) and mode
// (b) alike, exact - so every one of those tables IS now directly readable
// through vpp::Container, exactly like any other archive entry. This header
// and its parser were validated directly against the real base-game
// `customization_items.xtbl` / `customization_outfits.xtbl` inside
// `misc_tables.vpp_pc` (see tools/validation/validate_customization_population.cpp),
// not just the DLC samples the spec text itself was limited to. Every FIELD
// NAME, structural nesting and enum value below still comes only from the
// spec text (cleanroom rule 1) - only the "can we read the base table at
// all" fact is corrected here, mirroring what happened for
// spec-vehicle-data.md section 7 (also unblocked by the same 9.78 fix).
//
// ---------------------------------------------------------------------------
// CONVENTIONS (matching sr3vehicleinfo / sr3tables_weapons house style)
// ---------------------------------------------------------------------------
// 1. Always<T> (sr3xtbl.h) vs std::optional<T>: the spec does not mark
//    individual fields with an explicit "always write" vs "write only if
//    present" reader discipline (unlike spec-tables-weapons-combat.md's
//    "(a)"/"(i)" markers). Per this project's established precedent
//    (sr3vehicleinfo/vehicle_entry.h convention 1: "where the spec gives no
//    explicit marker, default to Always<T>"), every NUMERIC/BOOL scalar field
//    here defaults to Always<T>; every plain STRING/text field (names,
//    references, category text) uses std::optional<std::string> instead,
//    since sr3xtbl has no "always write" string reader and a zero-length C
//    string stand-in would be misleading for a field whose real content is
//    text, not a number.
// 2. Nowhere does this reader invent a numeric TYPE the spec does not state.
//    Where the spec names a field but not its width/signedness (e.g.
//    Base_Price, Price, Respect_Bonus, File_Id, Weight), this reader makes an
//    explicit, documented judgement call at that field - same footing as
//    sr3vehicleinfo's Component_Density/Value/Props precedent (vehicle_entry.h
//    convention/comment: "type not distinguished by the spec beyond ...").
// 3. Several field GROUPS are named by the spec only as a category label over
//    a list ("Active_Flags", "Particle_Systems", "VID_List", ...) without the
//    spec giving the literal inner (per-item) XML element name. Each such
//    case is flagged INTERPRETATION at its struct/field with the concrete
//    choice made and the precedent it follows (matching this project's
//    established practice: sr3vehicleinfo's Seat_Specific_Data/"Seat" and
//    NoCustVariantsGrid/NoCustVariantElement precedent, vehicle_entry.h).
// 4. The `Color` field of a vehicle-customization `Color_Choice` (spec
//    section 3) is documented as "a ComboElement offering either a single
//    Color_Pool reference or a Color_Set (palette) reference" - modeled in
//    vehicle_customization.h as an explicit optional-either (ColorRef), not
//    two separate optional fields, so a caller cannot observe an invalid
//    "both present" or silently-ambiguous state. See that header for the
//    wrapper-element judgement call.
//
// ---------------------------------------------------------------------------
// DELIBERATELY OPEN / SKIPPED (nothing below is a silent gap - see the task's
// own instructions and spec-customization-data.md sections 4/5.4)
// ---------------------------------------------------------------------------
//  * `customization_slots.xtbl` / `customization_slot_defaults.xtbl` /
//    `vehicle_cust_color_pool.xtbl` / `vehicle_cust_color_sets.xtbl`: the
//    spec names only their FILENAMES and container positions, never their
//    internal field schema (section 1, section 3's "Cross-reference chain"
//    paragraph, section 4 item 1) - so no typed struct exists for them here.
//    The validation harness DOES locate and report on them (they are now
//    readable, per the correction above) but does not parse their rows.
//  * `player_morph.vpp_pc` content (section 2.4): the spec explicitly says
//    this was "not opened or investigated further" - no reader here.
//  * The per-vehicle `<vehicle_name>_cust.xtbl` slot-definition file (spec
//    section 3's "Cross-reference chain" paragraph): the spec says it does
//    not exist as a loose file and was likely redirected into the per-vehicle
//    `.cvtf_pc` binary (a different, unrelated format, out of scope here).
//  * `.cmorph_pc` / `.ccmesh_pc` binary sub-formats (spec sections 2.3/4 item
//    4): out of scope, already partially covered elsewhere in this project.
//  * The `Wear_Option` flag system's runtime semantics (spec section 4 item
//    5): the spec confirms the mechanism exists (Active_Flags/Required_Flags/
//    Incompatible_Flags) but every sample it checked had these lists empty,
//    so what actually SETS/CHECKS them at runtime is unconfirmed - this
//    reader stores the lists verbatim (see convention 3) but computes nothing
//    from them.
//
// See vehicle_customization.h for the DIFFERENT `<vehicle_name>.xtbl` (no
// `_veh` suffix) vehicle-customization variant file (spec section 3).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ---------------------------------------------------------------------------
// customization_items.xtbl (spec section 2.1)
// ---------------------------------------------------------------------------

// `Wear_Option > Mesh_Information` (spec 2.1 "Mesh assignment").
struct MeshInformation {
    std::optional<std::string> maleMeshFilename;    // Male_Mesh_Filename > Filename
    std::optional<std::string> femaleMeshFilename;   // Female_Mesh_Filename > Filename (only on items with a female mesh)
    Always<bool> cutsceneOnly;                         // Cutscene_Only

    // Obscured_slots > Obscured_slot > Slot: the "hats hide hair" mechanic -
    // other clothing slot names hidden while this item is worn (spec 2.1
    // gives real examples: "head hair", "eyewear", "facewear").
    std::vector<std::string> obscuredSlots;

    // VID_List: spec names this field only as a category label ("a
    // 'visibility ID' concept, not resolved further this pass") and never
    // gives its inner per-entry element name. INTERPRETATION: read as a flat
    // list of <VID> children directly under <VID_List> (the same flat-list
    // shape as sr3vehicleinfo's Hardtop_VIDs/RoofObstruction_VIDs, whose
    // inner element the spec names "Element" - here the sibling
    // Obscured_VIDs's own confirmed inner name, "VID", is reused instead,
    // since VID_List and Obscured_VIDs are the same "VID" concept per the
    // spec's own text).
    std::vector<uint32_t> vidList;

    // Obscured_VIDs > Obscured_VID > VID: VIDs hidden while this item is
    // worn (the VID sibling of Obscured_slots).
    std::vector<uint32_t> obscuredVids;
};

// `Wear_Options > Wear_Option` (spec 2.1 "Wear options").
struct WearOption {
    std::optional<std::string> name;   // Name, e.g. "CUST_WEAR_NORMAL"

    // `disabled`: the spec lists this as a plain field alongside Name, with
    // no further shape given. INTERPRETATION: modeled as a child element
    // read the same way every other unmarked scalar here is (convention 1).
    Always<bool> disabled;

    // Active_Flags / Required_Flags / Incompatible_Flags: "a flag-based
    // prerequisite/incompatibility system ... the flag lists were empty in
    // every sample checked" (spec 2.1) - "empty" in the DLC-only sample the
    // spec itself drew from; real base-game data (fair game per this
    // project's rule 2 - see tools/validation/validate_customization_population.cpp's
    // G2) shows these are NOT always empty and confirms the inner element
    // name directly: each wrapper contains repeated singular-named children
    // - `Active_Flags > Active_Flag`, `Required_Flags > Required_Flag`,
    // `Incompatible_Flags > Incompatible_Flag` - the SAME
    // "plural wrapper > singular(wrapper)" pattern the spec's own text
    // already shows for `Obscured_slots > Obscured_slot` and
    // `Obscured_VIDs > Obscured_VID` just above, not the generic `<Flag>`
    // this reader first guessed (that guess is what real data disproved: 125 /
    // 64 / 54 real occurrences of `Active_Flag` / `Incompatible_Flag` /
    // `Required_Flag` respectively, 0 real `<Flag>` children under any of the
    // three). Stored as the raw flag TEXT, not a bitmask, since the spec
    // gives no bit-position table for these (semantics unconfirmed, section 4
    // item 5).
    std::vector<std::string> activeFlags;
    std::vector<std::string> requiredFlags;
    std::vector<std::string> incompatibleFlags;

    // Particle_Systems: named only as a category label by the spec, no inner
    // element name given. INTERPRETATION: repeated <Particle_System>
    // children under the <Particle_Systems> wrapper (matching the plural
    // wrapper / singular item naming convention seen everywhere else in this
    // project's tables, e.g. Obscured_slot/Obscured_VID above).
    std::vector<std::string> particleSystems;

    MeshInformation meshInformation;
};

// `Multi_Slot` (spec 2.1 "Slot span"): "some items occupy more than one
// customization slot simultaneously".
struct MultiSlot {
    bool present = false;
    std::optional<std::string> firstSlot;  // First_Slot
    std::optional<std::string> lastSlot;   // Last_Slot
};

// `Default_Colors_Grid > Default_Color > Clothing_Color` (spec 2.1 "Misc").
// Reused verbatim for a Variant's own Default_Colors_Grid (spec 2.1
// "Variants" row: the field is listed again there with the same name/shape).
struct DefaultColorsGrid {
    std::vector<std::string> clothingColors;  // Default_Color > Clothing_Color, in document order
};

// `Variants > Variant` (spec 2.1 "Variants (color/material options within
// one item)").
struct CustomizationVariant {
    std::optional<std::string> name;  // Name

    // Price / Respect_Bonus: no numeric width given by the spec.
    // INTERPRETATION (judgement call, same footing as sr3vehicleinfo's
    // Component_Density/Value/Props precedent): modeled as signed 32-bit,
    // matching Base_Price/Base_Respect_Bonus below (an item's top-level
    // economy fields, presumably the same underlying type as a variant's).
    Always<int32_t> price;         // Price
    Always<int32_t> respectBonus;  // Respect_Bonus

    std::optional<std::string> meshVariantName;  // Mesh_Variant_Info > Variant_Name

    // VariantID: spec section 5.1/5.3 (the custmesh_<N> recipe, CONFIRMED)
    // establishes this is read from Mesh_Variant_Info and is "essentially
    // always 1 in real data" - modeled as signed to match ParseInt32's
    // engine grammar (sr3xtbl.h), consistent with how the recipe treats it
    // ("ordinary 32-bit add, wraps").
    Always<int32_t> variantId;  // Mesh_Variant_Info > VariantID

    // Material_List > Material_Element > Material: INTERPRETATION of the
    // inner wrapper/item names, which the spec gives only as
    // "Material_List > Material_Element > Material" (this IS the literal
    // 3-level path the spec states, so no judgement call is needed here
    // beyond the usual "each Material_Element node's own Material child
    // holds the text").
    std::vector<std::string> materials;

    std::optional<std::string> shaderType;  // Shader_Type, e.g. "ir_sr3pccloth"

    DefaultColorsGrid defaultColorsGrid;  // Default_Colors_Grid (this Variant's own)
};

// One `<Customization_Item>` row (spec 2.1).
struct CustomizationItem {
    // --- Identity ---
    std::optional<std::string> name;  // Name
    // DisplayName: "a localization key string ... not literal display text"
    // (spec 2.1) - stored as the raw key text, never resolved (no string
    // table is in scope for this project).
    std::optional<std::string> displayName;

    // --- Wear options ---
    std::vector<WearOption> wearOptions;  // Wear_Options > Wear_Option

    // --- Categorization ---
    std::optional<std::string> editorCategory;  // _Editor > Category, e.g. "Entries:DLC Packs:Panda"
    std::vector<std::string> flags;               // Flags > Flag, e.g. "big hat"
    std::optional<std::string> slot;                // Slot, e.g. "headwear" (the customization-menu category)

    // --- Economy ---
    Always<int32_t> baseRespectBonus;  // Base_Respect_Bonus
    Always<int32_t> basePrice;         // Base_Price

    // --- Variants ---
    std::vector<CustomizationVariant> variants;  // Variants > Variant

    // --- Slot span ---
    MultiSlot multiSlot;  // Multi_Slot

    // --- Misc ---
    std::optional<std::string> framework;  // Framework, e.g. "dlc1"
    DefaultColorsGrid defaultColorsGrid;     // item-level Default_Colors_Grid > Default_Color > Clothing_Color
    Always<bool> isDlc;                        // Is_DLC
};

// Parses one `<Customization_Item>` element into a CustomizationItem. `row`
// must be that element (case-insensitive), directly under `<root><Table>`.
CustomizationItem ParseCustomizationItem(const Node* row);

// Convenience: parses every `<Customization_Item>` row of a whole
// `customization_items.xtbl`-shaped document, in file order.
std::vector<CustomizationItem> ParseAllCustomizationItems(const Document& doc);

// ---------------------------------------------------------------------------
// customization_outfits.xtbl (spec section 2.2)
// ---------------------------------------------------------------------------

// `Content_Grid > Content_Element` (spec 2.2).
struct ContentElement {
    // Item: "a Reference to customization_items.xtbl's Customization_Item.Name
    // ... confirmed via the file's own inline <TableDescription>" (spec 2.2).
    // Per spec-xtbl-format.md 3.2, a <Reference> is purely editor-facing
    // TABLE metadata (inside <TableDescription>, which "the row readers
    // never visit" - sr3xtbl.h) - the ROW's own <Item> element is plain text
    // (the referenced item's Name), stored here as-is with no resolution
    // attempted (resolving it needs the whole loaded item table, out of
    // scope for a single-row parser - same footing as
    // sr3vehicleinfo::VehicleEntry::group).
    std::optional<std::string> item;

    // Default_Wear_Option: "references the item's
    // Wear_Option.Mesh_Information...Filename" (spec 2.2) - again plain
    // text, not resolved here.
    std::optional<std::string> defaultWearOption;

    // Primary_Color/Secondary_Color/Tertiary_Color: "preset color-slot
    // values, only present on items that use them" (spec 2.2).
    std::optional<std::string> primaryColor;
    std::optional<std::string> secondaryColor;
    std::optional<std::string> tertiaryColor;
};

// `Styles_Grid > Style_Element` (spec 2.2).
struct StyleElement {
    std::optional<std::string> displayName;  // Display_Name (localization key, same convention as CustomizationItem::displayName)

    // Variant_Grid > Variant_Element > Variant: "selects a specific
    // Mesh_Variant_Info.Variant_Name per item" (spec 2.2) - flattened to the
    // Variant text values in document order (the Variant_Element wrapper
    // itself carries no other field the spec documents).
    std::vector<std::string> variantGrid;
};

// One `<Outfit>` row (spec 2.2): bundles several Customization_Item
// references together with preset colors - a selectable "preset outfit".
struct Outfit {
    std::optional<std::string> name;         // Name
    std::optional<std::string> displayName;  // Display_Name (localization key)

    std::vector<ContentElement> contentGrid;  // Content_Grid > Content_Element
    std::vector<StyleElement> stylesGrid;     // Styles_Grid > Style_Element

    std::optional<std::string> editorCategory;  // _Editor > Category
    std::optional<std::string> framework;        // Framework
    Always<bool> isDlc;                            // Is_DLC
};

// Parses one `<Outfit>` element into an Outfit. `row` must be that element
// (case-insensitive), directly under `<root><Table>`.
Outfit ParseOutfit(const Node* row);

// Convenience: parses every `<Outfit>` row of a whole
// `customization_outfits.xtbl`-shaped document, in file order.
std::vector<Outfit> ParseAllOutfits(const Document& doc);

}  // namespace sr3customization
