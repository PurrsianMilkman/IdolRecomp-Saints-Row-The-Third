// Parse functions for sr3customization (spec-customization-data.md sections
// 2 and 3). Built entirely on sr3xtbl's accessors (include/sr3xtbl/xtbl.h).
// See include/sr3customization/customization.h and vehicle_customization.h
// for the shared conventions this file follows (Always<T> vs optional<T>,
// what is a documented judgement call, what is deliberately OPEN/skipped).

#include "sr3customization/customization.h"
#include "sr3customization/vehicle_customization.h"

namespace sr3customization {

using namespace sr3xtbl;

namespace {

// ---------------------------------------------------------------------------
// Generic helpers (mirrors sr3vehicleinfo/vehicle_entry.cpp's style).
// ---------------------------------------------------------------------------
std::optional<std::string> OptText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

// A flat list of child elements each named `childName`, taking each such
// child's OWN text (e.g. `<VID_List><VID>7</VID><VID>9</VID></VID_List>`).
std::vector<uint32_t> ReadUInt32ChildList(const Node* wrap, std::string_view childName) {
    std::vector<uint32_t> out;
    for (const Node* e = FindChild(wrap, childName); e; e = NextSibling(wrap, e, childName))
        out.push_back(GetUInt32(e).value_or(0));
    return out;
}

// A list of wrapper items, each holding the real value in a nested leaf
// child (e.g. `Obscured_slots > Obscured_slot > Slot`: itemName =
// "Obscured_slot", leafName = "Slot").
std::vector<std::string> ReadNestedTextList(const Node* wrap, std::string_view itemName, std::string_view leafName) {
    std::vector<std::string> out;
    for (const Node* e = FindChild(wrap, itemName); e; e = NextSibling(wrap, e, itemName)) {
        if (const std::string* t = ChildText(e, leafName)) out.push_back(*t);
    }
    return out;
}
std::vector<uint32_t> ReadNestedUInt32List(const Node* wrap, std::string_view itemName, std::string_view leafName) {
    std::vector<uint32_t> out;
    for (const Node* e = FindChild(wrap, itemName); e; e = NextSibling(wrap, e, itemName))
        out.push_back(GetUInt32(e, leafName).value_or(0));
    return out;
}

// A flat list of child elements each named `childName`, taking each such
// child's own text directly (e.g. `<Flags><Flag>big hat</Flag></Flags>`).
std::vector<std::string> ReadTextChildList(const Node* wrap, std::string_view childName) {
    std::vector<std::string> out;
    for (const Node* e = FindChild(wrap, childName); e; e = NextSibling(wrap, e, childName)) {
        if (e->text()) out.push_back(*e->text());
    }
    return out;
}

DefaultColorsGrid ParseDefaultColorsGrid(const Node* parent) {
    DefaultColorsGrid r;
    const Node* wrap = FindChild(parent, "Default_Colors_Grid");
    r.clothingColors = ReadNestedTextList(wrap, "Default_Color", "Clothing_Color");
    return r;
}

// ---------------------------------------------------------------------------
// customization_items.xtbl (spec 2.1)
// ---------------------------------------------------------------------------

MeshInformation ParseMeshInformation(const Node* wearOptionNode) {
    MeshInformation r;
    const Node* n = FindChild(wearOptionNode, "Mesh_Information");
    r.maleMeshFilename = OptText(FindChild(n, "Male_Mesh_Filename"), "Filename");
    r.femaleMeshFilename = OptText(FindChild(n, "Female_Mesh_Filename"), "Filename");
    r.cutsceneOnly = ReadBoolAlways(n, "Cutscene_Only");
    r.obscuredSlots = ReadNestedTextList(FindChild(n, "Obscured_slots"), "Obscured_slot", "Slot");
    r.vidList = ReadUInt32ChildList(FindChild(n, "VID_List"), "VID");
    r.obscuredVids = ReadNestedUInt32List(FindChild(n, "Obscured_VIDs"), "Obscured_VID", "VID");
    return r;
}

WearOption ParseWearOption(const Node* n) {
    WearOption r;
    r.name = OptText(n, "Name");
    r.disabled = ReadBoolAlways(n, "disabled");
    // Real base-game data confirms the singular-wrapper-name pattern (see
    // customization.h's field comment) - NOT a generic <Flag> child.
    r.activeFlags = ReadTextChildList(FindChild(n, "Active_Flags"), "Active_Flag");
    r.requiredFlags = ReadTextChildList(FindChild(n, "Required_Flags"), "Required_Flag");
    r.incompatibleFlags = ReadTextChildList(FindChild(n, "Incompatible_Flags"), "Incompatible_Flag");
    r.particleSystems = ReadTextChildList(FindChild(n, "Particle_Systems"), "Particle_System");
    r.meshInformation = ParseMeshInformation(n);
    return r;
}

std::vector<WearOption> ParseWearOptions(const Node* row) {
    std::vector<WearOption> out;
    const Node* wrap = FindChild(row, "Wear_Options");
    for (const Node* e = FindChild(wrap, "Wear_Option"); e; e = NextSibling(wrap, e, "Wear_Option"))
        out.push_back(ParseWearOption(e));
    return out;
}

MultiSlot ParseMultiSlot(const Node* row) {
    MultiSlot r;
    const Node* n = FindChild(row, "Multi_Slot");
    r.present = (n != nullptr);
    r.firstSlot = OptText(n, "First_Slot");
    r.lastSlot = OptText(n, "Last_Slot");
    return r;
}

CustomizationVariant ParseVariant(const Node* n) {
    CustomizationVariant r;
    r.name = OptText(n, "Name");
    r.price = ReadInt32Always(n, "Price");
    r.respectBonus = ReadInt32Always(n, "Respect_Bonus");
    const Node* mvi = FindChild(n, "Mesh_Variant_Info");
    r.meshVariantName = OptText(mvi, "Variant_Name");
    r.variantId = ReadInt32Always(mvi, "VariantID");
    r.materials = ReadNestedTextList(FindChild(n, "Material_List"), "Material_Element", "Material");
    // LABEL: spec-customization-data.md §2.1 (Variants row) "[OPEN - desk review 2026-09-30: the parent of
    // `Shader_Type` is ambiguous in this cell (a `Variant` child, or a `Material_Element` sibling of ...)]";
    // spec-tables-customization.md §4.3 marks the same parent [OPEN]. This reader takes it as a Variant child;
    // sr3tables_customization (tables_customization_items.cpp ParseMaterialElement) reads it under
    // Material_Element. The two sites are inconsistent by design until the parent is settled; no behaviour change.
    r.shaderType = OptText(n, "Shader_Type");
    r.defaultColorsGrid = ParseDefaultColorsGrid(n);
    return r;
}

std::vector<CustomizationVariant> ParseVariants(const Node* row) {
    std::vector<CustomizationVariant> out;
    const Node* wrap = FindChild(row, "Variants");
    for (const Node* e = FindChild(wrap, "Variant"); e; e = NextSibling(wrap, e, "Variant"))
        out.push_back(ParseVariant(e));
    return out;
}

}  // namespace

CustomizationItem ParseCustomizationItem(const Node* row) {
    CustomizationItem r;
    r.name = OptText(row, "Name");
    r.displayName = OptText(row, "DisplayName");

    r.wearOptions = ParseWearOptions(row);

    const Node* editor = FindChild(row, "_Editor");
    r.editorCategory = OptText(editor, "Category");
    r.flags = ReadTextChildList(FindChild(row, "Flags"), "Flag");
    r.slot = OptText(row, "Slot");

    r.baseRespectBonus = ReadInt32Always(row, "Base_Respect_Bonus");
    r.basePrice = ReadInt32Always(row, "Base_Price");

    r.variants = ParseVariants(row);

    r.multiSlot = ParseMultiSlot(row);

    r.framework = OptText(row, "Framework");
    r.defaultColorsGrid = ParseDefaultColorsGrid(row);
    r.isDlc = ReadBoolAlways(row, "Is_DLC");

    return r;
}

std::vector<CustomizationItem> ParseAllCustomizationItems(const Document& doc) {
    std::vector<CustomizationItem> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Customization_Item"); row; row = NextSibling(table, row, "Customization_Item"))
        out.push_back(ParseCustomizationItem(row));
    return out;
}

// ---------------------------------------------------------------------------
// customization_outfits.xtbl (spec 2.2)
// ---------------------------------------------------------------------------

namespace {

ContentElement ParseContentElement(const Node* n) {
    ContentElement r;
    r.item = OptText(n, "Item");
    r.defaultWearOption = OptText(n, "Default_Wear_Option");
    r.primaryColor = OptText(n, "Primary_Color");
    r.secondaryColor = OptText(n, "Secondary_Color");
    r.tertiaryColor = OptText(n, "Tertiary_Color");
    return r;
}

std::vector<ContentElement> ParseContentGrid(const Node* row) {
    std::vector<ContentElement> out;
    const Node* wrap = FindChild(row, "Content_Grid");
    for (const Node* e = FindChild(wrap, "Content_Element"); e; e = NextSibling(wrap, e, "Content_Element"))
        out.push_back(ParseContentElement(e));
    return out;
}

StyleElement ParseStyleElement(const Node* n) {
    StyleElement r;
    r.displayName = OptText(n, "Display_Name");
    r.variantGrid = ReadNestedTextList(FindChild(n, "Variant_Grid"), "Variant_Element", "Variant");
    return r;
}

std::vector<StyleElement> ParseStylesGrid(const Node* row) {
    std::vector<StyleElement> out;
    const Node* wrap = FindChild(row, "Styles_Grid");
    for (const Node* e = FindChild(wrap, "Style_Element"); e; e = NextSibling(wrap, e, "Style_Element"))
        out.push_back(ParseStyleElement(e));
    return out;
}

}  // namespace

Outfit ParseOutfit(const Node* row) {
    Outfit r;
    r.name = OptText(row, "Name");
    r.displayName = OptText(row, "Display_Name");

    r.contentGrid = ParseContentGrid(row);
    r.stylesGrid = ParseStylesGrid(row);

    const Node* editor = FindChild(row, "_Editor");
    r.editorCategory = OptText(editor, "Category");
    r.framework = OptText(row, "Framework");
    r.isDlc = ReadBoolAlways(row, "Is_DLC");

    return r;
}

std::vector<Outfit> ParseAllOutfits(const Document& doc) {
    std::vector<Outfit> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Outfit"); row; row = NextSibling(table, row, "Outfit"))
        out.push_back(ParseOutfit(row));
    return out;
}

// ---------------------------------------------------------------------------
// Vehicle customization variant file, <vehicle_name>.xtbl (spec section 3)
// ---------------------------------------------------------------------------

namespace {

ComponentElement ParseComponentElement(const Node* n) {
    ComponentElement r;
    r.slot = OptText(n, "Slot");
    r.component = OptText(n, "Component");
    return r;
}

ExternalizedComponent ParseExternalizedComponent(const Node* n) {
    ExternalizedComponent r;
    r.component = OptText(n, "Component");
    return r;
}

ComponentChance ParseComponentChance(const Node* n) {
    ComponentChance r;
    r.name = OptText(n, "Name");
    r.weight = ReadFloatAlways(n, "Weight");
    const Node* comps = FindChild(n, "Components");
    for (const Node* e = FindChild(comps, "Component_Element"); e; e = NextSibling(comps, e, "Component_Element"))
        r.components.push_back(ParseComponentElement(e));
    const Node* ext = FindChild(n, "Externalized_Components");
    for (const Node* e = FindChild(ext, "Externalized_Component"); e; e = NextSibling(ext, e, "Externalized_Component"))
        r.externalizedComponents.push_back(ParseExternalizedComponent(e));
    return r;
}

ComponentGroup ParseComponentGroup(const Node* n) {
    ComponentGroup r;
    r.name = OptText(n, "Name");
    const Node* wrap = FindChild(n, "Component_Chances");
    for (const Node* e = FindChild(wrap, "Component_Chance"); e; e = NextSibling(wrap, e, "Component_Chance"))
        r.componentChances.push_back(ParseComponentChance(e));
    return r;
}

std::vector<ComponentGroup> ParseComponentGroups(const Node* parent) {
    std::vector<ComponentGroup> out;
    const Node* wrap = FindChild(parent, "Components");
    for (const Node* e = FindChild(wrap, "Component_Group"); e; e = NextSibling(wrap, e, "Component_Group"))
        out.push_back(ParseComponentGroup(e));
    return out;
}

// The `Color` ComboElement - see vehicle_customization.h's JUDGEMENT CALL
// banner: `Color` is treated as a literal wrapper child of Color_Choice,
// itself holding either a `Color_Pool` or a `Color_Set` child (Color_Pool
// tried first).
ColorRef ParseColorRef(const Node* colorChoiceNode) {
    ColorRef r;
    const Node* colorNode = FindChild(colorChoiceNode, "Color");
    if (const std::string* pool = ChildText(colorNode, "Color_Pool")) {
        r.kind = ColorRefKind::ColorPool;
        r.value = *pool;
        return r;
    }
    if (const std::string* set = ChildText(colorNode, "Color_Set")) {
        r.kind = ColorRefKind::ColorSet;
        r.value = *set;
        return r;
    }
    return r;  // None: neither present, or <Color> itself absent
}

ColorChoice ParseColorChoice(const Node* n) {
    ColorChoice r;
    r.slot = OptText(n, "Slot");
    r.component = OptText(n, "Component");
    r.colorSlot = OptText(n, "Color_Slot");
    r.color = ParseColorRef(n);
    return r;
}

ColorChance ParseColorChance(const Node* n) {
    ColorChance r;
    r.name = OptText(n, "Name");
    r.weight = ReadFloatAlways(n, "Weight");
    const Node* wrap = FindChild(n, "Color_Choices");
    for (const Node* e = FindChild(wrap, "Color_Choice"); e; e = NextSibling(wrap, e, "Color_Choice"))
        r.colorChoices.push_back(ParseColorChoice(e));
    return r;
}

ColorGroup ParseColorGroup(const Node* n) {
    ColorGroup r;
    r.name = OptText(n, "Name");
    const Node* wrap = FindChild(n, "Color_Chances");
    for (const Node* e = FindChild(wrap, "Color_Chance"); e; e = NextSibling(wrap, e, "Color_Chance"))
        r.colorChances.push_back(ParseColorChance(e));
    return r;
}

std::vector<ColorGroup> ParseColorGroups(const Node* parent) {
    std::vector<ColorGroup> out;
    const Node* wrap = FindChild(parent, "Colors");
    for (const Node* e = FindChild(wrap, "Color_Group"); e; e = NextSibling(wrap, e, "Color_Group"))
        out.push_back(ParseColorGroup(e));
    return out;
}

WheelGroup ParseWheelGroup(const Node* n) {
    WheelGroup r;
    r.name = OptText(n, "Name");
    r.frontWidth = ReadFloatAlways(n, "Front_Width");
    r.frontSize = ReadFloatAlways(n, "Front_Size");
    r.rearWidth = ReadFloatAlways(n, "Rear_Width");
    r.rearSize = ReadFloatAlways(n, "Rear_Size");
    r.weight = ReadFloatAlways(n, "Weight");
    return r;
}

std::vector<WheelGroup> ParseWheelGroups(const Node* parent) {
    std::vector<WheelGroup> out;
    const Node* wrap = FindChild(parent, "Wheels");
    for (const Node* e = FindChild(wrap, "Wheel_Group"); e; e = NextSibling(wrap, e, "Wheel_Group"))
        out.push_back(ParseWheelGroup(e));
    return out;
}

// Components / Colors / Wheels are PER-VARIANT (see vehicle_customization.h's
// REAL-DATA CORRECTION banner: confirmed children of `<Variant>`, not of
// `<Vehicle>`, against the real `sp_mdsphere_genki.xtbl` sample) - so `n`
// here is the `<Variant>` node itself, not the vehicle row.
VehicleVariant ParseVehicleVariant(const Node* n) {
    VehicleVariant r;
    r.fileId = ReadInt32Always(n, "File_Id");
    r.name = OptText(n, "Name");
    r.weight = ReadFloatAlways(n, "Weight");
    r.parkingWeight = ReadFloatAlways(n, "ParkingWeight");
    r.type = EnumIndex(FindChild(n, "Type"), kVehicleVariantTypeNames, 5);
    r.siren = ReadBoolAlways(n, "Siren");
    r.fullyCustomizable = ReadBoolAlways(n, "Fully_Customizable");
    r.hasPeg = ReadBoolAlways(n, "Has_Peg");
    r.bitmap = OptText(n, "Bitmap");
    r.componentGroups = ParseComponentGroups(n);
    r.colorGroups = ParseColorGroups(n);
    r.wheelGroups = ParseWheelGroups(n);
    return r;
}

std::vector<VehicleVariant> ParseVehicleVariants(const Node* vehicleRow) {
    std::vector<VehicleVariant> out;
    const Node* wrap = FindChild(vehicleRow, "Variants");
    for (const Node* e = FindChild(wrap, "Variant"); e; e = NextSibling(wrap, e, "Variant"))
        out.push_back(ParseVehicleVariant(e));
    return out;
}

}  // namespace

VehicleCustVariants ParseVehicleCustVariants(const Node* row) {
    VehicleCustVariants r;
    r.vehicleName = OptText(row, "Name");
    r.variants = ParseVehicleVariants(row);
    return r;
}

std::vector<VehicleCustVariants> ParseAllVehicleCustVariants(const Document& doc) {
    std::vector<VehicleCustVariants> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Vehicle"); row; row = NextSibling(table, row, "Vehicle"))
        out.push_back(ParseVehicleCustVariants(row));
    return out;
}

}  // namespace sr3customization
