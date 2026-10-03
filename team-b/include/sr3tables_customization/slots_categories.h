#pragma once

// sr3tables_customization/slots_categories.h - customization_slots.xtbl
// (spec-tables-customization.md §6.1), customization_slot_defaults.xtbl
// (§6.2), customization_categories.xtbl (§7.1), customization_flags.xtbl
// (§7.2) and customization_icons.xtbl (§7.3). Included from
// sr3tables_customization/tables.h, BEFORE items.h (items.h's Slot
// resolution uses kCanonicalSlotNames, defined below).
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

// A `State`/`Animation` pair resolved via `FUN_004bf810` to an anim-state
// handle - shared across customization_categories.xtbl (§7.1's
// Male_Animations/Female_Animations), gang_customization.xtbl (§10's
// Gang_Signs > Pose animation) and customizable_action.xtbl (§11's Action).
// The resolution itself is derived (an opaque runtime handle); the State/
// Animation text pair is what's kept.
struct StateAnimation {
    std::optional<std::string> state;      // State
    std::optional<std::string> animation;  // Animation
};

// ===========================================================================
// 6.1 customization_slots.xtbl - per-slot memory budgets/flags for the 24
// canonical slot names. [CONFIRMED - disassembly + empirical]
// ===========================================================================
// The 24 canonical slot names (`PTR_DAT_0130b780`, §6.1) - the fixed,
// data-segment name array every `Slot` reference in this whole table group
// resolves against (§4's Customization_Item.Slot, §6.2's Slot_Default.Slot,
// §7.1's Slots_Grid/Obscure_Grid). Index = the id used everywhere else.
inline constexpr std::array<std::string_view, 24> kCanonicalSlotNames = {
    "body", "head hair", "beard", "eyebrows", "mouth", "headwear", "facewear", "eyewear",
    "ear piercings", "long chain", "bra", "underwear", "upper body", "lower body", "shoes",
    "left wrist", "right wrist", "gloves", "face piercings", "suit", "hat hair combined",
    "privacy bar top", "privacy bar bottom", "parachute",
};

struct SlotEntry {
    std::optional<std::string> name;  // Name -> resolved against kCanonicalSlotNames; an unmatched name is NOT
                                        // an error (index -1) - the real loader still checks that row's `npc
                                        // only` flag and continues either way (§6.1, open item - what happens
                                        // to the row's OTHER fields in the unmatched case wasn't traced further
                                        // by the spec, §22.4); this reader always parses/returns every row's
                                        // fields regardless of match, leaving `slotIndex` to tell the caller
    int slotIndex = -1;                // index into kCanonicalSlotNames, or -1 if unmatched

    // CPU_Size / GPU_Size: "authored value inflated by value + value/8 (a
    // fixed 12.5% headroom margin) before being stored" (§6.1) - kept as
    // read (raw, pre-inflation) PLUS a helper applying the documented
    // transform, matching this project's `...OrDefault()` convention for a
    // spec-stated concrete computation (sr3tables_environment/tables.h).
    // Width not stated by the spec beyond "authored value"; modelled as
    // int32_t, matching a memory-budget-sized field elsewhere in this group
    // (a documented judgement call, same footing as this project's other
    // "type not distinguished by the spec beyond ..." calls).
    Always<int32_t> cpuSizeRaw;  // CPU_Size, as authored
    Always<int32_t> gpuSizeRaw;  // GPU_Size, as authored
    int32_t CpuSizeInflated() const { return cpuSizeRaw.present ? cpuSizeRaw.value + cpuSizeRaw.value / 8 : 0; }
    int32_t GpuSizeInflated() const { return gpuSizeRaw.present ? gpuSizeRaw.value + gpuSizeRaw.value / 8 : 0; }

    // Flags > Flag membership: 3 bits (§6.1).
    bool required = false;        // "required" (0x1)
    bool npcOnly = false;         // "npc only" (0x2)
    bool removeOnOutfit = false;  // "remove on outfit" (0x4)

    Always<int32_t> renderOrder;   // Render_Order
};
SlotEntry ParseSlotEntry(const Node* row);
std::vector<SlotEntry> ParseCustomizationSlotsTable(const Document& doc);

// ===========================================================================
// 6.2 customization_slot_defaults.xtbl - shipped <Table> is EMPTY.
// [CONFIRMED - disassembly + empirical, this doc's own application of the
// task brief's central lesson: real content for this file lives only under
// <TableTemplates>, never counted/trusted as real rows - §6.2, §21]
// ===========================================================================
// "Structurally this table maps a Slot name (24-entry array) + an Item name
// (customization_items.xtbl array) + a Variant name (that item's own
// Variant array) to a default-selection record" (§6.2) - that MECHANISM
// sentence is the only CONFIRMED field-level source used here. The
// TableTemplates EXAMPLE rows additionally show `Name`/`Mesh` fields, but
// per this project's/this spec's own governing methodology TableTemplates
// content is not real authored data and is not trusted as schema evidence -
// `Name`/`Mesh` are therefore deliberately NOT modelled below.
struct SlotDefaultEntry {
    std::optional<std::string> slot;     // Slot -> kCanonicalSlotNames
    std::optional<std::string> item;      // Item -> customization_items.xtbl CustomizationItemEntry.name
    std::optional<std::string> variant;   // Variant -> that item's own Variant name
};
SlotDefaultEntry ParseSlotDefault(const Node* row);
// Row element tag: the spec's own mechanism paragraph (§6.2) does not name
// one; "Slot_Default" is the TableTemplates EXAMPLE's tag, used here only as
// the naming CONVENTION to scan for (matching every other table in this
// document's wrapper/row naming pattern) - not as trusted template CONTENT.
// Since the shipped <Table> node is confirmed empty, this always returns an
// empty vector against real shipped data (§21) - included for completeness
// and for any future/modded archive that does populate the table.
std::vector<SlotDefaultEntry> ParseCustomizationSlotDefaultsTable(const Document& doc);

// ===========================================================================
// 7.1 customization_categories.xtbl. [CONFIRMED - disassembly + empirical]
// ===========================================================================
struct ObscureElement {
    std::optional<std::string> slot;  // Slot -> kCanonicalSlotNames
    std::optional<std::string> flag;   // Flag -> customization_flags.xtbl's 39-entry registry (§7.2) - the SAME
                                         // shared flag vocabulary §4.2's Wear_Option Active/Required/Incompatible
                                         // flags resolve against
};
struct Category {
    std::optional<std::string> name;          // Name (category-specific hash helper FUN_00a74910)
    std::optional<std::string> displayName;    // DisplayName
    std::vector<std::string> slotsGrid;          // Slots_Grid > Slot_Element[] (own text -> kCanonicalSlotNames)
    std::vector<ObscureElement> obscureGrid;      // Obscure_Grid > Obscure_Element[]
    std::optional<StateAnimation> maleAnimations;    // Male_Animations
    std::optional<StateAnimation> femaleAnimations;  // Female_Animations
    std::optional<std::string> noWearString;          // No_Wear_String (localized; shown when nothing is
                                                        // equipped in this category)
};
Category ParseCategory(const Node* row);
std::vector<Category> ParseCustomizationCategoriesTable(const Document& doc);

// ===========================================================================
// 7.2 customization_flags.xtbl - the shared flag-name registry.
// [CONFIRMED - disassembly + empirical]
// ===========================================================================
struct FlagEntry {
    std::optional<std::string> name;  // Name (hashed into the 39-entry array every Active/Required/
                                        // Incompatible_Flag and Obscure_Element.Flag reference resolves
                                        // against, §4.2/§7.1). Four names get dedicated sentinel globals
                                        // instead of a generic array slot - BLING_COLLIDER_ACTIVE, "HAT
                                        // ACTIVE", "PIMP COAT", "flasher coat" (§7.2) - that fast-path
                                        // caching is derived/runtime, not modelled; the row's Name is kept
                                        // uniformly regardless of which path it takes. CONFIRMED 2026-10-02
                                        // (re-derived from the executable): the exact name->global mapping is
                                        // now known (HAT ACTIVE/PIMP COAT/flasher coat -> 3 dedicated index
                                        // caches; BLING_COLLIDER_ACTIVE belongs to a wholly separate 4-entry
                                        // checklist, 2 of 4 names still unknown) - still not modelled, no
                                        // behaviour change, this is purely a derived/runtime mechanism.
};
FlagEntry ParseFlagEntry(const Node* row);
std::vector<FlagEntry> ParseCustomizationFlagsTable(const Document& doc);

// ===========================================================================
// 7.3 customization_icons.xtbl. [CONFIRMED - disassembly + empirical]
// ===========================================================================
struct Icon {
    std::optional<std::string> name;          // Name (same FUN_00a74910 helper as §7.1's categories)
    std::optional<std::string> displayName;    // DisplayName
    std::vector<std::string> categoryElements;  // Categories > Category_Element[] (own text; resolved via
                                                  // FUN_00828340, PRESUMED against §7.1's category array by
                                                  // naming-convention proximity only - not independently
                                                  // confirmed, §7.3/§22.5 OPEN item)
    Always<bool> outfitsOnly;                    // Outfits_Only
};
Icon ParseIcon(const Node* row);
std::vector<Icon> ParseCustomizationIconsTable(const Document& doc);

}  // namespace sr3tables_customization
