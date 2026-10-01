#pragma once

// sr3tables_ui_controls - typed readers for the control-binding / QTE /
// camera-preset `.xtbl` table group: control_binding_sets.xtbl,
// control_filters.xtbl, control_parameters.xtbl, control_scheme_text.xtbl,
// control_schemes.xtbl, hud_qte_interface.xtbl, hud_qte_interface_presets.xtbl,
// qte.xtbl, qte_sequences.xtbl, user_interface.xtbl, voice_control.xtbl,
// credits_pc.xtbl, item_cust_cameras.xtbl, player_cust_cameras_new.xtbl,
// vehicle_cust_cameras_new.xtbl, vehicle_group_cameras.xtbl,
// vehicle_cameras.xtbl.
//
// Source: spec-tables-ui-controls.md (SPEC TEAM, agent AP, 2026-09-23), the
// ONLY source consulted for this file's schema (cleanroom boundary - see the
// HARD RULES in the task that produced this file; the shared reader grammar
// itself is reused, not re-derived, from spec-tables-weapons-combat.md 1, per
// spec-tables-ui-controls.md 1.3). Built entirely on top of
// include/sr3xtbl/xtbl.h's document model and accessors (Node, FindChild,
// ChildText, GetX/ReadXAlways, EnumIndex, NameHash); nothing here was derived
// from the game executable, disassembly or decompiled code - every
// offset/address cited in a comment is copied verbatim from the spec's own
// prose, purely to let a reader cross-reference the spec section, never
// re-derived.
//
// CONVENTION (matches sr3xtbl.h and sr3tables_environment/tables.h exactly):
//   * sr3xtbl::Always<T> (value + present) = the spec's "always write"
//     elements, OR any field the spec does not explicitly flag either way
//     (the "usual" accessor family per xtbl.h's own banner: 0x00DACCB0/
//     0x00DABC70/0x00DABDF0/0x00DABF20/0x00DAC480). When !present the
//     ENGINE's real behaviour is an indeterminate stack-residue value, NOT
//     sr3xtbl::Always<T>'s 0 stand-in - see xtbl.h's own warning.
//   * std::optional<T> = the spec's EXPLICITLY-STATED "write only if
//     present" elements.
//   * A handful of elements have a spec-STATED CONCRETE default distinct
//     from both of the above (e.g. control_schemes.xtbl's X360 -> true when
//     absent, voice_control.xtbl's Priority -> 10, qte_sequences.xtbl's
//     Action_Count -> 0). These are modelled as std::optional<T> plus a
//     small `...OrDefault()` helper that applies the spec's own documented
//     constant.
//   * For CONFIRMED, FULLY-ENUMERATED small lookup tables (spec 4.4-4.8, 9.2,
//     13) a field is resolved directly to an `int32_t` (default -1 = "no
//     match", the engine's own sentinel), matching sr3tables_weapons's own
//     convention for its 23/7/etc.-name enums (weapons.h: `int32_t
//     weaponClass = -1`). For the THREE large lookup tables the spec confirms
//     exist (exact address/size/value-range) but does NOT enumerate beyond a
//     handful of representative names - the 166-entry CBA button-action
//     table (4.1), the 34-entry CAA axis-action table (4.2), and the 80-entry
//     key-name table (4.3) - the corresponding fields are kept as RAW TEXT
//     (std::optional<std::string>), per this task's explicit "do not
//     fabricate placeholder names" rule. See the banner below section 4 for
//     the itemised list of what IS and is NOT given for each.
//
// WHAT IS DELIBERATELY LEFT OUT:
//   * Every load-time TRANSFORM the spec documents (CRC-32 hashing of a Name
//     into a row key; degrees->radians conversion; ms/1000 integer division;
//     RNG-drawn fallbacks; cross-table pointer resolution) is a derived
//     value, not a readable XML element, and is not modelled - the raw
//     authored text/number is kept instead, with the transform noted in a
//     comment (matches sr3tables_environment's convention exactly, e.g. its
//     decal_info.xtbl Slope_Fade_Start/End).
//   * `FUN_00AC5CF0`'s "three camera-angle sets" internal field layout (8.1)
//     is OPEN in the spec (not decompiled past the dispatch call) - only
//     element PRESENCE is modelled for Primary_Camera_Angle/
//     Secondary_Camera_Angle/RC_Camera_Angle.
//   * item_cust_cameras.xtbl/player_cust_cameras_new.xtbl's "static array of
//     {default float[32], filename[224]}" consumer (8.2) is OPEN in the spec
//     (the container SHAPE is inferred, not the code that walks it) - not
//     modelled; this header only parses the `.xtbl` FILE content itself
//     (CONFIRMED - empirical schema, identical to vehicle_cust_cameras_new.xtbl).
//   * control_filters.xtbl/control_parameters.xtbl's shared loader
//     `FUN_005D25F0` (14) was not decompiled to field level in the spec - the
//     row schema below is the spec's own EMPIRICAL description of real file
//     content, not a byte-offset-verified struct.
//   * control_schemes.xtbl's per-row Scheme_Type classification (3) - the
//     spec describes a SUBSTRING/CONTAINS test against some text, not a
//     confirmed XML element name or shape (spec Open Item 12 flags this as
//     "not independently confirmed against its full calling convention") -
//     not modelled; ControlScheme below carries no classification field.
//   * The vehicle-info-table lookup mechanism itself (spec-vehicle-data.md
//     territory, cross-referenced by 8.1 but not re-derived here) - this
//     header models only the CAMERA FIELD SLICE spec-tables-ui-controls.md
//     8.1 documents, parsed directly off a `<Vehicle>`/`<Vehicle_Group>`
//     row's own children/attributes, not the full 0xB80-byte vehicle record.
//
// JUDGEMENT CALLS (flagged here and in the delivery report; none change a
// CONFIRMED fact, all are reconstructions of an element's exact shape from
// spec prose that abbreviates or does not fully spell it out):
//   * kStickDirectionNames/kStickAxisNames (4.7/4.8): the spec writes
//     "`LEFT_STICK_DIR_UP`=0, `_DOWN`=1, `_RIGHT`=2, `_LEFT`=3" - the leading
//     underscore is read as shorthand continuing the "LEFT_STICK_DIR_"
//     prefix (a plain-English "ditto" convention), not as literal table
//     content beginning with an underscore. Same for `_Y` under
//     `RIGHT_STICK_AXIS_X`/`LEFT_STICK_AXIS_X`.
//   * QteNodeNpcAnimation's per-slot field names (7.2's NPC_Animations list)
//     are inferred by symmetry with the QTE_Node's OWN top-level field names
//     of the same words (Persona_Line, Line_Delay, Enter_Action,
//     Looping_State, Additive_Action) and with Animated_NPC's own NPC_Name
//     child - the spec's prose lists these words but does not spell out
//     NPC_Animation's exact child-tag shape the way it does for every other
//     field in this document. Flagged in the delivery report.
//   * hud_qte_interface_presets.xtbl's row tag ("Hud_qte_preset") and
//     hud_qte_interface.xtbl's row tag ("Hud_qte") are read from the spec's
//     "`X` record" naming (the same convention that names control_schemes.xtbl's
//     literal `<Control>`/`<Axis>` tags elsewhere in this document), not from
//     an explicit `<Hud_qte_preset>`/`<Hud_qte>` literal in the spec text -
//     the population validator checks this against real archive data.
//   * VehicleCameraRow/UserInterfaceRow/ControlSchemeText's row-key element
//     is assumed to be `Name`, following this project's near-universal
//     convention (every other table in this spec uses `Name`) rather than an
//     explicit citation in spec section 8.1/10/11 (which name only the
//     camera/UI/text fields, not the row key itself).
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> for every repeated-row table. All definitions are in
// src/tables_ui_controls.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_ui_controls {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// 4. The shared action-name and input-vocabulary tables
// (spec-tables-ui-controls.md 4)
// ===========================================================================
//
// See the file banner above for the "what is and is not given" rule this
// section follows. Summary:
//   * CBA (button-action) table - 166 entries at 0x01122160/0x01122164 (4.1).
//     NOT enumerated in full by the spec (9 of 166 names given as examples).
//     NOT modelled as a lookup table here; Action fields stay raw text.
//   * CAA (axis-action) table - 34 entries at 0x01122690/0x01122694 (4.2).
//     NOT enumerated in full (7 of 34 names given). NOT modelled; Action
//     fields stay raw text.
//   * Key-name table - 80 entries at 0x01121D20/0x01121D24 (4.3). Roughly 60
//     of 80 named, several via a start/end-value shorthand that cannot be
//     safely expanded (A=0x1E..Z=0x2C spans only 15 values for 26 letters -
//     see the file banner). NOT modelled; Key fields stay raw text.
// The five tables below ARE fully enumerated by the spec text and ARE
// modelled, resolved directly to an int32_t value (see the file banner).

// A `{name, value}` pair, for the small tables whose values are NOT simply
// "array index" (mouse-button's UNBOUND=-1 trailing the list, gamepad-
// button's intentional L_JOY/R_JOY/STICK_UNBOUND value collisions with
// A/B/A - spec 4.6).
struct NamedValue {
    std::string_view name;
    int32_t value;
};

// Mouse-button table - 8 entries at 0x01121FA0/0x01121FA4 (4.4). "Exactly 7
// real values (0-6), confirming spec-save-format.md 7.6's own '< 7'
// acceptance range field-for-field, by name."
inline constexpr NamedValue kMouseButtonTable[8] = {
    {"MOUSE LEFT", 0}, {"MOUSE MIDDLE", 1}, {"MOUSE RIGHT", 2}, {"MOUSE 4", 3},
    {"MOUSE 5", 4}, {"WHEEL UP", 5}, {"WHEEL DOWN", 6}, {"UNBOUND", -1},
};

// Mouse-axis table - 3 entries at 0x01121FE0/0x01121FE4 (4.5).
inline constexpr NamedValue kMouseAxisTable[3] = {
    {"MOUSE X", 0}, {"MOUSE Y", 1}, {"UNBOUND", -1},
};

// [OPEN - spec-tables-ui-controls.md 4.6: "`STICK_UNBOUND`=0 is the only `UNBOUND` entry with a
// non-negative value and it collides with both `A` and `L_JOY`; whether the value column was
// read correctly ... to be settled against the executable"; Review status 4.6: "NEEDS-EXE:
// `STICK_UNBOUND`=0 collision". The spec gives NO confirmed sentinel for an unbound stick (the
// other UNBOUND entries are -1), so the 0 below is reproduced as listed, collision included,
// and no replacement value is invented.]
// Gamepad-button table - 19 entries at 0x011220C8/0x011220CC (4.6). The last
// three (L_JOY, R_JOY, STICK_UNBOUND) are a REAL, CONFIRMED value collision
// with A/B/A respectively - the spec: "consumers must disambiguate by which
// of the two lookup tables/fields they used, not by the returned value
// alone." LookupNamedValue below returns the value of the FIRST matching
// NAME (never ambiguous by name), so this collision only matters if a caller
// tries to go the other direction (value -> name).
inline constexpr NamedValue kGamepadButtonTable[19] = {
    {"A", 0}, {"B", 1}, {"X", 2}, {"Y", 3}, {"LB", 4}, {"RB", 5}, {"LT", 6}, {"RT", 7},
    {"D_RIGHT", 8}, {"D_UP", 9}, {"D_LEFT", 10}, {"D_DOWN", 11}, {"START", 12}, {"BACK", 13},
    {"LS", 14}, {"RS", 15}, {"L_JOY", 0}, {"R_JOY", 1}, {"STICK_UNBOUND", 0},
};

// Stick-direction table - 9 entries at 0x011227A0/0x011227A4 (4.7). See the
// file banner's JUDGEMENT CALLS note on the "_DOWN"/"_RIGHT"/"_LEFT" shorthand
// expansion.
inline constexpr NamedValue kStickDirectionTable[9] = {
    {"STICK_DIR_UNBOUND", -1},
    {"LEFT_STICK_DIR_UP", 0}, {"LEFT_STICK_DIR_DOWN", 1}, {"LEFT_STICK_DIR_RIGHT", 2}, {"LEFT_STICK_DIR_LEFT", 3},
    {"RIGHT_STICK_DIR_UP", 4}, {"RIGHT_STICK_DIR_DOWN", 5}, {"RIGHT_STICK_DIR_RIGHT", 6}, {"RIGHT_STICK_DIR_LEFT", 7},
};

// Stick-axis table - 5 entries at 0x011227E8/0x011227EC (4.8). Same
// shorthand-expansion note as kStickDirectionTable.
inline constexpr NamedValue kStickAxisTable[5] = {
    {"STICK_AXIS_UNBOUND", -1},
    {"RIGHT_STICK_AXIS_X", 0}, {"RIGHT_STICK_AXIS_Y", 1},
    {"LEFT_STICK_AXIS_X", 2}, {"LEFT_STICK_AXIS_Y", 3},
};

// Linear scan by exact case-insensitive name match (sr3xtbl::NameEquals);
// first match wins. Returns -1 if `node` is null/textless or nothing
// matches - the SAME sentinel the engine's own "no match"/UNBOUND idiom uses
// throughout this group (2.2, 4.4, 4.5).
int32_t LookupNamedValue(const Node* node, const NamedValue* table, size_t count);
template <size_t N>
int32_t LookupNamedValue(const Node* node, const NamedValue (&table)[N]) {
    return LookupNamedValue(node, table, N);
}

// Index-of-match tables (index == stored value; via sr3xtbl::EnumIndex, the
// generic reader FUN_00DAC830 - spec 1.3/9.2).
// Button_Animation_Type - 3-entry table at 0x012FFF1C (9.2).
inline constexpr std::array<std::string_view, 3> kButtonAnimationTypeNames = {
    "Mash Standard", "Mash Fast", "Alternate Triggers",
};
// hud_qte_interface.xtbl's Button_Type - 9-entry gamepad table at 0x012FFEF8 (9.2).
inline constexpr std::array<std::string_view, 9> kQteButtonTypeNames = {
    "A", "X", "Y", "LT", "RT", "LS_UP", "LS_DOWN", "LS_RIGHT", "LS_LEFT",
};
// credits_pc.xtbl's <type>/<type_override> - 4-entry display-type table at
// 0x012FD3E0 (13). "Image" unused in the shipped file.
inline constexpr std::array<std::string_view, 4> kCreditsItemTypeNames = {
    "Name - Role", "Music", "Image", "Centered Single Line",
};

// ===========================================================================
// 2. control_binding_sets.xtbl - PC keyboard/mouse binding sets
// (spec-tables-ui-controls.md 2)
// ===========================================================================

// 2.2 <Control> (button binding) record, 20 bytes.
struct BindingSetControl {
    std::optional<std::string> action;  // Action -> CBA table (166 entries; NOT enumerated - raw text, 4.1)
    std::optional<std::string> key1;    // Key -> key-name table (80 entries; NOT enumerated - raw text, 4.3)
    std::optional<std::string> key2;    // Alt_Key -> same key-name table
    int32_t mouse1 = -1;                // Mouse_Button -> kMouseButtonTable (4.4)
    int32_t mouse2 = -1;                // Alt_Mouse_Button -> same
    // Debug_Only/Non_Release_Final: "either one true skips the record
    // entirely - it is parsed but never stored" (2.2). This reader keeps
    // every row regardless (raw parse convention); these two fields let a
    // caller replicate the engine's gate. No spec-stated default on
    // absence was given for either, so both use the "usual" always-write
    // family per the file banner (hazard applies when !present).
    Always<bool> debugOnly;
    Always<bool> nonReleaseFinal;
};

// 2.3 <Axis> (axis binding) record, 40 bytes. "Closes spec-save-format.md
// 7.6's field-order OPEN item" (2.3) - the field order below IS that answer.
struct BindingSetAxis {
    std::optional<std::string> action;  // Action -> CAA table (34 entries; NOT enumerated - raw text, 4.2)
    std::optional<std::string> key1;    // Key_Pos -> key-name table (raw text)
    std::optional<std::string> key2;    // Key_Neg
    std::optional<std::string> key3;    // Alt_Key_Pos
    std::optional<std::string> key4;    // Alt_Key_Neg
    int32_t mouse1 = -1;                // Mouse_Button_Pos -> kMouseButtonTable
    int32_t mouse2 = -1;                // Mouse_Button_Neg
    int32_t mouse3 = -1;                // Alt_Mouse_Button_Pos
    int32_t mouse4 = -1;                // Alt_Mouse_Button_Neg
    int32_t mouseAxis = -1;             // Mouse_Axis -> kMouseAxisTable (the "direction selector", 4.5)
};

struct BindingSet {
    std::optional<std::string> name;  // Name (bounded copy, <= 0x18 chars, name-cache 0x0129EE6C)
    // Common_Bindings: read into the row header; no consumer found this pass
    // (spec Open Item 11) - surfaced raw since the element itself is real.
    std::optional<bool> commonBindings;
    std::vector<BindingSetControl> controls;  // Button_Controls/Control
    std::vector<BindingSetAxis> axes;         // Axis_Controls/Axis
};
BindingSetControl ParseBindingSetControl(const Node* row);
BindingSetAxis ParseBindingSetAxis(const Node* row);
BindingSet ParseBindingSet(const Node* row);
std::vector<BindingSet> ParseControlBindingSetsTable(const Document& doc);

// ===========================================================================
// 3. control_schemes.xtbl - console gamepad button-layout presets
// (spec-tables-ui-controls.md 3)
// ===========================================================================

// 3.1 <Control> (button) record, 20 bytes.
struct ControlSchemeControl {
    std::optional<std::string> action;         // Action -> CBA table (same as 2.2; raw text)
    int32_t button = -1;                        // Button -> kGamepadButtonTable (4.6)
    int32_t alternateButton = -1;                 // Alternate_Button -> same
    int32_t stickDirection = -1;                   // Stick_Direction -> kStickDirectionTable (4.7)
    std::optional<std::string> actionName;          // Action_Name - free-text display label, NOT looked up
    // X360 gates storage; spec-STATED concrete default true when absent (3.1)
    // - modelled as optional + OrDefault, per the project's convention for a
    // stated default distinct from the general Always-hazard.
    std::optional<bool> x360;
    bool X360OrDefault() const { return x360.value_or(true); }
    // Debug_Only/Non_Release_Final: same gate/no-stated-default situation as
    // BindingSetControl's own pair (2.2) - kept raw, "usual" always family.
    Always<bool> debugOnly;
    Always<bool> nonReleaseFinal;
};

// 3.2 <Axis> record, 20 bytes - a sibling block of <Control_Scheme>, separate
// from <Controls>.
struct ControlSchemeAxis {
    std::optional<std::string> action;    // Action -> CAA table (raw text)
    int32_t axis = -1;                     // Axis -> kStickAxisTable (4.8)
    Always<bool> inverted;                   // Inverted
    int32_t alternateAxis = -1;               // Alternate_Axis -> same table
    Always<bool> alternateInverted;             // Alternate_Inverted
};

struct ControlScheme {
    // Name -> the lettered/named scheme identifier: "A"/"B"/"C"/"N"/"T"/"X"/
    // "General Controls"/"GCN"/"Satellite Controls" (spec 3, 5 item 4).
    std::optional<std::string> name;
    // Weapon_Selection_Stick -> kGamepadButtonTable subset (L_JOY/R_JOY/
    // STICK_UNBOUND); resolved the same way as Button/Alternate_Button.
    int32_t weaponSelectionStick = -1;
    std::vector<ControlSchemeControl> controls;  // Controls/Control
    std::vector<ControlSchemeAxis> axes;         // Axes/Axis
    // NOTE: the per-row Driving/On_Foot/Satellite/General classification (3)
    // is deliberately NOT modelled here - see the file banner.
};
ControlSchemeControl ParseControlSchemeControl(const Node* row);
ControlSchemeAxis ParseControlSchemeAxis(const Node* row);
ControlScheme ParseControlScheme(const Node* row);
std::vector<ControlScheme> ParseControlSchemesTable(const Document& doc);

// ===========================================================================
// 6. qte.xtbl - global QTE reward parameters (spec-tables-ui-controls.md 6)
// ===========================================================================
// Trivial single-row table: <root><Table><QTE><Name>QTE</Name>...
struct QteGlobalParams {
    Always<float> hudTime;      // Hud_Time - RAW seconds; the engine multiplies
                                 // by a global scale constant (DAT_012A2D90,
                                 // not identified - spec Open Item 8) and
                                 // rounds - a load-time transform, not applied here
    Always<float> cooldownTime; // Cooldown_Time - RAW seconds as authored; the
                                 // engine stores an offset relative to the
                                 // converted Hud_Time, not this value directly
                                 // (load-time transform, not applied here)
    Always<int32_t> respect;              // Respect (engine clamps >= 0)
    Always<int32_t> maxLifetimeRespect;   // Max_Lifetime_Respect (engine clamps >= 0)
    Always<float> cash;                    // Cash (engine clamps >= 0.0)
};
QteGlobalParams ParseQteGlobalParams(const Node* row);
// Single-block convenience parser - the file has exactly one <QTE> row.
std::optional<QteGlobalParams> ParseQteTable(const Document& doc);

// ===========================================================================
// 7. qte_sequences.xtbl - QTE state-machine sequences
// (spec-tables-ui-controls.md 7)
// ===========================================================================
// NOTE (7.3): the spec's original "167 nodes crossing the 160 cap" claim was
// RETRACTED - real, loader-fed data (root's <Table> children only, NOT
// <TableTemplates>) is 26 rows / 90 nodes, nowhere near either capacity
// figure. Document::table() already returns only <Table>'s own children, so
// this reader is unaffected by the retracted claim either way.

// One slot of QTE_Node's NPC_Animations list (+0x78-0xD7, 7.2). See the file
// banner's JUDGEMENT CALLS note: the exact child-tag shape here is inferred
// by symmetry with the node's own top-level field names, not independently
// spelled out by the spec for THIS specific sub-element.
struct QteNodeNpcAnimation {
    std::optional<std::string> npcName;           // matched-slot selector, inferred as NPC_Name (cf. Animated_NPC/NPC_Name, 7.1)
    std::optional<std::string> personaLine;        // Persona_Line (line id, resolved via FUN_0070A2F0 - raw text kept)
    std::optional<int32_t> lineDelay;              // Line_Delay
    std::optional<std::string> enterActionAnim;    // Enter_Action (per-NPC override; distinct from the node's own Enter_Action wrapper block)
    std::optional<std::string> loopingStateAnim;   // Looping_State (per-NPC override)
    std::optional<std::string> additiveActionAnim; // Additive_Action (per-NPC override)
};

// QTE_Node record, 240 (0xF0) bytes (7.2). Global capacity across ALL
// sequences: 160 (0xA0); per-sequence capacity 24 (7.1) - neither cap is
// enforced by this reader (raw parse convention; see 7.3's retraction).
struct QteNode {
    std::optional<std::string> nodeName;     // Node_Name (row key; CRC-32 seed 0xFFFFFFFF, 1.4/7.2)
    std::optional<std::string> hudInterface; // HUD_Interface (join key into hud_qte_interface.xtbl's Name, same seed, 7.2/9.2)
    std::optional<int32_t> failTime;         // Fail_Time (if-present)

    std::optional<std::string> personaLine;         // Persona/Persona_Line (line id via FUN_0070A2F0; raw text)
    Always<int32_t> personaLineDelay;                 // Persona/Line_Delay
    std::optional<std::string> partnerPersonaLine;      // Partner_Persona/Persona_Line
    Always<int32_t> partnerPersonaLineDelay;              // Partner_Persona/Line_Delay

    std::optional<std::string> cameraShake;  // Camera_Shake -> FUN_0057BEE0 resolver (raw text; cross-refs spec-tables-weapons-combat.md 1.6)

    std::optional<float> healthChange;       // Health_Change (if-present)
    std::optional<int32_t> playerImpactDmg;  // Player_Impact_Dmg (if-present)

    std::optional<std::string> enterMeleeMove;         // Enter_Action/Synced_Melee_Move/Melee_Move (id via FUN_009828F0; raw text)
    Always<bool> enterMeleeMovePlayerIsAttacker;          // Enter_Action/Synced_Melee_Move/Player_Is_Attacker
    std::optional<std::string> enterPlayerAnimation;       // Enter_Action/Player_Animation (id via FUN_004BF810; raw text)

    std::optional<std::string> loopingSyncedAnimation;      // Looping_State/Synced_State/Synced_Animation (via FUN_004BF810)
    Always<bool> loopingSyncedPlayerIsAttacker;                // Looping_State/Synced_State/Player_Is_Attacker
    std::optional<std::string> loopingPlayerAnimationState;     // Looping_State/Player_Animation_State (via FUN_004BF810)

    std::optional<std::string> additiveMeleeMove;         // Additive_Action/Synced_Melee_Move/Melee_Move (via FUN_009828F0)
    Always<bool> additiveMeleeMovePlayerIsAttacker;          // Additive_Action/Synced_Melee_Move/Player_Is_Attacker
    std::optional<std::string> additivePlayerAnimation;       // Additive_Action/Player_Animation (via FUN_004BF810)
    std::optional<uint32_t> additiveNewFailTime;                // Additive_Action/New_Fail_Time (if-present)
    std::optional<int32_t> additiveActionCount;                   // Additive_Action/Action_Count (if-present, spec-stated default 0)
    int32_t AdditiveActionCountOrDefault() const { return additiveActionCount.value_or(0); }

    bool shootingModePresent = false;  // Additive_Action/Shooting_Mode - presence flag (1 if the child exists)
    Always<float> shootingModeRecoil;    // Additive_Action/Shooting_Mode/Recoil (f32, always)
    std::optional<std::string> shootingModeSyncedPartnerFlinch;  // .../Shooting_Mode/Synced_Partner_Flinch (via FUN_004BF810)
    // .../Shooting_Mode/Restrict_Camera_Angle/{Min,Max}_{Heading,Pitch} - f32 x4, always, radians AS AUTHORED.
    Always<float> restrictCameraMinHeading, restrictCameraMaxHeading;
    Always<float> restrictCameraMinPitch, restrictCameraMaxPitch;
    // .../Shooting_Mode/Target_Camera_Angle/{Min,Max}_{Heading,Pitch} - f32 x4,
    // always, RAW DEGREES as authored; the engine's deg->rad multiply
    // (x DAT_012A30D8) is a load-time transform, not applied here.
    Always<float> targetCameraMinHeadingDeg, targetCameraMaxHeadingDeg;
    Always<float> targetCameraMinPitchDeg, targetCameraMaxPitchDeg;

    std::vector<QteNodeNpcAnimation> npcAnimations;  // NPC_Animations/NPC_Animation (capacity 4; extras kept here regardless)

    Always<bool> onSuccessInterrupt;    // On_Success/Interrupt
    Always<bool> onSuccessKeepHud;      // On_Success/Keep_HUD
    Always<bool> onSuccessKillPartner;  // On_Success/Kill_Partner
    Always<int32_t> onSuccessRepeat;      // On_Success/Repeat
    std::optional<std::string> onSuccessGoToNode;  // On_Success/Go_To_Node (hashed node-name reference, seed 0xFFFFFFFF; raw text)
    std::optional<std::string> onFailGoToNode;      // On_Fail/Go_To_Node (same)
    std::optional<bool> onFailForceSuccess;           // On_Fail/Force_Success (if-present)
    Always<bool> onFailKeepHud;                         // On_Fail/Keep_HUD

    std::optional<std::string> triggeredExplosion;  // Triggered_Explosion -> FUN_00590D20 resolver (raw text; cross-refs weapons-combat 1.6)
};

struct QteSequence {  // <QTE> row, 128 (0x80) bytes
    std::optional<std::string> name;  // Name (row key; CRC-32 seed 0xFFFFFFFF)
    Always<bool> disablePlayer;         // Disable_Player
    Always<bool> viewRemotely;            // View_Remotely
    std::optional<std::string> playerWeapon;  // Player_Weapon -> FUN_00B81220 weapons-array resolver (raw text)
    // [OPEN: numeric u16 vs name - spec-tables-ui-controls.md 7.1/7.2, NEEDS-EXE; see the .cpp]
    std::optional<uint16_t> successAnimation;  // Succes_State/Synced_Animation (NOTE: "Succes" - one 's' - is the
                                                // actual, confirmed-real XML tag; not a transcription error, 7.1)
    Always<bool> successPlayerIsAttacker;        // Succes_State/Player_Is_Attacker
    std::vector<std::string> animatedNpcNames;     // Animated_NPCs/Animated_NPC/NPC_Name (capacity 4; extras kept here regardless)
    std::vector<QteNode> nodes;                      // QTE_Nodes/QTE_Node (per-sequence capacity 24; extras kept here regardless)
};
QteNodeNpcAnimation ParseQteNodeNpcAnimation(const Node* row);
QteNode ParseQteNode(const Node* row);
QteSequence ParseQteSequence(const Node* row);
std::vector<QteSequence> ParseQteSequencesTable(const Document& doc);

// ===========================================================================
// 8.1 vehicle_cameras.xtbl / vehicle_group_cameras.xtbl - vehicle-info-table
// camera block (spec-tables-ui-controls.md 8.1). Extends
// spec-vehicle-data.md 7.3's own struct (NOT re-derived here); this models
// only the isolated field slice THIS spec documents, parsed from a
// <Vehicle>/<Vehicle_Group> row's own children/attributes.
// ===========================================================================
struct VehicleCameraFields {
    std::optional<float> cameraProximityLockY;  // Camera_Proximity_Lock_Y (+0x624, if-present)

    // The "three camera-angle sets" (dispatched to FUN_00AC5CF0, selectors
    // 0/1/2) - internal field layout is OPEN in the spec (8.1/17.2); only
    // element PRESENCE is modelled.
    bool primaryCameraAnglePresent = false;    // Primary_Camera_Angle
    bool secondaryCameraAnglePresent = false;  // Secondary_Camera_Angle
    bool rcCameraAnglePresent = false;         // RC_Camera_Angle

    // Position_Based and Sphere_Based are mutually exclusive (8.1); +0x870
    // bit 0x40000000 selects between them at load time (set = Sphere_Based,
    // cleared = Position_Based; inherited from the group row when NEITHER
    // block is present here - a cross-row behaviour this struct cannot
    // resolve alone, so it is not modelled as a derived bool).
    bool positionBasedPresent = false;
    std::optional<float> positionSwingRateFwd;  // Position_Based/Swing_Rate_Fwd (+0x6EC, if-present; absent -> engine
                                                 // RNG-draws clamped [0,1), NOT a fixed default - not modelled)
    std::optional<float> positionSwingRateRev;  // Position_Based/Swing_Rate_Rev (+0x6FC)
    std::optional<float> positionFromStickyFwd; // Position_Based/From_Sticky_Fwd (+0x70C; defaults to Swing_Rate_Fwd
                                                 // if absent, else independently RNG-drawn - neither applied here)
    std::optional<float> positionFromStickyRev; // Position_Based/From_Sticky_Rev (+0x710)
    std::optional<float> positionHeadingRetentionNormal;   // Position_Based/Heading_Retention_Normal (+0x6F0, default 1.0)
    std::optional<float> positionHeadingRetentionTurning;  // Position_Based/Heading_Retention_Turning (+0x6F4, default 1.0)
    float PositionHeadingRetentionNormalOrDefault() const { return positionHeadingRetentionNormal.value_or(1.0f); }
    float PositionHeadingRetentionTurningOrDefault() const { return positionHeadingRetentionTurning.value_or(1.0f); }

    bool sphereBasedPresent = false;
    Always<float> sphereSlerpValue;                    // Sphere_Based/Slerp_Value (+0x700, f32, always)
    std::optional<float> sphereFromStickySlerpValue;    // Sphere_Based/From_Sticky_Slerp_Value (+0x704; defaults to Slerp_Value)
    std::optional<float> sphereMinAngleDeg;             // Sphere_Based/Min_Angle (+0x708, if-present, RAW DEGREES as
                                                         // authored; engine deg->rad multiply not applied here; presence
                                                         // sets +0x870 bit 0x80000000 - not modelled, derived)

    std::optional<std::string> cameraRollTypeText;   // Camera_Roll/Roll_Type (+0xAF0; vehicle-mode only, groupMode=0)
    // "bool: true iff the text differs from the literal default 'none'" (8.1) -
    // a direct, spec-stated derivation of the raw text above.
    bool CameraRollTypeIsCustom() const { return cameraRollTypeText.has_value() && !sr3xtbl::NameEquals(*cameraRollTypeText, "none"); }
    std::optional<float> cameraRollIntensityMultiplier;  // Camera_Roll/Intensity_Multiplier (+0xAF4, if-present)

    // Row ATTRIBUTES (not child elements) - "yes" (case-insensitive; the
    // spec's own exact-casing is not stated - see the .cpp).
    // [OPEN: attribute vs child element - spec-tables-ui-controls.md 8.1, NEEDS-EXE; see the .cpp]
    bool skipCameraTransition = false;  // skip_camera_transition == "yes" (+0x870 bit 0x8000)
    bool useAltFreckleCam = false;      // use_alt_freckle_cam == "yes" (+0x870 bit 0x2000000; inherited from the
                                        // group row when absent, vehicle mode - not modelled, cross-row)

    std::optional<float> cameraFovScale;  // camera_fov_scale (+0x860, if-present, default 1.0, engine clamps >= 0)
    float CameraFovScaleOrDefault() const { return cameraFovScale.value_or(1.0f); }
    Always<float> maxFov;  // Max_FOV (+0x864, f32, always - vehicle-mode only; group-fill mode leaves it untouched)
    std::optional<float> minFollowDistMultiplier;  // Min_Follow_Dist_Multiplier (+0x868, if-present, default 1.0)
    float MinFollowDistMultiplierOrDefault() const { return minFollowDistMultiplier.value_or(1.0f); }
};
struct VehicleCameraRow {
    std::optional<std::string> name;  // row key - assumed "Name" (see file banner's JUDGEMENT CALLS)
    VehicleCameraFields camera;
};
VehicleCameraFields ParseVehicleCameraFields(const Node* row);
VehicleCameraRow ParseVehicleCameraRow(const Node* row);
std::vector<VehicleCameraRow> ParseVehicleCamerasTable(const Document& doc);       // <Vehicle> rows, groupMode=0
std::vector<VehicleCameraRow> ParseVehicleGroupCamerasTable(const Document& doc);  // <Vehicle_Group> rows, groupMode=1

// ===========================================================================
// 8.2 item_cust_cameras.xtbl / player_cust_cameras_new.xtbl /
// vehicle_cust_cameras_new.xtbl - customization-viewer camera presets
// (spec-tables-ui-controls.md 8.2). "Empirically identical schema across all
// three files" - one shared reader below. Field-level always/if-present
// accessor type is NOT individually stated per field by the spec for this
// table (unlike most others in this group); Always<T> (the "usual" family
// per the file banner) is used throughout.
// ===========================================================================
struct CustCameraView {  // camera/camera_view_list/camera_view
    std::optional<std::string> name;  // Name
    Always<float> distance;             // distance
    Always<float> height;                // height
    Always<float> subjHeading;            // subj_heading
    Always<float> camHeading;              // cam_heading
    Always<float> pitch;                    // pitch
    Always<float> fieldOfView;                // field_of_view
    Always<bool> snap;                          // snap
    // <standard_def/> - an empty marker element ("roughly every second
    // camera_view"); paired HD/SD variant of the same named view (8.2).
    bool standardDef = false;
};
struct CustCameraSet {  // <camera> row
    std::optional<std::string> name;   // Name
    std::vector<CustCameraView> views;   // camera_view_list/camera_view
};
CustCameraView ParseCustCameraView(const Node* row);
CustCameraSet ParseCustCameraSet(const Node* row);
// Shared reader for item_cust_cameras.xtbl, player_cust_cameras_new.xtbl and
// vehicle_cust_cameras_new.xtbl - all three share this exact schema (8.2).
std::vector<CustCameraSet> ParseCustCamerasTable(const Document& doc);

// ===========================================================================
// 9. hud_qte_interface.xtbl + hud_qte_interface_presets.xtbl - QTE on-screen
// prompt layout (spec-tables-ui-controls.md 9). Both capacity-gated at 16
// (0x10) rows: "if either file's row count reaches 16, the entire load is
// refused" - not enforced by this reader (raw parse convention).
// ===========================================================================

// 9.1 Hud_qte_preset record, 80 (0x50) bytes. Row tag per the file banner's
// JUDGEMENT CALLS note.
struct HudQtePreset {
    std::optional<std::string> name;  // Name (char[64], name-cache 0x0129EE6C)
    Always<int32_t> xPosition;          // X_Position
    Always<int32_t> yPosition;            // Y_Position
    Always<int32_t> xPositionSD;            // X_Position_SD
    Always<int32_t> yPositionSD;              // Y_Position_SD
};
HudQtePreset ParseHudQtePreset(const Node* row);
std::vector<HudQtePreset> ParseHudQteInterfacePresetsTable(const Document& doc);

// 9.2 Hud_qte record, 56 (0xE-dword) bytes. Row tag per the file banner's
// JUDGEMENT CALLS note.
struct HudQteInterface {
    std::optional<std::string> name;  // Name (row key; CRC-32 seed 0xFFFFFFFF - joins qte_sequences.xtbl's HUD_Interface, 7.2/9.2)
    int32_t buttonType = -1;            // Button_Type -> kQteButtonTypeNames (9-entry, EnumIndex)
    int32_t buttonAnimationType = -1;     // Button_Animation_Type -> kButtonAnimationTypeNames (3-entry, EnumIndex)
    std::optional<std::string> buttonAction;  // Button_Action -> CBA table (raw text; second copy of the same 4.1 table)
    // Axis_Action -> CAA table (raw text; second copy of 4.2). NOTE: real
    // data's placeholder text "CAA_AXIS_UNBOUND" is NOT a real table entry -
    // it simply fails every comparison and falls through to the same "no
    // match" result a genuine sentinel would give (9.2); this reader keeps
    // whatever text is authored, unmodified.
    std::optional<std::string> axisAction;
    Always<bool> axisDirPos;               // Axis_Dir_Pos
    std::optional<std::string> position;     // Position -> hud_qte_interface_presets.xtbl Name (exact stricmp lookup)
    std::optional<std::string> positionPC;     // Position_PC -> same lookup, a separate PC-specific slot
};
HudQteInterface ParseHudQteInterface(const Node* row);
std::vector<HudQteInterface> ParseHudQteInterfaceTable(const Document& doc);

// ===========================================================================
// 10. user_interface.xtbl - UI cluster/resolution layout
// (spec-tables-ui-controls.md 10). Schema CONFIRMED - empirical; byte-offset
// layout OPEN (the four downstream fill functions were not decompiled -
// spec Open Item 6). This models the element-TREE SHAPE only, reconstructed
// from the spec's own tag-chain prose (see the file banner's JUDGEMENT
// CALLS). Numeric field WIDTH (int vs float) is not stated by the spec for
// this table at all; float is used throughout as the generic default.
// ===========================================================================
struct UiSlotOffset {  // BitmapSlots/Part/SlotList/SlotOffset
    std::optional<std::string> resolutionRatio;  // ResolutionRatio
    Always<float> xOffset, yOffset;
    Always<float> scale;
    Always<float> alpha;
};
struct UiPart {  // UIClusters/Cluster/BitmapSlots/Part
    std::optional<std::string> name;    // Name
    std::vector<UiSlotOffset> slots;      // SlotList/SlotOffset
    Always<float> xOffset, yOffset;         // Part's own direct XOffset/YOffset (distinct from per-slot values)
    Always<float> scale;
    Always<float> alpha;
};
struct UiClusterOffset {  // UIClusters/Cluster/ClusterOffsetList/ClusterOffset
    std::optional<std::string> resolutionRatio;  // ResolutionRatio
    Always<float> xOffset, yOffset;
};
struct UiCluster {  // UIClusters/Cluster
    std::optional<std::string> clusterName;  // ClusterName
    Always<float> xPosition, yPosition;         // XPosition, YPosition
    std::vector<UiClusterOffset> offsets;          // ClusterOffsetList/ClusterOffset
    std::vector<UiPart> bitmapParts;                 // BitmapSlots/Part
};
struct UserInterfaceRow {
    std::optional<std::string> name;    // Name (row selector; PC build reads the row whose Name == "XBox2" - 10)
    std::vector<std::string> resolutions; // ResolutionList/Resolutions/Resolution (text labels, e.g. "4:3 (640x480)")
    std::vector<UiCluster> clusters;        // UIClusters/Cluster
};
UiSlotOffset ParseUiSlotOffset(const Node* row);
UiPart ParseUiPart(const Node* row);
UiClusterOffset ParseUiClusterOffset(const Node* row);
UiCluster ParseUiCluster(const Node* row);
UserInterfaceRow ParseUserInterfaceRow(const Node* row);
std::vector<UserInterfaceRow> ParseUserInterfaceTable(const Document& doc);
// Convenience: the row the PC build actually uses (Name == "XBox2", 10) -
// nullptr if not found in `rows`.
const UserInterfaceRow* FindActiveUserInterfaceRow(const std::vector<UserInterfaceRow>& rows);

// ===========================================================================
// 11. control_scheme_text.xtbl - on-screen control-legend text
// (spec-tables-ui-controls.md 11). Deeper field-offset mapping past row
// selection was not pursued by the spec (Open Item 7) - schema is fully
// confirmed from real data regardless.
// ===========================================================================
struct ControlSchemeTextEntry {  // <ControlScheme>/Controls/Control
    std::optional<std::string> control1;  // Control1 (a display token, e.g. "R1", "L2")
    std::optional<std::string> locDesc;     // LocDesc (a localization string KEY, e.g. CONTROL_DESC_GRENADE -
                                             // the resolving system is not identified by the spec; raw key kept)
    std::optional<std::string> platform;      // Platform ("360"/"PS3"/presumably others)
};
struct ControlSchemeText {  // <ControlScheme> row (note: NO underscore - distinct from
                             // control_schemes.xtbl's <Control_Scheme>, spec 11 vs 3)
    // Name is matched against a hardcoded array of expected literals (e.g.
    // "Scheme A - On Foot") rather than accepted freely (11) - that literal
    // list is NOT enumerated by the spec beyond one example, so (like the
    // CBA/CAA/key-name tables) it is kept as raw text, not a lookup table.
    std::optional<std::string> name;
    std::vector<ControlSchemeTextEntry> controls;  // Controls/Control
};
ControlSchemeTextEntry ParseControlSchemeTextEntry(const Node* row);
ControlSchemeText ParseControlSchemeText(const Node* row);
std::vector<ControlSchemeText> ParseControlSchemeTextTable(const Document& doc);

// ===========================================================================
// 12. voice_control.xtbl - AI voice-bark trigger/cooldown configuration
// (spec-tables-ui-controls.md 12). NOT a player-input table despite the
// `control_` group context - see the spec's own naming note (12); grouped
// here only because the assignment brief named it explicitly.
// ===========================================================================
struct VoiceControlEntry {  // <Entry> row
    Always<int32_t> voicelineId;      // Voiceline_id (the row's numeric identity, read before the bitfield is built)
    Always<int32_t> localCooldownMs;    // Local_cooldown - RAW milliseconds as authored (bits 0-5 of the packed
                                         // word; engine: ms/1000, clamped to 63 - see LocalCooldownField())
    Always<int32_t> globalCooldownMs;     // Global_cooldown - RAW ms (bits 6-9; engine: ms/1000, clamped to 15)
    Always<int32_t> minDelayRaw;            // Min_delay - RAW value (bits 10-14; engine: raw/70, clamped to 31)
    Always<int32_t> maxDelayRaw;              // Max_delay - RAW value (bits 15-20; engine: (Max_delay-Min_delay)/80, clamped to 63)
    Always<int32_t> playPercent;                // Play_percent - RAW value (bits 21-22; engine buckets into 4 tiers at 26/51/76)
    std::optional<int32_t> priority;              // Priority (if-present, spec-stated default 10; bits 23-27, 5-bit field)
    int32_t PriorityOrDefault() const { return priority.value_or(10); }
    std::optional<uint32_t> externalSource;  // External_source (if-present; registered with the audio subsystem only
                                              // when a debug/registration flag is set - not modelled, engine-side condition)
    std::optional<uint32_t> playEvent;         // Play_event (if-present; same registration condition)

    // Load-time-computed bitfield helpers reproducing the spec's own fully
    // CONFIRMED (disassembly of every shift/mask/clamp constant, 12) packing
    // formula. The packed u32 itself is not an XML element and is not stored
    // - these are pure functions of the raw fields above.
    uint32_t LocalCooldownField() const;   // clamp(localCooldownMs/1000, 0, 63)
    uint32_t GlobalCooldownField() const;  // clamp(globalCooldownMs/1000, 0, 15)
    uint32_t MinDelayField() const;        // clamp(minDelayRaw/70, 0, 31)
    uint32_t MaxDelayField() const;        // clamp((maxDelayRaw - minDelayRaw)/80, 0, 63) - uses the RAW Min_delay,
                                            // NOT MinDelayField()'s already-packed value (12's own formula)
    uint32_t PlayPercentTier() const;      // 0..3, bucketed at thresholds 26/51/76 (edge inclusivity at the exact
                                            // threshold values is not specified by the spec; this uses "< threshold")
    uint32_t PriorityField() const;        // PriorityOrDefault() clamped/masked to 5 bits (0-31)
};
VoiceControlEntry ParseVoiceControlEntry(const Node* row);
std::vector<VoiceControlEntry> ParseVoiceControlTable(const Document& doc);

// ===========================================================================
// 13. credits_pc.xtbl - credits screen content
// (spec-tables-ui-controls.md 13). Builds nested lists (section -> heading ->
// item) from a growable pool - "no capacity ceiling applies to this table"
// (13); std::vector already has none. NOTE: the spec documents a real,
// confirmed load-time data correction ("Neil Heilmann" silently rewritten to
// "Nick Heilmann") that this reader does NOT apply - the raw authored text
// is kept, consistent with every other load-time transform in this file.
// ===========================================================================
struct CreditsItem {  // items/items (note the DOUBLED tag name - the wrapper
                       // element and each repeated item share the literal
                       // name "items", exactly as spec 13's prose shows)
    std::optional<std::string> name;          // name
    std::optional<std::string> desc;            // desc
    int32_t typeOverride = -1;                    // type_override -> kCreditsItemTypeNames (overrides the parent heading's type)
};
struct CreditsHeading {  // headings/headings (same doubled-tag-name shape as items/items)
    std::optional<std::string> heading;  // heading (a sub-heading label)
    int32_t type = -1;                     // type -> kCreditsItemTypeNames
    std::vector<CreditsItem> items;          // items/items
};
struct CreditsSection {  // <Credits> row
    std::optional<std::string> name;           // Name
    std::optional<std::string> sectionTitle;     // section_title
    std::vector<CreditsHeading> headings;          // headings/headings
};
CreditsItem ParseCreditsItem(const Node* row);
CreditsHeading ParseCreditsHeading(const Node* row);
CreditsSection ParseCreditsSection(const Node* row);
std::vector<CreditsSection> ParseCreditsTable(const Document& doc);

// ===========================================================================
// 14. control_filters.xtbl + control_parameters.xtbl - animation blend-tree
// control-filter graph (spec-tables-ui-controls.md 14). NOT player-input
// tables despite the name - see the spec's own naming note (14); their
// natural home is animation-table territory (spec-tables-animation.md), not
// modelled further here. Both files use the non-<root><Table>-wrapped shape
// (spec-xtbl-format.md 8's "10 others" family) - rows are read from
// doc.root(), NOT doc.table() (which would return nullptr for these two).
// The shared loader FUN_005D25F0 was located but NOT decompiled to field
// level (spec Open Item 5) - the schema below is the spec's own EMPIRICAL
// description of real file content, not a byte-offset-verified struct.
// ===========================================================================
struct ControlFilter {  // <root><control_filters><control_filter>
    std::optional<bool> validated;         // validated
    std::optional<std::string> name;         // name
    std::optional<std::string> type;          // type ("linear map" / "delta multiplier" / "cap")
    std::optional<std::string> sourceName;     // source_name - references another filter's own `name`, forming a chain
    // Fields present depending on `type` (14):
    std::optional<float> inputMin, inputMax;    // linear map: input_min/input_max
    std::optional<float> outputMin, outputMax;   // linear map AND cap: output_min/output_max (same element names, different `type`)
    std::optional<float> multiplier;               // delta multiplier: multiplier
};
ControlFilter ParseControlFilter(const Node* row);
std::vector<ControlFilter> ParseControlFiltersTable(const Document& doc);

struct ControlParameter {  // <root><control_parameters><control_parameter>
    std::optional<std::string> name;        // name
    std::optional<std::string> dataType;      // data_type ("bool"/"float"/"integer")
    // default_value's TEXT interpretation depends on data_type; kept raw
    // (not type-converted) rather than guessing a per-type parse the spec
    // does not itself specify.
    std::optional<std::string> defaultValue;
    std::optional<float> minValue, maxValue;  // min_value/max_value (numeric types only)
};
ControlParameter ParseControlParameter(const Node* row);
std::vector<ControlParameter> ParseControlParametersTable(const Document& doc);

}  // namespace sr3tables_ui_controls
