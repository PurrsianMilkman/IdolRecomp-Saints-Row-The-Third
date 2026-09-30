#pragma once

// sr3tables_progression - typed readers for respect_levels.xtbl,
// notoriety.xtbl, notoriety_levels.xtbl, notoriety_spawn.xtbl,
// difficulty_levels.xtbl, cheats.xtbl and collectibles.xtbl.
//
// Source: spec-tables-progression.md sections 5-9 (cited per struct below).
// See tables_core.h for the shared reader conventions (Always<T> vs
// std::optional<T>, localisation keys, name-hash references) - they apply
// identically here.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_progression {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// respect_levels.xtbl - spec-tables-progression.md S5
// ===========================================================================

// spec 5.2. `unlockables` are Unlockable NAMEs (spec 4); a name that doesn't
// resolve among the already-loaded Unlockable records is dropped by the real
// loader (spec 5.2: "the id is kept only if the record exists") - NOT
// reproduced here (see the population harness). The real loader also caps
// the per-level list to 20 ids (HIGH CONFIDENCE, spec 5.2) - not enforced here.
struct RespectLevel {
    std::optional<std::string> rank;      // <Rank>, localisation key
    Always<int32_t> respect;              // <Respect>, "signed int, always-write"
    std::vector<std::string> unlockables; // <Unlockables><Unlockable> text, repeated
};

// Parses one <respect_level> row (spec 5.2).
RespectLevel ParseRespectLevel(const Node* row);

// All <respect_level> rows under <respect_levels><levels>, in file order.
// Real loader stops STORING at 50 while continuing to iterate (spec 5.1) -
// NOT reproduced here; every row present is returned (the population
// harness measures the real row count against 50).
std::vector<RespectLevel> ParseRespectLevelsTable(const Document& doc);

// ===========================================================================
// notoriety.xtbl - spec-tables-progression.md S6.1
// ===========================================================================

// The 30 static activity names of spec 6.1 (table at 0x012ED0B8, matched by
// stricmp; row index = position in this array). A <Name> outside this list
// "writes nothing" (spec 6.1) - such rows are simply not returned by
// ParseNotorietyTable.
extern const char* const kNotorietyActivityNames[30];
constexpr size_t kNotorietyActivityCount = 30;

// spec 6.1 <Gang> / <Police> sub-block. `checkDetection`'s ONLY accepted
// spelling is the literal text "true" (case-insensitive) - unlike
// sr3xtbl::ParseBool, "yes" does NOT count here (spec 6.1: "the *only*
// accepted spelling"), so this is a dedicated field, not sr3xtbl::GetBool.
struct NotorietyTrack {
    Always<int32_t> points;
    Always<int32_t> minLevel;
    Always<int32_t> maxLevel;
    bool checkDetection = false; // <Check_Detection> text == "true" (case-insensitive), else false
};

// spec-tables-progression.md S6.1. One <Entry> row, keyed by its matched
// activity index (0..29 into kNotorietyActivityNames).
struct NotorietyEntry {
    int activityIndex = -1; // -1 if <Name> matched none of the 30 (row ignored by the real loader)
    Always<int32_t> delay;  // <Delay>, shared by both sub-blocks
    NotorietyTrack gang;
    NotorietyTrack police;
};

// Parses one <Entry> row (spec 6.1). activityIndex is -1 when <Name> matches
// none of kNotorietyActivityNames (case-insensitive) - the caller decides
// whether to keep such a row (the real loader drops it).
NotorietyEntry ParseNotorietyEntry(const Node* row);

// All <Entry> rows of a notoriety.xtbl <Table>, in file order (including
// unmatched ones, activityIndex == -1, for population measurement).
std::vector<NotorietyEntry> ParseNotorietyTable(const Document& doc);

// ===========================================================================
// notoriety_levels.xtbl - spec-tables-progression.md S6.2
// ===========================================================================

// spec 6.2 <NotorietyLevelElement>. All targets are read by the "always"
// int reader (spec: "All numbers ... read with the always reader"). The
// three ms fields are the RAW XML seconds value; the engine additionally
// multiplies by 1000 (spec 6.2 table) - not applied here, this is the
// element's own parsed value.
struct NotorietyLevelElement {
    Always<int32_t> notorietyLevel;         // <NotorietyLevel> (index within its set; used unchecked)
    Always<int32_t> notorietyLevelLimit;     // <NotorietyLevelLimit> (engine stores only when level > 0)
    Always<int32_t> notorietyDecayRate;      // <NotorietyDecayRate>
    Always<int32_t> notorietyFirstDecay;     // <NotorietyFirstDecay> (raw seconds; engine: x1000 -> ms)
    Always<int32_t> roadblockSpawnTimeMin;   // <RoadblockSpawnTimeMin> (raw seconds; engine: x1000 -> ms)
    Always<int32_t> roadblockSpawnTimeMax;   // <RoadblockSpawnTimeMax> (raw seconds; engine: x1000 -> ms)
    Always<int32_t> bruteSpawnTimeMin;       // <BruteSpawnTimeMin> (raw seconds; engine: x1000 -> ms)
    Always<int32_t> bruteSpawnTimeMax;       // <BruteSpawnTimeMax> (raw seconds; engine: x1000 -> ms)
};

// spec 6.2: one <NotorietyLevels><NotorietyLevelsData> block (a "set"; the
// file holds exactly two, first = set A, second = set B - spec: "which set
// is police and which is gang is positional ... OPEN").
struct NotorietyLevelsSet {
    std::vector<NotorietyLevelElement> levels; // <NotorietyLevelElement>, in file order
};

// Parses one <NotorietyLevels> element (spec 6.2).
NotorietyLevelsSet ParseNotorietyLevelsSet(const Node* setNode);

// spec-tables-progression.md S6.2: the file's (at most two) top-level
// <NotorietyLevels> elements, first -> setA, second -> setB.
struct NotorietyLevelsTable {
    std::optional<NotorietyLevelsSet> setA;
    std::optional<NotorietyLevelsSet> setB;
};

NotorietyLevelsTable ParseNotorietyLevelsTable(const Document& doc);

// ===========================================================================
// notoriety_spawn.xtbl - spec-tables-progression.md S6.3
//
// HIGH CONFIDENCE / no real or DLC sample exists for this table (spec 6.4:
// "no DLC or save sample exists"); the row shape ("each spawn definition is
// a child element literally called `Table`") is itself HIGH CONFIDENCE, not
// CONFIRMED. This reader follows the spec's element tree as given but
// carries the same confidence level - flagged here rather than silently
// assumed solid.
// ===========================================================================

// The 24 static group names of spec 6.3 (table at 0x013080F8, row index =
// position in this array; case-SENSITIVE compare per spec 6.3).
extern const char* const kNotorietySpawnGroupNames[24];
constexpr size_t kNotorietySpawnGroupCount = 24;

// spec 6.3 <level_info> (times are the raw XML seconds value; engine: x1000 -> ms, spec table).
struct SpawnLevelInfo {
    Always<int32_t> level;
    Always<int32_t> minSpawnTime;      // raw seconds; engine x1000 -> ms
    Always<int32_t> maxSpawnTime;      // raw seconds; engine x1000 -> ms
    Always<int32_t> vehMinSpawnTime;   // raw seconds; engine x1000 -> ms
    Always<int32_t> vehMaxSpawnTime;   // raw seconds; engine x1000 -> ms
    Always<int32_t> maxVehicleOccupants;
    Always<int32_t> maxVehicles;
    Always<int32_t> npcCap;
    Always<int32_t> specialistCap;
    Always<int32_t> bruteCap;
};

// spec 6.3 <group_info>; consecutive rows sharing <level/> form that level's group list.
struct SpawnGroupInfo {
    Always<int32_t> level;
    Always<float> chance;
    std::optional<std::string> tagName;      // -> matches a group_details tag_name
    std::optional<std::string> vehicleName;  // -> vehicles.xtbl Vehicle.Name
    std::optional<std::string> variantName;
};

// spec 6.3 <group_details>; consecutive rows sharing <tag_name/> form one group.
// `seatName` raw text: "Default | Driver | Passenger 1 .. Passenger 7" -> -1, 0, 1..7 (spec table); kept as text.
struct SpawnGroupDetail {
    std::optional<std::string> tagName;
    std::optional<std::string> seatName;     // raw text (see above); numeric decode left to the caller
    std::optional<std::string> npcName;
    bool meleeBruteSeat = false, weaponsBruteSeat = false, rollerbladersSeat = false, outsideSeat = false; // bools -> 4 flag bits
};

// spec-tables-progression.md S6.3. One row (a child literally named `Table`, per spec's HIGH CONFIDENCE reading).
struct NotorietySpawnRow {
    std::string name; // <Name>, matched case-sensitively against kNotorietySpawnGroupNames by the real loader
    std::vector<SpawnLevelInfo> levelInfos;
    std::vector<SpawnGroupInfo> groupInfos;
    std::vector<SpawnGroupDetail> groupDetails;
    std::optional<std::string> overrideGroup; // <override_group>, optional: one of the 24 names
    // <spawn_flags><Flag> list (spec 6.3): the 7 named flags.
    bool spawnOnLand = false, spawnOnWater = false, spawnVsBikesOnly = false, spawnIndoorsOnly = false;
    bool attackHelicopter = false, forceNoRam = false, activeWithStag = false;
};

// Parses one row (a <Table> child of the outer <Table>, per spec 6.3's HIGH CONFIDENCE shape).
NotorietySpawnRow ParseNotorietySpawnRow(const Node* row);

// All row-shaped children literally named "Table" under the outer <Table> (spec 6.3).
std::vector<NotorietySpawnRow> ParseNotorietySpawnTable(const Document& doc);

// ===========================================================================
// difficulty_levels.xtbl - spec-tables-progression.md S7
// ===========================================================================

// spec 7.2. All nine multiplier/scalar fields plus the delay are the
// "always" float reader (absent -> treat as 0.0 per the S1.3 caveat). The
// engine stores Notoriety_Decay_Mult's RECIPROCAL and Player_Ram_Delay as
// round(value*1000) ms (spec 7.2 table) - neither conversion is applied
// here; these are the element's own parsed values.
struct DifficultyLevel {
    Always<float> damageReceivedMult;
    Always<float> healthRegenWaitMult;
    Always<float> healthRegenSpeedMult;
    Always<float> damageDealtMult;
    Always<float> notorietyDecayMult;           // raw; engine stores 1.0 / value
    Always<float> homieReviveTimerMult;
    Always<float> vehicleDamageReceivedMult;
    Always<float> bruteHealthScalar;
    Always<float> friendlyHealthScalar;
    Always<float> playerRamDelaySeconds;        // raw seconds; engine: round(x*1000) -> ms
};

// spec 7.2 <Rank_Occurrence_Element>. `missionName` is "written only if the
// element has text" (if-present); the four RankN floats are pre-set to 0.25
// per rank before parsing, but read by the ALWAYS float reader, so "an
// absent RankN is not guaranteed to keep the 0.25 preset" (spec 7.2) -
// modelled as Always<float>, not a 0.25-defaulted optional.
struct RankOccurrenceElement {
    std::optional<std::string> missionName;  // <Mission_Name>, <= 31 chars
    Always<float> rank1, rank2, rank3, rank4;
};

// spec-tables-progression.md S7.2: one <Dynamic_Difficulty> element (a
// "set"; the file holds at most two - first = set A, second = set B).
struct DynamicDifficultySet {
    std::vector<DifficultyLevel> levels;              // <Difficulty_Level>, at most 3 kept by the real loader
    std::vector<RankOccurrenceElement> rankOccurrences; // <Rank_Occurrence_Element>, at most 4 kept
};

DynamicDifficultySet ParseDynamicDifficultySet(const Node* setNode);

struct DifficultyLevelsTable {
    std::optional<DynamicDifficultySet> setA; // first <Dynamic_Difficulty>
    std::optional<DynamicDifficultySet> setB; // second <Dynamic_Difficulty>
};

DifficultyLevelsTable ParseDifficultyLevelsTable(const Document& doc);

// ===========================================================================
// cheats.xtbl - spec-tables-progression.md S8
// ===========================================================================

// spec 8.2's 4 AI actions and 27 Gameplay_Physics actions (the tree diagram's
// own "(25 names, below)" undercounts the prose list actually given - the 27
// below is a verbatim transcription of that prose list), for population
// cross-checks (any <Action> text not in the matching list is a silent
// no-op cheat per spec 8.2 - worth surfacing, not worth rejecting here).
extern const char* const kCheatAiActions[4];
extern const char* const kCheatGameplayPhysicsActions[27];

struct CheatTimeAction {
    std::optional<int32_t> setHour;  // <Set_Hour><Hour>
    bool stopTime = false;           // <Stop_Time/> present
    bool todCycle = false;           // <TOD_Cycle/> present
};
struct CheatVehiclesAction {
    bool hasDrop = false;
    std::optional<std::string> dropVehicleName;    // <Drop><Vehicle_Name>
    std::optional<std::string> dropVehicleVariant;  // <Drop><Vehicle_Variant>
    bool infiniteMass = false, repairVehicle = false, playerVehicleNoDamage = false, playerVehicleSmash = false;
};
struct CheatWeaponAction {
    bool hasGive = false;
    std::optional<std::string> giveWeapon; // <Give><Weapon>
    std::optional<int32_t> giveAmmo;       // <Give><Ammo>
    bool infiniteAmmo = false, allWeapons = false;
};
struct CheatWeatherAction {
    bool clear = false;                         // <Clear/> present
    std::optional<std::string> setToConditions; // <Set_To><Conditions>
    bool wrathOfGod = false;                    // <Wrath_of_God/> present
};

// spec-tables-progression.md S8.2. <Type> tested in this order: AI,
// Gameplay_Physics, Time, Vehicles, Weapon, Weather (first present wins);
// at most one of the type-specific members below is populated.
struct Cheat {
    std::string name;                              // <Name>, <= 32 chars
    std::optional<std::string> unlockString;        // <UnlockString>: the typed code; save id = NameHash(lower(text))
    std::optional<std::string> displayName;         // <DisplayName>, <= 31 chars, copied VERBATIM (not a loc key)
    std::optional<std::string> cheatDescription;    // <Cheat_Description>, <= 31 chars
    std::optional<std::string> interfaceCategory;    // <Interface_Category>: resolved against a RUNTIME table
                                                      // that is zero at load time (spec 8.3: "until they are filled
                                                      // every row gets -1") - kept as raw text, index not modelled
    Always<bool> dontFlagAsCheating;                 // <Dont_Flag_As_Cheating>, always-write
    Always<bool> isDlc;                              // <Is_DLC>, always-write
    std::string typeName;                            // matched <Type> child's element name; empty if none matched
    std::optional<std::string> aiAction;              // AI <Action> text (one of kCheatAiActions, or unrecognised)
    std::optional<std::string> gameplayPhysicsAction; // Gameplay_Physics <Action> text
    std::optional<CheatTimeAction> timeAction;
    std::optional<CheatVehiclesAction> vehiclesAction;
    std::optional<CheatWeaponAction> weaponAction;
    std::optional<CheatWeatherAction> weatherAction;
};

// Parses one <Cheats> row (spec 8.2 - "the row element is literally 'Cheats'").
Cheat ParseCheat(const Node* row);

// All <Cheats> rows of a cheats.xtbl <Table>, in file order. The real loader
// caps at 200 (spec 8.1) - not enforced here.
std::vector<Cheat> ParseCheatsTable(const Document& doc);

// ===========================================================================
// collectibles.xtbl - spec-tables-progression.md S9
// ===========================================================================

// The 4 static collectible names of spec 9.1 (table at 0x01125FD0; row must
// equal one of these case-insensitively, else the row is skipped - spec 9.2).
extern const char* const kCollectibleNames[4];

// spec 9.2 <Threshold_Reward>. The real loader drops a record "unless
// Threshold > 0" (spec 9.2) - NOT applied here; every parsed
// <Threshold_Reward> (up to the real loader's cap of 6) is returned.
struct ThresholdReward {
    Always<int32_t> threshold;              // <Threshold> (real loader keeps the record only if > 0)
    Always<int32_t> respectAward;           // <Respect_Award>
    Always<float> cashAward;                // <Cash_Award>
    std::optional<std::string> unlockable;  // <Unlockable>: an Unlockable.Name, stored as CRC-32 (no existence check)
};

// spec-tables-progression.md S9.2.
struct Collectible {
    std::string name;                        // <Name>: must equal one of kCollectibleNames (case-insensitive)
    std::optional<std::string> titleMessage;    // <Title_Message>, localisation key, if-present
    std::optional<std::string> subtitleMessage; // <Subtitle_Message>, localisation key, if-present
    std::optional<int32_t> respectAward;      // <Respect_Award>, if-present, preset 0
    std::optional<float> cashAward;           // <Cash_Award>, if-present, preset 0
    std::vector<ThresholdReward> thresholdRewards; // <Threshold_Rewards><Threshold_Reward>, at most 6 kept by the real loader
};

// Parses one <collectible> row (spec 9.2 - "row element is lower-case").
// The caller should check `name` against kCollectibleNames; rows that don't
// match are still returned here (skipped by the real loader - spec 9.2).
Collectible ParseCollectible(const Node* row);

// All <collectible> rows of a collectibles.xtbl <Table>, in file order.
std::vector<Collectible> ParseCollectiblesTable(const Document& doc);

} // namespace sr3tables_progression
