#pragma once

// sr3tables_customization/gang_actions.h - gang_customization.xtbl
// (spec-tables-customization.md §10) and customizable_action.xtbl (§11).
// Included from sr3tables_customization/tables.h (after
// slots_categories.h, which declares the shared StateAnimation type).
//
// Source: spec-tables-customization.md, the ONLY document consulted for this
// file's schema. Built entirely on include/sr3xtbl/xtbl.h.

#include <optional>
#include <string>
#include <vector>

#include "sr3tables_customization/slots_categories.h"  // StateAnimation
#include "sr3xtbl/xtbl.h"

namespace sr3tables_customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// 10. gang_customization.xtbl. [CONFIRMED - disassembly + empirical]
// ===========================================================================
struct GangVehicleGroup {
    std::optional<std::string> name;  // gang_vehicles (the row tag; wrapper and row share the identical tag
                                        // name, §10) > Name (hashed)
    bool locked = false;                // Flags > Flag membership, "locked" - the real loader sets two
                                          // identical destination bytes (+8/+9) for this one bit; modelled as
                                          // one boolean here (a storage-duplication detail, not a schema fact)
    std::vector<std::string> vehicles;   // Vehicles > Vehicle[] (own text -> the global vehicle-info table,
                                          // FUN_00ac2400, the same lookup spec-vehicle-data.md §7.1 documents -
                                          // cross-referenced only per this spec's own text, not resolved here)
};
struct GangSignPose {
    std::optional<std::string> displayName;  // display_name. CORRECTED 2026-10-02 (§10, re-derived from the
                                               // executable, `func_0x00839290.txt` read in full): this is a RAW
                                               // TEXT HASH (via FUN_00daba10 then FUN_00db12c0 - the same
                                               // raw-hash helper customization_materials.xtbl's Name and
                                               // customization_compositing.xtbl's Name/diffuse/normal go
                                               // through, §9.1/§9.3), NOT one of the FUN_0084xxxx-family
                                               // localization-id resolvers every other "DisplayName"-style field
                                               // in this document uses - previously documented as "(localized)"
                                               // here, which was wrong. No behaviour change: this reader only
                                               // ever captured the raw element text either way (no hash is
                                               // computed in this project's readers), so the correction is
                                               // comment-only.
    StateAnimation animation;                  // animation (State/Animation pair via FUN_004bf810)
};
struct GangCustomization {
    std::optional<std::string> name;  // Name - the real loader only READS a row whose Name equals the
                                        // hardcoded sentinel "SR2_Player_Gang_Cust" (a Saints Row 2
                                        // legacy row name, §10); ParseGangCustomization itself does NOT
                                        // enforce that filter (it parses whatever row you hand it) - see
                                        // ParseGangCustomizationTable below for the filtered convenience.
    std::vector<GangVehicleGroup> gangVehicles;  // gang_vehicles > gang_vehicles[]
    std::optional<std::string> defaultVehicleSlot1;  // PlayerDefaults > DefaultVehicles > Slot1 (resolved to a
    std::optional<std::string> defaultVehicleSlot2;  // vehicle-group id via FUN_00ac1ae0 - not modelled, raw
    std::optional<std::string> defaultVehicleSlot3;  // vehicle name text kept)
    std::vector<GangSignPose> gangSigns;               // Gang_Signs > Pose[] (the player's gang-sign gesture list)
};
GangCustomization ParseGangCustomization(const Node* row);
// "the branch taken if the row's Name does NOT match [SR2_Player_Gang_Cust]
// simply walks to the next GangCustomization sibling without reading
// anything, so in practice only one specific row is ever consumed" (§10).
// Returns the FIRST <GangCustomization> row (directly under <Table>) whose
// Name equals "SR2_Player_Gang_Cust" (case-insensitive, sr3xtbl.h's general
// name-comparison convention); std::nullopt if no row matches.
std::optional<GangCustomization> ParseGangCustomizationTable(const Document& doc);

// ===========================================================================
// 11. customizable_action.xtbl. [CONFIRMED - disassembly + empirical]
// Each entry is a 16-byte (0x10) record; row element tag "CustomizableActions"
// (the validated real-data element name, §11/§21 - the plural wrapper used
// as the per-row tag, the same convention §10's gang_vehicles and §16.3's
// RegionalPresets already show elsewhere in this document).
// ===========================================================================
struct CustomizableAction {
    std::optional<std::string> actionType;   // ActionType - the 2-entry split key ("Insult"/"Compliment",
                                               // matched via FUN_dac830 against a 2-entry array, §11)
    StateAnimation action;                     // Action (State/Animation pair via FUN_004bf810)
    std::optional<std::string> situation;       // Situation (optional, via FUN_0070a2f0 - resolution target
                                                  // not independently confirmed here, raw text kept)
    std::optional<std::string> team;             // Team (optional enum via FUN_0094cc60 - "same field name/
                                                  // helper family as spec-tables-weapons-combat.md's team
                                                  // enums", §11 - not re-derived here, raw text kept)
    std::optional<std::string> localizedTag;      // LocalizedTag. NOTE (§11): a hardcoded override exists in the
                                                  // real code - if the CURRENT RUNTIME text language is one of
                                                  // three specific ids (9, 0, 10) and this tag is exactly
                                                  // "CUST_COMPLIMENT_K", the display text is force-set to the
                                                  // literal wide string "TOUCH DOWN" regardless of the string
                                                  // table. This reader has no notion of a "current language" -
                                                  // `localizedTag` is always the raw authored tag text, never
                                                  // the overridden display string.
};
CustomizableAction ParseCustomizableAction(const Node* row);
std::vector<CustomizableAction> ParseCustomizableActionTable(const Document& doc);

// Convenience split matching the real loader's own behaviour (§11: rows are
// separated into two fixed arrays by ActionType at load time). Rows whose
// ActionType is absent or matches neither name are NOT silently dropped
// here (unlike the real loader, whose 2-entry array match has no
// documented third bucket) - kept in `unmatched` instead, since inventing
// what the real code does with them would be a guess.
struct CustomizableActionSplit {
    std::vector<CustomizableAction> insults;
    std::vector<CustomizableAction> compliments;
    std::vector<CustomizableAction> unmatched;
};
CustomizableActionSplit SplitCustomizableActions(const std::vector<CustomizableAction>& rows);

}  // namespace sr3tables_customization
