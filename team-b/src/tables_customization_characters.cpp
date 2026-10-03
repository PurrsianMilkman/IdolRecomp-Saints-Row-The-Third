// Parse functions for sr3tables_customization/characters.h (spec-tables-
// customization.md §12.1, §12.2, §13.1, §13.2, §13.3). Built ONLY on
// sr3xtbl's accessors; nothing here touches the game executable,
// disassembly or decompiled code.

#include "sr3tables_customization/characters.h"

namespace sr3tables_customization {

using namespace sr3xtbl;

namespace {
std::optional<std::string> OptText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}
}  // namespace

// ===========================================================================
// 12.1 character_definitions.xtbl
// ===========================================================================
CharacterDefinitionRow ParseCharacterDefinitionRow(const Node* row) {
    CharacterDefinitionRow r;
    r.framework = OptText(row, "Framework");
    return r;
}

std::vector<CharacterDefinitionRow> ParseCharacterDefinitionsTable(const Document& doc) {
    std::vector<CharacterDefinitionRow> out;
    for (const Node* row : Children(doc.table(), "Character")) out.push_back(ParseCharacterDefinitionRow(row));
    return out;
}

// ===========================================================================
// 12.2 character_height.xtbl
// ===========================================================================
HeightClass ParseHeightClass(const Node* row) {
    HeightClass h;
    h.height = ReadFloatAlways(row, "Height");
    h.name = OptText(row, "Name");
    return h;
}

std::vector<HeightClass> ParseCharacterHeightTable(const Document& doc) {
    std::vector<HeightClass> out;
    for (const Node* row : Children(doc.table(), "Height_Class")) out.push_back(ParseHeightClass(row));
    return out;
}

// ===========================================================================
// 13.1 character_customization_categories.xtbl
// ===========================================================================
CharacterCustomizationCategory ParseCharacterCustomizationCategory(const Node* row) {
    CharacterCustomizationCategory c;
    c.name = OptText(row, "Name");
    c.displayName = OptText(row, "Display_name");
    c.isDlc = ReadBoolAlways(row, "Is_DLC");
    c.locked = HasFlag(FindChild(row, "Flags"), "locked");
    return c;
}

std::vector<CharacterCustomizationCategory> ParseCharacterCustomizationCategoriesTable(const Document& doc) {
    std::vector<CharacterCustomizationCategory> out;
    const Node* wrap = doc.table();
    for (const Node* row = FindChild(wrap, "category"); row; row = NextSibling(wrap, row, "category")) {
        // Hard cap 20 rows (0x14, §13.1), checked pre-increment against the
        // not-yet-stored count - keeps a maximum of exactly 20.
        if (out.size() >= 20) break;
        CharacterCustomizationCategory c = ParseCharacterCustomizationCategory(row);
        // Duplicate-Name truncation hazard (§13.1, "FOR TEAM B - real
        // hazard, not just documentation"): the real loader registers each
        // row into a dedup hash table by Name as it reads it; a Name that
        // collides with one already registered makes the loader `return`
        // immediately, abandoning EVERY remaining row in the file (not just
        // the duplicate). Reproduced by stopping the whole parse - this row
        // is not pushed either, matching the real loader never finishing
        // this row's own registration before bailing.
        // JUDGEMENT CALL: the dedup hash's own case-sensitivity was not
        // independently confirmed for this table; NameEquals (this
        // project's general case-insensitive text-match convention, used
        // for every other Name-based lookup in this library) is reused here.
        bool duplicate = false;
        if (c.name) {
            for (const CharacterCustomizationCategory& existing : out) {
                if (existing.name && NameEquals(*existing.name, *c.name)) {
                    duplicate = true;
                    break;
                }
            }
        }
        if (duplicate) break;
        out.push_back(std::move(c));
    }
    return out;
}

// Superseded by ParseCharacterCustomizationCategoriesTable above - kept as a
// thinner, schema-free row locator (see characters.h banner).
std::vector<const Node*> FindCharacterCustomizationCategoryRows(const Document& doc) {
    return Children(doc.table(), "category");
}

// ===========================================================================
// 13.2 character_types.xtbl
// ===========================================================================
CharacterType ParseCharacterType(const Node* row) {
    CharacterType t;
    t.name = OptText(row, "Name");
    return t;
}

std::vector<CharacterType> ParseCharacterTypesTable(const Document& doc) {
    std::vector<CharacterType> out;
    const Node* wrap = doc.table();
    for (const Node* row = FindChild(wrap, "Type"); row; row = NextSibling(wrap, row, "Type")) {
        // CONFIRMED 2026-10-02 (re-derived from the executable, `FUN_00be1d80` read in full): a do/while that
        // stores a row, increments the row index, THEN tests `(short)index < 0x78` to continue - a post-
        // increment strict `<` that keeps a true maximum of exactly 120 (not 119), matching the `>= 120`
        // already coded here. Real data has 96 rows, so it cannot change a result on real data.
        if (out.size() >= 120) break;  // hard cap 0x78 (§13.2)
        out.push_back(ParseCharacterType(row));
    }
    return out;
}

// ===========================================================================
// 13.3 character.xtbl - row locator only (no field schema recovered, see
// characters.h banner).
// ===========================================================================
std::vector<const Node*> FindCharacterRows(const Document& doc) {
    return Children(doc.table(), "Character");
}

}  // namespace sr3tables_customization
