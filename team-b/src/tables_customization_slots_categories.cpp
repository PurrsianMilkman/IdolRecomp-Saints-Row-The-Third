// Parse functions for sr3tables_customization/slots_categories.h (spec-
// tables-customization.md §6.1, §6.2, §7.1, §7.2, §7.3). Built ONLY on
// sr3xtbl's accessors; nothing here touches the game executable,
// disassembly or decompiled code.

#include "sr3tables_customization/slots_categories.h"

#include <algorithm>
#include <cctype>

namespace sr3tables_customization {

using namespace sr3xtbl;

namespace {

std::optional<std::string> OptText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

// A flat list of child elements each named `childName`, taking each such
// child's own text directly (matches sr3customization/customization.cpp's
// ReadTextChildList convention).
std::vector<std::string> ReadTextChildList(const Node* wrap, std::string_view childName) {
    std::vector<std::string> out;
    for (const Node* e = FindChild(wrap, childName); e; e = NextSibling(wrap, e, childName))
        if (e->text()) out.push_back(*e->text());
    return out;
}

int MatchName(const std::optional<std::string>& text, const std::string_view* names, size_t count) {
    if (!text) return -1;
    for (size_t i = 0; i < count; ++i)
        if (NameEquals(*text, names[i])) return static_cast<int>(i);
    return -1;
}

StateAnimation ReadStateAnimation(const Node* parent, std::string_view wrapName) {
    StateAnimation sa;
    const Node* n = FindChild(parent, wrapName);
    if (!n) return sa;
    sa.state = OptText(n, "State");
    sa.animation = OptText(n, "Animation");
    return sa;
}

}  // namespace

// ===========================================================================
// 6.1 customization_slots.xtbl
// ===========================================================================
SlotEntry ParseSlotEntry(const Node* row) {
    SlotEntry s;
    s.name = OptText(row, "Name");
    s.slotIndex = MatchName(s.name, kCanonicalSlotNames.data(), kCanonicalSlotNames.size());
    s.cpuSizeRaw = ReadInt32Always(row, "CPU_Size");
    s.gpuSizeRaw = ReadInt32Always(row, "GPU_Size");
    s.required = HasFlag(FindChild(row, "Flags"), "required");
    s.npcOnly = HasFlag(FindChild(row, "Flags"), "npc only");
    s.removeOnOutfit = HasFlag(FindChild(row, "Flags"), "remove on outfit");
    s.renderOrder = ReadInt32Always(row, "Render_Order");
    return s;
}

std::vector<SlotEntry> ParseCustomizationSlotsTable(const Document& doc) {
    std::vector<SlotEntry> out;
    for (const Node* row : Children(doc.table(), "Slot")) out.push_back(ParseSlotEntry(row));
    return out;
}

// ===========================================================================
// 6.2 customization_slot_defaults.xtbl
// ===========================================================================
SlotDefaultEntry ParseSlotDefault(const Node* row) {
    SlotDefaultEntry d;
    d.slot = OptText(row, "Slot");
    d.item = OptText(row, "Item");
    d.variant = OptText(row, "Variant");
    return d;
}

std::vector<SlotDefaultEntry> ParseCustomizationSlotDefaultsTable(const Document& doc) {
    std::vector<SlotDefaultEntry> out;
    for (const Node* row : Children(doc.table(), "Slot_Default")) out.push_back(ParseSlotDefault(row));
    return out;
}

// ===========================================================================
// 7.1 customization_categories.xtbl
// ===========================================================================
namespace {
ObscureElement ParseObscureElement(const Node* row) {
    ObscureElement o;
    o.slot = OptText(row, "Slot");
    o.flag = OptText(row, "Flag");
    return o;
}
}  // namespace

Category ParseCategory(const Node* row) {
    Category c;
    c.name = OptText(row, "Name");
    c.displayName = OptText(row, "DisplayName");
    c.slotsGrid = ReadTextChildList(FindChild(row, "Slots_Grid"), "Slot_Element");
    const Node* obscureWrap = FindChild(row, "Obscure_Grid");
    for (const Node* e = FindChild(obscureWrap, "Obscure_Element"); e; e = NextSibling(obscureWrap, e, "Obscure_Element"))
        c.obscureGrid.push_back(ParseObscureElement(e));
    if (FindChild(row, "Male_Animations")) c.maleAnimations = ReadStateAnimation(row, "Male_Animations");
    if (FindChild(row, "Female_Animations")) c.femaleAnimations = ReadStateAnimation(row, "Female_Animations");
    c.noWearString = OptText(row, "No_Wear_String");
    return c;
}

std::vector<Category> ParseCustomizationCategoriesTable(const Document& doc) {
    std::vector<Category> out;
    for (const Node* row : Children(doc.table(), "Category")) out.push_back(ParseCategory(row));
    return out;
}

// ===========================================================================
// 7.2 customization_flags.xtbl
// ===========================================================================
FlagEntry ParseFlagEntry(const Node* row) {
    FlagEntry f;
    f.name = OptText(row, "Name");
    return f;
}

std::vector<FlagEntry> ParseCustomizationFlagsTable(const Document& doc) {
    std::vector<FlagEntry> out;
    for (const Node* row : Children(doc.table(), "Flag")) out.push_back(ParseFlagEntry(row));
    return out;
}

// ===========================================================================
// 7.3 customization_icons.xtbl
// ===========================================================================
Icon ParseIcon(const Node* row) {
    Icon i;
    i.name = OptText(row, "Name");
    i.displayName = OptText(row, "DisplayName");
    i.categoryElements = ReadTextChildList(FindChild(row, "Categories"), "Category_Element");
    i.outfitsOnly = ReadBoolAlways(row, "Outfits_Only");
    return i;
}

std::vector<Icon> ParseCustomizationIconsTable(const Document& doc) {
    std::vector<Icon> out;
    for (const Node* row : Children(doc.table(), "Icon")) out.push_back(ParseIcon(row));
    return out;
}

}  // namespace sr3tables_customization
