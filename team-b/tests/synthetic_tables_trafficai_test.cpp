// Synthetic tests for sr3tables_trafficai, built FROM THE TEXT of
// spec-tables-traffic-ai.md (hand-written XML fixtures), never from the
// reader's own source. A pass here proves the reader implements the spec's
// element trees and reader-flavour rules; it says nothing about whether real
// base-game rows look like these fixtures (that is
// tools/validation/validate_tables_trafficai_population.cpp).
//
// Each block below names the spec section/rule it exercises. Negative/absent
// cases are checked alongside positive ones wherever the rule under test has
// two sides (present vs absent, matched vs unmatched, case-sensitive vs not).

#include <cstring>
#include <iostream>
#include <string>

#include "sr3tables_trafficai/tables.h"
#include "sr3xtbl/xtbl.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

using namespace sr3tables_trafficai;

sr3xtbl::Document P(std::string_view s) { return sr3xtbl::ParseDocument(s); }

bool near(float a, float b, float tol = 0.0001f) { return (a > b ? a - b : b - a) <= tol; }

// ---------------------------------------------------------------------------
// §2 traffic_lanes.xtbl
// ---------------------------------------------------------------------------
void testTrafficLanes() {
    sr3xtbl::Document d = P(
        "<root><Table><NewEntity><LaneGrid>"
        "<LaneSpeed><Speed>5.5</Speed></LaneSpeed>"
        "<LaneSpeed><Speed>12</Speed></LaneSpeed>"
        "<LaneSpeed></LaneSpeed>" // no Speed child -> unspecified (Always.present == false)
        "</LaneGrid></NewEntity></Table></root>");
    TrafficLanesTable t = ParseTrafficLanesTable(d);
    CHECK(t.laneSpeeds.size() == 3);
    CHECK(t.laneSpeeds[0].speed.present && near(t.laneSpeeds[0].speed.value, 5.5f));
    CHECK(t.laneSpeeds[1].speed.present && near(t.laneSpeeds[1].speed.value, 12.0f));
    CHECK(!t.laneSpeeds[2].speed.present); // spec §2: absent Speed -> unspecified, not 0
}

// ---------------------------------------------------------------------------
// §4 ambient_traffic_events.xtbl - normal row, boundary values, missing optional
// ---------------------------------------------------------------------------
void testAmbientTrafficEvents() {
    // Normal row with the boundary values the spec calls out: TOD_start 0..23,
    // TOD_end 0..24, Min_life/Cooldown_time 0.1..240.0.
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Ambient_traffic_event>"
        "<Name>Downtown Sweep</Name>"
        "<TOD_start>23</TOD_start><TOD_end>24</TOD_end>"
        "<Flags><Flag>disabled_during_notoriety</Flag></Flags>"
        "<Min_life>0.1</Min_life><Max_life>240.0</Max_life><Cooldown_time>240.0</Cooldown_time>"
        "<Roadblock_layout>Downtown_Layout</Roadblock_layout>"
        "</Ambient_traffic_event>"
        "<Ambient_traffic_event>" // minimal row: no Name, no Flags, no Roadblock_layout
        "<TOD_start>0</TOD_start><TOD_end>0</TOD_end>"
        "<Min_life>1.0</Min_life><Max_life>2.0</Max_life><Cooldown_time>3.0</Cooldown_time>"
        "</Ambient_traffic_event>"
        "</Table></root>");
    AmbientTrafficEventsTable t = ParseAmbientTrafficEventsTable(d);
    CHECK(t.rows.size() == 2);

    const AmbientTrafficEvent& r0 = t.rows[0];
    CHECK(r0.name.has_value() && *r0.name == "Downtown Sweep");
    CHECK(r0.todStart.present && r0.todStart.value == 23);
    CHECK(r0.todEnd.present && r0.todEnd.value == 24);
    CHECK(r0.flagsPresent && r0.disabledDuringNotoriety);
    CHECK(r0.minLife.present && near(r0.minLife.value, 0.1f));
    CHECK(r0.maxLife.present && near(r0.maxLife.value, 240.0f));
    CHECK(r0.cooldownTime.present && near(r0.cooldownTime.value, 240.0f));
    CHECK(r0.roadblockLayout.has_value() && *r0.roadblockLayout == "Downtown_Layout");

    const AmbientTrafficEvent& r1 = t.rows[1];
    CHECK(!r1.name.has_value());              // absent Name -> nullopt
    CHECK(!r1.flagsPresent);                  // absent Flags container -> not present
    CHECK(!r1.disabledDuringNotoriety);       // no Flag children -> HasFlag false
    CHECK(!r1.roadblockLayout.has_value());   // absent Roadblock_layout -> nullopt
}

// ---------------------------------------------------------------------------
// §5 roadblock_layouts.xtbl - flag list, enum-index Type, nested Object/Child
// ---------------------------------------------------------------------------
void testRoadblockLayouts() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Roadblock_layouts>"
        "<Name>Bridge_Block</Name><Num_lanes>2</Num_lanes>"
        "<Flags><Flag>end cap</Flag><Flag>no flee</Flag><Flag>made up flag</Flag></Flags>"
        "<Objects>"
        "<Object>"
        "<Type>vehicle</Type>" // lower-case: EnumIndex is case-insensitive (spec 1.3/§5)
        "<Resource_name>police_car</Resource_name>"
        "<X_offset>1.5</X_offset><Z_offset>-2.5</Z_offset><Heading>0.75</Heading><Hitpoints>500</Hitpoints>"
        "<Children>"
        "<Child><Type>NPC</Type><Resource_name>cop01</Resource_name>"
        "<X_offset>0.1</X_offset><Z_offset>0.2</Z_offset><Heading>0.0</Heading></Child>"
        "</Children>"
        "</Object>"
        "<Object><Type>Not A Real Type</Type><Resource_name>x</Resource_name></Object>"
        "</Objects>"
        "</Roadblock_layouts>"
        "</Table></root>");
    RoadblockLayoutsTable t = ParseRoadblockLayoutsTable(d);
    CHECK(t.rows.size() == 1);
    const RoadblockLayout& l = t.rows[0];
    CHECK(l.name.has_value() && *l.name == "Bridge_Block");
    CHECK(l.numLanes.present && l.numLanes.value == 2);
    CHECK(l.flagEndCap && l.flagNoFlee);
    CHECK(!l.flagIntersection && !l.flagCanSpawnOnDisabledLanes); // not listed -> false
    CHECK(l.objects.size() == 2);

    const RoadblockObject& o0 = l.objects[0];
    CHECK(o0.type.text.has_value() && *o0.type.text == "vehicle");
    CHECK(o0.type.index == 0); // "Vehicle" is index 0 of kRoadblockObjectTypeNames
    CHECK(o0.resourceName.has_value() && *o0.resourceName == "police_car");
    CHECK(o0.xOffset.present && near(o0.xOffset.value, 1.5f));
    CHECK(o0.zOffset.present && near(o0.zOffset.value, -2.5f));
    CHECK(o0.hitpoints.has_value() && *o0.hitpoints == 500);
    CHECK(o0.children.size() == 1);
    CHECK(o0.children[0].type.index == 2); // "NPC" is index 2
    CHECK(o0.children[0].resourceName.has_value() && *o0.children[0].resourceName == "cop01");
    CHECK(!o0.children[0].hitpoints.has_value()); // absent Hitpoints -> nullopt (if-present)

    const RoadblockObject& o1 = l.objects[1];
    CHECK(o1.type.text.has_value() && *o1.type.text == "Not A Real Type");
    CHECK(o1.type.index == -1); // no match -> -1 (sr3xtbl::EnumIndex)
}

// ---------------------------------------------------------------------------
// §6 roadblock_notoriety.xtbl
// ---------------------------------------------------------------------------
void testRoadblockNotoriety() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Roadblock_notoriety><Name>Saints_Notoriety</Name><Team>saints</Team>"
        "<Levels><Level>"
        "<Notoriety_level>3</Notoriety_level><Max_active_roadblocks>2</Max_active_roadblocks>"
        "<Min_time>60</Min_time><Max_time>300</Max_time>"
        "<Roadblock_layouts>"
        "<Roadblock_layout><Roadblock>Bridge_Block</Roadblock><Cooldown>120</Cooldown></Roadblock_layout>"
        "</Roadblock_layouts>"
        "</Level></Levels>"
        "</Roadblock_notoriety>"
        "</Table></root>");
    RoadblockNotorietyTable t = ParseRoadblockNotorietyTable(d);
    CHECK(t.rows.size() == 1);
    CHECK(t.rows[0].team.has_value() && *t.rows[0].team == "saints");
    CHECK(t.rows[0].levels.size() == 1);
    const RoadblockNotorietyLevel& lvl = t.rows[0].levels[0];
    CHECK(lvl.notorietyLevel.value == 3 && lvl.maxActiveRoadblocks.value == 2);
    CHECK(lvl.roadblockLayoutsPresent);
    CHECK(lvl.layouts.size() == 1);
    CHECK(lvl.layouts[0].roadblock.has_value() && *lvl.layouts[0].roadblock == "Bridge_Block");
    CHECK(lvl.layouts[0].cooldown.value == 120);
}

// ---------------------------------------------------------------------------
// §7 roadblock_stag_lockdown.xtbl - the "Roadbock" (sic) misspelling
// ---------------------------------------------------------------------------
void testRoadblockStagLockdown() {
    // A correctly-spelled "Roadblock" element must be INVISIBLE to this reader
    // (spec §7: the engine's own reader looks for the misspelling).
    sr3xtbl::Document d = P(
        "<root><Table><Roadblock_stag_lockdown><Roadblock_List>"
        "<Roadbock><Roadblock_Layout>Bridge_Block</Roadblock_Layout><Spawn_Point>spawn01</Spawn_Point></Roadbock>"
        "<Roadblock><Roadblock_Layout>Should_Be_Invisible</Roadblock_Layout></Roadblock>"
        "</Roadblock_List></Roadblock_stag_lockdown></Table></root>");
    RoadblockStagLockdownTable t = ParseRoadblockStagLockdownTable(d);
    CHECK(t.rowPresent);
    CHECK(t.entries.size() == 1); // only the "Roadbock" (sic) entry is read
    CHECK(t.entries[0].roadblockLayout.has_value() && *t.entries[0].roadblockLayout == "Bridge_Block");
    CHECK(t.entries[0].spawnPoint.has_value() && *t.entries[0].spawnPoint == "spawn01");

    // Missing Roadblock_List -> rowPresent false.
    sr3xtbl::Document d2 = P("<root><Table><Roadblock_stag_lockdown></Roadblock_stag_lockdown></Table></root>");
    RoadblockStagLockdownTable t2 = ParseRoadblockStagLockdownTable(d2);
    CHECK(!t2.rowPresent);
    CHECK(t2.entries.empty());
}

// ---------------------------------------------------------------------------
// §8 panic_reactions.xtbl - optional Timed/Movement/Drug_Effects containers
// ---------------------------------------------------------------------------
void testPanicReactions() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Panic><Name>On Fire</Name><Priority>5</Priority>"
        "<Movement><Run>True</Run><Min_Dist>1.0</Min_Dist><Max_Dist>5.0</Max_Dist></Movement>"
        "<Player_Animations><Anim_State>burning</Anim_State><NumAnimVariants>3</NumAnimVariants></Player_Animations>"
        "</Panic>"
        "<Panic><Name>No_Timed_No_Movement</Name><Priority>1</Priority></Panic>"
        "</Table></root>");
    PanicReactionsTable t = ParsePanicReactionsTable(d);
    CHECK(t.rows.size() == 2);

    const PanicReaction& r0 = t.rows[0];
    CHECK(!r0.timedPresent);
    CHECK(!r0.minTimeNpc.present); // Timed absent -> unspecified (Always.present == false)
    CHECK(r0.movementPresent);
    CHECK(r0.movementRun.present && r0.movementRun.value == true);
    CHECK(r0.minDist.present && near(r0.minDist.value, 1.0f));
    CHECK(r0.playerAnimationsPresent);
    CHECK(r0.playerAnimations.animState.has_value() && *r0.playerAnimations.animState == "burning");
    CHECK(r0.playerAnimations.numAnimVariants.present && r0.playerAnimations.numAnimVariants.value == 3);
    CHECK(!r0.npcAnimationsPresent);

    const PanicReaction& r1 = t.rows[1];
    CHECK(!r1.timedPresent && !r1.movementPresent && !r1.drugEffectsPresent);
    CHECK(!r1.movementRun.present); // Movement absent -> Run unspecified too
}

// ---------------------------------------------------------------------------
// §9 ai_goals.xtbl - enum-index Name/action against the compiled-in §1.5 lists
// ---------------------------------------------------------------------------
void testAiGoals() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<ai_goal><Name>idle</Name><order_of_actions>"
        "<action_elm><action>idle</action></action_elm>"
        "<action_elm><action>not a real action</action></action_elm>"
        "</order_of_actions></ai_goal>"
        "<ai_goal><Name>not a real goal</Name></ai_goal>"
        "</Table></root>");
    AiGoalsTable t = ParseAiGoalsTable(d);
    CHECK(t.rows.size() == 2);
    CHECK(t.rows[0].name.index == 16); // "idle" is the last (17th) of kAiGoalNames, index 16
    CHECK(t.rows[0].actions.size() == 2);
    CHECK(t.rows[0].actions[0].action.index == 69); // "idle" is the last (70th) combat action, index 69
    CHECK(t.rows[0].actions[1].action.index == -1); // unmatched
    CHECK(t.rows[1].name.index == -1);
}

// ---------------------------------------------------------------------------
// §10 ai_behavior.xtbl - goal presence gating, Cover's take_cover_when flags
// ---------------------------------------------------------------------------
void testAiBehavior() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Behavior><Name>Aggressive</Name>"
        "<Goals>"
        "<Cover><take_cover_when><Flag>aimed at</Flag><Flag>damaged</Flag></take_cover_when>"
        "<abandon_cover_time>4.0</abandon_cover_time></Cover>"
        "<Kill_Enemy><seen_in_last>2.5</seen_in_last></Kill_Enemy>"
        "<goals_can_process_early>true</goals_can_process_early>"
        "</Goals>"
        "<Actions><ability_elm><Action>reload</Action><Repeat_min>0.5</Repeat_min><Repeat_max>1.0</Repeat_max></ability_elm></Actions>"
        "</Behavior>"
        "</Table></root>");
    AiBehaviorTable t = ParseAiBehaviorTable(d);
    CHECK(t.rows.size() == 1);
    const AiBehavior& b = t.rows[0];
    CHECK(!b.survivePresent);       // Goals/Survive absent
    CHECK(!b.survivalLowHealthPct.present);
    CHECK(b.cover.present);
    CHECK(b.cover.takeCoverAimedAt && b.cover.takeCoverDamaged && !b.cover.takeCoverShotAt);
    CHECK(b.cover.abandonCoverTime.present && near(b.cover.abandonCoverTime.value, 4.0f));
    CHECK(!b.cover.findNewCoverTime.present); // present container, absent leaf -> unspecified
    CHECK(b.killEnemy.present && b.killEnemy.seenInLast.present && near(b.killEnemy.seenInLast.value, 2.5f));
    CHECK(!b.flushOut.present && !b.flushOut.seenInLast.present);
    CHECK(!b.group.present); // Goals/Group absent
    CHECK(b.actions.size() == 1 && b.actions[0].action.index == 44); // "reload" (kCombatActionNames[44])
    // spec §10 (CORRECTED 2026-10-02): goals_can_process_early is a child of Goals.
    CHECK(b.goalsCanProcessEarly.present && b.goalsCanProcessEarly.value == true);
}

// ---------------------------------------------------------------------------
// §10 ai_behavior.xtbl - goals_can_process_early's parent (regression for the
// CORRECTED 2026-10-02 fix: it is a child of Goals, NOT a row-level child of
// Behavior as this project previously, incorrectly, read it).
// ---------------------------------------------------------------------------
void testAiBehaviorGoalsCanProcessEarlyParent() {
    // Placed correctly, under Goals: must be read.
    sr3xtbl::Document underGoals = P(
        "<root><Table>"
        "<Behavior><Name>UnderGoals</Name>"
        "<Goals><goals_can_process_early>true</goals_can_process_early></Goals>"
        "</Behavior>"
        "</Table></root>");
    AiBehaviorTable tg = ParseAiBehaviorTable(underGoals);
    CHECK(tg.rows.size() == 1);
    CHECK(tg.rows[0].goalsCanProcessEarly.present && tg.rows[0].goalsCanProcessEarly.value == true);

    // Placed at row level (a sibling of Goals, not inside it): must NOT be read -
    // this is the old (wrong) reading this project previously used; it must now
    // report absent, not true, proving the reader no longer falls back to row-level.
    sr3xtbl::Document rowLevel = P(
        "<root><Table>"
        "<Behavior><Name>RowLevelOnly</Name>"
        "<Goals><Survive><low_health_pct>0.3</low_health_pct></Survive></Goals>"
        "<goals_can_process_early>true</goals_can_process_early>"
        "</Behavior>"
        "</Table></root>");
    AiBehaviorTable tr = ParseAiBehaviorTable(rowLevel);
    CHECK(tr.rows.size() == 1);
    CHECK(!tr.rows[0].goalsCanProcessEarly.present); // row-level placement is invisible to the fixed reader
}

// ---------------------------------------------------------------------------
// §11 ai_personalities.xtbl - named-leaf percentage groups
// ---------------------------------------------------------------------------
void testAiPersonalities() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Personality><Name>Calm</Name>"
        "<Ambient_tweaks><attack_unfriendly>10</attack_unfriendly>"
        "<Taunt_reaction><flee>20</flee><taunt_back>30</taunt_back><attack>50</attack></Taunt_reaction>"
        "</Ambient_tweaks>"
        "</Personality>"
        "</Table></root>");
    AiPersonalitiesTable t = ParseAiPersonalitiesTable(d);
    CHECK(t.rows.size() == 1);
    CHECK(t.rows[0].ambientTweaksPresent);
    CHECK(t.rows[0].attackUnfriendly.present && t.rows[0].attackUnfriendly.value == 10);
    CHECK(t.rows[0].tauntReaction.flee.value == 20);
    CHECK(t.rows[0].tauntReaction.tauntBack.value == 30);
    CHECK(t.rows[0].tauntReaction.attack.value == 50);
    CHECK(!t.rows[0].fleeTweaksPresent);
}

// ---------------------------------------------------------------------------
// §12 generic_vehicles.xtbl - CASE-SENSITIVE Name match (unlike most enums)
// ---------------------------------------------------------------------------
void testGenericVehicles() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Generics_Table><Name>Police Car</Name><Spawn_Name>police_car_01</Spawn_Name></Generics_Table>"
        "<Generics_Table><Name>police car</Name><Spawn_Name>x</Spawn_Name></Generics_Table>" // wrong case
        "</Table></root>");
    GenericVehiclesTable t = ParseGenericVehiclesTable(d);
    CHECK(t.rows.size() == 2);
    CHECK(t.rows[0].index == 1); // "Police Car" is kGenericVehicleSlotNames[1]
    CHECK(t.rows[1].index == -1); // exact-case mismatch -> no match (spec §12: case-SENSITIVE)
}

// ---------------------------------------------------------------------------
// §12 generic_characters.xtbl - CASE-INSENSITIVE Name match (unlike the vehicle
// file above). Regression for the 2026-10-02 RESOLVED fix: the 21 slot names are
// now known in slot order and resolved to an index, like the vehicle file already
// was.
// ---------------------------------------------------------------------------
void testGenericCharacters() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Generics_Table><Name>Old Female</Name><Spawn_Name>npc_old_female</Spawn_Name></Generics_Table>"
        "<Generics_Table><Name>old female</Name><Spawn_Name>x</Spawn_Name></Generics_Table>" // different case
        "<Generics_Table><Name>Not A Slot</Name><Spawn_Name>y</Spawn_Name></Generics_Table>"
        "</Table></root>");
    GenericCharactersTable t = ParseGenericCharactersTable(d);
    CHECK(t.rows.size() == 3);
    CHECK(t.rows[0].index == 3); // "Old Female" is kGenericCharacterSlotNames[3]
    CHECK(t.rows[1].index == 3); // case-insensitive match (spec §12: CRC lower-cases every byte)
    CHECK(t.rows[2].index == -1); // not one of the 21 slot names
}

// ---------------------------------------------------------------------------
// §13.1 action_node_groups.xtbl - explicit-default (optional) vs always-write
// ---------------------------------------------------------------------------
void testActionNodeGroups() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<action_node_group><Name>Bench01</Name><Min_spacing>3.0</Min_spacing>"
        "<npc_list><npc>Wino</npc><npc>Drunk</npc></npc_list>"
        "</action_node_group>"
        "</Table></root>");
    ActionNodeGroupsTable t = ParseActionNodeGroupsTable(d);
    CHECK(t.rows.size() == 1);
    const ActionNodeGroup& g = t.rows[0];
    CHECK(!g.convertToPedMinDelay.has_value());   // absent, explicit-default field -> nullopt (caller applies default 0)
    CHECK(!g.convertToPedMaxDelay.present);       // absent, always-write field -> unspecified
    CHECK(!g.minPlayerRank.has_value());          // absent, explicit-default field -> nullopt (caller applies default 0)
    CHECK(!g.instanceCap.has_value() && !g.maxSpawns.has_value());
    CHECK(g.npcList.size() == 2);
    CHECK(g.npcList[0].has_value() && *g.npcList[0] == "Wino");
    CHECK(!g.referenceObjectPresent);
}

// ---------------------------------------------------------------------------
// §13.3 action_node_npcs.xtbl - the TextBool convention (True/False vs
// sr3xtbl's own true/yes bool reader)
// ---------------------------------------------------------------------------
void testActionNodeNpcs() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<action_node_npc><Name>Bum01</Name>"
        "<Single_Use>True</Single_Use><Get_Bag>False</Get_Bag>"
        "<Reverse_Enter>yes</Reverse_Enter>" // NOT "True"/"False" -> must read as Unset, unlike sr3xtbl::ParseBool
        "<Sex>FEMALE</Sex><Team>Gang</Team>"
        "</action_node_npc>"
        "<action_node_npc><Name>Bum02</Name></action_node_npc>" // everything absent
        "</Table></root>");
    ActionNodeNpcsTable t = ParseActionNodeNpcsTable(d);
    CHECK(t.rows.size() == 2);
    const ActionNodeNpc& n0 = t.rows[0];
    CHECK(n0.singleUse == TextBool::True);
    CHECK(n0.getBag == TextBool::False);
    CHECK(n0.reverseEnter == TextBool::Unset); // "yes" is not "True"/"False" under this table's own convention
    CHECK(n0.sex.index == 1);  // "FEMALE" matches "female" case-insensitively, index 1
    CHECK(n0.team.index == 2); // "Gang" is kTeamNames[2]

    const ActionNodeNpc& n1 = t.rows[1];
    CHECK(n1.stationaryNode == TextBool::Unset); // absent -> Unset (engine's own pre-set default is True, not applied here)
    CHECK(n1.sex.index == -1 && n1.team.index == -1);
}

// ---------------------------------------------------------------------------
// §13.4 action_nodes.xtbl - NO <Table> wrapper; the seven-float `transform` text
// ---------------------------------------------------------------------------
void testActionNodes() {
    sr3xtbl::Document d = P(
        "<root>"
        "<object><name>Block42</name><num_action_nodes>2</num_action_nodes>"
        "<action_node><group>Bench_Group</group><npc>Wino</npc>"
        "<transform>1.0 2.0 3.0 0.0 0.0 0.0 1.0</transform><player_node>true</player_node></action_node>"
        "<spawn_node><group>Spawn_Group</group><name>sp1</name><transform>4 5 6</transform></spawn_node>"
        "</object>"
        "</root>");
    ActionNodesFile f = ParseActionNodesFile(d);
    CHECK(f.objects.size() == 1);
    const ActionNodePlacementObject& o = f.objects[0];
    CHECK(o.name.has_value() && *o.name == "Block42");
    CHECK(o.numActionNodes.present && o.numActionNodes.value == 2);
    CHECK(o.actionNodes.size() == 1);
    const ActionNodeTransform& tr = o.actionNodes[0].transform;
    CHECK(tr.present && tr.tokenCount == 7);
    CHECK(near(tr.values[0], 1.0f) && near(tr.values[2], 3.0f) && near(tr.values[6], 1.0f));
    CHECK(o.actionNodes[0].playerNode.present && o.actionNodes[0].playerNode.value == true);
    CHECK(o.spawnNodes.size() == 1);
    const ActionNodeTransform& tr2 = o.spawnNodes[0].transform;
    CHECK(tr2.present && tr2.tokenCount == 3); // fewer than 7 tokens supplied - only that many are kept
    CHECK(near(tr2.values[0], 4.0f) && near(tr2.values[2], 6.0f));

    // A row with no transform element at all.
    sr3xtbl::Document d2 = P("<root><object><name>Empty</name></object></root>");
    ActionNodesFile f2 = ParseActionNodesFile(d2);
    CHECK(f2.objects.size() == 1);
    CHECK(f2.objects[0].actionNodes.empty() && f2.objects[0].spawnNodes.empty());
}

// ---------------------------------------------------------------------------
// §14.2 distant_ped_colors.xtbl - the R/G/B convention (not sr3xtbl's X/Y/Z vec3)
// ---------------------------------------------------------------------------
void testDistantPedColors() {
    sr3xtbl::Document d = P(
        "<root><Table><Color_Set>"
        "<Skin_Colors><Skin_Color><R>255</R><G>200</G><B>150</B></Skin_Color></Skin_Colors>"
        "<Clothing_Colors><Clothing_Color_Set>"
        "<Undershirt_Color><R>1</R><G>2</G><B>3</B></Undershirt_Color>"
        "</Clothing_Color_Set></Clothing_Colors>"
        "</Color_Set></Table></root>");
    DistantPedColorsTable t = ParseDistantPedColorsTable(d);
    CHECK(t.colorSets.size() == 1);
    CHECK(t.colorSets[0].skinColors.size() == 1);
    CHECK(t.colorSets[0].skinColors[0].r.present && near(t.colorSets[0].skinColors[0].r.value, 255.0f));
    CHECK(t.colorSets[0].hairColors.empty());
    CHECK(t.colorSets[0].clothingColors.size() == 1);
    CHECK(near(t.colorSets[0].clothingColors[0].undershirt.g.value, 2.0f));
    CHECK(!t.colorSets[0].clothingColors[0].overshirt.r.present); // absent Overshirt_Color -> unspecified
}

// ---------------------------------------------------------------------------
// §14.3 distant_*_spawn_parameters.xtbl - whole-file optional (absent -> nullopt)
// ---------------------------------------------------------------------------
void testDistantSpawnParameters() {
    sr3xtbl::Document present = P(
        "<root><Table><Distant_Ped_Spawn_Parameters>"
        "<Length_Per_Spawn>0.04</Length_Per_Spawn><Base_Spawning_Dist>200</Base_Spawning_Dist>"
        "<Spawn_Angle>0.8</Spawn_Angle>"
        "</Distant_Ped_Spawn_Parameters></Table></root>");
    auto p = ParseDistantPedSpawnParameters(present);
    CHECK(p.has_value());
    CHECK(p->lengthPerSpawn.present && near(p->lengthPerSpawn.value, 0.04f));
    CHECK(!p->maxSpawningDist.present); // present element, absent leaf

    sr3xtbl::Document absent = P("<root><Table></Table></root>");
    CHECK(!ParseDistantPedSpawnParameters(absent).has_value());
    CHECK(!ParseDistantVehicleSpawnParameters(absent).has_value());
}

// ---------------------------------------------------------------------------
// §15 homies.xtbl - CASE-SENSITIVE Flags (unlike most flag containers)
// ---------------------------------------------------------------------------
void testHomies() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Homie><Name>Angry Tiger</Name>"
        "<Humans><Human><Character>npc_angry_tiger</Character></Human></Humans>"
        "<Vehicles><Vehicle_Entry><Vehicle>bootlegger</Vehicle><Weight>1</Weight></Vehicle_Entry></Vehicles>"
        "<Flags><Flag>requires saints</Flag><Flag>Requires Saints</Flag><Flag>taxi</Flag></Flags>"
        "<Is_DLC>True</Is_DLC><Framework>dlc1</Framework>"
        "</Homie>"
        "</Table></root>");
    HomiesTable t = ParseHomiesTable(d);
    CHECK(t.rows.size() == 1);
    const Homie& h = t.rows[0];
    CHECK(h.humans.size() == 1 && h.humans[0].character.has_value() && *h.humans[0].character == "npc_angry_tiger");
    CHECK(h.vehicles.size() == 1 && h.vehicles[0].weight.value == 1);
    CHECK(h.flagRequiresSaints);       // exact-case "requires saints" matched
    CHECK(h.flagTaxi);
    CHECK(!h.flagClearGangNotoriety);  // not present
    CHECK(h.isDlc.present && h.isDlc.value == true);
    CHECK(h.framework.has_value() && *h.framework == "dlc1");
}

// ---------------------------------------------------------------------------
// §16 follower_heads.xtbl
// ---------------------------------------------------------------------------
void testFollowerHeads() {
    sr3xtbl::Document d = P(
        "<root><Table><Heads><Associations>"
        "<Follower><Persona>event_x</Persona><Charname>tami</Charname><Bitmap>tami.tga</Bitmap></Follower>"
        "<Follower><Charname>nobitmap</Charname></Follower>"
        "</Associations></Heads></Table></root>");
    FollowerHeadsTable t = ParseFollowerHeadsTable(d);
    CHECK(t.rows.size() == 2);
    CHECK(t.rows[0].bitmap.has_value() && *t.rows[0].bitmap == "tami.tga");
    CHECK(!t.rows[1].bitmap.has_value()); // absent Bitmap -> nullopt
}

// ---------------------------------------------------------------------------
// §17 driver_bailout.xtbl - whole-file optional + explicit-default leaves
// ---------------------------------------------------------------------------
void testDriverBailout() {
    sr3xtbl::Document absent = P("<root><Table></Table></root>");
    CHECK(!ParseDriverBailout(absent).has_value());

    sr3xtbl::Document d = P(
        "<root><Table><Driver_Bailout>"
        "<Max_Distance>50.0</Max_Distance><Min_Distance>5.0</Min_Distance>"
        "<Show_Damage_Record>true</Show_Damage_Record>"
        "</Driver_Bailout></Table></root>");
    auto b = ParseDriverBailout(d);
    CHECK(b.has_value());
    CHECK(near(b->maxDistance.value, 50.0f));
    CHECK(b->showDamageRecord.present && b->showDamageRecord.value == true);
    CHECK(!b->recordDisplayTime.has_value()); // explicit "default 0 when absent" field -> nullopt here
    CHECK(!b->endTime.present);               // no stated default -> unspecified (Always)
}

// ---------------------------------------------------------------------------
// §18 vehicle_despawn.xtbl - the nested Table/Table shape
// ---------------------------------------------------------------------------
void testVehicleDespawn() {
    sr3xtbl::Document noInner = P("<root><Table></Table></root>");
    CHECK(!ParseVehicleDespawnTable(noInner).has_value());

    sr3xtbl::Document d = P(
        "<root><Table><Table>"
        "<in_view_despawn_info><abandon_despawn_info><num_cars>3</num_cars><time>10</time></abandon_despawn_info></in_view_despawn_info>"
        "<abandon_despawn_delay>5</abandon_despawn_delay>"
        "<player_vehicle_despawn_distance>100</player_vehicle_despawn_distance>"
        "</Table></Table></root>");
    auto t = ParseVehicleDespawnTable(d);
    CHECK(t.has_value());
    CHECK(t->inViewDespawnInfo.size() == 1 && t->inViewDespawnInfo[0].numCars.value == 3);
    CHECK(t->outOfViewDespawnInfo.empty());
    CHECK(t->abandonDespawnDelay.present && t->abandonDespawnDelay.value == 5);
}

// ---------------------------------------------------------------------------
// §19 Escort_constants.xtbl - group-level presence (whole group absent vs a
// present group missing a leaf)
// ---------------------------------------------------------------------------
void testEscortConstants() {
    sr3xtbl::Document d = P(
        "<root><Table><Escort_Constants><Tiger_Constants>"
        "<Rage><Desired_speed_MPS>20.0</Desired_speed_MPS></Rage>"
        "</Tiger_Constants></Escort_Constants></Table></root>");
    auto c = ParseEscortConstants(d);
    CHECK(c.has_value());
    CHECK(c->rage.present);
    CHECK(c->rage.desiredSpeedMps.present && near(c->rage.desiredSpeedMps.value, 20.0f));
    CHECK(!c->rage.rageAttackDamageHp.present); // present group, absent leaf -> unspecified
    CHECK(!c->vehicles.present);                // whole group absent
    CHECK(!c->vehicles.vehicleDamagePenaltyMs.present);

    // Vehicle_Damage_Penalty_MS is a u32 (spec s19): fraction text is truncated by the integer grammar.
    sr3xtbl::Document u = P(
        "<root><Table><Escort_Constants><Tiger_Constants><Penalties><Vehicles>"
        "<Vehicle_Damage_Penalty_MS>4500.75</Vehicle_Damage_Penalty_MS>"
        "</Vehicles></Penalties></Tiger_Constants></Escort_Constants></Table></root>");
    auto cu = ParseEscortConstants(u);
    CHECK(cu.has_value() && cu->vehicles.vehicleDamagePenaltyMs.present);
    CHECK(cu->vehicles.vehicleDamagePenaltyMs.value == 4500u);

    // spec s19 (RESOLVED 2026-10-02): ALL seven `_MS`-suffixed leaves are u32, not
    // just Vehicle_Damage_Penalty_MS - regression for the fix (previously these six
    // were wrongly read as float). Fraction text truncates the same way.
    sr3xtbl::Document ms = P(
        "<root><Table><Escort_Constants><Tiger_Constants><Penalties>"
        "<Vehicles><Vehicle_Damage_Cooldown_MS>250.9</Vehicle_Damage_Cooldown_MS></Vehicles>"
        "<Humans><Human_Damage_Penalty_MS>2000.9</Human_Damage_Penalty_MS></Humans>"
        "<Movers><Mover_Damage_Penalty_MS>500.9</Mover_Damage_Penalty_MS>"
        "<Mover_Damage_Cooldown_MS>250.9</Mover_Damage_Cooldown_MS></Movers>"
        "<World><World_Damage_Penalty_MS>500.9</World_Damage_Penalty_MS>"
        "<World_Damage_Cooldown_MS>250.9</World_Damage_Cooldown_MS></World>"
        "</Penalties></Tiger_Constants></Escort_Constants></Table></root>");
    auto cms = ParseEscortConstants(ms);
    CHECK(cms.has_value());
    CHECK(cms->vehicles.vehicleDamageCooldownMs.present && cms->vehicles.vehicleDamageCooldownMs.value == 250u);
    CHECK(cms->humans.humanDamagePenaltyMs.present && cms->humans.humanDamagePenaltyMs.value == 2000u);
    CHECK(cms->movers.moverDamagePenaltyMs.present && cms->movers.moverDamagePenaltyMs.value == 500u);
    CHECK(cms->movers.moverDamageCooldownMs.present && cms->movers.moverDamageCooldownMs.value == 250u);
    CHECK(cms->world.worldDamagePenaltyMs.present && cms->world.worldDamagePenaltyMs.value == 500u);
    CHECK(cms->world.worldDamageCooldownMs.present && cms->world.worldDamageCooldownMs.value == 250u);

    sr3xtbl::Document absent = P("<root><Table></Table></root>");
    CHECK(!ParseEscortConstants(absent).has_value());
}

// ---------------------------------------------------------------------------
// §20 human_transition.xtbl
// ---------------------------------------------------------------------------
void testHumanTransition() {
    sr3xtbl::Document d = P(
        "<root><Table>"
        "<Human_Transition><Name>stand</Name>"
        "<Transitions><Transition><End_State>walk</End_State><Transition_Animation>anim_walk</Transition_Animation>"
        "<Speed_Multiplier>1.0</Speed_Multiplier><Allow_Player>True</Allow_Player></Transition></Transitions>"
        "<Blends><Blend><End_State>run</End_State><Blend_Time>0.2</Blend_Time></Blend></Blends>"
        "</Human_Transition>"
        "</Table></root>");
    HumanTransitionTable t = ParseHumanTransitionTable(d);
    CHECK(t.rows.size() == 1);
    CHECK(t.rows[0].transitions.size() == 1);
    CHECK(t.rows[0].transitions[0].transitionAnimation.has_value() &&
          *t.rows[0].transitions[0].transitionAnimation == "anim_walk");
    CHECK(t.rows[0].transitions[0].allowPlayer.present && t.rows[0].transitions[0].allowPlayer.value == true);
    CHECK(!t.rows[0].transitions[0].allowPolice.present); // absent Allow_Police -> unspecified
    CHECK(t.rows[0].blends.size() == 1 && near(t.rows[0].blends[0].blendTime.value, 0.2f));
}

// ---------------------------------------------------------------------------
// §21 PEDF_Life.xtbl / Life_default.xtbl - shared schema, no identifying leaf
// on Skeleton_Set/Group (spec: left unmodelled)
// ---------------------------------------------------------------------------
void testLifeTable() {
    sr3xtbl::Document d = P(
        "<root><Table><Skeleton_Set><Groups><Group>"
        "<States><State><ID>idle_state</ID><Animation><Filename>idle.anim</Filename></Animation></State></States>"
        "<Actions><Action><ID>wave</ID><Animation><Filename>wave.anim</Filename></Animation></Action></Actions>"
        "</Group></Groups></Skeleton_Set></Table></root>");
    LifeTable t = ParseLifeTable(d);
    CHECK(t.skeletonSets.size() == 1);
    CHECK(t.skeletonSets[0].groups.size() == 1);
    CHECK(t.skeletonSets[0].groups[0].states.size() == 1);
    CHECK(t.skeletonSets[0].groups[0].states[0].id.has_value() && *t.skeletonSets[0].groups[0].states[0].id == "idle_state");
    CHECK(t.skeletonSets[0].groups[0].states[0].filename.has_value() &&
          *t.skeletonSets[0].groups[0].states[0].filename == "idle.anim");
    CHECK(t.skeletonSets[0].groups[0].actions.size() == 1);
    CHECK(t.skeletonSets[0].groups[0].actions[0].filename.has_value() &&
          *t.skeletonSets[0].groups[0].actions[0].filename == "wave.anim");
}

// ---------------------------------------------------------------------------
// §22 node_graph_files.xtbl - NO <Table> wrapper
// ---------------------------------------------------------------------------
void testNodeGraphFiles() {
    // Real shipped shape (confirmed against misc_tables.vpp_pc by the population
    // harness): root's child is a WRAPPER also named node_graph_files, one level
    // below Document::root() - the rows are its children, not root()'s direct
    // children (unlike action_nodes.xtbl's `object` rows, tested above).
    sr3xtbl::Document d = P(
        "<root><node_graph_files>"
        "<node_graph_file><name>human_locomotion</name></node_graph_file>"
        "<node_graph_file><name>vehicle_states</name></node_graph_file>"
        "</node_graph_files></root>");
    NodeGraphFilesFile f = ParseNodeGraphFilesFile(d);
    CHECK(f.files.size() == 2);
    CHECK(f.files[0].name.has_value() && *f.files[0].name == "human_locomotion");
    CHECK(f.files[1].name.has_value() && *f.files[1].name == "vehicle_states");

    // Rows placed directly under root with no wrapper (the WRONG shape) must NOT
    // be picked up - this is the negative case that proves the wrapper lookup
    // is load-bearing, not a no-op.
    sr3xtbl::Document noWrapper = P("<root><node_graph_file><name>x</name></node_graph_file></root>");
    CHECK(ParseNodeGraphFilesFile(noWrapper).files.empty());
}

// ---------------------------------------------------------------------------
// A few remaining tables: simple round-trip smoke tests
// ---------------------------------------------------------------------------
void testRemainingSmoke() {
    // §12 generic_characters.xtbl
    {
        sr3xtbl::Document d =
            P("<root><Table><Generics_Table><Name>Young Male</Name><Spawn_Name>ym01</Spawn_Name></Generics_Table>"
              "</Table></root>");
        GenericCharactersTable t = ParseGenericCharactersTable(d);
        CHECK(t.rows.size() == 1 && t.rows[0].spawnName.has_value() && *t.rows[0].spawnName == "ym01");
    }
    // §13.2 action_node_notoriety.xtbl
    {
        sr3xtbl::Document d = P(
            "<root><Table><Action_Node_Notoriety><Name>N1</Name>"
            "<Spawn_Flags><Air_Only>True</Air_Only></Spawn_Flags>"
            "<Min_Notoriety>1</Min_Notoriety><Max_Notoriety>5</Max_Notoriety><Weapon>pistol</Weapon>"
            "</Action_Node_Notoriety></Table></root>");
        ActionNodeNotorietyTable t = ParseActionNodeNotorietyTable(d);
        CHECK(t.rows.size() == 1 && t.rows[0].spawnFlagsAirOnly.value == true);
        CHECK(t.rows[0].minNotoriety.value == 1 && t.rows[0].maxNotoriety.value == 5);
    }
    // §14.1 distant_peds.xtbl
    {
        sr3xtbl::Document d =
            P("<root><Table><Distant_Pedestrian><Name>dp1</Name><Mesh>dp1.csmesh_pc</Mesh></Distant_Pedestrian>"
              "</Table></root>");
        DistantPedsTable t = ParseDistantPedsTable(d);
        CHECK(t.rows.size() == 1 && t.rows[0].mesh.has_value() && *t.rows[0].mesh == "dp1.csmesh_pc");
    }
    // §14.4 distant_vehicle_traffic_types.xtbl (the only place a traffic type exists, §3)
    {
        sr3xtbl::Document d = P(
            "<root><Table><Distant_Vehicle_Traffic_Type><Name>tt1</Name><Spline_Type>2</Spline_Type>"
            "<Length_Per_Spawn>50.0</Length_Per_Spawn><No_Spawn_Zone>10.0</No_Spawn_Zone><Dummy_Car_Length>4.5</Dummy_Car_Length>"
            "</Distant_Vehicle_Traffic_Type></Table></root>");
        DistantVehicleTrafficTypesTable t = ParseDistantVehicleTrafficTypesTable(d);
        CHECK(t.rows.size() == 1 && t.rows[0].splineType.value == 2);
    }
    // §14.5 / §14.6
    {
        sr3xtbl::Document d = P(
            "<root><Table>"
            "<Distant_Vehicle_Color><Color><R>10</R><G>20</G><B>30</B></Color></Distant_Vehicle_Color>"
            "<Distant_Vehicle><Name>dv1</Name><Mesh>m1</Mesh><Traffic_Type>tt1</Traffic_Type></Distant_Vehicle>"
            "</Table></root>");
        DistantVehicleColorsTable c = ParseDistantVehicleColorsTable(d);
        DistantVehiclesTable v = ParseDistantVehiclesTable(d);
        CHECK(c.rows.size() == 1 && near(c.rows[0].color.g.value, 20.0f));
        CHECK(v.rows.size() == 1 && v.rows[0].trafficType.has_value() && *v.rows[0].trafficType == "tt1");
    }
}

} // namespace

int main() {
    testTrafficLanes();
    testAmbientTrafficEvents();
    testRoadblockLayouts();
    testRoadblockNotoriety();
    testRoadblockStagLockdown();
    testPanicReactions();
    testAiGoals();
    testAiBehavior();
    testAiBehaviorGoalsCanProcessEarlyParent();
    testAiPersonalities();
    testGenericVehicles();
    testGenericCharacters();
    testActionNodeGroups();
    testActionNodeNpcs();
    testActionNodes();
    testDistantPedColors();
    testDistantSpawnParameters();
    testHomies();
    testFollowerHeads();
    testDriverBailout();
    testVehicleDespawn();
    testEscortConstants();
    testHumanTransition();
    testLifeTable();
    testNodeGraphFiles();
    testRemainingSmoke();

    if (g_failures == 0) {
        std::cout << "All synthetic traffic-ai table tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
