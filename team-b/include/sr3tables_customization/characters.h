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
// 13.1 character_customization_categories.xtbl. [CONFIRMED real, single-
// literal, consumed twice; per-row SCHEMA is OPEN - both callers route
// through a shared, undecompiled stash function, §13.1, open item §22.1]
// ===========================================================================
// Element tag is lowercase "category" (confirmed against real data,
// distinct casing from customization_categories.xtbl's "Category", §7.1 -
// the accessor grammar is case-insensitive so this has no functional
// effect, noted only to avoid confusing the two tables). Nothing about this
// row's OWN fields (not even whether it has a Name) is confirmed by the
// spec text - per this project's confidence discipline, this reader does
// NOT invent a field list for it. It only LOCATES the rows (a real,
// confirmed-to-exist element with a confirmed tag) and hands back the raw
// Node for a caller to inspect further themselves - a deliberately thinner
// contract than every other table in this file.
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
