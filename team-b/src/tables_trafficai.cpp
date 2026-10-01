// Parse functions for sr3tables_trafficai (include/sr3tables_trafficai/tables.h).
// Built ONLY on sr3xtbl's shared accessors (include/sr3xtbl/xtbl.h) - no parser
// logic, no container access, nothing derived from the game exe. See the header
// banner for what is and is not modelled (business-logic conversions, whole-row
// accept/reject rules, and external-registry resolution are all deliberately
// left out - see tables.h's own comment for why).

#include "sr3tables_trafficai/tables.h"

#include <array>
#include <string_view>

namespace sr3tables_trafficai {

using namespace sr3xtbl;

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// A node's own text, or a named child's text: nullopt for "absent or empty"
// (the one ambiguity sr3xtbl's ChildText cannot resolve either - see Text's
// declaration in tables.h). `name` empty means "the node's own text"
// (ChildText's own convention, xtbl.h).
Text getText(const Node* n, std::string_view name = {}) {
    const std::string* t = ChildText(n, name);
    return t ? Text(*t) : std::nullopt;
}

// Iterates every child of `container` named `rowName` (FindChild/NextSibling,
// the row-iteration primitive of spec-tables-traffic-ai.md 1.3) and parses each
// with `parseRow`. `container` may be null (reads as "no children" - the
// null-node guard every sr3xtbl accessor honours).
template <class T, class F>
std::vector<T> parseRows(const Node* container, std::string_view rowName, F parseRow) {
    std::vector<T> out;
    for (const Node* n = FindChild(container, rowName); n; n = NextSibling(container, n, rowName)) {
        out.push_back(parseRow(n));
    }
    return out;
}

// A child's raw text plus its sr3xtbl::EnumIndex() result against a fixed,
// compiled-in, case-insensitive name list (spec 1.3, 0x00DAC830).
template <size_t N>
EnumText enumTextOf(const Node* n, std::string_view name, const std::array<std::string_view, N>& names) {
    EnumText e;
    e.text = getText(n, name);
    const Node* child = FindChild(n, name);
    e.index = EnumIndex(child, names.data(), names.size());
    return e;
}

// action_node_npcs.xtbl's own text-boolean convention (spec 1.3): the child's
// text compared to "True"/"False" case-insensitively; anything else (including
// absence) is Unset - see TextBool's declaration in tables.h for why this is
// NOT modelled with sr3xtbl::ReadBoolAlways (a different, incompatible
// convention: only "true"/"yes" are true, everything else present is false).
TextBool readTextBool(const Node* n, std::string_view name) {
    const std::string* t = ChildText(n, name);
    if (!t) return TextBool::Unset;
    if (NameEquals(*t, "True")) return TextBool::True;
    if (NameEquals(*t, "False")) return TextBool::False;
    return TextBool::Unset;
}

// generic_vehicles.Name is matched CASE-SENSITIVELY (spec §12, "unlike the
// character file") - sr3xtbl::EnumIndex is case-insensitive, so this is a plain
// exact compare, not that accessor.
template <size_t N>
int caseSensitiveIndex(const Text& t, const std::array<std::string_view, N>& names) {
    if (!t) return -1;
    for (size_t i = 0; i < N; ++i) {
        if (*t == names[i]) return static_cast<int>(i);
    }
    return -1;
}

Rgb parseRgb(const Node* n) {
    Rgb c;
    c.r = ReadFloatAlways(n, "R");
    c.g = ReadFloatAlways(n, "G");
    c.b = ReadFloatAlways(n, "B");
    return c;
}

// action_node_npcs.xtbl's Spawn_Priority/Sex/Team enum name lists - not given in
// spec 1.5's compiled-in table list (they are local to §13.3), so kept here
// rather than in tables.h's `enums` namespace.
constexpr std::array<std::string_view, 2> kSpawnPriorityNames = {"Sidewalk (normal)", "Action Node (high)"};
constexpr std::array<std::string_view, 2> kSexNames = {"male", "female"};
constexpr std::array<std::string_view, 4> kTeamNames = {"Civilian", "Police", "Gang", "Saints"};

} // namespace

// ---------------------------------------------------------------------------
// §2 traffic_lanes.xtbl
// ---------------------------------------------------------------------------
TrafficLanesTable ParseTrafficLanesTable(const Document& doc) {
    TrafficLanesTable out;
    const Node* newEntity = FindChild(doc.table(), "NewEntity");
    const Node* laneGrid = FindChild(newEntity, "LaneGrid");
    out.laneSpeeds = parseRows<TrafficLaneSpeed>(laneGrid, "LaneSpeed", [](const Node* row) {
        TrafficLaneSpeed s;
        s.speed = ReadFloatAlways(row, "Speed");
        return s;
    });
    return out;
}

// ---------------------------------------------------------------------------
// §4 ambient_traffic_events.xtbl
// ---------------------------------------------------------------------------
AmbientTrafficEvent ParseAmbientTrafficEvent(const Node* row) {
    AmbientTrafficEvent e;
    e.name = getText(row, "Name");
    e.todStart = ReadInt32Always(row, "TOD_start");
    e.todEnd = ReadInt32Always(row, "TOD_end");
    const Node* flags = FindChild(row, "Flags");
    e.flagsPresent = flags != nullptr;
    e.disabledDuringNotoriety = HasFlag(flags, "disabled_during_notoriety");
    e.minLife = ReadFloatAlways(row, "Min_life");
    e.maxLife = ReadFloatAlways(row, "Max_life");
    e.cooldownTime = ReadFloatAlways(row, "Cooldown_time");
    e.roadblockLayout = getText(row, "Roadblock_layout");
    return e;
}
AmbientTrafficEventsTable ParseAmbientTrafficEventsTable(const Document& doc) {
    AmbientTrafficEventsTable out;
    out.rows = parseRows<AmbientTrafficEvent>(doc.table(), "Ambient_traffic_event", ParseAmbientTrafficEvent);
    return out;
}

// ---------------------------------------------------------------------------
// §5 roadblock_layouts.xtbl
// ---------------------------------------------------------------------------
RoadblockChild ParseRoadblockChild(const Node* row) {
    RoadblockChild c;
    c.type = enumTextOf(row, "Type", enums::kRoadblockObjectTypeNames);
    c.resourceName = getText(row, "Resource_name");
    c.xOffset = ReadFloatAlways(row, "X_offset");
    c.zOffset = ReadFloatAlways(row, "Z_offset");
    c.heading = ReadFloatAlways(row, "Heading");
    c.hitpoints = GetInt32(row, "Hitpoints");
    c.seatIndex = ReadUInt8Always(row, "Seat_Index");
    return c;
}
RoadblockObject ParseRoadblockObject(const Node* row) {
    RoadblockObject o;
    o.type = enumTextOf(row, "Type", enums::kRoadblockObjectTypeNames);
    o.resourceName = getText(row, "Resource_name");
    o.variantName = getText(row, "Variant_Name");
    o.laneIndex = GetUInt8(row, "Lane_index");
    o.xOffset = ReadFloatAlways(row, "X_offset");
    o.zOffset = ReadFloatAlways(row, "Z_offset");
    o.heading = ReadFloatAlways(row, "Heading");
    o.hitpoints = GetInt32(row, "Hitpoints");
    const Node* children = FindChild(row, "Children");
    o.children = parseRows<RoadblockChild>(children, "Child", ParseRoadblockChild);
    return o;
}
RoadblockLayout ParseRoadblockLayout(const Node* row) {
    RoadblockLayout l;
    l.name = getText(row, "Name");
    l.numLanes = ReadInt32Always(row, "Num_lanes");
    const Node* flags = FindChild(row, "Flags");
    l.flagEndCap = HasFlag(flags, "end cap");
    l.flagIntersection = HasFlag(flags, "intersection");
    l.flagNoFlee = HasFlag(flags, "no flee");
    l.flagCanSpawnOnDisabledLanes = HasFlag(flags, "Can Spawn on Disabled Lanes");
    const Node* objects = FindChild(row, "Objects");
    l.objects = parseRows<RoadblockObject>(objects, "Object", ParseRoadblockObject);
    return l;
}
RoadblockLayoutsTable ParseRoadblockLayoutsTable(const Document& doc) {
    RoadblockLayoutsTable out;
    out.rows = parseRows<RoadblockLayout>(doc.table(), "Roadblock_layouts", ParseRoadblockLayout);
    return out;
}

// ---------------------------------------------------------------------------
// §6 roadblock_notoriety.xtbl
// ---------------------------------------------------------------------------
RoadblockNotorietyLayoutEntry ParseRoadblockNotorietyLayoutEntry(const Node* row) {
    RoadblockNotorietyLayoutEntry e;
    e.roadblock = getText(row, "Roadblock");
    e.cooldown = ReadInt32Always(row, "Cooldown");
    return e;
}
RoadblockNotorietyLevel ParseRoadblockNotorietyLevel(const Node* row) {
    RoadblockNotorietyLevel lvl;
    lvl.notorietyLevel = ReadInt32Always(row, "Notoriety_level");
    lvl.maxActiveRoadblocks = ReadInt32Always(row, "Max_active_roadblocks");
    lvl.minTime = ReadInt32Always(row, "Min_time");
    lvl.maxTime = ReadInt32Always(row, "Max_time");
    const Node* container = FindChild(row, "Roadblock_layouts");
    lvl.roadblockLayoutsPresent = container != nullptr;
    lvl.layouts = parseRows<RoadblockNotorietyLayoutEntry>(container, "Roadblock_layout",
                                                             ParseRoadblockNotorietyLayoutEntry);
    return lvl;
}
RoadblockNotoriety ParseRoadblockNotoriety(const Node* row) {
    RoadblockNotoriety r;
    r.name = getText(row, "Name");
    r.team = getText(row, "Team");
    const Node* levels = FindChild(row, "Levels");
    r.levels = parseRows<RoadblockNotorietyLevel>(levels, "Level", ParseRoadblockNotorietyLevel);
    return r;
}
RoadblockNotorietyTable ParseRoadblockNotorietyTable(const Document& doc) {
    RoadblockNotorietyTable out;
    out.rows = parseRows<RoadblockNotoriety>(doc.table(), "Roadblock_notoriety", ParseRoadblockNotoriety);
    return out;
}

// ---------------------------------------------------------------------------
// §7 roadblock_stag_lockdown.xtbl
// ---------------------------------------------------------------------------
RoadblockStagLockdownEntry ParseRoadblockStagLockdownEntry(const Node* row) {
    RoadblockStagLockdownEntry e;
    e.roadblockLayout = getText(row, "Roadblock_Layout");
    e.spawnPoint = getText(row, "Spawn_Point");
    return e;
}
RoadblockStagLockdownTable ParseRoadblockStagLockdownTable(const Document& doc) {
    RoadblockStagLockdownTable out;
    const Node* outer = FindChild(doc.table(), "Roadblock_stag_lockdown");
    const Node* list = FindChild(outer, "Roadblock_List");
    out.rowPresent = outer != nullptr && list != nullptr;
    out.entries = parseRows<RoadblockStagLockdownEntry>(list, "Roadbock", ParseRoadblockStagLockdownEntry); // sic
    return out;
}

// ---------------------------------------------------------------------------
// §8 panic_reactions.xtbl
// ---------------------------------------------------------------------------
PanicAnimBlock ParsePanicAnimBlock(const Node* container) {
    PanicAnimBlock b;
    b.animState = getText(container, "Anim_State");
    b.animAction = getText(container, "Anim_Action");
    b.exitAnimAction = getText(container, "Exit_Anim_Action");
    b.enterAnimAction = getText(container, "Enter_Anim_Action");
    b.numAnimVariants = ReadInt32Always(container, "NumAnimVariants");
    return b;
}
PanicReaction ParsePanicReaction(const Node* row) {
    PanicReaction r;
    r.name = getText(row, "Name");
    r.priority = ReadUInt8Always(row, "Priority");
    const Node* timed = FindChild(row, "Timed");
    r.timedPresent = timed != nullptr;
    r.minTimeNpc = ReadFloatAlways(timed, "Min_Time_NPC");
    r.maxTimeNpc = ReadFloatAlways(timed, "Max_Time_NPC");
    r.minTimePlayer = ReadFloatAlways(timed, "Min_Time_Player");
    r.maxTimePlayer = ReadFloatAlways(timed, "Max_Time_Player");
    const Node* movement = FindChild(row, "Movement");
    r.movementPresent = movement != nullptr;
    r.movementRun = ReadBoolAlways(movement, "Run");
    r.minDist = ReadFloatAlways(movement, "Min_Dist");
    r.maxDist = ReadFloatAlways(movement, "Max_Dist");
    const Node* drug = FindChild(row, "Drug_Effects");
    r.drugEffectsPresent = drug != nullptr;
    r.weedFactorMax = ReadFloatAlways(drug, "Weed_Factor_Max");
    r.boozeFactorMax = ReadFloatAlways(drug, "Booze_Factor_Max");
    r.weedFactorMin = ReadFloatAlways(drug, "Weed_Factor_Min");
    r.boozeFactorMin = ReadFloatAlways(drug, "Booze_Factor_Min");
    r.personaSituation = getText(row, "Persona_Situation");
    const Node* playerAnims = FindChild(row, "Player_Animations");
    r.playerAnimationsPresent = playerAnims != nullptr;
    r.playerAnimations = ParsePanicAnimBlock(playerAnims);
    const Node* npcAnims = FindChild(row, "NPC_Animations");
    r.npcAnimationsPresent = npcAnims != nullptr;
    r.npcAnimations = ParsePanicAnimBlock(npcAnims);
    return r;
}
PanicReactionsTable ParsePanicReactionsTable(const Document& doc) {
    PanicReactionsTable out;
    out.rows = parseRows<PanicReaction>(doc.table(), "Panic", ParsePanicReaction);
    return out;
}

// ---------------------------------------------------------------------------
// §9 ai_goals.xtbl
// ---------------------------------------------------------------------------
AiGoalAction ParseAiGoalAction(const Node* row) {
    AiGoalAction a;
    a.action = enumTextOf(row, "action", enums::kCombatActionNames);
    return a;
}
AiGoal ParseAiGoal(const Node* row) {
    AiGoal g;
    g.name = enumTextOf(row, "Name", enums::kAiGoalNames);
    const Node* order = FindChild(row, "order_of_actions");
    g.actions = parseRows<AiGoalAction>(order, "action_elm", ParseAiGoalAction);
    return g;
}
AiGoalsTable ParseAiGoalsTable(const Document& doc) {
    AiGoalsTable out;
    out.rows = parseRows<AiGoal>(doc.table(), "ai_goal", ParseAiGoal);
    return out;
}

// ---------------------------------------------------------------------------
// §10 ai_behavior.xtbl
// ---------------------------------------------------------------------------
AiHumanDescEntry ParseAiHumanDescEntry(const Node* row) {
    AiHumanDescEntry e;
    e.weaponClass = enumTextOf(row, "Weapon_Class", enums::kWeaponClassNames);
    e.team = getText(row, "Team");
    e.rank = getText(row, "Rank");
    return e;
}
AiAbilityEntry ParseAiAbilityEntry(const Node* row) {
    AiAbilityEntry e;
    e.action = enumTextOf(row, "Action", enums::kCombatActionNames);
    e.repeatMin = ReadFloatAlways(row, "Repeat_min");
    e.repeatMax = ReadFloatAlways(row, "Repeat_max");
    return e;
}
AiBehavior ParseAiBehavior(const Node* row) {
    AiBehavior b;
    b.name = getText(row, "Name");
    b.scriptedOnly = ReadBoolAlways(row, "Scripted_Only");
    b.combatAggression = getText(row, "combat_aggression");
    const Node* humanDesc = FindChild(row, "Human_Description");
    b.humanDescription = parseRows<AiHumanDescEntry>(humanDesc, "human_elm", ParseAiHumanDescEntry);
    b.riotshieldOnly = ReadBoolAlways(row, "Riotshield_Only");
    const Node* actions = FindChild(row, "Actions");
    b.actions = parseRows<AiAbilityEntry>(actions, "ability_elm", ParseAiAbilityEntry);

    const Node* options = FindChild(row, "Options");
    b.options.present = options != nullptr;
    b.options.canFireWhileWalking = ReadBoolAlways(options, "can_fire_while_walking");
    b.options.canBackStepFire = ReadBoolAlways(options, "can_back_step_fire");
    b.options.dodgeChance = ReadUInt8Always(options, "dodge_chance");

    const Node* goals = FindChild(row, "Goals");

    const Node* survive = FindChild(goals, "Survive");
    b.survivePresent = survive != nullptr;
    b.survivalLowHealthPct = ReadFloatAlways(survive, "low_health_pct");

    const Node* suppress = FindChild(goals, "Suppress");
    b.suppressPresent = suppress != nullptr;
    b.suppressLength = ReadFloatAlways(suppress, "length");

    const Node* cover = FindChild(goals, "Cover");
    b.cover.present = cover != nullptr;
    const Node* takeCoverWhen = FindChild(cover, "take_cover_when");
    b.cover.takeCoverAimedAt = HasFlag(takeCoverWhen, "aimed at");
    b.cover.takeCoverShotAt = HasFlag(takeCoverWhen, "shot at");
    b.cover.takeCoverDamaged = HasFlag(takeCoverWhen, "damaged");
    b.cover.abandonCoverTime = ReadFloatAlways(cover, "abandon_cover_time");
    b.cover.findNewCoverTime = ReadFloatAlways(cover, "find_new_cover_time");
    b.cover.advanceCoverTime = ReadFloatAlways(cover, "advance_cover_time");
    b.cover.stayDownTime = ReadFloatAlways(cover, "stay_down_time");
    b.cover.popUpTime = ReadFloatAlways(cover, "pop_up_time");
    b.cover.squadCoverPct = ReadUInt8Always(cover, "squad_cover_pct");

    b.takingDamagePresent = FindChild(goals, "Taking_damage") != nullptr;

    const Node* group = FindChild(goals, "Group");
    b.group.present = group != nullptr;
    b.group.seperatedDistance = ReadFloatAlways(group, "seperated_distance");
    b.group.seperatedDistanceInterior = GetFloat(group, "seperated_distance_interior");

    b.advancePresent = FindChild(goals, "Advance") != nullptr;
    b.avoidBrutePresent = FindChild(goals, "Avoid_brute") != nullptr;
    b.killNearPlayerPresent = FindChild(goals, "Kill_near_player") != nullptr;
    b.retreatPresent = FindChild(goals, "Retreat") != nullptr;

    const Node* killEnemy = FindChild(goals, "Kill_Enemy");
    b.killEnemy.present = killEnemy != nullptr;
    b.killEnemy.seenInLast = ReadFloatAlways(killEnemy, "seen_in_last");

    const Node* reload = FindChild(goals, "Reload");
    b.reloadPresent = reload != nullptr;
    b.reloadClipPct = ReadFloatAlways(reload, "clip_pct");

    const Node* flushOut = FindChild(goals, "Flush_Out");
    b.flushOut.present = flushOut != nullptr;
    b.flushOut.seenInLast = ReadFloatAlways(flushOut, "seen_in_last");

    const Node* acquireTarget = FindChild(goals, "Acquire_Target");
    b.acquireTarget.present = acquireTarget != nullptr;
    b.acquireTarget.seenInLast = ReadFloatAlways(acquireTarget, "seen_in_last");

    const Node* investigate = FindChild(goals, "Investigate");
    b.investigate.present = investigate != nullptr;
    b.investigate.seenInLast = ReadFloatAlways(investigate, "seen_in_last");

    b.goalsCanProcessEarly = ReadBoolAlways(row, "goals_can_process_early");
    return b;
}
AiBehaviorTable ParseAiBehaviorTable(const Document& doc) {
    AiBehaviorTable out;
    out.rows = parseRows<AiBehavior>(doc.table(), "Behavior", ParseAiBehavior);
    return out;
}

// ---------------------------------------------------------------------------
// §11 ai_personalities.xtbl
// ---------------------------------------------------------------------------
AiPersonality ParseAiPersonality(const Node* row) {
    AiPersonality p;
    p.name = getText(row, "Name");
    p.combatAggression = getText(row, "combat_aggression");

    const Node* fleeTweaks = FindChild(row, "Flee_tweaks");
    p.fleeTweaksPresent = fleeTweaks != nullptr;
    p.fleeHealthPct = ReadUInt8Always(fleeTweaks, "Flee_health_pct");
    const Node* fleeEvents = FindChild(fleeTweaks, "flee_events");
    p.fleeAimedAt = HasFlag(fleeEvents, "aimed at");
    p.fleeMeleeHit = HasFlag(fleeEvents, "melee hit");
    p.fleeBulletHeard = HasFlag(fleeEvents, "bullet heard");
    p.fleeBulletHit = HasFlag(fleeEvents, "bullet hit");
    p.fleeExplosionHeard = HasFlag(fleeEvents, "explosion heard");

    const Node* ambient = FindChild(row, "Ambient_tweaks");
    p.ambientTweaksPresent = ambient != nullptr;
    p.attackUnfriendly = ReadUInt8Always(ambient, "attack_unfriendly");

    const Node* taunt = FindChild(ambient, "Taunt_reaction");
    p.tauntReaction.flee = ReadUInt8Always(taunt, "flee");
    p.tauntReaction.tauntBack = ReadUInt8Always(taunt, "taunt_back");
    p.tauntReaction.attack = ReadUInt8Always(taunt, "attack");

    const Node* melee = FindChild(ambient, "MeleeReaction");
    p.meleeReaction.flee = ReadUInt8Always(melee, "Flee");
    p.meleeReaction.worry = ReadUInt8Always(melee, "Worry");
    p.meleeReaction.doNothing = ReadUInt8Always(melee, "DoNothing");
    p.meleeReaction.watch = ReadUInt8Always(melee, "Watch");
    p.meleeReaction.cheer = ReadUInt8Always(melee, "Cheer");
    p.meleeReaction.attack = ReadUInt8Always(melee, "Attack");

    const Node* wait = FindChild(ambient, "VehicleWaitResponse");
    p.vehicleWaitResponse.doNothing = ReadUInt8Always(wait, "DoNothing");
    p.vehicleWaitResponse.honk = ReadUInt8Always(wait, "Honk");
    p.vehicleWaitResponse.threaten = ReadUInt8Always(wait, "Threaten");

    const Node* coll = FindChild(ambient, "VehicleCollisionResponse");
    p.vehicleCollisionResponse.doNothing = ReadUInt8Always(coll, "DoNothing");
    p.vehicleCollisionResponse.honk = ReadUInt8Always(coll, "Honk");
    p.vehicleCollisionResponse.threaten = ReadUInt8Always(coll, "Threaten");
    p.vehicleCollisionResponse.flee = ReadUInt8Always(coll, "Flee");
    p.vehicleCollisionResponse.getOut = ReadUInt8Always(coll, "GetOut");
    p.vehicleCollisionResponse.attack = ReadUInt8Always(coll, "Attack");
    p.vehicleCollisionResponse.attackPlayer = ReadUInt8Always(coll, "AttackPlayer");

    const Node* hijack = FindChild(ambient, "HijackBehavior");
    p.hijackBehavior.jackFailureChance = ReadUInt8Always(hijack, "JackFailureChance");
    p.hijackBehavior.postResistFleeChance = ReadUInt8Always(hijack, "PostResistFleeChance");
    p.hijackBehavior.preJackFleeChance = ReadUInt8Always(hijack, "PreJackFleeChance");
    return p;
}
AiPersonalitiesTable ParseAiPersonalitiesTable(const Document& doc) {
    AiPersonalitiesTable out;
    out.rows = parseRows<AiPersonality>(doc.table(), "Personality", ParseAiPersonality);
    return out;
}

// ---------------------------------------------------------------------------
// §12 generic_characters.xtbl / generic_vehicles.xtbl
// ---------------------------------------------------------------------------
GenericCharacterRow ParseGenericCharacterRow(const Node* row) {
    GenericCharacterRow r;
    r.name = getText(row, "Name");
    r.spawnName = getText(row, "Spawn_Name");
    return r;
}
GenericCharactersTable ParseGenericCharactersTable(const Document& doc) {
    GenericCharactersTable out;
    out.rows = parseRows<GenericCharacterRow>(doc.table(), "Generics_Table", ParseGenericCharacterRow);
    return out;
}
GenericVehicleRow ParseGenericVehicleRow(const Node* row) {
    GenericVehicleRow r;
    r.name = getText(row, "Name");
    r.index = caseSensitiveIndex(r.name, enums::kGenericVehicleSlotNames);
    r.spawnName = getText(row, "Spawn_Name");
    r.variantName = getText(row, "Variant_Name");
    return r;
}
GenericVehiclesTable ParseGenericVehiclesTable(const Document& doc) {
    GenericVehiclesTable out;
    out.rows = parseRows<GenericVehicleRow>(doc.table(), "Generics_Table", ParseGenericVehicleRow);
    return out;
}

// ---------------------------------------------------------------------------
// §13.1 action_node_groups.xtbl
// ---------------------------------------------------------------------------
ActionNodeGroup ParseActionNodeGroup(const Node* row) {
    ActionNodeGroup g;
    g.name = getText(row, "Name");
    g.minSpacing = ReadFloatAlways(row, "Min_spacing");
    g.convertToPedMinDelay = GetUInt32(row, "Convert_to_ped_min_delay");
    g.convertToPedMaxDelay = ReadUInt32Always(row, "Convert_to_ped_max_delay");

    const Node* spawning = FindChild(row, "Spawning");
    g.spawningNight = HasFlag(spawning, "night");
    g.spawningDay = HasFlag(spawning, "day");
    g.spawningNoon = HasFlag(spawning, "noon");
    g.spawningEvening = HasFlag(spawning, "evening");

    g.capturePeds = ReadBoolAlways(row, "Capture_peds");
    g.outdoorNode = ReadBoolAlways(row, "Outdoor_node");
    g.useInRain = ReadBoolAlways(row, "Use_In_Rain");
    g.storeBrowsing = ReadBoolAlways(row, "Store_browsing");
    g.storeOwnershipSpawn = ReadBoolAlways(row, "Store_ownership_spawn");
    g.exclusiveActions = ReadBoolAlways(row, "Exclusive_Actions");
    g.sequenceNodes = ReadBoolAlways(row, "sequence_nodes");

    g.onWater = ReadBoolAlways(row, "On_Water");
    g.playerAllowed = ReadBoolAlways(row, "Player_Allowed");
    g.spawnDuringHighNotoriety = ReadBoolAlways(row, "Spawn_During_High_Notoriety");
    g.groupDisabled = ReadBoolAlways(row, "GroupDisabled");
    g.stagNode = ReadBoolAlways(row, "Stag_Node");
    g.antiSaintsNode = ReadBoolAlways(row, "Anti_Saints_Node");

    g.referenceObject = getText(row, "Reference_object");
    g.referenceObjectPresent = FindChild(row, "Reference_object") != nullptr;
    g.spawnTimer = ReadInt32Always(row, "Spawn_Timer");
    // LABEL: spec-tables-traffic-ai.md s13.1 (+0x2C/+0x2E) "signed -> stored as a byte; default -1 ...
    // [OPEN - desk review 2026-09-30: which accessor reads these is not stated (the unsigned 0x00DAC1D0 would
    // read `-1` as 0); to be settled against the executable (0x008C9760)]". Signed-byte read kept; not changed.
    g.instanceCap = GetInt8(row, "Instance_Cap");
    g.maxSpawns = GetInt8(row, "MaxSpawns");

    const Node* npcListNode = FindChild(row, "npc_list");
    g.npcList = parseRows<Text>(npcListNode, "npc", [](const Node* n) { return getText(n); });

    g.notorietyInfo = getText(row, "Notoriety_Info");
    g.musicEmitter = getText(row, "Music_Emitter");
    g.minPlayerRank = GetUInt32(row, "Min_Player_Rank");
    return g;
}
ActionNodeGroupsTable ParseActionNodeGroupsTable(const Document& doc) {
    ActionNodeGroupsTable out;
    out.rows = parseRows<ActionNodeGroup>(doc.table(), "action_node_group", ParseActionNodeGroup);
    return out;
}

// ---------------------------------------------------------------------------
// §13.2 action_node_notoriety.xtbl
// ---------------------------------------------------------------------------
ActionNodeNotoriety ParseActionNodeNotoriety(const Node* row) {
    ActionNodeNotoriety n;
    n.name = getText(row, "Name");
    const Node* spawnFlags = FindChild(row, "Spawn_Flags");
    n.spawnFlagsAirOnly = ReadBoolAlways(spawnFlags, "Air_Only");
    n.spawnFlagsGang = ReadBoolAlways(spawnFlags, "Gang");
    n.minNotoriety = ReadInt32Always(row, "Min_Notoriety");
    n.maxNotoriety = ReadInt32Always(row, "Max_Notoriety");
    n.weapon = getText(row, "Weapon");
    return n;
}
ActionNodeNotorietyTable ParseActionNodeNotorietyTable(const Document& doc) {
    ActionNodeNotorietyTable out;
    out.rows = parseRows<ActionNodeNotoriety>(doc.table(), "Action_Node_Notoriety", ParseActionNodeNotoriety);
    return out;
}

// ---------------------------------------------------------------------------
// §13.3 action_node_npcs.xtbl
// ---------------------------------------------------------------------------
ActionNodeAnimationState ParseActionNodeAnimationState(const Node* row) {
    ActionNodeAnimationState s;
    s.animationState = getText(row, "Animation_State");
    const Node* actions = FindChild(row, "Actions");
    s.actions = parseRows<ActionNodeAnimationAction>(actions, "Animation_action", [](const Node* n) {
        ActionNodeAnimationAction a;
        a.action = getText(n);
        return a;
    });
    return s;
}
ActionNodeNpc ParseActionNodeNpc(const Node* row) {
    ActionNodeNpc n;
    n.name = getText(row, "Name");
    n.personaSituation = getText(row, "Persona_situation");
    n.enterAnim = getText(row, "Enter_anim");
    n.exitAnim = getText(row, "Exit_anim");
    n.startleAnim = getText(row, "Startle_anim");

    const Node* animations = FindChild(row, "Animations");
    n.animations = parseRows<ActionNodeAnimationState>(animations, "State", ParseActionNodeAnimationState);

    const Node* cower = FindChild(row, "CowerAnims");
    n.cowerAnims = parseRows<Text>(cower, "CowerAnim", [](const Node* c) { return getText(c); });

    n.minAnimDelay = ReadFloatAlways(row, "Min_anim_delay");
    n.maxAnimDelay = ReadFloatAlways(row, "Max_anim_delay");

    n.syncedAnimation = readTextBool(row, "Synced_Animation");
    n.singleUse = readTextBool(row, "Single_Use");
    n.getBag = readTextBool(row, "Get_Bag");
    n.reverseEnter = readTextBool(row, "Reverse_Enter");
    n.canUseWithBag = readTextBool(row, "Can_Use_With_Bag");
    n.fleeOnExit = readTextBool(row, "Flee_On_Exit");
    n.neverExit = readTextBool(row, "Never_Exit");
    n.canNotExit = readTextBool(row, "Can_Not_Exit");

    n.spawnOnly = readTextBool(row, "Spawn_Only");
    n.standardizeHeight = readTextBool(row, "Standardize_Height");
    n.reactToPeds = readTextBool(row, "React_To_Peds");
    n.requiredNpc = readTextBool(row, "Required_NPC");
    n.npcDies = readTextBool(row, "NPC_Dies");
    n.drunkNode = readTextBool(row, "Drunk_Node");
    n.canBeBumped = readTextBool(row, "Can_Be_Bumped");
    n.stationaryNode = readTextBool(row, "Stationary_Node");

    n.spawnPriority = enumTextOf(row, "Spawn_Priority", kSpawnPriorityNames);
    n.exitOnItemDislodge = readTextBool(row, "ExitOnItemDislodge");
    n.spawnInAir = readTextBool(row, "SpawnInAir");
    n.spawnOffNavmesh = readTextBool(row, "SpawnOffNavmesh");
    n.equipWeapon = readTextBool(row, "EquipWeapon");
    n.requiresRifle = readTextBool(row, "RequiresRifle");
    n.supportsHeroic = readTextBool(row, "SupportsHeroic");
    n.supportsNonHeroic = readTextBool(row, "SupportsNonHeroic");

    const Node* followNodes = FindChild(row, "Follow_Nodes");
    n.followNodes = parseRows<Text>(followNodes, "Node", [](const Node* c) { return getText(c); });

    n.randomDestinationPercent = ReadFloatAlways(row, "Random_destination_percent");

    const Node* modelList = FindChild(row, "Model_List");
    n.modelList = parseRows<Text>(modelList, "Model", [](const Node* c) { return getText(c); });

    n.numResourceRequests = ReadUInt32Always(row, "Num_Resource_Requests");
    n.sex = enumTextOf(row, "Sex", kSexNames);
    n.team = enumTextOf(row, "Team", kTeamNames);
    return n;
}
ActionNodeNpcsTable ParseActionNodeNpcsTable(const Document& doc) {
    ActionNodeNpcsTable out;
    out.rows = parseRows<ActionNodeNpc>(doc.table(), "action_node_npc", ParseActionNodeNpc);
    return out;
}

// ---------------------------------------------------------------------------
// §13.4 action_nodes.xtbl (no <Table> wrapper)
// ---------------------------------------------------------------------------
ActionNodeTransform ParseActionNodeTransform(const Node* node, std::string_view childName) {
    ActionNodeTransform t;
    const std::string* text = ChildText(node, childName);
    if (!text) return t;
    t.present = true;
    const std::string& s = *text;
    auto isWs = [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
    size_t i = 0;
    while (i < s.size() && t.tokenCount < t.values.size()) {
        while (i < s.size() && isWs(s[i])) ++i;
        if (i >= s.size()) break;
        size_t start = i;
        while (i < s.size() && !isWs(s[i])) ++i;
        t.values[t.tokenCount++] = ParseFloat(std::string_view(s).substr(start, i - start));
    }
    return t;
}
ActionNodePlacementActionNode ParseActionNodePlacementActionNode(const Node* row) {
    ActionNodePlacementActionNode a;
    a.group = getText(row, "group");
    a.npc = getText(row, "npc");
    a.transform = ParseActionNodeTransform(row, "transform");
    a.playerNode = ReadBoolAlways(row, "player_node");
    return a;
}
ActionNodePlacementSpawnNode ParseActionNodePlacementSpawnNode(const Node* row) {
    ActionNodePlacementSpawnNode s;
    s.group = getText(row, "group");
    s.name = getText(row, "name");
    s.transform = ParseActionNodeTransform(row, "transform");
    return s;
}
ActionNodePlacementVehicle ParseActionNodePlacementVehicle(const Node* row) {
    ActionNodePlacementVehicle v;
    v.group = getText(row, "group");
    v.name = getText(row, "name");
    v.transform = ParseActionNodeTransform(row, "transform");
    return v;
}
ActionNodePlacementObject ParseActionNodePlacementObject(const Node* row) {
    ActionNodePlacementObject o;
    o.name = getText(row, "name");
    o.numActionNodes = ReadInt32Always(row, "num_action_nodes");
    o.numSpawnNodes = ReadInt32Always(row, "num_spawn_nodes");
    o.numVehicles = ReadInt32Always(row, "num_vehicles");
    o.actionNodes = parseRows<ActionNodePlacementActionNode>(row, "action_node", ParseActionNodePlacementActionNode);
    o.spawnNodes = parseRows<ActionNodePlacementSpawnNode>(row, "spawn_node", ParseActionNodePlacementSpawnNode);
    o.vehicles = parseRows<ActionNodePlacementVehicle>(row, "vehicle", ParseActionNodePlacementVehicle);
    return o;
}
ActionNodesFile ParseActionNodesFile(const Document& doc) {
    ActionNodesFile out;
    out.objects = parseRows<ActionNodePlacementObject>(doc.root(), "object", ParseActionNodePlacementObject);
    return out;
}

// ---------------------------------------------------------------------------
// §14 Distant-population tables
// ---------------------------------------------------------------------------
DistantPedestrian ParseDistantPedestrian(const Node* row) {
    DistantPedestrian p;
    p.name = getText(row, "Name");
    p.mesh = getText(row, "Mesh");
    return p;
}
DistantPedsTable ParseDistantPedsTable(const Document& doc) {
    DistantPedsTable out;
    out.rows = parseRows<DistantPedestrian>(doc.table(), "Distant_Pedestrian", ParseDistantPedestrian);
    return out;
}

DistantPedColorSet ParseDistantPedColorSet(const Node* row) {
    DistantPedColorSet s;
    const Node* skin = FindChild(row, "Skin_Colors");
    s.skinColors = parseRows<Rgb>(skin, "Skin_Color", parseRgb);
    const Node* hair = FindChild(row, "Hair_Colors");
    s.hairColors = parseRows<Rgb>(hair, "Hair_Color", parseRgb);
    const Node* clothing = FindChild(row, "Clothing_Colors");
    s.clothingColors = parseRows<DistantPedClothingColorSet>(clothing, "Clothing_Color_Set", [](const Node* c) {
        DistantPedClothingColorSet set;
        set.undershirt = parseRgb(FindChild(c, "Undershirt_Color"));
        set.overshirt = parseRgb(FindChild(c, "Overshirt_Color"));
        set.pants = parseRgb(FindChild(c, "Pants_Color"));
        set.shoe = parseRgb(FindChild(c, "Shoe_Color"));
        set.accent = parseRgb(FindChild(c, "Accent_Color"));
        return set;
    });
    return s;
}
DistantPedColorsTable ParseDistantPedColorsTable(const Document& doc) {
    DistantPedColorsTable out;
    out.colorSets = parseRows<DistantPedColorSet>(doc.table(), "Color_Set", ParseDistantPedColorSet);
    return out;
}

std::optional<DistantPedSpawnParameters> ParseDistantPedSpawnParameters(const Document& doc) {
    const Node* n = FindChild(doc.table(), "Distant_Ped_Spawn_Parameters");
    if (!n) return std::nullopt;
    DistantPedSpawnParameters p;
    p.lengthPerSpawn = ReadFloatAlways(n, "Length_Per_Spawn");
    p.baseSpawningDist = ReadFloatAlways(n, "Base_Spawning_Dist");
    p.maxSpawningDist = ReadFloatAlways(n, "Max_Spawning_Dist");
    p.doubleSpawnHeight = ReadFloatAlways(n, "Double_Spawn_Height");
    p.despawnExpansionDistance = ReadFloatAlways(n, "Despawn_Expansion_Distance");
    p.cameraLookaheadTime = ReadFloatAlways(n, "Camera_Lookahead_Time");
    p.spawnAngle = ReadFloatAlways(n, "Spawn_Angle");
    p.despawnAngle = ReadFloatAlways(n, "Despawn_Angle");
    return p;
}
std::optional<DistantVehicleSpawnParameters> ParseDistantVehicleSpawnParameters(const Document& doc) {
    const Node* n = FindChild(doc.table(), "Distant_Vehicle_Spawn_Parameters");
    if (!n) return std::nullopt;
    DistantVehicleSpawnParameters p;
    p.baseSpawningDist = ReadFloatAlways(n, "Base_Spawning_Dist");
    p.maxSpawningDist = ReadFloatAlways(n, "Max_Spawning_Dist");
    p.doubleSpawnHeight = ReadFloatAlways(n, "Double_Spawn_Height");
    p.despawnExpansionDistance = ReadFloatAlways(n, "Despawn_Expansion_Distance");
    p.cameraLookaheadTime = ReadFloatAlways(n, "Camera_Lookahead_Time");
    p.spawnAngle = ReadFloatAlways(n, "Spawn_Angle");
    p.despawnAngle = ReadFloatAlways(n, "Despawn_Angle");
    return p;
}

DistantVehicleTrafficType ParseDistantVehicleTrafficType(const Node* row) {
    DistantVehicleTrafficType t;
    t.name = getText(row, "Name");
    t.splineType = ReadUInt32Always(row, "Spline_Type");
    t.lengthPerSpawn = ReadFloatAlways(row, "Length_Per_Spawn");
    t.noSpawnZone = ReadFloatAlways(row, "No_Spawn_Zone");
    t.dummyCarLength = ReadFloatAlways(row, "Dummy_Car_Length");
    return t;
}
DistantVehicleTrafficTypesTable ParseDistantVehicleTrafficTypesTable(const Document& doc) {
    DistantVehicleTrafficTypesTable out;
    out.rows = parseRows<DistantVehicleTrafficType>(doc.table(), "Distant_Vehicle_Traffic_Type",
                                                      ParseDistantVehicleTrafficType);
    return out;
}

DistantVehicleColorRow ParseDistantVehicleColorRow(const Node* row) {
    DistantVehicleColorRow r;
    r.color = parseRgb(FindChild(row, "Color"));
    return r;
}
DistantVehicleColorsTable ParseDistantVehicleColorsTable(const Document& doc) {
    DistantVehicleColorsTable out;
    out.rows = parseRows<DistantVehicleColorRow>(doc.table(), "Distant_Vehicle_Color", ParseDistantVehicleColorRow);
    return out;
}

DistantVehicleRow ParseDistantVehicleRow(const Node* row) {
    DistantVehicleRow r;
    r.name = getText(row, "Name");
    r.mesh = getText(row, "Mesh");
    r.trafficType = getText(row, "Traffic_Type");
    return r;
}
DistantVehiclesTable ParseDistantVehiclesTable(const Document& doc) {
    DistantVehiclesTable out;
    out.rows = parseRows<DistantVehicleRow>(doc.table(), "Distant_Vehicle", ParseDistantVehicleRow);
    return out;
}

// ---------------------------------------------------------------------------
// §15 homies.xtbl
// ---------------------------------------------------------------------------
HomieHuman ParseHomieHuman(const Node* row) {
    HomieHuman h;
    h.character = getText(row, "Character");
    return h;
}
HomieVehicleEntry ParseHomieVehicleEntry(const Node* row) {
    HomieVehicleEntry e;
    e.vehicle = getText(row, "Vehicle");
    e.vehicleVariant = getText(row, "Vehicle_Variant");
    e.weight = ReadInt32Always(row, "Weight");
    return e;
}
Homie ParseHomie(const Node* row) {
    Homie h;
    h.name = getText(row, "Name");
    const Node* humans = FindChild(row, "Humans");
    h.humans = parseRows<HomieHuman>(humans, "Human", ParseHomieHuman);
    const Node* vehicles = FindChild(row, "Vehicles");
    h.vehicles = parseRows<HomieVehicleEntry>(vehicles, "Vehicle_Entry", ParseHomieVehicleEntry);

    // §15: case-SENSITIVE exact strings - NOT HasFlag (case-insensitive).
    const Node* flags = FindChild(row, "Flags");
    for (const Node* f = FindChild(flags, "Flag"); f; f = NextSibling(flags, f, "Flag")) {
        const std::string* t = ChildText(f, "");
        if (!t) continue;
        if (*t == "clear gang notoriety") h.flagClearGangNotoriety = true;
        else if (*t == "clear police notoriety") h.flagClearPoliceNotoriety = true;
        else if (*t == "form own squad") h.flagFormOwnSquad = true;
        else if (*t == "give random weapons") h.flagGiveRandomWeapons = true;
        else if (*t == "player selects vehicle") h.flagPlayerSelectsVehicle = true;
        else if (*t == "repair players vehicle") h.flagRepairPlayersVehicle = true;
        else if (*t == "turn to ped after driveup") h.flagTurnToPedAfterDriveup = true;
        else if (*t == "emergency") h.flagEmergency = true;
        else if (*t == "taxi") h.flagTaxi = true;
        else if (*t == "requires saints") h.flagRequiresSaints = true;
    }

    h.imageName = getText(row, "Image_name");
    h.displayName = getText(row, "Display_Name");
    h.displayDesc = getText(row, "Display_Desc");
    const Node* blocked = FindChild(row, "Blocked_for_Mission");
    h.blockedForMission = parseRows<Text>(blocked, "Flag", [](const Node* f) { return getText(f); });
    h.audio = getText(row, "Audio");
    h.framework = getText(row, "Framework");
    h.isDlc = ReadBoolAlways(row, "Is_DLC");
    return h;
}
HomiesTable ParseHomiesTable(const Document& doc) {
    HomiesTable out;
    out.rows = parseRows<Homie>(doc.table(), "Homie", ParseHomie);
    return out;
}

// ---------------------------------------------------------------------------
// §16 follower_heads.xtbl
// ---------------------------------------------------------------------------
FollowerHead ParseFollowerHead(const Node* row) {
    FollowerHead f;
    f.persona = getText(row, "Persona");
    f.charname = getText(row, "Charname");
    f.bitmap = getText(row, "Bitmap");
    return f;
}
FollowerHeadsTable ParseFollowerHeadsTable(const Document& doc) {
    FollowerHeadsTable out;
    const Node* heads = FindChild(doc.table(), "Heads");
    const Node* associations = FindChild(heads, "Associations");
    out.rows = parseRows<FollowerHead>(associations, "Follower", ParseFollowerHead);
    return out;
}

// ---------------------------------------------------------------------------
// §17 driver_bailout.xtbl
// ---------------------------------------------------------------------------
std::optional<DriverBailout> ParseDriverBailout(const Document& doc) {
    const Node* n = FindChild(doc.table(), "Driver_Bailout");
    if (!n) return std::nullopt;
    DriverBailout d;
    d.maxDistance = ReadFloatAlways(n, "Max_Distance");
    d.minDistance = ReadFloatAlways(n, "Min_Distance");
    d.maxDamage = ReadUInt32Always(n, "Max_Damage");
    d.minDamage = ReadUInt32Always(n, "Min_Damage");
    d.maxDamagePercentReward = ReadFloatAlways(n, "Max_Damage_Percent_Reward");
    d.minVelocity = ReadFloatAlways(n, "Min_Velocity");
    d.endTime = ReadFloatAlways(n, "End_Time");
    d.recordDisplayTime = GetFloat(n, "Record_Display_Time");
    d.recordQueueTime = GetFloat(n, "Record_Queue_Time");
    d.recordThreshold = ReadFloatAlways(n, "Record_Threshold");
    d.showDamageRecord = ReadBoolAlways(n, "Show_Damage_Record");
    d.maxRespect = ReadInt32Always(n, "Max_Respect");
    d.maxLifetimeRespect = ReadInt32Always(n, "Max_Lifetime_Respect");
    d.maxCash = ReadFloatAlways(n, "Max_Cash");
    d.vehicleDamageMultiplier = ReadFloatAlways(n, "Vehicle_Damage_Multiplier");
    return d;
}

// ---------------------------------------------------------------------------
// §18 vehicle_despawn.xtbl
// ---------------------------------------------------------------------------
VehicleDespawnEntry ParseVehicleDespawnEntry(const Node* row) {
    VehicleDespawnEntry e;
    e.numCars = ReadInt32Always(row, "num_cars");
    e.time = ReadInt32Always(row, "time");
    return e;
}
std::optional<VehicleDespawnTable> ParseVehicleDespawnTable(const Document& doc) {
    const Node* innerTable = FindChild(doc.table(), "Table"); // §18: Table -> a SECOND element also named Table
    if (!innerTable) return std::nullopt;
    VehicleDespawnTable t;
    const Node* inView = FindChild(innerTable, "in_view_despawn_info");
    t.inViewDespawnInfo = parseRows<VehicleDespawnEntry>(inView, "abandon_despawn_info", ParseVehicleDespawnEntry);
    const Node* outOfView = FindChild(innerTable, "out_of_view_despawn_info");
    t.outOfViewDespawnInfo =
        parseRows<VehicleDespawnEntry>(outOfView, "abandon_despawn_info", ParseVehicleDespawnEntry);
    t.abandonDespawnDelay = ReadInt32Always(innerTable, "abandon_despawn_delay");
    t.playerVehicleDespawnDistance = ReadInt32Always(innerTable, "player_vehicle_despawn_distance");
    return t;
}

// ---------------------------------------------------------------------------
// §19 Escort_constants.xtbl
// ---------------------------------------------------------------------------
std::optional<EscortConstants> ParseEscortConstants(const Document& doc) {
    const Node* escort = FindChild(doc.table(), "Escort_Constants");
    const Node* tiger = FindChild(escort, "Tiger_Constants");
    if (!tiger) return std::nullopt;
    EscortConstants c;

    const Node* rage = FindChild(tiger, "Rage");
    c.rage.present = rage != nullptr;
    c.rage.desiredSpeedMps = ReadFloatAlways(rage, "Desired_speed_MPS");
    c.rage.rageAttackDamageHp = ReadFloatAlways(rage, "Rage_Attack_Damage_HP");
    c.rage.rageIncreaseRate = ReadFloatAlways(rage, "Rage_Increase_Rate");
    c.rage.rageDecreaseRate = ReadFloatAlways(rage, "Rage_Decrease_Rate");

    const Node* penalties = FindChild(tiger, "Penalties");

    const Node* vehicles = FindChild(penalties, "Vehicles");
    c.vehicles.present = vehicles != nullptr;
    // spec-tables-traffic-ai.md s19: "Vehicle_Damage_Penalty_MS 0x014BB25C 4000 (u32)" under the section's
    // [CONFIRMED - disassembly, read in full] marker (the other leaves' reader types are OPEN there, so they stay float).
    c.vehicles.vehicleDamagePenaltyMs = ReadUInt32Always(vehicles, "Vehicle_Damage_Penalty_MS");
    c.vehicles.vehicleDamageThreshold = ReadFloatAlways(vehicles, "Vehicle_Damage_Threshold");
    c.vehicles.vehicleDamageCooldownMs = ReadFloatAlways(vehicles, "Vehicle_Damage_Cooldown_MS");

    const Node* humans = FindChild(penalties, "Humans");
    c.humans.present = humans != nullptr;
    c.humans.humanDamagePenaltyMs = ReadFloatAlways(humans, "Human_Damage_Penalty_MS");
    c.humans.humanDamageThreshold = ReadFloatAlways(humans, "Human_Damage_Threshold");

    const Node* movers = FindChild(penalties, "Movers");
    c.movers.present = movers != nullptr;
    c.movers.moverDamagePenaltyMs = ReadFloatAlways(movers, "Mover_Damage_Penalty_MS");
    c.movers.moverMassThresholdKg = ReadFloatAlways(movers, "Mover_Mass_Threshold_KG");
    c.movers.moverDamageCooldownMs = ReadFloatAlways(movers, "Mover_Damage_Cooldown_MS");

    const Node* world = FindChild(penalties, "World");
    c.world.present = world != nullptr;
    c.world.worldDamagePenaltyMs = ReadFloatAlways(world, "World_Damage_Penalty_MS");
    c.world.worldDamageThresholdHp = ReadFloatAlways(world, "World_Damage_Threshold_HP");
    c.world.worldDamageCooldownMs = ReadFloatAlways(world, "World_Damage_Cooldown_MS");

    return c;
}

// ---------------------------------------------------------------------------
// §20 human_transition.xtbl
// ---------------------------------------------------------------------------
HumanTransitionTransition ParseHumanTransitionTransition(const Node* row) {
    HumanTransitionTransition t;
    t.endState = getText(row, "End_State");
    t.transitionAnimation = getText(row, "Transition_Animation");
    t.speedMultiplier = ReadFloatAlways(row, "Speed_Multiplier");
    t.allowPlayer = ReadBoolAlways(row, "Allow_Player");
    t.allowPlayerGang = ReadBoolAlways(row, "Allow_Player_Gang");
    t.allowOtherGang = ReadBoolAlways(row, "Allow_Other_Gang");
    t.allowPolice = ReadBoolAlways(row, "Allow_Police");
    t.allowCivilian = ReadBoolAlways(row, "Allow_Civilian");
    return t;
}
HumanTransitionBlend ParseHumanTransitionBlend(const Node* row) {
    HumanTransitionBlend b;
    b.endState = getText(row, "End_State");
    b.blendTime = ReadFloatAlways(row, "Blend_Time");
    b.oneHBlendTime = ReadFloatAlways(row, "OneH_Blend_Time");
    b.twoHBlendTime = ReadFloatAlways(row, "TwoH_Blend_Time");
    return b;
}
HumanTransition ParseHumanTransition(const Node* row) {
    HumanTransition h;
    h.name = getText(row, "Name");
    const Node* grid = FindChild(row, "start_state_grid");
    h.startStateGrid =
        parseRows<Text>(grid, "start_state_elm", [](const Node* n) { return getText(n, "start_state"); });
    h.defaultTransitionFromStand = ReadBoolAlways(row, "default_transition_from_stand");
    const Node* transitions = FindChild(row, "Transitions");
    h.transitions = parseRows<HumanTransitionTransition>(transitions, "Transition", ParseHumanTransitionTransition);
    const Node* blends = FindChild(row, "Blends");
    h.blends = parseRows<HumanTransitionBlend>(blends, "Blend", ParseHumanTransitionBlend);
    return h;
}
HumanTransitionTable ParseHumanTransitionTable(const Document& doc) {
    HumanTransitionTable out;
    out.rows = parseRows<HumanTransition>(doc.table(), "Human_Transition", ParseHumanTransition);
    return out;
}

// ---------------------------------------------------------------------------
// §21 PEDF_Life.xtbl / Life_default.xtbl
// ---------------------------------------------------------------------------
LifeAnimationEntry ParseLifeAnimationEntry(const Node* row) {
    LifeAnimationEntry e;
    e.id = getText(row, "ID");
    const Node* anim = FindChild(row, "Animation");
    e.filename = getText(anim, "Filename");
    return e;
}
LifeGroup ParseLifeGroup(const Node* row) {
    LifeGroup g;
    const Node* states = FindChild(row, "States");
    g.states = parseRows<LifeAnimationEntry>(states, "State", ParseLifeAnimationEntry);
    const Node* actions = FindChild(row, "Actions");
    g.actions = parseRows<LifeAnimationEntry>(actions, "Action", ParseLifeAnimationEntry);
    return g;
}
LifeSkeletonSet ParseLifeSkeletonSet(const Node* row) {
    LifeSkeletonSet s;
    const Node* groups = FindChild(row, "Groups");
    s.groups = parseRows<LifeGroup>(groups, "Group", ParseLifeGroup);
    return s;
}
LifeTable ParseLifeTable(const Document& doc) {
    LifeTable out;
    out.skeletonSets = parseRows<LifeSkeletonSet>(doc.table(), "Skeleton_Set", ParseLifeSkeletonSet);
    return out;
}

// ---------------------------------------------------------------------------
// §22 node_graph_files.xtbl (no <Table> wrapper)
// ---------------------------------------------------------------------------
NodeGraphFileEntry ParseNodeGraphFileEntry(const Node* row) {
    NodeGraphFileEntry e;
    e.name = getText(row, "name");
    return e;
}
NodeGraphFilesFile ParseNodeGraphFilesFile(const Document& doc) {
    NodeGraphFilesFile out;
    // Real data: root()'s child is a wrapper also named "node_graph_files"; the
    // rows are ITS children (see the header's REAL-DATA CORRECTION comment).
    const Node* wrapper = FindChild(doc.root(), "node_graph_files");
    out.files = parseRows<NodeGraphFileEntry>(wrapper, "node_graph_file", ParseNodeGraphFileEntry);
    return out;
}

} // namespace sr3tables_trafficai
