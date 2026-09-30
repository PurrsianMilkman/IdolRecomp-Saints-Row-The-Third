#pragma once

// sr3tables_progression - typed readers for the twelve smaller "rule
// tables" of spec-tables-progression.md S10: store_discounts.xtbl,
// gameplay_constants.xtbl, gameplay_nags.xtbl + Gameplay_nag_globals.xtbl,
// mission_checkpoints.xtbl, mission_help.xtbl, spawn_info_categories.xtbl,
// spawn_info_groups.xtbl, spawn_info_ranks.xtbl, drunk_levels.xtbl,
// rank_reactions.xtbl, default_global.xtbl, tweak_table.xtbl,
// metered_sprint.xtbl, activity_types.xtbl.
//
// See tables_core.h for the shared reader conventions. All [CONFIRMED -
// disassembly] per spec S10 unless a struct's comment says otherwise; NONE
// of these 14 files has a real-game or DLC sample except the one raw DLC
// tweak_table row (spec 10.10) and the one raw DLC activity_types file
// (spec 10.12) - the rest rest on the loader disassembly alone (spec S12,
// "Not validated (no sample exists)").

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
// store_discounts.xtbl - spec-tables-progression.md S10.1
// ===========================================================================

// spec 10.1 <DiscountElement>. Every field here is "write-only-if-present"
// per the spec's own wording (each with a documented default the real
// loader applies, none of them the sr3xtbl Always-reader's 0/false).
struct StoreDiscountElement {
    std::optional<std::string> discountName; // <DiscountName>: CRC-32 of text; shop_names.xtbl namespace (HIGH CONFIDENCE)
    std::optional<float> amount;              // <Amount>, default 0
    std::optional<std::string> radioEvent;    // <RadioEvent>: CRC-32 of text, default the invalid-id constant
    std::optional<uint32_t> hours;            // <Hours>, default 1
    std::optional<float> triggered;           // <Triggered>, default 0.1
};

struct StoreDiscount {
    std::string name;                             // <Name>: record hash = NameHash(lower(name))
    std::vector<StoreDiscountElement> discounts;   // <Discounts><DiscountElement>, at most 255 (byte count)
};

// Parses one <StoreDiscounts> row (spec 10.1 - row element is literally "StoreDiscounts").
StoreDiscount ParseStoreDiscount(const Node* row);
std::vector<StoreDiscount> ParseStoreDiscountsTable(const Document& doc);

// ===========================================================================
// gameplay_constants.xtbl - spec-tables-progression.md S10.2
//
// One <Gameplay_Constants> element under <Table> holding ~30 named
// sections; every leaf is read by the "always" reader (spec: "an absent
// leaf is unspecified ... treat as 0"). Two documented unit conversions
// (FriendlyFirePercentage /100, Fat_Bones_* x0.1) and one Power_Attack_Angle
// -> half-angle-cosine conversion are NOT applied here - these are the raw
// parsed element values. Colour fields (Object_Glow_Colors) are read by a
// 12-byte reader the spec does not give element names for; modelled here as
// an X/Y/Z-style triple (the only 12-byte/3-float reader sr3xtbl exposes,
// sr3xtbl::ReadVec3Child) - an INFERENCE, flagged, not a spec-given fact.
// ===========================================================================

struct GcFatBones { Always<float> arm, leg; };                                            // <Fat_bones>
struct GcFire {                                                                            // <Fire>
    Always<int32_t> maxPlayerBurnTimeMs, maxNpcBurnTimeMs, maxCorpseBurnTimeMs;
    Always<float> mpFireDamagePerSecond;
    Always<int32_t> mpMaxPlayerBurnTimeMs, mpMaxNpcBurnTimeMs, mpMaxCorpseBurnTimeMs;
};
struct GcDrippingWet {                                                                     // <Dripping_Wet>
    Always<int32_t> maxPlayerDripTimeMs, maxNpcDripTimeMs, maxCorpseDripTimeMs;
    Always<int32_t> mpMaxPlayerDripTimeMs, mpMaxNpcDripTimeMs, mpMaxCorpseDripTimeMs;
};
struct GcStickyFireBurnRadii { Always<float> large, small1, small2; };
struct GcStickyFire {                                                                      // <sticky_fire>
    Always<float> conicSpreadThreshold, conicStretchFactor, circularStretchFactor, conicArc;
    GcStickyFireBurnRadii burnRadii;
};
struct GcPlayerHealth {                                                                    // <player_health>
    Always<float> restorePerSecSp, restorePerSecVampireSp, restoreMaxPctSp;
    Always<float> restorePerSecMp, restorePerSecVehicleMp, restoreMaxPctMp;
    Always<int32_t> restoreWaitTimeMs, restoreWaitTimeMsMp, fullRestoreTimeMs;
};
struct GcColor { Always<float> x, y, z; }; // 12-byte reader; element names INFERRED as X/Y/Z (see banner)
struct GcObjectGlowColors { GcColor weapons, cash, drugs; };                               // <Object_Glow_Colors>
struct GcHoPimpAi { Always<float> slapChance, customerSellChance, goCustomerChance; };      // <Ho_Pimp_AI>
struct GcRagdollDamageFactors {                                                            // <ragdoll_damage_factors>
    Always<float> global, normal, sliding, head, upperBody, body, upperArm, lowerArm, upperLeg, lowerLeg;
    Always<float> damageAgainstMoverMultiplier, damageAgainstMoverMinimumHp;
};
// spec: "n is unchecked - the layout fits exactly n in {1,2,3}".
struct GcMeleeAttackSpeedElement {
    Always<int32_t> numAttackers; // <Num_attackers> (index n)
    Always<int32_t> attackDelayMin, attackDelayMax, swordAttackDelayMin, swordAttackDelayMax;
};
struct GcMeleeAttack {                                                                     // <Melee_attack>
    std::vector<GcMeleeAttackSpeedElement> attackSpeeds; // <Melee_attack_speeds><Melee_attack_speed_element>
    Always<uint32_t> gunMeleeAttackDelay;
    Always<float> finisherCameraChance, wieldablePropPlayerDamageMult;
    Always<int32_t> powerAttackAngleDegrees; // raw degrees; engine stores cos(angle*0.017453*0.5)
};
struct GcMinSpreadMultiplierElement { Always<int32_t> attackerAdvantage; Always<float> minSpreadMultiplier; };
struct GcFirearmAttack {                                                                   // <Firearm_attack>
    std::vector<GcMinSpreadMultiplierElement> minSpreadMultipliers; // room for 4 before dual_wield_multiplier
    Always<float> dualWieldMultiplier, dualWieldMultiplierOnline;
};
struct GcControlPatterns { Always<uint32_t> orthogonalArc; Always<float> centeredThreshold; };
struct GcTauntReactions { Always<float> responseFlee, responseTaunt, responseAttack; };
struct GcVec3 { Always<float> x, y, z; };
struct GcBustOffsets {                                                                     // <Bust_Offsets>
    GcVec3 bustFaceDownLeft, bustFaceDownRight, bustFaceUpLeft, bustFaceUpRight, bustStanding;
};
struct GcHelicopterDraft { Always<float> height, radius, forceX, dislodgeDamageX, pedFleeRadiusStart, pedFleeRadiusStop; };
struct GcHelicopter { GcHelicopterDraft draft; };                                          // <Helicopter><Draft>
struct GcMoneyStorageElement { Always<int32_t> numberOfCribs; Always<int32_t> maxStash; };  // index n
struct GcCribs { std::vector<GcMoneyStorageElement> moneyStorage; };                        // <Cribs>
struct GcCombatAiGun { Always<uint32_t> repositionMin, repositionMax, cantFireRepositionMin, cantFireRepositionMax; };
// spec: the loader reads the element named Spray_Min TWICE (a Spray_Max element is never read).
struct GcCombatAiPepperspray { Always<uint32_t> sprayMin; };
struct GcCombatAiGunfireEvade { Always<float> cowerFleeChance; Always<int32_t> cowerFleeMaxRank; };
struct GcCombatAiBust { Always<float> bustHpPcnt; };
struct GcCombatAi {                                                                        // <Combat_AI>
    GcCombatAiGun gun;
    GcCombatAiPepperspray pepperspray;
    Always<uint32_t> pepperSprayMinUsageDelay, stunGunMinUsageDelay;
    Always<float> backAwayMinDist, backAwayMaxDist, backAwayAbsMinDist;
    GcCombatAiGunfireEvade gunfireEvade;
    GcCombatAiBust bust;
};
struct GcVehicleEvadeAi { Always<float> evadeChance, diveChance, threatChance, threatChanceAfterDive; };
struct GcIdleAiCrimeScene {
    Always<int32_t> getCallersDelay, dontCallDelay, policeResponseDelay, observeInterval, addCopInterval;
    Always<float> radius;
};
struct GcIdleAiDrunkActionType { Always<int32_t> drunkTauntPct, drunkStumblePct, drunkVomitPct, drunkStandPct, drinkMorePct; };
struct GcIdleAiDrunk {
    Always<int32_t> drunkKnockdownChance, drunkActionDelayMin, drunkActionDelayMax;
    GcIdleAiDrunkActionType drunkActionType;
};
struct GcIdleAiSidewalk { Always<int32_t> playerBlockingComplainMs, playerBlockingReactMs; };
struct GcIdleAi { GcIdleAiCrimeScene crimeScene; GcIdleAiDrunk drunk; GcIdleAiSidewalk sidewalk; };
struct GcPepperSpray {                                                                     // <PepperSpray>
    Always<float> sprayMeterReact, sprayMeterMax, playerSprayIncRate, playerSprayDecRate, sprayIncRate, sprayDecRate;
};
struct GcFinisherRateElement { Always<int32_t> numFailures; Always<float> npc, player; }; // Num_Failures must be < 4
struct GcFightClub {                                                                       // <Fight_Club>
    std::vector<GcFinisherRateElement> finisherRates;
    Always<float> healthWeight;
    Always<int32_t> maxImbalance, minImbalance;
};
struct GcCoopMetaGame {                                                                    // <Coop_Meta_Game>
    Always<float> rewardCash;
    Always<int32_t> pointsPerPlayerDeath, pointsPerGangVehicle, pointsPerHomieRevive, pointsPerMissionObjective;
    Always<int32_t> pointsPerHeadshot, pointsPerNutshot, pointsPerMeleeKill, pointsPerExplosionKill, pointsPerQuickKill;
};
struct GcWieldablePropThrow {                                                              // <Wieldable_Prop_Throw>
    Always<float> basketballMultiplierLow, basketballMultiplierHigh, h1hMult, h2hMult, h2hHighMult, h2hLowMult;
};
struct GcWatercraftParams { Always<float> reducedSpeedPct, reducedSpeedPctMin, reducedSpeedPctMax; }; // Min clamped <= Max
struct GcSirenWhoopParams {
    Always<int32_t> whoopMinCount, whoopMaxCount;
    Always<float> honkWhoopChance, ramWhoopChance, hitVehicleWhoopChance, hitPedWhoopChance;
};
struct GcFallDamage { Always<float> minDistance, percentDamagePerMeter, mpPercentDamagePerMeter; };

// spec-tables-progression.md S10.2. Top-level fields plus one member per section.
struct GameplayConstants {
    Always<float> panicFallVelocity;
    Always<float> friendlyFirePercentage;      // raw XML value; engine divides by 100
    Always<int32_t> respawnDelayMs;            // <Death_and_busted>
    GcFatBones fatBones;                       // raw XML values; engine multiplies both by 0.1
    GcFire fire;
    GcDrippingWet drippingWet;
    GcStickyFire stickyFire;
    GcPlayerHealth playerHealth;
    GcObjectGlowColors objectGlowColors;
    GcHoPimpAi hoPimpAi;
    GcRagdollDamageFactors ragdollDamageFactors;
    GcMeleeAttack meleeAttack;
    GcFirearmAttack firearmAttack;
    GcControlPatterns controlPatterns;
    GcTauntReactions tauntReactions;
    GcBustOffsets bustOffsets;
    GcHelicopter helicopter;
    GcCribs cribs;
    GcCombatAi combatAi;
    GcVehicleEvadeAi vehicleEvadeAi;
    GcIdleAi idleAi;
    GcPepperSpray pepperSpray;
    GcFightClub fightClub;
    GcCoopMetaGame coopMetaGame;
    GcWieldablePropThrow wieldablePropThrow;
    GcWatercraftParams watercraftParams;
    GcSirenWhoopParams sirenWhoopParams;
    GcFallDamage fallDamage;
};

// Parses the single <Gameplay_Constants> element of a gameplay_constants.xtbl <Table>.
// Returns nullopt if the element is absent (spec: no leaves are then written).
std::optional<GameplayConstants> ParseGameplayConstants(const Document& doc);

// ===========================================================================
// gameplay_nags.xtbl / Gameplay_nag_globals.xtbl - spec-tables-progression.md S10.3
// ===========================================================================

// The 20 static nag names of spec 10.3 (table at 0x012F47F0; row index =
// position in this array; case-insensitive match; an unrecognised <Name>
// ends the whole load, same rule as stats.xtbl - not reproduced here).
extern const char* const kGameplayNagNames[20];
constexpr size_t kGameplayNagCount = 20;

// spec 10.3 <Nag_Data> of gameplay_nags.xtbl. Both times are the raw XML
// seconds value; the engine stores round(value*1000) ms (spec: not applied here).
struct GameplayNagEntry {
    int nagIndex = -1;              // -1 if <Name> matched none of kGameplayNagNames
    Always<float> incrementSeconds; // <Increment>, raw seconds
    Always<float> nagTimeSeconds;   // <Nag_Time>, raw seconds
};

GameplayNagEntry ParseGameplayNagEntry(const Node* row);
std::vector<GameplayNagEntry> ParseGameplayNagsTable(const Document& doc);

// spec 10.3 Gameplay_nag_globals.xtbl <Nag_Data> (a single row; the file's
// preset is 30s for each, per spec - NOT applied here as a default, this is
// only the parsed element).
struct GameplayNagGlobals {
    Always<int32_t> missionPostponeSeconds;      // <Mission_postpone_s>
    Always<int32_t> nagDisplayPostponeSeconds;   // <Nag_display_postpone_s>
};

// Parses the (single) <Nag_Data> row of Gameplay_nag_globals.xtbl.
std::optional<GameplayNagGlobals> ParseGameplayNagGlobals(const Document& doc);

// ===========================================================================
// mission_checkpoints.xtbl - spec-tables-progression.md S10.4
// ===========================================================================

struct MissionCheckpoint {
    std::optional<std::string> missionName;    // <MissionName>
    std::optional<std::string> checkpointName; // <CheckpointName>
    Always<int32_t> index;                     // <Index>
    Always<bool> debug;                        // <Debug>
};

MissionCheckpoint ParseMissionCheckpoint(const Node* row);
// All <MissionCheckpoints> rows, in file order. Real loader keeps at most
// 166 (spec 10.4) and then qsorts the array - neither is applied here.
std::vector<MissionCheckpoint> ParseMissionCheckpointsTable(const Document& doc);

// ===========================================================================
// mission_help.xtbl - spec-tables-progression.md S10.5
// ===========================================================================

// spec 10.5: every <Text> of every <String> becomes one flat record (the
// <String>/<Texts> grouping carries no extra data the record needs).
// `name` is hashed WITHOUT lower-casing by the real loader (sr3xtbl::NameHashCaseSensitive,
// not the usual NameHash) - kept as raw text here, the caller picks the hash function.
struct MissionHelpText {
    std::optional<std::string> name;      // <Name>
    std::optional<std::string> english;   // <English>, preferred; falls back to <DisplayText> if absent
    Always<float> durationSeconds;        // <Duration>; a negative value is replaced by FLT_MAX at runtime (not applied here)
};

// Every <Text> under every <String> of a mission_help.xtbl <Table>, flattened, in file order.
std::vector<MissionHelpText> ParseMissionHelpTable(const Document& doc);

// ===========================================================================
// spawn_info_categories.xtbl / spawn_info_groups.xtbl / spawn_info_ranks.xtbl
// - spec-tables-progression.md S10.6
// ===========================================================================

// spec 10.6 <Group> under a Category's <Groups> ("only when the matched
// group's kind byte is 7" - a runtime cross-reference this reader cannot
// resolve; the conditional fields below are simply if-present).
struct SpawnCategoryGroup {
    std::optional<std::string> name;               // CRC-32; matched against spawn_info_groups' hash
    std::optional<float> dayChance, nightChance;
    std::optional<int32_t> dayCap, nightCap;
    std::optional<uint32_t> vehicleDayCap, vehicleNightCap;
    std::optional<std::string> itemCarried;         // raw text: "None" | "Luggage" | "Shopping Bags"
    std::optional<uint32_t> carryPercent;
    std::optional<std::string> groupCategory;        // raw text: "General Ped" | "Special Ped" | "Special Vehicle"
};

// spec 10.6 <Category>. The six slot weights are normalised by the real
// loader (spec: "each divided by their sum...") - NOT applied here, these
// are the raw parsed weights.
struct SpawnCategory {
    std::optional<std::string> name;
    std::vector<SpawnCategoryGroup> groups;
    // <Flags><Flag> list:
    bool noSpawnOutsideCategory = false, noEnemyGangSpawning = false, lawSpawningArea = false, lockdownArea = false;
    Always<float> carDay, carNight, pedDay, pedNight;
    std::optional<std::string> lawSpawnGroup;  // CRC-32 -> spawn_info_groups lookup
    Always<int32_t> lawCap, lawDelay;
    Always<float> generalPedSlotDay, generalPedSlotNight, specialPedSlotDay, specialPedSlotNight;
    Always<float> specialVehicleSlotDay, specialVehicleSlotNight;
};

SpawnCategory ParseSpawnCategory(const Node* row);
std::vector<SpawnCategory> ParseSpawnInfoCategoriesTable(const Document& doc);

// spec 10.6 <Group> (spawn_info_groups.xtbl). `splineType` raw text: one of
// "Highway Only"|"Boat"|"Offroad"|"Indoor"|"Baggage"|"Taxi" (-1 if none).
struct SpawnGroupVehicle { std::optional<std::string> name, variant; }; // FUN_00ACB350(node, "Name", "Variant")

struct SpawnGroup {
    std::optional<std::string> name;   // CRC-32
    // <GeneralFlags><Flag> list:
    bool unique = false, vehicleOnly = false, hasDesignatedDriver = false;
    std::optional<std::string> team;    // resolved by the team resolver elsewhere (spec 11); kept as raw text
    std::optional<std::string> splineType;
    std::vector<std::string> characters;  // <Characters><Character> text, hashed by the real loader
    std::optional<uint32_t> spawnDrunkPctDay, spawnDrunkPctNight;
    std::vector<SpawnGroupVehicle> vehicles; // <Vehicles><Vehicle>, at most 132 (spec: the vehicle-table size)
};

SpawnGroup ParseSpawnGroup(const Node* row);
std::vector<SpawnGroup> ParseSpawnInfoGroupsTable(const Document& doc);

// spec 10.6 <Rank> (spawn_info_ranks.xtbl). `type` raw text: one of
// "Civilian"|"Gang"|"Homie"|"Law enforcement"|"Stag"|"National Guard"|"Other"|"Specialist" (-1 if none).
struct WeaponLoadoutEntry {
    Always<uint8_t> chance;   // <chance> (a 0 entry does not advance the real loader's array - not applied here)
    std::optional<std::string> melee, firearm, thrown;
};
struct PerTeamInfo {
    std::optional<std::string> team;                    // <Team>; team index = resolver(text) & 0xFF
    std::vector<WeaponLoadoutEntry> weaponLoadout;        // <weapon_loadout><weapon_loadout_elm>, at most 12
    std::vector<std::string> personalityRotation;         // <personality_rotation><personality>, at most 12
};
struct SpawnRank {
    std::optional<std::string> name;    // CRC-32
    std::optional<std::string> type;     // raw text (see above)
    Always<uint32_t> rankNumeric;
    Always<uint32_t> hitPoints, bleedOutHitPoints, knockdownPoints; // engine clamps each to 0x7FFF (not applied here)
    Always<float> meleeDamageModifier;
    std::optional<float> pctDmgToFlinch; // if-present, else 0
    std::vector<PerTeamInfo> perTeamInfo; // <per_team_info><per_team_elm>; real loader has room for 8 team blocks
};

SpawnRank ParseSpawnRank(const Node* row);
std::vector<SpawnRank> ParseSpawnInfoRanksTable(const Document& doc);

// ===========================================================================
// drunk_levels.xtbl - spec-tables-progression.md S10.7
// ===========================================================================

struct DrunkLevel {
    Always<float> percentDrunk, cameraRotationMult, cameraPitchMult;
    Always<int32_t> randomInputSwitchTime;
    Always<float> randomInputAmountFoot, randomInputAmountVehicle;
    Always<int32_t> controlDelayOnFoot, controlDelayVehicle, ragdollOnImpactTime;
    Always<float> reticleXMaxOffset, reticleYMaxOffset, reticleXSpeed, reticleYSpeed;
    Always<bool> sleepy;
};

// spec 10.7. `drugType` raw text: "drunk" | "weed" | "escort tiger"; other
// values "skip the row" at the real loader (kept here, not filtered).
struct DrunkLevelsEntry {
    std::optional<std::string> drugType;
    Always<float> maxBoozePoints, maxTimeDrunk;
    std::vector<DrunkLevel> levels; // <Levels><Level>, at most 5 kept by the real loader
};

DrunkLevelsEntry ParseDrunkLevelsEntry(const Node* row);
std::vector<DrunkLevelsEntry> ParseDrunkLevelsTable(const Document& doc);

// ===========================================================================
// rank_reactions.xtbl - spec-tables-progression.md S10.8
// ===========================================================================

// spec 10.8: values are read as int (percent) and stored by the engine as
// float*0.01 - the conversion is NOT applied here, these are the raw ints.
struct ReactionPercentages {
    std::optional<std::string> playerRank; // case-SENSITIVE match against 4 names ("Rank 1 - Thug" ...)
    Always<int32_t> ignore, avoid, compliment, flipOff, gangSign, sayAmbient, threaten, observe;
};

// spec 10.8. `name` raw text, case-SENSITIVE match against 5 names
// ("Civilian", "Civilian Saints Hated", "Gang, Enemy", "Gang, Friendly", "Police").
struct NpcGroup {
    std::optional<std::string> name;
    std::vector<ReactionPercentages> reactions; // <Reactions><ReactionPercentages>
};

NpcGroup ParseNpcGroup(const Node* row);
std::vector<NpcGroup> ParseRankReactionsTable(const Document& doc);

// ===========================================================================
// default_global.xtbl - spec-tables-progression.md S10.9
//
// Opened WITHOUT the usual <Table> wrapper (spec: "the document root is
// used directly"); ParseDefaultGlobal therefore takes the root, not a
// <Table> row.
// ===========================================================================

struct DefaultGlobal {
    // Both defaults below are the literal strings the spec quotes from the executable
    // (a real, specified default - unlike the Always<T> "unspecified" caveat).
    std::string skyboxMeshFilename = "rfg_skybox"; // <skybox_mesh_filename>, if-present else this default
    std::string cloudMeshFilename = "skybox_clouds"; // <cloud_mesh_filename>, if-present else this default
    std::vector<std::string> orbitalMapNames; // <orbitals><orbital><map_name>, kept only if non-empty, at most 15
};

// Parses a default_global.xtbl document's root element directly (spec 10.9: opened with the "plain open" helper).
DefaultGlobal ParseDefaultGlobal(const Document& doc);

// ===========================================================================
// tweak_table.xtbl - spec-tables-progression.md S10.10
// ===========================================================================

struct TweakTableEntry {
    std::string name;                       // <Name>, <= 255 chars; looked up (case-insensitive) in the 586-name registry (spec 10.10)
    Always<float> value;                     // <Value>, always-write
    std::optional<std::string> description;  // <Description>: authoring-only, "NOT read into the game" (spec) - kept for schema completeness
    std::optional<std::string> framework;    // <Framework>, default "main"
};

TweakTableEntry ParseTweakTableEntry(const Node* row);
std::vector<TweakTableEntry> ParseTweakTable(const Document& doc);

// ===========================================================================
// metered_sprint.xtbl - spec-tables-progression.md S10.11
// ===========================================================================

struct SprintEntry {
    std::optional<std::string> name;   // <Name>; the boot path matches case-SENSITIVELY against "Single Player"
    Always<int32_t> useTime, rechargeTime, delayTime;
    Always<float> pantPercentage;      // <PantPercentage>; engine: threshold_ms = round(RechargeTime * PantPercentage)
    Always<float> jumpPenalty, startPenalty;
};

SprintEntry ParseSprintEntry(const Node* row);
std::vector<SprintEntry> ParseMeteredSprintTable(const Document& doc);

// ===========================================================================
// activity_types.xtbl - spec-tables-progression.md S10.12
// ===========================================================================

// <Activity_type_flags><Flag> list (spec table, bytes +0x41/+0x42). Bit 3 of
// +0x42 ("set when the loading framework is not main") is DERIVED at load
// time, not a Flag string in the XML, and is intentionally not a member here.
struct ActivityTypeFlags {
    bool statsDoesntCount = false, keepScreenFaded = false, autoAdvanceLevel = false, removeNoterietySpawns = false;
    bool resetNoterietyEachLevel = false, usesButtonMashingInterface = false, failOnDeath = false, finisherOnlyDeath = false;
    bool noVehicleEject = false, doInitialWarp = false, racing = false;
};

// <Disable_flags><Flag> list, optional (spec table, bytes +0x43/+0x44; bit 2
// of +0x43 is unnamed in the spec and is not modelled).
struct ActivityDisableFlags {
    bool turnOffSpawning = false, disableDistantSpawns = false, disableParkingSpawns = false, disableAllStores = false;
    bool disableCrib = false, disableHud = false, allowCopsToShootFromVehicle = false;
    bool disableHelicopters = false, disableAttackHelis = false, disablePlayerSwapCheats = false;
    bool disableWarpTriggers = false, disableRoadblocks = false;
};

// spec-tables-progression.md S10.12. `name` must be a registered activity
// type (the 19 names are filled at run time, not recoverable from the
// loader - spec: unmatched writes before the array, unchecked).
struct Activity {
    std::string name;
    std::optional<std::string> framework;             // <Framework>, default "main"
    std::vector<std::string> unlockableNames;           // <Unlockables><Unlockable>, kept only if it exists in unlockables.xtbl (not checked here)
    // <Completion_Image><Filename>; extension IS stripped here (spec states this transform plainly); empty
    // string when absent (spec: "empty string constant if absent" - a real, specified default).
    std::string completionImageName;
    ActivityTypeFlags typeFlags;                        // <Activity_type_flags>
    std::optional<ActivityDisableFlags> disableFlags;    // <Disable_flags>, optional; processed only if present
    std::optional<std::string> soundbankName;            // <Soundbank_Name> -> audio_banks.xtbl NewEntity.Name
    // <Type>, <Additional_Resources>, <_Editor> are present in DLC rows but explicitly NOT read by the loader
    // (spec 10.12) and are intentionally not members here.
};

Activity ParseActivity(const Node* row);
std::vector<Activity> ParseActivityTypesTable(const Document& doc);

} // namespace sr3tables_progression
