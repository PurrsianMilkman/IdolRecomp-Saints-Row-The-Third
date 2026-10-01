#pragma once

// Typed per-table readers for the "traffic, ambient population, roadblocks and AI"
// group of `.xtbl` tables (spec-tables-traffic-ai.md). Built ONLY on sr3xtbl's shared
// accessors (include/sr3xtbl/xtbl.h) - this header/its .cpp never touches the parser,
// never opens a container, and reproduces no code from the game exe (cleanroom
// boundary, spec-tables-traffic-ai.md's own preamble).
//
// SCOPE. spec-tables-traffic-ai.md's per-element tables give three things for each
// element: its TYPE, its reader FLAVOUR (the "always write" reader that leaves an
// UNSPECIFIED value when the element is absent, vs the "write only if present"
// reader - spec 1.3), and sometimes a DESTINATION conversion (a unit scale, a clamp,
// a cos()/reciprocal, a cross-field rule such as "raised to at least Repeat_min").
// This reader surfaces the first two faithfully (one struct field per element,
// typed, using sr3xtbl::Always<T> / std::optional<T> exactly as sr3xtbl.h's own
// convention distinguishes them) and documents the third in a comment WITHOUT
// applying it. Three reasons, stated once here rather than on every field:
//   1. A destination conversion is deterministic and mechanical (multiply by a
//      constant, cos(), 1/x) - safe in isolation - but several of them are chained
//      with a validation/rejection/clamp/monotonicity rule this project has decided
//      NOT to reimplement (see point 2), and splitting "apply the arithmetic" from
//      "apply the rule it feeds" per field, table by table, is exactly the kind of
//      partial reproduction that invites a silent mismatch with no way to check it.
//   2. Row ACCEPTANCE/REJECTION, capacity clamps, "last write wins" buffer-reuse
//      quirks (§6) and cross-field monotonicity (§10's seen_in_last chain) are
//      LOADER decisions about whether/how a row is used, not per-element schema -
//      out of scope for a struct-plus-parse-function library. A row this reader
//      returns is not a claim that the game's loader would have accepted it.
//   3. spec-tables-traffic-ai.md §24.3: real base-game values are readable for only
//      one table in this group (homies.xtbl's DLC samples); every numeric range,
//      default and conversion elsewhere rests on disassembly alone, never checked
//      against a real row. Baking business-logic arithmetic into a shared reader
//      that cannot be checked is exactly the risk the project's no-invented-fixes
//      rule exists for.
// Downstream code that needs the engine's post-processed value can apply the
// documented formula itself, with the same citation this header gives it.
//
// PRESENCE. Every "always write" element uses sr3xtbl::Always<T>: value is a
// deterministic 0 stand-in, NOT the engine's value, whenever !present - check
// present, never assume 0 (sr3xtbl/xtbl.h, spec 1.3). Every "if present" element
// uses std::optional<T>. Every TEXT element uses sr3tables_trafficai::Text
// (std::optional<std::string>) uniformly: sr3xtbl has no "always write" text
// reader (CopyText's own contract, spec 1.3 row for 0x00DABA70, "leaves dst
// untouched if absent"), and the engine cannot tell "absent" from "present but
// empty" for text either way (sr3xtbl/xtbl.h TEXT rules) - optional<string> models
// exactly that one ambiguity, nothing more.
//
// READER FLAVOUR WHEN THE SPEC DOESN'T SAY. Most per-table tables in the spec give
// a type ("s32", "float", "u8") and a validity rule without saying which of the two
// reader flavours was used. spec 1.3's own general note answers this: "Every table
// below that needs a default therefore either pre-initialises the destination and
// uses the if-present reader, or is fed a value the row validators later
// range-check" - i.e. a validated field with NO stated absence-default is the
// always-write flavour (Always<T>), and a field whose spec text states an explicit
// default value/behaviour when absent is the if-present flavour (optional<T>).
// This header applies that rule mechanically and cites it per field only where it
// is not obvious.
//
// EXTERNAL REGISTRIES. Several elements resolve through a runtime registry this
// project has no access to (the character-definition registry, the vehicle table,
// the weapon table, the team registry, the animation-name table, Wwise event ids,
// the item/mesh registry, the rank/character-descriptor table - all "OPEN" or
// "not traced" in the spec, or simply outside this table group's own tools). Such
// fields keep the RAW text (Text) and are commented "external, unresolved here";
// none of the numeric ids/pointers the engine would compute for them are guessed.
//
// TWO FILES WITH NO <Table> WRAPPER. action_nodes.xtbl (§13.4) and
// node_graph_files.xtbl (§22) take their rows directly from Document::root()
// (sr3xtbl.h's own comment on Document::table()); every other table below reads
// Document::table().
//
// WHAT IS DELIBERATELY LEFT OUT (spec explicitly marks it OPEN/UNKNOWN, so this
// header does not guess it):
//   * generic_characters.xtbl's 21 compiled-in slot NAMES: spec §1.5/§12 gives only
//     the first and last ("Young Male ... Camera Man"), not the list. Only the
//     vehicle file's 20 names are given in full (§12), so only that name table is
//     compiled in here (kGenericVehicleSlotNames); GenericCharacterRow::name stays
//     raw text with no enum resolution.
//   * life files (§21): Skeleton_Set and Group have no identifying leaf element in
//     the spec's element tree ("the meaning of Skeleton_Set names were not traced
//     [OPEN]") - LifeSkeletonSet/LifeGroup carry no name field.
//   * action_nodes.xtbl (§13.4): "the per-object packing ... and the runtime use
//     were not traced [OPEN]" - this reader stops at the element tree the spec DOES
//     give (object/action_node/spawn_node/vehicle and their children); no packed
//     record layout is modelled.
//   * node_graph_files.xtbl (§22): only the file-list element tree is read; the
//     state_machine/blend_tree graph format it names is explicitly out of scope
//     ("a separate subsystem", not specified here).
//
// Every struct below cites its spec section; read spec-tables-traffic-ai.md for the
// full prose (ranges, capacities, cross-references) this header intentionally
// keeps out of the type system.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_trafficai {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ---------------------------------------------------------------------------
// Shared field shapes (see the header banner for why these exist)
// ---------------------------------------------------------------------------

// A text element: nullopt when the child is absent OR present with no text (the
// engine's own ambiguity - sr3xtbl::ChildText, xtbl.h TEXT rules).
using Text = std::optional<std::string>;

// A row's raw text matched against a fixed, compiled-in, case-insensitive name
// list via sr3xtbl::EnumIndex (spec 1.3, 0x00DAC830): `text` is the element's raw
// text (nullopt if the element is absent/empty), `index` is EnumIndex()'s result
// (-1 if the node is null, has no text, or nothing matches).
struct EnumText {
    Text text;
    int index = -1;
};

// action_node_npcs.xtbl's own text-boolean convention (spec 1.3, distinct from the
// general true/yes bool reader 0x00DAC480/0x00DAC510): the element's text is
// compared to "True"/"False" case-insensitively; ANY other text - including an
// absent element - is reported here as Unset, matching the engine's own rule that
// an unrecognised spelling "leaves the field at the value it had before the
// compare" (i.e. at whatever default the byte was pre-set to; see each field's
// comment for that default - it is documented, not applied, for the reasons in
// the header banner).
enum class TextBool { True, False, Unset };

// One count/pointer-free RGB triple read as three named children (`R`, `G`, `B`),
// each an always-write float (spec §14: "a colour element has children R, G, B,
// each an always-write float divided by 255 on load"; the /255 is NOT applied
// here - see the header banner).
struct Rgb {
    Always<float> r, g, b;
};

// ---------------------------------------------------------------------------
// §1.5 Fixed enum tables compiled into the exe (game data, quoted verbatim from
// spec-tables-traffic-ai.md 1.5; listed because rows in this group refer to them
// by name). Only the lists the spec gives IN FULL are here - see the header
// banner for the one that is not (generic_characters' 21 slot names).
// ---------------------------------------------------------------------------
namespace enums {

// 70 entries, ids 0..69 in this order (spec 1.5). Used by ai_goals.action and
// ai_behavior.ability_elm.Action (spec §23 cross-reference map: both matched
// case-insensitively against this table).
inline constexpr std::array<std::string_view, 70> kCombatActionNames = {
    "fire", "fire sweep", "fire suppress", "fire chaos", "throw grenade", "throw weapon",
    "melee", "melee vehicle", "cover take def", "cover take off", "cover advance",
    "cover stay down", "cover fire", "cover popout", "hold position", "melee stand back",
    "move retreat", "move regroup", "move advance", "move chase", "avatar chase",
    "move surround", "move rush", "move rush to melee", "move scripted", "move to navmesh",
    "move sidestep", "move to post combat", "move to post idle", "move close in",
    "follow make way", "follow avoid LOF", "follow formation", "follow acquire",
    "follow player", "follow teleport", "follow lemming", "follow to car", "follow other car",
    "follow carjack", "follow browse", "investigate move", "investigate look",
    "investigate peek", "reload", "pickup weapon", "human shield grab", "taunt",
    "vehicle passenger", "vehicle enter scpt", "vehicle extract", "brute throw prop",
    "brute car flip", "brute bull rush", "brute qte player", "brute attack brute",
    "brute gun finisher", "avatar shockwave", "avatar stomp", "avatar taunt",
    "avatar fireballs", "avatar teleport stomp", "avatar bullrush", "quick kill",
    "zombie eat", "zombie explode", "killbane chase", "killbane strafe",
    "roller blader change", "idle",
};

// 17 entries, ids 0..16 in this order (spec 1.5). Used by ai_goals.Name.
inline constexpr std::array<std::string_view, 17> kAiGoalNames = {
    "change mode", "avoid brute", "kill near player", "survive", "retreat", "group",
    "suppress", "cover", "taking_damage", "advance", "reload", "kill enemy", "flush out",
    "frustrated", "acquire target", "investigate", "idle",
};

// 23 entries (spec 1.5). Used by ai_behavior.human_elm.Weapon_Class.
inline constexpr std::array<std::string_view, 23> kWeaponClassNames = {
    "pistol", "smg", "rifle", "shotgun", "launcher", "thrown", "knife", "nightstick",
    "stungun", "bat", "sword", "pimp slap", "video camera", "knuckles", "flamethrower",
    "cutscene only", "man cannon", "minigun", "pepper spray", "chainsaw", "waterspray",
    "script", "vehicle",
};

// 20 entries (spec §12). Used by generic_vehicles.Name - matched CASE-SENSITIVELY
// per spec §12 ("unlike the character file"), so this table is NOT looked up with
// sr3xtbl::EnumIndex (case-insensitive); see ParseGenericVehicleRow.
inline constexpr std::array<std::string_view, 20> kGenericVehicleSlotNames = {
    "Truck", "Police Car", "Police Helicopter", "Attack Helicopter", "Swat Car",
    "Swat APC Police", "Swat APC Ultor", "Armored Truck", "News Van", "Taxi",
    "Ambulance", "Garbage Truck", "Delivery Gyro", "Delivery Freckled Bitches",
    "Delivery Lik A Chick", "Pimp", "Ship It", "Firetruck", "Hazmat", "Gang Wagon",
};

// 5 entries (spec 1.5, §5). Used by roadblock_layouts.Object/Child.Type.
inline constexpr std::array<std::string_view, 5> kRoadblockObjectTypeNames = {
    "Vehicle", "Vehicle Group", "NPC", "Item", "Action Node",
};

// 4 entries (spec 1.5, §5). Used by roadblock_layouts.Flags/Flag.
inline constexpr std::array<std::string_view, 4> kRoadblockLayoutFlagNames = {
    "end cap", "intersection", "no flee", "Can Spawn on Disabled Lanes",
};

// 6 entries (spec 1.5). panic_reactions.Name fills these fixed slots (0-5) when it
// CRC-matches one; not exposed as an EnumText field (the match is by 32-bit CRC of
// the whole name, not a direct EnumIndex text compare) - listed here for callers
// that want to replicate the slot check themselves.
inline constexpr std::array<std::string_view, 6> kPanicBuiltInNames = {
    "On Fire", "Pepper Spray", "Sticky Projectile", "Flashbang", "Luch Grenade",
    "Fart In A Jar",
};

} // namespace enums

// ---------------------------------------------------------------------------
// §2 traffic_lanes.xtbl - lane-speed classes
// ---------------------------------------------------------------------------
// Table -> NewEntity -> LaneGrid -> repeated LaneSpeed -> Speed. Loader 0x00A69060
// (whole function read, exhaustive). No bound check in the loader; the array
// adjacency implies capacity 754 (HIGH CONFIDENCE, not enforced here). Consumers
// and the unit of Speed are OPEN (spec open-item 11) - Speed is stored raw, no
// conversion, by the loader itself, so there is nothing to apply/omit here.
struct TrafficLaneSpeed {
    Always<float> speed; // Speed: always-write float (§2); array element i in document order
};
struct TrafficLanesTable {
    std::vector<TrafficLaneSpeed> laneSpeeds; // Table/NewEntity/LaneGrid/LaneSpeed*
};
TrafficLanesTable ParseTrafficLanesTable(const Document& doc);

// ---------------------------------------------------------------------------
// §4 ambient_traffic_events.xtbl - scripted ambient-traffic events
// ---------------------------------------------------------------------------
// Table -> repeated Ambient_traffic_event. Loader 0x005E3660, row reader
// 0x005E3430 (both read in full). Whole-row accept/reject rules (Name required,
// range checks, Max_life >= Min_life, layout-name resolution) are loader
// decisions - NOT enforced by this reader; a row returned here is not a claim the
// loader would have kept it.
struct AmbientTrafficEvent {
    Text name;                  // Name: required non-empty per the loader, but never itself stored (§4)
    Always<int32_t> todStart;   // TOD_start: valid 0..23 (rejected outside); engine stores value*100
    Always<int32_t> todEnd;     // TOD_end: valid 0..24; engine stores value*100
    bool flagsPresent = false;  // Flags element itself: required (absent -> row rejected, not enforced here)
    bool disabledDuringNotoriety = false; // Flags/Flag == "disabled_during_notoriety" (HasFlag)
    Always<float> minLife;      // Min_life: valid 0.1..240.0 (hours); engine stores *3,600,000 as ms
    Always<float> maxLife;      // Max_life: same range; engine requires >= Min_life (post-scale, not enforced)
    Always<float> cooldownTime; // Cooldown_time: valid 0.0..240.0 (hours); engine stores *3,600,000 as ms
    Text roadblockLayout;       // Roadblock_layout: required non-empty; -> roadblock_layouts.xtbl Name (§23)
};
struct AmbientTrafficEventsTable {
    std::vector<AmbientTrafficEvent> rows;
};
AmbientTrafficEvent ParseAmbientTrafficEvent(const Node* row);
AmbientTrafficEventsTable ParseAmbientTrafficEventsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §5 roadblock_layouts.xtbl - physical roadblock compositions
// ---------------------------------------------------------------------------
// Table -> repeated Roadblock_layouts (row; plural IS the row element name) ->
// Name, Num_lanes, Flags, Objects -> repeated Object -> ... -> Children ->
// repeated Child. Loader 0x0092BDE0, row reader 0x0092B7B0 (both read in full).
struct RoadblockChild {
    EnumText type;               // Type: §1.5 5 names (kRoadblockObjectTypeNames), case-insensitive
    Text resourceName;           // Resource_name: required non-empty
    // Variant_Name is read for top-level Objects only; ignored for Children (spec §5)
    Always<float> xOffset, zOffset; // X_offset/Z_offset (position; Y has no element - engine keeps its prior value)
    Always<float> heading;       // Heading, offset by the parent's heading at runtime (not applied here)
    std::optional<int32_t> hitpoints; // Hitpoints: if-present (engine's image default is -1 when absent)
    Always<uint8_t> seatIndex;   // Seat_Index: children only; engine default 0xFF (unreadable/absent -> 0xFF)
};
struct RoadblockObject {
    EnumText type;               // Type: §1.5 5 names; unknown/empty rejects the object (not enforced here)
    Text resourceName;           // Resource_name: required non-empty
    Text variantName;            // Variant_Name (top-level objects only)
    std::optional<uint8_t> laneIndex; // Lane_index: top-level only; must be 1..Num_lanes (byte, not enforced)
    Always<float> xOffset, zOffset;
    Always<float> heading;       // stored AS READ - the angle-wrap's result is discarded at both call sites (§5)
    std::optional<int32_t> hitpoints; // if-present (image default -1)
    std::vector<RoadblockChild> children; // Children/Child*
};
struct RoadblockLayout {
    Text name;                   // Name: char[0x40], required; interned; CRC is the cross-ref key (§23)
    Always<int32_t> numLanes;    // Num_lanes: valid 1..4 (2 required if the end-cap flag is set - not enforced)
    bool flagEndCap = false, flagIntersection = false, flagNoFlee = false,
         flagCanSpawnOnDisabledLanes = false; // Flags/Flag, §1.5's 4 names (kRoadblockLayoutFlagNames)
    std::vector<RoadblockObject> objects; // Objects/Object*
};
struct RoadblockLayoutsTable {
    std::vector<RoadblockLayout> rows;
};
RoadblockChild ParseRoadblockChild(const Node* row);
RoadblockObject ParseRoadblockObject(const Node* row);
RoadblockLayout ParseRoadblockLayout(const Node* row);
RoadblockLayoutsTable ParseRoadblockLayoutsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §6 roadblock_notoriety.xtbl - per-faction, per-notoriety-level schedule
// ---------------------------------------------------------------------------
// Table -> repeated Roadblock_notoriety -> Name, Team, Levels -> repeated Level ->
// ... -> Roadblock_layouts -> repeated Roadblock_layout -> Roadblock, Cooldown.
// Loader 0x0060A300, row reader 0x0060A010 (both read in full).
struct RoadblockNotorietyLayoutEntry {
    // NOTE (§6 "buffer-reuse quirk"): an entry with no Roadblock child re-resolves
    // whatever text a PRIOR CopyText call into the same stack buffer left there
    // (for the first entry, the row's own Team name). Not reproduced here - this
    // struct records only what THIS entry's own Roadblock element contains.
    Text roadblock;              // Roadblock: -> roadblock_layouts.Name (§23); required non-empty
    Always<int32_t> cooldown;    // Cooldown: valid 0..600 (s); engine stores *1000 ms; out-of-range -> not counted
};
struct RoadblockNotorietyLevel {
    Always<int32_t> notorietyLevel;        // Notoriety_level: 1..5, else the row stops (not enforced here)
    Always<int32_t> maxActiveRoadblocks;   // Max_active_roadblocks: 1..3
    Always<int32_t> minTime;               // Min_time: 0..300 (s); engine stores *1000 ms
    Always<int32_t> maxTime;               // Max_time: 0..600 (s); engine stores *1000 ms
    bool roadblockLayoutsPresent = false;  // Roadblock_layouts container: required (§6)
    std::vector<RoadblockNotorietyLayoutEntry> layouts; // up to 4 consumed at runtime; ALL present rows kept here
};
struct RoadblockNotoriety {
    Text name;                    // Name: required non-empty, never itself stored (§6)
    Text team;                    // Team: required; -> team registry (ids 1,2,3,5,6 only; names OPEN, §6/§25.2)
    std::vector<RoadblockNotorietyLevel> levels; // Levels/Level*
};
struct RoadblockNotorietyTable {
    std::vector<RoadblockNotoriety> rows;
};
RoadblockNotorietyLayoutEntry ParseRoadblockNotorietyLayoutEntry(const Node* row);
RoadblockNotorietyLevel ParseRoadblockNotorietyLevel(const Node* row);
RoadblockNotoriety ParseRoadblockNotoriety(const Node* row);
RoadblockNotorietyTable ParseRoadblockNotorietyTable(const Document& doc);

// ---------------------------------------------------------------------------
// §7 roadblock_stag_lockdown.xtbl - fixed roadblock sites, lockdown scenario
// ---------------------------------------------------------------------------
// Table -> Roadblock_stag_lockdown -> Roadblock_List -> repeated "Roadbock"
// (SIC - the reader looks for this misspelling; a correctly-spelled Roadblock
// element would be invisible to it, spec §7) -> Roadblock_Layout, Spawn_Point.
// Loader 0x005E3FD0 (only runs when the current level name begins "sr3_city" -
// a runtime gate, not a file-schema fact, so not modelled here).
struct RoadblockStagLockdownEntry {
    Text roadblockLayout; // Roadblock_Layout: required, -> roadblock_layouts.Name (§23)
    Text spawnPoint;      // Spawn_Point: name of a placed world object; registry lookup is OPEN (§7/§25.3)
};
struct RoadblockStagLockdownTable {
    bool rowPresent = false;  // Roadblock_stag_lockdown/Roadblock_List: both required, else load returns 0
    std::vector<RoadblockStagLockdownEntry> entries; // Roadblock_List/"Roadbock" (sic)*
};
RoadblockStagLockdownEntry ParseRoadblockStagLockdownEntry(const Node* row);
RoadblockStagLockdownTable ParseRoadblockStagLockdownTable(const Document& doc);

// ---------------------------------------------------------------------------
// §8 panic_reactions.xtbl - bystander reactions to hazards
// ---------------------------------------------------------------------------
// Table -> repeated Panic -> Name, Priority, optional Timed, optional Movement,
// Player_Animations/NPC_Animations, optional Drug_Effects, optional
// Persona_Situation. Loader 0x004EE110, row reader 0x004EDEF0, sub-reader
// 0x004EDD90 (all read in full).
struct PanicAnimBlock {
    Text animState;       // Anim_State: -> animation-name table index (external registry, unresolved here)
    Text animAction;      // Anim_Action
    Text exitAnimAction;  // Exit_Anim_Action
    Text enterAnimAction; // Enter_Anim_Action
    Always<int32_t> numAnimVariants; // NumAnimVariants: signed, always-write
};
struct PanicReaction {
    Text name;                    // Name: CRC vs the 6 built-ins (kPanicBuiltInNames) fills a fixed slot (§8)
    Always<uint8_t> priority;     // Priority: u8, always-write
    bool timedPresent = false;    // Timed element present? (also flag-byte bit 0 = its NEGATION, §8)
    Always<float> minTimeNpc, maxTimeNpc, minTimePlayer, maxTimePlayer; // meaningful only when timedPresent
    bool movementPresent = false;
    Always<bool> movementRun;     // Movement/Run: read only when Movement is present (§8)
    Always<float> minDist, maxDist; // meaningful only when movementPresent
    bool drugEffectsPresent = false;
    Always<float> weedFactorMax, boozeFactorMax, weedFactorMin, boozeFactorMin; // meaningful only when drugEffectsPresent
    Text personaSituation;        // Persona_Situation: -> Wwise event id (external, unresolved here)
    bool playerAnimationsPresent = false; // absent -> the block is left UNINITIALISED by the engine (§8)
    PanicAnimBlock playerAnimations;
    bool npcAnimationsPresent = false;
    PanicAnimBlock npcAnimations;
};
struct PanicReactionsTable {
    std::vector<PanicReaction> rows;
};
PanicAnimBlock ParsePanicAnimBlock(const Node* container);
PanicReaction ParsePanicReaction(const Node* row);
PanicReactionsTable ParsePanicReactionsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §9 ai_goals.xtbl - per-goal ordered action lists
// ---------------------------------------------------------------------------
// Table -> repeated ai_goal -> Name, order_of_actions -> repeated action_elm ->
// action. Loader 0x004F29B0 (read in full; always re-run by ai_behavior's loader).
struct AiGoalAction {
    EnumText action; // action: vs the 70 combat-action names (kCombatActionNames), case-insensitive; unknown dropped at runtime (not enforced here)
};
struct AiGoal {
    EnumText name; // Name: vs the 17 goal names (kAiGoalNames); index -1 (no match) is an error at runtime (not enforced here)
    std::vector<AiGoalAction> actions; // order_of_actions/action_elm*, document order (engine keeps at most 24)
};
struct AiGoalsTable {
    std::vector<AiGoal> rows;
};
AiGoalAction ParseAiGoalAction(const Node* row);
AiGoal ParseAiGoal(const Node* row);
AiGoalsTable ParseAiGoalsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §10 ai_behavior.xtbl - named combat behaviours
// ---------------------------------------------------------------------------
// Table -> repeated Behavior -> Name, Scripted_Only, combat_aggression,
// Human_Description -> repeated human_elm, Riotshield_Only, Actions -> repeated
// ability_elm, Options, Goals -> {Survive, Suppress, Cover, Taking_damage, Group,
// Advance, Avoid_brute, Kill_near_player, Retreat, Kill_Enemy, Reload, Flush_Out,
// Acquire_Target, Investigate}, goals_can_process_early. Loader 0x004F2B50 (whole
// loader read; record-base arithmetic cross-checked against the assembly).
struct AiHumanDescEntry { // human_elm
    EnumText weaponClass; // Weapon_Class: vs the 23 names (kWeaponClassNames); index -1 if absent/unmatched
    Text team;            // Team: -> team registry (external, unresolved); engine default id 9 ("none") if absent
    Text rank;             // Rank: -> the rank/character-descriptor table (OPEN, §10/§25.2, unresolved here)
};
struct AiAbilityEntry { // ability_elm
    EnumText action;             // Action: vs the 70 combat-action names; unknown -> dropped at runtime
    Always<float> repeatMin;     // Repeat_min: seconds; engine *1000->ms, then raised to a per-action minimum
    Always<float> repeatMax;     // Repeat_max: seconds; engine *1000->ms, then raised to >= Repeat_min
};
struct AiBehaviorOptions {
    bool present = false;
    Always<bool> canFireWhileWalking; // Options/can_fire_while_walking
    Always<bool> canBackStepFire;     // Options/can_back_step_fire
    Always<uint8_t> dodgeChance;      // Options/dodge_chance: u8, valid 0..100
};
struct AiBehaviorCover {
    // absent -> engine defaults 30000/30000/30000/1000/30000 ms and squad_cover_pct=100 (§10, explicit default -> not applied here)
    bool present = false;
    bool takeCoverAimedAt = false, takeCoverShotAt = false, takeCoverDamaged = false; // take_cover_when/Flag text (HasFlag-style); "grenade noticed" is tested by the engine but sets nothing (§10)
    Always<float> abandonCoverTime, findNewCoverTime, advanceCoverTime, stayDownTime, popUpTime; // seconds; engine *1000 -> s16 ms
    Always<uint8_t> squadCoverPct;
};
struct AiBehaviorGroup {
    bool present = false; // absent -> engine sets seperated_distance = FLT_MAX (explicit default -> not applied here)
    Always<float> seperatedDistance; // seperated_distance (sic, spec spelling)
    std::optional<float> seperatedDistanceInterior; // seperated_distance_interior (sic); if-present; defaults to seperatedDistance when absent (explicit default, not applied here)
};
struct AiBehaviorSeenInLast { // shared shape: Kill_Enemy/Flush_Out/Acquire_Target/Investigate
    bool present = false;     // the goal element itself present? (also sets the goal-enabled byte, §10)
    Always<float> seenInLast; // seconds; engine *1000 -> s16 ms; zeroed if absent, then forced monotone across the four (loader rule, not modelled)
};
struct AiBehavior {
    Text name;                    // Name: char[0x1E], bounded copy
    Always<bool> scriptedOnly;    // Scripted_Only (forces combat_aggression to 1 at runtime when true - not applied here)
    Text combatAggression;        // combat_aggression: first char - '0', valid 0..9 (raw text kept; digit not computed here)
    std::vector<AiHumanDescEntry> humanDescription; // Human_Description/human_elm* (skipped entirely by the engine when Scripted_Only, not enforced here)
    Always<bool> riotshieldOnly;  // Riotshield_Only (read by the engine only when not Scripted_Only)
    std::vector<AiAbilityEntry> actions; // Actions/ability_elm*
    AiBehaviorOptions options;
    Always<float> survivalLowHealthPct; // Goals/Survive/low_health_pct (meaningful only when survivePresent)
    bool survivePresent = false;
    Always<float> suppressLength;       // Goals/Suppress/length; seconds; engine *1000 -> s16 ms (meaningful only when suppressPresent)
    bool suppressPresent = false;
    AiBehaviorCover cover;
    bool takingDamagePresent = false; // Goals/Taking_damage: presence-only (enables the goal-enabled byte)
    AiBehaviorGroup group;
    bool advancePresent = false, avoidBrutePresent = false, killNearPlayerPresent = false,
         retreatPresent = false; // presence-only goals (enable the goal-enabled byte; no leaves of their own)
    AiBehaviorSeenInLast killEnemy;    // Goals/Kill_Enemy/seen_in_last
    Always<float> reloadClipPct; // Goals/Reload/clip_pct (meaningful only when reloadPresent)
    bool reloadPresent = false;
    AiBehaviorSeenInLast flushOut;      // Goals/Flush_Out/seen_in_last
    AiBehaviorSeenInLast acquireTarget; // Goals/Acquire_Target/seen_in_last
    AiBehaviorSeenInLast investigate;   // Goals/Investigate/seen_in_last
    Always<bool> goalsCanProcessEarly;  // goals_can_process_early
};
struct AiBehaviorTable {
    std::vector<AiBehavior> rows;
};
AiHumanDescEntry ParseAiHumanDescEntry(const Node* row);
AiAbilityEntry ParseAiAbilityEntry(const Node* row);
AiBehavior ParseAiBehavior(const Node* row);
AiBehaviorTable ParseAiBehaviorTable(const Document& doc);

// ---------------------------------------------------------------------------
// §11 ai_personalities.xtbl - ambient (non-combat) personality tweaks
// ---------------------------------------------------------------------------
// Table -> repeated Personality -> Name, combat_aggression, Flee_tweaks ->
// {Flee_health_pct, flee_events -> Flag*}, Ambient_tweaks -> {attack_unfriendly,
// Taunt_reaction{flee,taunt_back,attack}, MeleeReaction{...}, VehicleWaitResponse
// {...}, VehicleCollisionResponse{...}, HijackBehavior{...}}. Loader 0x00507AC0
// (read in full). NOTE: each of these grouped sub-elements is a set of NAMED
// CHILD LEAVES (each a u8 always-write weight), not a single enum choice - see
// the per-record byte table in §11.
struct AiPersonalityTauntReaction { Always<uint8_t> flee, tauntBack, attack; };
struct AiPersonalityMeleeReaction { Always<uint8_t> flee, worry, doNothing, watch, cheer, attack; };
struct AiPersonalityVehicleWaitResponse { Always<uint8_t> doNothing, honk, threaten; };
struct AiPersonalityVehicleCollisionResponse {
    Always<uint8_t> doNothing, honk, threaten, flee, getOut, attack, attackPlayer;
};
struct AiPersonalityHijackBehavior {
    Always<uint8_t> jackFailureChance, postResistFleeChance, preJackFleeChance;
};
struct AiPersonality {
    Text name;                     // Name -> CRC key
    Text combatAggression;         // combat_aggression: first char - '0', clamped <=2 at runtime (raw text kept)
    bool fleeTweaksPresent = false;
    Always<uint8_t> fleeHealthPct; // Flee_tweaks/Flee_health_pct
    bool fleeAimedAt = false, fleeMeleeHit = false, fleeBulletHeard = false, fleeBulletHit = false,
         fleeExplosionHeard = false; // flee_events/Flag (OR-accumulated across rows at runtime; this row's flags only)
    bool ambientTweaksPresent = false;
    Always<uint8_t> attackUnfriendly; // Ambient_tweaks/attack_unfriendly
    AiPersonalityTauntReaction tauntReaction;
    AiPersonalityMeleeReaction meleeReaction;
    AiPersonalityVehicleWaitResponse vehicleWaitResponse;
    AiPersonalityVehicleCollisionResponse vehicleCollisionResponse;
    AiPersonalityHijackBehavior hijackBehavior;
};
struct AiPersonalitiesTable {
    std::vector<AiPersonality> rows;
};
AiPersonality ParseAiPersonality(const Node* row);
AiPersonalitiesTable ParseAiPersonalitiesTable(const Document& doc);

// ---------------------------------------------------------------------------
// §12 generic_characters.xtbl / generic_vehicles.xtbl - generic-population slots
// ---------------------------------------------------------------------------
// Both: Table -> repeated Generics_Table -> Name, Spawn_Name, (vehicles only)
// Variant_Name. Loaders 0x009189A0 (characters, via helper 0x009188C0) and
// 0x009186E0 (vehicles). Both read in full.
struct GenericCharacterRow {
    Text name;      // Name: -> CRC vs 21 compiled-in slot names (list incomplete in the spec; see header banner)
    Text spawnName; // Spawn_Name: -> character-definition registry (external, unresolved here)
};
struct GenericCharactersTable {
    std::vector<GenericCharacterRow> rows;
};
struct GenericVehicleRow {
    // Name is matched CASE-SENSITIVELY against kGenericVehicleSlotNames (§12,
    // "unlike the character file") - `index` below is computed by an exact
    // (non-EnumIndex) compare; see ParseGenericVehicleRow.
    Text name;
    int index = -1; // case-sensitive match index into kGenericVehicleSlotNames, or -1
    Text spawnName;  // Spawn_Name: -> vehicle-info table by name hash (external, unresolved here)
    Text variantName; // Variant_Name (vehicles only)
};
struct GenericVehiclesTable {
    std::vector<GenericVehicleRow> rows;
};
GenericCharacterRow ParseGenericCharacterRow(const Node* row);
GenericCharactersTable ParseGenericCharactersTable(const Document& doc);
GenericVehicleRow ParseGenericVehicleRow(const Node* row);
GenericVehiclesTable ParseGenericVehiclesTable(const Document& doc);

// ---------------------------------------------------------------------------
// §13.1 action_node_groups.xtbl
// ---------------------------------------------------------------------------
// Table -> repeated action_node_group. Loader 0x008C9760 (read in full).
struct ActionNodeGroup {
    Text name;                     // Name: char[0x1E] -> CRC = the group key (§5, §13.4 cross-refs)
    Always<float> minSpacing;      // Min_spacing
    std::optional<uint32_t> convertToPedMinDelay; // Convert_to_ped_min_delay: u32; explicit default 0 when absent
    Always<uint32_t> convertToPedMaxDelay;        // Convert_to_ped_max_delay: u32 (clamped >= min at runtime, not applied)
    bool spawningNight = false, spawningDay = false, spawningNoon = false, spawningEvening = false; // Spawning/Flag
    Always<bool> capturePeds, outdoorNode, useInRain, storeBrowsing, storeOwnershipSpawn,
                 exclusiveActions, sequenceNodes; // +0x20 byte, bits 0,1,2,3,4,5,7 (bit 6 is Reference_object's OWN presence, tracked below)
    Always<bool> onWater, playerAllowed, spawnDuringHighNotoriety, groupDisabled, stagNode,
                 antiSaintsNode; // +0x21 byte, bits 0,1,2,4,6,7 (bits 3,5 unnamed in the spec)
    Text referenceObject;           // Reference_object: text -> CRC
    bool referenceObjectPresent = false;
    Always<int32_t> spawnTimer;     // Spawn_Timer: s32; engine forces any value < 60 to -1 (not applied here)
    std::optional<int8_t> instanceCap; // Instance_Cap: signed, stored as a byte; explicit default -1 when absent. LABEL: spec-tables-traffic-ai.md s13.1 table row +0x2C "[OPEN - desk review 2026-09-30: which accessor reads these is not stated ... the unsigned 0x00DAC1D0 would read -1 as 0]" - the spec's type is a SIGNED byte (not u32), so the signed GetInt8 read is kept, unverified
    std::optional<int8_t> maxSpawns;   // MaxSpawns: same
    std::vector<Text> npcList;      // npc_list/npc* (text = an action_node_npcs.xtbl row Name, §13.3); engine keeps at most 4
    Text notorietyInfo;             // Notoriety_Info: text -> action_node_notoriety.xtbl row (§13.2, CRC match)
    Text musicEmitter;              // Music_Emitter: -> Wwise event id (external, unresolved here)
    std::optional<uint32_t> minPlayerRank; // Min_Player_Rank: u32; explicit default 0 when absent
};
struct ActionNodeGroupsTable {
    std::vector<ActionNodeGroup> rows;
};
ActionNodeGroup ParseActionNodeGroup(const Node* row);
ActionNodeGroupsTable ParseActionNodeGroupsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §13.2 action_node_notoriety.xtbl
// ---------------------------------------------------------------------------
// Table -> Action_Node_Notoriety (row). Loader 0x008C83B0 (read in full). The
// engine keeps only the FIRST such row in the file (spec §13.2's own quirk); this
// reader parses every row present in the document - the "first row only" rule is
// a whole-FILE loader decision, not a per-row schema fact.
struct ActionNodeNotoriety {
    Text name;                        // Name -> CRC
    Always<bool> spawnFlagsAirOnly;   // Spawn_Flags/Air_Only
    Always<bool> spawnFlagsGang;      // Spawn_Flags/Gang
    Always<int32_t> minNotoriety;     // Min_Notoriety
    Always<int32_t> maxNotoriety;     // Max_Notoriety
    Text weapon;                      // Weapon: text -> weapon table scan (external, unresolved here)
};
struct ActionNodeNotorietyTable {
    std::vector<ActionNodeNotoriety> rows; // see note above: the engine only keeps rows[0]
};
ActionNodeNotoriety ParseActionNodeNotoriety(const Node* row);
ActionNodeNotorietyTable ParseActionNodeNotorietyTable(const Document& doc);

// ---------------------------------------------------------------------------
// §13.3 action_node_npcs.xtbl
// ---------------------------------------------------------------------------
// Table -> repeated action_node_npc. Loader 0x008C87D0 (two passes; read in full).
// EVERY boolean leaf of flag bytes A/B/C uses the TextBool convention (spec 1.3:
// compared to "True"/"False" case-insensitively; anything else - including
// absence - leaves the engine's byte at its PRE-SET default, documented per
// field below, not applied here).
struct ActionNodeAnimationAction { Text action; }; // Animation_action (unknown names dropped at runtime)
struct ActionNodeAnimationState {
    Text animationState; // Animation_State: -> state index (external, unresolved here)
    std::vector<ActionNodeAnimationAction> actions; // Actions/Animation_action* (engine keeps at most 4)
};
struct ActionNodeNpc {
    Text name;             // Name: char[0x1E] -> CRC (row key, §13.1's npc_list cross-ref)
    Text personaSituation; // Persona_situation: -> Wwise event id (external, unresolved here)
    Text enterAnim, exitAnim, startleAnim; // Enter_anim/Exit_anim/Startle_anim: -> animation-name index (external, -1 if unknown)
    std::vector<ActionNodeAnimationState> animations; // Animations/State* (engine keeps at most 4)
    std::vector<Text> cowerAnims; // CowerAnims/CowerAnim* (engine keeps at most 3)
    Always<float> minAnimDelay, maxAnimDelay; // seconds; engine *1000 -> ms

    // flag byte A (§13.3): engine pre-set default is False/0 for all of these.
    TextBool syncedAnimation, singleUse, getBag, reverseEnter, canUseWithBag;
    TextBool fleeOnExit; // read by the engine ONLY when Single_Use is True
    TextBool neverExit;  // Never_Exit (bit 6)
    TextBool canNotExit; // Can_Not_Exit (bit 7); engine cross-bit rule "True also sets bit 6, False clears only bit 7" is NOT modelled here (loader behaviour, not a per-element fact)

    // flag byte B (§13.3): pre-set default False/0 except stationaryNode (True).
    TextBool spawnOnly, standardizeHeight, reactToPeds, requiredNpc, npcDies, drunkNode, canBeBumped;
    TextBool stationaryNode; // engine pre-set default: True

    // flag byte C (§13.3): pre-set default False/0 except supportsNonHeroic (True).
    EnumText spawnPriority; // Spawn_Priority: "Sidewalk (normal)"=0, "Action Node (high)"=1
    TextBool exitOnItemDislodge, spawnInAir, spawnOffNavmesh, equipWeapon, requiresRifle, supportsHeroic;
    TextBool supportsNonHeroic; // engine pre-set default: True

    std::vector<Text> followNodes; // Follow_Nodes/Node* (pass 2; text = another row's Name; engine keeps at most 4)
    Always<float> randomDestinationPercent; // Random_destination_percent (pass 2); engine stores 1 - percent/100
    std::vector<Text> modelList; // Model_List/Model* (character-definition lookups, external; engine keeps at most 4)
    Always<uint32_t> numResourceRequests; // Num_Resource_Requests: u32 (clamped to the model count at runtime)
    EnumText sex;  // Sex: male=0/female=1, case-insensitive; engine default 3 when absent/unmatched (not applied here)
    EnumText team; // Team: Civilian=0, Police=1, Gang=2, Saints=3; -1 if other/absent
};
struct ActionNodeNpcsTable {
    std::vector<ActionNodeNpc> rows;
};
ActionNodeAnimationState ParseActionNodeAnimationState(const Node* row);
ActionNodeNpc ParseActionNodeNpc(const Node* row);
ActionNodeNpcsTable ParseActionNodeNpcsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §13.4 action_nodes.xtbl - the placement file (NO <Table> wrapper)
// ---------------------------------------------------------------------------
// Structural only (spec: "not a gameplay-tuning table"). `object` rows sit
// DIRECTLY under the document root (sr3xtbl::Document::root(), not ::table()).
// Loader 0x008F08A0. See the header banner for what is left out (record layout,
// consumers - explicitly OPEN in the spec).
struct ActionNodeTransform {
    // `transform` is a SINGLE text element of seven whitespace-separated floats
    // (strtok/atof over " \r\n\t", spec §13.4) - unlike every other table's X/Y/Z
    // child convention. Tokenised here with sr3xtbl::ParseFloat (a pure function,
    // still "built only on sr3xtbl's accessors").
    bool present = false;
    std::array<float, 7> values{};
    size_t tokenCount = 0; // number of whitespace-separated tokens actually found (<=7 kept)
};
struct ActionNodePlacementActionNode { // "action_node" child (reader 0x008F02C0)
    Text group; // group: -> action_node_groups.xtbl Name (§13.1)
    Text npc;   // npc: -> action_node_npcs.xtbl Name (§13.3), resolved to a row index at runtime
    ActionNodeTransform transform;
    Always<bool> playerNode; // player_node
};
struct ActionNodePlacementSpawnNode { // "spawn_node" (reader 0x008F0190)
    Text group;
    Text name;
    ActionNodeTransform transform;
};
struct ActionNodePlacementVehicle { // "vehicle"
    Text group;
    Text name; // -> vehicle-table name (external, unresolved here)
    ActionNodeTransform transform;
};
struct ActionNodePlacementObject { // root-level "object" row
    Text name; // name: char[0x18], CRC-hashed
    // Reader flavour not stated by the spec for these three counts; assumed
    // always-write per the general default (see header banner).
    Always<int32_t> numActionNodes, numSpawnNodes, numVehicles;
    std::vector<ActionNodePlacementActionNode> actionNodes;
    std::vector<ActionNodePlacementSpawnNode> spawnNodes;
    std::vector<ActionNodePlacementVehicle> vehicles;
};
struct ActionNodesFile {
    std::vector<ActionNodePlacementObject> objects; // root/object*, no <Table> wrapper (§1.2, §13.4)
};
ActionNodeTransform ParseActionNodeTransform(const Node* node, std::string_view childName);
ActionNodePlacementActionNode ParseActionNodePlacementActionNode(const Node* row);
ActionNodePlacementSpawnNode ParseActionNodePlacementSpawnNode(const Node* row);
ActionNodePlacementVehicle ParseActionNodePlacementVehicle(const Node* row);
ActionNodePlacementObject ParseActionNodePlacementObject(const Node* row);
ActionNodesFile ParseActionNodesFile(const Document& doc);

// ---------------------------------------------------------------------------
// §14 Distant-population tables
// ---------------------------------------------------------------------------

// §14.1 distant_peds.xtbl - loader 0x00B8FAD0 (read in full).
struct DistantPedestrian {
    Text name; // Name: char[0x19], bounded copy
    Text mesh; // Mesh: char[0x41], bounded copy
};
struct DistantPedsTable { std::vector<DistantPedestrian> rows; };
DistantPedestrian ParseDistantPedestrian(const Node* row);
DistantPedsTable ParseDistantPedsTable(const Document& doc);

// §14.2 distant_ped_colors.xtbl - loader 0x00B8FB60 (read in full). All Color_Set
// rows are merged into three global pools at runtime (the set boundary is lost);
// this reader keeps the per-row grouping the XML actually has.
struct DistantPedClothingColorSet {
    Rgb undershirt, overshirt, pants, shoe, accent; // Undershirt_Color, Overshirt_Color, Pants_Color, Shoe_Color, Accent_Color
};
struct DistantPedColorSet {
    std::vector<Rgb> skinColors;      // Skin_Colors/Skin_Color*
    std::vector<Rgb> hairColors;      // Hair_Colors/Hair_Color*
    std::vector<DistantPedClothingColorSet> clothingColors; // Clothing_Colors/Clothing_Color_Set*
};
struct DistantPedColorsTable { std::vector<DistantPedColorSet> colorSets; }; // Table/Color_Set*
DistantPedColorSet ParseDistantPedColorSet(const Node* row);
DistantPedColorsTable ParseDistantPedColorsTable(const Document& doc);

// §14.3 distant_ped_spawn_parameters.xtbl / distant_vehicle_spawn_parameters.xtbl
// - loaders 0x00B8FD40, 0x00B92CE0 (read in full). Table -> one element whose
// children are ALL always-write floats (spec explicit).
struct DistantPedSpawnParameters {
    Always<float> lengthPerSpawn;   // Length_Per_Spawn (peds only); engine stores 1/value
    Always<float> baseSpawningDist, maxSpawningDist, doubleSpawnHeight, despawnExpansionDistance,
                  cameraLookaheadTime;
    Always<float> spawnAngle, despawnAngle; // degrees; engine stores cos(angle * pi/180) (not applied here)
};
struct DistantVehicleSpawnParameters { // no Length_Per_Spawn (vehicles)
    Always<float> baseSpawningDist, maxSpawningDist, doubleSpawnHeight, despawnExpansionDistance,
                  cameraLookaheadTime;
    Always<float> spawnAngle, despawnAngle;
};
std::optional<DistantPedSpawnParameters> ParseDistantPedSpawnParameters(const Document& doc);
std::optional<DistantVehicleSpawnParameters> ParseDistantVehicleSpawnParameters(const Document& doc);

// §14.4 distant_vehicle_traffic_types.xtbl - loader 0x00B93000 (read in full).
// The ONLY place a "traffic type" is defined (§3: traffic_types.xtbl has no loader).
struct DistantVehicleTrafficType {
    Text name;                     // Name -> CRC key
    Always<uint32_t> splineType;   // Spline_Type: u32, always-write; engine stores as a one-hot 1<<n (not applied)
    Always<float> lengthPerSpawn;  // engine stores 1/value (not applied)
    Always<float> noSpawnZone;     // No_Spawn_Zone
    Always<float> dummyCarLength;  // Dummy_Car_Length
};
struct DistantVehicleTrafficTypesTable { std::vector<DistantVehicleTrafficType> rows; };
DistantVehicleTrafficType ParseDistantVehicleTrafficType(const Node* row);
DistantVehicleTrafficTypesTable ParseDistantVehicleTrafficTypesTable(const Document& doc);

// §14.5 distant_vehicle_colors.xtbl - loader 0x00B92C40 (read in full).
struct DistantVehicleColorRow { Rgb color; }; // Color (R,G,B)
struct DistantVehicleColorsTable { std::vector<DistantVehicleColorRow> rows; };
DistantVehicleColorRow ParseDistantVehicleColorRow(const Node* row);
DistantVehicleColorsTable ParseDistantVehicleColorsTable(const Document& doc);

// §14.6 distant_vehicles.xtbl - loader 0x00B93180 (read in full).
struct DistantVehicleRow {
    Text name;        // Name: char[0x19]
    Text mesh;        // Mesh: -> item/mesh registry (external, unresolved here)
    Text trafficType; // Traffic_Type: -> §14.4 Name (CRC match, §23)
};
struct DistantVehiclesTable { std::vector<DistantVehicleRow> rows; };
DistantVehicleRow ParseDistantVehicleRow(const Node* row);
DistantVehiclesTable ParseDistantVehiclesTable(const Document& doc);

// ---------------------------------------------------------------------------
// §15 homies.xtbl (and every <framework>_homies.xtbl - identical schema, §15)
// ---------------------------------------------------------------------------
// Table -> repeated Homie -> Name, Display_Name, Display_Desc, Humans -> repeated
// Human -> Character, Vehicles -> repeated Vehicle_Entry -> Vehicle,
// Vehicle_Variant, Weight, Flags -> repeated Flag, Image_name, Audio,
// Blocked_for_Mission -> repeated Flag, Framework, Is_DLC. Shared row reader
// 0x007E6760 (whole reader read); empirically confirmed against the 3 raw DLC
// files (spec §24.1 - 84 predicate instances, 0 failures). `_Editor` (present in
// the files) is never read - deliberately not modelled.
struct HomieHuman { Text character; }; // Humans/Human/Character (engine keeps at most 8, no bound check)
struct HomieVehicleEntry {
    Text vehicle;        // Vehicle: -> vehicle-table slot (external, unresolved here)
    Text vehicleVariant; // Vehicle_Variant
    Always<int32_t> weight; // Weight (engine keeps at most 2 pairs, no bound check - a 3rd overwrites the count)
};
struct Homie {
    Text name; // Name: char[0x1F]
    std::vector<HomieHuman> humans;
    std::vector<HomieVehicleEntry> vehicles;
    // Flags/Flag* - case-SENSITIVE exact strings (§15), unlike most other tables' flag containers.
    bool flagClearGangNotoriety = false, flagClearPoliceNotoriety = false, flagFormOwnSquad = false,
         flagGiveRandomWeapons = false, flagPlayerSelectsVehicle = false, flagRepairPlayersVehicle = false,
         flagTurnToPedAfterDriveup = false, flagEmergency = false;
    bool flagTaxi = false, flagRequiresSaints = false; // a separate engine byte (+0xF2) from the eight above (+0xF1); same Flags/Flag container in the XML
    // NOTE: "is wheelman" is declared in the DLC editor blocks but never tested by
    // the engine reader (spec §15/§24.1) - deliberately not a field here.
    Text imageName;   // Image_name -> CRC
    Text displayName; // Display_Name: -> localisation key CRC, kept only if the text table recognises it (external, unresolved here)
    Text displayDesc; // Display_Desc
    std::vector<Text> blockedForMission; // Blocked_for_Mission/Flag* (matched vs the 33-name mission array, §15/§23; external, raw text kept)
    Text audio;       // Audio -> Wwise event id (external, unresolved here)
    Text framework;   // Framework (engine default "main" when absent)
    Always<bool> isDlc; // Is_DLC
};
struct HomiesTable { std::vector<Homie> rows; };
HomieHuman ParseHomieHuman(const Node* row);
HomieVehicleEntry ParseHomieVehicleEntry(const Node* row);
Homie ParseHomie(const Node* row);
HomiesTable ParseHomiesTable(const Document& doc);

// ---------------------------------------------------------------------------
// §16 follower_heads.xtbl - follower head-portrait associations
// ---------------------------------------------------------------------------
// Table -> Heads -> Associations -> repeated Follower -> Persona, Charname,
// Bitmap. Loader 0x007F0670 (read in full).
struct FollowerHead {
    Text persona;  // Persona: -> Wwise event id (external, unresolved here); engine value 0 when absent
    Text charname; // Charname: -> CRC-32 (lower-cased); engine value 0 when absent
    Text bitmap;   // Bitmap: engine treats this as effectively REQUIRED (an absent element null-derefs, §16)
};
struct FollowerHeadsTable { std::vector<FollowerHead> rows; };
FollowerHead ParseFollowerHead(const Node* row);
FollowerHeadsTable ParseFollowerHeadsTable(const Document& doc);

// ---------------------------------------------------------------------------
// §17 driver_bailout.xtbl - reward/penalty rules for a driver bailing out
// ---------------------------------------------------------------------------
// Table -> one Driver_Bailout whose leaves are all direct children. Loader
// 0x006A5310 (read in full; every constant quoted from the image).
struct DriverBailout {
    Always<float> maxDistance, minDistance;       // engine: both forced to 0 if Max<=0 or Max<Min (not applied)
    Always<uint32_t> maxDamage, minDamage;        // engine: both forced to 0 if Max==0 or Max<Min (not applied)
    Always<float> maxDamagePercentReward;         // engine: /100, clamped [0,1] (not applied)
    Always<float> minVelocity;                    // engine: clamped >=0, *0.44704 (mph->m/s), squared (not applied)
    Always<float> endTime;                        // seconds; engine: clamped >=0, *1000 -> ms (not applied)
    std::optional<float> recordDisplayTime;       // seconds; if-present; explicit default 0 when absent; engine *1000 -> ms
    std::optional<float> recordQueueTime;         // same
    Always<float> recordThreshold;                // engine: clamped >=0, /100 (not applied)
    Always<bool> showDamageRecord;
    Always<int32_t> maxRespect, maxLifetimeRespect; // engine: clamped >=0 (not applied)
    Always<float> maxCash;                          // engine: clamped >=0 (not applied)
    Always<float> vehicleDamageMultiplier;          // engine's PRE-LOAD image value is 1.0 (not an XML absence-default)
};
std::optional<DriverBailout> ParseDriverBailout(const Document& doc); // nullopt if Driver_Bailout itself is absent

// ---------------------------------------------------------------------------
// §18 vehicle_despawn.xtbl (and vehicle_despawn_mp.xtbl - same schema; which mode
// selects the _mp file is OPEN, spec §18/§25.8)
// ---------------------------------------------------------------------------
// Table -> a SECOND element also named Table -> in_view_despawn_info,
// out_of_view_despawn_info (each holding repeated abandon_despawn_info
// {num_cars, time}), abandon_despawn_delay, player_vehicle_despawn_distance.
// Parser 0x00905C20 (read in full for the fields below). `safety_despawn_time`
// is named in the file's own descriptor but this parser does not read it (§18) -
// deliberately not a field here.
struct VehicleDespawnEntry {
    Always<int32_t> numCars; // num_cars
    Always<int32_t> time;    // time: seconds; engine *1000 -> ms (not applied)
};
struct VehicleDespawnTable {
    std::vector<VehicleDespawnEntry> inViewDespawnInfo;    // in_view_despawn_info/abandon_despawn_info* (engine keeps at most 10)
    std::vector<VehicleDespawnEntry> outOfViewDespawnInfo; // out_of_view_despawn_info/abandon_despawn_info* (engine keeps at most 10)
    Always<int32_t> abandonDespawnDelay;         // read into two engine destinations, both *1000 ms (not applied)
    Always<int32_t> playerVehicleDespawnDistance; // engine stores this value SQUARED (not applied)
};
// nullopt if the inner Table/Table shape is not found (spec §18: the parser
// descends into a CHILD of the outer Table that is itself named Table).
std::optional<VehicleDespawnTable> ParseVehicleDespawnTable(const Document& doc);

// ---------------------------------------------------------------------------
// §19 Escort_constants.xtbl - escort-mission ("Tiger") tuning constants
// ---------------------------------------------------------------------------
// Table -> Escort_Constants -> Tiger_Constants -> Rage{...}, Penalties ->
// Vehicles{...}, Humans{...}, Movers{...}, World{...}. Loader at 0x0067F1D0
// (read in full). Defaults are written first and each group's leaves are
// always-write readers: the documented image defaults (noted per field) survive
// ONLY when the WHOLE group element is absent - a present group missing a leaf
// stores an unspecified value (Always<T>.present=false), exactly as elsewhere.
struct EscortRage {
    bool present = false; // absent -> engine keeps its image defaults (see field comments), not applied here
    Always<float> desiredSpeedMps;      // Desired_speed_MPS; image default 18.0
    Always<float> rageAttackDamageHp;   // Rage_Attack_Damage_HP; image default 250.0
    Always<float> rageIncreaseRate;     // image default 1.0
    Always<float> rageDecreaseRate;     // image default 1.0
};
struct EscortVehiclesPenalty {
    bool present = false;
    Always<uint32_t> vehicleDamagePenaltyMs;  // u32 (spec-tables-traffic-ai.md s19: "Vehicle_Damage_Penalty_MS 0x014BB25C 4000 (u32)", CONFIRMED - disassembly); image default 4000
    Always<float> vehicleDamageThreshold;  // image default 50.0
    Always<float> vehicleDamageCooldownMs; // image default 250
};
struct EscortHumansPenalty {
    bool present = false;
    Always<float> humanDamagePenaltyMs; // image default 2000
    Always<float> humanDamageThreshold; // image default 10.0
};
struct EscortMoversPenalty {
    bool present = false;
    Always<float> moverDamagePenaltyMs;  // image default 500
    Always<float> moverMassThresholdKg;  // image default 75.0
    Always<float> moverDamageCooldownMs; // image default 250
};
struct EscortWorldPenalty {
    bool present = false;
    Always<float> worldDamagePenaltyMs;   // image default 500
    Always<float> worldDamageThresholdHp; // image default 15.0
    Always<float> worldDamageCooldownMs;  // image default 250
};
struct EscortConstants {
    EscortRage rage;
    EscortVehiclesPenalty vehicles;
    EscortHumansPenalty humans;
    EscortMoversPenalty movers;
    EscortWorldPenalty world;
};
// nullopt if Escort_Constants/Tiger_Constants itself is absent.
std::optional<EscortConstants> ParseEscortConstants(const Document& doc);

// ---------------------------------------------------------------------------
// §20 human_transition.xtbl - human animation-state transition graph
// ---------------------------------------------------------------------------
// Table -> repeated Human_Transition (one per state) -> Name, start_state_grid ->
// repeated start_state_elm -> start_state, default_transition_from_stand,
// Transitions -> repeated Transition, Blends -> repeated Blend. Loader 0x009B8630
// (whole function read). Runtime meaning of the state names is OPEN (§20/§25.10).
struct HumanTransitionTransition {
    Text endState;             // End_State: must name a registered state (cross-row; not resolved here)
    Text transitionAnimation;  // Transition_Animation: -> animation-name index (external, unresolved here)
    Always<float> speedMultiplier; // Speed_Multiplier: engine encodes this to a byte (not applied here)
    Always<bool> allowPlayer, allowPlayerGang, allowOtherGang, allowPolice, allowCivilian;
};
struct HumanTransitionBlend {
    Text endState; // End_State
    Always<float> blendTime, oneHBlendTime, twoHBlendTime;
};
struct HumanTransition {
    Text name; // Name: registered as a state (50-slot registry at runtime, not modelled)
    std::vector<Text> startStateGrid; // start_state_grid/start_state_elm/start_state* (text = animation-name index, external)
    Always<bool> defaultTransitionFromStand;
    std::vector<HumanTransitionTransition> transitions; // Transitions/Transition*
    std::vector<HumanTransitionBlend> blends;            // Blends/Blend*
};
struct HumanTransitionTable { std::vector<HumanTransition> rows; };
HumanTransitionTransition ParseHumanTransitionTransition(const Node* row);
HumanTransitionBlend ParseHumanTransitionBlend(const Node* row);
HumanTransition ParseHumanTransition(const Node* row);
HumanTransitionTable ParseHumanTransitionTable(const Document& doc);

// ---------------------------------------------------------------------------
// §21 PEDF_Life.xtbl and Life_default.xtbl - pedestrian "life" animation sets
// ---------------------------------------------------------------------------
// STRUCTURAL, interior partly OPEN (spec: "the exact string built ... and the
// meaning of Skeleton_Set names were not traced [OPEN]"). Both files share the
// same reader (0x008C7BC0) and schema: Table -> repeated Skeleton_Set -> Groups
// -> repeated Group -> States -> repeated State (ID, Animation -> Filename), and
// Actions -> repeated Action (ID, Animation -> Filename). Neither Skeleton_Set
// nor Group has an identifying leaf element in the spec's element tree - none is
// guessed here (see the header banner).
struct LifeAnimationEntry { // State or Action
    Text id;       // ID: -> animation-name index (external, unresolved here)
    Text filename; // Animation/Filename
};
struct LifeGroup {
    std::vector<LifeAnimationEntry> states;  // States/State*
    std::vector<LifeAnimationEntry> actions; // Actions/Action*
};
struct LifeSkeletonSet {
    std::vector<LifeGroup> groups; // Groups/Group*
};
struct LifeTable { std::vector<LifeSkeletonSet> skeletonSets; }; // Table/Skeleton_Set*
LifeAnimationEntry ParseLifeAnimationEntry(const Node* row);
LifeGroup ParseLifeGroup(const Node* row);
LifeSkeletonSet ParseLifeSkeletonSet(const Node* row);
LifeTable ParseLifeTable(const Document& doc); // used for BOTH PEDF_Life.xtbl and Life_default.xtbl (identical schema, §21)

// ---------------------------------------------------------------------------
// §22 node_graph_files.xtbl - the animation-network graph file list (NO <Table>
// wrapper)
// ---------------------------------------------------------------------------
// Reader of slot 14, 0x004CB510: elements taken directly from the document root
// (sr3xtbl::Document::root(), like action_nodes.xtbl) -> repeated
// node_graph_file -> name (text). Each name gets ".xtbl" appended and names
// ANOTHER file (a state_machine/blend_tree graph); that per-graph format belongs
// to the animation-network subsystem and is explicitly out of scope here (§22).
//
// REAL-DATA CORRECTION (found by tools/validation/validate_tables_trafficai_population.cpp
// against the shipped misc_tables.vpp_pc copy, not by re-reading the spec or the
// exe): the root element's only child is a wrapper ALSO named `node_graph_files`
// (`<root><node_graph_files><node_graph_file>...`), one level below
// Document::root() - the rows are children of THAT wrapper, not of root()
// itself. root()'s direct children are just the one wrapper (unlike
// action_nodes.xtbl, whose `object` rows really do sit directly on root(),
// confirmed the same way). "No <Table> wrapper" (spec §1.2/§22) is still true -
// the wrapper here is named after the table, not called `Table` - but it is a
// wrapper, and this reader has to find it by name first.
struct NodeGraphFileEntry { Text name; }; // node_graph_file/name
struct NodeGraphFilesFile { std::vector<NodeGraphFileEntry> files; }; // root/node_graph_files/node_graph_file*, no <Table> wrapper (§1.2, §22)
NodeGraphFileEntry ParseNodeGraphFileEntry(const Node* row);
NodeGraphFilesFile ParseNodeGraphFilesFile(const Document& doc);

} // namespace sr3tables_trafficai
