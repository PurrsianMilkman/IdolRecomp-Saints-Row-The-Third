#pragma once

// sr3tables_customization/characters.h - character_definitions.xtbl
// (spec-tables-customization.md §12.1), character_height.xtbl (§12.2),
// character_customization_categories.xtbl (§13.1), character_types.xtbl
// (§13.2) and character.xtbl (§13.3). Included from
// sr3tables_customization/tables.h.
//
// Source: spec-tables-customization.md, the ONLY document consulted for this
// file's schema. Built entirely on include/sr3xtbl/xtbl.h.

#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// 12.1 character_definitions.xtbl. [CONFIRMED - disassembly for the loader
// chain; per-Character FIELD reader (FUN_00be0630) located but NOT
// decompiled this pass - an honest partial, §12.1, open item §22.1]
// ===========================================================================
// Rows are `Character` elements filtered by `Framework` (matching "main" or
// the current DLC framework string, "the same convention as every other
// framework-scoped table in this session", §12.1) - `framework` is the
// ONLY per-row field this spec confirms at the text level. The 236-byte
// (0xec) record size is known only indirectly, from the caller's own
// allocation arithmetic (`(realCharacterCount + 0x18) * 0xec`), not from
// the (undecompiled) field reader itself - not a basis for inventing what
// else the record holds.
struct CharacterDefinitionRow {
    std::optional<std::string> framework;  // Framework (filter-only at the loader level; also surfaced here
                                             // since it is a real, confirmed-to-exist element)
};
CharacterDefinitionRow ParseCharacterDefinitionRow(const Node* row);
std::vector<CharacterDefinitionRow> ParseCharacterDefinitionsTable(const Document& doc);

// ===========================================================================
// 12.2 character_height.xtbl - a hard dependency, loaded before
// character_definitions.xtbl on every call. [CONFIRMED - disassembly +
// empirical]
// ===========================================================================
struct HeightClass {
    Always<float> height;              // Height
    std::optional<std::string> name;    // Name (hashed) - "a small named enum of body-height presets that
                                          // character_definitions.xtbl rows presumably reference by name (the
                                          // cross-reference itself lives inside the undecompiled FUN_00be0630,
                                          // so this is inferred from load order and naming, not directly
                                          // confirmed field-by-field)" (§12.2)
};
HeightClass ParseHeightClass(const Node* row);
std::vector<HeightClass> ParseCharacterHeightTable(const Document& doc);

// ===========================================================================
// 13.1 character_customization_categories.xtbl. [CONFIRMED - disassembly +
// empirical, re-derived 2026-10-02: `FUN_00be4120` read in full and
// confirmed to be this table's OWN dedicated reader (it hardcodes the
// child-element name "category"), not a generic shared stash function as
// the 2026-09-30 text assumed - see spec-tables-customization.md §13.1's
// 2026-10-02 review status.]
// ===========================================================================
// Element tag is lowercase "category" (confirmed against real data,
// distinct casing from customization_categories.xtbl's "Category", §7.1 -
// the accessor grammar is case-insensitive so this has no functional
// effect, noted only to avoid confusing the two tables).
//
// Full `0xc`-byte (12-byte) record CONFIRMED 2026-10-02: `+0x00` Name hash,
// `+0x04` Display_name localized id, `+0x08` the Is_DLC sentinel byte
// (`0xff` base / `0` DLC - the same convention as every other table in this
// document), `+0x09` a Flags>flag membership byte that sets bits 0x1+0x2
// together ("| 3") when a flag's own text equals "locked" (the same two-
// bits-at-once idiom gang_customization.xtbl's `locked` flag uses, §10 -
// modelled as one boolean here, same storage-duplication treatment as
// GangVehicleGroup::locked above).
struct CharacterCustomizationCategory {
    std::optional<std::string> name;         // Name (hashed)
    std::optional<std::string> displayName;   // Display_name (localized id)
    Always<bool> isDlc;                        // Is_DLC sentinel byte
    bool locked = false;                        // Flags > flag == "locked"
};
CharacterCustomizationCategory ParseCharacterCustomizationCategory(const Node* row);

// Applies BOTH real-loader behaviours CONFIRMED 2026-10-02 (spec-tables-
// customization.md §13.1):
//  1. Hard cap 20 rows (0x14), checked pre-increment against the
//     not-yet-stored count ("if (0x13 < count) return") - keeps a maximum
//     of 20 (16 real rows ship, well under it).
//  2. A genuine hazard the spec flags explicitly "FOR TEAM B": rows are
//     registered into a dedup hash table by Name as they're read; if a
//     row's Name collides with one already registered, the REAL loader
//     `return`s immediately, abandoning every remaining row in the file -
//     not just skipping the duplicate. Reproduced here by stopping the
//     whole parse (not pushing the colliding row, and not reading any row
//     after it) the first time a Name repeats.
std::vector<CharacterCustomizationCategory> ParseCharacterCustomizationCategoriesTable(const Document& doc);

// Superseded by ParseCharacterCustomizationCategoriesTable above (which now
// has a CONFIRMED full schema, the 20-row cap and the duplicate-Name
// truncation hazard) - kept only as a thinner, schema-free row locator for
// any existing caller that just wants the raw <category> Nodes.
std::vector<const Node*> FindCharacterCustomizationCategoryRows(const Document& doc);

// ===========================================================================
// 13.2 character_types.xtbl. [CONFIRMED - disassembly + empirical] HARD CAP
// 120 rows (0x78). A flat name-only registry, matching
// spec-tables-animation.md's anim_states.xtbl/anim_actions.xtbl pattern.
// ===========================================================================
struct CharacterType {
    std::optional<std::string> name;  // Name (hashed via FUN_00db1510, the loop index used as a salt/context
                                        // argument - a derived hashing detail, not modelled; raw Name text is
                                        // what's kept). No other field is read (§13.2).
};
CharacterType ParseCharacterType(const Node* row);
// Stops after the 120th accepted row - not modelled as an error, only as a
// stopping point (same convention as this library's other hard-capped
// tables).
std::vector<CharacterType> ParseCharacterTypesTable(const Document& doc);

// ===========================================================================
// 13.3 character.xtbl - loaded twice, not decompiled to field level.
// [CONFIRMED real, single-literal, consumed twice; SCHEMA OPEN, §13.3]
// ===========================================================================
// Row element tag is "Character" - THE SAME TAG character_definitions.xtbl
// uses (§12.1) - but this is "confirmed to be a structurally separate file
// with its own loader entry point and its own, larger, real row count (382
// vs. 282)" (§13.3). What distinguishes a character.xtbl Character row from
// a character_definitions.xtbl Character row was explicitly NOT resolved
// (open item §22.2, "flagged rather than guessed") - no field is modelled
// here for the same reason as §13.1 above: a row locator only.
std::vector<const Node*> FindCharacterRows(const Document& doc);

}  // namespace sr3tables_customization
