// Instanced activities: Activity record reader and reward grant - spec-activity-runtime.md
// section 6.1. See include/sr3activity/activity.h.

#include "sr3activity/activity.h"

#include <cctype>
#include <cmath>

namespace sr3activity {

namespace {

const sr3xtbl::Node* FindActivityRow(const sr3xtbl::Document& doc) {
    if (const sr3xtbl::Node* t = doc.table())
        if (const sr3xtbl::Node* a = sr3xtbl::FindChild(t, "Activity")) return a;
    if (const sr3xtbl::Node* r = doc.root()) {
        if (sr3xtbl::NameEquals(r->name(), "Activity")) return r;
        if (const sr3xtbl::Node* a = sr3xtbl::FindChild(r, "Activity")) return a;
    }
    return nullptr;
}

std::optional<std::string> Text(const sr3xtbl::Node* n, const char* name) {
    if (const std::string* t = sr3xtbl::ChildText(n, name)) return *t;
    return std::nullopt;
}

// A per-type field: on the Level row, in a per-type block one element below it (the
// shipped Snatch rows keep theirs in Level/Snatch_Info), or on the Activity row.
std::optional<double> Number(const sr3xtbl::Node* level, const sr3xtbl::Node* row, const char* name) {
    if (auto v = sr3xtbl::GetFloat(level, name)) return *v;
    for (const sr3xtbl::Node* c : level->children())
        if (auto v = sr3xtbl::GetFloat(c, name)) return *v;
    if (auto v = sr3xtbl::GetFloat(row, name)) return *v;
    return std::nullopt;
}

bool IsNone(const std::optional<std::string>& s) {
    return !s || s->empty() || sr3xtbl::NameEquals(*s, "none");
}

}  // namespace

std::optional<ActivityInstance> ReadActivityInstance(const sr3xtbl::Document& doc) {
    const sr3xtbl::Node* row = FindActivityRow(doc);
    if (!row) return std::nullopt;
    ActivityInstance a;
    a.name = Text(row, "Name").value_or("");
    a.exploredAtStart = sr3xtbl::GetBool(row, "Explored_at_start").value_or(false);
    a.endWarpSidewalk = sr3xtbl::GetBool(row, "End_Warp_Sidewalk").value_or(false);
    a.disableTrigger = sr3xtbl::GetBool(row, "Disable_Trigger").value_or(false);
    if (auto s = Text(row, "Start_Location")) a.startLocationEnabled = sr3xtbl::NameEquals(*s, "Enabled");
    a.playerOutfit = Text(row, "Player_Outfit");
    a.cutscene = Text(row, "Cutscene");
    a.startFunction = Text(row, "Start_function");
    a.cleanupFunction = Text(row, "Cleanup_function");
    a.warpLocation = Text(row, "Warp_Location");
    a.secondWarpLocation = Text(row, "Second_Warp_Location");
    a.endWarpLocation = Text(row, "End_Warp_Location");
    a.remoteEndWarpLocation = Text(row, "Remote_End_Warp_Location");
    for (auto* w : {&a.warpLocation, &a.secondWarpLocation, &a.endWarpLocation, &a.remoteEndWarpLocation})
        if (IsNone(*w)) w->reset();
    if (const sr3xtbl::Node* levels = sr3xtbl::FindChild(row, "Levels")) {
        for (const sr3xtbl::Node* lv : sr3xtbl::Children(levels, "Level")) {
            if (static_cast<int>(a.levels.size()) >= kMaxActivityLevels) break;
            ActivityLevel l;
            l.reward = Text(lv, "Reward");
            l.reward2 = Text(lv, "Reward2");
            if (IsNone(l.reward)) l.reward.reset();
            if (IsNone(l.reward2)) l.reward2.reset();
            // The awards are read only inside Level (6.3: the top-level siblings are not read).
            if (const std::string* c = sr3xtbl::ChildText(lv, "Cash_Award")) l.cashAward = sr3xtbl::ParseInt32(*c);
            if (const std::string* p = sr3xtbl::ChildText(lv, "Props_Award")) l.propsAward = sr3xtbl::ParseInt32(*p);
            l.goal = Number(lv, row, "Goal");
            l.coopGoal = Number(lv, row, "Coop_Goal");
            l.duration = Number(lv, row, "Duration");
            l.coopDuration = Number(lv, row, "Coop_Duration");
            l.maxSpots = Number(lv, row, "Max_Spots");
            l.crazyHitChance = Number(lv, row, "Crazy_Hit_Chance");
            l.hoWinAmount = Number(lv, row, "Ho_Win_Amount");
            a.levels.push_back(l);
        }
    }
    return a;
}

double Distance(const ActivityPos& a, const ActivityPos& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool AtActivityStart(const ActivityPos& player, const ActivityPos& start, double radius) {
    return Distance(player, start) <= radius;
}

ActivityType TypeFromInstanceName(const std::string& name) {
    std::string n;
    for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const size_t p = n.find("_a_");
    if (p == std::string::npos) return ActivityType::Unknown;
    const std::string code = n.substr(p + 3, n.find('_', p + 3) - (p + 3));
    if (code == "if") return ActivityType::InsuranceFraud;
    if (code == "dt") return ActivityType::Trafficking;
    if (code == "sn") return ActivityType::Snatch;
    if (code == "ha") return ActivityType::HeliAssault;
    if (code == "mh") return ActivityType::Mayhem;
    if (code == "tm") return ActivityType::TankMayhem;
    if (code == "es") return ActivityType::Escort;
    if (code == "rm") return ActivityType::Genki;
    return ActivityType::Unknown;
}

GrantResult GrantActivityRewards(const ActivityInstance& inst, ActivitySaveRecord& save,
                                 const ActivityTypeInfo& typeInfo, float respectMultiplier,
                                 sr3progression::PlayerRespect& player,
                                 const sr3progression::RespectTable& respectTable,
                                 sr3progression::AppliedUnlocks& applied,
                                 const sr3progression::AwardContext& ctx) {
    GrantResult r;
    for (int i = 0; i < static_cast<int>(inst.levels.size()) && i < 8; ++i) {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        if (save.doneMask & bit) continue;
        const ActivityLevel& lv = inst.levels[static_cast<size_t>(i)];
        LevelGrant g;
        g.level = i;
        save.doneMask |= bit;
        if (lv.reward) g.unlocks.push_back(*lv.reward);
        if (lv.reward2) g.unlocks.push_back(*lv.reward2);
        for (const std::string& u : g.unlocks) applied.insert(u);
        g.respectFirst = sr3progression::AwardRespect(player, lv.propsAward, respectTable, applied, ctx);
        const sr3progression::ActivityAwards aw =
            sr3progression::ActivityCompletionAwards(lv.propsAward, respectMultiplier);
        g.respectBonus = sr3progression::AwardRespect(player, aw.second, respectTable, applied, ctx);
        g.cashRaw = sr3progression::DollarsToRawCash(lv.cashAward);
        r.hints.push_back(kHintCash);
        r.hints.push_back(kHintRespect);
        if (!typeInfo.suppressCompletionHints) {
            r.hints.push_back(kHintCompleteA);
            r.hints.push_back(kHintCompleteB);
            if (typeInfo.completionHintId != -1) r.hints.push_back(typeInfo.completionHintId);
        }
        ++r.completionCountIncrement;
        r.levels.push_back(std::move(g));
    }
    return r;
}

}  // namespace sr3activity
