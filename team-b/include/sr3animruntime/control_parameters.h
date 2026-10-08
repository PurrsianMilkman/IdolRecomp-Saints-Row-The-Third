#pragma once

// sr3animruntime/control_parameters.h - animation runtime unit U0: the
// control-parameter contract (spec-anim-runtime.md Sec1-Sec5, SPEC TEAM
// 2026-10-03; the ONLY source for every rule below - addresses and offsets
// are cited from that text as cross-references, never derived).
//
// What is reproduced:
//   * the global registry built from control_parameters.xtbl (Sec1.1-Sec1.3):
//     one 0x14-byte record per <control_parameter> row in document order, id
//     = 0-based row index, typed default/min/max per the row reader's rules;
//   * the global name -> id resolver 0x004cbaf0 and the id -> record accessor
//     0x004cbb40 (Sec2.1);
//   * the graph-scoped alias table of a node-graph definition and its three
//     lookups 0x004b8d90 / 0x004b8e90 / 0x004b8e10 (Sec2.2);
//   * the 8-byte parameter slot used by the per-controller block (Sec3.1) and
//     by a graph instance's alias array (Sec3.5).
// The per-controller block, the three typed setters and the child ring live
// on sr3animruntime::Controller (controller.h), because they are controller
// state (+0x28, +0x08).
//
// Not modelled: the network forward of the graph-scoped wrappers (0x008cafc0,
// Sec4.3; network scope), the object_rig replication pair (Sec4.3), and every
// writer in the Sec4 census - wiring writers is a later pass.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sr3xtbl {
class Document;
class Node;
}

namespace sr3animruntime {

// Sec1.3: the static type table 0x013503d4, compared case-insensitively in
// this order; the index of the first match is the type code.
enum class ParamType : int32_t { None = 0, Bool = 1, Integer = 2, Float = 3 };
inline constexpr std::array<std::string_view, 4> kParamTypeNames = {"none", "bool", "integer", "float"};

// One 0x14-byte registry record (Sec1.2). The three value dwords are kept as
// raw 32-bit words because that is what the engine copies into a graph
// instance (Sec3.5: "copies the registry dword +0x08"); typed views below.
struct ParamRecord {
    std::string name;                  // +0x00 private copy of `name`; "none" when the element is missing
    ParamType type = ParamType::None;  // +0x04
    // +0x08 / +0x0c / +0x10. Float rows: IEEE bits of the float. Integer rows:
    // the int32. Bool rows: the LOW BYTE is 0/1 (min byte 0, max byte 1); the
    // engine writes only that byte and the other three bytes are "whatever the
    // allocator returned" (Sec1.2). This port stores 0 in them - a stand-in,
    // not engine behaviour: per Sec3.5 a reader of a bool must test the low
    // byte only, and every typed accessor below does exactly that.
    uint32_t defaultDword = 0;
    uint32_t minDword = 0;
    uint32_t maxDword = 0;
    // False for type 0: the row reader writes nothing at +0x08..+0x10 and the
    // loader zeroes only +0x00/+0x04 (Sec1.1/Sec1.2), so the engine's values
    // there are indeterminate. The typed accessors throw OpenStateError for
    // such a record instead of answering.
    bool valuesWritten = false;

    // Typed views of the three dwords. Each throws std::logic_error when the
    // record is not of that type (asking a float row for its bool default is
    // a caller bug, not engine state) and OpenStateError for type 0.
    bool defaultBool() const;
    int32_t defaultInt() const;
    float defaultFloat() const;
    bool minBool() const;
    bool maxBool() const;
    int32_t minInt() const;
    int32_t maxInt() const;
    float minFloat() const;
    float maxFloat() const;
    // min_value/max_value are STORED but no code read for U0/U1 clamps a write
    // against them (Sec1.4, Sec3.2; Sec5.1 item 3 leaves the evaluator's use
    // OPEN). Nothing in this library clamps.
};

class ControlParameterRegistry {
public:
    // An unloaded registry: the state left when manifest slot 12 returns
    // nothing (Sec1.1): count 0, every lookup -1, every write dropped.
    ControlParameterRegistry() = default;

    // 0x004cae20 + per-row reader 0x004d00e0 (Sec1.1-Sec1.3). Rows are the
    // `control_parameter` children of the `control_parameters` child of the
    // document's root, in document order.
    //   name:         text, else "none".
    //   data_type:    text (else "none") compared case-insensitively with
    //                 kParamTypeNames in order; first match is the code; no
    //                 match leaves 0.
    //   float (3):    default_value/min_value/max_value through 0x00dc5210
    //                 (C runtime atof - spec-tables-environment.md Sec1.4),
    //                 0.0 when absent. Reproduced with std::strtod, which is
    //                 what atof is defined as, narrowed to float.
    //   integer (2):  the same three through 0x00dc5240, 0 when absent.
    //                 Grammar from Sec7.3 (HIGH): the C runtime's base-0
    //                 conversion 0x00ea7ea9 (a leading 0x / 0 selects hex /
    //                 octal), reproduced with std::strtol(.., 0). The shipped
    //                 integer rows hold plain non-negative decimals (0..10000,
    //                 Sec1.4), on which every plausible grammar agrees.
    //   bool (1):     default_value (absent -> "none") compared
    //                 case-insensitively with "false": equal -> 0, ANYTHING
    //                 else (true, other text, missing) -> 1. min byte 0, max
    //                 byte 1; min_value/max_value are not read.
    //   type 0:       nothing at +0x08..+0x10 (valuesWritten = false).
    // A missing `control_parameters` element gives an empty registry
    // (CHOSEN: Sec1.1 does not say what 0x004cae20 does then; count 0 is the
    // same observable state as the "slot 12 returned nothing" case).
    // Throws std::length_error for more than 32,767 rows: the count is a
    // SIGNED 16-bit value (0x03171bc8, Sec1.1) and would wrap (CHOSEN refusal;
    // the shipped file has 87).
    static ControlParameterRegistry load(const sr3xtbl::Document& doc);
    // Convenience: parse the bytes with sr3xtbl, then load().
    static ControlParameterRegistry loadXml(std::string_view xmlBytes);

    bool loaded() const { return loaded_; }
    int16_t count() const { return static_cast<int16_t>(records_.size()); }
    const std::vector<ParamRecord>& records() const { return records_; }

    // 0x004cbaf0 (Sec2.1): ids 0..count-1, case-insensitive compare (the CRT
    // routine; ASCII folding), FIRST match wins, -1 when nothing matches. The
    // engine's second argument is never read and is not modelled. Linear, no
    // hashing - callers may cache, the registry never changes after load.
    int16_t resolve(std::string_view name) const;
    // 0x004cbb40 (Sec2.1): the record for an id in [0, count), else null
    // (including -1).
    const ParamRecord* record(int32_t id) const;

private:
    std::vector<ParamRecord> records_;
    bool loaded_ = false;
};

// U0's row reader 0x004d00e0 on one `control_parameter` element, exactly as
// load() applies it per row. Also reached from a node-graph `condition`'s
// private `control_parameter` (Sec7.7).
ParamRecord ReadControlParameterRow(const sr3xtbl::Node* row);

// The alias table of one node-graph definition (Sec2.2): definition +0x1c is
// a 16-bit alias count and +0x20 an array of 16-bit GLOBAL parameter ids.
// The definition itself is U2's (node-graph file format); this is only the
// seam U0 needs.
struct GraphAliasTable {
    bool present = false;            // +0x20 non-null
    std::vector<int16_t> globalIds;  // +0x20[0 .. +0x1c)

    // 0x004b8d90: the LOCAL alias index whose registry name matches
    // case-insensitively (first match), or -1; -1 at once when !present.
    // CHOSEN deviation (the spec's own advice, Sec2.2 "Hazard ... A port should
    // skip such entries"): an alias holding -1, a negative id or an id >=
    // count maps to a null record and crashes the engine loop; it is skipped.
    int16_t resolveLocal(std::string_view name, const ControlParameterRegistry& registry) const;
    // 0x004b8e90: local alias index -> global id, or -1 when the index is not
    // below the alias count. CHOSEN: a negative index also gives -1.
    int16_t aliasToGlobal(int32_t alias) const;
    // 0x004b8e10: global id -> local alias index, linear, or -1.
    int16_t globalToAlias(int16_t id) const;
};

// The graph-scoped name -> global id path used by the by-name writers
// (Sec4.3 step 2): resolve in the graph (0x004b8d90), then alias -> global
// (0x004b8e90). -1 when the graph does not use the name. A name is therefore
// writable this way only if the target object's own graph aliases it.
int16_t ResolveGraphScoped(const GraphAliasTable& aliases, std::string_view name,
                           const ControlParameterRegistry& registry);

// One 8-byte slot (Sec3.1 per-controller block, indexed by global id; Sec3.5
// graph-instance array, indexed by local alias). dword +0 holds the value
// (bool -> low byte, integer -> int32, float -> float bits).
struct ParamSlot {
    uint32_t dword0 = 0;
    // Block: bytes +4..+7, never written by the setters (Sec3.1), so always
    // the zero of the allocation fill. Instance: byte +4 is the deferred
    // toggle request (Sec6.10 item 2), cleared at instancing.
    uint32_t dword1 = 0;
};

// The 62 distinct compiled-in parameter names (Sec4.1 "Totals"), each with
// the setter type its writer uses (Sec4.1 table / Sec4.2 rows / Sec4.3
// wrappers). Sec4.1 states all 62 match a shipped row exactly, including
// case, and that every setter site agrees with the row's registry type.
//   * 59 names are spelled out in Sec4.1-Sec4.3.
//   * The rider writer's "three `... Abs` forms" (Sec4.1) are not spelled
//     out. They are pinned here by two clean facts, both re-checked against
//     the shipped file by tests/real_anim_control_parameters_test.cpp:
//     Sec4.6 names the 25 shipped rows WITHOUT a compiled-in writer, so the
//     62 compiled-in names are exactly the other 62 shipped rows; and of
//     those, the only rows of the form "<one of the six listed rider bases>
//     Abs" are the three below.
struct CompiledInParameterName {
    const char* name;
    ParamType setterType;
    const char* writer;  // the Sec4 writer, for cross-reference
};
inline constexpr CompiledInParameterName kCompiledInParameterNames[62] = {
    // 0x00958690, the human writer (Sec4.2), 32 names in execution order.
    {"Jump Dive Bool", ParamType::Bool, "0x00958690"},
    {"Offhand Grenade Bool", ParamType::Bool, "0x00958690"},
    {"dead", ParamType::Bool, "0x00958690"},
    {"combat ready", ParamType::Bool, "0x00958690"},
    {"Supported By Ground", ParamType::Bool, "0x00958690"},
    {"desired speed", ParamType::Float, "0x00958690"},
    {"Vertical Speed", ParamType::Float, "0x00958690"},
    {"move forward bool", ParamType::Bool, "0x00958690"},
    {"move left bool", ParamType::Bool, "0x00958690"},
    {"move right bool", ParamType::Bool, "0x00958690"},
    {"move back bool", ParamType::Bool, "0x00958690"},
    {"backpedal bool", ParamType::Bool, "0x00958690"},
    {"alternate strafe bool", ParamType::Bool, "0x00958690"},
    {"walk bool", ParamType::Bool, "0x00958690"},
    {"run bool", ParamType::Bool, "0x00958690"},
    {"sprint bool", ParamType::Bool, "0x00958690"},
    {"crouch bool", ParamType::Bool, "0x00958690"},
    {"aim bool", ParamType::Bool, "0x00958690"},
    {"fine aim bool", ParamType::Bool, "0x00958690"},
    {"current speed", ParamType::Float, "0x00958690"},
    {"direction", ParamType::Float, "0x00958690"},
    {"desired direction", ParamType::Float, "0x00958690"},
    {"Free Fall Vert Control", ParamType::Float, "0x00958690"},
    {"Parachute Open Bool", ParamType::Bool, "0x00958690"},
    {"Speed Along rvec", ParamType::Float, "0x00958690"},
    {"turn angle", ParamType::Float, "0x00958690"},
    {"Aim Pitch", ParamType::Float, "0x00958690"},
    {"Aim Heading", ParamType::Float, "0x00958690"},
    {"fire bool", ParamType::Bool, "0x00958690"},
    {"Weapon Sprint Bool", ParamType::Bool, "0x00958690"},
    {"vehicle surf lean", ParamType::Float, "0x00958690"},
    {"Reloading Bool", ParamType::Bool, "0x00958690"},
    // 0x009896d0, climb (Sec4.1).
    {"Climb Height", ParamType::Float, "0x009896d0"},
    {"Climb Dist", ParamType::Float, "0x009896d0"},
    {"Climb Platform Bool", ParamType::Bool, "0x009896d0"},
    {"Climb Duration ms", ParamType::Integer, "0x009896d0"},
    // 0x009895b0 / 0x0098bea0 / 0x0098c840, jump (Sec4.1).
    {"Jump Start Land Bool", ParamType::Bool, "0x009895b0"},
    {"Jump Left Ground Bool", ParamType::Bool, "0x0098c840"},
    // 0x00af4b80, rider (Sec4.1).
    {"Vehicle Steering", ParamType::Float, "0x00af4b80"},
    {"Vehicle Steering Time", ParamType::Float, "0x00af4b80"},
    {"Vehicle MPH", ParamType::Float, "0x00af4b80"},
    {"Vehicle Forward Lean", ParamType::Float, "0x00af4b80"},
    {"Vehicle Vert Lean", ParamType::Float, "0x00af4b80"},
    {"Vehicle Tilt", ParamType::Float, "0x00af4b80"},
    {"Vehicle Tilt Abs", ParamType::Float, "0x00af4b80"},          // "... Abs" form, pinned via Sec4.6 (see above)
    {"Vehicle Forward Lean Abs", ParamType::Float, "0x00af4b80"},  // "
    {"Vehicle Vert Lean Abs", ParamType::Float, "0x00af4b80"},     // "
    {"Wheelie Bool", ParamType::Bool, "0x00af4b80"},
    {"Endo Bool", ParamType::Bool, "0x00af4b80"},
    // 0x00a760e0, vehicle (Sec4.1; `Vehicle MPH` repeats the rider's).
    {"Helicopter Steer Vertical", ParamType::Float, "0x00a760e0"},
    {"Helicopter Steer Horizontal", ParamType::Float, "0x00a760e0"},
    {"Airplane Mode Bool", ParamType::Bool, "0x00a760e0"},
    {"Vehicle Landed Bool", ParamType::Bool, "0x00a760e0"},
    {"Vehicle On", ParamType::Bool, "0x00a760e0"},
    // 0x00b09ab0, collision (Sec4.1).
    {"Collision Bool", ParamType::Bool, "0x00b09ab0"},
    {"Collision Dir", ParamType::Float, "0x00b09ab0"},
    // 0x00a3c910, animation_sequence_advance (Sec4.1).
    {"Ball Rider Stage Number", ParamType::Integer, "0x00a3c910"},
    // The five literals fixed at the graph-scoped callers (Sec4.3), all
    // through the bool wrapper 0x0064a1f0.
    {"spike closed bool", ParamType::Bool, "0x0064a1f0"},
    {"target down bool", ParamType::Bool, "0x0064a1f0"},
    {"is door open", ParamType::Bool, "0x0064a1f0"},
    {"is door open back", ParamType::Bool, "0x0064a1f0"},
    {"Light", ParamType::Bool, "0x0064a1f0"},
};

// Sec4.6: the 25 shipped rows with no compiled-in writer (10 referenced by a
// graph, 15 neither written nor referenced).
inline constexpr const char* kParametersWithoutCompiledWriter[25] = {
    "locomotion", "cower type", "idle type", "cower bool", "speed", "Left Foot Bool", "Weapon Equipped",
    "Sprint Holster Bool", "Coop Rider Stage Number", "is statue broken",
    "turnto", "jump bool", "land bool", "fall bool", "supported bool", "idle bool", "panic bool",
    "land hard bool", "land slam bool", "getup bool", "l deflection", "stand bool", "Weapon Group",
    "Right Foot Bool", "Left Stick Horizontal",
};

// Bit-exact float <-> dword helpers (the engine stores raw 32-bit floats).
uint32_t FloatBits(float f);
float BitsFloat(uint32_t bits);

}  // namespace sr3animruntime
