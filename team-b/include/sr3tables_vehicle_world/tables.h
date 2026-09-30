#pragma once

// sr3tables_vehicle_world - typed readers for the vehicle-interaction /
// entry-exit animation, wheel/customization catalogue, and world-object /
// items `.xtbl` table group.
//
// Source: spec-tables-vehicle-world.md (SPEC TEAM, agent AS), the ONLY
// source consulted for this file's schema (cleanroom boundary - see the HARD
// RULES in the task that produced this file: no other spec document, and
// certainly no game executable / disassembly / decompiled code, was read to
// write this reader). Built entirely on top of include/sr3xtbl/xtbl.h's
// document model and accessors (Node, FindChild, ChildText, GetX/ReadXAlways,
// ReadVec3, FlagMask/EnumIndex, HasFlag, NameHash); every offset/address
// cited in a comment is copied verbatim from the spec's own prose, purely to
// let a reader cross-reference the spec section, never re-derived.
//
// CONVENTION (matches sr3xtbl.h and sr3tables_environment/tables.h exactly):
//   * sr3xtbl::Always<T> (value + present) = the DEFAULT this reader applies
//     to every scalar (int/float/bool) element the spec does not explicitly
//     call "if-present" / "write-if-present" / "only if present", or give a
//     spec-stated concrete default distinct from the engine's "always write"
//     hazard. Reason: spec-tables-environment.md's own banner documents this
//     as the group-wide default reader idiom (0x00DACCB0-family), and this
//     spec (spec-tables-vehicle-world.md) frequently omits the "always"/
//     "if-present" qualifier on individual bool/int fields inside an
//     otherwise fully-disassembly-confirmed record (e.g. section 4.1's
//     Orient_To_Seat/Project_Onto_World/Orient_To_World/
//     Ignore_When_In_Chassis) - there is no stated reason to believe those
//     follow a DIFFERENT accessor family than the fields around them that
//     ARE explicitly marked "always" (e.g. 4.1's own Heading). When
//     !present, Always<T>::value is sr3xtbl's 0 stand-in, NOT the engine's
//     real (possibly indeterminate) residue - see xtbl.h's own warning.
//   * std::optional<T> = fields the spec explicitly marks "if-present" /
//     "only if present", or documents a concrete non-hazard default for
//     (e.g. vehicle_wheel_groups.xtbl's Price -> default 0, items_inventory's
//     Cost -> default -1). A `...OrDefault()` helper is added wherever the
//     spec states a concrete default distinct from 0/absent.
//   * Cross-table reference fields (a Name hashed and linear-scanned against
//     another table's loaded array, an effects/explosions/audio CRC, an
//     animation-state index, ...) are kept as the RAW TEXT read from the XML
//     (std::optional<std::string> / std::vector<std::string>), exactly as
//     tables_environment.h keeps weather.xtbl's Next_Stage_List names, vfx.xtbl's
//     Radial_blur_entry, etc. - never as a pre-resolved index/pointer, since
//     resolving is a load-time, cross-table operation this per-row reader
//     does not perform.
//   * Enum-like text fields (a Name matched case-insensitively against a
//     fixed literal list) are ALSO kept as raw text, with the literal list
//     exposed alongside the struct (kXxxNames) so a caller/validator can
//     compute the match itself via sr3xtbl::EnumIndex - the same pattern
//     time_of_day_objects.xtbl / bitmap_materials.xtbl use in
//     tables_environment.h. This avoids baking a precomputed index into the
//     struct for enums this spec gives only a partial literal list for
//     (section 2.1's 83-entry Parameter token table: only 20/83 sampled).
//
// WHAT IS DELIBERATELY LEFT OUT / MODELLED ONLY PARTIALLY, per the spec's
// own §23 "Open items" and this task's "don't invent" rule:
//   * vehicle_cust_color_pool.xtbl's Shader_Values block (§9.1, open item 1):
//     the CLASSIFICATION MECHANISM is confirmed (three specially-hashed
//     names, a generic Vector_Element/Float_Element list, a separate
//     String-valued list) but the exact per-field byte layout is OPEN. Only
//     the confirmed +0x04 Name and the Vector_Element/Float_Element COUNTS
//     are modelled; per-element internal shape (whether each element itself
//     carries its own Name, and the three specially-classified names' own
//     XML shape) is not stated by the spec and is not guessed here.
//   * externalized_vehicle_components.xtbl's Component record (§7, open item
//     4): only the Slot header (fully CONFIRMED) and the Components/Component
//     COUNT are modelled. STALE UNTIL 2026-09-29 (rule 16): this used to say
//     no field was identified "not even a name" - spec §23 item 4 (updated
//     2026-09-28) confirms 4 real, populated field names (Name/DisplayName/
//     Price/Buyable, 235/199/199/199 real occurrences) - but the BYTE-OFFSET
//     schema is still genuinely unfound, so this reader correctly still
//     does not model per-field data; only the "not even a name" framing
//     was wrong, not the modelling decision.
//   * items_3d.xtbl (§16): array/resolver/row-tag CONFIRMED, and several
//     element names are CONFIRMED (empirically, from real base rows read by
//     OTHER fully-decompiled passes over the same document) but the real
//     per-row byte-offset builder (FUN_00904d10) was not decompiled by the
//     spec's own author, so no offset table exists. Confirmed-to-exist
//     elements are still surfaced as raw text/counts (Mesh, character_mesh,
//     Props/Prop/Flags/Flag, LargeProp, streaming_category, and the
//     HYPOTHESIS-tier-but-empirically-observed Anim_set/Glow_Type/
//     Scale_Ambient/Color_Variants/Item_Flags) - never given a byte offset or
//     an interpreted meaning the spec does not commit to. The spec's own
//     mid-investigation correction (§16.1) that "items_preload_containers"/
//     "items_containers" are profiler zone labels, NOT XML section names, is
//     honoured here: neither string appears anywhere in this file as a type,
//     field, or section name.
//   * The shared "LightSet" table (Vehicle-Customization-Lightset.xtbl,
//     store_gang_lightset.xtbl, test_shop_light_01.xtbl - split out into
//     lightset.h): the RUNTIME ACTIVATION record (§11.1) is explicitly a
//     different thing from the table's own XML-sourced content and is not
//     modelled at all (it is not read from this table's XML - see §11.1's
//     own text). The XML element VOCABULARY (§11.2) is CONFIRMED - empirical
//     and is modelled in full; no byte offset exists to cite because none was
//     recovered (the real per-field reader, inside FUN_005984c0, was not
//     decompiled by the spec's author).
//
// THE §1.3 / §22 NAMING CORRECTIONS, applied consistently (never left as the
// old/wrong guess anywhere in this library):
//   * section 3 (vehicle_interaction_info.xtbl): the per-seat sub-record's
//     own identifying child is confirmed EMPIRICALLY to be `<Seat>` (e.g.
//     `<Seat>Driver</Seat>`), not `<Name>` - modelled as `seatText` read from
//     "Seat", never "Name".
//   * section 8.2 (vehicle_cust_interface.xtbl): a slot reference's text
//     lives on the lowercase `<name>` CHILD of each `slots/slots` wrapper
//     item, not on the wrapper element's own text - modelled by reading the
//     "name" child, never the wrapper's own ChildText("").
//   * section 16.1 (items_3d.xtbl): "items_preload_containers"/
//     "items_containers" are confirmed to be profiler/memory-pool zone
//     labels passed as a plain function argument, NOT XML section names -
//     no type or field anywhere in this header is named after them.
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> for every repeated-row table, and ParseXxx(const
// sr3xtbl::Document&) -> Xxx (or optional<Xxx>) for the few single-block /
// fixed-slot tables (vehicle_surfing_style_two.xtbl, airplane_takeoff_land_curves.xtbl,
// props.xtbl). All definitions are in src/tables_vehicle_world.cpp.
//
// Split headers (included at the bottom, same pattern as tables_environment.h):
//   lightset.h     - section 11, the shared LightSet schema (3 files)
//   world_items.h  - sections 12-19, the level/props/triggers/items/contacts/
//                    persona/airplane-curve tables

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_vehicle_world {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3;
using sr3xtbl::Vec3Result;

// ===========================================================================
// 2. vi_enter.xtbl / vi_exit.xtbl / vi_ride.xtbl - the shared vehicle-
//    interaction animation-set schema (spec-tables-vehicle-world.md 2)
// ===========================================================================
// The three files share ONE loader and ONE per-row reader (spec 2, CONFIRMED
// - disassembly, "not just likely as the brief guessed"). Modelled here as a
// single struct/parser reused three ways, per the task's own instruction.

// spec 2.1: 20 of the 83-entry Parameter token table were dumped; the other
// 63 are CONFIRMED to exist (the reader loops "Parameter%d" until absent,
// unbounded, matched against this table) but their literal text is OPEN.
// Kept here only for tests/validators that want to sanity-check the 20 known
// samples against real data - NOT used to compute an index during parsing
// (see the file banner: enum-like fields are kept as raw text).
inline constexpr std::array<std::string_view, 20> kVehicleInteractionParameterTokenSample = {
    "none", "any", "activate", "deactivate", "change seat", "close", "enter", "exit", "extract", "open",
    "prepare", "traverse", "fast", "prepare lift", "prepare pull", "open standard", "open shady",
    "open shopper", "traverse standard", "traverse no door",
};

struct VehicleInteractionAnimationElement {
    // Animation_Grid/Element/Parameter1, Parameter2, ... (+0x00-+0x14, up to 6
    // dwords wide in real data - spec 2.1/2.2): read sequentially by
    // "Parameter%d" until one is absent. Real base data never exceeds 6
    // (657/657, 813/813, 454/454 - spec 2.2); this reader keeps whatever was
    // actually present, regardless of count, and does NOT reproduce the
    // spec's documented unbounded-write hazard past a 7th ParameterN (no
    // fixed-size destination exists in this library to overrun).
    std::vector<std::string> parameters;
    // Animation (+0x18) - text resolved through the animation-state table
    // (FUN_004BF810); raw name kept, "-1" no-match sentinel not modelled
    // (that is a load-time resolution result, not XML content).
    std::optional<std::string> animation;
    // Animated_Camera_Tests/Camera_Pos (+0x1C/+0x20) - rare (21 total in
    // vi_enter.xtbl, 0 in vi_exit.xtbl/vi_ride.xtbl - spec 2.2). Standard
    // X/Y/Z vec3 convention assumed (not flagged as an exception the way
    // LightSet's Color/Position are - see lightset.h).
    std::vector<Vec3> cameraPos;
};

struct VehicleInteractionAnimationSet {
    std::optional<std::string> name;  // Name (row key; also hashed, +0x00)
    std::vector<VehicleInteractionAnimationElement> elements;  // Animation_Grid/Element
};

VehicleInteractionAnimationSet ParseVehicleInteractionAnimationSet(const Node* row);
// vi_enter.xtbl, vi_exit.xtbl and vi_ride.xtbl all use the row tag
// "Vehicle_Interaction_Animation_Set" and this exact schema (spec 2.1) - one
// parser, three call sites below.
std::vector<VehicleInteractionAnimationSet> ParseVehicleInteractionAnimationSetTable(const Document& doc);
inline std::vector<VehicleInteractionAnimationSet> ParseViEnterTable(const Document& doc) {
    return ParseVehicleInteractionAnimationSetTable(doc);
}
inline std::vector<VehicleInteractionAnimationSet> ParseViExitTable(const Document& doc) {
    return ParseVehicleInteractionAnimationSetTable(doc);
}
inline std::vector<VehicleInteractionAnimationSet> ParseViRideTable(const Document& doc) {
    return ParseVehicleInteractionAnimationSetTable(doc);
}

// ===========================================================================
// The shared 8-seat resolver (spec 3.2/3.3, 21.1 "the shared 8-seat
// resolver ... FUN_00AC1BA0") - two parallel 8-name alias lists for the same
// 8 indices, positionally corresponding as the spec lists them. Used by
// vehicle_interaction_info.xtbl's Seat/Primary_Access_Seat/Secondary_Access_Seat.
// NOTE: this is a DIFFERENT, larger convention than vehicle_animation_modifiers.xtbl's
// own 4-named-seat convention (spec 6.1) - see kVehicleAnimModifierSeatNames below.
// ===========================================================================
inline constexpr std::array<std::string_view, 8> kSeatResolverGenericNames = {
    "driver", "passenger 1", "passenger 2", "passenger 3", "passenger 4", "passenger 5", "passenger 6", "passenger 7",
};
inline constexpr std::array<std::string_view, 8> kSeatResolverVehicleSpecificNames = {
    "front driver", "front passenger", "rear driver", "rear passenger", "extra 1", "extra 2", "extra 3", "extra 4",
};

// ===========================================================================
// 3. vehicle_interaction_info.xtbl - per-seat vehicle-interaction behaviour
//    (spec-tables-vehicle-world.md 3)
// ===========================================================================
inline constexpr std::array<std::string_view, 6> kCapsuleShapeNames = {
    "stand", "vehicle", "motorcycle", "boat", "boat stand", "crouch",
};
inline constexpr std::array<std::string_view, 8> kQueueNames = {
    "Queue 1", "Queue 2", "Queue 3", "Queue 4", "Queue 5", "Queue 6", "Queue 7", "Queue 8",
};

// Seat_Info/Element, one per <Seat> (spec 3.2, 0x24-byte record). NOTE the
// §1.3/§22 naming correction: the identifying child is confirmed empirically
// to be `<Seat>`, never `<Name>` (spec 3.3).
struct VehicleInteractionSeatInfo {
    std::optional<std::string> seatText;  // <Seat> (raw text; resolve via kSeatResolverGenericNames /
                                           // kSeatResolverVehicleSpecificNames - either alias set names the
                                           // same 8 indices, spec 3.1)

    std::optional<std::string> interactionPointSet;  // Interaction_Point_Set -> vehicle_interaction_point_sets.xtbl
    std::optional<std::string> capsuleShape;          // Capsule_Shape -> kCapsuleShapeNames (6 literals); the further
                                                       // 6-entry indirection-table mapping (spec 3.2) is a load-time,
                                                       // consumer-side value (HYPOTHESIS on meaning) and is not modelled

    // Flags (spec 3.2's 4 flag bytes, +0x08-+0x0B; 24 named bits total, one
    // bit0-of-byte1 excluded - it is an unconditional "populated" marker,
    // never element-driven, so it is not a field here). Presence of the
    // whole <Flags> element itself is not separately observable from the
    // spec's text beyond "gated on their respective XML children being
    // present" - modelled per-flag via HasFlag, which already reports
    // "no matching <Flag> child" uniformly whether <Flags> itself, or just
    // that one <Flag>, is absent.
    bool mirrorInteractionPoints = false;
    bool meleeBruteSeat = false;
    bool weaponsBruteSeat = false;
    bool rollerbladerSeat = false;
    bool riotShieldSeat = false;
    bool entryOnly = false;
    bool checkPointProjectionForUsability = false;
    bool noDoorRequired = false;
    bool ragdollOnDeath = false;
    bool quickDespawnOnDeath = false;
    bool ignoreDistanceChecks = false;
    bool ignoreTeamDisposition = false;
    bool snapEntryAnimation = false;
    bool hideOccupant = false;
    bool ignoreHumanTypeCheck = false;
    bool noAiExit = false;
    bool specialEntryOnly = false;
    bool noFreefallAtAltitude = false;
    bool holdWeaponInLeftHand = false;
    bool noDoorCloseAnim = false;
    bool shouldHideBackpack = false;
    bool shouldScrunchHair = false;
    bool shouldHideBigHats = false;
    bool equipRifleOnExit = false;  // the only flag in byte 4 (+0x0B)

    std::optional<std::string> queue;  // Queue -> kQueueNames (8 literals); "-1" no-match/absent not modelled
    std::optional<std::string> primaryAccessSeat;    // Primary_Access_Seat -> same 8-seat resolver
    std::optional<std::string> secondaryAccessSeat;  // Secondary_Access_Seat -> same 8-seat resolver
    std::optional<std::string> enterAnimations;  // Enter_Animations -> vi_enter.xtbl (spec 21: CRC, linear scan)
    std::optional<std::string> exitAnimations;   // Exit_Animations -> vi_exit.xtbl
    std::optional<std::string> rideAnimations;   // Ride_Animations -> vi_ride.xtbl
};

struct VehicleInteractionInfo {
    std::optional<std::string> name;  // Name (char[0x40] bounded copy, +0x00; also hashed at +0x40)
    // Seat_Info/Element (repeated; spec 3.1 documents 8 fixed sub-record
    // SLOTS at the runtime record level, but real XML need not populate
    // every slot - 209 Elements / 51 rows, ~4 average - spec 3.3). Kept as a
    // vector of however many <Element>s were actually present, rather than a
    // fixed 8-array, so an <Seat> text that fails to resolve against either
    // 8-name alias list is preserved here rather than silently discarded
    // (the spec does not state what the loader does with an unresolved
    // <Seat> for this particular table).
    std::vector<VehicleInteractionSeatInfo> seats;
};

VehicleInteractionInfo ParseVehicleInteractionInfo(const Node* row);
std::vector<VehicleInteractionInfo> ParseVehicleInteractionInfoTable(const Document& doc);

// ===========================================================================
// 4. vehicle_interaction_point_sets.xtbl - named entry/exit point geometry
//    (spec-tables-vehicle-world.md 4)
// ===========================================================================
// "None" (spec value -1) and any unmatched text both read as "not found" via
// sr3xtbl::EnumIndex's own convention, so listing only the 17 non-"None"
// literals here reproduces the spec's -1 sentinel exactly.
inline constexpr std::array<std::string_view, 17> kInteractionPointTypeNames = {
    "Enter Start", "Enter Start Convertible", "Enter Start Lift", "Enter Start Pull", "Enter Start Extract",
    "Enter Start Extract Convertible", "Enter Traverse End", "Enter Traverse End Extract", "Exit End",
    "Exit End Convertible", "Exit End Fast Outward", "Exit End Fast Forward", "Exit End Fast Backward",
    "Exit End At Speed Cruise", "Exit End At Speed Fast", "Exit End At Altitude", "Exit Traverse End",
};
// 7 literals incl. the anomalous, never-exercised-by-real-data 7th ("stand" -
// spec 4.1/4.2: "likely table-tail bleed from an adjacent constant").
inline constexpr std::array<std::string_view, 7> kInteractionPointDirectionNames = {
    "Left", "Right", "Front", "Front Left", "Front Right", "Back", "stand",
};

struct InteractionPointSetElement {
    std::optional<std::string> interactionPointType;  // Interaction_Point_Type -> kInteractionPointTypeNames
    Vec3Result seatOffset;                              // Seat_Offset (+0x04-0x0F)
    Always<float> heading;                              // Heading (+0x10, always)
    std::optional<std::string> direction;                // Direction -> kInteractionPointDirectionNames (+0x14)
    Always<bool> orientToSeat;            // Orient_To_Seat (+0x18)
    Always<bool> projectOntoWorld;        // Project_Onto_World (+0x19)
    Always<bool> orientToWorld;           // Orient_To_World (+0x1A)
    Always<bool> ignoreWhenInChassis;     // Ignore_When_In_Chassis (+0x1B)
};

struct InteractionPointSet {
    std::optional<std::string> name;  // Name (hashed only; the text itself is discarded by the engine - spec 4.1)
    std::vector<InteractionPointSetElement> elements;  // Interaction_Point_Set_Elements/Interaction_Point_Set_Element
};

InteractionPointSet ParseInteractionPointSet(const Node* row);
std::vector<InteractionPointSet> ParseInteractionPointSetsTable(const Document& doc);

// ===========================================================================
// 5. vehicle_wheel_groups.xtbl - rim and spinner customization catalogue
//    (spec-tables-vehicle-world.md 5)
// ===========================================================================
struct WheelGroupRimElement {
    // Rims_Grid/Rim_Element - stored as 3 PARALLEL arrays at the runtime
    // record level (structure-of-arrays, spec 5.1), modelled here as one
    // array-of-structs for convenience; the SoA-vs-AoS distinction is a
    // runtime storage detail, not an XML shape difference.
    std::optional<std::string> displayName;  // Display_Name (localized handle)
    std::optional<std::string> frontRim;      // Front_Rim -> externalized_vehicle_components.xtbl (FUN_00A96040)
    std::optional<std::string> rearRim;       // Rear_Rim -> same resolver
};

struct WheelGroupSpinnerElement {
    std::optional<std::string> displayName;  // Display_Name (localized handle)
    std::optional<int32_t> price;             // Price (if-present, spec-stated default 0)
    int32_t PriceOrDefault() const { return price.value_or(0); }
    // "both spinner fields default to 0 if Front_Spinner is absent" (spec
    // 5.1) - modelled as: frontSpinner absent -> rearSpinner not read at all
    // (matches "read only if Front_Spinner was present").
    std::optional<std::string> frontSpinner;  // Front_Spinner -> externalized_vehicle_components.xtbl
    std::optional<std::string> rearSpinner;   // Rear_Spinner -> same resolver; only read if Front_Spinner present
};

struct WheelGroup {
    std::optional<std::string> name;         // Name (hashed, +0x00)
    std::optional<std::string> displayName;  // Display_Name (localized handle, +0x04)
    std::optional<int32_t> price;             // Price (if-present, spec-stated default 0, +0x08)
    int32_t PriceOrDefault() const { return price.value_or(0); }
    std::vector<WheelGroupRimElement> rims;        // Rims_Grid/Rim_Element
    std::vector<WheelGroupSpinnerElement> spinners; // Spinners_Grid/S_Element (0 in real base data - spec 5.2)
};

WheelGroup ParseWheelGroup(const Node* row);
std::vector<WheelGroup> ParseWheelGroupsTable(const Document& doc);

// ===========================================================================
// 6. vehicle_animation_modifiers.xtbl - hierarchical seat/weapon animation-
//    offset overrides (spec-tables-vehicle-world.md 6)
// ===========================================================================
// This table PATCHES already-loaded vehicle-info entries (spec-vehicle-data.md
// 7's 0xB80-stride table) rather than owning its own row array (spec 6). It
// is modelled here purely as what the XML itself carries, independent of the
// vehicle-info entry it would be applied to (this library has no dependency
// on sr3vehicleinfo, per the task's "no required integration" note).

// The 4-named-seat convention this table uses - DIFFERENT and smaller than
// section 3's shared 8-seat resolver (spec 6.1).
inline constexpr std::array<std::string_view, 4> kVehicleAnimModifierSeatNames = {
    "driver", "front passenger", "back left passenger", "back right passenger",
};

struct WeaponAnimationGroupEntry {
    // Weapon_Animation_Group produces one base sub-entry PLUS one more per
    // nested Animation_States/Animation_State (spec 6.1) - both kinds are
    // flattened into this same vector by the parser; `animationState`
    // distinguishes them (nullopt == the group-level sub-entry itself, "0xFFFFFFFF
    // / default state for this weapon category" per spec).
    std::optional<std::string> weaponCategory;   // Weapon_Animation_Group's own Name -> the 7-entry weapon-class
                                                  // table (FUN_00B81900/FUN_00DAC830); HIGH CONFIDENCE, not itself
                                                  // dumped this pass (spec 23 item 5) - raw text kept regardless
    std::optional<std::string> animationState;   // Animation_State's own Name (nullopt for the group-level entry)
    Vec3Result humanSeatOffset;                   // Human_Seat_Offset (zeroed if absent, per spec 6.1)
};

struct VehicleAnimModifierSeat {
    std::optional<std::string> seatText;  // <Seat> matched against kVehicleAnimModifierSeatNames (spec 6.1)
    Vec3Result humanSeatOffset;            // per-seat Human_Seat_Offset override (zeroed if absent)
    std::vector<WeaponAnimationGroupEntry> weaponAnimationGroups;  // Weapon_Animation_Groups/Weapon_Animation_Group
};

struct VehicleAnimModifierVehicle {
    std::optional<std::string> name;  // Name -> vehicle-info entry lookup (FUN_00AC2400); unmatched names silently
                                       // skipped by the real loader (spec 6.1) - kept here regardless (this reader
                                       // does not perform the lookup)
    std::optional<float> humanScale;   // Human_Scale (if-present, +0x484)
    Vec3Result humanSeatOffset;        // per-vehicle Human_Seat_Offset override (zeroed if absent, +0x488)
    std::vector<VehicleAnimModifierSeat> seats;  // per <Seat> matched against kVehicleAnimModifierSeatNames (+0x90C)
};

struct VehicleAnimModifiers {
    Vec3Result humanSeatOffsetDefault;  // Vehicle_Anim_Modifiers/Human_Seat_Offset - the single global default
                                          // (spec 6.1; a single value, not per-row)
    std::vector<VehicleAnimModifierVehicle> vehicles;  // Vehicles/Vehicle
};

VehicleAnimModifiers ParseVehicleAnimModifiers(const Document& doc);

// ===========================================================================
// 7. externalized_vehicle_components.xtbl - shared vehicle-customization
//    component catalogue (spec-tables-vehicle-world.md 7)
// ===========================================================================
struct ExternalizedComponentSlot {
    std::optional<std::string> name;         // Name (hashed; matched by FUN_00A96040, cross-referenced by
                                              // vehicle_wheel_groups.xtbl's Front_Rim/Rear_Rim/etc. - spec 5, 7)
    std::optional<std::string> cameraInfo;    // Camera_Info (hashed); absent -> a shared runtime "no override"
                                              // global (DAT_029C9964) - a runtime default, not modelled here
    // Components/Component (+0x0C/+0x10): only the COUNT is modelled. Byte
    // layout still genuinely unfound (only one non-data-driven flag byte,
    // spec 7.1/23 item 4) - but 4 real field NAMES (Name/DisplayName/Price/
    // Buyable) are now confirmed present in real data (spec 23 item 4,
    // 2026-09-28); this reader still correctly doesn't model per-field data
    // without a real byte offset, per this project's own standing rule.
    size_t componentCount = 0;
};

ExternalizedComponentSlot ParseExternalizedComponentSlot(const Node* row);
std::vector<ExternalizedComponentSlot> ParseExternalizedVehicleComponentsTable(const Document& doc);

// ===========================================================================
// 8.1 vehicle_cust_slots.xtbl (spec-tables-vehicle-world.md 8.1)
// ===========================================================================
inline constexpr std::array<std::string_view, 5> kVehicleCustSlotTypeNames = {
    "body mod", "color", "performance", "wheels", "chassis",
};
inline constexpr std::array<std::string_view, 6> kVehicleComponentTypeNames = {
    "front wheels", "rear wheels", "front spinners", "rear spinners", "front rims", "rear rims",
};

struct VehicleCustSlot {
    std::optional<std::string> displayName;  // DisplayName (localized handle, +0x00)
    std::optional<std::string> name;          // Name (hashed, +0x04)
    std::optional<std::string> slotType;      // SlotType -> kVehicleCustSlotTypeNames (+0x08)
    std::optional<std::string> vehicleComponentType;  // Vehicle_Component_Type -> kVehicleComponentTypeNames
                                                        // (+0x0C; "0xFFFFFFFF if absent" per spec - raw text kept)
};

VehicleCustSlot ParseVehicleCustSlot(const Node* row);
std::vector<VehicleCustSlot> ParseVehicleCustSlotsTable(const Document& doc);

// ===========================================================================
// 8.2 vehicle_cust_interface.xtbl - the customization-menu category list
//     (spec-tables-vehicle-world.md 8.2)
// ===========================================================================
struct VehicleCustInterfaceEntry {
    std::optional<std::string> displayName;   // Display_Name (raw NUL-terminated text, unbounded copy, +0x00)
    std::optional<std::string> name;           // Name (hashed, +0x80)
    std::optional<std::string> parentCategory;  // parent_category (hashed; "0 if absent", +0x84)
    // slots/slots/name (+0x98/+0x9C): per the §1.3/§22 naming correction,
    // this is the lowercase `name` CHILD of each inner `slots` wrapper item,
    // NOT that item's own text - resolved via FUN_00A96130 against
    // vehicle_cust_slots.xtbl's own loaded array (a confirmed cross-
    // reference, spec 8.2). Raw text kept, one per `slots/slots` item.
    std::vector<std::string> slotNames;
    // Flags (+0xA0): bit1 "can only be set, never explicitly cleared" per
    // spec - modelled as a plain observed bool regardless (this reader has
    // no persistent state to "never clear").
    bool isColorMenu = false;   // bit0 is_color_menu
    bool isWheelMenu = false;   // bit1 is_wheel_menu
    bool isPerfMenu = false;    // bit3 is_perf_menu
};

VehicleCustInterfaceEntry ParseVehicleCustInterfaceEntry(const Node* row);
std::vector<VehicleCustInterfaceEntry> ParseVehicleCustInterfaceTable(const Document& doc);

// ===========================================================================
// 9.1 vehicle_cust_color_pool.xtbl (spec-tables-vehicle-world.md 9.1)
// ===========================================================================
// Open item 1 (spec 23): the exact per-field byte layout of the
// Shader_Values block is OPEN; only the classification MECHANISM is
// confirmed. Modelled minimally and honestly - see the file banner.
struct ColorPoolEntry {
    std::optional<std::string> name;  // Name (+0x04 hash key; the SAME text also resolves a "material" handle
                                       // at +0x00 via FUN_00A74910 - HYPOTHESIS on the exact semantic, spec
                                       // 9.1/23 item 7 - not a second XML element, so nothing further to store)
    bool shaderValuesPresent = false;   // Shader_Values wrapper present
    size_t vectorElementCount = 0;      // Shader_Values/Vector_Element children (3-float values)
    size_t floatElementCount = 0;       // Shader_Values/Float_Element children (1-float values)
    // The three specially-classified names (Reflection_Cos_Min_Angles,
    // Reflection_Inv_Range_Cos_Angles, Glass_Color) and the separate
    // String-valued shader-parameter list (spec 9.1) are mechanism-CONFIRMED
    // but their own XML shape inside Shader_Values was not stated by the
    // spec - not modelled further here (see the file banner and spec 23
    // item 1).
};

ColorPoolEntry ParseColorPoolEntry(const Node* row);
std::vector<ColorPoolEntry> ParseVehicleCustColorPoolTable(const Document& doc);

// ===========================================================================
// 9.2 vehicle_cust_color_sets.xtbl (spec-tables-vehicle-world.md 9.2)
// ===========================================================================
struct ColorSetElement {
    // Color_Grid/Color_Element/Color: an unresolved name is NOT fatal (the
    // engine sprintfs a diagnostic and stores 0, spec 9.2) - raw text kept.
    std::optional<std::string> color;
};

struct ColorSet {
    std::optional<std::string> displayName;  // Display_Name (localized handle, +0x00)
    std::optional<std::string> name;          // Name (hashed, +0x04)
    Always<int32_t> price;                     // Price (always, +0x08)
    std::vector<ColorSetElement> colors;       // Color_Grid/Color_Element (+0x0C count / +0x10 pointer)
};

ColorSet ParseColorSet(const Node* row);
std::vector<ColorSet> ParseVehicleCustColorSetsTable(const Document& doc);

// ===========================================================================
// 10. vehicle_surfing_style_two.xtbl - global surfing/handstanding tuning
//     (spec-tables-vehicle-world.md 10)
// ===========================================================================
// A single-row global settings table (spec 10): exactly one Vehicle_Surfing
// element, no per-row array. RAW values are kept (pre-conversion): the
// seconds->ticks / percent->fraction / mph->m/s-squared conversions and the
// various clamps described in spec 10's table are load-time transforms, not
// XML content, matching this project's established "keep the raw authored
// value" convention (e.g. tables_environment.h's FadeCategory, Groundfire).
struct BalanceBarParams {
    // Balance_Bar_Params (spec 10): 12 f32 fields. The spec lists their
    // names in an ABBREVIATED form ("Balanced_Region_Size, _Size_Change,
    // _Min_Size, _Acceleration, Balanced_Acceleration_Change, _Max,
    // Unbalanced_Region_Acceleration, Unbalanced_Acceleration_Change, _Max,
    // Balancing_Acceleration, _Change, _Max") using a leading-underscore
    // shorthand for "same prefix as the nearest preceding full name" that
    // this spec does not itself define. The full element names below are
    // this reader's RECONSTRUCTION of that shorthand (HIGH CONFIDENCE, not
    // itself spec-verbatim) - see src/tables_vehicle_world.cpp and this
    // library's report for the reasoning, and
    // tools/validation/validate_tables_vehicle_world_population.cpp, which
    // checks these reconstructed names against real archive data as a
    // real-world test of the guess. A SEPARATE, unrelated SR2-legacy asset
    // lookup ("sr2_balance_meter") also happens after this block is read;
    // that part is not XML content and is not modelled here.
    Always<float> balancedRegionSize;         // Balanced_Region_Size
    Always<float> balancedRegionSizeChange;    // reconstructed: Balanced_Region_Size_Change
    Always<float> balancedRegionMinSize;       // reconstructed: Balanced_Region_Min_Size
    Always<float> balancedRegionAcceleration;  // reconstructed: Balanced_Region_Acceleration
    Always<float> balancedAccelerationChange;  // Balanced_Acceleration_Change (spec-verbatim)
    Always<float> balancedAccelerationMax;     // reconstructed: Balanced_Acceleration_Max
    Always<float> unbalancedRegionAcceleration;  // Unbalanced_Region_Acceleration (spec-verbatim)
    Always<float> unbalancedAccelerationChange;  // Unbalanced_Acceleration_Change (spec-verbatim)
    Always<float> unbalancedAccelerationMax;     // reconstructed: Unbalanced_Acceleration_Max
    Always<float> balancingAcceleration;         // Balancing_Acceleration (spec-verbatim)
    Always<float> balancingAccelerationChange;   // reconstructed: Balancing_Acceleration_Change
    Always<float> balancingAccelerationMax;      // reconstructed: Balancing_Acceleration_Max
};

struct VehicleSurfing {
    Always<float> maxSurfingTime, minSurfingTime;              // Max_Surfing_Time / Min_Surfing_Time
    Always<float> maxHandstandTime, minHandstandTime;          // Max_Handstand_Time / Min_Handstand_Time
    Always<float> maxHandstandPercentReward;                    // Max_Handstand_Percent_Reward
    Always<float> surfingMinSpeed;                              // Surfing_Min_Speed (RAW mph-ish value pre-square)
    Always<float> surfingTimeout;                                // Surfing_Timeout
    Always<float> startTime;                                    // Start_Time
    Always<float> maxSpeedDelay;                                 // Max_Speed_Delay
    std::optional<float> recordDisplayTime;                      // Record_Display_Time (if-present, default 0)
    float RecordDisplayTimeOrDefault() const { return recordDisplayTime.value_or(0.0f); }
    std::optional<float> recordQueueTime;                        // Record_Queue_Time (if-present, default 0)
    float RecordQueueTimeOrDefault() const { return recordQueueTime.value_or(0.0f); }
    Always<float> recordThreshold;                                // Record_Threshold
    Always<float> handstandingSensitivityMultiplier;              // Handstanding_Sensitivity_Multiplier
    Always<int32_t> maxRespect;                                    // Max_Respect
    Always<int32_t> maxLifetimeRespect;                            // Max_Lifetime_Respect
    Always<float> maxCash;                                         // Max_Cash
    BalanceBarParams balanceBarParams;                             // Balance_Bar_Params (nested block)
};

// Whole-file, single <Vehicle_Surfing> element under <Table> (spec 10);
// nullopt if absent (matches tables_environment.h's ParseMotionBlurSettings
// no-op convention for a missing single-block element).
std::optional<VehicleSurfing> ParseVehicleSurfing(const Document& doc);

}  // namespace sr3tables_vehicle_world

#include "sr3tables_vehicle_world/lightset.h"
#include "sr3tables_vehicle_world/world_items.h"
