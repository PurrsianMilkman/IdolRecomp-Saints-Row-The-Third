#pragma once
// Synthetic ai_goals / ai_behavior / combat_actions / ai_personalities tables mirroring the shipped data the spec
// describes (spec-ai-runtime.md Sec21.1), shared by the NPC AI tests.
#include <memory>
#include <string>

#include "sr3airuntime/npc_ai_runtime.h"

namespace npc_ai_test {
using namespace sr3airuntime;

inline sr3xtbl::Document P(const std::string& s) { return sr3xtbl::ParseDocument(s); }

inline std::string GoalXml(const char* name, std::initializer_list<const char*> actions) {
    std::string s = std::string("<ai_goal><Name>") + name + "</Name><order_of_actions>";
    for (const char* a : actions) s += std::string("<action_elm><action>") + a + "</action></action_elm>";
    return s + "</order_of_actions></ai_goal>";
}

inline std::string ActionXml(const char* name, int noInterrupt, const char* flags) {
    std::string s = std::string("<actions><Name>") + name + "</Name><No_interrupt_ms>" + std::to_string(noInterrupt) +
                    "</No_interrupt_ms><precondition><pre_flags>";
    std::string f = flags;
    size_t pos = 0;
    while (pos < f.size()) {
        size_t comma = f.find(',', pos);
        if (comma == std::string::npos) comma = f.size();
        s += "<Flag>" + f.substr(pos, comma - pos) + "</Flag>";
        pos = comma + 1;
    }
    return s + "</pre_flags></precondition></actions>";
}

inline std::shared_ptr<NpcAiTables> MakeTables() {
    using namespace sr3tables_trafficai;
    const std::string goals =
        "<root><Table>" + GoalXml("change mode", {"move to post combat"}) +
        GoalXml("survive", {"cover fire", "cover stay down", "cover take def"}) +
        GoalXml("retreat", {"move to navmesh", "cover take def", "move retreat"}) +
        GoalXml("cover", {"human shield grab", "cover fire", "cover stay down", "cover take def"}) +
        GoalXml("advance", {"move to navmesh", "move to post combat", "cover advance", "move advance"}) +
        GoalXml("kill enemy", {"quick kill", "cover popout", "throw grenade", "fire sweep", "move close in", "fire",
                                "fire suppress"}) +
        GoalXml("acquire target", {"move to navmesh", "cover popout", "move chase", "move advance"}) +
        GoalXml("idle", {"idle"}) + "</Table></root>";
    const std::string actions =
        "<root><Table>" + ActionXml("fire", 250, "on foot,must have target") +
        ActionXml("cover take def", 1000, "on foot,must have target") +
        ActionXml("cover stay down", 500, "on foot,must have target,can do in cover") +
        ActionXml("cover fire", 500, "on foot,must have target,can do in cover") +
        ActionXml("cover popout", 500, "on foot,must have target,can do in cover") +
        ActionXml("cover advance", 500, "on foot,must have target,can do in cover") +
        ActionXml("move advance", 1000, "on foot,must have target") +
        ActionXml("move chase", 1000, "on foot,must have target") +
        ActionXml("move close in", 250, "on foot,must have target") +
        ActionXml("move retreat", 1000, "on foot,must have target") +
        ActionXml("move to navmesh", 250, "on foot") + ActionXml("move to post combat", 1000, "on foot") +
        ActionXml("human shield grab", 250, "on foot,must have target") +
        ActionXml("hold position", 1000, "on foot,in vehicle") + ActionXml("idle", 0, "on foot,in vehicle") +
        "</Table></root>";
    const std::string behaviors =
        "<root><Table>"
        "<Behavior><Name>Gang Average</Name><Scripted_Only>false</Scripted_Only><combat_aggression>1</combat_aggression>"
        "<Actions>"
        "<ability_elm><Action>fire</Action><Repeat_min>0.5</Repeat_min><Repeat_max>1.0</Repeat_max></ability_elm>"
        "<ability_elm><Action>cover take def</Action><Repeat_min>6</Repeat_min><Repeat_max>8</Repeat_max></ability_elm>"
        "<ability_elm><Action>cover stay down</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "<ability_elm><Action>cover fire</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "<ability_elm><Action>cover popout</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "<ability_elm><Action>cover advance</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "<ability_elm><Action>move advance</Action><Repeat_min>0.5</Repeat_min><Repeat_max>1.0</Repeat_max></ability_elm>"
        "<ability_elm><Action>move chase</Action><Repeat_min>0.5</Repeat_min><Repeat_max>1.0</Repeat_max></ability_elm>"
        "<ability_elm><Action>move retreat</Action><Repeat_min>0.5</Repeat_min><Repeat_max>1.0</Repeat_max></ability_elm>"
        "<ability_elm><Action>move to navmesh</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "<ability_elm><Action>move to post combat</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "<ability_elm><Action>idle</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm>"
        "</Actions>"
        "<Options><dodge_chance>80</dodge_chance></Options>"
        "<Goals>"
        "<Survive><low_health_pct>0.4</low_health_pct></Survive>"
        "<Cover><take_cover_when><Flag>shot at</Flag><Flag>damaged</Flag></take_cover_when>"
        "<abandon_cover_time>20</abandon_cover_time><find_new_cover_time>15</find_new_cover_time>"
        "<advance_cover_time>4</advance_cover_time><stay_down_time>3</stay_down_time><pop_up_time>5</pop_up_time>"
        "<squad_cover_pct>60</squad_cover_pct></Cover>"
        "<Taking_damage/><Advance/><Retreat/>"
        "<Kill_Enemy><seen_in_last>4</seen_in_last></Kill_Enemy>"
        "<Flush_Out><seen_in_last>5</seen_in_last></Flush_Out>"
        "<Acquire_Target><seen_in_last>10</seen_in_last></Acquire_Target>"
        "<Investigate><seen_in_last>20</seen_in_last></Investigate>"
        "</Goals></Behavior>"
        "<Behavior><Name>Civilian Unarmed</Name><combat_aggression>0</combat_aggression>"
        "<Actions><ability_elm><Action>idle</Action><Repeat_min>0</Repeat_min><Repeat_max>0</Repeat_max></ability_elm></Actions>"
        "<Goals><Survive><low_health_pct>0.4</low_health_pct></Survive>"
        "<Kill_Enemy><seen_in_last>2</seen_in_last></Kill_Enemy>"
        "<Acquire_Target><seen_in_last>15</seen_in_last></Acquire_Target></Goals></Behavior>"
        "</Table></root>";
    const std::string personalities =
        "<root><Table><Personality><Name>civilian normal</Name><combat_aggression>0</combat_aggression>"
        "<Flee_tweaks><Flee_health_pct>50</Flee_health_pct><flee_events><Flag>bullet hit</Flag></flee_events>"
        "</Flee_tweaks></Personality></Table></root>";
    auto g = ParseAiGoalsTable(P(goals));
    auto b = ParseAiBehaviorTable(P(behaviors));
    auto a = sr3tables_weapons::ParseAllCombatActionOverrides(P(actions));
    auto p = ParseAiPersonalitiesTable(P(personalities));
    return std::make_shared<NpcAiTables>(BuildNpcAiTables(g, b, a, &p));
}


}  // namespace npc_ai_test
