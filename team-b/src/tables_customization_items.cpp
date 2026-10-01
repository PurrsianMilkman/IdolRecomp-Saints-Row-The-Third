// Parse functions for sr3tables_customization/items.h (spec-tables-
// customization.md §4, §5.2). Built ONLY on sr3xtbl's accessors; nothing
// here touches the game executable, disassembly or decompiled code.

#include "sr3tables_customization/items.h"

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

// Flag name references (Active_Flag/Required_Flag/Incompatible_Flag): the
// element's OWN text is the flag name, a "Comparison" child holds yes/no
// (items.h's WearOptionFlagRef banner).
std::vector<WearOptionFlagRef> ReadFlagRefList(const Node* wrap, std::string_view itemName) {
    std::vector<WearOptionFlagRef> out;
    for (const Node* e = FindChild(wrap, itemName); e; e = NextSibling(wrap, e, itemName)) {
        WearOptionFlagRef r;
        if (e->text()) r.name = *e->text();
        r.comparison = GetBool(e, "Comparison");
        out.push_back(r);
    }
    return out;
}

MaterialElementEntry ParseMaterialElement(const Node* row) {
    MaterialElementEntry m;
    m.material = OptText(row, "Material");
    // LABEL: spec-tables-customization.md §4.3, [OPEN - desk review 2026-09-30]: whether Shader_Type is a
    // child of Material_Element or of Variant is unsettled (spec-customization-data.md §2.1 lists it at
    // variant level; conflict, see its §2.1 Variants row "[OPEN - desk review 2026-09-30: the parent of
    // `Shader_Type` is ambiguous ...]"). Read under Material_Element here as our assumption; the other site is
    // src/customization.cpp ParseVariant (reads it as a Variant child). No behaviour change.
    m.shaderType = OptText(row, "Shader_Type");
    return m;
}

VariantEntry ParseVariant(const Node* row) {
    VariantEntry v;
    v.name = OptText(row, "Name");
    v.price = GetInt32(row, "Price");
    v.respectBonus = GetInt32(row, "Respect_Bonus");
    const Node* meshInfo = FindChild(row, "Mesh_Variant_Info");
    v.meshVariantName = OptText(meshInfo, "Variant_Name");
    v.variantId = ReadInt32Always(meshInfo, "VariantID");
    const Node* matList = FindChild(row, "Material_List");
    for (const Node* e = FindChild(matList, "Material_Element"); e; e = NextSibling(matList, e, "Material_Element"))
        v.materialList.push_back(ParseMaterialElement(e));
    return v;
}

WearOptionEntry ParseWearOptionAccepted(const Node* row) {
    WearOptionEntry w;
    w.name = OptText(row, "Name");
    const Node* meshInformation = FindChild(row, "Mesh_Information");
    // LABEL: spec-tables-customization.md §4.2, [OPEN - desk review 2026-09-30]: that section names only
    // Male_Mesh_Filename, while spec-customization-data.md §2.1/§5.2 also documents Female_Mesh_Filename >
    // Filename (it drives the custmesh_<N>f bundle). Only the male name is read here pending the executable
    // check; no behaviour change. Also: spec-tables-customization.md §4.2 review status "NEEDS-EXE: ...
    // `Female_Mesh_Filename` handling". spec-customization-data.md §2.1 lists the element under a
    // "[CONFIRMED - empirical]" scope that is narrowed to "the elements exist with the names shown" (one
    // sample row) and §5.2 says the composer registers custmesh_<N>f "only if the wear option has a
    // <Female_Mesh_Filename>"; neither says the 0x28 wear-option record loader (FUN_00829650) reads it, so it
    // is NOT added to this typed reader (sr3customization::MeshInformation, a different layer, does read it).
    w.maleMeshFilename = OptText(FindChild(meshInformation, "Male_Mesh_Filename"), "Filename");
    w.activeFlags = ReadFlagRefList(FindChild(row, "Active_Flags"), "Active_Flag");
    w.requiredFlags = ReadFlagRefList(FindChild(row, "Required_Flags"), "Required_Flag");
    w.incompatibleFlags = ReadFlagRefList(FindChild(row, "Incompatible_Flags"), "Incompatible_Flag");
    return w;
}

std::vector<WearOptionEntry> ParseWearOptionsList(const Node* itemRow) {
    std::vector<WearOptionEntry> out;
    const Node* wrap = FindChild(itemRow, "Wear_Options");
    for (const Node* e = FindChild(wrap, "Wear_Option"); e; e = NextSibling(wrap, e, "Wear_Option")) {
        // "A Wear_Option with Disabled=yes is dropped entirely" (§4.2).
        if (GetBool(e, "Disabled").value_or(false)) continue;
        out.push_back(ParseWearOptionAccepted(e));
    }
    return out;
}

std::vector<DefaultColorEntry> ParseDefaultColorsGrid(const Node* itemRow) {
    std::vector<DefaultColorEntry> out;
    const Node* wrap = FindChild(itemRow, "Default_Colors_Grid");
    for (const Node* e = FindChild(wrap, "Default_Color"); e; e = NextSibling(wrap, e, "Default_Color")) {
        DefaultColorEntry d;
        d.clothingColor = OptText(e, "Clothing_Color");
        d.tattooColor = OptText(e, "Tattoo_Color");
        d.makeupColor = OptText(e, "Makeup_Color");
        out.push_back(d);
    }
    return out;
}

}  // namespace

// ===========================================================================
// 4. customization_items.xtbl
// ===========================================================================
CustomizationItemEntry ParseCustomizationItem(const Node* row) {
    CustomizationItemEntry e;
    const Node* flagsNode = FindChild(row, "Flags");
    e.notReady = HasFlag(flagsNode, "not ready");
    e.bigHat = HasFlag(flagsNode, "big hat");
    e.variantsAreSameItem = HasFlag(flagsNode, "variants are same item");
    e.npcOnly = HasFlag(flagsNode, "npc only");

    e.slot = OptText(row, "Slot");
    e.slotIndex = MatchName(e.slot, kCanonicalSlotNames.data(), kCanonicalSlotNames.size());

    e.basePrice = ReadInt32Always(row, "Base_Price");
    e.baseRespectBonus = ReadInt32Always(row, "Base_Respect_Bonus");
    e.brand = OptText(row, "Brand");

    e.name = OptText(row, "Name");
    e.isDlc = ReadBoolAlways(row, "Is_DLC");
    e.displayName = OptText(row, "DisplayName");

    e.wearOptions = ParseWearOptionsList(row);
    const Node* variantsWrap = FindChild(row, "Variants");
    for (const Node* v = FindChild(variantsWrap, "Variant"); v; v = NextSibling(variantsWrap, v, "Variant"))
        e.variants.push_back(ParseVariant(v));
    e.defaultColorsGrid = ParseDefaultColorsGrid(row);

    e.heelAngle = GetFloat(row, "Heel_Angle");
    e.heelHeight = GetFloat(row, "Heel_Height");
    e.shoeAudioSwitch = OptText(row, "ShoeAudioSwitch");
    e.clothingAudioSwitch = OptText(row, "ClothingAudioSwitch");
    e.fatBoneArm = GetFloat(row, "Fat_Bone_Arm");
    e.fatBoneLeg = GetFloat(row, "Fat_Bone_Leg");

    e.style = OptText(row, "Style");
    e.styleIndex = MatchName(e.style, kStyleNames.data(), kStyleNames.size());

    return e;
}

std::vector<CustomizationItemEntry> ParseCustomizationItemsTable(const Document& doc) {
    std::vector<CustomizationItemEntry> out;
    const Node* wrap = doc.table();
    for (const Node* row = FindChild(wrap, "Customization_Item"); row; row = NextSibling(wrap, row, "Customization_Item")) {
        // "not ready (bit 0 - row is entirely skipped ... without allocating
        // a slot)" (§4.1).
        if (HasFlag(FindChild(row, "Flags"), "not ready")) continue;
        // Hard cap 858 rows (0x35a, §4.1).
        // LABEL: spec-tables-customization.md §4.1, [OPEN - desk review 2026-09-30]: whether the engine's
        // counter test runs before or after a row is stored (858 vs 859 kept) is not settled; the `>=`
        // here is our assumption, not a spec fact. Real data has 574 rows (§21), so this operator cannot
        // change a result on real data.
        if (out.size() >= 858) break;
        out.push_back(ParseCustomizationItem(row));
    }
    return out;
}

// ===========================================================================
// 5.2 customization_stores.xtbl
// ===========================================================================
namespace {

StoreItemEntry ParseStoreItem(const Node* row) {
    StoreItemEntry s;
    if (row->text()) s.itemName = *row->text();
    const Node* variantsWrap = FindChild(row, "Variants");
    s.variantsPresent = variantsWrap != nullptr;
    for (const Node* e = FindChild(variantsWrap, "Item_Variant"); e; e = NextSibling(variantsWrap, e, "Item_Variant"))
        if (e->text()) s.itemVariants.push_back(*e->text());
    return s;
}

}  // namespace

Store ParseStore(const Node* row) {
    Store s;
    s.name = OptText(row, "Name");
    s.allowWardrobe = HasFlag(FindChild(row, "Flags"), "allow_wardrobe");
    for (const Node* e = FindChild(row, "Store_Item"); e; e = NextSibling(row, e, "Store_Item")) {
        // LABEL: spec-tables-customization.md §5.2, [OPEN - desk review 2026-09-30]: decimal 255 and hex 0xfe
        // (= 254) disagree and no compare operator is given, so 254 vs 255 is unsettled; `>= 255` is our
        // assumption. Real stores are never near the cap (§5.2 validation), so it cannot change a real result.
        if (s.storeItems.size() >= 255) break;  // cap 0xfe (§5.2)
        s.storeItems.push_back(ParseStoreItem(e));
    }
    const Node* outfitsWrap = FindChild(row, "Outfits");
    for (const Node* e = FindChild(outfitsWrap, "Outfit_Element"); e; e = NextSibling(outfitsWrap, e, "Outfit_Element")) {
        // LABEL: spec-tables-customization.md §5.2, [OPEN - desk review 2026-09-30]: no compare operator is
        // given for the 73 cap; `>= 73` is our assumption. Not near the cap in real data (§5.2 validation).
        if (s.outfitElements.size() >= 73) break;  // cap 0x48+1 (§5.2)
        if (e->text()) s.outfitElements.push_back(*e->text());
    }
    return s;
}

std::vector<Store> ParseCustomizationStoresTable(const Document& doc) {
    std::vector<Store> out;
    for (const Node* row : Children(doc.table(), "Store")) out.push_back(ParseStore(row));
    return out;
}

}  // namespace sr3tables_customization
