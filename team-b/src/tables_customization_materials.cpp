// Parse functions for sr3tables_customization/materials.h (spec-tables-
// customization.md §8, §9.1, §9.2, §9.3). Built ONLY on sr3xtbl's
// accessors; nothing here touches the game executable, disassembly or
// decompiled code.

#include "sr3tables_customization/materials.h"

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

std::vector<ColorPoolOverrideEntry> ReadColorGrid(const Node* wrap) {
    std::vector<ColorPoolOverrideEntry> out;
    for (const Node* e = FindChild(wrap, "Color_Element"); e; e = NextSibling(wrap, e, "Color_Element")) {
        ColorPoolOverrideEntry c;
        c.color = OptText(e, "Color");
        out.push_back(c);
    }
    return out;
}

}  // namespace

// ===========================================================================
// 8. customization_default_items.xtbl
// ===========================================================================
DefaultEntry ParseDefaultEntry(const Node* row) {
    DefaultEntry d;
    d.item = OptText(row, "Item");
    d.wearOption = OptText(row, "Wear_Option");
    d.variant = OptText(row, "Variant");
    d.gender = OptText(row, "Gender");
    d.race = OptText(row, "Race");
    d.raceIndex = MatchName(d.race, kDefaultItemRaceNames.data(), kDefaultItemRaceNames.size());

    const Node* colorPool = FindChild(row, "Color_Pool");
    d.colorPoolPresent = colorPool != nullptr;
    d.characterColorGrid = ReadColorGrid(FindChild(colorPool, "Character_Color_Grid"));
    d.makeupColorGrid = ReadColorGrid(FindChild(colorPool, "Makeup_Color_Grid"));
    return d;
}

std::vector<DefaultEntry> ParseCustomizationDefaultItemsTable(const Document& doc) {
    std::vector<DefaultEntry> out;
    const Node* defaultsList = FindChild(FindChild(doc.table(), "Defaults"), "Defaults_List");
    for (const Node* row = FindChild(defaultsList, "Default"); row; row = NextSibling(defaultsList, row, "Default"))
        out.push_back(ParseDefaultEntry(row));
    return out;
}

// ===========================================================================
// 9.1 customization_materials.xtbl
// ===========================================================================
CustMaterial ParseCustMaterial(const Node* row) {
    CustMaterial m;
    m.name = OptText(row, "Name");
    const Node* wrap = FindChild(row, "Variables");
    for (const Node* v = FindChild(wrap, "Variable"); v; v = NextSibling(wrap, v, "Variable")) {
        MaterialVariable var;
        var.shaderVarName = OptText(v, "Shader_Var_Name");
        var.displayNameGame = OptText(v, "Display_Name_Game");
        m.variables.push_back(var);
    }
    return m;
}

std::vector<CustMaterial> ParseCustomizationMaterialsTable(const Document& doc) {
    std::vector<CustMaterial> out;
    for (const Node* row : Children(doc.table(), "Cust_Material")) out.push_back(ParseCustMaterial(row));
    return out;
}

// ===========================================================================
// 9.2 customization_normals.xtbl
// ===========================================================================
Normals ParseNormals(const Node* row) {
    Normals n;
    n.race = OptText(row, "Race");
    const int idx = MatchName(n.race, kNormalsRaceNames.data(), kNormalsRaceNames.size());
    n.raceIndex = idx >= 0 ? idx : 3;  // §9.2: "default index 3 if unmatched"
    n.female = ReadBoolAlways(row, "Female");
    n.ideal = OptText(row, "Ideal");
    n.muscular = OptText(row, "Muscular");
    n.skinny = OptText(row, "Skinny");
    n.fat = OptText(row, "Fat");
    n.age = OptText(row, "Age");
    return n;
}

std::vector<Normals> ParseCustomizationNormalsTable(const Document& doc) {
    std::vector<Normals> out;
    const Node* wrap = doc.table();
    for (const Node* row = FindChild(wrap, "Normals"); row; row = NextSibling(wrap, row, "Normals")) {
        // CONFIRMED 2026-10-02 (re-derived from the executable, `FUN_009fcc00` read in full): the check
        // `if (7 < count) stop` runs at the TOP of the loop against the not-yet-incremented count - keeps a
        // true maximum of exactly 8, matching the `>= 8` already coded here. THIS IS THE ONE CAP THAT CAN CHANGE
        // A RESULT ON REAL DATA: the shipped file has exactly 8 Normals rows, i.e. it sits exactly at the cap -
        // now confirmed safe (not an 8-vs-9 ambiguity). Every other cap in this reader is well above its real
        // row count.
        if (out.size() >= 8) break;  // hard cap (§9.2)
        out.push_back(ParseNormals(row));
    }
    return out;
}

// ===========================================================================
// 9.3 customization_compositing.xtbl
// ===========================================================================
CompositeLayer ParseCompositeLayer(const Node* row) {
    CompositeLayer c;
    c.name = OptText(row, "Name");
    c.displayName = OptText(row, "Display_Name");
    c.diffuseTexture = OptText(row, "diffuse");
    c.normalTexture = OptText(row, "normal");
    c.slot = OptText(row, "Slot");
    c.xCoord = ReadInt32Always(row, "x_coord");
    c.yCoord = ReadInt32Always(row, "y_coord");
    c.alpha = ReadFloatAlways(row, "Alpha");
    c.layer = ReadInt32Always(row, "Layer");
    const Node* flagsNode = FindChild(row, "Flags");
    c.colorizable = HasFlag(flagsNode, "Colorizable");
    c.playerCreation = HasFlag(flagsNode, "Player_creation");
    c.showAlphaSelection = HasFlag(flagsNode, "Show_alpha_selection");
    c.noDiffuse = HasFlag(flagsNode, "No_diffuse");
    c.price = ReadInt32Always(row, "Price");
    c.isDlc = ReadBoolAlways(row, "Is_DLC");
    return c;
}

std::vector<CompositeLayer> ParseCustomizationCompositingTable(const Document& doc) {
    std::vector<CompositeLayer> out;
    const Node* wrap = doc.table();
    for (const Node* row = FindChild(wrap, "Composite_Layer"); row; row = NextSibling(wrap, row, "Composite_Layer")) {
        // CONFIRMED 2026-10-02 (re-derived from the executable): same pre-increment `<` pattern as every other
        // cap in this document - `if (599 < count) stop` runs before the row is stored, keeping a true maximum
        // of 600, matching the `>= 600` already coded here. Real data has 374 rows, so this operator cannot
        // change a result on real data.
        if (out.size() >= 600) break;  // hard cap (§9.3)
        out.push_back(ParseCompositeLayer(row));
    }
    return out;
}

}  // namespace sr3tables_customization
