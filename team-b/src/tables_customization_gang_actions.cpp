// Parse functions for sr3tables_customization/gang_actions.h (spec-tables-
// customization.md §10, §11). Built ONLY on sr3xtbl's accessors; nothing
// here touches the game executable, disassembly or decompiled code.

#include "sr3tables_customization/gang_actions.h"

namespace sr3tables_customization {

using namespace sr3xtbl;

namespace {

std::optional<std::string> OptText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

StateAnimation ReadStateAnimationChild(const Node* parent, std::string_view wrapName) {
    StateAnimation sa;
    const Node* n = FindChild(parent, wrapName);
    if (!n) return sa;
    sa.state = OptText(n, "State");
    sa.animation = OptText(n, "Animation");
    return sa;
}

GangVehicleGroup ParseGangVehicleGroup(const Node* row) {
    GangVehicleGroup g;
    g.name = OptText(row, "Name");
    g.locked = HasFlag(FindChild(row, "Flags"), "locked");
    const Node* vehiclesWrap = FindChild(row, "Vehicles");
    for (const Node* v = FindChild(vehiclesWrap, "Vehicle"); v; v = NextSibling(vehiclesWrap, v, "Vehicle"))
        if (v->text()) g.vehicles.push_back(*v->text());
    return g;
}

GangSignPose ParseGangSignPose(const Node* row) {
    GangSignPose p;
    p.displayName = OptText(row, "display_name");
    p.animation = ReadStateAnimationChild(row, "animation");
    return p;
}

}  // namespace

// ===========================================================================
// 10. gang_customization.xtbl
// ===========================================================================
GangCustomization ParseGangCustomization(const Node* row) {
    GangCustomization g;
    g.name = OptText(row, "Name");

    const Node* gvWrap = FindChild(row, "gang_vehicles");
    for (const Node* e = FindChild(gvWrap, "gang_vehicles"); e; e = NextSibling(gvWrap, e, "gang_vehicles"))
        g.gangVehicles.push_back(ParseGangVehicleGroup(e));

    const Node* playerDefaults = FindChild(row, "PlayerDefaults");
    const Node* defaultVehicles = FindChild(playerDefaults, "DefaultVehicles");
    g.defaultVehicleSlot1 = OptText(defaultVehicles, "Slot1");
    g.defaultVehicleSlot2 = OptText(defaultVehicles, "Slot2");
    g.defaultVehicleSlot3 = OptText(defaultVehicles, "Slot3");

    const Node* gangSigns = FindChild(row, "Gang_Signs");
    for (const Node* p = FindChild(gangSigns, "Pose"); p; p = NextSibling(gangSigns, p, "Pose"))
        g.gangSigns.push_back(ParseGangSignPose(p));

    return g;
}

std::optional<GangCustomization> ParseGangCustomizationTable(const Document& doc) {
    const Node* wrap = doc.table();
    for (const Node* row = FindChild(wrap, "GangCustomization"); row; row = NextSibling(wrap, row, "GangCustomization")) {
        const std::string* name = ChildText(row, "Name");
        if (name && NameEquals(*name, "SR2_Player_Gang_Cust")) return ParseGangCustomization(row);
    }
    return std::nullopt;
}

// ===========================================================================
// 11. customizable_action.xtbl
// ===========================================================================
CustomizableAction ParseCustomizableAction(const Node* row) {
    CustomizableAction a;
    a.actionType = OptText(row, "ActionType");
    a.action = ReadStateAnimationChild(row, "Action");
    a.situation = OptText(row, "Situation");
    a.team = OptText(row, "Team");
    a.localizedTag = OptText(row, "LocalizedTag");
    return a;
}

std::vector<CustomizableAction> ParseCustomizableActionTable(const Document& doc) {
    std::vector<CustomizableAction> out;
    for (const Node* row : Children(doc.table(), "CustomizableActions")) out.push_back(ParseCustomizableAction(row));
    return out;
}

CustomizableActionSplit SplitCustomizableActions(const std::vector<CustomizableAction>& rows) {
    CustomizableActionSplit s;
    for (const CustomizableAction& a : rows) {
        if (a.actionType && NameEquals(*a.actionType, "Insult")) {
            s.insults.push_back(a);
        } else if (a.actionType && NameEquals(*a.actionType, "Compliment")) {
            s.compliments.push_back(a);
        } else {
            s.unmatched.push_back(a);
        }
    }
    return s;
}

}  // namespace sr3tables_customization
