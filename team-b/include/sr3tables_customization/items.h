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
//  1. Mesh_Information / Female_Mesh_Filename: RESOLVED 2026-10-02 (spec-
//     tables-customization.md §4.2, re-derived from the executable,
//     `func_0x00829650.txt`/`func_0x009fd8c0.txt` read in full). `Female_
//     Mesh_Filename` is CONFIRMED absent from `FUN_00829650`'s own
//     decompile (the function this header's WearOptionEntry models) - it
//     genuinely is NOT one of this reader's fields, not a gap in an earlier
//     pass's decompile. It IS read, but by a separate composer function
//     (`FUN_009FD8C0`, called once per item right after the Wear_Option
//     loop, building the `custmesh_<N>`/`custmesh_<N>f` bundle names) -
//     i.e. the two specs were both right about different functions, no
//     real conflict. sr3customization::MeshInformation (built from
//     spec-customization-data.md, a different layer / different spec) IS
//     the right place for Female_Mesh_Filename and already models it
//     correctly; THIS header correctly omits it. No code change needed on
//     either side - this discrepancy is now a confirmed non-bug.
//  2. Shader_Type: RESOLVED 2026-10-02 (spec-tables-customization.md §4.3,
//     re-derived from the executable, `FUN_00829650` read in full):
//     Shader_Type's parent is CONFIRMED to be Material_Element ("the
//     `FUN_00daba40(elementHandle, "Shader_Type")` call sits inside the
//     per-Material_Element loop, using that loop's own element handle") -
//     settling the conflict IN FAVOUR of this header's own reading
//     (MaterialElementEntry::shaderType, below). sr3customization::
//     CustomizationVariant used to model it once at the Variant level -
//     that was the actual bug; it has been corrected to match this header
//     (see include/sr3customization/customization.h's MaterialElement).
//  3. Default_Colors_Grid: still differs from the older reader's schema,
//     unaffected by the 2026-10-02 pass (no new text on this specific
//     item-vs-variant modelling choice) - see DefaultColorEntry below for
//     the now-CONFIRMED clamp/back-fill quirk this header still does not
//     compute (it would need the per-variant matched-material cross-
//     reference neither reader models).
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
    std::optional<std::string> maleMeshFilename;  // Mesh_Information > Male_Mesh_Filename > Filename (the
                                                    // .cmeshx name - the mesh-string half of the
                                                    // custmesh_<N>.str2_pc bundle recipe, spec-customization-
                                                    // data.md §5 / this spec §4.2). CORRECTED 2026-10-02 (§4.2,
                                                    // re-derived from the executable, `func_0x00da7930.txt`
                                                    // read in full): `FUN_00da7930` is NOT a hash - it is
                                                    // exactly `_strncpy(dest, src, n); dest[n-1] = '\0';`, a
                                                    // plain bounded string copy, used only to make a private
                                                    // NUL-terminated copy of this element's text. Inside
                                                    // `FUN_00829650`'s own Wear_Option loop, Male_Mesh_Filename
                                                    // is fetched via `FUN_00daba10` (plain text fetch) and
                                                    // bounded-copied via `FUN_00da7930` with NO hash call
                                                    // applied to it at all - whatever consumes it as a hash
                                                    // (the composer `FUN_009FD8C0`, via `FUN_00d9e8b0`) does so
                                                    // downstream, not here. This reader already matched that
                                                    // behaviour (OptText, no hash computed) - comment only, no
                                                    // behaviour change.
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
    std::optional<std::string> shaderType;   // Shader_Type - CONFIRMED 2026-10-02 a Material_Element child (see
                                               // DISCREPANCY 2 above); modelled per-element here, matching the
                                               // real loader
    // The hardcoded __stricmp(name,"cm_suit_deckersuit") special case (4.3)
    // is load-time control flow (feeds FUN_00828de0), not an XML field - not modelled.
};

struct VariantEntry {
    std::optional<std::string> name;             // Name (localized + hashed)
    std::optional<int32_t> price;                 // Price - "default to the item's own Base_Price ... when the
                                                    // authored value equals a sentinel" (4.3). CONFIRMED 2026-10-02
                                                    // (re-derived from the executable): the sentinel is simply
                                                    // plain 0/0.0 - "Price is read as a float and replaced with
                                                    // the item's Base_Price only when the read-back value is
                                                    // exactly 0.0" - i.e. the fallback is absent/default, not a
                                                    // special marker value. Kept as read here (the fallback
                                                    // against the owning item's Base_Price is a load-time
                                                    // transform across two structs, not modelled) - callers can
                                                    // apply `price.value_or(0) == 0 ? item.basePrice : *price`
                                                    // themselves now that the rule is confirmed.
    std::optional<int32_t> respectBonus;           // Respect_Bonus - same fallback convention as Price (sentinel
                                                    // is plain 0, confirmed 2026-10-02, same treatment as above)
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
//
// CONFIRMED 2026-10-02 (§4.4, re-derived from the executable, `FUN_00829650`
// read in full) - the clamp/back-fill quirk this section's header comment
// references is now fully pinned down, still NOT computed here: the
// "target" count is specifically Variant[0]'s own `cm_suit_deckersuit`-match
// slot's +0x08 field (a cross-reference into customization_materials.xtbl's
// loaded array via the hardcoded stricmp special case, §4.3 - neither
// reader in this project models that match at all), read only when that
// slot is non-zero. The real sequence: (1) if the item's own accumulated
// colour count is LESS than the target, slots from the current count up to
// the target are back-filled from a fixed Crimson-headed name list; (2)
// regardless, the record's STORED slot count is then set to MIN(original
// accumulated count, target) - using the ORIGINAL count, not the count
// after back-fill. Net effect: when original < target, back-filled array
// entries are written into memory but the stored count reverts to the
// smaller original value anyway, orphaning them - a genuine engine quirk,
// not a guess. This reader still cannot reproduce it: it would need the
// cm_suit_deckersuit-match cross-reference (not modelled, see
// MaterialElementEntry's own comment) as well as the §2 colour-pool
// resolution this project does not perform. DefaultColorEntry below stays
// the raw, unclamped, un-back-filled authored list.
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
    bool npcOnly = false;              // "npc only" - not one of the 3 recovered bit positions. CORRECTED
                                        // 2026-10-02 (§4.1, re-derived from the executable, `FUN_00829650` read
                                        // in full): the "unmatched Slot still accepted iff npcOnly" exception
                                        // this field used to be justified by is WRONG for THIS table - an
                                        // unmatched Slot causes an unconditional `goto` skip in FUN_00829650
                                        // with no Flags/npc-only check anywhere near that code. That exception
                                        // is real, but belongs to customization_slots.xtbl's OWN reader
                                        // (FUN_009fca30, §6.1), not to this item reader. `npcOnly` is kept as a
                                        // raw flag (real, confirmed bit) but no longer described as gating Slot
                                        // acceptance here.
                                        //
                                        // Also newly found (same dump, not modelled): a SECOND, previously-
                                        // undocumented skip gate immediately after Slot resolution -
                                        // `FUN_009fd5f0` is called on the resolved slot index and the row is
                                        // skipped (same goto) if bit 2 of its returned byte is set. Its exact
                                        // meaning was not traced past that; flagged, not guessed, not modelled.

    std::optional<std::string> slot;   // Slot - text, hashed then resolved against the 24-name canonical slot
                                        // array (§6.1, CanonicalSlotNames()). An unmatched Slot causes the real
                                        // loader to unconditionally skip the row (confirmed 2026-10-02, see
                                        // `npcOnly` above) - NOT enforced as a drop here regardless (see
                                        // slotIndex, which is simply -1 when unmatched, the same EnumIndex
                                        // convention this project uses elsewhere, e.g.
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
// CONFIRMED 2026-10-02 (spec-tables-customization.md §4.1, and §5.2 for the store caps 255/73): every compare
// operator in this document was re-derived from the executable and turned out to be checked BEFORE storing a
// row, against the not-yet-incremented count - exactly matching the `>= N` coding already used throughout this
// library (see the CONFIRMED comments in each .cpp). Only the normals cap (materials.h, §9.2) can change a
// result on real data.
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
