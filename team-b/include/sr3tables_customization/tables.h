#pragma once

// sr3tables_customization - typed readers for the character/vehicle
// customization `.xtbl` table group (spec-tables-customization.md,
// 2026-09-23, agent AQ): customization_*.xtbl, character_*.xtbl, the shared
// named-color-pool family, and the player_creation*/player_cust_*/
// player_master_sliders/player_presets/player_regional_presets face-and-
// body-creation subsystem.
//
// Source: spec-tables-customization.md, the ONLY document consulted for
// this library's schema (cleanroom boundary - see the task's HARD RULES).
// Built entirely on top of include/sr3xtbl/xtbl.h's document model and
// accessors (Node, FindChild, ChildText, GetX/ReadXAlways, Always<T>,
// FlagMask/HasFlag/EnumIndex, NameHash); nothing here was derived from the
// game executable, disassembly or decompiled code - every offset/address
// cited in a comment is copied verbatim from the spec's own prose, purely
// to let a reader cross-reference the spec section, never re-derived.
//
// CONVENTION (matches sr3tables_environment/tables.h exactly - see its
// banner):
//   * sr3xtbl::Always<T> (value + present) = an "always write" element.
//     When !present the ENGINE's real behaviour is an indeterminate
//     stack-residue value (per this project's general Always-hazard
//     convention, sr3xtbl.h); `value` is then a deterministic 0 stand-in a
//     caller must NOT treat as engine behaviour.
//   * std::optional<T> = a "write only if present" element.
//   * A handful of fields have a spec-STATED CONCRETE default distinct from
//     both of the above (e.g. items.h's Heel_Angle/Heel_Height -> 0.0,
//     player_creation.h's Composite.Color -> opaque white). These are
//     modelled as std::optional<T> plus a small `...OrDefault()` helper, so
//     a caller never has to guess and "was it actually present in real
//     data" stays answerable.
//   * A field the spec confirms exists as an XML element, but whose runtime
//     MEANING/consumer is HYPOTHESIS or OPEN, is still surfaced as a raw
//     value (with a comment citing the spec's own open-item number) - never
//     given an interpreted meaning the spec itself didn't commit to. Where
//     NOTHING about a row's fields is confirmed at all, this library
//     surfaces only a row LOCATOR (raw `const Node*`), not a guessed struct
//     - see characters.h. As of the 2026-10-02 executable re-derivation this
//     applies ONLY to character.xtbl (§13.3, still genuinely OPEN);
//     character_customization_categories.xtbl (§13.1) gained a full
//     CONFIRMED schema that pass (Name/Display_name/Is_DLC/locked, a 20-row
//     cap, and a duplicate-Name whole-table-truncation hazard) and is now a
//     typed reader like every other table here - its old row-locator-only
//     function is kept only for backward compatibility, see characters.h.
//
// ---------------------------------------------------------------------------
// SCOPE DECISIONS (see the per-file banners for detail; summarised here)
// ---------------------------------------------------------------------------
//  * §4 customization_items.xtbl IS reimplemented here (items.h,
//    CustomizationItemEntry) under new type names, because its schema is
//    materially richer than include/sr3customization/customization.h's
//    CustomizationItem (built from the older, narrower
//    spec-customization-data.md): Brand, Heel_Angle/Heel_Height, the
//    ShoeAudioSwitch/ClothingAudioSwitch same-destination-field bug,
//    Fat_Bone_Arm/Fat_Bone_Leg, the Style enum, the specific Flags bits, the
//    "npc only" Slot fallback, the 858-row hard cap and the Comparison
//    sub-flag on each Wear_Option flag reference are all real, disassembly-
//    confirmed fields the older reader has no equivalent for. See items.h's
//    banner for the specific points where this header's reading of §4 used
//    to differ from the older reader's own schema (DISCREPANCIES 1-3) - as
//    of the 2026-10-02 executable re-derivation, 1 (Female_Mesh_Filename)
//    and 2 (Shader_Type's parent) are both RESOLVED (both readers now agree,
//    and the older reader's Shader_Type-at-Variant-level bug was fixed in
//    include/sr3customization/customization.h); only 3 (Default_Colors_Grid
//    item-vs-variant modelling) remains a genuine, documented difference.
//  * §5.1 customization_outfits.xtbl is DELIBERATELY NOT reimplemented -
//    materially the same schema as sr3customization::Outfit; see
//    include/sr3customization/customization.h for that type and items.h's
//    banner for the one field (a CRC) this spec adds, which is a derived
//    value rather than a new authored XML element.
//  * Everything else in this library (§2-§3, §5.2-§17) is genuinely new
//    territory with no existing reader in this project.
//  * §18's 12 confirmed-dead/orphaned filenames are NOT implemented (no
//    code loader reaches them at all, per the spec's own exhaustive
//    per-filename Ghidra literal scan) - same "no loader, no reader"
//    policy sr3tables_environment applies to materials.xtbl.
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> for every repeated-row table, and ParseXxx(const
// sr3xtbl::Document&) -> optional<Xxx> for the few single-block/singleton
// tables (char_color_balance.xtbl, gang_customization.xtbl,
// player_master_sliders.xtbl's real-world-always-empty case aside). All
// definitions are split across src/tables_customization_*.cpp, one file per
// header below.

#include "sr3xtbl/xtbl.h"

// Split by subsystem (this is a large spec, 22 numbered sections across 8
// independent load chains, spec §1.3's chain table) - each header below
// re-opens `namespace sr3tables_customization` and is meant to be included
// only via this umbrella file, in this order (later headers use types
// declared by earlier ones: items.h uses slots_categories.h's
// kCanonicalSlotNames; gang_actions.h uses slots_categories.h's
// StateAnimation; player_creation.h uses color_pools.h's ColorText).
#include "sr3tables_customization/color_pools.h"        // §2, §3.1, §3.2
#include "sr3tables_customization/slots_categories.h"   // §6.1, §6.2, §7.1, §7.2, §7.3
#include "sr3tables_customization/items.h"               // §4, §5.2 (§5.1 deliberately not reimplemented - see above)
#include "sr3tables_customization/materials.h"            // §8, §9.1, §9.2, §9.3
#include "sr3tables_customization/gang_actions.h"          // §10, §11
#include "sr3tables_customization/characters.h"             // §12.1, §12.2, §13.1, §13.2, §13.3
#include "sr3tables_customization/player_creation.h"         // §14.1, §14.2, §15.1-3, §16.1-3, §17
