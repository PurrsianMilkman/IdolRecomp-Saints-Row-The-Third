// Parse functions for sr3tables_customization/color_pools.h (spec-tables-
// customization.md §2, §3.1, §3.2). Built ONLY on sr3xtbl's accessors;
// nothing here touches the game executable, disassembly or decompiled code.

#include "sr3tables_customization/color_pools.h"

namespace sr3tables_customization {

using namespace sr3xtbl;

namespace {

std::optional<std::string> OptText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::vector<std::string_view> splitWs(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isWs(s[i])) ++i;
        const size_t start = i;
        while (i < s.size() && !isWs(s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

// "r g b [a]" whitespace-separated colour text (see ColorText's banner in
// color_pools.h for the judgement call this makes).
std::optional<ColorText> ParseColorTextField(const Node* node, std::string_view name) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    const std::vector<std::string_view> toks = splitWs(*t);
    if (toks.empty()) return std::nullopt;
    ColorText c;  // r=g=b=0, a=1.0 defaults
    if (toks.size() >= 1) c.r = ParseFloat(toks[0]);
    if (toks.size() >= 2) c.g = ParseFloat(toks[1]);
    if (toks.size() >= 3) c.b = ParseFloat(toks[2]);
    if (toks.size() >= 4) c.a = ParseFloat(toks[3]);
    return c;
}

}  // namespace

// ===========================================================================
// 2. The shared named-color-pool family
// ===========================================================================
ColorPoolEntry ParseColorPoolEntry(const Node* row) {
    ColorPoolEntry e;
    e.name = OptText(row, "Name");
    e.color = ParseColorTextField(row, "Color");
    e.displayName = OptText(row, "DisplayName");
    return e;
}

std::vector<ColorPoolEntry> ParseColorPoolTable(const Document& doc) {
    std::vector<ColorPoolEntry> out;
    for (const Node* row : Children(doc.table(), "Color_Entry")) out.push_back(ParseColorPoolEntry(row));
    return out;
}

std::optional<ColorBalance> ParseColorBalance(const Document& doc) {
    const Node* row = FindChild(doc.table(), "ColorBalance");
    if (!row) return std::nullopt;
    ColorBalance cb;
    cb.blackLevel = ReadFloatAlways(row, "Black_Level");
    cb.whiteLevel = ReadFloatAlways(row, "White_Level");
    cb.saturation = ReadFloatAlways(row, "Saturation");
    cb.pedBlackLevel = ReadFloatAlways(row, "Ped_Black_Level");
    cb.pedWhiteLevel = ReadFloatAlways(row, "Ped_White_Level");
    cb.pedSaturation = ReadFloatAlways(row, "Ped_Saturation");
    return cb;
}

// ===========================================================================
// 3.1 items_color_pool.xtbl
// ===========================================================================
ItemColorEntry ParseItemColorEntry(const Node* row) {
    ItemColorEntry e;
    e.name = OptText(row, "Name");
    e.color = OptText(row, "Color");
    e.editor = OptText(row, "_Editor");
    return e;
}

std::vector<ItemColorEntry> ParseItemsColorPoolTable(const Document& doc) {
    std::vector<ItemColorEntry> out;
    for (const Node* row : Children(doc.table(), "Item_Color")) out.push_back(ParseItemColorEntry(row));
    return out;
}

// ===========================================================================
// 3.2 npc_color_palette.xtbl
// ===========================================================================
namespace {

TertiaryColor ParseTertiaryColor(const Node* row) {
    TertiaryColor c;
    c.r = ReadFloatAlways(row, "R");
    c.g = ReadFloatAlways(row, "G");
    c.b = ReadFloatAlways(row, "B");
    return c;
}

SecondaryColor ParseSecondaryColor(const Node* row) {
    SecondaryColor c;
    c.r = ReadFloatAlways(row, "R");
    c.g = ReadFloatAlways(row, "G");
    c.b = ReadFloatAlways(row, "B");
    const Node* wrap = FindChild(row, "Tertiary_Colors");
    for (const Node* t : Children(wrap, "Tertiary_Color")) c.tertiaryColors.push_back(ParseTertiaryColor(t));
    return c;
}

PrimaryColor ParsePrimaryColor(const Node* row) {
    PrimaryColor c;
    c.r = ReadFloatAlways(row, "R");
    c.g = ReadFloatAlways(row, "G");
    c.b = ReadFloatAlways(row, "B");
    const Node* wrap = FindChild(row, "Secondary_Colors");
    for (const Node* s : Children(wrap, "Secondary_Color")) c.secondaryColors.push_back(ParseSecondaryColor(s));
    return c;
}

}  // namespace

Palette ParsePalette(const Node* row) {
    Palette p;
    p.name = OptText(row, "Name");
    const Node* wrap = FindChild(row, "Primary_Colors");
    for (const Node* pr : Children(wrap, "Primary_Color")) p.primaryColors.push_back(ParsePrimaryColor(pr));
    return p;
}

std::vector<Palette> ParseNpcColorPaletteTable(const Document& doc) {
    std::vector<Palette> out;
    for (const Node* row : Children(doc.table(), "Palette")) out.push_back(ParsePalette(row));
    return out;
}

}  // namespace sr3tables_customization
