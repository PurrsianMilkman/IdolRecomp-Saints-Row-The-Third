#pragma once

// sr3tables_progression - typed readers for the "core" progression tables:
// stats.xtbl, achievements.xtbl, unlockables.xtbl (+ patch_unlockables.xtbl
// and the per-DLC *_unlockables.xtbl, which share unlockables.xtbl's schema).
//
// Source: spec-tables-progression.md sections 2, 3, 4 (cited per struct
// below). Built ONLY on include/sr3xtbl/xtbl.h's accessors - nothing here
// touches the parser or the game executable (cleanroom boundary, see the
// project's HARD RULES). Offsets quoted in comments are the spec's evidence
// citations (where a fact came from in the loader disassembly), not memory
// layout this reader reproduces: these are ordinary typed C++ structs, not
// packed records.
//
// CONVENTION (matches sr3xtbl::Always<T> / std::optional<T>):
//   * an element the spec calls "write-only-if-present" (the engine leaves
//     the destination untouched when absent)              -> std::optional<T>
//   * an element the spec calls "always written" / "always-write reader"
//     (the engine parses even when absent, from a caveat-laden scratch
//     buffer per xtbl.h S2 / spec-tables-progression.md 1.3's caveat)
//                                                            -> sr3xtbl::Always<T>
//   * a field the spec marks OPEN/UNKNOWN/unspecced (e.g. stats.xtbl's
//     per-id Value_Type assignment, achievements.xtbl's base row count,
//     unlockables.xtbl's unread DLC-gate/state bytes) is simply not a member
//     of these structs - nothing is invented for it.
//
// Localisation keys (DisplayName, Description, ...): the engine resolves
// these through a string table this project does not have (spec 1.3); these
// structs keep the raw key TEXT (std::optional<std::string>), not a handle.
//
// Name-hash cross-references (Priority, shop names, ...): kept as raw text;
// sr3xtbl::NameHash(text) reproduces the record id a caller needs (spec 1.3,
// verified in tools/validation/validate_xtbl_population.cpp G6).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_progression {

using sr3xtbl::Always;
using sr3xtbl::Node;
using sr3xtbl::Document;

// ===========================================================================
// stats.xtbl - spec-tables-progression.md S2
// ===========================================================================

// NOTE on the 217 static stat names (spec 2.1, table at 0x01141BD0): the
// spec text CITES this table (count, a handful of example names, and the
// external artifact tools/ai_stat_static_table.tsv) but does not
// TRANSCRIBE all 217 name strings inline. Per the project's cleanroom rule
// (implement ONLY from the spec text), this reader does not hardcode that
// list - fabricating 217 names not actually given by the spec would be
// worse than leaving the gate out. ParseStat() below reads whatever <Name>
// text is present without validating it against the 217-name enumeration;
// a population harness comparing real <Name> values against the true list
// needs that list supplied by Team A first.

// spec 2.4: the Value_Type child selects a handler/class pair; `complex`
// additionally depends on the stat's id (spec 2.4's 44-entry table) which
// this reader does not resolve (that needs the id, supplied by the caller -
// see StatValueClass below). "none present" -> None (handler 0x33, class 6).
enum class StatValueKind {
    None,      // no Value_Type child matched (spec 2.2 row 3: "none present")
    Integer,
    Float,
    Distance,
    Time,
    Money,
    Boolean,
    Percent,
    Complex,   // <complex/> present; per-id handler/class resolution is spec 2.4's table, not reproduced here
};

// spec-tables-progression.md S2.2.
struct Stat {
    std::string name;                              // <Name>, required (spec: "unknown name aborts the whole load")
    std::optional<std::string> displayName;         // <DisplayName>, localisation key
    StatValueKind valueKind = StatValueKind::None;  // <Value_Type> child test order (spec 2.2 row 3)
    // Required when valueKind == Percent (spec: "a missing element dereferences NULL" - HIGH CONFIDENCE, not
    // exercised); case-SENSITIVE cross-reference to an earlier-loaded Stat.Name (spec 2.2, 11 "cross-table" row).
    std::optional<std::string> percentageOfStat;
    // "text compared to true (case-insensitive)"; spec's own bool-always semantics (false if absent) - this is
    // the DETERMINISTIC bool-always reader (xtbl.h S3: "always (false if absent)"), not the int/float caveat.
    Always<bool> allowUpdateByServer;
    std::optional<int32_t> livePropertyId;    // "signed int, write-only-if-present", default 0 (spec 2.2 row 7)
    std::optional<int32_t> liveLeaderboardId; // same, spec 2.2 row 8
};

// Parses one <Stat> row (spec 2.2). Does not enforce the 217-name gate or
// the "unknown name ends the load" rule (spec 2.1/2.2) - that is a real-data
// population concern (see tools/validation/validate_tables_progression_population.cpp),
// not a per-row parse; the caller sees whatever `Name` text is present.
Stat ParseStat(const Node* row);

// All <Stat> rows of a stats.xtbl <Table>, in file order. Does not apply the
// "abort on the first unrecognised Name" rule of spec 2.1/2.2: a real
// population harness needs to see every row to measure how often that rule
// would fire, so this returns everything the file contains.
std::vector<Stat> ParseStatsTable(const Document& doc);

// ===========================================================================
// achievements.xtbl - spec-tables-progression.md S3
// ===========================================================================

// spec 3.2/3.3: Condition decodes to 0 ("at least") / 1 ("at most"); any
// other text "leaves the field unset" - modelled as nullopt.
enum class RequirementCondition { AtLeast, AtMost };

// spec 3.2 <Requirement>. `stat` is the case-insensitive cross-reference to
// stats.xtbl Stat.Name (spec 11 table); an unknown name silently drops the
// requirement at the real loader (not reproduced - see the row parser doc).
// `Value` is read as an int if the referenced stat's class is 2, a float if
// 3 (spec 3.3) - this reader cannot resolve that without the stats table, so
// both parses are offered; the caller picks based on the resolved stat.
struct AchievementRequirement {
    std::optional<std::string> stat;                    // <Stat> text
    std::optional<RequirementCondition> condition;       // <Condition> text, decoded
    std::optional<int32_t> valueAsInt;                   // <Value> parsed as int (if present)
    std::optional<float> valueAsFloat;                   // <Value> parsed as float (if present)
};

// spec-tables-progression.md S3.2.
struct Achievement {
    std::string name;                                    // <Name>, the identifying key
    std::optional<std::string> displayName;               // <DisplayName>, localisation key
    std::optional<std::string> image;                     // <Image>, optional heap copy
    std::vector<AchievementRequirement> requirements;      // <Requirements><Requirement>...
    Always<int32_t> hudUpdateFrequency;                    // "always written" (spec: 0 = no HUD update)
    Always<bool> hudUpdateNumSuppress;                     // "always written"
    bool avatarAwardPresent = false;                       // <Avatar_award>: presence-tested only (spec 3.2/3.3:
                                                            // "the element's text is never used" - not implemented on this build)
    std::optional<std::string> framework;                  // <Framework>, default "main" (spec 1.4)
    // <PC_Only> is explicitly documented as present-but-never-read (spec 3.2/3.5: "an authoring-only flag on
    // this build") and is intentionally NOT a member here.
};

// Parses one <Achievement> row (spec 3.2).
Achievement ParseAchievement(const Node* row);

// All <Achievement> rows of an achievements.xtbl <Table>, in file order.
std::vector<Achievement> ParseAchievementsTable(const Document& doc);

// ===========================================================================
// unlockables.xtbl / patch_unlockables.xtbl / *_unlockables.xtbl - spec S4
// ===========================================================================

// spec 4.2 (10 names, case-insensitive; unmatched/absent -> index 0, spec:
// "default 0 if unmatched").
enum class UnlockableCategory {
    PlayerAbilities = 0, Health = 1, Damage = 2, Weapons = 3, Vehicles = 4,
    Homies = 5, Discounts = 6, Customization = 7, Strongholds = 8, Activities = 9,
};
// spec 4.2 (absent = 0).
enum class UnlockableAutoUnlock { None = 0, Silently = 1, Loudly = 2, LoudlyHelipad = 3, SilentlyLast = 4 };
// spec 4.2 (absent = 0).
enum class UnlockableIsDlc { False = 0, True = 1, Sometimes = 2 };

// spec 4.4 table #0 `Vehicle`: up to 5 <Vehicle> entries under <Vehicles>.
struct UnlockableVehicleEntry {
    std::string vehicleName;                 // <Type>: a vehicles.xtbl Vehicle.Name (spec 11)
    std::optional<std::string> variant;      // <Variant>, optional, pooled
};
// spec 4.4 table #4 `Discount`.
struct UnlockableDiscountPayload {
    std::vector<std::string> shopNames;      // <name>, <name_2> .. <name_8> (up to 8); shop_names.xtbl Shop_Names.Name
    std::optional<float> amount;             // <Amount>
};
// spec 4.4 table #6 `Crib_Level`.
struct UnlockableCribLevelPayload {
    std::optional<std::string> cribName;     // <Crib_Name>, pooled
    std::optional<int32_t> level;            // <Level>
};
// spec 4.4 table #7 `Clothes`: up to 9 <Item> entries under <Items>.
struct UnlockableClothesItem {
    std::optional<std::string> itemName;     // <ItemName> -> customization_items.xtbl
    std::optional<std::string> itemVariant;  // <ItemVariant>
    std::optional<std::string> color1, color2, color3; // <Color1..3> -> character_color_pool.xtbl (hashes)
};
// spec 4.4 table #8 `Sprint_Bonus`. `amount` is the raw XML int; the engine
// stores it as a float divided by 100 (spec: "float = Amount / 100") - the
// conversion is NOT applied here, this is the element's own text value.
struct UnlockableSprintBonusPayload {
    std::optional<int32_t> amount;           // <Amount> (raw; engine divides by 100)
    std::optional<int32_t> level;            // <Level>
};
// spec 4.4 table #9 `Damage_Resist`.
struct UnlockableDamageResistPayload {
    std::optional<std::string> source;       // <Source>: bullet|explosion|vehicle|fire|falling|script
    std::optional<int32_t> resistPercent;    // <Resist_Percent> (raw; engine divides by 100)
};
// spec 4.4 table #10 `Notoriety`.
struct UnlockableNotorietyPayload {
    std::optional<std::string> score;        // <Score>: an alias name resolving to a category (spec 4.4 #10)
    std::optional<int32_t> amount;           // <Amount> (raw; engine divides by 100)
};
// spec 4.4 table #21 `Ammo_Multiplier`.
struct UnlockableAmmoMultiplierPayload {
    std::optional<std::string> invSlot;      // <inv_slot>: weapon_inventory_slots.xtbl name
    std::optional<float> ammoMul;            // <ammo_mul>
};
// spec 4.4 table #30 `Dual_Wield`.
// (`Weapon_Type` text: "pistols" -> 0, "SMGs" -> 1, else neither; kept as raw text.)
// spec 4.4 table #32 `Pay_Cash_for_Respect`.
struct UnlockablePayCashForRespectPayload {
    std::optional<int32_t> cashCost;         // <Cash_Cost>
    std::optional<int32_t> respectReceived;  // <Respect_Received>
};
// spec 4.4 table #44 `Player_Health_Bonus`.
struct UnlockablePlayerHealthBonusPayload {
    std::optional<float> modifier;           // <Modifier>
    std::optional<int32_t> level;            // <Level>
};
// spec 4.4 table #45 `Muscles`.
struct UnlockableMusclesPayload {
    std::optional<float> boost;              // <Boost>
    std::optional<float> meleeMultiplier;    // <Melee_Multiplier>
    std::optional<float> throwX, throwY, throwZ; // <Throw_x/y/z>
};
// spec 4.4 table #59 `Crib_Cash_Stronghold_Scalar`.
struct UnlockableCribCashStrongholdScalarPayload {
    std::optional<float> scalar;             // <scalar>
    std::optional<std::string> district;     // <district> -> map_districts.xtbl (default: invalid id when absent)
};

// spec-tables-progression.md S4.2/S4.4. One struct per row; the <Type>
// element's first present child selects one of 61 types (spec 4.4's table);
// `typeIndex`/`typeName` record which one, and at most one of the payload
// members below is populated (only the 44 types that read any element -
// see spec 4.4; the other 17 carry no payload and are identified by
// typeName alone). typeIndex is -1 when no recognised <Type> child matched
// (spec 4.1: such a row is dropped by the real loader and its slot reused -
// NOT reproduced here; see the population harness for how often this fires
// against real base-game rows).
struct Unlockable {
    std::string name;                        // <Name>; record id = sr3xtbl::NameHash(lower(name)) (spec 4.1, CONFIRMED - empirical)
    int typeIndex = -1;                      // position among the 61 names in spec 4.4's table, in the tested order
    std::string typeName;                    // the matched <Type> child's element name (e.g. "Vehicle"); empty if none matched

    // Payload for the 44 types that read at least one element (spec 4.4). Types with no
    // payload (Music, Mission_Stronghold, Custom, Free_Driveup_Homie, Pickpocket, Vehicle_Customization,
    // Character_Customization, Gang_Customization_Unlock, Reminder, Auto_Complete_City_Takeover_District/All,
    // Explosive_No_Ragdoll, Vampire, Bloody_Mess, Immune_To_Flat_Tires, Collectable_Finder) are fully
    // identified by typeIndex/typeName alone.
    std::vector<UnlockableVehicleEntry> vehicleEntries;              // #0 Vehicle
    std::optional<std::string> homieName;                            // #1 Homie <Name>
    std::optional<std::string> cribWeaponName;                       // #2 Crib_Weapon <Name>
    std::optional<std::string> storeWeaponName;                      // #3 Store_Weapon <Name>
    std::optional<UnlockableDiscountPayload> discount;                // #4 Discount
    std::optional<float> cribCustomizationDiscountAmount;             // #5 Crib_Customization_Discount <Amount>
    std::optional<UnlockableCribLevelPayload> cribLevel;              // #6 Crib_Level
    std::vector<UnlockableClothesItem> clothesItems;                  // #7 Clothes
    std::optional<UnlockableSprintBonusPayload> sprintBonus;          // #8 Sprint_Bonus
    std::optional<UnlockableDamageResistPayload> damageResist;        // #9 Damage_Resist
    std::optional<UnlockableNotorietyPayload> notoriety;              // #10 Notoriety
    std::optional<float> healthModifier;                              // #11 Health <Modifier>
    std::optional<std::string> cribName;                              // #12 Crib <Name>
    std::optional<float> repairDiscountAmount;                        // #14 Repair_Discount <Amount>
    std::optional<std::string> gangCustomizationGangType;             // #17 Gang_Customization <Gang_Type>
    std::optional<std::string> gangVehicleCustomizationVehicleGroup;  // #18 Gang_Vehicle_Customization <Vehicle_Group>
    std::optional<float> meleeDamageBonusMultiplier;                  // #19 Melee_Damage_Bonus <Damage_Multiplier>
    std::optional<std::string> unlimitedCribAmmoWeaponClass;          // #20 Unlimited_Crib_Ammo <weapon_class>
    std::optional<UnlockableAmmoMultiplierPayload> ammoMultiplier;    // #21 Ammo_Multiplier
    std::optional<std::string> noReloadingInvSlot;                    // #22 No_Reloading <inv_slot>
    std::optional<float> firearmAccuracyMultiplier;                   // #23 Firearm_Accuracy <accuracy_multiplier>
    std::optional<std::string> gangTauntsTeam;                        // #24 Gang_Taunts <Team>
    std::vector<std::string> tauntsActions;                           // #25 Taunts <Actions><Animation_Action> (up to 8)
    std::vector<std::string> complimentsActions;                      // #26 Compliments (same shape as Taunts)
    std::optional<std::string> outfitName;                            // #27 Outfit <Outfit>
    std::optional<std::string> dualWieldWeaponType;                   // #30 Dual_Wield <Weapon_Type> (raw text: "pistols"/"SMGs"/other)
    std::optional<int32_t> weeklyPaymentsAmount;                      // #31 Weekly_Payments <Amount>
    std::optional<UnlockablePayCashForRespectPayload> payCashForRespect; // #32 Pay_Cash_for_Respect
    std::optional<int32_t> lumpSumOfMoneyAmount;                      // #33 Lump_Sum_of_Money <Amount>
    std::optional<float> respectBonusModifier;                       // #37 Respect_Bonus_Modifier <Modifier>
    std::optional<float> cashBonusModifier;                          // #38 Cash_Bonus_Modifier <Modifier>
    std::optional<float> npcCashDropModifier;                        // #39 NPC_Cash_Drop_Modifier <Modifier>
    std::optional<int32_t> dbnoExtraTimeMs;                          // #40 DBNO_Extra_Time <ms>
    std::optional<float> reviveHoldTimeModifier;                     // #41 Revive_Hold_Time_Modifier <Modifier>
    std::optional<float> homieHealthBonusModifier;                   // #42 Homie_Health_Bonus <Modifier>
    std::optional<float> specialHomieHealthBonusModifier;            // #43 Special_Homie_Health_Bonus <Modifier>
    std::optional<UnlockablePlayerHealthBonusPayload> playerHealthBonus; // #44 Player_Health_Bonus
    std::optional<UnlockableMusclesPayload> muscles;                 // #45 Muscles
    std::optional<float> weaponCustomizationDiscountAmount;          // #46 Weapon_Customization_Discount <Amount>
    std::optional<std::string> gangWeaponsSlot;                      // #51 Gang_Weapons <Weapon_Slot>
    std::optional<std::string> customizationItemsTeamName;           // #53 Customization_Items <Team_Name>
    std::optional<float> reloadSpeedBonusScalar;                     // #55 Reload_Speed_Bonus <scalar>
    std::optional<float> cribCashLimitScalar;                        // #58 Crib_Cash_Limit_Scalar <scalar>
    std::optional<UnlockableCribCashStrongholdScalarPayload> cribCashStrongholdScalar; // #59
    std::optional<float> cribAmmoPercent;                            // #60 Crib_Ammo <percent>

    std::optional<std::string> displayName;      // <DisplayName>, localisation key
    std::optional<std::string> description;      // <Description>, localisation key
    std::optional<std::string> imageFilename;     // <Image_Source><Filename> (extension NOT stripped here; that
                                                   // is a display/pooling transform, not part of the XML schema)
    UnlockableCategory category = UnlockableCategory::PlayerAbilities; // <Category>; default/unmatched = 0 (spec 4.2)
    std::optional<std::string> detailedDescriptionText; // <Detailed_Description_Text>, pooled string
    std::optional<std::string> eventText;         // <Event_Text>, pooled string
    // spec 4.2/4.3 give the type (u32) but not which reader flavour (always vs if-present); treated as
    // always-write per the S1.3 default for a plain numeric leaf with no "optional"/"if present" wording.
    Always<uint32_t> price;                       // <Price>
    std::optional<std::string> priority;          // <Priority>: another Unlockable.Name; hashed to 0 if absent (spec 4.2)
    UnlockableAutoUnlock autoUnlock = UnlockableAutoUnlock::None; // <Auto_Unlock>; absent = 0
    UnlockableIsDlc isDlc = UnlockableIsDlc::False;                // <Is_DLC>; absent = 0
    std::optional<std::string> framework;          // <Framework>, default "main" (spec 1.4)
};

// Parses one <Unlockable> row (spec 4.2/4.4).
Unlockable ParseUnlockable(const Node* row);

// All <Unlockable> rows of an unlockables.xtbl / patch_unlockables.xtbl /
// *_unlockables.xtbl <Table>, in file order. Duplicate-hash suppression
// (spec 4.1: "skip the row if a record with the same hash already exists")
// and the 384-record capacity are real-loader behaviours this reader does
// NOT apply - every row present in the file is returned.
std::vector<Unlockable> ParseUnlockablesTable(const Document& doc);

} // namespace sr3tables_progression
