#pragma once

// sr3tables_customization/materials.h - customization_default_items.xtbl
// (spec-tables-customization.md §8), customization_materials.xtbl (§9.1),
// customization_normals.xtbl (§9.2) and customization_compositing.xtbl
// (§9.3). Included from sr3tables_customization/tables.h.
//
// Source: spec-tables-customization.md, the ONLY document consulted for this
// file's schema. Built entirely on include/sr3xtbl/xtbl.h.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// 8. customization_default_items.xtbl. [CONFIRMED - disassembly + empirical]
// Root shape: Table > Defaults > Defaults_List > Default[] - the table's own
// direct-child count is 1 (the single Defaults wrapper); the real row array
// is two levels further in (§8; §21's validation explicitly calls this out
// as "a real, correctly-parsed nesting artifact, not an empty table").
// Each Default is an 80-byte (0x50) record, cap not explicit (heap-sized to
// the counted element count).
// ===========================================================================

// Race: "a 4-name enum specific to this table" (§8) - distinct vocabulary
// AND distinct underlying array from customization_normals.xtbl's
// Asian/Black/Hispanic/Caucasian (§9.2, kNormalsRaceNames below), despite
// being "same concept, different authored vocabulary".
inline constexpr std::array<std::string_view, 4> kDefaultItemRaceNames = {"asian", "black", "hispanic", "white"};

// Color_Pool > Character_Color_Grid|Makeup_Color_Grid > Color_Element: each
// Color_Element's own "Color" child holds a colour NAME, resolved through
// the shared §2 pool helper (FUN_00746d60) - the same name-lookup
// convention as customization_items.xtbl's Default_Color entries (items.h).
struct ColorPoolOverrideEntry {
    std::optional<std::string> color;  // Color_Element > Color (a colour-pool entry NAME, not an RGBA literal)
};

struct DefaultEntry {
    std::optional<std::string> item;        // Item -> customization_items.xtbl array (FUN_008282c0 - the SAME
                                              // helper player_presets.xtbl's Hair field uses, §16.2)
    std::optional<std::string> wearOption;   // Wear_Option -> that item's own Wear_Option array
    std::optional<std::string> variant;      // Variant -> that item's own Variant array
    // The combined Item+Wear_Option+Variant lookup id (FUN_009fd5b0) is
    // derived at load time, not a separate XML element - not modelled.

    std::optional<std::string> gender;  // Gender: "male"/"female"/absent - absent is an explicit "either"
                                          // sentinel (the value 3), distinct from both real genders (§8)
    std::string GenderOrDefault() const { return gender.value_or("either"); }

    std::optional<std::string> race;  // Race -> kDefaultItemRaceNames
    int raceIndex = -1;                // index into kDefaultItemRaceNames, or -1 if unmatched/absent

    // Optional colour-pool override (§8): "if the Default itself has a
    // Color_Pool > Character_Color_Grid (or, failing that, Makeup_Color_Grid)
    // child, its Color_Element > Color entries ... REPLACE the item's own
    // default colours for this specific default-selection row" (clamped to
    // the larger of the two counts - a load-time transform, not modelled).
    // The item's own colour-slot carry-over (when no override is present)
    // is likewise a load-time copy from the item's Default_Colors_Grid, not
    // a field of THIS row - not modelled here.
    bool colorPoolPresent = false;
    std::vector<ColorPoolOverrideEntry> characterColorGrid;  // Color_Pool > Character_Color_Grid > Color_Element[]
    std::vector<ColorPoolOverrideEntry> makeupColorGrid;      // Color_Pool > Makeup_Color_Grid > Color_Element[]
                                                                 // (fallback used only "failing that" the first)
};
DefaultEntry ParseDefaultEntry(const Node* row);
// Descends Table > Defaults > Defaults_List before iterating `Default` rows
// (§8's confirmed nesting).
std::vector<DefaultEntry> ParseCustomizationDefaultItemsTable(const Document& doc);

// ===========================================================================
// 9.1 customization_materials.xtbl. [CONFIRMED - disassembly + empirical]
// Each Cust_Material is a 12-byte (0xc) record.
// ===========================================================================
struct MaterialVariable {
    std::optional<std::string> shaderVarName;    // Shader_Var_Name (hashed)
    std::optional<std::string> displayNameGame;   // Display_Name_Game (localized)
};
struct CustMaterial {
    std::optional<std::string> name;  // Name (hashed) - the registry §4.3's Variant > Material_List >
                                        // Material_Element > Material names resolve against. The row named
                                        // "SR2Skin1" gets its own dedicated global pointer (a Saints Row 2
                                        // legacy-naming callback, the same convention as §10's
                                        // "SR2_Player_Gang_Cust" sentinel) - derived dispatch, not modelled;
                                        // the row's Name is kept uniformly regardless.
    std::vector<MaterialVariable> variables;  // Variables > Variable[]
};
CustMaterial ParseCustMaterial(const Node* row);
std::vector<CustMaterial> ParseCustomizationMaterialsTable(const Document& doc);

// ===========================================================================
// 9.2 customization_normals.xtbl - per-race/gender body normal-map sets.
// [CONFIRMED - disassembly + empirical] HARD CAP 8 rows - the real data
// ships exactly 8 (at capacity, not overflowing, §9.2/§21).
// ===========================================================================
inline constexpr std::array<std::string_view, 4> kNormalsRaceNames = {"Asian", "Black", "Hispanic", "Caucasian"};

struct Normals {
    std::optional<std::string> race;  // Race -> kNormalsRaceNames
    int raceIndex = 3;                 // §9.2: "default index 3 if unmatched" - note this default is NOT -1,
                                         // unlike this project's usual EnumIndex convention elsewhere; matches
                                         // the spec's explicit statement for THIS table only
    Always<bool> female;                 // Female

    // Five body-composite normal-map bindings, each resolved via
    // FUN_00db1160 then baked into a runtime texture-load request tagged
    // "pcust" (§9.2) - the tag/bake is derived, not modelled; raw texture
    // reference text is kept.
    std::optional<std::string> ideal;
    std::optional<std::string> muscular;
    std::optional<std::string> skinny;
    std::optional<std::string> fat;
    std::optional<std::string> age;
};
Normals ParseNormals(const Node* row);
// Stops after the 8th accepted row (the real loader's hard cap, §9.2) -
// not modelled as an error, only as a stopping point (same convention as
// items.h's 858-row cap).
// LABEL: spec-tables-customization.md §9.2 / §21 mark the compare operator [OPEN]; `>= 8` is our assumption and
// this is the one cap that can change a result on real data (the shipped file has exactly 8 rows).
std::vector<Normals> ParseCustomizationNormalsTable(const Document& doc);

// ===========================================================================
// 9.3 customization_compositing.xtbl - texture layer-compositing recipe.
// [CONFIRMED - disassembly + empirical] HARD CAP 600 rows. Each
// Composite_Layer is a 68-byte (0x44) record.
// ===========================================================================
struct CompositeLayer {
    std::optional<std::string> name;           // Name (hashed, FUN_00db12c0)
    std::optional<std::string> displayName;     // Display_Name (optional)
    std::optional<std::string> diffuseTexture;  // diffuse (required texture ref)
    std::optional<std::string> normalTexture;   // normal (optional texture ref)

    // Slot: matched against a SEPARATE 34-entry name array (`0x0130b7f0`),
    // distinct from both the §6.1 24-slot array and the §9.2 4-race array
    // (§9.3). The spec gives this array only in abbreviated "x/y/z x3/x6"
    // shorthand (lips, eyes, tattoo upper/lower/entire arm left/right x6,
    // ...), not a full literal dump the way the 24-slot array or the 4-name
    // race arrays are given - so NO fixed name array/enum index is modelled
    // here (fabricating the exact per-entry spelling/order from shorthand
    // would violate this task's "don't guess" rule); raw text only.
    std::optional<std::string> slot;

    Always<int32_t> xCoord, yCoord;  // x_coord, y_coord (compositing-sheet placement)
    Always<float> alpha;              // Alpha
    Always<int32_t> layer;            // Layer

    // Flags: resolved against a 4-entry array Colorizable/Player_creation/
    // Show_alpha_selection/No_diffuse (`0x0130b87c`). The spec notes the
    // STORED bit order is remapped (authored bit0<->bit1, bits2/3 pass
    // through, §9.3) - that remap only matters when reproducing the row's
    // own internal flags DWORD, which this reader does not attempt; these
    // four booleans are plain named Flags>Flag membership checks.
    bool colorizable = false;
    bool playerCreation = false;
    bool showAlphaSelection = false;
    bool noDiffuse = false;

    Always<int32_t> price;  // Price
    Always<bool> isDlc;      // Is_DLC
};
CompositeLayer ParseCompositeLayer(const Node* row);
std::vector<CompositeLayer> ParseCustomizationCompositingTable(const Document& doc);

}  // namespace sr3tables_customization
