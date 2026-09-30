// Synthetic tests for sr3customization (spec-customization-data.md sections
// 2 and 3). Every fixture below is a hand-built XML string transcribed from
// the SPEC TEXT (section cited per test, including the spec's own worked
// examples: `cm_dlc_mask_panda` section 2.1, `Panda Outfit` section 2.2,
// `sp_mdsphere_genki` section 3 - field VALUES not given verbatim by the
// spec's prose, e.g. exact price numbers, are plausible synthetic filler,
// but every ELEMENT NAME and NESTING used below is the spec's own) - a pass
// here proves the reader implements what the spec says, not that the spec
// is right. The real-data cross-check is
// tools/validation/validate_customization_population.cpp.
//
// Style follows this project's synthetic-suite convention (see
// tests/synthetic_vehicleinfo_test.cpp): a tiny hand-rolled CHECK macro, a
// running pass count, "<N> tests passed." on the last line.
//
// Mutation-check note (this project's process, measurement-discipline.md):
// at least 3 of the CHECKs below were confirmed to actually catch a broken
// reader by hand, before this file was finalised (each done by temporarily
// editing src/customization.cpp, rebuilding, confirming the expected CHECK
// failed, then reverting):
//   1. testColorRefEitherOr: temporarily removed the `Color_Pool` branch
//      from ParseColorRef (leaving only the Color_Set check) ->
//      `CHECK(cc0.color.kind == ColorRefKind::ColorPool)` failed (a
//      Color_Choice with only <Color_Pool> present read back as
//      ColorRefKind::None instead); reverted.
//   2. testMultiSlotSpan: temporarily changed `r.present = (n != nullptr);`
//      in ParseMultiSlot to `r.present = true;` unconditionally ->
//      `CHECK(!bare.multiSlot.present)` (on a row with no <Multi_Slot> at
//      all) failed (got true instead of false); reverted.
//   3. testCustomizationItemNormalRow's VariantID check: temporarily changed
//      `r.variantId = ReadInt32Always(mvi, "VariantID");` in ParseVariant to
//      read from `n` (the Variant node) instead of `mvi`
//      (Mesh_Variant_Info) -> `CHECK(v.variantId.present && v.variantId.value == 1)`
//      failed (VariantID is a child of Mesh_Variant_Info per spec 2.1, not a
//      direct child of Variant, so the mutated reader found nothing and
//      variantId.present was false); reverted.

#include <iostream>
#include <string>

#include "sr3customization/customization.h"
#include "sr3customization/vehicle_customization.h"
#include "sr3xtbl/xtbl.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) {                                                                     \
            ++g_passed;                                                                 \
        } else {                                                                        \
            ++g_failed;                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
        }                                                                               \
    } while (0)

using namespace sr3xtbl;
using namespace sr3customization;

Document P(std::string_view s) { return ParseDocument(s); }

// ---------------------------------------------------------------------------
// customization_items.xtbl (spec 2.1). Worked example: cm_dlc_mask_panda.
// ---------------------------------------------------------------------------
void testCustomizationItemNormalRow() {
    const char* xml =
        "<root><Table><Customization_Item>"
        "<Name>cm_dlc_mask_panda</Name>"
        "<DisplayName>CUST_ITEM_CM_DLC_MASK_PANDA</DisplayName>"
        "<Wear_Options><Wear_Option>"
        "<Name>CUST_WEAR_NORMAL</Name>"
        "<disabled>false</disabled>"
        "<Active_Flags><Active_Flag>flag_a</Active_Flag><Active_Flag>flag_b</Active_Flag></Active_Flags>"
        "<Required_Flags><Required_Flag>flag_c</Required_Flag></Required_Flags>"
        "<Incompatible_Flags></Incompatible_Flags>"
        "<Particle_Systems><Particle_System>fx_panda_sparkle</Particle_System></Particle_Systems>"
        "<Mesh_Information>"
        "<Male_Mesh_Filename><Filename>cm_dlc_mask_panda.cmeshx</Filename></Male_Mesh_Filename>"
        "<Female_Mesh_Filename><Filename>cf_dlc_mask_panda.cmeshx</Filename></Female_Mesh_Filename>"
        "<Cutscene_Only>false</Cutscene_Only>"
        "<Obscured_slots>"
        "<Obscured_slot><Slot>head hair</Slot></Obscured_slot>"
        "<Obscured_slot><Slot>eyewear</Slot></Obscured_slot>"
        "<Obscured_slot><Slot>facewear</Slot></Obscured_slot>"
        "</Obscured_slots>"
        "<VID_List><VID>3</VID><VID>7</VID></VID_List>"
        "<Obscured_VIDs><Obscured_VID><VID>12</VID></Obscured_VID></Obscured_VIDs>"
        "</Mesh_Information>"
        "</Wear_Option></Wear_Options>"
        "<_Editor><Category>Entries:DLC Packs:Panda</Category></_Editor>"
        "<Flags><Flag>big hat</Flag></Flags>"
        "<Slot>headwear</Slot>"
        "<Base_Respect_Bonus>0</Base_Respect_Bonus>"
        "<Base_Price>500</Base_Price>"
        "<Variants><Variant>"
        "<Name>Default</Name>"
        "<Price>500</Price>"
        "<Respect_Bonus>0</Respect_Bonus>"
        "<Mesh_Variant_Info><Variant_Name>panda_default</Variant_Name><VariantID>1</VariantID></Mesh_Variant_Info>"
        "<Material_List><Material_Element><Material>mtl_panda_fur</Material></Material_Element></Material_List>"
        "<Shader_Type>ir_sr3pccloth</Shader_Type>"
        "<Default_Colors_Grid><Default_Color><Clothing_Color>white</Clothing_Color></Default_Color></Default_Colors_Grid>"
        "</Variant></Variants>"
        "<Multi_Slot><First_Slot>headwear</First_Slot><Last_Slot>eyewear</Last_Slot></Multi_Slot>"
        "<Framework>dlc1</Framework>"
        "<Default_Colors_Grid><Default_Color><Clothing_Color>black</Clothing_Color></Default_Color></Default_Colors_Grid>"
        "<Is_DLC>true</Is_DLC>"
        "</Customization_Item></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    const Node* row = FindChild(doc.table(), "Customization_Item");
    CHECK(row != nullptr);
    CustomizationItem it = ParseCustomizationItem(row);

    CHECK(it.name.has_value() && *it.name == "cm_dlc_mask_panda");
    CHECK(it.displayName.has_value() && *it.displayName == "CUST_ITEM_CM_DLC_MASK_PANDA");

    CHECK(it.wearOptions.size() == 1);
    const WearOption& wo = it.wearOptions[0];
    CHECK(wo.name.has_value() && *wo.name == "CUST_WEAR_NORMAL");
    CHECK(wo.disabled.present && wo.disabled.value == false);
    // Real base-game data (see customization.h's field comment) confirms the
    // singular-wrapper-name pattern: Active_Flags > Active_Flag, etc. - NOT
    // a generic <Flag> child.
    CHECK(wo.activeFlags.size() == 2 && wo.activeFlags[0] == "flag_a" && wo.activeFlags[1] == "flag_b");
    CHECK(wo.requiredFlags.size() == 1 && wo.requiredFlags[0] == "flag_c");
    CHECK(wo.incompatibleFlags.empty());  // <Incompatible_Flags></Incompatible_Flags>: present but empty
    CHECK(wo.particleSystems.size() == 1 && wo.particleSystems[0] == "fx_panda_sparkle");

    const MeshInformation& mi = wo.meshInformation;
    CHECK(mi.maleMeshFilename.has_value() && *mi.maleMeshFilename == "cm_dlc_mask_panda.cmeshx");
    CHECK(mi.femaleMeshFilename.has_value() && *mi.femaleMeshFilename == "cf_dlc_mask_panda.cmeshx");
    CHECK(mi.cutsceneOnly.present && mi.cutsceneOnly.value == false);
    CHECK(mi.obscuredSlots.size() == 3);
    CHECK(mi.obscuredSlots[0] == "head hair" && mi.obscuredSlots[1] == "eyewear" && mi.obscuredSlots[2] == "facewear");
    CHECK(mi.vidList.size() == 2 && mi.vidList[0] == 3 && mi.vidList[1] == 7);
    CHECK(mi.obscuredVids.size() == 1 && mi.obscuredVids[0] == 12);

    CHECK(it.editorCategory.has_value() && *it.editorCategory == "Entries:DLC Packs:Panda");
    CHECK(it.flags.size() == 1 && it.flags[0] == "big hat");
    CHECK(it.slot.has_value() && *it.slot == "headwear");

    CHECK(it.baseRespectBonus.present && it.baseRespectBonus.value == 0);
    CHECK(it.basePrice.present && it.basePrice.value == 500);

    CHECK(it.variants.size() == 1);
    const CustomizationVariant& v = it.variants[0];
    CHECK(v.name.has_value() && *v.name == "Default");
    CHECK(v.price.present && v.price.value == 500);
    CHECK(v.respectBonus.present && v.respectBonus.value == 0);
    CHECK(v.meshVariantName.has_value() && *v.meshVariantName == "panda_default");
    CHECK(v.variantId.present && v.variantId.value == 1);  // Mesh_Variant_Info > VariantID (spec 2.1/5.1)
    CHECK(v.materials.size() == 1 && v.materials[0] == "mtl_panda_fur");
    CHECK(v.shaderType.has_value() && *v.shaderType == "ir_sr3pccloth");
    CHECK(v.defaultColorsGrid.clothingColors.size() == 1 && v.defaultColorsGrid.clothingColors[0] == "white");

    CHECK(it.multiSlot.present);
    CHECK(it.multiSlot.firstSlot.has_value() && *it.multiSlot.firstSlot == "headwear");
    CHECK(it.multiSlot.lastSlot.has_value() && *it.multiSlot.lastSlot == "eyewear");

    CHECK(it.framework.has_value() && *it.framework == "dlc1");
    CHECK(it.defaultColorsGrid.clothingColors.size() == 1 && it.defaultColorsGrid.clothingColors[0] == "black");
    CHECK(it.isDlc.present && it.isDlc.value == true);
}

// ---------------------------------------------------------------------------
// A row missing every optional element: absence must stay observable.
// ---------------------------------------------------------------------------
void testCustomizationItemMissingOptional() {
    const char* xml = "<root><Table><Customization_Item><Name>bare_item</Name></Customization_Item></Table></root>";
    Document doc = P(xml);
    CustomizationItem it = ParseCustomizationItem(FindChild(doc.table(), "Customization_Item"));

    CHECK(it.name.has_value() && *it.name == "bare_item");
    CHECK(!it.displayName.has_value());
    CHECK(it.wearOptions.empty());
    CHECK(!it.editorCategory.has_value());
    CHECK(it.flags.empty());
    CHECK(!it.slot.has_value());
    CHECK(!it.baseRespectBonus.present);
    CHECK(!it.basePrice.present);
    CHECK(it.variants.empty());
    CHECK(!it.multiSlot.present);
    CHECK(!it.multiSlot.firstSlot.has_value());
    CHECK(!it.framework.has_value());
    CHECK(it.defaultColorsGrid.clothingColors.empty());
    CHECK(!it.isDlc.present);
}

// ---------------------------------------------------------------------------
// customization_outfits.xtbl (spec 2.2). Worked example: "Panda Outfit".
// ---------------------------------------------------------------------------
void testOutfitNormalRow() {
    const char* xml =
        "<root><Table><Outfit>"
        "<Name>Panda Outfit</Name>"
        "<Display_Name>CUST_OUTFIT_PANDA</Display_Name>"
        "<Content_Grid>"
        "<Content_Element><Item>cm_dlc_mask_panda</Item><Default_Wear_Option>cm_dlc_mask_panda.cmeshx</Default_Wear_Option>"
        "<Primary_Color>white</Primary_Color></Content_Element>"
        "<Content_Element><Item>cm_dlc_suit_panda</Item><Default_Wear_Option>cm_dlc_suit_panda.cmeshx</Default_Wear_Option></Content_Element>"
        "</Content_Grid>"
        "<Styles_Grid><Style_Element>"
        "<Display_Name>CUST_STYLE_DEFAULT</Display_Name>"
        "<Variant_Grid><Variant_Element><Variant>panda_default</Variant></Variant_Element></Variant_Grid>"
        "</Style_Element></Styles_Grid>"
        "<_Editor><Category>Entries:DLC Packs:Panda</Category></_Editor>"
        "<Framework>dlc1</Framework>"
        "<Is_DLC>true</Is_DLC>"
        "</Outfit></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    Outfit o = ParseOutfit(FindChild(doc.table(), "Outfit"));

    CHECK(o.name.has_value() && *o.name == "Panda Outfit");
    CHECK(o.displayName.has_value() && *o.displayName == "CUST_OUTFIT_PANDA");

    CHECK(o.contentGrid.size() == 2);
    CHECK(o.contentGrid[0].item.has_value() && *o.contentGrid[0].item == "cm_dlc_mask_panda");
    CHECK(o.contentGrid[0].defaultWearOption.has_value() && *o.contentGrid[0].defaultWearOption == "cm_dlc_mask_panda.cmeshx");
    CHECK(o.contentGrid[0].primaryColor.has_value() && *o.contentGrid[0].primaryColor == "white");
    CHECK(!o.contentGrid[0].secondaryColor.has_value());  // "only present on items that use them" (spec 2.2)
    CHECK(o.contentGrid[1].item.has_value() && *o.contentGrid[1].item == "cm_dlc_suit_panda");
    CHECK(!o.contentGrid[1].primaryColor.has_value());

    CHECK(o.stylesGrid.size() == 1);
    CHECK(o.stylesGrid[0].displayName.has_value() && *o.stylesGrid[0].displayName == "CUST_STYLE_DEFAULT");
    CHECK(o.stylesGrid[0].variantGrid.size() == 1 && o.stylesGrid[0].variantGrid[0] == "panda_default");

    CHECK(o.editorCategory.has_value() && *o.editorCategory == "Entries:DLC Packs:Panda");
    CHECK(o.framework.has_value() && *o.framework == "dlc1");
    CHECK(o.isDlc.present && o.isDlc.value == true);
}

void testOutfitMissingOptional() {
    const char* xml = "<root><Table><Outfit><Name>bare_outfit</Name></Outfit></Table></root>";
    Document doc = P(xml);
    Outfit o = ParseOutfit(FindChild(doc.table(), "Outfit"));

    CHECK(o.name.has_value() && *o.name == "bare_outfit");
    CHECK(!o.displayName.has_value());
    CHECK(o.contentGrid.empty());
    CHECK(o.stylesGrid.empty());
    CHECK(!o.editorCategory.has_value());
    CHECK(!o.framework.has_value());
    CHECK(!o.isDlc.present);
}

// ---------------------------------------------------------------------------
// Vehicle customization variant file, <vehicle_name>.xtbl (spec 3). Worked
// example: sp_mdsphere_genki.
// ---------------------------------------------------------------------------
void testVehicleCustVariantsNormalRow() {
    // Components/Colors/Wheels nest inside EACH <Variant> (confirmed against
    // the real sp_mdsphere_genki.xtbl - see vehicle_customization.h's
    // REAL-DATA CORRECTION banner), not directly under <Vehicle>.
    const char* xml =
        "<root><Table><Vehicle>"
        "<Name>sp_mdsphere_genki</Name>"
        "<Variants>"
        "<Variant><File_Id>1</File_Id><Name>Rich</Name><Weight>20</Weight><ParkingWeight>10</ParkingWeight>"
        "<Type>rich</Type><Siren>false</Siren><Fully_Customizable>true</Fully_Customizable><Has_Peg>true</Has_Peg>"
        "<Bitmap>genki_rich_bitmap</Bitmap>"
        "<Components><Component_Group><Name>Chassis</Name>"
        "<Component_Chances><Component_Chance><Name>Stock</Name><Weight>50</Weight>"
        "<Components><Component_Element><Slot>bumper_f</Slot><Component>bumper_f_stock</Component></Component_Element></Components>"
        "<Externalized_Components><Externalized_Component><Component>ext_spoiler</Component></Externalized_Component></Externalized_Components>"
        "</Component_Chance></Component_Chances>"
        "</Component_Group></Components>"
        "<Colors><Color_Group><Name>Primary</Name>"
        "<Color_Chances><Color_Chance><Name>StockColors</Name><Weight>100</Weight>"
        "<Color_Choices>"
        "<Color_Choice><Slot>body</Slot><Component>main</Component><Color_Slot>primary</Color_Slot><Color><Color_Pool>genki_pool</Color_Pool></Color></Color_Choice>"
        "<Color_Choice><Slot>trim</Slot><Component>main</Component><Color_Slot>secondary</Color_Slot><Color><Color_Set>genki_set</Color_Set></Color></Color_Choice>"
        "<Color_Choice><Slot>none</Slot></Color_Choice>"
        "</Color_Choices>"
        "</Color_Chance></Color_Chances>"
        "</Color_Group></Colors>"
        "<Wheels><Wheel_Group><Name>Stock</Name><Front_Width>18</Front_Width><Front_Size>20</Front_Size>"
        "<Rear_Width>18</Rear_Width><Rear_Size>20</Rear_Size><Weight>100</Weight></Wheel_Group></Wheels>"
        "</Variant>"
        "<Variant><File_Id>2</File_Id><Name>Poor</Name><Type>poor</Type></Variant>"
        "</Variants>"
        "</Vehicle></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    VehicleCustVariants vc = ParseVehicleCustVariants(FindChild(doc.table(), "Vehicle"));

    CHECK(vc.vehicleName.has_value() && *vc.vehicleName == "sp_mdsphere_genki");

    CHECK(vc.variants.size() == 2);
    CHECK(vc.variants[0].fileId.present && vc.variants[0].fileId.value == 1);
    CHECK(vc.variants[0].name.has_value() && *vc.variants[0].name == "Rich");
    CHECK(vc.variants[0].weight.present && vc.variants[0].weight.value == 20.0f);
    CHECK(vc.variants[0].parkingWeight.present && vc.variants[0].parkingWeight.value == 10.0f);
    CHECK(vc.variants[0].type == 0);  // "rich" is index 0
    CHECK(vc.variants[0].siren.present && vc.variants[0].siren.value == false);
    CHECK(vc.variants[0].fullyCustomizable.present && vc.variants[0].fullyCustomizable.value == true);
    CHECK(vc.variants[0].hasPeg.present && vc.variants[0].hasPeg.value == true);
    CHECK(vc.variants[0].bitmap.has_value() && *vc.variants[0].bitmap == "genki_rich_bitmap");
    CHECK(vc.variants[1].type == 1);  // "poor" is index 1
    CHECK(!vc.variants[1].weight.present);
    CHECK(vc.variants[1].componentGroups.empty() && vc.variants[1].colorGroups.empty() && vc.variants[1].wheelGroups.empty());

    CHECK(vc.variants[0].componentGroups.size() == 1);
    const ComponentGroup& cg = vc.variants[0].componentGroups[0];
    CHECK(cg.name.has_value() && *cg.name == "Chassis");
    CHECK(cg.componentChances.size() == 1);
    const ComponentChance& cc = cg.componentChances[0];
    CHECK(cc.name.has_value() && *cc.name == "Stock");
    CHECK(cc.weight.present && cc.weight.value == 50.0f);
    CHECK(cc.components.size() == 1 && cc.components[0].slot.has_value() && *cc.components[0].slot == "bumper_f");
    CHECK(cc.components[0].component.has_value() && *cc.components[0].component == "bumper_f_stock");
    CHECK(cc.externalizedComponents.size() == 1 && cc.externalizedComponents[0].component.has_value() &&
          *cc.externalizedComponents[0].component == "ext_spoiler");

    CHECK(vc.variants[0].colorGroups.size() == 1);
    const ColorGroup& colg = vc.variants[0].colorGroups[0];
    CHECK(colg.name.has_value() && *colg.name == "Primary");
    CHECK(colg.colorChances.size() == 1);
    const ColorChance& colc = colg.colorChances[0];
    CHECK(colc.name.has_value() && *colc.name == "StockColors");
    CHECK(colc.weight.present && colc.weight.value == 100.0f);
    CHECK(colc.colorChoices.size() == 3);
    CHECK(colc.colorChoices[0].slot.has_value() && *colc.colorChoices[0].slot == "body");
    CHECK(colc.colorChoices[0].colorSlot.has_value() && *colc.colorChoices[0].colorSlot == "primary");
    CHECK(colc.colorChoices[0].color.kind == ColorRefKind::ColorPool);
    CHECK(colc.colorChoices[0].color.value == "genki_pool");
    CHECK(colc.colorChoices[1].color.kind == ColorRefKind::ColorSet);
    CHECK(colc.colorChoices[1].color.value == "genki_set");
    CHECK(colc.colorChoices[2].color.kind == ColorRefKind::None);  // no <Color> at all
    CHECK(colc.colorChoices[2].color.value.empty());

    CHECK(vc.variants[0].wheelGroups.size() == 1);
    const WheelGroup& wg = vc.variants[0].wheelGroups[0];
    CHECK(wg.name.has_value() && *wg.name == "Stock");
    CHECK(wg.frontWidth.present && wg.frontWidth.value == 18.0f);
    CHECK(wg.frontSize.present && wg.frontSize.value == 20.0f);
    CHECK(wg.rearWidth.present && wg.rearWidth.value == 18.0f);
    CHECK(wg.rearSize.present && wg.rearSize.value == 20.0f);
    CHECK(wg.weight.present && wg.weight.value == 100.0f);
}

// ---------------------------------------------------------------------------
// Dedicated ComboElement test (spec 3's Color_Choice.Color: "either a single
// Color_Pool reference or a Color_Set (palette) reference").
// ---------------------------------------------------------------------------
void testColorRefEitherOr() {
    Document poolDoc = P(
        "<root><Table><Vehicle><Name>t1</Name><Variants><Variant><Name>v</Name>"
        "<Colors><Color_Group><Name>g</Name><Color_Chances><Color_Chance><Name>c</Name>"
        "<Color_Choices><Color_Choice><Color><Color_Pool>pool_a</Color_Pool></Color></Color_Choice></Color_Choices>"
        "</Color_Chance></Color_Chances></Color_Group></Colors></Variant></Variants></Vehicle></Table></root>");
    VehicleCustVariants vc1 = ParseVehicleCustVariants(FindChild(poolDoc.table(), "Vehicle"));
    const ColorChoice& cc0 = vc1.variants[0].colorGroups[0].colorChances[0].colorChoices[0];
    CHECK(cc0.color.kind == ColorRefKind::ColorPool);
    CHECK(cc0.color.value == "pool_a");

    Document setDoc = P(
        "<root><Table><Vehicle><Name>t2</Name><Variants><Variant><Name>v</Name>"
        "<Colors><Color_Group><Name>g</Name><Color_Chances><Color_Chance><Name>c</Name>"
        "<Color_Choices><Color_Choice><Color><Color_Set>set_a</Color_Set></Color></Color_Choice></Color_Choices>"
        "</Color_Chance></Color_Chances></Color_Group></Colors></Variant></Variants></Vehicle></Table></root>");
    VehicleCustVariants vc2 = ParseVehicleCustVariants(FindChild(setDoc.table(), "Vehicle"));
    const ColorChoice& cc1 = vc2.variants[0].colorGroups[0].colorChances[0].colorChoices[0];
    CHECK(cc1.color.kind == ColorRefKind::ColorSet);
    CHECK(cc1.color.value == "set_a");
}

// ---------------------------------------------------------------------------
// Dedicated Multi_Slot presence/absence test (spec 2.1 "Slot span").
// ---------------------------------------------------------------------------
void testMultiSlotSpan() {
    Document present = P(
        "<root><Table><Customization_Item><Name>i1</Name>"
        "<Multi_Slot><First_Slot>headwear</First_Slot><Last_Slot>eyewear</Last_Slot></Multi_Slot>"
        "</Customization_Item></Table></root>");
    CustomizationItem it1 = ParseCustomizationItem(FindChild(present.table(), "Customization_Item"));
    CHECK(it1.multiSlot.present);
    CHECK(*it1.multiSlot.firstSlot == "headwear" && *it1.multiSlot.lastSlot == "eyewear");

    Document bareDoc = P("<root><Table><Customization_Item><Name>i2</Name></Customization_Item></Table></root>");
    CustomizationItem bare = ParseCustomizationItem(FindChild(bareDoc.table(), "Customization_Item"));
    CHECK(!bare.multiSlot.present);
    CHECK(!bare.multiSlot.firstSlot.has_value());
    CHECK(!bare.multiSlot.lastSlot.has_value());
}

// ---------------------------------------------------------------------------
// Dedicated Type enum membership test (spec 3: rich/poor/pimped/riced/normal).
// ---------------------------------------------------------------------------
void testVehicleVariantTypeEnum() {
    static const char* kNames[5] = {"rich", "poor", "pimped", "riced", "normal"};
    for (int i = 0; i < 5; ++i) {
        std::string xml = std::string("<root><Table><Vehicle><Name>t</Name><Variants><Variant><Type>") + kNames[i] +
                           "</Type></Variant></Variants></Vehicle></Table></root>";
        Document doc = P(xml);
        VehicleCustVariants vc = ParseVehicleCustVariants(FindChild(doc.table(), "Vehicle"));
        CHECK(vc.variants.size() == 1);
        CHECK(vc.variants[0].type == i);
    }
    // Unmatched / absent text -> -1 (this project's general enum convention).
    Document unmatched =
        P("<root><Table><Vehicle><Name>t</Name><Variants><Variant><Type>not_a_real_type</Type></Variant></Variants></Vehicle></Table></root>");
    CHECK(ParseVehicleCustVariants(FindChild(unmatched.table(), "Vehicle")).variants[0].type == -1);
    Document absent = P("<root><Table><Vehicle><Name>t</Name><Variants><Variant><Name>x</Name></Variant></Variants></Vehicle></Table></root>");
    CHECK(ParseVehicleCustVariants(FindChild(absent.table(), "Vehicle")).variants[0].type == -1);
}

// ---------------------------------------------------------------------------
// ParseAll* convenience functions: multiple rows in file order.
// ---------------------------------------------------------------------------
void testParseAllConvenience() {
    Document items = P(
        "<root><Table>"
        "<Customization_Item><Name>a</Name></Customization_Item>"
        "<Customization_Item><Name>b</Name></Customization_Item>"
        "</Table></root>");
    std::vector<CustomizationItem> all = ParseAllCustomizationItems(items);
    CHECK(all.size() == 2);
    CHECK(*all[0].name == "a" && *all[1].name == "b");

    Document outfits = P(
        "<root><Table>"
        "<Outfit><Name>o1</Name></Outfit>"
        "<Outfit><Name>o2</Name></Outfit>"
        "</Table></root>");
    std::vector<Outfit> allO = ParseAllOutfits(outfits);
    CHECK(allO.size() == 2);
    CHECK(*allO[0].name == "o1" && *allO[1].name == "o2");

    Document vehs = P(
        "<root><Table>"
        "<Vehicle><Name>v1</Name></Vehicle>"
        "<Vehicle><Name>v2</Name></Vehicle>"
        "</Table></root>");
    std::vector<VehicleCustVariants> allV = ParseAllVehicleCustVariants(vehs);
    CHECK(allV.size() == 2);
    CHECK(*allV[0].vehicleName == "v1" && *allV[1].vehicleName == "v2");
}

}  // namespace

int main() {
    testCustomizationItemNormalRow();
    testCustomizationItemMissingOptional();
    testOutfitNormalRow();
    testOutfitMissingOptional();
    testVehicleCustVariantsNormalRow();
    testColorRefEitherOr();
    testMultiSlotSpan();
    testVehicleVariantTypeEnum();
    testParseAllConvenience();

    if (g_failed) {
        std::cerr << g_failed << " check(s) FAILED (" << g_passed << " passed).\n";
        return 1;
    }
    std::cout << g_passed << " tests passed.\n";
    return 0;
}
