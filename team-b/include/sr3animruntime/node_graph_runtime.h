#pragma once

// sr3animruntime/node_graph_runtime.h - animation runtime unit U3: the
// node-graph runtime (spec-anim-runtime.md Sec8, SPEC TEAM 2026-10-03). The
// spec is the ONLY source; addresses and offsets in comments are
// cross-references to its text.
//
// WHAT IS REAL HERE
//   * Sec8.1 the evaluation entry 0x004cc6c0: pass 0 (clear row +0x48 bit
//     0x04, every filter in root filter-list order, the node-instance garbage
//     collector 0x004cca80, the graph's evaluate slot) and pass 1 (the slot,
//     the alias toggle requests, counter++ and dt = 0). Nothing while row
//     +0x30 is 0.
//   * Sec8.2 node instances keyed (graph, node, transition), first-free reuse,
//     per-evaluation advance, the 32-entry output list, every reset slot.
//   * Sec8.3 the state machine: default state, state step, completion loop
//     (zero-length chains in one frame), in-flight targets stepped from the
//     moment their transition starts, root-motion sync, and pass 1's nested
//     linear cross-fades with the bit-1 / bit-2 entry marks and phase alignment.
//   * Sec8.4 the transition test with its codes 0-4, the cancel window
//     (including the wrap when cancelable_before < cancelable_after), the
//     freeze bit, and the state step's first-in-document-order rule.
//   * Sec8.5 the expression fold (no precedence), conditions with
//     short-circuit, every operand type's value, the comparison type from
//     operand 1, the comparator (bool ==/!= on the low byte only, signed
//     integers, floats compared as doubles), the clip-time family.
//   * Sec8.6 clip blocks: tag mapping first, then the lazily cached
//     per-DEFINITION clip_name record; nothing at all with neither; clip time
//     clamped to the duration; match_name -> row +0x48 bit 0x04.
//   * Sec8.7 merge (union), switch (selector in the DEFINITION, shared by all
//     instances), multi way blender (first containing segment in DATA order,
//     0 when none, max_blend_rate as a duration, snap when fresh or 0),
//     references.
//   * Sec8.8 the four filter kinds, a source filter evaluated by its consumer
//     first - so a stateful delta multiplier that is also a listed source runs
//     TWICE per evaluation (Turn Angle Delta Multi: 0.75 per frame, not 0.5).
//   * U1 Sec6.4's named-clip-node search (NodeGraphEvaluator).
//
// HOST-SAFETY GUARDS (not engine behaviour; the engine would crash or corrupt
// memory; none reachable with shipped data):
//   * the output list's 33rd entry (Sec8.2: the engine overwrites the return
//     address) is DROPPED and counted: GraphOutputList::overflowGuardCount();
//   * a 9th active transition, a 6th multi-way input weight, an unfilled
//     (abandoned) multi-way input, a -1 node / graph / operand index, a
//     parameter not in the root alias table, a filter not in the root filter
//     list, an empty expression group, more than 250 clip-time keys, and a
//     completion loop that has not settled after kCompletionLoopGuard
//     iterations -> sr3luahost::OpenStateError naming the engine path.
//
// BOUNDARIES (GraphRuntimeServices; defaults THROW OpenStateError):
//   * 0x004ccba0 tag index -> record through the row's anim-group tables
//     (0x004ccf30 is not specified);
//   * 0x004bf650 clip_name -> loaded-animation record (the anim_files
//     hashtable belongs to the animation-table loader);
//   * 0x004bd8c0 a record's clip duration;
//   * 0x004bddb0 "did this trigger fire on any layer" (U1 Sec6.6);
//   * 0x004bcdb0 float +0x60 of the controller's animation instance (phase);
//   * 0x004bcf00 stop a clip layer (reset paths).
// ControllerGraphServices implements the duration (AnimDataSource) and the
// trigger test (LayerApi) from a Controller's environment; the rest stay OPEN.
//
// THE POST STEP WRITES INTO THE NODE INSTANCES. The clip-time operands read
// the clip index at instance +0x38 (their duration, Sec5.1 item 29) and a
// named one needs the layer handle at +0x3c (Sec8.5); both are written by
// U1 Sec6.10 step 5 when it creates or refreshes a clip's layer. Until a
// postOutput() implementation writes them, a `clip time remaining` operand
// (and a percent with a non-zero clip time) refuses with OpenStateError, and a
// named clip-time operand finds no match (0.0).
//
// OPEN (NodeGraphEvaluator throws): U1 Sec6.10 step 5's post step (turning
// the output list into layers: 0x004cca40, 0x004bce30 / 0x004bce90,
// 0x004bdb10, 0x004bd9b0, 0x004bda10 - U4) and 0x004cc000's clip-node
// collection (its body past the first 140 instructions is unread, Sec5.1 item
// 17). The 1-D blend-tree evaluator (U1 Sec6.9) is real in EvaluationBoundary's
// defaults (anim_controller.cpp), which NodeGraphEvaluator inherits.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3animruntime/controller.h"
#include "sr3animruntime/node_graph.h"

namespace sr3animruntime {

// ---------------------------------------------------------------------------
// Sec8.5 expression fold
// ---------------------------------------------------------------------------
// Each node's value is combined with the running value through the PREVIOUS
// node's connective (and / or; -1 = replace); a group passes its incoming
// running value and connective to its first child and combines the child
// chain's result with them again. The root starts with running value false and
// connective -1. `comparisonValue(i)` gives comparison node i's own value.
// Throws OpenStateError for an empty group (the engine calls through a null
// child, Sec8.5) and std::invalid_argument for an expression with no tree.
bool FoldExpression(const Expression& expr, const std::function<bool(int32_t comparisonNode)>& comparisonValue);

// 0x004d0070: bool compares the low byte only and supports only ==/!= (any
// ordering comparison of bools is false); integer is signed; float is compared
// as doubles with exact ==. Type None (and comparison None) is always false.
bool CompareValues(ParamType type, Comparison cmp, uint32_t a, uint32_t b);

// Sec8.7 / U1 Sec6.9 curve rule: the first segment j (in DATA order) with
// range[j] <= x <= range[j+1]; value[j] for a zero-width segment; 0 when no
// segment contains x (and for a single point).
float MultiWayCurve(const BlendInput& input, float x);

// Sec8.8: one filter kind's output from its input x, its previous output and
// dt (only `delta multiplier` uses the last two).
float FilterMath(const FilterDef& filter, float x, float previousOutput, float dt);

// ---------------------------------------------------------------------------
// Sec8.2 the output list
// ---------------------------------------------------------------------------
inline constexpr uint32_t kOutLoop = 0x1;               // bit 0
inline constexpr uint32_t kOutZeroedInTransition = 0x2;  // bit 1: weight became 0 in a transition
inline constexpr uint32_t kOutOutgoing = 0x4;            // bit 2: outgoing side of a transition

struct OutputEntry {  // 0x24 bytes
    int32_t layer = 4;          // +0x00 the clip block's `layer`
    int16_t inputTag = 0;       // +0x04 (CHOSEN 0 at emission; overwritten by every enclosing tagger)
    int16_t graph = -1;         // +0x06
    int16_t node = -1;          // +0x08
    int32_t tagIndex = -1;      // +0x0c State/Action index of tag_name
    int32_t record = -1;        // +0x10
    float weight = 1.0f;        // +0x14
    float clipTime = 0.0f;      // +0x18 (non-looping clips only)
    float prevClipTime = 0.0f;  // +0x1c
    uint32_t flags = 0;         // +0x20
};

class GraphOutputList {
public:
    static constexpr size_t kCapacity = 32;  // 0x004b8850's capacity word
    // HOST-SAFETY GUARD: the engine appends with no test against the
    // capacity (Sec8.2). Here an append to a full list is DROPPED, the guard
    // counter is incremented and false is returned.
    bool append(const OutputEntry& e);
    size_t size() const { return entries_.size(); }
    OutputEntry& operator[](size_t i) { return entries_[i]; }
    const OutputEntry& operator[](size_t i) const { return entries_[i]; }
    const std::vector<OutputEntry>& entries() const { return entries_; }
    int32_t overflowGuardCount() const { return overflowGuardCount_; }
    size_t peak() const { return peak_; }
    void clear() { entries_.clear(); }

private:
    std::vector<OutputEntry> entries_;
    int32_t overflowGuardCount_ = 0;
    size_t peak_ = 0;
};

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------
class GraphRuntimeServices {
public:
    virtual ~GraphRuntimeServices() = default;
    virtual int32_t recordForTag(const GraphInstance& row, int32_t tagIndex);       // 0x004ccba0
    virtual int32_t recordForClipName(const std::string& clipName);                // 0x004bf650
    virtual float recordDuration(int32_t record);                                  // 0x004bd8c0 on the record's clip (Sec8.6 clamp)
    virtual float clipDuration(int32_t clipIndex);                                 // 0x004bd8c0 on a clip index (Sec8.5, instance +0x38)
    virtual bool triggerFiredAnyLayer(const GraphInstance& row, int32_t triggerIndex);  // 0x004bddb0
    virtual float animationInstancePhase(const GraphInstance& row);                // 0x004bcdb0
    virtual void stopLayer(const GraphInstance& row, int32_t layerHandle);         // 0x004bcf00
};

// The parts a Controller's environment can answer: recordDuration through
// AnimDataSource (record +0x08 -> clip header word +0x06 / 30, U1 Sec6.6), and
// the trigger test through LayerApi::triggerFiredAnyLayer(trigger, -1) (U1
// Sec6.6 0x004b4320 with id -1). Everything else stays OPEN.
class ControllerGraphServices : public GraphRuntimeServices {
public:
    ControllerGraphServices(const AnimDataSource* data, LayerApi* layers) : data_(data), layers_(layers) {}
    float recordDuration(int32_t record) override;
    float clipDuration(int32_t clipIndex) override;
    bool triggerFiredAnyLayer(const GraphInstance& row, int32_t triggerIndex) override;

private:
    const AnimDataSource* data_;
    LayerApi* layers_;
};

// ---------------------------------------------------------------------------
// The evaluator
// ---------------------------------------------------------------------------
class NodeGraphRuntime {
public:
    // Iterations of Sec8.3's completion loop after which the host refuses
    // (the engine would spin; Sec8.10 finds no zero-length cycle).
    static constexpr int32_t kCompletionLoopGuard = 64;

    NodeGraphRuntime(const NodeGraphLibrary& library, const ControlParameterRegistry& registry,
                     const ControlFilterTable& filters)
        : lib_(library), registry_(registry), filters_(filters) {}

    // 0x004cc6c0 on one row for pass 0 or 1.
    void evaluate(GraphInstance& row, int32_t pass, GraphOutputList& out, GraphRuntimeServices& services) const;

    // Pieces, exposed for tests and for the boundary.
    // Sec8.4 step 2: the transition's condition (the tree if `start` is
    // non-empty, else the conditions).
    bool transitionCondition(const GraphInstance& row, int32_t graph, int32_t node, int32_t transition,
                             GraphRuntimeServices& services) const;
    // The Sec8.5 comparison type of an operand (operand 1 decides).
    ParamType operandType(const Operand& op, const Condition* condition) const;

    const NodeGraphLibrary& library() const { return lib_; }

private:
    const NodeGraphLibrary& lib_;
    const ControlParameterRegistry& registry_;
    const ControlFilterTable& filters_;
};

// U1's EvaluationBoundary over the runtime.
class NodeGraphEvaluator : public EvaluationBoundary {
public:
    // `services` null: a ControllerGraphServices over the controller's
    // environment is used for each call.
    NodeGraphEvaluator(const NodeGraphRuntime& runtime, GraphRuntimeServices* services = nullptr)
        : runtime_(runtime), services_(services) {}

    // U1 Sec6.10 steps 4-5: a fresh 32-entry list (0x004b8850), 0x004cc6c0
    // with pass 0 then pass 1, then the post step. An EMPTY list posts nothing
    // and reports "no output" (Sec8.9 / Sec6.10: a non-member slot then ends
    // itself); a non-empty list goes to postOutput().
    GraphStepOutput evaluateGraphAndPost(Controller& controller, int32_t slotIndex, GraphInstance& row,
                                         ClipDedupList& clips) override;
    // U1 Sec6.4: the first node-instance index >= firstNode of `row` whose
    // words +0/+2 != -1 and +4 = -1 and whose clip block (state node or `clip`
    // node, 0x004cbcb0) carries alias id `id`; -1.
    int32_t findNamedClipNode(const GraphInstance& row, int32_t id, int32_t firstNode) override;
    // OPEN: the post step of 0x004b0b80 (U1 Sec6.10 step 5 / U4). Throws. An
    // implementation turns the entries into layers (weight x slot weight;
    // entries below 0.0001 dropped unless bit 1 is set, Sec8.9) and writes
    // each emitting clip instance's +0x38 clip index and +0x3c layer handle
    // (row.nodeInstances, keyed by the entry's graph / node).
    virtual GraphStepOutput postOutput(Controller& controller, int32_t slotIndex, GraphInstance& row,
                                       const GraphOutputList& out, ClipDedupList& clips);

    // The list of the most recent evaluateGraphAndPost (for inspection).
    const GraphOutputList& lastOutput() const { return last_; }

private:
    const NodeGraphRuntime& runtime_;
    GraphRuntimeServices* services_;
    GraphOutputList last_;
};

}  // namespace sr3animruntime
