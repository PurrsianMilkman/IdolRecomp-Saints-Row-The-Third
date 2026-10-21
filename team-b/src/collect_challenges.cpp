// Challenges - spec-tables-progression.md C.3. See include/sr3collect/challenges.h.

#include "sr3collect/challenges.h"

#include <algorithm>
#include <cmath>

namespace sr3collect {

namespace {

std::string Text(const sr3xtbl::Node* n, const char* name) {
    const std::string* t = sr3xtbl::ChildText(n, name);
    return t ? *t : std::string();
}

bool Matches(const std::string& filter, const std::string& value) {
    return filter.empty() || sr3xtbl::NameEquals(filter, "Any") || sr3xtbl::NameEquals(filter, value);
}

bool TeamMatches(const std::string& filter, const std::string& team, bool hostile) {
    if (sr3xtbl::NameEquals(filter, "Any Hostile But Police")) return hostile && !sr3xtbl::NameEquals(team, "Police");
    return Matches(filter, team);
}

}  // namespace

ChallengeUnit ChallengeUnitFromName(const std::string& n) {
    if (sr3xtbl::NameEquals(n, "Meters")) return ChallengeUnit::Meters;
    if (sr3xtbl::NameEquals(n, "Miles")) return ChallengeUnit::Miles;
    if (sr3xtbl::NameEquals(n, "Seconds")) return ChallengeUnit::Seconds;
    if (sr3xtbl::NameEquals(n, "Minutes")) return ChallengeUnit::Minutes;
    if (sr3xtbl::NameEquals(n, "Hours")) return ChallengeUnit::Hours;
    return ChallengeUnit::None;
}

double ChallengeDisplayValue(ChallengeUnit unit, double v) {
    switch (unit) {
    case ChallengeUnit::Miles: return v * 0.000621371;
    case ChallengeUnit::Minutes: return v / 60.0;
    case ChallengeUnit::Hours: return v / 3600.0;
    default: return v;
    }
}

std::vector<ChallengeDef> ReadChallenges(const sr3xtbl::Document& doc) {
    std::vector<ChallengeDef> out;
    const sr3xtbl::Node* table = doc.table() ? doc.table() : doc.root();
    for (const sr3xtbl::Node* row : sr3xtbl::Children(table, "Challenge")) {
        ChallengeDef c;
        c.name = Text(row, "Name");
        c.displayString = Text(row, "Display_String");
        c.description = Text(row, "description");
        c.image = Text(row, "image");
        c.contact = Text(row, "Contact");
        c.goal = sr3xtbl::GetFloat(row, "Goal").value_or(0.0);
        if (const sr3xtbl::Node* r = sr3xtbl::FindChild(row, "Reward")) {
            c.respect = static_cast<int32_t>(sr3xtbl::GetFloat(r, "Respect").value_or(0.0));
            c.cash = sr3xtbl::GetFloat(r, "Cash").value_or(0.0);
        }
        c.unit = ChallengeUnitFromName(Text(row, "Units"));
        c.displayUnit = c.unit;
        if (const sr3xtbl::Node* hud = sr3xtbl::FindChild(row, "HUD")) {
            if (const sr3xtbl::Node* p = sr3xtbl::FindChild(hud, "HUD_Percent")) {
                c.hudPercent = true;
                c.hudStep = sr3xtbl::GetFloat(p, "HUD_Percent_Value").value_or(0.0);
            } else if (const sr3xtbl::Node* l = sr3xtbl::FindChild(hud, "HUD_Linear")) {
                c.hudPercent = false;
                c.hudStep = sr3xtbl::GetFloat(l, "HUD_Linear_Value").value_or(0.0);
            }
        }
        if (const sr3xtbl::Node* u = sr3xtbl::FindChild(row, "Unlock_Requirement")) {
            const std::string n = Text(u, "Unlock_Name");
            if (!n.empty()) c.unlockName = n;
        }
        if (const sr3xtbl::Node* trig = sr3xtbl::FindChild(row, "Trigger")) {
            if (const sr3xtbl::Node* k = sr3xtbl::FindChild(trig, "Trigger_Kill")) {
                c.trigger = ChallengeTrigger::Kill;
                c.team = Text(k, "Team");
                c.weapon = Text(k, "Weapon");
                c.weaponSlot = Text(k, "Weapon_Slot");
                c.damageType = Text(k, "Damage_Type");
            } else if (const sr3xtbl::Node* d = sr3xtbl::FindChild(trig, "Trigger_Destroy")) {
                c.trigger = ChallengeTrigger::Destroy;
                c.vehicleType = Text(d, "Vehicle_Type");
                c.team = Text(d, "Team");
                c.weapon = Text(d, "Weapon");
                c.weaponSlot = Text(d, "Weapon_Slot");
                for (const sr3xtbl::Node* vs : sr3xtbl::Children(sr3xtbl::FindChild(d, "Vehicle_List"), "Vehicles")) {
                    const std::string v = Text(vs, "Vehicle");
                    if (!v.empty()) c.vehicles.push_back(v);
                }
            } else if (const sr3xtbl::Node* s = sr3xtbl::FindChild(trig, "Trigger_Stat")) {
                c.trigger = ChallengeTrigger::Stat;
                c.stat = Text(s, "Stat");
                const std::string u = Text(s, "Units");
                if (!u.empty()) c.displayUnit = ChallengeUnitFromName(u);
            } else if (const sr3xtbl::Node* sp = sr3xtbl::FindChild(trig, "Trigger_Special")) {
                c.trigger = ChallengeTrigger::Special;
                c.specialCase = Text(sp, "Special_Case_Name");
                c.team = Text(sp, "Team");
                c.store = Text(sp, "Store");
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

ChallengeTracker::ChallengeTracker(std::vector<ChallengeDef> defs) : defs_(std::move(defs)), states_(defs_.size()) {
    for (size_t i = 0; i < defs_.size(); ++i) states_[i].unlocked = !defs_[i].unlockName.has_value();
}

void ChallengeTracker::missionCompleted(const std::string& name) {
    for (size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].unlockName && sr3xtbl::NameEquals(*defs_[i].unlockName, name)) states_[i].unlocked = true;
}

void ChallengeTracker::unlockAll() {
    for (ChallengeState& s : states_) s.unlocked = true;
}

ChallengeUpdate ChallengeTracker::progress(int index, double amount) {
    ChallengeUpdate u;
    u.index = index;
    if (index < 0 || static_cast<size_t>(index) >= defs_.size()) return u;
    ChallengeState& s = states_[static_cast<size_t>(index)];
    const ChallengeDef& d = defs_[static_cast<size_t>(index)];
    if (!s.unlocked || s.completed || !(amount > 0.0)) return u;
    const double before = s.value;
    s.value += amount;
    if (s.value >= d.goal) {
        s.completed = true;
        s.value = d.goal;
        u.completedNow = true;
        u.notice = true;
        return u;
    }
    const double step = d.hudPercent ? d.goal * d.hudStep / 100.0 : d.hudStep;
    if (step > 0.0 && std::floor(s.value / step) > std::floor(before / step)) u.notice = true;
    return u;
}

std::vector<ChallengeUpdate> ChallengeTracker::statChanged(const std::string& stat, double delta) {
    std::vector<ChallengeUpdate> out;
    for (size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].trigger == ChallengeTrigger::Stat && sr3xtbl::NameEquals(defs_[i].stat, stat)) {
            const ChallengeUpdate u = progress(static_cast<int>(i), delta);
            if (u.notice || u.completedNow) out.push_back(u);
        }
    return out;
}

std::vector<ChallengeUpdate> ChallengeTracker::kill(const ChallengeKill& k) {
    std::vector<ChallengeUpdate> out;
    for (size_t i = 0; i < defs_.size(); ++i) {
        const ChallengeDef& d = defs_[i];
        if (d.trigger != ChallengeTrigger::Kill) continue;
        if (!TeamMatches(d.team, k.team, k.hostile) || !Matches(d.weapon, k.weapon) ||
            !Matches(d.weaponSlot, k.weaponSlot) || !Matches(d.damageType, k.damageType))
            continue;
        const ChallengeUpdate u = progress(static_cast<int>(i), 1.0);
        if (u.notice || u.completedNow) out.push_back(u);
    }
    return out;
}

std::vector<ChallengeUpdate> ChallengeTracker::destroy(const ChallengeDestroy& e) {
    std::vector<ChallengeUpdate> out;
    for (size_t i = 0; i < defs_.size(); ++i) {
        const ChallengeDef& d = defs_[i];
        if (d.trigger != ChallengeTrigger::Destroy) continue;
        if (!d.vehicles.empty() &&
            std::none_of(d.vehicles.begin(), d.vehicles.end(),
                         [&](const std::string& v) { return sr3xtbl::NameEquals(v, e.vehicle); }))
            continue;
        if (!Matches(d.vehicleType, e.vehicleType) || !Matches(d.team, e.team) || !Matches(d.weapon, e.weapon) ||
            !Matches(d.weaponSlot, e.weaponSlot))
            continue;
        const ChallengeUpdate u = progress(static_cast<int>(i), 1.0);
        if (u.notice || u.completedNow) out.push_back(u);
    }
    return out;
}

std::vector<ChallengeUpdate> ChallengeTracker::special(const std::string& specialCase, double amount,
                                                       const std::string& store) {
    std::vector<ChallengeUpdate> out;
    for (size_t i = 0; i < defs_.size(); ++i) {
        const ChallengeDef& d = defs_[i];
        if (d.trigger != ChallengeTrigger::Special || !sr3xtbl::NameEquals(d.specialCase, specialCase)) continue;
        if (!d.store.empty() && !sr3xtbl::NameEquals(d.store, store)) continue;
        const ChallengeUpdate u = progress(static_cast<int>(i), amount);
        if (u.notice || u.completedNow) out.push_back(u);
    }
    return out;
}

int ChallengeTracker::completedCount() const {
    return static_cast<int>(std::count_if(states_.begin(), states_.end(), [](const ChallengeState& s) { return s.completed; }));
}

}  // namespace sr3collect
