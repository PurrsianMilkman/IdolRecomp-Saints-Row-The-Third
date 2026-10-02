#pragma once

// sr3tables_animation - typed readers for the animation data table group
// (`anim_*.xtbl` + creation lipsync / visemes).
//
// Source: spec-tables-animation.md (SPEC TEAM, agent AN, Phase 2 "schema-
// from-loader" campaign), the ONLY source consulted for this file's schema
// (cleanroom boundary - see the HARD RULES in the task that produced this
// file). Built entirely on top of include/sr3xtbl/xtbl.h's document model
// and accessors (Node, FindChild, Children, ChildText, CopyText, Always<T>,
// GetX/ReadXAlways, ReadVec3Child, FlagMask, EnumIndex); nothing here was
// derived from the game executable, disassembly or decompiled code - every
// offset/address/function name cited in a comment is copied verbatim from
// the spec's own prose, purely to let a reader cross-reference the spec
// section, never re-derived.
//
// CONVENTION (matches sr3tables_environment/tables.h exactly - see its
// banner):
//   * sr3xtbl::Always<T> (value + present) = the spec's "always write"
//     elements. When !present the ENGINE's real behaviour is an
//     indeterminate stack-residue value (see xtbl.h's own warning), NOT
//     Always<T>'s 0 stand-in.
//   * std::optional<T> = the spec's "write only if present" / "if present"
//     elements, and EVERY text field (this project's convention throughout:
//     there is no Always<std::string> anywhere - see sr3tables_environment).
//   * A field with a spec-STATED CONCRETE default distinct from both of the
//     above gets an `...OrDefault()` helper (none of this group's fields
//     carry one - unlike sr3tables_environment, this spec never states a
//     numeric default other than the Always-hazard 0 or an explicit
//     "if/else chain -> zero default", see the anim_flinches.xtbl note
//     below).
//
// JUDGEMENT CALL, flagged exactly like sr3tables_environment/camera_free.h's
// own precedent: `anim_files.xtbl`'s per-item IK/Sound fields (Enable,
// Frame, Blend, Replay_On_Cycle_Restart, Stop_When_Anim_Stops) and
// `anim_blend_trees.xtbl`'s `Action` fields `Always_cap`/`Speed_throttling`
// (7.2, added 2026-10-02) are the ONLY bool/int scalars in this whole spec
// that carry NO explicit "always"/"if present" accessor annotation (contrast
// e.g. `Action`'s own `Cap_ramp_min`/`Cap_ramp_max` in 7.2, EXPLICITLY "if
// present" with a stated default, or Morphed_Surface_Max_Adjustment in 8.2,
// EXPLICITLY "always"). Modelled here as Always<T>, matching this project's
// general finding that bare engine scalars default to the always-write
// family - NOT a spec-stated fact for these specific fields, called out
// here and again in the report. (`Animation/Preload`, formerly in this
// list, was RE-DERIVED 2026-10-01: it is read by a DIFFERENT reader - func
// `0x008C7F80`, not this struct's primary reader `FUN_004AE7A0` - "as a bool
// defaulting to false"; moved to `AnimFile::preload` as `std::optional<bool>`
// + `PreloadOrDefault()`, see 3.2. `Control_input_low`/`Control_input_high`/
// `Ramp_speed`/`Blend_time`/`Control_point`'s `Range`/`Value`, formerly cited
// here as "if present" contrast examples, were RE-DERIVED 2026-10-02 to all
// be "always" readers too - modelled as `Always<T>`, not a judgement call.)
//
// CROSS-TABLE REFERENCES (spec 16): every field that the spec says is
// "resolved" against another table or a runtime hashtable (anim_files.xtbl's
// Triggers/IKs-Situation/anim_blend_trees.xtbl's Name, State/Animation AND
// Action/Animation (the latter's miss path HANGS the original loader - spec
// 7's WARNING, re-derivation 2026-10-02 - never reproduced here, see
// AnimBlendTreeAction::animationFilename)/anim_correction_offsets.xtbl's
// File/anim_synced.xtbl's *Anim/anim_transitions.xtbl's *_State+Action/
// anim_flinches.xtbl's Name/creation_lipsync_animations.xtbl's Animation/
// character_visemes.xtbl's Target_Name) is kept here as the RAW text the row
// carries, never as a resolved index or hash - the resolution is a
// load-time, cross-file operation this reader does not perform, exactly as
// sr3tables_environment leaves SkyboxEffect's Name "-> CRC-32 key (derived;
// not stored here)".
//
// WHAT IS DELIBERATELY LEFT OUT (per the task's "don't guess" rule):
//   * `anim_set_properties.xtbl` and `anim_prop_sets.xtbl` are SKIPPED
//     ENTIRELY (spec 15): both occupy a slot in the exe's 16-slot table-
//     dependency registry and pass its completeness check, but an
//     EXHAUSTIVE cross-reference scan (spec 15.1) found no function
//     anywhere that ever opens either file - "no literal consumer, no
//     loader, skip" is this project's standing rule for such a case (the
//     same situation as sr3tables_environment's materials.xtbl / traffic-
//     ai's traffic_types.xtbl, cited by that project's own precedent). 14 of
//     the 16 named tables are implemented here.
//   * The 34 compiled-in `anim_ik_situation.xtbl` presets (spec 8.1) are NOT
//     XML data at all (a literal table baked into the exe, filled before the
//     file is even opened) - out of scope for an XML-table reader by
//     construction, not modelled.
//   * Derived/computed values are not modelled: a row's resolved CRC-32 (or
//     rotate/XOR, `anim_triggers.xtbl`) hash, the `anim_blend_trees.xtbl`/
//     `anim_correction_offsets.xtbl`/`anim_synced.xtbl` cross-table index
//     resolution, `anim_correction_offsets.xtbl`'s second derived 4-float
//     block (spec 12.2, RE-DERIVED 2026-10-02: `FUN_00DA38E0` widens the
//     authored vec3 to `(X, Y, Z, Z)`, CONFIRMED NOT a quaternion and reads
//     no further element - still not computed here, only the raw `Offset`
//     vec3 is kept), the IK `Blend` divisor (spec 3.2, RE-DERIVED
//     2026-10-01: `30.0 / Blend`, `0.0` if `Blend` == 0, the constant at
//     `DAT_012A2D58` now CONFIRMED 30.0 - still not computed here, only the
//     raw int is kept), and `character_visemes.xtbl`'s `Target_Value` scale
//     constant (spec 14.1, RE-DERIVED 2026-10-02: CONFIRMED a `x 0.01`
//     double-precision multiply by `DAT_01117DC8` - still not computed
//     here, only the raw authored percentage is kept).
//   * Purely decorative, never-visited-by-any-code-path fields are omitted
//     entirely, matching sr3tables_environment's own `_Editor` precedent:
//     `anim_actions.xtbl`'s `_Editor`, `anim_correction_offsets.xtbl`'s
//     `Name` and `_Editor` (spec 12.4: "never read by the traced code",
//     with no consumer even hypothesised elsewhere - unlike the fields kept
//     below).
//   * Fields the spec marks OPEN for their CONSUMER (not their existence)
//     are still surfaced raw, exactly per the task's confidence-discipline
//     rule: `anim_set_filenames.xtbl`'s Path/Default_model/Default_rig
//     (spec 2.3: present in real data, unread by the traced loader, but
//     explicitly hypothesised to feed "a customization/creation-mode
//     default-appearance lookup" elsewhere), `anim_files.xtbl`'s
//     `Animation/Preload` (spec 3.2, RE-DERIVED 2026-10-01: read by a
//     second reader, func `0x008C7F80`/`FUN_008C9FD0`, not this struct's
//     primary reader `FUN_004AE7A0` - "as a bool defaulting to false"; the
//     installing/consuming subsystem for that second reader is still OPEN)
//     and `Voice_Lines`/`misc`
//     presence (spec 3.2/3.5: `Voice_Lines` is confirmed always-empty and
//     never read - kept ONLY as a presence bit to document this "doubly-
//     dead field" finding, not because it carries data; `misc` is a real
//     generic extension hook whose installer is OPEN).
//
// A GENUINE SPEC QUIRK, not smoothed over here either: `anim_transitions.xtbl`
// (spec 6.3) ships with its `<Table>` element structurally EMPTY in the base
// game - every authored `<Anim_Transition>` row lives under the sibling
// `<TableTemplates>` element instead, which the real loader (and this
// reader, matching it exactly) never visits. ParseAnimTransitionsTable()
// therefore returns an empty vector on the real base-game file - this is
// the CORRECT, confirmed behaviour, not a bug in this reader.
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> (every implemented table in this group is row-
// repeated; none are the single-block whole-file shape). All definitions
// are in src/tables_animation.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_animation {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3Result;

// ===========================================================================
// 2. anim_set_filenames.xtbl - name -> satellite .xtbl file binding (2)
// ===========================================================================
struct AnimSetFilename {
    std::optional<std::string> name;  // Name - bounded <=0x100 byte copy; the ONLY field the traced
                                        // reader (FUN_004AEA30) consumes (2.2) - the value is itself
                                        // another .xtbl filename (e.g. "anim_auto.xtbl"), concatenated
                                        // after a "tables\" prefix and opened as a second-layer file;
                                        // that satellite file's own schema was not traced (2.2, OPEN)
    std::optional<std::string> path;          // Path - present in real data, unread by the traced loader (2.3, OPEN consumer)
    std::optional<std::string> defaultModel;   // Default_model (.cmeshx) - same status (2.3, OPEN consumer)
    std::optional<std::string> defaultRig;     // Default_rig (.rigx) - same status (2.3, OPEN consumer)
};
AnimSetFilename ParseAnimSetFilename(const Node* row);
std::vector<AnimSetFilename> ParseAnimSetFilenamesTable(const Document& doc);

// ===========================================================================
// 3. anim_files.xtbl - the loaded-animation record / name->.animx binding (3)
// ===========================================================================

// The 4-value IK Location enum (spec 3.2, literal table at 0x012959C0):
// index == the engine's own value (Left Hand=0 .. Right Foot=3).
inline constexpr std::array<std::string_view, 4> kAnimFileIkLocationNames = {
    "Left Hand", "Right Hand", "Left Foot", "Right Foot",
};

// The 30-entry Flags table (spec 3.2, literal pointer table at 0x013507B0),
// in bit order (index == bit position, matching sr3xtbl::FlagMask's "first
// match wins, index selects the bit" semantics exactly - the same shared
// accessor the spec cites, FUN_00DAC740).
inline constexpr std::array<std::string_view, 30> kAnimFileFlagNames = {
    "Additive", "Block_Rotation", "Block_Translation", "Transition_Action", "Zero_Input",
    "Female_Root_Offset", "No_Interpolation", "No_Spinebending", "Anim_Movement",
    "Disable_state_audio", "Use_Shortest_Rotation", "Steering_Offset_180",
    "Ignore_camera_collision", "Use_Camera_Root_Offset", "Use_Animated_Camera", "No_IK",
    "Sitting", "Translates_In_Vehicle", "Transform_When_Offscreen", "Combat_Ready",
    "Hide_Weapon", "No_Fall", "Fat_bones_forward", "Fat_bones_disabled",
    "Disable_Controller_Actions", "Delay_Weapon_Fire", "Disable_Weapon_Fire",
    "Transition to 50%", "Invalid_synced_target", "Cloth Sim Hack",
};

struct AnimFileIk {
    Always<bool> enable;                        // Enable (no explicit accessor tag - see the header
                                                  // banner's judgement-call note)
    std::optional<std::string> location;         // Location -> kAnimFileIkLocationNames via EnumIndex
    std::optional<std::string> situation;         // Situation -> anim_ik_situation.xtbl Name (cross-table,
                                                   // raw text kept; RE-DERIVED 2026-10-01: CRC-32 hash
                                                   // compare against DAT_034248C8, stored value is the
                                                   // matching 16-byte record's ADDRESS, 0 if absent/unmatched
                                                   // - lookup/storage not performed here)
    Always<int32_t> frame;                        // Frame
    Always<int32_t> blend;                        // Blend - RAW int; the engine converts this to a float
                                                   // divisor 30.0/Blend (0.0 if Blend==0) - RE-DERIVED
                                                   // 2026-10-01: the constant at DAT_012A2D58 is CONFIRMED
                                                   // 30.0 (previously OPEN); the divisor is still
                                                   // deliberately NOT computed here, matching this file's
                                                   // "raw field in, derived value out is the caller's job"
                                                   // convention (see the header banner)
};

struct AnimFileSound {
    Always<int32_t> frame;                         // Frame
    std::optional<std::string> audioSwitch0;       // Audio_Switch_0 - "bank:event" text, split+resolved at
    std::optional<std::string> audioSwitch1;       // Audio_Switch_1 - load time (FUN_0046FD00); raw text kept
    Always<bool> replayOnCycleRestart;              // Replay_On_Cycle_Restart
    Always<bool> stopWhenAnimStops;                 // Stop_When_Anim_Stops
    std::optional<std::string> audioEvent;          // Audio_Event - resolved the same way; raw text kept
};

struct AnimFile {
    std::optional<std::string> filename;    // Animation/Filename -> the DAT_02EF2178 record key (3.2);
                                             // this is the ACTUAL name->.animx binding this group runs on
    std::optional<bool> preload;              // Animation/Preload - present in real data (True/False on
                                               // every sampled row, 3.2). RE-DERIVED 2026-10-01: NOT read by
                                               // this struct's own primary reader (FUN_004AE7A0) at all -
                                               // it is read by a SEPARATE second reader of this same file
                                               // (func 0x008C7F80/FUN_008C9FD0, Sec1.1), "as a bool
                                               // defaulting to false" (if-present semantics, NOT the
                                               // Always<T>/indeterminate-when-absent family - this was
                                               // previously modelled as Always<bool>, now corrected). Which
                                               // subsystem calls that second reader is still OPEN.
    bool PreloadOrDefault() const { return preload.value_or(false); }
    std::vector<std::string> triggerNames;   // Triggers/Trigger/Name (repeated) - each is hashed with the
                                              // rotate/XOR hash and validated against anim_triggers.xtbl's
                                              // registry (3.2/9); this is a VALIDATION PASS ONLY - "no
                                              // per-trigger data is written into the 0x44-byte Anim_file
                                              // record" (a per-trigger Frame IS forwarded to func
                                              // 0x004CAD90 though - storage target HYPOTHESIS, untraced) -
                                              // kept here as real, present XML data, not as stored engine
                                              // state
    // Shape VALIDATED-BY-DATA 2026-10-01 (job 20261001T004133-team-b-kmsh, 7,060/7,060 real rows):
    // Sec3.1's `<Trigger>` with direct text was WRONG; Sec3.2's `<Trigger><Name>...</Name>` is the
    // real shape, matching the Name-child reading below. Every real Trigger also carries a `Frame`
    // child (7,060/7,060) this struct does not yet model (RE-DERIVED 2026-10-01: read by
    // FUN_004ADED0 and forwarded to FUN_004CAD90 with the trigger index - not added here, a residual
    // gap flagged for a future pass, see the report). Every Trigger element without a Name child
    // is COUNTED here instead of being dropped silently.
    size_t triggerElements = 0;              // all Triggers/Trigger elements in this row
    size_t triggerElementsUnread = 0;        // of those, no Name child (not in triggerNames)
    std::vector<AnimFileIk> iks;             // IKs/IK (repeated)
    std::vector<AnimFileSound> sounds;       // Sounds/Sound (repeated)
    bool voiceLinesPresent = false;          // Voice_Lines - CONFIRMED always empty in the base game
                                              // (0/5,968 rows carry any child, 3.5) AND never read by the
                                              // row reader at all (3.2) - a "doubly-dead" field; this bit
                                              // exists only to document that finding, never to carry data
    bool miscPresent = false;                // misc - generic extension-point element (3.2): if present
                                              // AND a global callback pointer is installed, the engine
                                              // invokes it with this node; which subsystem installs the
                                              // callback is OPEN, so only presence is surfaced
    uint32_t flags = 0;                       // Flags/Flag bitmask via kAnimFileFlagNames (FlagMask, 3.2)
};
AnimFile ParseAnimFile(const Node* row);
std::vector<AnimFile> ParseAnimFilesTable(const Document& doc);

// ===========================================================================
// 4. anim_groups.xtbl - group name registry with parent links (4)
// ===========================================================================
struct AnimGroup {
    std::optional<std::string> name;    // Name -> hashed (CRC-32) and used as this row's registry key (4.1);
                                          // hash not computed here (derived, see the header banner)
    std::optional<std::string> parent;   // Parent (optional) -> another row's Name, by case-insensitive
                                          // match, building a tree (4.1); raw text kept, resolution not
                                          // performed here
};
AnimGroup ParseAnimGroup(const Node* row);
std::vector<AnimGroup> ParseAnimGroupsTable(const Document& doc);

// ===========================================================================
// 5. anim_states.xtbl / anim_actions.xtbl - a merged name registry (5)
// ===========================================================================
// Both are flat name-only registries, "both by design and by what the engine
// reads" (5.3): the complete element vocabulary of every real row in the
// base game is {Name} (anim_states.xtbl) / {Name, _Editor} (anim_actions.xtbl,
// _Editor omitted per this project's standing convention - never visited by
// any row reader). Nothing beyond Name is read by the one function
// (FUN_004BF8D0) that opens either file.
struct AnimState {
    std::optional<std::string> name;  // Name
};
AnimState ParseAnimState(const Node* row);
std::vector<AnimState> ParseAnimStatesTable(const Document& doc);

struct AnimAction {
    std::optional<std::string> name;  // Name
};
AnimAction ParseAnimAction(const Node* row);
std::vector<AnimAction> ParseAnimActionsTable(const Document& doc);

// ===========================================================================
// 6. anim_transitions.xtbl - the state-machine transition table (6)
// ===========================================================================
// NOTE (spec 6.3, see the header banner): the base game's own <Table> is
// structurally empty - ParseAnimTransitionsTable() over the real shipped
// file returns an empty vector, which is CORRECT and matches the real
// loader's own behaviour (it never visits the sibling <TableTemplates>
// element the authored rows actually live under).
struct AnimTransition {
    std::optional<std::string> fromState;  // From_State -> merged State/Action registry index (6.1)
    std::optional<std::string> toState;    // To_State -> same registry
    std::optional<std::string> action;     // Action -> same registry
};
AnimTransition ParseAnimTransition(const Node* row);
std::vector<AnimTransition> ParseAnimTransitionsTable(const Document& doc);

// ===========================================================================
// 7. anim_blend_trees.xtbl - 1D blend-space definitions (7)
// ===========================================================================
// Root/row shape quirk (7.1, CONFIRMED - disassembly): the per-row tag
// REPEATS the root-fetch name ("Blend_trees" is both the table's own child
// name AND each row's tag), so rows are read as direct repeated children of
// <Table>, not nested under a plural container.
//
// WARNING - infinite-loop hazard, NOT reproduced here (spec Sec7, the
// section's own WARNING block; re-derivation 2026-10-02, CONFIRMED -
// disassembly and decompile): in the REAL engine, an `Actions/Action` entry
// whose `Animation` does not resolve against `anim_files.xtbl`'s
// loaded-animation hashtable makes the ORIGINAL loader HANG - the miss path
// (empty bucket, exhausted chain, or null record) never reassigns the node
// pointer it is walking, so the loop condition is permanently true. THIS
// READER CANNOT REPRODUCE THAT HANG, and does not try to: it is a
// tree-walking XML parser built on sr3xtbl's Node/Children model (recursion
// over an already-fully-parsed document tree), not a re-implementation of
// the original engine's index-walking loader that the hazard lives inside.
// There is no "node pointer that fails to advance" in this reader's control
// flow for ANY field - every cross-table reference in this whole file
// (Situation, File, *Anim, Target_Name, this Action's own Animation, etc.)
// is read as raw text and handed back to the caller, resolved or not, the
// same as every other field (see the header banner's CROSS-TABLE REFERENCES
// note). "Detect and report, don't loop" is therefore trivially satisfied
// here by construction, not something that needed active engineering - a
// host that DID walk an engine-style hashtable/index structure to resolve
// these names would need its own bounds-check/advance-on-miss guard that
// the original game does not have; this reader never performs that walk at
// all, so there's nothing to guard. (Checked: no code path anywhere in this
// file performs a loaded-animation hashtable lookup/resolution - every
// cross-table field stays raw text, per the header banner's standing
// convention - so this conclusion holds for the file as a whole, not just
// this one field.)
struct AnimBlendTreeControlPoint {
    Always<float> range;  // Range - RE-DERIVED 2026-10-02: "always" reader (was "if present")
    Always<float> value;  // Value - RE-DERIVED 2026-10-02: "always" reader (was "if present")
};
struct AnimBlendTreeState {
    std::optional<std::string> animationFilename;  // Animation/Filename -> loaded-animation hashtable (7.2);
                                                     // raw text kept, cross-table resolution not performed
    Always<float> blendTime;                        // Blend_time - RE-DERIVED 2026-10-02: "always" reader
                                                      // (was "if present")
    std::vector<AnimBlendTreeControlPoint> controlPoints;  // Control_points/Control_point (repeated).
                                                             // RE-DERIVED 2026-10-02 (settles the former
                                                             // 7.2/7.3 stride-vs-cap OPEN contradiction):
                                                             // real engine cap is 5 per State (NOT 10 - the
                                                             // literal 10 in the code is the dword index of
                                                             // the entry's own Value[0], not a bound), with
                                                             // NO cap check (a 6th Control_point overwrites
                                                             // the entry's own Value[0] and the next entry) -
                                                             // NOT enforced here either way, all parsed
                                                             // elements are kept.
};
// Sec7.2, stride 0x44, RE-DERIVED 2026-10-02 (CONFIRMED - disassembly). Per
// the row-level record layout (Sec7.3: the 0x20-byte Blend_trees row record
// carries a SEPARATE `Action count`/`Action array pointer` pair alongside
// its `State count`/`State array pointer` pair, and the State's own 0x3C
// stride is fully accounted for with no room for Action data) and the Sec16
// cross-table map (which lists `Action/Animation` as its own row, parallel
// to `Blend_trees/Name` and `State/Animation`, not nested under `State`) and
// the WARNING block's own wording ("a `Blend_trees` ROW's
// `Actions/Action/Animation`..."), `Actions` is modelled here as a field of
// the ROW (AnimBlendTree), a sibling of `states` - NOT nested inside
// AnimBlendTreeState. NOTE, for a future reader of this code: Sec7.2's own
// prose lists the `Actions` bullet at the same indentation as
// `Animation`/`Blend_time`/`Control_points` under "Each `<State>`:", which
// reads, taken alone, as if `Actions` were a child of `<State>` in the XML.
// This is an internal inconsistency in the spec text (flagged here per this
// project's "verify before writing, don't smooth over a contradiction"
// discipline) - the three independent, numerically-checked sources above
// (record layout, cross-reference table, WARNING prose) all agree with each
// other and point the other way, so they were taken as authoritative over a
// single ambiguous bullet-list indentation.
struct AnimBlendTreeAction {
    // Animation/Filename -> loaded-animation hashtable (7.2), same lookup as
    // AnimBlendTreeState::animationFilename; raw text kept, cross-table
    // resolution not performed here (this project's standing convention -
    // see the header banner's CROSS-TABLE REFERENCES note).
    //
    // WARNING, re-derivation 2026-10-02 (CONFIRMED - disassembly/decompile):
    // in the REAL engine, an Action whose Animation does NOT resolve against
    // anim_files.xtbl's loaded-animation hashtable makes the ORIGINAL
    // loader HANG forever (see the full WARNING block above this struct).
    // This reader does not and cannot reproduce that hang - it simply reads
    // whatever text is here, resolved or not, same as every other raw-text
    // cross-reference field in this file. Documented here, at the exact
    // field the hazard is about, so a future reader of this code
    // understands why no loop-guard/bounds-check was needed: there is no
    // loop over engine-style hashtable buckets here to guard.
    std::optional<std::string> animationFilename;
    std::optional<std::string> controlType;    // Control_type - text; "Ramped input" (case-insensitive)
                                                // sets a byte flag at the record's +0x08 (7.2/7.3); raw text
                                                // kept here, the derived flag is not computed
    Always<float> startRangeMin;   // Start_range_min ("always", 7.2)
    Always<float> startRangeMax;   // Start_range_max ("always", 7.2)
    Always<float> endRangeMin;     // End_range_min ("always", 7.2)
    Always<float> endRangeMax;     // End_range_max ("always", 7.2)
    Always<float> timeVariance;    // Time_variance ("always", 7.2)
    std::optional<float> capRampMin;  // Cap_ramp_min ("if present", 7.2) - spec-stated default is the
                                       // ROW's own Control_input_low, a sibling field's value, not a
                                       // standalone constant; left as nullopt when absent (default not
                                       // applied here - see the header banner's "derived/computed values
                                       // are not modelled" note)
    std::optional<float> capRampMax;  // Cap_ramp_max ("if present", 7.2) - default is the row's
                                       // Control_input_high; same treatment as Cap_ramp_min above
    Always<bool> alwaysCap;        // Always_cap (no explicit accessor tag - see the header banner's
                                    // judgement-call note)
    std::optional<float> endStatePercent;        // End_state_percent ("if present", 7.2; spec-stated
                                                  // default -1.0, not applied here)
    std::optional<float> startStatePercentMin;   // Start_state_percent_min ("if present", 7.2;
                                                  // spec-stated default 0.0, not applied here)
    std::optional<float> startStatePercentMax;   // Start_state_percent_max ("if present", 7.2;
                                                  // spec-stated default 1.0, not applied here)
    std::optional<float> cancelRangeMin;  // Cancel_range_min ("if present", 7.2) - default is the row's
                                           // Control_input_low minus 4000, not applied here
    std::optional<float> cancelRangeMax;  // Cancel_range_max ("if present", 7.2) - default is the row's
                                           // Control_input_high plus 4000, not applied here
    Always<bool> speedThrottling;  // Speed_throttling (no explicit accessor tag - see the header
                                    // banner's judgement-call note)
};
struct AnimBlendTree {
    std::optional<std::string> name;         // Name -> hashed (CRC-32) and looked up in the loaded-
                                              // animation hashtable (7.1); an unresolved Name means the
                                              // WHOLE row is skipped by the real engine - not enforced here
    Always<float> controlInputLow;   // Control_input_low - RE-DERIVED 2026-10-02: "always" reader
                                      // (was "if present")
    Always<float> controlInputHigh;  // Control_input_high - RE-DERIVED 2026-10-02: "always" reader
                                      // (was "if present")
    Always<float> rampSpeed;         // Ramp_speed - RE-DERIVED 2026-10-02: "always" reader
                                      // (was "if present")
    std::vector<AnimBlendTreeState> states;    // States/State (repeated)
    std::vector<AnimBlendTreeAction> actions;  // Actions/Action (repeated), ROW-level - see the
                                                // AnimBlendTreeAction struct's own comment for why this
                                                // is not nested inside AnimBlendTreeState. NEW 2026-10-02:
                                                // this whole container was previously unimplemented (the
                                                // 2026-09-30 desk review's OPEN `State` stride vs
                                                // control-point cap contradiction masked its existence).
};
AnimBlendTree ParseAnimBlendTree(const Node* row);
std::vector<AnimBlendTree> ParseAnimBlendTreesTable(const Document& doc);

// ===========================================================================
// 8. anim_ik_situation.xtbl - IK situation records (8)
// ===========================================================================
// NOTE: the 34 compiled-in presets (8.1) are NOT XML data - see the header
// banner. This struct/parser cover only the file's own authored rows.
inline constexpr std::array<std::string_view, 3> kAnimIkSituationFlagNames = {
    "Hard Coded", "Two Person", "Morphed Surface",
};
struct AnimIkSituation {
    std::optional<std::string> name;      // Name -> hashed (CRC-32) into the record (8.2)
    std::optional<std::string> propName;  // Prop_Name (optional) -> hashed with a DIFFERENT, unidentified
                                            // hash function (FUN_00DB1510, 8.2) - raw text kept, no hash
                                            // computed (algorithm itself is OPEN)
    Always<float> morphedSurfaceMaxAdjustment;  // Morphed_Surface_Max_Adjustment ("always" reader, 8.2)
    uint32_t flags = 0;                          // Flags/Flag bitmask via kAnimIkSituationFlagNames (8.2).
                                                  // NOTE (8.2, a real runtime effect NOT modelled by this
                                                  // struct): a row whose Flags include "Hard Coded" is
                                                  // discarded by the loader (immediately overwritten) -
                                                  // this is a whole-table load-order concern, not a per-
                                                  // row parsing one, so it is reported by the validation
                                                  // harness rather than enforced here.
};
AnimIkSituation ParseAnimIkSituation(const Node* row);
std::vector<AnimIkSituation> ParseAnimIkSituationsTable(const Document& doc);

// ===========================================================================
// 9. anim_triggers.xtbl - a flat, bounded trigger-name registry (9)
// ===========================================================================
// NOTE: real rows are capped at 110 and Name at 31 chars + NUL by the loader
// (9.1) - table-level guards, reported by the validation harness rather than
// enforced by this struct (matching sr3tables_environment's own convention
// for similar caps, e.g. lens_flares.xtbl's 8-row guard).
struct AnimTrigger {
    std::optional<std::string> name;  // Name - bounded <=0x20 byte copy (9.1)
};
AnimTrigger ParseAnimTrigger(const Node* row);
std::vector<AnimTrigger> ParseAnimTriggersTable(const Document& doc);

// ===========================================================================
// 10. anim_flinches.xtbl - hit-reaction selection records (10)
// ===========================================================================
// NOTE: capacity 149 rows RE-DERIVED 2026-10-02 (CONFIRMED - disassembly) to
// be a 150-SLOT zero-filled array, not a hard "150th row refused": on the
// 150th <Flinch> the loader returns with its count already at 150 (slot 149
// left all-zero) WITHOUT closing the document handle (a real resource leak,
// not modelled by an XML-tree reader either way) - not modelled here, same
// as every other cap in this group.

// Hit_Direction (10.1): a genuine 4-entry literal table with index == value
// (front=0, right=1, back=2, left=3) - unlike Hit_Location/Type below, the
// spec does not state an "if/else chain -> zero default" for this field, so
// (per this project's "don't extend a confirmed fact past what it was
// stated for" discipline) only the raw text + name table are exposed here,
// matching sr3tables_environment's own kTimeOfDayObjectNames/EnumIndex
// convention - apply sr3xtbl::EnumIndex yourself if you need the index.
inline constexpr std::array<std::string_view, 4> kAnimFlinchHitDirectionNames = {
    "front", "right", "back", "left",
};

// Hit_Location (10.1): a non-sequential if/else literal chain
// ("unrecognised text leaves the field at its zero default", spec-stated
// explicitly for this field) - exposed as a name/value table plus
// ResolveFlinchHitLocation(), which reproduces exactly that documented
// fallback (0 is not one of the four named values, so it is an unambiguous
// "no match" signal).
struct AnimFlinchNamedValue {
    std::string_view name;
    uint8_t value;
};
inline constexpr std::array<AnimFlinchNamedValue, 4> kAnimFlinchHitLocationTable = {{
    {"Head", 6}, {"Chest", 2}, {"Groin", 3}, {"Pelvis", 0xB},
}};
// Returns the matched value, or 0 if `text` matches none of the table above
// (the spec-stated "zero default" for an absent/unrecognised Hit_Location).
uint8_t ResolveFlinchHitLocation(std::string_view text);

// Type (10.1): also an if/else literal chain, values given but the spec does
// not repeat the "zero default" wording for this specific field (it is only
// stated once, for Hit_Location) - so, per the same discipline, only the raw
// text + this reference table are exposed; no Resolve helper is provided.
inline constexpr std::array<AnimFlinchNamedValue, 7> kAnimFlinchTypeTable = {{
    {"Normal", 1}, {"Heavy", 2}, {"Death", 4}, {"Death Heavy", 8},
    {"Base", 0x10}, {"Riot1", 0x20}, {"Riot2", 0x40},
}};

inline constexpr std::array<std::string_view, 3> kAnimFlinchFlagNames = {
    "Crouch", "Cover", "Skydive",
};

struct AnimFlinch {
    std::optional<std::string> name;          // Name -> merged State/Action registry (10.1)
    std::optional<std::string> hitLocation;    // Hit_Location -> kAnimFlinchHitLocationTable / ResolveFlinchHitLocation()
    std::optional<std::string> hitDirection;   // Hit_Direction -> kAnimFlinchHitDirectionNames via EnumIndex
    std::optional<std::string> type;           // Type -> kAnimFlinchTypeTable
    uint32_t flags = 0;                        // Flags/Flag bitmask via kAnimFlinchFlagNames (FlagMask,
                                                // 10.1) - RE-DERIVED 2026-10-02: record field is a DWORD at
                                                // +0x08 (CORRECTED from a formerly-cited byte at +0x08),
                                                // already modelled correctly here as uint32_t
};
AnimFlinch ParseAnimFlinch(const Node* row);
std::vector<AnimFlinch> ParseAnimFlinchesTable(const Document& doc);

// ===========================================================================
// 11. anim_synced.xtbl - two-character synchronized-move records (11)
// ===========================================================================
// Flags (11.2): unlike every other Flag-list in this group, each child's
// FULL text (not a short identifier) is compared against ten fixed phrases.
// RE-DERIVED 2026-10-02 (CONFIRMED - disassembly), CORRECTING this file's
// former claim: the real compare is an INLINED, CASE-SENSITIVE string
// compare ("no _stricmp") - genuinely the one exception in this whole group,
// which otherwise uses the shared case-INsensitive FlagMask (FUN_00DAC740)
// everywhere else. sr3xtbl::FlagMask is always case-insensitive (see
// xtbl.h), so it CANNOT be reused here; AnimSyncedMove's Flags uses a
// dedicated case-sensitive mask function instead (caseSensitiveFlagMask, in
// tables_animation.cpp), matching the real engine exactly.
inline constexpr std::array<std::string_view, 10> kAnimSyncedFlagNames = {
    "victim is object/prop", "attacker use weapon", "victim use weapon",
    "attacker doesn't correct", "victim doesn't correct pos", "attacker is brute",
    "victim is brute", "attacker is avatar", "victim is avatar", "hold last frame",
};

struct AnimSyncedMove {
    std::optional<std::string> name;          // Name - the table's own key (hashed into the row record's
                                               // +0x10, Sec11.1/11.3); read by the OUTER loader
                                               // (FUN_0095DE40), not the detailed per-row reader
                                               // (FUN_0095DAA0) the rest of this struct otherwise models -
                                               // surfaced here anyway since it is a real part of this row's
                                               // data (same reasoning as AnimFile::preload, 3.2)
    std::optional<std::string> attackerAnim;  // AttackerAnim/Filename -> loaded-animation lookup (11.2);
                                                // raw text kept, resolution not performed here
    std::optional<std::string> victimAnim;     // VictimAnim/Filename -> same lookup
    Always<float> xOffset;                      // VictimOffsets/XOffset ("always", 11.2)
    std::optional<float> yOffset;               // VictimOffsets/YOffset ("if present" - the ONE field in
                                                 // this row that differs in presence behaviour, 11.2)
    Always<float> zOffset;                      // VictimOffsets/ZOffset ("always", 11.2)
    Always<float> heading;                       // VictimOffsets/heading ("always", 11.2)
    uint32_t flags = 0;                          // Flags/Flag bitmask via kAnimSyncedFlagNames
                                                  // (caseSensitiveFlagMask, NOT sr3xtbl::FlagMask - see the
                                                  // note above kAnimSyncedFlagNames, 11.2)
};
AnimSyncedMove ParseAnimSyncedMove(const Node* row);
std::vector<AnimSyncedMove> ParseAnimSyncedTable(const Document& doc);

// ===========================================================================
// 12. anim_correction_offsets.xtbl - per-animation position correction (12)
// ===========================================================================
// NOTE (12.1, CONFIRMED - disassembly, empirically confirmed 12.4): the real
// reader walks <Table>'s children directly via the raw node-model pointers
// (tag-agnostic), rather than asking for a row by tag name. Real base-game
// data (12.4) shows the one authored tag is <Correction_Offset> - this
// reader's ParseAnimCorrectionOffsetsTable() reads exactly that tag, which
// matches every real row while staying representative of what the shipped
// data actually contains.
struct AnimCorrectionOffset {
    std::optional<std::string> fileFilename;  // File/Filename -> loaded-animation lookup (12.2); a row
                                                // whose File doesn't resolve is skipped entirely by the
                                                // real engine - not enforced here (raw text kept)
    Vec3Result offset;                          // Offset/{X,Y,Z} ("always" reader FUN_00DACF60, 12.2) - NOTE
                                                 // the Always-hazard: the spec states the engine PRE-SEEDS
                                                 // the destination from DAT_029CDB98/9C/A0 rather than zero
                                                 // before reading. RE-DERIVED 2026-10-01: that global is a
                                                 // zero-filled, never-written global shared by 352 other
                                                 // functions - i.e. the seed is (0, 0, 0) itself (HIGH
                                                 // CONFIDENCE, not full CONFIRMED) - so this struct's own
                                                 // 0.0f stand-in when !present is now believed to already
                                                 // match the real engine value, superseding this file's
                                                 // former "further from the real engine value than usual"
                                                 // warning.
                                                 //
                                                 // RE-DERIVED 2026-10-02 (CONFIRMED - disassembly): the real
                                                 // engine does not store this vec3 directly - FUN_00DA38E0
                                                 // (a generic, 40+-caller vec3->vec4 widening helper) turns
                                                 // it into an aligned 4-float `(X, Y, Z, Z)` block before
                                                 // storage. CONFIRMED NOT a rotation quaternion and reads no
                                                 // further XML element - this settles the former OPEN
                                                 // question. The widened block is a pure, fully-specified
                                                 // function of Offset/{X,Y,Z} alone; still not computed
                                                 // here, matching this file's "raw field in, derived value
                                                 // out is the caller's job" convention (see the header
                                                 // banner) - only the raw vec3 is kept.
    // Repeat policy (12.2), RE-DERIVED 2026-10-02: a target animation gets
    // at most one correction record - the per-animation slot allocator
    // REFUSES an animation index it already holds, so the FIRST
    // <Correction_Offset> row for a given File wins and later repeats for
    // the same File are silently dropped (CORRECTED from an earlier "last
    // wins" guess). The allocator's apparent "255" capacity was also
    // CORRECTED: 0xFF is the allocator's null/failure sentinel, not a
    // capacity - the real slot count is at most 34 (HYPOTHESIS). Neither the
    // repeat policy nor any capacity is enforced here - every
    // <Correction_Offset> row present in the XML is kept, per this group's
    // standing convention of leaving whole-table/load-order concerns to the
    // validation harness.
};
AnimCorrectionOffset ParseAnimCorrectionOffset(const Node* row);
std::vector<AnimCorrectionOffset> ParseAnimCorrectionOffsetsTable(const Document& doc);

// ===========================================================================
// 13. creation_lipsync_animations.xtbl - character-creation idle lipsync (13)
// ===========================================================================
// NOTE: capacity 7 Entry rows (enforced by the loader, 13.1) - not modelled
// here. Real engine behaviour NOT modelled here either (a whole-row
// load-order concern, per this group's convention of leaving such things to
// the validation harness): "if Trigger doesn't resolve to a non-zero audio
// event id, the rest of the Chance entry is not read and the array slot is
// not advanced" (13.1) - every Chance element present in the XML is kept
// here regardless.
struct LipsyncChance {
    std::optional<std::string> trigger;    // Trigger -> Wwise audio-event id (FUN_00462960 via
                                             // FUN_0070A2F0, 13.1); raw text kept, id not resolved
    std::optional<uint16_t> variant;        // Variant ("if present", 13.1)
    std::optional<std::string> animation;   // Animation -> merged State/Action registry (13.1, NOT the
                                             // loaded-animation-file lookup despite the element's name)
};
struct LipsyncEntry {
    std::optional<std::string> name;              // Name (13.1)
    std::vector<LipsyncChance> chances;            // Chances/Chance (repeated)
};
LipsyncEntry ParseLipsyncEntry(const Node* row);
std::vector<LipsyncEntry> ParseCreationLipsyncAnimationsTable(const Document& doc);

// ===========================================================================
// 14. character_visemes.xtbl - facial viseme -> morph-target weights (14)
// ===========================================================================
struct VisemeTarget {
    std::optional<std::string> targetName;   // Target_Name -> hashed (CRC-32); HIGH CONFIDENCE this
                                              // resolves against a character's morph-target table
                                              // (spec-morph-format.md's territory), consumer not traced
                                              // (14.1, OPEN); raw text kept, hash not computed
    Always<float> targetValue;                // Target_Value ("always", 14.1) - RAW value; the engine
                                               // scales this by a fixed global multiplier (DAT_01117DC8)
                                               // before storage. RE-DERIVED 2026-10-02 (CONFIRMED -
                                               // disassembly for the multiply; the constant's exact
                                               // encoding is HIGH CONFIDENCE): the multiplier is 0.01,
                                               // applied in DOUBLE precision and narrowed back to float -
                                               // i.e. an authored Target_Value is a PERCENTAGE and the
                                               // engine's stored value a FRACTION (the same shared
                                               // percent->fraction constant spec-tables-diversions.md
                                               // Sec1.4 already names). Previously OPEN, now settled - the
                                               // scale is still deliberately NOT applied here, only the raw
                                               // authored percentage is kept (matches this file's
                                               // "raw field in, derived value out is the caller's job"
                                               // convention, see the header banner).
};
struct Viseme {
    std::optional<std::string> name;         // Name (14.1)
    std::vector<VisemeTarget> targets;        // Targets/Target (repeated)
};
Viseme ParseViseme(const Node* row);
std::vector<Viseme> ParseCharacterVisemesTable(const Document& doc);

// ===========================================================================
// 15. anim_set_properties.xtbl / anim_prop_sets.xtbl - SKIPPED (15)
// ===========================================================================
// Both are registered in the exe's table-dependency manifest but NO function
// anywhere opens either file (spec 15, an exhaustive cross-reference scan,
// not a "didn't find a caller in a quick look" negative) - no loader, so no
// reader here, matching this project's materials.xtbl / traffic_types.xtbl
// precedent (see the header banner). For the record, the spec (15.2)
// documents each file's authored shape even though nothing consumes it:
// anim_set_properties.xtbl: <AnimSet>{Name, Flags, _Editor}, 18 rows.
// anim_prop_sets.xtbl:      <Anim_Prop_Set>{Name, Props, _Editor}, 3 rows.

}  // namespace sr3tables_animation
