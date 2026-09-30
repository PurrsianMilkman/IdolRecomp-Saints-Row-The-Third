#pragma once

// horde_mode.xtbl (spec-tables-diversions.md 8.1) - split into its own
// header per this project's convention (mirrors sr3tables_environment's
// weather_time_of_day.h/camera_free.h split for its own largest tables);
// included from the bottom of sr3tables_diversions/tables.h. See that
// file's banner for the general conventions this header follows.
//
// SCOPE OF WHAT IS MODELLED, and why (spec 8.1/15 item 1): horde_mode.xtbl
// is read by TWO loader functions. The FIRST (FUN_006EA230) is fully
// offset-mapped by the spec: UI_Settings/Floating_Points,
// Wave_Failsafe_Completion_Delay_ms/_Min_Enemy_Kills_Pct/_No_Enemies_Radius,
// and Player_Characters (a Grid, CONFIRMED capacity exactly 5, 0x184-byte
// stride) with a fairly deep nested Customization sub-schema. The SECOND
// (FUN_006F54E9 - Cooperative_Multiplayer_Constants, Special_Spawn_Conditions,
// Powerups, Enemy_Data, Point_Multipliers, Audio) was NOT offset-mapped
// (Ghidra's parameter recovery degraded on its 16-parameter __thiscall
// signature under -noanalysis, spec 8.1) - but its ELEMENT SCHEMA is still
// independently recovered from the table's own shipped TableDescription
// (real structured XML documentation data, not disassembly), which is
// sufficient to write a reader that finds elements BY NAME (this reader
// never depends on byte offsets - those are cited in comments purely for
// spec cross-reference). Sub-pieces are modelled to the precision the spec's
// prose or its own cross-reference table (spec 13) gives an EXACT element
// name for. Where the spec's prose alone did not give a concrete tag name
// (Enemy_Data's own numeric "point value" leaf, Point_Multipliers.Weapons.
// Weapon's own multiplier leaf, Customization_Items' inner item/colour-pool
// fields), this reader's OWN validation harness
// (tools/validation/validate_tables_diversions_population.cpp) was run
// against the real shipped horde_mode.xtbl DATA (never the executable, per
// the HARD RULE - real .xtbl game data is explicitly in scope) and every one
// of those leaf names is now CONFIRMED-empirical (see each field's own
// comment for the specific correction this made - e.g. the point-value leaf
// is "Value", not the "Points" this reader first guessed; the weapon
// multiplier leaf is "Point_Multiplier", not "Multiplier"). Only the Audio
// block (spec 8.1/15 item 9 - not reached by either decompiled loader
// fragment, no concrete leaf names anywhere in the spec text, and not
// investigated against real data this pass since the spec itself does not
// even name its top-level children precisely enough to start from) remains
// genuinely OPEN and unmodelled, per this project's "no invented fixes"
// policy - see the task report.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_diversions {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ---------------------------------------------------------------------------
// Player_Characters (spec 8.1: CONFIRMED capacity exactly 5, 0x184-byte
// stride - the most fully offset-mapped part of this table).
// ---------------------------------------------------------------------------
struct HordeModeDefaultWeapon {
    std::optional<std::string> name;     // Default_Weapon.Name (Reference into weapons.xtbl -
                                          // exact path confirmed, spec 13 cross-ref table)
    Always<int32_t> upgradedLevel;        // Default_Weapon.Upgraded_Level
};
// Customization_Items/Customization_Item (spec 8.1/13: "an optional Grid of
// item+colour-pool references" into customization_items.xtbl/
// character_color_pool.xtbl). The spec's prose never gives this grid's row
// element name or its leaf field names; this shape is instead CONFIRMED
// against real shipped horde_mode.xtbl DATA (via this reader's own
// validation harness - never the executable, per the HARD RULE): the ROW
// element is "Customization_Item", and the item Reference lives on a CHILD
// element that reuses that SAME tag name ("Customization_Item" nested inside
// "Customization_Item" - an authoring-tool quirk in the real data, not a
// mistake in this reader), so the row's own text is absent (it has child
// elements) and the reference must be read via a by-name child lookup, not
// the row's own text. The row also holds a "Custom_Colors" grid of
// "Custom_Color.Color_Name" (repeated - a colour POOL, not a single colour
// reference as the spec's "colour-pool reference" wording alone might suggest).
struct HordeModeCustomizationItem {
    std::optional<std::string> itemName;         // the CHILD "Customization_Item" element's text
                                                  // (Reference into customization_items.xtbl) - NOT
                                                  // the row's own text, see above
    std::vector<std::string> customColorNames;   // Custom_Colors/Custom_Color.Color_Name* (Reference
                                                  // into character_color_pool.xtbl)
};
struct HordeModeCustomization {
    std::optional<std::string> gender;  // Customization.Gender - a "Selection" enum (spec 8.1); no
                                         // choice list is given, kept as raw text
    std::optional<std::string> race;    // Customization.Race
    std::optional<std::string> figure;  // Customization.Figure
    std::optional<std::string> persona; // Customization.Persona (Reference into
                                         // audio_personas.xtbl, spec 13)
    std::optional<std::string> outfit;  // Customization.Outfit (optional Reference into
                                         // customization_outfits.xtbl, spec 13)
    bool customizationItemsPresent = false;             // Customization_Items - whole block optional
    std::vector<HordeModeCustomizationItem> customizationItems;  // Customization_Items/Customization_Item*
};
struct HordeModePlayerCharacterLines {
    std::optional<std::string> death;         // Lines.Death (Reference into audio_line_tags.xtbl)
    std::optional<std::string> waveVictory;   // Lines.Wave_Victory
    std::optional<std::string> levelVictory;  // Lines.Level_Victory
};
struct HordeModePlayerCharacter {
    std::optional<std::string> name;         // Name
    std::optional<std::string> displayName;  // display_name
    bool customizationPresent = false;
    HordeModeCustomization customization;    // Customization
    Always<float> sprintTimeSeconds;          // Sprint_Time_Seconds
    Always<float> health;                     // Health
    Always<float> healthRechargeRate;         // Health_Recharge_Rate
    // Default_Weapons - a Grid, capacity 3 (spec 8.1), row element
    // "Default_Weapon" (spec 13 cross-ref: Default_Weapons.Default_Weapon.Name);
    // cap not enforced here.
    std::vector<HordeModeDefaultWeapon> defaultWeapons;
    HordeModePlayerCharacterLines lines;      // Lines
};
// Levels grid (spec 8.1/13): Level{Unique_ID, xtbl.Filename, display_name}.
// The referenced per-level gameplay .xtbl (e.g. zombie_area.xtbl) is a
// SEPARATE file this spec/reader does not open (spec 8.1, explicitly out of
// scope).
struct HordeModeLevel {
    std::optional<std::string> uniqueId;   // Unique_ID
    std::optional<std::string> filename;   // xtbl/Filename (spec 13: "Levels.Level.xtbl.Filename" -
                                            // a child literally named "xtbl" holding "Filename")
    std::optional<std::string> displayName; // display_name
};

// ---------------------------------------------------------------------------
// Cooperative_Multiplayer_Constants (spec 8.1 names every field exactly).
// ---------------------------------------------------------------------------
struct HordeModeMultiplierPair {
    Always<float> enemyHealth;  // Enemy_Health
    Always<float> enemyDamage;  // Enemy_Damage
};
struct HordeModeCoopMultiplayerConstants {
    HordeModeMultiplierPair base;        // Enemy_Health/Enemy_Damage directly under this block
    HordeModeMultiplierPair multipliers; // Multipliers/{Enemy_Health,Enemy_Damage} (nested, same
                                          // two names, spec 8.1)
    Always<int32_t> reviveLimit;          // Revive_Limit
    Always<int32_t> reviveResetWaveInterval; // Revive_Reset_Wave_Interval
};

// ---------------------------------------------------------------------------
// Special_Spawn_Conditions (spec 8.1: row Special_Spawn_Condition, confirmed
// by the "10 Special_Spawn_Condition rows" validation count).
// ---------------------------------------------------------------------------
// spec 8.1: "Data - a tagged union of Kills/Points/Wave_Number/
// Every_N_Waves, each with its own Repeat/Count/Number". Which of
// Repeat/Count/Number applies to which of the 4 variants is not spelled out
// precisely by the spec text, so all three are read generically from
// whichever variant child is present (an absent field on the Always
// accessor is simply the normal Always-hazard, not an error - reading a
// field that turns out not to apply to a given variant is harmless).
struct HordeModeSpawnConditionData {
    Always<bool> repeat;      // Repeat
    Always<int32_t> count;    // Count
    Always<int32_t> number;   // Number
};
struct HordeModeSpecialSpawnCondition {
    std::optional<std::string> name;  // Name
    std::optional<std::string> variant;  // which of Kills/Points/Wave_Number/Every_N_Waves was
                                          // present as Data's child (raw tag name kept, not an enum
                                          // - the 4 names themselves ARE spec-confirmed exactly)
    HordeModeSpawnConditionData data;
};

// ---------------------------------------------------------------------------
// Powerups (spec 8.1: row "Powerup", confirmed by "4 Powerup rows (all four
// enumerated types...)"). All 4 Type choices ARE given exactly.
// ---------------------------------------------------------------------------
inline constexpr std::array<std::string_view, 4> kHordeModePowerupTypeNames = {
    "Invulnerability", "Kill All Enemies", "Unlimited Ammo", "Weapon Upgrade",
};
struct HordeModePowerup {
    std::optional<std::string> type;      // Type - matched against kHordeModePowerupTypeNames
                                           // (raw text kept)
    std::optional<std::string> itemName;  // Item_Name (Reference into items_3d.xtbl)
    std::optional<std::string> audioCue;  // Audio_Cue (optional)
};

// ---------------------------------------------------------------------------
// Enemy_Data (spec 8.1/13).
// ---------------------------------------------------------------------------
struct HordeModeDefaultAiSettings {
    std::optional<std::string> personality;  // Default_AI_Settings.Personality (Reference into
                                              // AI\ai_personalities.xtbl - exact leaf name
                                              // confirmed by spec 13's cross-reference table)
    std::optional<std::string> behavior;     // Default_AI_Settings.Behavior (Reference into
                                              // AI\ai_behavior.xtbl - same)
};
struct HordeModePointValue {
    std::optional<std::string> name;   // Point_Value.Name (Reference into character.xtbl - exact
                                        // leaf name confirmed, spec 13 cross-ref)
    Always<int32_t> points;             // Point_Value.Value - the "point value" itself (spec 8.1
                                        // prose: "character-name Reference + point value"). The
                                        // spec never gives this leaf's own tag name in prose; an
                                        // earlier version of this reader guessed "Points" by
                                        // analogy with other reward-grid fields in this group, and
                                        // that guess was WRONG - corrected to the CONFIRMED real
                                        // tag "Value" via this reader's own validation harness
                                        // against real shipped horde_mode.xtbl DATA (never the
                                        // executable, per the HARD RULE).
};
struct HordeModeEnemyData {
    bool defaultAiSettingsPresent = false;
    HordeModeDefaultAiSettings defaultAiSettings;  // Default_AI_Settings
    // Point_Values - row element "Point_Value" (spec 13 cross-ref
    // confirms this exactly, via "60 Point_Value rows" in the validation
    // summary, spec 14).
    std::vector<HordeModePointValue> pointValues;
};

// ---------------------------------------------------------------------------
// Point_Multipliers (spec 8.1/13: Weapons grid + Melee_Point_Multiplier,
// the latter given exactly).
// ---------------------------------------------------------------------------
struct HordeModeWeaponMultiplier {
    std::optional<std::string> name;   // Weapon.Name (Reference into weapons.xtbl - exact leaf
                                        // name confirmed, spec 13 cross-ref:
                                        // "Point_Multipliers.Weapons.Weapon.Name")
    Always<float> multiplier;           // Weapon.Point_Multiplier - the multiplier value itself.
                                        // Corrected the same way as HordeModePointValue.points
                                        // above: an earlier "Multiplier" guess was WRONG; the
                                        // CONFIRMED real tag is "Point_Multiplier" (same name as
                                        // the whole containing block one level up, an authoring
                                        // reuse - not this reader's error), verified against real
                                        // shipped horde_mode.xtbl DATA via the validation harness.
};
struct HordeModePointMultipliers {
    // Weapons - row element "Weapon" (spec 13 cross-ref confirmed exactly);
    // real row: "20 weapon Point_Multiplier rows" (spec 8.1).
    std::vector<HordeModeWeaponMultiplier> weapons;
    Always<float> meleePointMultiplier;  // Melee_Point_Multiplier
};

// ---------------------------------------------------------------------------
// The whole table.
// ---------------------------------------------------------------------------
struct HordeModeFloatingPoints {
    // UI_Settings/Floating_Points/{Intensity,Scale,Location_Scale} - the spec
    // names the parent path but not each leaf's own exact tag beyond these
    // three concepts (spec 8.1); modelled with those three names directly,
    // matching the spec's own wording verbatim.
    Always<float> intensity;        // Intensity
    Always<float> scale;            // Scale
    Always<float> locationScale;    // Location_Scale
};
struct HordeMode {
    HordeModeFloatingPoints floatingPoints;  // UI_Settings/Floating_Points

    // Wave_Failsafe_* - read DIRECTLY as s32 with NO seconds->ms conversion
    // (the element's own name already carries the "_ms" suffix - spec 8.1's
    // control case confirming the seconds->ms constant's role elsewhere in
    // this whole group).
    Always<int32_t> waveFailsafeCompletionDelayMs;      // Wave_Failsafe_Completion_Delay_ms
    Always<float> waveFailsafeMinEnemyKillsPct;          // Wave_Failsafe_Completion_Min_Enemy_Kills_Pct
    Always<float> waveFailsafeNoEnemiesRadius;           // Wave_Failsafe_Completion_No_Enemies_Radius

    // Levels - real row: 3 (zombie_area/angels_area/daedelus_area, spec 8.1).
    std::vector<HordeModeLevel> levels;

    // Player_Characters - CONFIRMED capacity exactly 5 (spec 8.1/14); real
    // row has exactly 5. Cap not enforced here.
    std::vector<HordeModePlayerCharacter> playerCharacters;

    bool coopMultiplayerConstantsPresent = false;
    HordeModeCoopMultiplayerConstants coopMultiplayerConstants;  // Cooperative_Multiplayer_Constants

    // Special_Spawn_Conditions - real row: 10 (spec 8.1, covering all 4 Data
    // tag variants).
    std::vector<HordeModeSpecialSpawnCondition> specialSpawnConditions;

    // Powerups - real row: 4 (all four enumerated types, spec 8.1).
    std::vector<HordeModePowerup> powerups;

    bool enemyDataPresent = false;
    HordeModeEnemyData enemyData;  // Enemy_Data

    bool pointMultipliersPresent = false;
    HordeModePointMultipliers pointMultipliers;  // Point_Multipliers

    // Audio (Sound_Effects/Music, per-event Switches grids referencing
    // audio_syncs.xtbl) - spec 8.1/15 item 9 explicitly says this block was
    // not reached by either decompiled loader fragment and is
    // schema-confirmed from the TableDescription ONLY, with no concrete leaf
    // element names given anywhere in the spec text. NOT modelled - left
    // entirely OPEN, consistent with this project's "don't guess element
    // names" rule.
};
HordeMode ParseHordeMode(const Node* row);
std::optional<HordeMode> ParseHordeModeTable(const Document& doc);

}  // namespace sr3tables_diversions
