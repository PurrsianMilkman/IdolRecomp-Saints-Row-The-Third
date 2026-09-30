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
// 13.1 character_customization_categories.xtbl - row locator only (no field
// schema recovered, see characters.h banner).
// ===========================================================================
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
