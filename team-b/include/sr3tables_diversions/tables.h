#pragma once

// sr3tables_diversions - typed readers for the side-activities / diversions /
// stunt-scoring `.xtbl` table group.
//
// Source: spec-tables-diversions.md (SPEC TEAM, agent AR), the ONLY source
// consulted for this file's schema (cleanroom boundary - see HARD RULES in
// the task that produced this file). Built entirely on top of
// include/sr3xtbl/xtbl.h's document model and accessors (Node, FindChild,
// ChildText, ReadXAlways/GetX, FlagMask/EnumIndex, NameHash); nothing here
// was derived from the game executable, disassembly or decompiled code -
// every offset/address/FUN_00XXXXXX cited in a comment is copied verbatim
// from the spec's own prose, purely to let a reader cross-reference the spec
// section, never re-derived.
//
// CONVENTION (matches sr3xtbl.h and sr3tables_environment/tables.h exactly):
//   * sr3xtbl::Always<T> (value + present) = the spec's "always write"
//     elements (the accessor catalogue in spec 1.3). When !present the
//     ENGINE's real behaviour is an indeterminate stack-residue value (the
//     "Always"-hazard already documented for this whole project), NOT
//     sr3xtbl::Always<T>'s 0 stand-in.
//   * std::optional<T> = the spec's "write only if present" elements, PLUS
//     (per the environment reader's precedent) any element that the spec
//     states has a real, spec-confirmed concrete default distinct from the
//     Always-hazard (e.g. the stunt family's Record_Display_Time/
//     Record_Queue_Time, spec 1.2 item 3/1.3: "every 'if present' call ...
//     is preceded by an explicit zero of the destination, so an absent
//     element reliably yields 0 here"). A small `...OrZero()`/`...OrDefault()`
//     helper applies the spec's own documented constant where one is given.
//
// UNIT CONVERSIONS ARE DELIBERATELY NOT APPLIED. Every unit-conversion
// constant this group uses (seconds->ms, percent->fraction, mph->(m/s)^2,
// degrees->radians, the 180-degree/90-degree clamp-and-supplement idioms,
// the various floor/reset-pair validation idioms) is spec 1.4/2.1: role
// CONFIRMED by usage pattern. UPDATED 2026-10-02 (spec 1.4, re-derived from
// the executable): the ORIGINAL premise here - that every constant's exact
// value reads as zero in the static image, filled only at start-up - was
// itself wrong and has been corrected in the spec; nearly every constant's
// exact value (1000.0 / -1000.0 / 100.0 / 0.44704 / ~0.017453 / 180.0 / 90.0
// / 1.0, per role, genuinely a mix of f64 and f32 widths by role not a
// uniform width) is now directly readable and spec-CONFIRMED. That does NOT
// change the modelling choice below, though: this project's standing "only
// CONFIRMED facts should drive parsing, derived/computed values are not
// modelled" rule (see sr3tables_environment/tables.h) still applies - the
// struct fields hold the RAW as-authored XML value (seconds, percent, mph,
// degrees, ...) regardless of whether the conversion constant is known,
// with a comment citing the spec's stated transform; validation/reset/clamp
// logic that only makes sense after two fields are compared together
// (Min/Max reset pairs, acceptance predicates on grid rows, capacity caps)
// is likewise a load-time step, not modelled - each is called out in a
// comment at the field/table it applies to and is left to the validation
// harness to report against real data.
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> for repeated-row tables, and ParseXxx(const
// sr3xtbl::Document&) -> optional<Xxx> for the group's single-row tables
// (every stunt_* table, barnstorming, base_jumping, cat_and_mouse,
// exploration_diversion, mugging_diversion, streaking, photo_op,
// escort_name_generator, fraud_globals). All definitions are in
// src/tables_diversions.cpp.
//
// SCOPE: all 25 tables assigned to this group (spec 1.1) are covered by some
// struct below or in horde_mode.h - see each section's own header comment
// for what is modelled vs deliberately left OPEN/skipped (mainly
// horde_mode.xtbl's second loader half, spec 8.1/15 item 1, and fraud_globals'
// three un-decompiled sub-readers, spec 10/15 item 3 - both schema-known from
// the tables' own shipped TableDescription regardless of loader-offset
// coverage, which is enough to write an element-name-driven reader; see
// horde_mode.h's own banner for exactly what subset of horde_mode's huge
// schema is modelled).

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

// ===========================================================================
// Shared shapes (spec 1.2 items 2-3, reused by the 9 stunt_* tables and, for
// the record triad only, by base_jumping.Gen_Info - spec 4).
// ===========================================================================

// Reward triad (spec 1.2 item 2): present in all 9 stunt_* tables. RAW values
// (the floor-at-0/0.0 clamps are a load-time step, not modelled).
struct RewardTriad {
    Always<int32_t> maxRespect;          // Max_Respect
    Always<int32_t> maxLifetimeRespect;  // Max_Lifetime_Respect
    Always<float> maxCash;               // Max_Cash
};

// Record-notification triad (spec 1.2 item 3): present in 8 of the 9
// stunt_* tables (absent only from stunt_hijacking, spec 2.3) and reused by
// base_jumping.Gen_Info (spec 4). Record_Display_Time/Record_Queue_Time are
// write-if-present with a REAL spec-stated zero default (spec 1.3 - the
// destination is pre-zeroed by the loader itself before the read, not the
// general Always-hazard); Record_Threshold is read by the always accessor.
// All three kept RAW as authored (seconds / percent 0-100) - see the file
// banner on why the seconds->ms / percent->fraction conversions are not
// applied here. stunt_peel_out's Record_Threshold does NOT use this struct -
// see StuntPeelOut below and spec 1.5's documented engine quirk.
struct RecordNotificationTriad {
    std::optional<float> displayTimeSeconds;  // Record_Display_Time
    std::optional<float> queueTimeSeconds;    // Record_Queue_Time
    float DisplayTimeSecondsOrZero() const { return displayTimeSeconds.value_or(0.0f); }
    float QueueTimeSecondsOrZero() const { return queueTimeSeconds.value_or(0.0f); }
    Always<float> thresholdPercent;  // Record_Threshold (percent 0-100; ->fraction at load, spec 1.4)
};

// ===========================================================================
// 2.2 stunt_drifting.xtbl - row Stunt_Drifting (spec-tables-diversions.md 2.2)
// ===========================================================================
struct StuntDrifting {
    Always<float> minTimeSeconds, maxTimeSeconds;  // Min_Time/Max_Time (seconds; ->ms at load;
                                                     // reset-to-0/0 pair if Max<=0 or Max<Min - not
                                                     // modelled, spec 1.2 item 4)
    Always<float> minSpeedMph;                       // Min_Speed (mph; ->(m/s)^2 at load, spec 1.4)
    Always<float> maxTimeNotDriftingSeconds;         // Max_Time_Not_Drifting (seconds; ->ms at load)
    Always<float> minDriftAngleDegrees;              // Min_Drift_Angle (degrees; ->radians then an
                                                       // unidentified trig function at load - spec
                                                       // 2.2 item 5, OPEN which function)
    RecordNotificationTriad record;
    RewardTriad reward;
};
StuntDrifting ParseStuntDrifting(const Node* row);
std::optional<StuntDrifting> ParseStuntDriftingTable(const Document& doc);

// ===========================================================================
// 2.3 stunt_hijacking.xtbl - row Stunt_Hijacking (spec 2.3)
// ===========================================================================
// The ONLY member of the family with no Record_* triad (spec 2.3).
struct StuntHijacking {
    Always<float> hudTimeSeconds;       // Hud_Time (seconds; ->ms at load, floor 0)
    Always<float> cooldownTimeSeconds;  // Cooldown_Time - RAW seconds as authored. CORRECTED
                                         // 2026-10-02 (spec 2.3, re-derived from the executable):
                                         // the engine stores it combined as Hud_Time_ms +
                                         // Cooldown_Time_ms (ADDITION, a "cooldown ends at"
                                         // timestamp) - NOT subtraction as earlier text here said.
                                         // The distinct constant DAT_01115368 (used instead of the
                                         // general DAT_012a2d90) is itself -1000.0, i.e.
                                         // round(Cooldown_Time_s * -1000.0) = -Cooldown_Time_ms, and
                                         // Hud_Time_ms - (-Cooldown_Time_ms) = Hud_Time_ms +
                                         // Cooldown_Time_ms. No clamp exists or is needed (both
                                         // operands are floor-clamped >=0, so the sum can never go
                                         // negative). The combined derived value is not computed
                                         // here regardless (spec 2.3).
    Always<bool> onlyRewardHijacks;      // Only_Reward_Hijacks (schema-documented default True;
                                         // still read by the always accessor, so absence is the
                                         // general Always-hazard, not this schema default - spec 2.3)
    RewardTriad reward;
};
StuntHijacking ParseStuntHijacking(const Node* row);
std::optional<StuntHijacking> ParseStuntHijackingTable(const Document& doc);

// ===========================================================================
// 2.4 stunt_near_miss.xtbl - row Near_Miss (spec 2.4)
// ===========================================================================
struct StuntNearMiss {
    Always<int32_t> minVehicles, maxVehicles;  // Min_Vehicles/Max_Vehicles (reset pair if Max<1
                                                 // or Max<Min - not modelled)
    Always<float> minSpeedMph;                  // Min_Speed (mph; ->(m/s)^2)
    Always<float> relativeSpeedMph;             // Relative_Speed (mph; ->(m/s)^2)
    Always<float> vehicleDistance;               // Vehicle_Distance - ONE XML element; the engine
                                                 // broadcasts it into 4 separate destination globals
                                                 // (a contiguous 16-byte array of four f32s, written
                                                 // via one broadcast MOVAPS - CONFIRMED 2026-10-02,
                                                 // spec 2.4, re-derived from the executable). CORRECTED
                                                 // 2026-10-02: the stored value is PLAIN, not squared -
                                                 // floored to >=0.0 then copied unmodified into all 4
                                                 // slots (unlike Min_Speed/Relative_Speed in this same
                                                 // function, which genuinely are squared). HIGH
                                                 // CONFIDENCE per-side/quadrant hit-test radii; exact
                                                 // meaning of the 4 slots OPEN. The fan-out itself is a
                                                 // runtime-layout detail, not modelled; this is the
                                                 // single XML value as read
    Always<float> maxTimeBetweenMissesSeconds, minTimeBetweenMissesSeconds;  // seconds; ->ms
    Always<float> delayAfterCrashSeconds;         // Delay_After_Crash (seconds; ->ms)
    Always<float> delayAfterCompletionSeconds;    // Delay_After_Completion (seconds; ->ms)
    RecordNotificationTriad record;
    RewardTriad reward;
};
StuntNearMiss ParseStuntNearMiss(const Node* row);
std::optional<StuntNearMiss> ParseStuntNearMissTable(const Document& doc);

// ===========================================================================
// 2.5 stunt_back_seat_driver.xtbl - row Back_Seat_Driver (spec 2.5)
// ===========================================================================
struct StuntBackSeatDriver {
    Always<float> maxDistance, minDistance;  // reset pair (not modelled)
    Always<float> minSpeedMph;                // Min_Speed (mph; ->(m/s)^2)
    Always<float> endTimeSeconds;             // End_Time (seconds; ->ms)
    RecordNotificationTriad record;
    RewardTriad reward;
};
StuntBackSeatDriver ParseStuntBackSeatDriver(const Node* row);
std::optional<StuntBackSeatDriver> ParseStuntBackSeatDriverTable(const Document& doc);

// ===========================================================================
// 2.6 stunt_oncoming.xtbl - row Stunt_Oncoming (spec 2.6)
// ===========================================================================
struct StuntOncoming {
    Always<float> maxDistance, minDistance;  // reset pair (not modelled)
    Always<float> minSpeedMph;                // Min_Speed (mph; ->(m/s)^2)
    Always<int32_t> maxDamage;                // Max_Damage (floor 0)
    Always<float> angleToleranceDegrees;      // Angle_Tolerance - RAW degrees as authored; the
                                               // engine clamps [0,180] then stores its own
                                               // supplement (180 - x), no radian conversion (spec
                                               // 1.4/2.6) - the clamp+supplement is not applied here
    Always<float> laneTolerance;              // Lane_Tolerance - RAW as authored; the engine stores
                                               // it SQUARED, no clamp (spec 2.6) - not applied here
    Always<float> maxTimeNotOncomingSeconds;  // Max_Time_Not_Oncoming (seconds; ->ms)
    RecordNotificationTriad record;
    RewardTriad reward;
};
StuntOncoming ParseStuntOncoming(const Node* row);
std::optional<StuntOncoming> ParseStuntOncomingTable(const Document& doc);

// ===========================================================================
// 2.7 stunt_peel_out.xtbl - row Peel_Out (spec 2.7)
// ===========================================================================
struct StuntPeelOut {
    Always<int32_t> maxSpectators, minSpectators;  // reset pair; schema MaxValue=15 (not enforced)
    Always<float> maxWatchDistance;                 // Max_Watch_Distance (floor 0)
    Always<float> instanceDelaySeconds;              // Instance_Delay (seconds; ->ms)
    std::optional<float> recordDisplayTimeSeconds, recordQueueTimeSeconds;  // same pre-zeroed idiom
                                                                              // as RecordNotificationTriad
    float RecordDisplayTimeSecondsOrZero() const { return recordDisplayTimeSeconds.value_or(0.0f); }
    float RecordQueueTimeSecondsOrZero() const { return recordQueueTimeSeconds.value_or(0.0f); }
    // Record_Threshold: spec 1.5's documented ENGINE QUIRK. Unlike every
    // sibling stunt_* table (which treats this field as a percent 0-100
    // value that gets ->fraction at load), THIS table's loader
    // (FUN_006C0CF0) runs it through the exact same seconds->ms
    // ROUND(x*k) idiom used for Instance_Delay immediately above it in the
    // same function - almost certainly a copy-paste slip in the original
    // source (the shipped TableDescription documents this field identically
    // to every sibling table), not a deliberate design difference. The RAW
    // authored value is kept here exactly like every other table's
    // Record_Threshold; ONLY the downstream conversion differs, and neither
    // conversion is applied by this reader (file banner) - so this field's
    // representation is identical in shape to RecordNotificationTriad's, but
    // it is intentionally NOT that shared type, to keep this quirk visible
    // at the type level for anyone building a byte-faithful engine
    // reimplementation on top of this reader. PARTIALLY CONFIRMED 2026-10-02
    // (spec 1.5, re-derived from the executable): the destination is now
    // settled as a plain 4-byte dword (not a float, not 8 bytes), floored to
    // >=0.0 before the multiply so no sign clamp is needed; the field's
    // downstream consumer (percent vs. ms) remains OPEN. Neither changes
    // this reader's raw-value modelling.
    Always<float> recordThresholdRaw;
    RewardTriad reward;
};
StuntPeelOut ParseStuntPeelOut(const Node* row);
std::optional<StuntPeelOut> ParseStuntPeelOutTable(const Document& doc);

// ===========================================================================
// 2.8 stunt_wheels.xtbl - row Stunt_Wheels (spec 2.8)
// ===========================================================================
// A single sub-stunt block (Two_Wheels / Wheelie / Stoppie), each holding its
// OWN nested copy of the reward triad (spec 2.8: "the same reward triad as
// every other stunt table, nested three times").
struct StuntWheelsSubStunt {
    Always<float> maxDistance, minDistance;  // reset pair (not modelled)
    Always<float> stuntToleranceSeconds;      // Stunt_Tolerance (seconds; ->ms)
    Always<float> endTimeSeconds;             // End_Time (seconds; ->ms)
    RewardTriad reward;
};
// NOTE on the spec's own field count: spec 2.8's original text stated "7/7
// top-level + 3x7/7 nested = 28/28 real-row fields accounted for" while only
// NAMING 3 top-level fields (the record triad, at fixed offsets
// +0x68/+0x6C/+0x70) - a self-reported spec-internal inconsistency this
// reader deliberately did not try to resolve by guessing. **CORRECTED
// 2026-10-02** (spec 2.8, re-derived from the executable, FUN_006C16A0):
// this is now settled, not just sidestepped - no instruction in the loader
// touches any offset below +0x14 (where the sub-stunt loop begins), so there
// is no disassembly support for "~4 more top-level fields" at all. The
// "7/7 top-level" framing does not hold; only the 3 named fields (the record
// triad) are actually read from XML by this function. The modelling below
// (3 top-level fields) was already correct by construction - this note is
// updated so it no longer describes an open question.
struct StuntWheels {
    RecordNotificationTriad record;  // +0x68/+0x6C/+0x70 (see the NOTE above re: "7/7 top-level")
    StuntWheelsSubStunt twoWheels;   // Two_Wheels
    StuntWheelsSubStunt wheelie;     // Wheelie
    StuntWheelsSubStunt stoppie;     // Stoppie
};
StuntWheels ParseStuntWheels(const Node* row);
std::optional<StuntWheels> ParseStuntWheelsTable(const Document& doc);

// ===========================================================================
// 2.9 stunt_jumping_diversion.xtbl - row Stunt_Jumping (spec 2.9)
// ===========================================================================
inline constexpr std::array<std::string_view, 3> kStuntJumpingVehicleTypeNames = {
    "Car", "Motorcycle", "Boat",
};
// One of the six identically-shaped per-axis sub-blocks inside a Vehicle_Type
// entry (Land_Distance, Air_Height, Fall_Dist, Pitch_Rotation, Roll_Rotation,
// Spin_Rotation - spec 2.9, confirmed by the literal-pointer table
// PTR_s_Land_Distance_...).
struct StuntJumpingAxis {
    Always<float> maxValue, minValue;  // Max_Value/Min_Value (reset pair, not modelled)
    std::optional<float> maxPercent;    // Max_Percent (percent 0-100; ->fraction + an additional
                                         // floor clamp at load, spec 1.4/2.9 - write-if-present)
};
// spec 2.9: the three ROTATION sub-blocks (Pitch/Roll/Spin, indices 3-5) are
// converted degrees->radians at load; the three distance/height sub-blocks
// (indices 0-2) are left unconverted. Neither conversion is applied here
// (file banner) - the RAW authored values are identical either way.
struct StuntJumpingVehicleType {
    std::optional<std::string> name;  // Vehicle_Name - matched by name against
                                       // kStuntJumpingVehicleTypeNames. Spec 2.9 says "matched by
                                       // name" but does not give the exact XML element holding that
                                       // name; this project's HARD RULE forbids consulting the
                                       // executable, but real shipped game DATA is explicitly fair
                                       // game (task scope) - this reader's own validation harness
                                       // (tools/validation/validate_tables_diversions_population.cpp)
                                       // was run against the real da_tables.vpp_pc copy and found the
                                       // element is "Vehicle_Name", NOT "Name" (an initial "Name"
                                       // guess, by analogy with every other named Grid row in this
                                       // group, was WRONG and is corrected here from that real-data
                                       // read - CONFIRMED-empirical, not from the executable).
    StuntJumpingAxis landDistance;    // Land_Distance (index 0, unconverted)
    StuntJumpingAxis airHeight;       // Air_Height (index 1, unconverted)
    StuntJumpingAxis fallDist;        // Fall_Dist (index 2, unconverted)
    StuntJumpingAxis pitchRotation;   // Pitch_Rotation (index 3, degrees->radians at load)
    StuntJumpingAxis rollRotation;    // Roll_Rotation (index 4, degrees->radians at load)
    StuntJumpingAxis spinRotation;    // Spin_Rotation (index 5, degrees->radians at load)
};
struct StuntJumpingDiversion {
    Always<float> endTimeSeconds;  // End_Time - NOT part of the record (CORRECTED 2026-10-02,
                                    // spec 2.9): it is a separate flat global (DAT_014c74c0),
                                    // same as the reward triad below; only kept here as a
                                    // struct field for convenience, grouped with its sibling
                                    // table fields the way every other table in this reader is.
    RecordNotificationTriad record;  // +0x14/+0x18/+0x1C exactly (CORRECTED 2026-10-02, spec
                                      // 2.9, re-derived from the executable - was "+0x18/+0x1C/
                                      // +0x20ish"; this is the ONLY part of the schema actually
                                      // carried through the record pointer)
    RewardTriad reward;  // Also NOT part of the record - 3 more separate flat globals
                          // (DAT_014c74c4/_c8/_cc), same correction as End_Time above
    // Vehicle_Types - capped at EXACTLY 3 (matches TableDescription's
    // Max_Children=3, Min_Children=3; real base row has all 3: Car,
    // Motorcycle, Boat, each with all 6 sub-blocks present, spec 2.9/14).
    // CORRECTED 2026-10-02 (spec 2.9): the grid itself is ALSO not part of
    // the record - it is a separate flat-global array, each matched vehicle
    // type written at base+matched_index*0x48 (slot = literal-table match
    // index: Car=0/Motorcycle=1/Boat=2, NOT XML encounter order). An
    // unmatched Vehicle_Name is silently skipped (no write, no error); a
    // repeated name silently overwrites that same index's slot again (no
    // duplicate guard). This vector preserves XML encounter order rather
    // than index-assignment order/overwrite semantics - a runtime-layout
    // detail not modelled here (file banner); for the real base row (one
    // each of Car/Motorcycle/Boat) the two orderings coincide. The cap is
    // not enforced by this struct either - see the file banner.
    std::vector<StuntJumpingVehicleType> vehicleTypes;
};
StuntJumpingDiversion ParseStuntJumpingDiversion(const Node* row);
std::optional<StuntJumpingDiversion> ParseStuntJumpingDiversionTable(const Document& doc);

// ===========================================================================
// 2.10 stunt_low_flying.xtbl - row Low_Flying (spec 2.10)
// ===========================================================================
struct StuntLowFlying {
    Always<float> maxDistance, minDistance;  // reset pair (not modelled)
    Always<float> verticalityMult;            // Verticality_Mult - RAW as authored; the engine
                                               // floors it at a shared constant then SUBTRACTS 1.0,
                                               // stored as "bonus over 1" (spec 2.10) - not applied
    std::optional<float> maxHeight;            // Max_Height - write-if-present; absent/<=0 falls
                                               // back to the shared floor constant DAT_01117A4C, a
                                               // non-zero non-schema default (spec 2.10) - kept as
                                               // std::optional (no OrDefault helper: the constant's
                                               // exact value is OPEN, spec 1.4 row DAT_01117a4c)
    std::optional<float> activationHeight;     // Activation_Height - same absent-fallback behaviour
    Always<float> minSpeedMph;                 // Min_Speed (mph; ->(m/s)^2)
    Always<float> maxTimeNotLowFlyingSeconds;  // Max_Time_Not_Low_Flying (seconds; ->ms)
    Always<float> completionCooldownSeconds;   // Completion_Cooldown (seconds; ->ms)
    RecordNotificationTriad record;
    RewardTriad reward;
};
StuntLowFlying ParseStuntLowFlying(const Node* row);
std::optional<StuntLowFlying> ParseStuntLowFlyingTable(const Document& doc);

// ===========================================================================
// 3. barnstorming.xtbl - row Barnstorming (spec-tables-diversions.md 3)
// ===========================================================================
// NOTE (spec 3): unlike the whole stunt_* family, this table has NO separate
// Max_Lifetime_Respect / cash-instead-of-respect mechanism - the shipped
// TableDescription's own field order and text confirm Max_Respect/Max_Cash
// are the ONLY two reward fields here. This is a genuine table-design
// difference, not merely a field the loader failed to read.
struct Barnstorming {
    std::optional<float> maxDistance;    // Max_Distance - absent/<=0 falls back to a shared
                                          // constant, NOT 0 (spec 3) - kept optional, no OrDefault
                                          // (constant value OPEN)
    Always<float> minSpeedMph;            // Min_Speed (mph; ->(m/s)^2)
    Always<int32_t> maxDamage;            // Max_Damage (floor 0)
    Always<float> crashDelaySeconds;      // Crash_Delay (seconds; ->ms)
    Always<float> restartDelaySeconds;    // Restart_Delay (seconds; ->ms)
    Always<float> knifeEdgeToleranceDegrees;  // Knife_Edge_Tolerance - RAW degrees; clamped
                                               // [0,90] (schema MaxValue=90) then ->radians at
                                               // load, not applied here
    Always<float> knifeEdgeMultiplier;         // Knife_Edge_Multiplier - RAW; floored at a shared
                                               // ~1.0 constant at load, not applied here
    Always<float> invertedToleranceDegrees;    // Inverted_Tolerance - same clamp+convert as
                                               // Knife_Edge_Tolerance
    Always<float> invertedMultiplier;          // Inverted_Multiplier - same floor as
                                               // Knife_Edge_Multiplier
    Always<bool> allowReplay;                  // Allow_Replay
    Always<int32_t> maxRespect;                // Max_Respect (floor 0)
    Always<float> maxCash;                     // Max_Cash (floor 0.0)
};
Barnstorming ParseBarnstorming(const Node* row);
std::optional<Barnstorming> ParseBarnstormingTable(const Document& doc);

// ===========================================================================
// 4. base_jumping.xtbl - row Base_Jumping, 4 sub-blocks (spec 4)
// ===========================================================================
inline constexpr std::array<std::string_view, 5> kBaseJumpingIndicatorFlagNames = {
    "Sticky", "Display Distance", "Pulse", "Fade", "Partial Hide",
};
struct BaseJumpingGenInfo {
    Always<float> startTimeSeconds;            // Start_Time (seconds; ->ms)
    Always<float> completeDisplayTimeSeconds;  // Complete_Display_Time (seconds; ->ms)
    Always<float> minAltitude, maxAltitude;    // Min_Altitude/Max_Altitude (Max clamped >= Min,
                                                // not modelled)
    Always<float> yTolerance;                  // Y_Tolerance (floor 0)
    RecordNotificationTriad record;
};
struct BaseJumpingTargetLocationIndicator {
    std::optional<std::string> indicatorType;  // Indicator_Type - an 8-entry enum matched by name;
                                                // the spec does not enumerate all 8 choices (only
                                                // that the real row uses "Location"), so no name
                                                // table is exposed here - kept as raw text
    uint32_t indicatorFlags = 0;                // Indicator_Flags - FlagMask over
                                                // kBaseJumpingIndicatorFlagNames (5 entries, spec 4)
};
struct BaseJumpingSecondaryEffects {
    std::optional<std::string> effectName;  // Effect_Name (resolved via the shared effects-name
                                             // resolver, spec 1.3/4)
    std::optional<int32_t> numEffects;       // Num_Effects (floor 1)
    std::optional<float> effectsDist;        // Effects_Dist (floor 0.0)
    // spec 4: "all three only stored if the effect name resolves" - that
    // load-time gate is not evaluated here; whatever text is present in the
    // XML is surfaced regardless of whether Effect_Name would have resolved.
};
struct BaseJumpingTargetInfo {
    Always<float> minTargetDistance;         // Min_Target_Distance (floor 0)
    Always<float> minTargetCheckRadius;      // Min_Target_Check_Radius (floored at a shared ~1.0
                                              // constant)
    std::optional<float> maxTargetDistance;   // Max_Target_Distance - defaults to a shared constant
                                              // if absent; clamped >= Min_Target_Distance +
                                              // Min_Target_Check_Radius (not modelled) - kept
                                              // optional (constant value OPEN, no OrDefault)
    Always<float> minTargetPercent, maxTargetPercent;  // Min_/Max_Target_Percent - RAW percent
                                                        // 0-100; clamped then ->fraction then
                                                        // floored again at load (spec 1.4/4), not
                                                        // applied here
    Always<bool> disableHighwaySpawn;         // Disable_Highway_Spawn
    bool targetLocationIndicatorPresent = false;         // Target_Location_Indicator - whole block optional
    BaseJumpingTargetLocationIndicator targetLocationIndicator;
    std::optional<std::string> primaryEffect;  // Primary_Effect (Reference via the shared effects
                                                // resolver; defaults to "none"/0xFFFFFFFF if absent,
                                                // spec 1.3/4)
    bool secondaryEffectsPresent = false;
    BaseJumpingSecondaryEffects secondaryEffects;
};
struct BaseJumpingVehInfo {
    Always<float> minVehDistance, maxVehDistance;  // Min_/Max_Veh_Distance (Max clamped >= Min,
                                                    // not modelled)
};
struct BaseJumpingRewardTier {
    Always<float> maxDistance;   // Max_Distance
    Always<int32_t> respect;     // Respect
    Always<float> cash;          // Cash (the same &DAT_01126FA4="Cash" literal streaking.xtbl uses,
                                  // spec 1.4)
};
struct BaseJumpingRewardInfo {
    Always<int32_t> maxLifetimeRespect;  // Max_Lifetime_Respect (floor 0)
    // Reward_Tiers - a Grid of Reward_Tier, capacity EXACTLY 10 (matches
    // TableDescription's Max_Children=10; real base row has 9, in strictly
    // descending Max_Distance order, spec 4/14). A tier's acceptance
    // predicate (Max_Distance>0 AND strictly-descending AND Respect>=0 AND
    // Cash>=0 AND (Respect>0 OR Cash>0)) is a load-time filter, NOT applied
    // here - every Reward_Tier element found is surfaced raw, accepted or not.
    std::vector<BaseJumpingRewardTier> rewardTiers;
};
struct BaseJumping {
    bool genInfoPresent = false;
    BaseJumpingGenInfo genInfo;
    bool targetInfoPresent = false;
    BaseJumpingTargetInfo targetInfo;
    bool vehInfoPresent = false;
    BaseJumpingVehInfo vehInfo;
    bool rewardInfoPresent = false;
    BaseJumpingRewardInfo rewardInfo;
};
BaseJumping ParseBaseJumping(const Node* row);
std::optional<BaseJumping> ParseBaseJumpingTable(const Document& doc);

// ===========================================================================
// 5. cat_and_mouse.xtbl - row Cat_And_Mouse (spec 5)
// ===========================================================================
struct CatAndMouseVehicleMatchup {
    std::optional<std::string> catVehicle;      // Cat_Vehicle (Reference into vehicles.xtbl,
                                                 // hashed via the shared CRC-32 name-hash, seed 0)
    Always<int32_t> catVehicleHealth;            // Cat_Vehicle_Health ("0 = default" per its own
                                                 // TableDescription text)
    std::optional<std::string> mouseVehicle;    // Mouse_Vehicle (hashed identically)
    Always<int32_t> mouseVehicleHealth;          // Mouse_Vehicle_Health
    Always<float> mouseMinSpeedMph;              // Mouse_Min_Speed - the ONE speed field in this
                                                 // ENTIRE group that is read with the plain f32
                                                 // accessor and never converted mph->m/s (spec
                                                 // 1.4/5) - stored raw either way in this reader,
                                                 // called out because it is a genuine engine
                                                 // behaviour difference, not a modelling choice
};
struct CatAndMouse {
    // Vehicle_Matchups - a grid capped at exactly 8 (real row uses 5 of 8,
    // spec 5/14); cap not enforced here.
    std::vector<CatAndMouseVehicleMatchup> vehicleMatchups;
    Always<float> triggerHoldStartDelaySeconds;  // Trigger_Hold_Start_Delay (seconds; ->ms)
    Always<float> roundTimeSeconds;               // Round_Time - f32, NO conversion (stays raw
                                                   // seconds per spec 5); schema MinValue=15
    Always<int32_t> roundsPerMatch;                // Rounds_Per_Match
    Always<float> cashReward;                      // Cash_Reward
    Always<float> mouseScoreIntervalSeconds;       // Mouse_Score_Interval (seconds; ->ms)
    Always<int32_t> mouseIntervalPoints;            // Mouse_Interval_Points
    Always<int32_t> mouseNavpointPoints;            // Mouse_Navpoint_Points
    Always<uint32_t> mouseNavpointTimeAddedMs;      // Mouse_Navpoint_Time_Added_ms - u32, read
                                                   // DIRECTLY with NO conversion (already
                                                   // milliseconds in the XML - spec 5's control
                                                   // case for the seconds->ms constant's role)
    Always<float> mouseNavpointStartDistance;       // Mouse_Navpoint_Start_Distance (unconverted m)
    Always<float> mouseNavpointDistanceIncrement;   // Mouse_Navpoint_Distance_Increment (unconverted m)
    Always<float> catSpawnDist;                     // Cat_Spawn_Dist (unconverted m)
};
CatAndMouse ParseCatAndMouse(const Node* row);
std::optional<CatAndMouse> ParseCatAndMouseTable(const Document& doc);

// ===========================================================================
// 6.1 exploration_diversion.xtbl - row "Exploration" (spec 6.1)
// ===========================================================================
// NOTE (spec 6.1): the internal row tag is "Exploration", NOT
// "Exploration_Diversion" - the only table in this group whose row tag
// doesn't match a simple transform of its filename/TableDescription name.
struct ExplorationDiversion {
    Always<float> maxExplorationDist;     // Max_Exploration_Dist (floor 0)
    Always<float> messageTimeSeconds;     // Message_Time (seconds; ->ms)
    Always<int32_t> districtRespect;      // District_Respect (floor 0)
    Always<int32_t> hoodRespect;          // Hood_Respect (floor 0)
    Always<int32_t> secretLocationRespect; // Secret_Location_Respect (floor 0)
    Always<int32_t> stuntJumpRespect;      // Stunt_Jump_Respect (floor 0)
    Always<float> secretLocationCash;      // Secret_Location_Cash (floor 0.0)
    Always<float> stuntJumpCash;           // Stunt_Jump_Cash (floor 0.0)
};
ExplorationDiversion ParseExplorationDiversion(const Node* row);
std::optional<ExplorationDiversion> ParseExplorationDiversionTable(const Document& doc);

// ===========================================================================
// 6.2 mugging_diversion.xtbl - row Mugging_Diversion (spec 6.2)
// ===========================================================================
struct MuggingDiversion {
    Always<float> muggingStartTimeSeconds;     // Mugging_Start_Time (seconds; ->ms)
    Always<float> muggingCompleteTimeSeconds;  // Mugging_Complete_Time (seconds; ->ms)
    Always<float> barterLineSeconds;           // Barter_Line (seconds; ->ms)
    Always<float> cowerLineSeconds;            // Cower_Line (seconds; ->ms)
    Always<float> maxDistance;                 // Max_Distance - RAW as authored (floor 0); the
                                                // engine stores it SQUARED (a plain squared-distance
                                                // threshold, spec 6.2) - not applied here
    Always<float> defaultMinCash;              // Default_Min_Cash (floored at a shared constant)
    Always<float> defaultMaxCash;              // Default_Max_Cash (clamped >= Default_Min_Cash,
                                                // not modelled)
};
MuggingDiversion ParseMuggingDiversion(const Node* row);
std::optional<MuggingDiversion> ParseMuggingDiversionTable(const Document& doc);

// ===========================================================================
// 6.3 streaking.xtbl - row Streaking (spec 6.3)
// ===========================================================================
// Gated by the same existence check as escort_name_generator.xtbl (spec 1.3:
// FUN_00DA90D0/FUN_00DA8D90 - "this specific file may legitimately be
// absent"), not a data-shape concern for this reader.
struct StreakingLevel {
    Always<int32_t> shockQuota;    // Shock_Quota
    Always<float> timeLimitSeconds; // Time_Limit (seconds; ->ms)
    Always<int32_t> respect;        // Respect
    Always<float> cash;             // Cash (same &DAT_01126FA4="Cash" literal as base_jumping)
};
struct Streaking {
    Always<int32_t> maxLifetimeRespect;  // Max_Lifetime_Respect (floor 0)
    // Levels - a grid capped at exactly 10 (matches TableDescription's
    // Max_Children=10; real row uses 8 of 10, spec 6.3/14). A Level's
    // acceptance predicate (Shock_Quota>0 AND Time_Limit>=0 AND Respect>=0
    // AND (Cash>0 OR Respect>0)) - the SAME shared idiom as base_jumping's
    // Reward_Tier, spec 6.3/14 - is a load-time filter, not applied here.
    std::vector<StreakingLevel> levels;
};
Streaking ParseStreaking(const Node* row);
std::optional<Streaking> ParseStreakingTable(const Document& doc);

// ===========================================================================
// 7. photo_op.xtbl - row Photo_Op (spec 7)
// ===========================================================================
// Saints_Loved and Saints_Hated share this exact shape (spec 7: both read by
// one shared sub-function FUN_0091D520 called twice on two destinations).
struct PhotoOpAudience {
    std::vector<std::string> charPresetNames;      // Char_Presets/Char_Preset.Preset_Name
                                                    // (Reference into character.xtbl)
    std::vector<std::string> lineSituationNames;   // Line_Situations/Line_Situation.
                                                    // Line_Situation_Name (Reference into
                                                    // audio_line_tags.xtbl)
};
struct PhotoOp {
    PhotoOpAudience saintsLoved;  // Saints_Loved
    PhotoOpAudience saintsHated;  // Saints_Hated
    std::optional<std::string> triggerEffect;  // Trigger_Effect (Reference via the shared effects
                                                // resolver; defaults to "none"/0xFFFFFFFF if absent)
};
PhotoOp ParsePhotoOp(const Node* row);
std::optional<PhotoOp> ParsePhotoOpTable(const Document& doc);

// ===========================================================================
// 11. escort_name_generator.xtbl - row Name_Generator (spec 11)
// ===========================================================================
// Gated by the same existence check as streaking.xtbl (spec 1.3/11).
struct NaughtyName {
    std::optional<std::string> firstName;   // First_name (Reference into
                                             // localized_exports\SR2_Escort_names.xtbl, stored as
                                             // an owned, heap-duplicated string - not hashed)
    std::optional<std::string> secondName;  // Second_name (same convention)
};
struct EscortNameGenerator {
    // Naughty_names - capped at EXACTLY 0x28 = 40 entries, a capacity
    // confirmed ONLY from the disassembly (the shipped TableDescription does
    // not itself state a Max_Children for this grid - spec 11, a genuine
    // case where the loader trace adds information the schema alone would
    // not give). Real base row: 39 of 40. Cap not enforced here.
    std::vector<NaughtyName> naughtyNames;
};
EscortNameGenerator ParseEscortNameGenerator(const Node* row);
std::optional<EscortNameGenerator> ParseEscortNameGeneratorTable(const Document& doc);

// ===========================================================================
// 12. shop_names.xtbl - row Shop_Names, PER-ROW reader (spec 12)
// ===========================================================================
// The ONE table in this entire group that is a genuine multi-row table
// rather than a single config block (spec 12) - no fixed capacity, sized to
// the data (real base row: 41 rows).
inline constexpr std::array<std::string_view, 8> kShopTypeNames = {
    "clothing", "weapon", "surgeon", "jewelry", "tattoo", "mechanic", "vehicle dealer", "misc property",
};
struct ShopName {
    std::optional<std::string> name;            // Name (+0x00) - the row key, hashed with the
                                                 // shared CRC-32 name-hash, seed 0
    // A second Name-derived value at +0x30 via FUN_00821EE0. CORRECTED
    // 2026-10-02 (spec 12, re-derived from the executable): this is now
    // fully traced and is NOT a hash of any kind and NOT an id derivation -
    // it is a case-insensitive linear name search (stricmp) into an
    // unrelated external array (base DAT_022db998), returning a pointer into
    // that array on a match or null otherwise. What that array is and what
    // consumes the +0x30 pointer both remain OPEN (spec 12/15 item 4). Still
    // a purely derived/computed value, not a distinct XML element, so it is
    // NOT modelled here.
    std::optional<std::string> localizedName;   // Localized_Name (+0x04, heap-duplicated string);
                                                 // spec-stated concrete default "{localize}franchise"
    std::string LocalizedNameOrDefault() const { return localizedName.value_or("{localize}franchise"); }
    // Cost/Income/Discount/Total_Owner_Discount: CORRECTED 2026-10-02 (spec
    // 12, re-derived from the executable, FUN_00A00B90). The TableDescription
    // documents defaults of 10000/500/15/20 for these four, but - UNLIKE
    // Localized_Name's default above, which IS loader-enforced (passed as an
    // explicit fallback argument to its own text-getter accessor) - the
    // "always" accessors used for these four have NO visible default-fallback
    // branch in the loader. Those four numbers are schema-only documentation,
    // not runtime behaviour: on the absent path the real engine leaves
    // whatever was already at the destination (the general Always-hazard),
    // not a concrete 10000/500/15/20. Modelled as Always<T>, matching this
    // project's own stated convention (file banner) that optional+OrDefault
    // is reserved for a spec-CONFIRMED loader-enforced default only.
    Always<int32_t> cost;                 // Ownership.Cost (+0x10)
    Always<int32_t> income;               // Ownership.Income (+0x14)
    Always<float> discount;               // Ownership.Discount (+0x18) - RAW percent as authored;
                                           // ->fraction at load via the shared percent constant, not
                                           // applied here
    Always<float> totalOwnerDiscount;     // Ownership.Total_Owner_Discount (+0x1C) - RAW percent
    std::optional<std::string> reward;           // Ownership.Reward (+0x20, optional Reference into
                                                 // unlockables.xtbl, resolved via FUN_0071CC90)
    std::optional<std::string> useMessage;       // Use_Message (+0x28, optional, heap-duplicated
                                                 // string, Reference into Stores_text.xtbl)
    std::optional<std::string> minimapIcon;      // Minimap_Icon (+0x24, optional, resolved via
                                                 // FUN_00802F60, Reference into
                                                 // bitmap_sheets\ui_bms_01.xtbl)
    std::optional<std::string> shopType;         // Shop_Type (+0x2C) - an 8-entry enum, matched via
                                                 // the shared enum reader against kShopTypeNames
                                                 // (raw text kept here; index resolution is a
                                                 // validation-time concern, matching this codebase's
                                                 // kTimeOfDayObjectNames/kBitmapMaterialSlotNames
                                                 // precedent in sr3tables_environment)
};
ShopName ParseShopName(const Node* row);
std::vector<ShopName> ParseShopNamesTable(const Document& doc);

// ===========================================================================
// 9. The activity-text descriptor registry (spec 9): fraud_text.xtbl,
// snatch_text.xtbl, snatch_kinzie_text.xtbl, escort_text.xtbl.
// ===========================================================================
// spec 9: these four are NOT opened by a per-file XML-to-struct loader in
// the usual sense - each filename literal's xref is a small "activity
// descriptor" filler using literal constants, and the actual Texts-grid
// reader is a shared, unlocated function (HIGH CONFIDENCE, not CONFIRMED,
// per spec 9's own confidence line). That mechanism is NOT modelled here (it
// is not a parseable XML shape at all - see horde_mode.h's Powerup-style
// callback-vtable note for the same reasoning applied elsewhere in this
// group). What IS modelled is the EMPIRICALLY CONFIRMED shape of the four
// files' own shipped data (spec 9, direct from real da_tables.vpp_pc rows):
// one <String> row named after the activity (internal row-name literal is
// "Escort" even for the two "Snatch" files, spec 9) containing a Texts grid
// of Text{Name, English (optional), DisplayText, Duration}.
struct ActivityText {
    std::optional<std::string> name;         // Text.Name
    std::optional<std::string> english;      // Text.English (optional)
    std::optional<std::string> displayText;  // Text.DisplayText (a Reference into
                                              // localized_exports\SR2_Activity_text.xtbl)
    std::optional<float> duration;            // Text.Duration; spec-stated default 5.0
    float DurationOrDefault() const { return duration.value_or(5.0f); }
};
struct ActivityTextTable {
    std::optional<std::string> rowName;  // the row's own name (spec-confirmed "Escort" in every
                                          // real copy checked, but not hardcoded - kept raw)
    std::vector<ActivityText> texts;     // Texts/Text
};
// Row element name is "String" (spec 9: "one <String> row"); parses whatever
// single String row the document holds, for any of the four files.
ActivityTextTable ParseActivityText(const Node* row);
std::optional<ActivityTextTable> ParseActivityTextTable(const Document& doc);

// ===========================================================================
// 8.2 horde_mode_text.xtbl (spec 8.2) - a DIFFERENT shape from the four
// activity-text files above: a flat list of bare localization-key names,
// no Texts grid, no Duration/DisplayText (spec 8.2 flags this explicitly as
// "genuinely different"). Row element: Horde_Mode_Identifier.
// ===========================================================================
struct HordeModeIdentifier {
    std::optional<std::string> name;  // Horde_Mode_Identifier.Name (a bare HUD-string
                                       // localization key, e.g. "HORDE_MODE_WAVE_TAG")
};
HordeModeIdentifier ParseHordeModeIdentifier(const Node* row);
std::vector<HordeModeIdentifier> ParseHordeModeTextTable(const Document& doc);

// ===========================================================================
// 10. fraud_globals.xtbl - row Fraud_Values (spec 10)
// ===========================================================================
// spec 10: the loader's own entry point and its 3-way split into sub-readers
// is CONFIRMED (disassembly); the individual sub-readers were not
// decompiled, but the FULL schema is independently recovered from the
// table's own shipped TableDescription (real, structured XML documentation
// data - not disassembly), so it is implementable here regardless.
//
// The "tiered multiplier" shape reused 12 times (spec 10): an
// <X>_Multiplier_Element (the newer, tiered scheme) alongside an older FLAT
// single-tier <X>_Multiplier (no grid) for the SAME category. This reader
// models 11 of the 12 named categories (Vehicle, Airtime, Witness,
// Cop_Witness, Linear_Dist, Civil_Vehicle, Crazy_Driver, Windshield,
// Surfing, Pinball, Cliff_Diver); the spec's 12th category is only
// described as "(implicitly, the base multiplier set)" with NO stated
// element-name prefix, so it is left OPEN / not modelled here rather than
// guessed.
//
// IMPORTANT CORRECTION FROM REAL DATA (this reader's own validation
// harness, tools/validation/validate_tables_diversions_population.cpp, run
// against real shipped fraud_globals.xtbl DATA - never the executable, per
// the HARD RULE): spec 10's prose implies one uniform naming formula
// ("Minimum_<X>" for the tier's floor field, "<X>_Multiplier"/
// "<X>_Multiplier_Element" for the flat/tiered elements, where X is the
// category's own prefix). Real shipped data shows this formula does NOT
// hold verbatim per category - several categories pluralise or substitute a
// different noun in "Minimum_<X>" (e.g. Cop_Witness -> "Minimum_Cops",
// Vehicle -> "Minimum_Vehicles", Witness -> "Minimum_Witnesses",
// Civil_Vehicle -> "Minimum_Civil_Vehicles", Crazy_Driver ->
// "Minimum_Crazy_Drivers", Pinball -> "Minimum_Cars", Cliff_Diver ->
// "Minimum_Cliff_Distance"), and Windshield's flat/tiered-inner element is
// actually named "Windshield_Cannon_Multiplier" (not "Windshield_Multiplier")
// while its "_Element" WRAPPER keeps the plain "Windshield_Multiplier_Element"
// name (no "_Cannon") - a genuine irregularity in the shipped data itself,
// not a parsing choice. ParseFraudMultiplierCategory (tables_diversions.cpp)
// therefore takes each category's element/minimum-field names EXPLICITLY
// per call rather than deriving them from a single formula; every name is
// either CONFIRMED against the real row (Vehicle, Airtime, Witness,
// Cop_Witness, Linear_Dist, Civil_Vehicle, Crazy_Driver, Windshield,
// Surfing - all 9 of these appear, flat and/or tiered, in the real base row)
// or, for the 2 categories whose tiered form never appears in the real base
// row at all (Pinball, Cliff_Diver), the "_Multiplier_Element" WRAPPER name
// is an unconfirmed best-guess by pattern (their FLAT form's own name and
// "Minimum_" field ARE confirmed real-data facts either way).
//
// A SECOND, larger correction from the same real-data check: spec 10's own
// blanket claim that the real shipped row "populates side-by-side" BOTH
// forms "for every one of the twelve fraud-combo categories simultaneously"
// does NOT hold - only 5 of the 11 modelled categories (Vehicle, Airtime,
// Linear_Dist, Crazy_Driver, Windshield) have BOTH a flat AND a tiered form
// present in the real base row; Surfing has ONLY the tiered form (no flat);
// Witness, Cop_Witness, Civil_Vehicle, Pinball and Cliff_Diver have ONLY the
// flat form. See the task report for this finding in full.
struct FraudMultiplierTier {
    Always<float> minimum;          // Minimum_<X>
    Always<float> multiplierMin;    // Multiplier_Min
    Always<float> multiplierMax;    // Multiplier_Max
    Always<float> perMultiplier;    // Per_Multiplier
};
struct FraudMultiplierCategory {
    // Old flat scheme: a direct "<X>_Multiplier" child holding ONE
    // Multiplier_Data (no grid).
    bool flatPresent = false;
    FraudMultiplierTier flat;  // <X>_Multiplier/Multiplier_Data (flat form)
    // New tiered scheme: "<X>_Multiplier_Element" holding Cash_Instead plus
    // a "<X>_Multiplier" GRID of Multiplier_Data rows.
    bool tieredElementPresent = false;
    Always<bool> cashInstead;                    // <X>_Multiplier_Element/Cash_Instead
    std::vector<FraudMultiplierTier> tiers;      // <X>_Multiplier_Element/<X>_Multiplier/Multiplier_Data*
};
struct FraudBodyMultipliers {
    Always<float> head, chest, groin, arm, leg;  // Body_Multipliers/Body/{Head,Chest,Groin,Arm,Leg}
};
struct FraudCashBonusTier {
    Always<float> cash;   // Cash
    Always<float> bonus;  // Bonus
};
struct FraudGlobals {
    FraudMultiplierCategory vehicle;      // Vehicle_*
    FraudMultiplierCategory airtime;      // Airtime_*
    FraudMultiplierCategory witness;      // Witness_*
    FraudMultiplierCategory copWitness;   // Cop_Witness_*
    FraudMultiplierCategory linearDist;   // Linear_Dist_*
    FraudMultiplierCategory civilVehicle; // Civil_Vehicle_*
    FraudMultiplierCategory crazyDriver;  // Crazy_Driver_*
    FraudMultiplierCategory windshield;   // Windshield_*
    FraudMultiplierCategory surfing;      // Surfing_*
    FraudMultiplierCategory pinball;      // Pinball_*
    FraudMultiplierCategory cliffDiver;   // Cliff_Diver_*

    bool bodyMultipliersPresent = false;
    FraudBodyMultipliers bodyMultipliers;  // Body_Multipliers

    std::vector<std::string> luxuryCars;   // Luxury_Cars (Grid of vehicle-name References)

    std::vector<FraudCashBonusTier> adrenalineGrid;         // Adrenaline_Grid/Adrenaline_Element* -
                                                              // row element CONFIRMED against real
                                                              // shipped data (and the table's own
                                                              // TableDescription)
    std::vector<FraudCashBonusTier> adrenalineDuringGrid;   // Adrenaline_During_Grid/Adrenaline_Element*
                                                              // (same row element name, both grids)
    // spec 10: "a further ~15 flat Adrenaline_* scalars" - not individually
    // named by the spec text, so not modelled here (would require guessing
    // element names).

    // The dozen-or-so remaining flat scalars the spec DOES name exactly
    // (spec 10; Foley_*/Mass_* are given only as wildcards, not exact names,
    // and are likewise not modelled):
    Always<float> damageMultiplier;     // Damage_Multiplier
    Always<float> moneyConversion;      // Money_Conversion
    Always<float> minMoney;             // Min_Money
    std::optional<std::string> newsVanType;  // News_Van_Type (Reference into vehicles.xtbl)
    Always<float> witnessBonusRadius;   // Witness_Bonus_Radius
    Always<float> onFireBonus;          // On_Fire_Bonus
};
FraudGlobals ParseFraudGlobals(const Node* row);
std::optional<FraudGlobals> ParseFraudGlobalsTable(const Document& doc);

}  // namespace sr3tables_diversions

#include "sr3tables_diversions/horde_mode.h"
