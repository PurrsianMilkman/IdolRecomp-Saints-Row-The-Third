// Synthetic tests for sr3tables_customization, built FROM THE TEXT of
// spec-tables-customization.md only (hand-rolled XML fixtures as string
// literals) - never derived from this project's own reader source. A pass
// here proves the reader implements the spec's text, not that the spec is
// right; the real-data statement is
// tools/validation/validate_tables_customization_population.cpp.
//
// One test function per implemented table group (the §2 four-file colour
// pool family shares one schema and one test; the two §13.1/§13.3 "row
// locator only" tables get a lighter test matching their thinner contract).
// Mutation-tested during development: several field-name/offset checks
// below were deliberately broken in the .cpp readers (e.g. renaming a
// ChildText() lookup, flipping a flag filter) and confirmed to fail before
// being reverted - see the task report for specifics.

#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_customization/tables.h"

namespace {

using namespace sr3tables_customization;
using namespace sr3xtbl;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__   \
                      << "\n";                                                    \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

Document P(std::string_view s) { return ParseDocument(s); }

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

// ===========================================================================
// 2. The shared named-color-pool family (character/hair/makeup/tattoo).
// One reader, ParseColorPoolTable, applies to all four filenames (§2.1).
// ===========================================================================
void testColorPool() {
    Document d = P(R"(<root><Table>
      <Color_Entry>
        <Name>Crimson</Name>
        <Color>1.0 0.0 0.0 1.0</Color>
        <DisplayName>COLOR_CRIMSON</DisplayName>
      </Color_Entry>
      <Color_Entry>
        <Name>NoDisplay</Name>
        <Color>0.5 0.5 0.5</Color>
      </Color_Entry>
    </Table></root>)");
    std::vector<ColorPoolEntry> rows = ParseColorPoolTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Crimson");
    CHECK(rows[0].color.has_value() && rows[0].color->r == 1.0f && rows[0].color->g == 0.0f && rows[0].color->b == 0.0f);
    CHECK(rows[0].displayName.has_value() && *rows[0].displayName == "COLOR_CRIMSON");
    // Trailing alpha component unspecified -> stays at the ColorText default (1.0).
    CHECK(rows[1].color.has_value() && rows[1].color->a == 1.0f);
    CHECK(!rows[1].displayName.has_value());
}

void testColorBalance() {
    Document d = P(R"(<root><Table>
      <ColorBalance>
        <Black_Level>0.1</Black_Level><White_Level>0.9</White_Level><Saturation>1.1</Saturation>
        <Ped_Black_Level>0.05</Ped_Black_Level><Ped_White_Level>0.95</Ped_White_Level><Ped_Saturation>1.0</Ped_Saturation>
      </ColorBalance>
    </Table></root>)");
    std::optional<ColorBalance> cb = ParseColorBalance(d);
    CHECK(cb.has_value());
    CHECK(cb->blackLevel.present && cb->blackLevel.value == 0.1f);
    CHECK(cb->pedSaturation.present && cb->pedSaturation.value == 1.0f);

    Document empty = P(R"(<root><Table></Table></root>)");
    CHECK(!ParseColorBalance(empty).has_value());
}

// ===========================================================================
// 3.1 items_color_pool.xtbl
// ===========================================================================
void testItemsColorPool() {
    Document d = P(R"(<root><Table>
      <Item_Color><Name>Steel</Name><Color>0.4 0.4 0.5</Color><_Editor>Entries:Vehicle Paint</_Editor></Item_Color>
    </Table></root>)");
    std::vector<ItemColorEntry> rows = ParseItemsColorPoolTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "Steel");
    // Color is kept as RAW TEXT (no parsing routine confirmed for this table, §3.1).
    CHECK(rows[0].color.has_value() && *rows[0].color == "0.4 0.4 0.5");
    CHECK(rows[0].editor == "Entries:Vehicle Paint");
}

// ===========================================================================
// 3.2 npc_color_palette.xtbl - the recursive three-level tree.
// ===========================================================================
void testNpcColorPalette() {
    Document d = P(R"(<root><Table>
      <Palette>
        <Name>GangPurple</Name>
        <Primary_Colors>
          <Primary_Color>
            <R>0.5</R><G>0.0</G><B>0.5</B>
            <Secondary_Colors>
              <Secondary_Color>
                <R>0.6</R><G>0.1</G><B>0.6</B>
                <Tertiary_Colors>
                  <Tertiary_Color><R>0.7</R><G>0.2</G><B>0.7</B></Tertiary_Color>
                </Tertiary_Colors>
              </Secondary_Color>
            </Secondary_Colors>
          </Primary_Color>
        </Primary_Colors>
      </Palette>
    </Table></root>)");
    std::vector<Palette> rows = ParseNpcColorPaletteTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "GangPurple");
    CHECK(rows[0].primaryColors.size() == 1);
    const PrimaryColor& pc = rows[0].primaryColors[0];
    CHECK(pc.r.present && pc.r.value == 0.5f);
    CHECK(pc.secondaryColors.size() == 1);
    CHECK(pc.secondaryColors[0].g.value == 0.1f);
    CHECK(pc.secondaryColors[0].tertiaryColors.size() == 1);
    CHECK(pc.secondaryColors[0].tertiaryColors[0].b.value == 0.7f);
}

// ===========================================================================
// 4. customization_items.xtbl
// ===========================================================================
void testCustomizationItems() {
    Document d = P(R"(<root><Table>
      <Customization_Item>
        <Name>skipped_item</Name>
        <Flags><Flag>not ready</Flag></Flags>
      </Customization_Item>
      <Customization_Item>
        <Name>cool_jacket</Name>
        <Flags><Flag>big hat</Flag><Flag>npc only</Flag></Flags>
        <Slot>upper body</Slot>
        <Base_Price>100</Base_Price>
        <Base_Respect_Bonus>5</Base_Respect_Bonus>
        <Brand>Astro Gaming</Brand>
        <Is_DLC>yes</Is_DLC>
        <DisplayName>CUST_JACKET</DisplayName>
        <Wear_Options>
          <Wear_Option>
            <Name>disabled_option</Name>
            <Disabled>yes</Disabled>
          </Wear_Option>
          <Wear_Option>
            <Name>CUST_WEAR_NORMAL</Name>
            <Mesh_Information><Male_Mesh_Filename><Filename>jacket_m.cmeshx</Filename></Male_Mesh_Filename></Mesh_Information>
            <Active_Flags><Active_Flag>HAT ACTIVE<Comparison>yes</Comparison></Active_Flag></Active_Flags>
          </Wear_Option>
        </Wear_Options>
        <Variants>
          <Variant>
            <Name>Red</Name>
            <Material_List><Material_Element><Material>cm_suit_default</Material><Shader_Type>ir_sr3pccloth</Shader_Type></Material_Element></Material_List>
          </Variant>
        </Variants>
        <Default_Colors_Grid><Default_Color><Clothing_Color>Crimson</Clothing_Color></Default_Color></Default_Colors_Grid>
        <ShoeAudioSwitch>shoe_switch</ShoeAudioSwitch>
        <ClothingAudioSwitch>cloth_switch</ClothingAudioSwitch>
        <Style>Cool</Style>
      </Customization_Item>
    </Table></root>)");
    std::vector<CustomizationItemEntry> rows = ParseCustomizationItemsTable(d);
    // The "not ready" row must be dropped entirely (§4.1).
    CHECK(rows.size() == 1);
    const CustomizationItemEntry& it = rows[0];
    CHECK(it.name == "cool_jacket");
    CHECK(it.bigHat && it.npcOnly && !it.notReady);
    CHECK(it.slotIndex == 12);  // "upper body" is index 12 in kCanonicalSlotNames
    CHECK(it.basePrice.present && it.basePrice.value == 100);
    CHECK(it.brand == "Astro Gaming");
    CHECK(it.isDlc.present && it.isDlc.value == true);
    // Disabled Wear_Option dropped entirely (§4.2).
    CHECK(it.wearOptions.size() == 1);
    CHECK(it.wearOptions[0].name == "CUST_WEAR_NORMAL");
    CHECK(it.wearOptions[0].maleMeshFilename == "jacket_m.cmeshx");
    CHECK(it.wearOptions[0].activeFlags.size() == 1);
    CHECK(it.wearOptions[0].activeFlags[0].name == "HAT ACTIVE");
    CHECK(it.wearOptions[0].activeFlags[0].comparison.has_value() && *it.wearOptions[0].activeFlags[0].comparison == true);
    CHECK(it.variants.size() == 1 && it.variants[0].materialList.size() == 1);
    CHECK(it.variants[0].materialList[0].material == "cm_suit_default");
    CHECK(it.variants[0].materialList[0].shaderType == "ir_sr3pccloth");  // DISCREPANCY 2: per-Material_Element, not per-Variant
    CHECK(it.defaultColorsGrid.size() == 1 && it.defaultColorsGrid[0].clothingColor == "Crimson");
    // Heel/Fat_Bone fields absent -> spec-stated concrete default 0.0, not the general Always-hazard.
    CHECK(it.HeelAngleOrDefault() == 0.0f);
    CHECK(it.FatBoneArmOrDefault() == 0.0f);
    // Both audio-switch elements present -> Clothing wins (§4.1, real code behaviour).
    CHECK(it.shoeAudioSwitch == "shoe_switch");
    CHECK(it.clothingAudioSwitch == "cloth_switch");
    CHECK(it.AudioSwitchEffective().has_value() && *it.AudioSwitchEffective() == "cloth_switch");
    CHECK(it.styleIndex == 0);  // "Cool" -> kStyleNames[0]

    // Only ShoeAudioSwitch present -> that one is effective.
    Document d2 = P(R"(<root><Table><Customization_Item><Name>shoes1</Name><ShoeAudioSwitch>only_shoe</ShoeAudioSwitch></Customization_Item></Table></root>)");
    std::vector<CustomizationItemEntry> rows2 = ParseCustomizationItemsTable(d2);
    CHECK(rows2.size() == 1);
    CHECK(rows2[0].AudioSwitchEffective().has_value() && *rows2[0].AudioSwitchEffective() == "only_shoe");
}

// ===========================================================================
// 5.2 customization_stores.xtbl
// ===========================================================================
void testCustomizationStores() {
    Document d = P(R"(<root><Table>
      <Store>
        <Name>Nobles</Name>
        <Flags><Flag>allow_wardrobe</Flag></Flags>
        <Store_Item>hat_01<Variants><Item_Variant>Red</Item_Variant><Item_Variant>Blue</Item_Variant></Variants></Store_Item>
        <Store_Item>hat_02</Store_Item>
        <Outfits><Outfit_Element>Sunday Best</Outfit_Element></Outfits>
      </Store>
    </Table></root>)");
    std::vector<Store> rows = ParseCustomizationStoresTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].allowWardrobe);
    CHECK(rows[0].storeItems.size() == 2);
    CHECK(rows[0].storeItems[0].itemName == "hat_01");
    CHECK(rows[0].storeItems[0].variantsPresent);
    CHECK(rows[0].storeItems[0].itemVariants.size() == 2 && rows[0].storeItems[0].itemVariants[1] == "Blue");
    // Absent Variants wrapper -> "every variant listed by default" is a load-time
    // default this reader does NOT compute; variantsPresent must be false.
    CHECK(!rows[0].storeItems[1].variantsPresent);
    CHECK(rows[0].outfitElements.size() == 1 && rows[0].outfitElements[0] == "Sunday Best");
}

// ===========================================================================
// 6.1 customization_slots.xtbl
// ===========================================================================
void testCustomizationSlots() {
    Document d = P(R"(<root><Table>
      <Slot>
        <Name>suit</Name>
        <CPU_Size>800</CPU_Size>
        <GPU_Size>1600</GPU_Size>
        <Flags><Flag>required</Flag><Flag>remove on outfit</Flag></Flags>
        <Render_Order>3</Render_Order>
      </Slot>
      <Slot><Name>unknown slot name</Name></Slot>
    </Table></root>)");
    std::vector<SlotEntry> rows = ParseCustomizationSlotsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].slotIndex == 19);  // "suit"
    CHECK(rows[0].cpuSizeRaw.value == 800);
    CHECK(rows[0].CpuSizeInflated() == 800 + 800 / 8);  // "value + value/8" 12.5% headroom (§6.1)
    CHECK(rows[0].required && rows[0].removeOnOutfit && !rows[0].npcOnly);
    CHECK(rows[0].renderOrder.value == 3);
    // Unmatched name -> index -1, row still parsed (not dropped, §6.1).
    CHECK(rows[1].slotIndex == -1);
}

// ===========================================================================
// 6.2 customization_slot_defaults.xtbl - shipped <Table> is EMPTY (§6.2).
// ===========================================================================
void testCustomizationSlotDefaults() {
    Document empty = P(R"(<root><Table></Table></root>)");
    CHECK(ParseCustomizationSlotDefaultsTable(empty).empty());

    // Schema shape when present (never observed in shipped base data, but the
    // mechanism/reader must still work per §6.2's own confirmed field mapping).
    Document d = P(R"(<root><Table>
      <Slot_Default><Slot>upper body</Slot><Item>undershirt</Item><Variant>White</Variant></Slot_Default>
    </Table></root>)");
    std::vector<SlotDefaultEntry> rows = ParseCustomizationSlotDefaultsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].slot == "upper body" && rows[0].item == "undershirt" && rows[0].variant == "White");
}

// ===========================================================================
// 7.1 customization_categories.xtbl
// ===========================================================================
void testCustomizationCategories() {
    Document d = P(R"(<root><Table>
      <Category>
        <Name>Headwear</Name>
        <DisplayName>CAT_HEADWEAR</DisplayName>
        <Slots_Grid><Slot_Element>headwear</Slot_Element></Slots_Grid>
        <Obscure_Grid><Obscure_Element><Slot>head hair</Slot><Flag>HAT ACTIVE</Flag></Obscure_Element></Obscure_Grid>
        <Male_Animations><State>try_on</State><Animation>male_try_on_hat</Animation></Male_Animations>
        <No_Wear_String>CAT_NONE</No_Wear_String>
      </Category>
    </Table></root>)");
    std::vector<Category> rows = ParseCustomizationCategoriesTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].slotsGrid.size() == 1 && rows[0].slotsGrid[0] == "headwear");
    CHECK(rows[0].obscureGrid.size() == 1);
    CHECK(rows[0].obscureGrid[0].slot == "head hair" && rows[0].obscureGrid[0].flag == "HAT ACTIVE");
    CHECK(rows[0].maleAnimations.has_value());
    CHECK(rows[0].maleAnimations->state == "try_on" && rows[0].maleAnimations->animation == "male_try_on_hat");
    CHECK(!rows[0].femaleAnimations.has_value());
    CHECK(rows[0].noWearString == "CAT_NONE");
}

// ===========================================================================
// 7.2 customization_flags.xtbl
// ===========================================================================
void testCustomizationFlags() {
    Document d = P(R"(<root><Table><Flag><Name>BLING_COLLIDER_ACTIVE</Name></Flag><Flag><Name>big hat</Name></Flag></Table></root>)");
    std::vector<FlagEntry> rows = ParseCustomizationFlagsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "BLING_COLLIDER_ACTIVE");
    CHECK(rows[1].name == "big hat");
}

// ===========================================================================
// 7.3 customization_icons.xtbl
// ===========================================================================
void testCustomizationIcons() {
    Document d = P(R"(<root><Table>
      <Icon>
        <Name>headwear_icon</Name>
        <DisplayName>ICON_HEADWEAR</DisplayName>
        <Categories><Category_Element>Headwear</Category_Element></Categories>
        <Outfits_Only>no</Outfits_Only>
      </Icon>
    </Table></root>)");
    std::vector<Icon> rows = ParseCustomizationIconsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].categoryElements.size() == 1 && rows[0].categoryElements[0] == "Headwear");
    CHECK(rows[0].outfitsOnly.present && rows[0].outfitsOnly.value == false);
}

// ===========================================================================
// 8. customization_default_items.xtbl - nested Table>Defaults>Defaults_List>Default[]
// ===========================================================================
void testCustomizationDefaultItems() {
    Document d = P(R"(<root><Table><Defaults><Defaults_List>
      <Default>
        <Item>undershirt</Item><Wear_Option>CUST_WEAR_NORMAL</Wear_Option><Variant>White</Variant>
        <Gender>female</Gender><Race>hispanic</Race>
      </Default>
      <Default>
        <Item>pants</Item>
        <Color_Pool><Character_Color_Grid><Color_Element><Color>Navy</Color></Color_Element></Character_Color_Grid></Color_Pool>
      </Default>
    </Defaults_List></Defaults></Table></root>)");
    std::vector<DefaultEntry> rows = ParseCustomizationDefaultItemsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].item == "undershirt" && rows[0].wearOption == "CUST_WEAR_NORMAL" && rows[0].variant == "White");
    CHECK(rows[0].gender == "female");
    CHECK(rows[0].GenderOrDefault() == "female");
    CHECK(rows[0].raceIndex == 2);  // kDefaultItemRaceNames = {asian, black, hispanic, white}
    // Absent Gender -> the engine's explicit "either" sentinel (§8), distinct from both real genders.
    CHECK(!rows[1].gender.has_value());
    CHECK(rows[1].GenderOrDefault() == "either");
    CHECK(rows[1].colorPoolPresent);
    CHECK(rows[1].characterColorGrid.size() == 1 && rows[1].characterColorGrid[0].color == "Navy");
    CHECK(rows[1].makeupColorGrid.empty());
}

// ===========================================================================
// 9.1 customization_materials.xtbl
// ===========================================================================
void testCustomizationMaterials() {
    Document d = P(R"(<root><Table>
      <Cust_Material>
        <Name>cm_suit_default</Name>
        <Variables><Variable><Shader_Var_Name>DiffuseColor</Shader_Var_Name><Display_Name_Game>MAT_DIFFUSE</Display_Name_Game></Variable></Variables>
      </Cust_Material>
    </Table></root>)");
    std::vector<CustMaterial> rows = ParseCustomizationMaterialsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].variables.size() == 1);
    CHECK(rows[0].variables[0].shaderVarName == "DiffuseColor");
    CHECK(rows[0].variables[0].displayNameGame == "MAT_DIFFUSE");
}

// ===========================================================================
// 9.2 customization_normals.xtbl - default index 3 if unmatched (§9.2)
// ===========================================================================
void testCustomizationNormals() {
    Document d = P(R"(<root><Table>
      <Normals>
        <Race>Black</Race><Female>yes</Female>
        <Ideal>tex_ideal</Ideal><Muscular>tex_musc</Muscular><Skinny>tex_skinny</Skinny><Fat>tex_fat</Fat><Age>tex_age</Age>
      </Normals>
      <Normals><Race>Xyzzy</Race></Normals>
    </Table></root>)");
    std::vector<Normals> rows = ParseCustomizationNormalsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].raceIndex == 1);  // Black
    CHECK(rows[0].female.present && rows[0].female.value == true);
    CHECK(rows[0].ideal == "tex_ideal" && rows[0].age == "tex_age");
    CHECK(rows[1].raceIndex == 3);  // unmatched -> spec default 3, NOT -1 (unlike this project's usual convention)
}

// ===========================================================================
// 9.3 customization_compositing.xtbl
// ===========================================================================
void testCustomizationCompositing() {
    Document d = P(R"(<root><Table>
      <Composite_Layer>
        <Name>lips_base</Name>
        <diffuse>lips_d.tga</diffuse>
        <Slot>lips</Slot>
        <x_coord>10</x_coord><y_coord>20</y_coord><Alpha>0.5</Alpha><Layer>2</Layer>
        <Flags><Flag>Colorizable</Flag><Flag>No_diffuse</Flag></Flags>
        <Price>50</Price><Is_DLC>no</Is_DLC>
      </Composite_Layer>
    </Table></root>)");
    std::vector<CompositeLayer> rows = ParseCustomizationCompositingTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].diffuseTexture == "lips_d.tga");
    CHECK(!rows[0].normalTexture.has_value());
    CHECK(rows[0].xCoord.value == 10 && rows[0].yCoord.value == 20);
    CHECK(near(rows[0].alpha.value, 0.5f, 1e-6f));
    CHECK(rows[0].colorizable && rows[0].noDiffuse && !rows[0].playerCreation && !rows[0].showAlphaSelection);
    CHECK(rows[0].price.value == 50);
    CHECK(rows[0].isDlc.present && rows[0].isDlc.value == false);
}

// ===========================================================================
// 10. gang_customization.xtbl - sentinel-name-gated singleton (§10)
// ===========================================================================
void testGangCustomization() {
    Document d = P(R"(<root><Table>
      <GangCustomization>
        <Name>SomeOtherRow</Name>
      </GangCustomization>
      <GangCustomization>
        <Name>SR2_Player_Gang_Cust</Name>
        <gang_vehicles>
          <gang_vehicles>
            <Name>SaintsRide</Name>
            <Flags><Flag>locked</Flag></Flags>
            <Vehicles><Vehicle>Compton</Vehicle><Vehicle>Bootlegger</Vehicle></Vehicles>
          </gang_vehicles>
        </gang_vehicles>
        <PlayerDefaults><DefaultVehicles><Slot1>Compton</Slot1><Slot2>Bootlegger</Slot2><Slot3>Voxel</Slot3></DefaultVehicles></PlayerDefaults>
        <Gang_Signs><Pose><display_name>GANG_SIGN_1</display_name><animation><State>gang_sign</State><Animation>sign_01</Animation></animation></Pose></Gang_Signs>
      </GangCustomization>
    </Table></root>)");
    std::optional<GangCustomization> g = ParseGangCustomizationTable(d);
    CHECK(g.has_value());
    CHECK(g->name == "SR2_Player_Gang_Cust");
    CHECK(g->gangVehicles.size() == 1);
    CHECK(g->gangVehicles[0].name == "SaintsRide" && g->gangVehicles[0].locked);
    CHECK(g->gangVehicles[0].vehicles.size() == 2 && g->gangVehicles[0].vehicles[1] == "Bootlegger");
    CHECK(g->defaultVehicleSlot1 == "Compton" && g->defaultVehicleSlot3 == "Voxel");
    CHECK(g->gangSigns.size() == 1);
    CHECK(g->gangSigns[0].displayName == "GANG_SIGN_1");
    CHECK(g->gangSigns[0].animation.state == "gang_sign" && g->gangSigns[0].animation.animation == "sign_01");

    // No sentinel row present -> nullopt (the mechanism reads no other row, §10).
    Document noSentinel = P(R"(<root><Table><GangCustomization><Name>NotIt</Name></GangCustomization></Table></root>)");
    CHECK(!ParseGangCustomizationTable(noSentinel).has_value());
}

// ===========================================================================
// 11. customizable_action.xtbl
// ===========================================================================
void testCustomizableAction() {
    Document d = P(R"(<root><Table>
      <CustomizableActions>
        <ActionType>Insult</ActionType>
        <Action><State>taunt</State><Animation>insult_01</Animation></Action>
        <Team>Saints</Team>
        <LocalizedTag>CUST_INSULT_A</LocalizedTag>
      </CustomizableActions>
      <CustomizableActions>
        <ActionType>Compliment</ActionType>
        <Action><State>taunt</State><Animation>compliment_01</Animation></Action>
        <LocalizedTag>CUST_COMPLIMENT_K</LocalizedTag>
      </CustomizableActions>
    </Table></root>)");
    std::vector<CustomizableAction> rows = ParseCustomizableActionTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].action.state == "taunt" && rows[0].action.animation == "insult_01");
    CHECK(rows[0].team == "Saints");
    // The "TOUCH DOWN" language override (§11) is NOT applied - localizedTag stays raw.
    CHECK(rows[1].localizedTag == "CUST_COMPLIMENT_K");

    CustomizableActionSplit split = SplitCustomizableActions(rows);
    CHECK(split.insults.size() == 1 && split.compliments.size() == 1 && split.unmatched.empty());
}

// ===========================================================================
// 12.1 character_definitions.xtbl / 12.2 character_height.xtbl
// ===========================================================================
void testCharacterDefinitions() {
    Document d = P(R"(<root><Table><Character><Framework>main</Framework></Character><Character><Framework>dlc1</Framework></Character></Table></root>)");
    std::vector<CharacterDefinitionRow> rows = ParseCharacterDefinitionsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].framework == "main" && rows[1].framework == "dlc1");
}

void testCharacterHeight() {
    Document d = P(R"(<root><Table><Height_Class><Height>1.0</Height><Name>Average</Name></Height_Class></Table></root>)");
    std::vector<HeightClass> rows = ParseCharacterHeightTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].height.present && rows[0].height.value == 1.0f);
    CHECK(rows[0].name == "Average");
}

// ===========================================================================
// 13.1 character_customization_categories.xtbl / 13.3 character.xtbl - row
// locators only (no field schema recovered, §13.1/§13.3).
// ===========================================================================
void testCharacterCustomizationCategoryRows() {
    Document d = P(R"(<root><Table><category><anything>x</anything></category><category/></Table></root>)");
    std::vector<const Node*> rows = FindCharacterCustomizationCategoryRows(d);
    CHECK(rows.size() == 2);
}

void testCharacterRows() {
    // Same row tag "Character" as character_definitions.xtbl, but a
    // structurally separate file/loader (§13.3) - this reader still just
    // locates rows, no field is assumed.
    Document d = P(R"(<root><Table><Character><Whatever>1</Whatever></Character></Table></root>)");
    std::vector<const Node*> rows = FindCharacterRows(d);
    CHECK(rows.size() == 1);
}

// ===========================================================================
// 13.2 character_types.xtbl
// ===========================================================================
void testCharacterTypes() {
    Document d = P(R"(<root><Table><Type><Name>civilian_male</Name></Type><Type><Name>civilian_female</Name></Type></Table></root>)");
    std::vector<CharacterType> rows = ParseCharacterTypesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "civilian_male");
}

// ===========================================================================
// 14.1 player_creation.xtbl
// ===========================================================================
void testPlayerCreation() {
    Document d = P(R"(<root><Table>
      <Morph_Set>
        <Name>Nose</Name>
        <DisplayName>MORPH_NOSE</DisplayName>
        <Morph_Infos><Morph_Info/><Morph_Info/></Morph_Infos>
      </Morph_Set>
    </Table></root>)");
    std::vector<MorphSet> rows = ParsePlayerCreationTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].categoryIndex == 6);  // kMorphSetCategoryNames[6] == "Nose"
    CHECK(rows[0].morphInfoRows.size() == 2);
}

// ===========================================================================
// 14.2 player_creation_morph_groups.xtbl
// ===========================================================================
void testPlayerCreationMorphGroups() {
    Document d = P(R"(<root><Table>
      <morph_group>
        <Name>NoseGroup</Name>
        <morph_list>
          <morph_item><morph_name>nose_width</morph_name><display_type>gender</display_type></morph_item>
          <morph_item><morph_name>nose_length</morph_name></morph_item>
        </morph_list>
      </morph_group>
    </Table></root>)");
    std::vector<MorphGroup> rows = ParsePlayerCreationMorphGroupsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].morphList.size() == 2);
    CHECK(rows[0].morphList[0].isGenderType);
    CHECK(!rows[0].morphList[1].isGenderType);
}

// ===========================================================================
// 15.1 player_creation_normal_maps.xtbl
// ===========================================================================
void testPlayerCreationNormalMaps() {
    Document d = P(R"(<root><Table>
      <Normal_Map_Settings>
        <Name>Female</Name>
        <Morph_Sliders><Morph_Slider>
          <Slider_Name>weight</Slider_Name>
          <Slider_Keys>
            <Slider_Key><Slider_Position>0.0</Slider_Position><Normal_Map_Strength>0.0</Normal_Map_Strength></Slider_Key>
            <Slider_Key><Slider_Position>1.0</Slider_Position><Normal_Map_Strength>1.0</Normal_Map_Strength></Slider_Key>
          </Slider_Keys>
        </Morph_Slider></Morph_Sliders>
      </Normal_Map_Settings>
    </Table></root>)");
    std::vector<NormalMapSettings> rows = ParsePlayerCreationNormalMapsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].bodyTypeIndex == 1);  // kBodyTypeNames[1] == "Female"
    CHECK(rows[0].morphSliders.size() == 1);
    CHECK(rows[0].morphSliders[0].sliderKeys.size() == 2);
    CHECK(rows[0].morphSliders[0].sliderKeys[1].normalMapStrength.value == 1.0f);
}

// ===========================================================================
// 15.2 player_creation_hair_color.xtbl
// ===========================================================================
void testPlayerCreationHairColor() {
    Document d = P(R"(<root><Table>
      <Hair_Color>
        <Name>Black1</Name><Display_Name>HAIR_BLACK1</Display_Name>
        <Shaderball>hair_sb</Shaderball><swatch_image>hair_sw</swatch_image>
        <Hue>0.5</Hue><Saturation_light>0.8</Saturation_light><Saturation_dark>0.2</Saturation_dark>
        <Specular_alpha>1.0</Specular_alpha><Brightness>0.5</Brightness><Brightness_bias>0.1</Brightness_bias>
        <Specular_bright>0.9 0.9</Specular_bright><Specular_dark>0.1 0.1</Specular_dark>
      </Hair_Color>
    </Table></root>)");
    std::vector<HairColor> rows = ParsePlayerCreationHairColorTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].hue.has_value() && rows[0].hue.value() == 0.5f);
    // Specular_bright/dark kept as RAW TEXT - arity (vec2 vs vec3) not narrowed (§15.2, §22.9).
    CHECK(rows[0].specularBrightRaw == "0.9 0.9");
    CHECK(!rows[0].swatchLabel.has_value());
}

// ===========================================================================
// 15.3 player_creation_skin_colors.xtbl
// ===========================================================================
void testPlayerCreationSkinColors() {
    Document d = P(R"(<root><Table>
      <Entry>
        <Name>Tan1</Name>
        <Unmasked_Hue>0.1</Unmasked_Hue><Unmasked_Saturation>0.2</Unmasked_Saturation><Unmasked_Brightness>0.3</Unmasked_Brightness>
        <Hue>0.4</Hue><Saturation>0.5</Saturation><Brightness>0.6</Brightness>
        <Specular_Alpha>0.7</Specular_Alpha><Fresnel_Alpha>0.8</Fresnel_Alpha><Specular_Power>16</Specular_Power>
      </Entry>
    </Table></root>)");
    std::vector<SkinColorEntry> rows = ParsePlayerCreationSkinColorsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].unmaskedHue.value == 0.1f);
    CHECK(rows[0].fresnelAlpha.value == 0.8f);  // NOT present on hair_color's record (§15.3)
    CHECK(!rows[0].displayName.has_value());
}

// ===========================================================================
// 16.1 player_master_sliders.xtbl - shipped Table AND TableTemplates are
// BOTH empty at retail (§16.1); schema still exercised here.
// ===========================================================================
void testPlayerMasterSliders() {
    Document empty = P(R"(<root><Table></Table></root>)");
    CHECK(ParsePlayerMasterSlidersTable(empty).empty());

    Document d = P(R"(<root><Table>
      <MasterSliders>
        <Name>Weight</Name><DisplayName>SLIDER_WEIGHT</DisplayName>
        <Slider><Name>weight_slider</Name><DisplayName>WEIGHT</DisplayName><InitialValue>0.5</InitialValue><MorphList>fat,skinny</MorphList></Slider>
      </MasterSliders>
    </Table></root>)");
    std::vector<MasterSliderCategory> rows = ParsePlayerMasterSlidersTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].sliders.size() == 1);
    CHECK(rows[0].sliders[0].initialValue.has_value() && rows[0].sliders[0].initialValue.value() == 0.5f);
    CHECK(rows[0].sliders[0].morphListRaw == "fat,skinny");  // kept raw, delimiter not confirmed by the spec
}

// ===========================================================================
// 16.2 player_presets.xtbl
// ===========================================================================
void testPlayerPresets() {
    Document d = P(R"(<root><Table>
      <Preset>
        <Name>DefaultMale</Name><Race>white</Race><Gender>male</Gender><Default>yes</Default>
        <DisplayName>PRESET_DEFAULT</DisplayName>
        <Hair>basic_hair</Hair><Hair_Length>0.5</Hair_Length>
        <Hair_Color_Primary>Black1</Hair_Color_Primary><Hair_Color_Secondary>Black1</Hair_Color_Secondary>
        <Skin_Color>Tan1</Skin_Color>
        <Composites>
          <Composite><Layer>lips_base</Layer></Composite>
          <Composite><Layer>eyes_base</Layer><Color>0.2 0.3 0.4</Color></Composite>
        </Composites>
        <Preset_Grid><Preset_Element><Morph_Name>nose_width</Morph_Name><Value>0.3</Value></Preset_Element></Preset_Grid>
        <RegionalPresets><RegionalPreset><Category>Face</Category><Preset>Broad</Preset></RegionalPreset></RegionalPresets>
      </Preset>
    </Table></root>)");
    std::vector<Preset> rows = ParsePlayerPresetsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].isDefault.present && rows[0].isDefault.value == true);
    CHECK(rows[0].hair == "basic_hair");
    CHECK(rows[0].composites.size() == 2);
    // Color absent -> spec-stated concrete default: opaque white (§16.2).
    CHECK(!rows[0].composites[0].color.has_value());
    ColorText c0 = rows[0].composites[0].ColorOrDefault();
    CHECK(c0.r == 1.0f && c0.g == 1.0f && c0.b == 1.0f && c0.a == 1.0f);
    CHECK(rows[0].composites[1].color.has_value() && rows[0].composites[1].color->g == 0.3f);
    CHECK(rows[0].presetGrid.size() == 1 && rows[0].presetGrid[0].morphName == "nose_width");
    CHECK(rows[0].regionalPresets.size() == 1 && rows[0].regionalPresets[0].category == "Face");
}

// ===========================================================================
// 16.3 player_regional_presets.xtbl
// ===========================================================================
void testPlayerRegionalPresets() {
    Document d = P(R"(<root><Table>
      <RegionalPresets>
        <Name>Face</Name><DisplayName>REGION_FACE</DisplayName>
        <Presets><Preset>
          <Name>Broad</Name><DisplayName>PRESET_BROAD</DisplayName><figure>broad_figure</figure>
          <MorphTargets><MorphTarget><Morph>jaw_width</Morph><Target>0.6</Target></MorphTarget></MorphTargets>
        </Preset></Presets>
      </RegionalPresets>
    </Table></root>)");
    std::vector<RegionalPresetsGroup> rows = ParsePlayerRegionalPresetsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].presets.size() == 1);
    CHECK(rows[0].presets[0].figure == "broad_figure");
    CHECK(rows[0].presets[0].morphTargets.size() == 1);
    CHECK(rows[0].presets[0].morphTargets[0].morph == "jaw_width");
    CHECK(rows[0].presets[0].morphTargets[0].target.value == 0.6f);
}

// ===========================================================================
// 17. player_cust_shot_map.xtbl
// ===========================================================================
void testPlayerCustShotMap() {
    Document d = P(R"(<root><Table><NewEntity><Name>mannequin_01</Name><Anim_Pos>store_shot_1</Anim_Pos></NewEntity></Table></root>)");
    std::vector<NewEntity> rows = ParsePlayerCustShotMapTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name == "mannequin_01" && rows[0].animPos == "store_shot_1");
}

}  // namespace

int main() {
    try {
        testColorPool();
        testColorBalance();
        testItemsColorPool();
        testNpcColorPalette();
        testCustomizationItems();
        testCustomizationStores();
        testCustomizationSlots();
        testCustomizationSlotDefaults();
        testCustomizationCategories();
        testCustomizationFlags();
        testCustomizationIcons();
        testCustomizationDefaultItems();
        testCustomizationMaterials();
        testCustomizationNormals();
        testCustomizationCompositing();
        testGangCustomization();
        testCustomizableAction();
        testCharacterDefinitions();
        testCharacterHeight();
        testCharacterCustomizationCategoryRows();
        testCharacterRows();
        testCharacterTypes();
        testPlayerCreation();
        testPlayerCreationMorphGroups();
        testPlayerCreationNormalMaps();
        testPlayerCreationHairColor();
        testPlayerCreationSkinColors();
        testPlayerMasterSliders();
        testPlayerPresets();
        testPlayerRegionalPresets();
        testPlayerCustShotMap();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}
