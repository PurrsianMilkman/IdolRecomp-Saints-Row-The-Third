#pragma once

// sr3animruntime/node_graph.h - animation runtime unit U2: the node-graph file
// format (spec-anim-runtime.md Sec7, SPEC TEAM 2026-10-03). The spec is the
// ONLY source; every address or offset in a comment is a cross-reference to
// its text, never derived.
//
// WHAT IS HERE
//   * Sec7.3 the lexical rules shared by every parser (LexText / LexFloat /
//     LexInt / LexBool).
//   * Sec7.2 the loader 0x004cb510 and its four passes (LoadNodeGraphs):
//     names, headers (open / root choice / `validated` gate / CRC / pass-1
//     parse), links (pass 3 against a REOPENED file), aliases (pass 4).
//   * Sec7.4 graph headers and state nodes, the 0x30-byte clip block.
//   * Sec7.5 transitions, Sec7.6 the `start` expression language (character
//     fix-up, tokenizer 0x004ba590, tree builder 0x004ba7e0), Sec7.7 operands
//     and `conditions`, Sec7.8 blend-tree nodes, Sec7.9 graph references and
//     the alias / filter collector 0x004cb200, Sec7.10 control_filters.xtbl.
//   * The two name registries the parsers resolve through, built per their
//     own specs: the merged State/Action registry 0x004bf810
//     (spec-tables-animation.md Sec1.3 / Sec5.1) and the trigger table
//     0x03132b18 (spec-tables-animation.md Sec9, hash 0x00da7890 Sec1.3).
//
// CORRECTION APPLIED FROM THE START: `loop_flag`. Sec7.4's table says bit 0
// is set "unless the text is `true`"; Sec8.6 corrects that from the listing:
// bit 0 is set WHEN the text equals `true` (case-insensitive), and means LOOP.
// ClipBlock::loop follows Sec8.6.
//
// ENGINE CRASH / CORRUPTION PATHS (the project does not simulate crashes).
// Each of these is refused or guarded, never reproduced; none occurs in the
// shipped data (Sec7.12):
//   * a node-graph file name longer than 250 characters, or more than 383
//     rows (Sec7.2 pass 1 stack overrun)            -> NodeGraphDataError;
//   * a blend-tree `type` matching no kind (Sec7.8 code 0, call through an
//     unset slot)                                    -> NodeGraphDataError;
//   * an operand with no `source_name` (Sec7.7 null read) -> NodeGraphDataError;
//   * a control_filters row of unknown `type`, or whose `source_name` is
//     neither a parameter nor a filter (Sec7.10)     -> NodeGraphDataError;
//   * a `child_node_graph` cycle or out-of-range index during the alias walk
//     (Sec7.9: endless recursion / null dereference)  -> NodeGraphDataError;
//   * the expression builder's writes past a group object or through a null
//     current node (Sec7.6) -> skipped and COUNTED on the Expression
//     (hostGuard* counters, this project's own test-observable fields).
// A file that cannot be opened is NOT a crash: the loader returns at once,
// exactly as Sec7.2 says (NodeGraphLibrary::aborted).

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sr3animruntime/control_parameters.h"

namespace sr3xtbl {
class Document;
class Node;
}  // namespace sr3xtbl

namespace sr3animruntime {

// A data shape the engine would crash on (see the banner). Thrown, not
// simulated.
class NodeGraphDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Sec7.3 lexical rules
// ---------------------------------------------------------------------------
// 0x00dc5190: the child's text, or `dflt` when the element or its text is
// missing. Never trimmed.
std::string LexText(const sr3xtbl::Node* parent, std::string_view child, std::string_view dflt);
// 0x00dc5210: C-runtime string-to-double (0x00ea801f, HIGH) narrowed to
// float; `dflt` when missing. Reproduced with std::strtod.
float LexFloat(const sr3xtbl::Node* parent, std::string_view child, float dflt);
// 0x00dc5240: base-0 conversion (0x00ea7ea9, HIGH: a leading 0x / 0 selects
// hex / octal); `dflt` when missing. Reproduced with std::strtol(.., 0).
int32_t LexInt(const sr3xtbl::Node* parent, std::string_view child, int32_t dflt);
// 0x00dc51b0: `yes`, `true` or `1` (case-insensitive) -> true, any other text
// -> false; `dflt` when missing.
bool LexBool(const sr3xtbl::Node* parent, std::string_view child, bool dflt);
// The same text rules on a bare string.
float LexFloatText(const std::string& text);
int32_t LexIntText(const std::string& text);
bool LexBoolText(std::string_view text);

// ---------------------------------------------------------------------------
// Name registries the parsers resolve through
// ---------------------------------------------------------------------------
// The merged State/Action registry (spec-tables-animation.md Sec5.1): a
// compiled-in preset list first (indices 0..P-1), then every anim_states.xtbl
// `State/Name`, then every anim_actions.xtbl `Action/Name`. A file name equal
// to a preset (hash, then case-insensitive compare) reuses the preset index; a
// file name duplicating another FILE name is appended again.
// The preset CONTENTS are OPEN (spec-tables-animation.md Sec5.3: "OPEN: preset
// contents", 0x011507f8, 1,318 names). A caller that does not have them passes
// an empty list; indices of file names are then shifted by the missing preset
// count, and a name that exists only as a preset does not resolve.
class StateActionRegistry {
public:
    static StateActionRegistry build(const std::vector<std::string>& presets, const std::vector<std::string>& stateNames,
                                     const std::vector<std::string>& actionNames);
    // 0x004bf810: case-insensitive compare (_stricmp, spec-tables-animation.md
    // Sec1.3), first match, -1 when nothing matches. An EMPTY name is not
    // looked up and gives -1 (CHOSEN: an empty `tag_name` is "not set",
    // Sec7.4; what 0x004bf810 does with "" is OPEN in spec-tables-animation.md
    // Sec5.3).
    int32_t lookup(std::string_view name) const;
    const std::vector<std::string>& names() const { return names_; }
    int32_t presetCount() const { return presetCount_; }

private:
    std::vector<std::string> names_;
    int32_t presetCount_ = 0;
};

// The rotate/XOR name hash 0x00da7890 (spec-tables-animation.md Sec1.3):
// bytes read SIGN-EXTENDED, only A-Z folded to lower case, rotate-left 6 then
// XOR, from 0.
uint32_t TriggerNameHash(std::string_view name);

// The trigger table 0x03132b18 (spec-tables-animation.md Sec9): built from
// anim_triggers.xtbl `Name`s with 0x004ca2d0's guards in order - length >= 32
// rejected, count >= 110 rejected, a name already registered skipped (CHOSEN:
// "already registered" = equal hash; 0x004ca340 is untraced) - then the hash
// is stored.
class TriggerRegistry {
public:
    static constexpr int32_t kCapacity = 110;
    static TriggerRegistry build(const std::vector<std::string>& names);
    // Sec7.7 type 12: index of the first stored hash equal to the operand's
    // hash, or -1.
    int32_t lookup(std::string_view name) const;
    int32_t count() const { return static_cast<int32_t>(hashes_.size()); }

private:
    std::vector<uint32_t> hashes_;
};

// ---------------------------------------------------------------------------
// Sec7.10 control_filters.xtbl
// ---------------------------------------------------------------------------
enum class FilterKind : int32_t { None = 0, Multiplier = 1, DeltaMultiplier = 2, LinearMap = 3, Cap = 4 };

struct FilterDef {
    std::string name;                   // +0x04 (`none` when missing)
    FilterKind kind = FilterKind::None;  // +0x08
    std::string sourceName;
    ParamType type = ParamType::None;   // +0x0c: the source parameter's type, or the source filter's +0x0c
                                        //        AT THE TIME this row was resolved (0 if that filter comes later)
    bool sourceIsFilter = false;        // +0x10
    int32_t sourceId = -1;              // +0x12: parameter id or filter id
    // Kind fields; a missing float element gives 0.0 (the initialisers' 1.0
    // defaults are overwritten, Sec7.10).
    float basis = 0.0f, multiplier = 0.0f;                                // multiplier / delta multiplier
    float inputMin = 0.0f, inputMax = 0.0f, outputMin = 0.0f, outputMax = 0.0f;  // linear map / cap
};

class ControlFilterTable {
public:
    // 0x004caee0: pass 1 builds one definition per `control_filter` row of
    // `<root><control_filters>` (document order; type matched
    // case-insensitively); pass 2 (0x004d12d0) resolves every row's
    // `source_name` in row order, first as a registry parameter, else as a
    // filter. The table's `validated` element is never read.
    static ControlFilterTable load(const sr3xtbl::Document& doc, const ControlParameterRegistry& registry);
    // 0x004cbb70: filter id by name, case-insensitive, first match, -1.
    int32_t lookup(std::string_view name) const;
    const FilterDef* filter(int32_t id) const;
    const std::vector<FilterDef>& filters() const { return filters_; }

private:
    std::vector<FilterDef> filters_;
};

// ---------------------------------------------------------------------------
// Sec7.7 operands and conditions
// ---------------------------------------------------------------------------
enum class OperandType : int32_t {
    None = 0,
    Boolean = 1,
    Integer = 2,
    Float = 3,
    ControlParameter = 4,
    ControlFilter = 5,
    LocalControlParameter = 6,
    AnimationGroup = 7,
    ClipTime = 8,
    ClipTimeRemaining = 9,
    ClipTimeAsPercent = 10,
    TransitionLength = 11,
    Trigger = 12,
    NodeActiveTime = 13,
};
// The table at 0x01350394, matched case-insensitively; no match leaves 0.
inline constexpr const char* kOperandTypeNames[14] = {
    "none", "boolean", "integer", "float", "control parameter", "control filter", "local control parameter",
    "animation group", "clip time", "clip time remaining", "clip time as percent", "transition length", "trigger",
    "node active time",
};

// The 0x18-byte operand record.
struct Operand {
    OperandType type = OperandType::None;  // +0x00
    std::string sourceName;                // +0x04 (as written in the file)
    int32_t id = -1;                       // +0x08 parameter / filter / State-Action id (-1 = none)
    int32_t triggerIndex = -1;             // +0x0c
    uint32_t literal = 0;                  // +0x10 raw dword (bool byte / int32 / float bits)
    bool ownClip = false;                  // +0x14 bit 0: `?` - the node's own clip
};

// Comparison codes: the expression operators and the `comparison` words share
// one numbering (Sec7.6 / Sec7.7). 0 = none.
enum class Comparison : int32_t { None = 0, Equal = 1, NotEqual = 2, Greater = 3, GreaterOrEqual = 4, Less = 5, LessOrEqual = 6 };
inline constexpr const char* kComparisonWords[7] = {
    "", "equal", "not equal", "greater", "greater or equal", "less", "less or equal",
};

struct Condition {  // 0x38 bytes
    std::optional<ParamRecord> local;  // +0x00 private `control_parameter` (U0 row reader), absent = null
    Comparison comparison = Comparison::None;  // +0x34
    Operand parameter1;                // +0x04
    Operand parameter2;                // +0x1c
};

// ---------------------------------------------------------------------------
// Sec7.6 expressions
// ---------------------------------------------------------------------------
enum class TokenCode : int32_t {
    Open = 0, Close = 1, Eq = 2, Ne = 3, Gt = 4, Lt = 5, Ge = 6, Le = 7, And = 8, Or = 9, Xor = 10, Name = 11, Number = 12,
};
struct ExprToken {
    TokenCode code;
    std::string text;  // as consumed; multi-word names joined by single spaces
};
// 0x004ba590 applied repeatedly to the converted text.
std::vector<ExprToken> TokenizeExpression(std::string_view convertedText);
// Sec7.6 character fix-up: every `[` -> `<`, every `]` -> `>`.
std::string ConvertExpressionText(std::string_view raw);

struct ExprNode {
    bool group = false;      // +0x04: 1 group (condition_group), 0 comparison (condition)
    int32_t connective = -1;  // +0x08: to the next sibling: -1 none, 0 and, 1 or
    int32_t parent = -1, prev = -1, next = -1;  // +0x0c / +0x10 / +0x14
    int32_t firstChild = -1;  // +0x18 (groups)
    int32_t operand1 = -1;    // +0x1c (comparisons)
    Comparison comparison = Comparison::None;  // +0x20 (comparisons)
    int32_t operand2 = -1;    // +0x24 (comparisons)
};

struct Expression {
    std::string text;                // converted `start` text; empty = no expression
    std::vector<Operand> parameters;  // the operand table (only read when `start` is non-empty)
    std::vector<ExprToken> tokens;
    std::vector<ExprNode> nodes;      // node storage; links are indices into this vector
    int32_t root = -1;
    std::vector<std::string> unmatchedOperandTokens;  // operand tokens no `parameter` matched (dropped, index -1)
    // Host-safety counters for the engine's corruption paths (Sec7.6; none in
    // shipped data). The write is skipped and counted.
    int32_t hostGuardComparisonOnGroup = 0;   // a comparison operator while the current node is a group (+0x20 of a 0x1c-byte object)
    int32_t hostGuardNoCurrentNode = 0;       // a comparison operator or connective with no current node (null write)
    bool present() const { return !text.empty(); }
};
// 0x004ba7e0 over `tokens`, operands matched against `parameters` by
// `source_name` (0x004bac80: case-insensitive, first match).
void BuildExpressionTree(Expression& expr);

// ---------------------------------------------------------------------------
// Sec7.4 / Sec7.5 / Sec7.8 definitions
// ---------------------------------------------------------------------------
// The 0x30-byte clip block (0x004b8930).
struct ClipBlock {
    std::string tagName;     // +0x00 (empty = not set)
    int32_t tagIndex = -1;   // +0x04 State/Action registry index (0x004bf810), -1 if not set / not found
    std::string clipName;    // +0x08 (empty = not set)
    // +0x0c: initialised to -1 and written by no parser; Sec8.6 fills it
    // LAZILY at run time with the clip_name's record. It is per-DEFINITION
    // state shared by every instance of the node - hence `mutable`.
    mutable int32_t clipNameRecord = -1;
    std::string aliasName;   // +0x10
    uint32_t aliasCrc = 0;   // +0x14
    int32_t aliasId = -1;    // +0x18: 7,589 + k (NamedClipNodeId), -1
    std::string matchName;   // +0x1c
    // +0x20: CRC of match_name; nullopt = the engine's "no name" hash
    // [0x029c9964] (its numeric value is not specced; only "is it the no-name
    // value" is ever asked, Sec8.6).
    std::optional<uint32_t> matchCrc;
    float startTime = 0.0f;  // +0x24 (no effect, Sec8.6)
    int32_t layer = 4;       // +0x28
    bool loop = false;       // +0x2c bit 0 = LOOP (Sec8.6 correction of Sec7.4)
    // "names a clip" in the Sec8.6 sense uses tagIndex / clipNameRecord.
};

struct Transition {  // 0x38 bytes
    std::string name = "none";        // +0x04
    std::string toNodeName = "none";
    int32_t toNode = -1;              // +0x08 (pass 3)
    float length = 0.0f;              // +0x0c
    float cancelableAfter = -1.0f;    // +0x10 (-1.0 when absent)
    float cancelableBefore = -1.0f;   // +0x14
    float cyclicPercent = -1.0f;      // +0x18
    Expression expression;            // +0x1c..+0x2b
    std::vector<Condition> conditions;  // +0x2c / +0x30
    bool orForm = false;              // +0x34 `or`
    bool syncControllerTransform = false;  // +0x35: element PRESENCE
};

enum class NodeKind : int32_t {
    State = 0,  // a state_machine `node` (state_node_info)
    Merge = 1,
    Switch = 2,
    Clip = 3,
    MultiWayBlender = 4,
    BlendTreeReference = 5,
    StateMachineReference = 6,
};
// The table at 0x013506e0 (codes 1..6; anything else is code 0).
inline constexpr const char* kBlendNodeTypeNames[7] = {
    "", "merge", "switch", "clip", "multi way blender", "blend tree reference", "state machine reference",
};

struct BlendInput {
    std::string name = "none";
    int32_t node = -1;  // pass 3 (0x004b8eb0)
    // multi way blender only (the 0x30-byte record, 0x004bbf40):
    bool filled = false;          // false = abandoned by a 6th control point (record left unfilled)
    float maxBlendRate = 0.0f;    // +0x00
    int32_t pointCount = 0;       // +0x04
    float range[5] = {0, 0, 0, 0, 0};  // +0x08
    float value[5] = {0, 0, 0, 0, 0};  // +0x1c
};

struct NodeDef {
    NodeKind kind = NodeKind::State;
    std::string name = "none";   // +0x0c
    // child_node_graph (state nodes and the two reference kinds). Present
    // only when the element exists.
    std::optional<std::string> childGraphName;
    int32_t childGraph = -1;     // +0x10 (pass 3; -1 when absent or not found)
    // Clip block: state nodes and `clip` nodes.
    std::optional<ClipBlock> clip;
    // State nodes.
    std::vector<Transition> transitions;  // +0x44 / +0x48
    // Blend-tree nodes (common header, 0x004bb7b0).
    uint16_t constant18 = 0, constant1a = 0;  // +0x18 / +0x1a (meaning OPEN, Sec5.1 item 27)
    std::vector<BlendInput> inputs;           // +0x1c count (every `input` of `inputs`) / +0x20
    // +0x24 the switch selector. Stored in the DEFINITION, not the instance,
    // so all instances of the graph share it (Sec8.7); -1 = all inputs.
    mutable int32_t selector = -1;
    std::optional<Operand> parameter;  // switch / multi way blender (+0x28)
    bool inputsTruncated = false;      // a 6th control point abandoned an input and every later one (Sec7.8)
};

enum class GraphKind : int32_t { StateMachine = 1, BlendTree = 2 };

struct GraphDef {  // the 0x30-byte graph object
    GraphKind kind = GraphKind::StateMachine;  // +0x08
    std::string fileName;          // `<row name>.xtbl` (port bookkeeping)
    uint32_t fileNameCrc = 0;      // +0x0c CRC-32 (0x00d9e8b0) of the file name incl. `.xtbl`
    std::string name = "none";     // +0x10
    std::vector<NodeDef> nodes;    // +0x14 / +0x18, document order
    GraphAliasTable aliases;       // +0x1c / +0x20 (pass 4); present = false until pass 4 runs
    std::vector<int16_t> filterIds;  // +0x24 / +0x28 (pass 4)
    // +0x2c: state machine default_node / blend tree output_node; -1.
    int32_t entryNode = -1;
    std::string entryNodeName = "none";
    bool linked = false;           // pass 3 ran for this graph
};

struct NodeGraphLibrary {
    std::vector<std::string> rowNames;  // pass 1, in row order
    std::vector<GraphDef> graphs;        // accepted graphs; position = graph index
    std::vector<int32_t> graphRow;       // the row of each accepted graph (the 384-dword array)
    std::vector<int32_t> skippedNotValidated;  // rows skipped by the `validated` gate (silently, no index)
    // Sec7.2: a file that cannot be opened makes the loader return at once.
    bool aborted = false;
    int32_t abortedPass = 0;             // 2 or 3
    std::string abortedFile;

    const GraphDef* graph(int32_t index) const;
    // 0x004cbc30: graph index by `name`, case-insensitive, first match among
    // accepted graphs; -1 when nothing matches, 0 when no graphs are loaded.
    int32_t findGraph(std::string_view name) const;
};

// 0x004b8eb0: node index by name within a graph, case-insensitive, first
// match; -1 when no node has that name, 0 when the graph has no nodes.
int32_t FindNodeIndex(const GraphDef& graph, std::string_view name);

// 0x004cbcb0 (U1 Sec6.4): the clip block of a state node or a `clip` node,
// else nullptr.
const ClipBlock* ClipBlockOf(const NodeDef& node);

struct NodeGraphLoadInputs {
    const ControlParameterRegistry* registry = nullptr;     // required
    const ControlFilterTable* filters = nullptr;            // required
    const StateActionRegistry* stateActions = nullptr;      // required
    const TriggerRegistry* triggers = nullptr;              // required
    // 0x00dc5ac0 on `<name>.xtbl`: the file's bytes, or nullopt when it
    // cannot be opened. Called once per row in pass 2 and once per accepted
    // graph in pass 3 (the loader REOPENS each file, Sec7.2).
    std::function<std::optional<std::string>(const std::string& fileName)> openFile;
};

// 0x004cb510 over `node_graph_files.xtbl` (rows: `<root><node_graph_files>
// <node_graph_file><name>`).
NodeGraphLibrary LoadNodeGraphs(const sr3xtbl::Document& nodeGraphFiles, const NodeGraphLoadInputs& in);

}  // namespace sr3animruntime
