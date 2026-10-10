// Animation runtime U3: the node-graph runtime. Source: spec-anim-runtime.md
// Sec8 only - see include/sr3animruntime/node_graph_runtime.h.
#include "sr3animruntime/node_graph_runtime.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

#include "sr3luahost/open_state.h"

namespace sr3animruntime {

namespace {

[[noreturn]] void open(const std::string& what, const std::string& spec) { throw OpenStateError(what, spec); }
[[noreturn]] void guard(const std::string& what) {
    throw OpenStateError("HOST-SAFETY GUARD: " + what, "spec-anim-runtime.md Sec8 (engine crash / corruption path; not simulated)");
}

bool combine(bool running, bool value, int32_t connective) {
    if (connective == 0) return running && value;
    if (connective == 1) return running || value;
    return value;  // -1: replace
}

bool foldNode(const Expression& e, int32_t i, bool running, int32_t prevConnective,
              const std::function<bool(int32_t)>& value) {
    const ExprNode& n = e.nodes.at(static_cast<size_t>(i));
    bool combined;
    if (n.group) {
        if (n.firstChild == -1)
            open("empty expression group (engine calls through a null child)", "spec-anim-runtime.md Sec8.5 hazard; none shipped (Sec8.10)");
        const bool chain = foldNode(e, n.firstChild, running, prevConnective, value);
        combined = combine(running, chain, prevConnective);
    } else {
        combined = combine(running, value(i), prevConnective);
    }
    if (n.next != -1) return foldNode(e, n.next, combined, n.connective, value);
    return combined;
}

}  // namespace

bool FoldExpression(const Expression& expr, const std::function<bool(int32_t)>& comparisonValue) {
    if (expr.root < 0) throw std::invalid_argument("FoldExpression: the expression has no tree");
    return foldNode(expr, expr.root, false, -1, comparisonValue);
}

bool CompareValues(ParamType type, Comparison cmp, uint32_t a, uint32_t b) {
    switch (type) {
    case ParamType::Bool: {
        const uint32_t la = a & 0xffu, lb = b & 0xffu;
        if (cmp == Comparison::Equal) return la == lb;
        if (cmp == Comparison::NotEqual) return la != lb;
        return false;
    }
    case ParamType::Integer: {
        const int32_t x = static_cast<int32_t>(a), y = static_cast<int32_t>(b);
        switch (cmp) {
        case Comparison::Equal: return x == y;
        case Comparison::NotEqual: return x != y;
        case Comparison::Greater: return x > y;
        case Comparison::GreaterOrEqual: return x >= y;
        case Comparison::Less: return x < y;
        case Comparison::LessOrEqual: return x <= y;
        default: return false;
        }
    }
    case ParamType::Float: {
        const double x = static_cast<double>(BitsFloat(a)), y = static_cast<double>(BitsFloat(b));
        switch (cmp) {
        case Comparison::Equal: return x == y;
        case Comparison::NotEqual: return x != y;
        case Comparison::Greater: return x > y;
        case Comparison::GreaterOrEqual: return x >= y;
        case Comparison::Less: return x < y;
        case Comparison::LessOrEqual: return x <= y;
        default: return false;
        }
    }
    default:
        return false;
    }
}

float MultiWayCurve(const BlendInput& in, float x) {
    for (int32_t j = 0; j + 1 < in.pointCount && j + 1 < 5; ++j) {
        const float r0 = in.range[j], r1 = in.range[j + 1];
        if (r0 <= x && x <= r1) {
            if (r1 == r0) return in.value[j];
            return in.value[j] + (in.value[j + 1] - in.value[j]) * (x - r0) / (r1 - r0);
        }
    }
    return 0.0f;
}

float FilterMath(const FilterDef& f, float x, float prev, float dt) {
    switch (f.kind) {
    case FilterKind::LinearMap: {
        const float c = std::min(std::max(x, f.inputMin), f.inputMax);  // input_max < input_min -> always input_max
        return (c - f.inputMin) / (f.inputMax - f.inputMin) * (f.outputMax - f.outputMin) + f.outputMin;
    }
    case FilterKind::DeltaMultiplier:
        return prev + f.multiplier * dt * (x - prev);
    case FilterKind::Cap:
        return std::min(std::max(x, f.outputMin), f.outputMax);  // output_min > output_max -> output_max
    case FilterKind::Multiplier:
        return f.basis + (x - f.basis) * f.multiplier;
    case FilterKind::None:
        break;
    }
    guard("filter of kind 0 (Sec7.10: the slot is unset)");
}

// ---------------------------------------------------------------------------
// GraphOutputList
// ---------------------------------------------------------------------------
bool GraphOutputList::append(const OutputEntry& e) {
    if (entries_.size() >= kCapacity) {
        ++overflowGuardCount_;
        return false;
    }
    entries_.push_back(e);
    peak_ = std::max(peak_, entries_.size());
    return true;
}

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------
int32_t GraphRuntimeServices::recordForTag(const GraphInstance&, int32_t) {
    open("0x004ccba0: State/Action index -> animation record through the row's anim-group tables (0x004ccf30)",
         "spec-anim-runtime.md Sec8.6 step 3: 0x004ccf30 is not specified");
}
int32_t GraphRuntimeServices::recordForClipName(const std::string&) {
    open("0x004bf650: clip_name -> loaded-animation record", "spec-anim-runtime.md Sec8.6 step 1 (the anim_files hashtable)");
}
float GraphRuntimeServices::recordDuration(int32_t) {
    open("0x004bd8c0: a record's clip duration", "spec-anim-runtime.md Sec8.6 (clip time clamp)");
}
float GraphRuntimeServices::clipDuration(int32_t) {
    open("0x004bd8c0: a clip index's duration", "spec-anim-runtime.md Sec8.5 clip-time family / Sec5.1 item 29");
}
bool GraphRuntimeServices::triggerFiredAnyLayer(const GraphInstance&, int32_t) {
    open("0x004bddb0: did this trigger fire on any layer", "spec-anim-runtime.md Sec8.5 trigger operand / U1 Sec6.6");
}
float GraphRuntimeServices::animationInstancePhase(const GraphInstance&) {
    open("0x004bcdb0: float +0x60 of the controller's animation instance", "spec-anim-runtime.md Sec8.3 pass 1 step 2 (U4)");
}
void GraphRuntimeServices::stopLayer(const GraphInstance&, int32_t) {
    open("0x004bcf00: stop a clip layer", "spec-anim-runtime.md Sec8.2 resets (U4)");
}

float ControllerGraphServices::recordDuration(int32_t record) {
    if (!data_) return GraphRuntimeServices::recordDuration(record);
    const std::optional<AnimRecordInfo> r = data_->record(record);
    if (!r) throw std::out_of_range("animation record " + std::to_string(record) + " does not exist");
    const std::optional<ClipTiming> t = data_->clipTiming(r->index08);
    if (!t) throw std::out_of_range("clip " + std::to_string(r->index08) + " has no header timing");
    return t->durationSeconds();
}

float ControllerGraphServices::clipDuration(int32_t clipIndex) {
    if (!data_) return GraphRuntimeServices::clipDuration(clipIndex);
    const std::optional<ClipTiming> t = data_->clipTiming(clipIndex);
    if (!t) throw std::out_of_range("clip " + std::to_string(clipIndex) + " has no header timing");
    return t->durationSeconds();
}

bool ControllerGraphServices::triggerFiredAnyLayer(const GraphInstance& row, int32_t triggerIndex) {
    if (!layers_) return GraphRuntimeServices::triggerFiredAnyLayer(row, triggerIndex);
    return layers_->triggerFiredAnyLayer(triggerIndex, -1);
}

// ---------------------------------------------------------------------------
// The evaluation of one row
// ---------------------------------------------------------------------------
namespace {

struct Eval {
    const NodeGraphRuntime& rt;
    const NodeGraphLibrary& lib;
    const ControlParameterRegistry& reg;
    const ControlFilterTable& filters;
    GraphInstance& row;
    GraphOutputList* out;  // null outside an evaluation (condition queries)
    GraphRuntimeServices& svc;

    // ---- definitions -------------------------------------------------------
    const GraphDef& graphDef(int32_t g) const {
        const GraphDef* d = lib.graph(g);
        if (!d) guard("graph index " + std::to_string(g) + " out of range");
        return *d;
    }
    const NodeDef& nodeDef(int32_t g, int32_t n) const {
        const GraphDef& d = graphDef(g);
        if (n < 0 || n >= static_cast<int32_t>(d.nodes.size()))
            guard("node index " + std::to_string(n) + " of graph '" + d.name + "' out of range");
        return d.nodes[static_cast<size_t>(n)];
    }
    const GraphDef& rootDef() const { return graphDef(row.graphIndex); }
    float dt() const { return row.timeStep3c; }

    // ---- node instances (0x004cc7b0 / 0x004ccc00) ---------------------------
    int32_t find(int32_t g, int32_t n, int32_t k) const {
        for (size_t i = 0; i < row.nodeInstances.size(); ++i) {
            const NodeInstance& x = row.nodeInstances[i];
            if (!x.isFree() && x.graph == g && x.node == n && x.transition == k) return static_cast<int32_t>(i);
        }
        return -1;
    }
    int32_t findOrCreate(int32_t g, int32_t n, int32_t k, bool* created = nullptr) {
        int32_t i = find(g, n, k);
        if (created) *created = false;
        if (i >= 0) return i;
        if (created) *created = true;
        for (size_t j = 0; j < row.nodeInstances.size(); ++j)
            if (row.nodeInstances[j].isFree()) {
                i = static_cast<int32_t>(j);
                break;
            }
        if (i < 0) {
            row.nodeInstances.emplace_back();  // the pool 0x03519890 grows by one
            i = static_cast<int32_t>(row.nodeInstances.size() - 1);
        }
        NodeInstance& x = row.nodeInstances[static_cast<size_t>(i)];
        x = NodeInstance{};
        x.graph = static_cast<int16_t>(g);
        x.node = static_cast<int16_t>(n);
        x.transition = static_cast<int16_t>(k);
        x.activeTime = 0.0f;
        x.lastEvaluated = row.evalCounter44;
        return i;
    }
    NodeInstance& inst(int32_t i) { return row.nodeInstances.at(static_cast<size_t>(i)); }
    void freeInstance(int32_t i) {
        if (i >= 0) inst(i) = NodeInstance{};
    }
    // Advance by `delta` once per evaluation, clamped at 0 (Sec8.2 +0x08 / +0x0c).
    void advance(int32_t i, float delta) {
        NodeInstance& x = inst(i);
        if (x.lastEvaluated == row.evalCounter44) return;
        x.activeTime = std::max(x.activeTime + delta, 0.0f);
        x.lastEvaluated = row.evalCounter44;
    }

    // ---- output list helpers -------------------------------------------------
    void tag(size_t from, int32_t t) {
        for (size_t e = from; e < out->size(); ++e) (*out)[e].inputTag = static_cast<int16_t>(t);
    }

    // ---- 0x004cca80 ------------------------------------------------------------
    void collectGarbage() {
        for (NodeInstance& x : row.nodeInstances) {
            if (x.isFree()) continue;
            if (static_cast<uint32_t>(row.evalCounter44 - x.lastEvaluated) > 1u) {
                x = NodeInstance{};
                continue;
            }
            x.phaseOffset = -1.0f;
            x.layerPhase = 0.0f;
            x.flags40 &= ~1u;
        }
        row.rootMotionSync1c = 0;
        row.rootMotionElapsed20 = 0.0f;
        row.rootMotionLength24 = 0.0f;
    }

    // ---- Sec8.8 filters --------------------------------------------------------
    int32_t filterListIndex(int32_t filterId) const {
        const GraphDef& r = rootDef();
        for (size_t i = 0; i < r.filterIds.size(); ++i)
            if (r.filterIds[i] == filterId) return static_cast<int32_t>(i);
        return -1;
    }
    uint32_t aliasValue(int32_t globalId) const {
        const int16_t a = rootDef().aliases.globalToAlias(static_cast<int16_t>(globalId));
        if (a < 0 || a >= static_cast<int32_t>(row.values.size()))
            guard("parameter id " + std::to_string(globalId) + " is not in the root graph's alias table");
        return row.values[static_cast<size_t>(a)].dword0;
    }
    void ensureFilterOutputs() {
        const size_t n = rootDef().filterIds.size();
        if (row.filterOutputs.size() != n) row.filterOutputs.assign(n, 0u);
    }
    void evaluateFilter(int32_t listIndex) {
        const FilterDef* f = filters.filter(rootDef().filterIds.at(static_cast<size_t>(listIndex)));
        if (!f) guard("filter id out of range");
        float x;
        if (f->sourceIsFilter) {
            const int32_t j = filterListIndex(f->sourceId);
            if (j < 0) guard("source filter of '" + f->name + "' is not in the root graph's filter list");
            evaluateFilter(j);  // the shared base 0x004d1390 evaluates the source first
            x = BitsFloat(row.filterOutputs[static_cast<size_t>(j)]);
        } else {
            x = BitsFloat(aliasValue(f->sourceId));  // raw dword read as a float whatever the type
        }
        const float prev = BitsFloat(row.filterOutputs[static_cast<size_t>(listIndex)]);
        row.filterOutputs[static_cast<size_t>(listIndex)] = FloatBits(FilterMath(*f, x, prev, dt()));
    }

    // ---- Sec8.5 operands -------------------------------------------------------
    // The clip-time family divides by the duration of the clip index at
    // instance +0x38, which the post step writes (Sec5.1 item 29), NOT the
    // record's clip of the Sec8.6 clamp. Unwritten (-1, never posted): a
    // percent with a zero numerator is 0 (item 29: "only a zero duration would
    // matter"), anything else is OPEN.
    float instanceDuration(const NodeInstance& x, bool zeroNumeratorOk) {
        if (x.clipIndex == -1) {
            if (zeroNumeratorOk && x.clipTime == 0.0f) return 1.0f;  // 0 / d = 0 for any non-zero d
            open("clip-time operand on a clip instance whose +0x38 clip index the post step has not written",
                 "spec-anim-runtime.md Sec5.1 item 29 / Sec6.10 step 5 (U4)");
        }
        return svc.clipDuration(x.clipIndex);
    }

    // 0x004b8fe0 / 0x004b8f20: the source node and every node of every graph
    // nested under it.
    void collectKeys(int32_t g, std::vector<std::pair<int32_t, int32_t>>& keys, std::set<int32_t>& stack) {
        if (!stack.insert(g).second) guard("child_node_graph cycle");
        const GraphDef& d = graphDef(g);
        for (size_t j = 0; j < d.nodes.size(); ++j) {
            keys.push_back({g, static_cast<int32_t>(j)});
            if (d.nodes[j].childGraph != -1) collectKeys(d.nodes[j].childGraph, keys, stack);
        }
        stack.erase(g);
    }
    float clipTimeOperand(const Operand& op, int32_t g, int32_t n) {
        std::vector<std::pair<int32_t, int32_t>> keys{{g, n}};
        std::set<int32_t> stack;
        const NodeDef& src = nodeDef(g, n);
        if (src.childGraph != -1) collectKeys(src.childGraph, keys, stack);
        if (keys.size() > 250) guard("more than 250 clip-time keys (Sec8.5: at most 250)");
        auto inKeys = [&](const NodeInstance& x) {
            for (const auto& k : keys)
                if (k.first == x.graph && k.second == x.node) return true;
            return false;
        };
        // CHOSEN reading of "live clip instance": a node instance (transition
        // word -1) of a state node or `clip` node whose record +0x34 is set.
        const NodeInstance* first = nullptr;
        const NodeInstance* last = nullptr;
        std::vector<const NodeInstance*> matches;
        for (const NodeInstance& x : row.nodeInstances) {
            if (x.isFree() || x.transition != -1 || x.node < 0 || !inKeys(x)) continue;
            const ClipBlock* cb = ClipBlockOf(nodeDef(x.graph, x.node));
            if (!cb || x.record == -1) continue;
            if (!op.ownClip && (cb->tagIndex != op.id || x.layerHandle == -1)) continue;
            if (!first) first = &x;
            last = &x;
            matches.push_back(&x);
        }
        switch (op.type) {
        case OperandType::ClipTime:
            return last ? last->clipTime : 0.0f;
        case OperandType::ClipTimeRemaining:
            return last ? instanceDuration(*last, false) - last->clipTime : 0.0f;
        default: {  // ClipTimeAsPercent
            if (!op.ownClip) return first ? first->clipTime / instanceDuration(*first, true) : 0.0f;
            if (matches.empty()) return 1.0f;
            float best = 0.0f;
            for (size_t i = 0; i < matches.size(); ++i) {
                const float pct = matches[i]->clipTime / instanceDuration(*matches[i], true);
                best = i ? std::max(best, pct) : pct;
            }
            return best;
        }
        }
    }
    // 0x004d04e0.
    uint32_t operandValue(const Operand& op, int32_t g, int32_t n, int32_t k, const Condition* cond) {
        switch (op.type) {
        case OperandType::Boolean:
        case OperandType::Integer:
        case OperandType::Float:
            return op.literal;
        case OperandType::ControlParameter:
            return aliasValue(op.id);
        case OperandType::ControlFilter: {
            const int32_t j = filterListIndex(op.id);
            if (j < 0 || j >= static_cast<int32_t>(row.filterOutputs.size()))
                guard("filter '" + op.sourceName + "' is not in the root graph's filter list");
            return row.filterOutputs[static_cast<size_t>(j)];
        }
        case OperandType::LocalControlParameter:
            return (cond && cond->local) ? cond->local->defaultDword : 0u;
        case OperandType::AnimationGroup:
            return row.skeletonSet38;
        case OperandType::ClipTime:
        case OperandType::ClipTimeRemaining:
        case OperandType::ClipTimeAsPercent:
            return FloatBits(clipTimeOperand(op, g, n));
        case OperandType::TransitionLength: {
            const NodeDef& nd = nodeDef(g, n);
            if (k < 0 || k >= static_cast<int32_t>(nd.transitions.size())) guard("transition length outside a transition");
            return FloatBits(nd.transitions[static_cast<size_t>(k)].length);
        }
        case OperandType::Trigger:
            return svc.triggerFiredAnyLayer(row, op.triggerIndex) ? 1u : 0u;
        case OperandType::NodeActiveTime: {
            // float +0x28 of the node's instance, no null check in the engine
            // (unused in shipped data; its comparison type is 0, always false).
            const int32_t i = find(g, n, -1);
            return i >= 0 ? FloatBits(inst(i).clipTime) : 0u;
        }
        case OperandType::None:
            break;
        }
        return 0u;
    }
    bool comparison(const Operand& a, Comparison c, const Operand& b, int32_t g, int32_t n, int32_t k, const Condition* cond) {
        const ParamType t = rt.operandType(a, cond);
        const uint32_t va = operandValue(a, g, n, k, cond);
        const uint32_t vb = operandValue(b, g, n, k, cond);
        return CompareValues(t, c, va, vb);
    }
    bool transitionCondition(int32_t g, int32_t n, int32_t k) {
        const Transition& t = nodeDef(g, n).transitions.at(static_cast<size_t>(k));
        if (t.expression.present()) {
            const Expression& e = t.expression;
            if (e.root < 0) guard("a non-empty start with no expression tree (null root)");
            return FoldExpression(e, [&](int32_t i) {
                const ExprNode& en = e.nodes[static_cast<size_t>(i)];
                if (en.operand1 < 0 || en.operand2 < 0) guard("comparison with a missing operand (index -1)");
                return comparison(e.parameters[static_cast<size_t>(en.operand1)], en.comparison,
                                  e.parameters[static_cast<size_t>(en.operand2)], g, n, k, nullptr);
            });
        }
        if (t.orForm) {
            for (const Condition& c : t.conditions)
                if (comparison(c.parameter1, c.comparison, c.parameter2, g, n, k, &c)) return true;
            return false;
        }
        for (const Condition& c : t.conditions)
            if (!comparison(c.parameter1, c.comparison, c.parameter2, g, n, k, &c)) return false;
        return true;
    }

    // ---- Sec8.2 resets -----------------------------------------------------------
    void stopClipLayer(int32_t i) {
        if (i >= 0 && inst(i).layerHandle != -1) svc.stopLayer(row, inst(i).layerHandle);
    }
    void resetNode(int32_t g, int32_t n) {
        const NodeDef& nd = nodeDef(g, n);
        switch (nd.kind) {
        case NodeKind::State:  // 0x004ba050
            if (nd.childGraph != -1) resetGraph(nd.childGraph);
            stopClipLayer(find(g, n, -1));
            for (size_t k = 0; k < nd.transitions.size(); ++k) {
                const int32_t t = find(g, n, static_cast<int32_t>(k));
                if (t < 0) continue;
                freeInstance(t);
                resetNode(g, nd.transitions[k].toNode);
            }
            freeInstance(find(g, n, -1));
            break;
        case NodeKind::Clip:  // 0x004bbe70
            stopClipLayer(find(g, n, -1));
            freeInstance(find(g, n, -1));
            break;
        case NodeKind::BlendTreeReference:
        case NodeKind::StateMachineReference:  // 0x004bba30
            if (nd.childGraph != -1) resetGraph(nd.childGraph);
            freeInstance(find(g, n, -1));
            break;
        default:  // merge / switch / multi way blender: 0x004bbb50 -> only themselves
            freeInstance(find(g, n, -1));
            break;
        }
    }
    void resetGraph(int32_t g) {  // 0x004bb6c0
        const GraphDef& d = graphDef(g);
        for (size_t j = 0; j < d.nodes.size(); ++j) resetNode(g, static_cast<int32_t>(j));
        freeInstance(find(g, -1, -1));
    }

    // ---- Sec8.6 clip blocks ------------------------------------------------------
    void clip(const ClipBlock& b, int32_t pass, int32_t g, int32_t n) {
        if (b.clipNameRecord == -1 && !b.clipName.empty()) b.clipNameRecord = svc.recordForClipName(b.clipName);
        if (b.tagIndex == -1 && b.clipNameRecord == -1) return;  // emits nothing in either pass
        const int32_t i = find(g, n, -1);
        if (i < 0) return;
        if (pass == 0) {
            int32_t rec = (b.tagIndex != -1) ? svc.recordForTag(row, b.tagIndex) : -1;
            if (rec == -1) rec = b.clipNameRecord;
            inst(i).record = rec;
            if (rec != -1) {
                NodeInstance& x = inst(i);
                x.prevClipTime = x.clipTime;
                x.clipTime = std::min(x.activeTime, svc.recordDuration(rec));
            }
            return;
        }
        const NodeInstance& x = inst(i);
        if (x.record == -1) return;
        OutputEntry e;
        e.layer = b.layer;
        e.graph = static_cast<int16_t>(g);
        e.node = static_cast<int16_t>(n);
        e.tagIndex = b.tagIndex;
        e.record = x.record;
        e.weight = 1.0f;
        e.flags = b.loop ? kOutLoop : 0u;
        if (!b.loop) {
            e.clipTime = x.clipTime;
            e.prevClipTime = x.prevClipTime;
        }
        out->append(e);
        if (b.matchCrc) row.flags48 |= 0x04u;
    }

    // ---- Sec8.3 / Sec8.4 state machines -------------------------------------------
    // 0x004baea0.
    int32_t testTransition(int32_t g, int32_t n, int32_t k, int32_t current) {
        const Transition& t = nodeDef(g, n).transitions.at(static_cast<size_t>(k));
        int32_t ti = find(g, n, k);
        if (ti >= 0) advance(ti, (k == current) ? dt() : -dt());
        const bool cond = transitionCondition(g, n, k);
        if (current == -1 && cond && !(row.flags48 & 0x02u)) {
            ti = findOrCreate(g, n, k);
            if (inst(ti).activeTime >= t.length) {
                inst(ti).activeTime = t.length;
                return 2;
            }
            return 1;
        }
        if (ti >= 0 && inst(ti).activeTime >= t.length) {
            inst(ti).activeTime = t.length;
            return 2;
        }
        if (k != current) {
            if (ti >= 0 && inst(ti).activeTime <= 0.0f) {
                resetNode(g, t.toNode);
                freeInstance(ti);  // 0x004bb1d0
                return 4;
            }
            return 0;
        }
        if (cond) return 0;
        const bool hasAfter = t.cancelableAfter != -1.0f, hasBefore = t.cancelableBefore != -1.0f;
        if (!hasAfter && !hasBefore) return 0;  // fire-and-forget
        const float a = hasAfter ? t.cancelableAfter : 0.0f;
        const float b = hasBefore ? t.cancelableBefore : t.length;
        const float e = ti >= 0 ? inst(ti).activeTime : 0.0f;
        if (b >= a) return (a <= e && e <= b) ? 3 : 0;
        return (e <= b || e >= a) ? 3 : 0;
    }
    static bool inActive(const NodeInstance& x, int32_t k) {
        for (int32_t v : x.active)
            if (v == k) return true;
        return false;
    }
    static void removeActive(NodeInstance& x, int32_t k) {
        int32_t w = 0;
        for (int32_t r = 0; r < 8; ++r)
            if (x.active[r] != -1 && x.active[r] != k) x.active[w++] = x.active[r];
        for (; w < 8; ++w) x.active[w] = -1;
    }
    static void moveToEnd(NodeInstance& x, int32_t k) {
        removeActive(x, k);
        for (int32_t& v : x.active)
            if (v == -1) {
                v = k;
                return;
            }
        guard("a 9th active transition (the list holds 8; Sec8.4)");
    }
    void runTransitions(int32_t g, int32_t n) {
        const NodeDef& nd = nodeDef(g, n);
        int32_t si = find(g, n, -1);
        if (si < 0) return;
        const int32_t inProgress = inst(si).current;
        if (inProgress != -1) {
            const int32_t code = testTransition(g, n, inProgress, inProgress);
            si = find(g, n, -1);
            if (si < 0) return;
            if (code == 2) return;
            if (code == 3) inst(si).current = -1;  // stays in the active list and rewinds
            if (code == 4) removeActive(inst(si), inProgress);
        }
        for (int32_t k = 0; k < static_cast<int32_t>(nd.transitions.size()); ++k) {
            if (k == inProgress) continue;
            si = find(g, n, -1);
            if (si < 0) return;
            const int32_t cur = inst(si).current;
            if (cur != -1 && !inActive(inst(si), k)) continue;  // only rewinding ones while one is in progress
            const int32_t code = testTransition(g, n, k, cur);
            si = find(g, n, -1);
            if (si < 0) return;
            if (code == 1) {
                inst(si).current = k;
                moveToEnd(inst(si), k);
            } else if (code == 2) {
                inst(si).current = k;
                return;
            } else if (code == 4) {
                removeActive(inst(si), k);
            }
        }
    }
    // 0x004b9cf0.
    void stateStep(int32_t pass, int32_t g, int32_t n, bool test) {
        const NodeDef& nd = nodeDef(g, n);
        if (pass == 0) {
            const int32_t i = findOrCreate(g, n, -1);
            advance(i, dt());
            if (nd.childGraph != -1) evaluateGraph(nd.childGraph, 0);
            else if (nd.clip) clip(*nd.clip, 0, g, n);
            if (test) runTransitions(g, n);
            return;
        }
        if (nd.childGraph != -1) evaluateGraph(nd.childGraph, 1);
        else if (nd.clip) clip(*nd.clip, 1, g, n);
    }
    void phaseAlign(int32_t g, int32_t stateNode, size_t from) {
        const int32_t si = find(g, stateNode, -1);
        if (si < 0 || inst(si).phaseOffset == -1.0f || from >= out->size()) return;
        const float offset = inst(si).phaseOffset;
        const float x = svc.animationInstancePhase(row);
        for (size_t e = from; e < out->size(); ++e) {
            const int32_t ci = find((*out)[e].graph, (*out)[e].node, -1);
            if (ci < 0) continue;
            float v = x - offset;
            if (v < 0.0f) v += 1.0f;
            inst(ci).layerPhase = v;
        }
    }
    // 0x004b9300.
    void stateMachine(int32_t g, int32_t pass) {
        const GraphDef& d = graphDef(g);
        if (pass == 0) {
            int32_t mi = findOrCreate(g, -1, -1);
            advance(mi, dt());
            if (inst(mi).current == -1) {
                if (d.entryNode < 0 || d.entryNode >= static_cast<int32_t>(d.nodes.size()))
                    guard("default_node of '" + d.name + "' did not resolve");
                inst(mi).current = d.entryNode;
                findOrCreate(g, d.entryNode, -1);
            }
            stateStep(0, g, inst(mi).current, true);
            // Completion loop.
            for (int32_t iter = 0;; ++iter) {
                if (iter >= NodeGraphRuntime::kCompletionLoopGuard)
                    guard("completion loop of '" + d.name + "' did not settle (a zero-length cycle would spin the engine)");
                mi = find(g, -1, -1);
                const int32_t cur = inst(mi).current;
                const int32_t si = find(g, cur, -1);
                if (si < 0) break;
                const int32_t t = inst(si).current;
                if (t == -1) break;
                const int32_t ti = find(g, cur, t);
                const Transition& tr = d.nodes[static_cast<size_t>(cur)].transitions.at(static_cast<size_t>(t));
                if (ti < 0 || inst(ti).activeTime != tr.length) break;
                if (tr.toNode < 0) guard("to_node '" + tr.toNodeName + "' did not resolve");
                const int32_t tgt = findOrCreate(g, tr.toNode, -1);
                if (tr.cyclicPercent != -1.0f) inst(tgt).phaseOffset = tr.cyclicPercent;
                freeInstance(ti);
                resetNode(g, cur);
                mi = find(g, -1, -1);
                inst(mi).current = tr.toNode;
                stateStep(0, g, tr.toNode, true);
            }
            // In-flight and rewinding targets: stepped, test byte clear.
            mi = find(g, -1, -1);
            const int32_t cur = inst(mi).current;
            const NodeDef& cd = d.nodes[static_cast<size_t>(cur)];
            for (size_t k = 0; k < cd.transitions.size(); ++k) {
                if (find(g, cur, static_cast<int32_t>(k)) < 0) continue;
                const Transition& tr = cd.transitions[k];
                if (tr.toNode < 0) guard("to_node '" + tr.toNodeName + "' did not resolve");
                stateStep(0, g, tr.toNode, false);
                if (tr.cyclicPercent != -1.0f) {
                    const int32_t ti = find(g, tr.toNode, -1);
                    if (ti >= 0) inst(ti).phaseOffset = tr.cyclicPercent;
                }
            }
            // Root-motion sync.
            const int32_t si = find(g, cur, -1);
            if (si >= 0 && inst(si).current != -1) {
                const Transition& tr = cd.transitions.at(static_cast<size_t>(inst(si).current));
                if (tr.syncControllerTransform) {
                    const int32_t ti = find(g, cur, inst(si).current);
                    row.rootMotionSync1c = 1;
                    row.rootMotionLength24 = tr.length;
                    row.rootMotionElapsed20 = ti >= 0 ? inst(ti).activeTime : -1.0f;
                }
            }
            return;
        }
        // Pass 1: the cross-fade.
        const int32_t mi = find(g, -1, -1);
        if (mi < 0 || inst(mi).current == -1) return;
        const int32_t cur = inst(mi).current;
        const size_t s0 = out->size();
        stateStep(1, g, cur, false);
        tag(s0, 0);
        phaseAlign(g, cur, s0);
        const int32_t si = find(g, cur, -1);
        if (si < 0) return;
        std::vector<int32_t> list;
        for (int32_t v : inst(si).active)
            if (v != -1) list.push_back(v);
        const NodeDef& cd = d.nodes[static_cast<size_t>(cur)];
        for (size_t idx = 0; idx < list.size(); ++idx) {
            const Transition& tr = cd.transitions.at(static_cast<size_t>(list[idx]));
            const int32_t ti = find(g, cur, list[idx]);
            if (ti < 0) guard("an active transition with no instance");
            const float p = (1.0f / tr.length) * inst(ti).activeTime;
            const size_t st = out->size();
            stateStep(1, g, tr.toNode, false);
            tag(st, static_cast<int32_t>(idx) + 1);
            phaseAlign(g, tr.toNode, st);
            for (size_t e = s0; e < st; ++e) {
                (*out)[e].weight *= 1.0f - p;
                (*out)[e].flags |= kOutOutgoing;
            }
            for (size_t e = st; e < out->size(); ++e) {
                const float before = (*out)[e].weight;
                (*out)[e].weight *= p;
                if (before != 0.0f && (*out)[e].weight == 0.0f) (*out)[e].flags |= kOutZeroedInTransition;
            }
        }
    }

    // ---- Sec8.7 blend trees --------------------------------------------------------
    // 0x004bb8a0.
    void sharedInputs(int32_t pass, int32_t g, int32_t n) {
        const NodeDef& nd = nodeDef(g, n);
        if (nd.childGraph != -1) {
            const size_t s = out ? out->size() : 0;
            evaluateGraph(nd.childGraph, pass);
            if (pass == 1) tag(s, 0);
            return;
        }
        for (int32_t i = 0; i < static_cast<int32_t>(nd.inputs.size()); ++i) {
            if (nd.selector != -1 && i != nd.selector) continue;
            const size_t s = out ? out->size() : 0;
            blendNode(pass, g, nd.inputs[static_cast<size_t>(i)].node);
            if (pass == 1) tag(s, i);
        }
    }
    void multiWayPass1(int32_t g, int32_t n) {
        const NodeDef& nd = nodeDef(g, n);
        bool fresh = false;
        const int32_t mi = findOrCreate(g, n, -1, &fresh);
        // CHOSEN: the instance is advanced like every other evaluated node, so
        // 0x004cca80 keeps it while the blender is evaluated (Sec8.10's ramp
        // replay needs it to persist).
        advance(mi, dt());
        const size_t s0 = out->size();
        sharedInputs(1, g, n);
        const float x = BitsFloat(operandValue(*nd.parameter, g, n, -1, nullptr));  // as a float whatever its type
        if (nd.inputs.size() > 5) guard("a multi way blender with more than 5 inputs (instance +0x48 holds 5)");
        for (size_t i = 0; i < nd.inputs.size(); ++i) {
            const BlendInput& bi = nd.inputs[i];
            if (!bi.filled) guard("an abandoned multi-way input (Sec7.8: its record was never filled)");
            const float target = MultiWayCurve(bi, x);
            float& w = inst(mi).blendWeights[i];
            if (fresh || bi.maxBlendRate == 0.0f) {
                w = target;
            } else {
                // Float arithmetic on the float fields (the spec gives no
                // precision). Sec8.10 originally said "12 updates" for 0.4 s,
                // from a double-precision replay at an exact boundary
                // (12 x (1/30) / 0.4 = 1); with the weight stored as a float
                // the 12th update lands within rounding of 1.0 and the 13th
                // snaps it - this project's own 13 was flagged as a finding
                // against that text, not assumed. 2026-10-07: Sec8.10 was
                // corrected to 13, re-derived by Team A from disassembling
                // the real 0x004bc0d0 (pure SSE/SSE2, no x87) rather than
                // from abstract reasoning - CONFIRMS this comment's own
                // account exactly, including the float32-ULP mechanism.
                const float step = dt() / bi.maxBlendRate;
                if (std::fabs(target - w) <= step) w = target;
                else w += (target > w) ? step : -step;
            }
        }
        for (size_t e = s0; e < out->size(); ++e) {
            const int32_t t = (*out)[e].inputTag;
            if (t < 0 || t >= static_cast<int32_t>(nd.inputs.size())) guard("an entry tag outside the blender's inputs");
            (*out)[e].weight *= inst(mi).blendWeights[static_cast<size_t>(t)];
        }
    }
    void blendNode(int32_t pass, int32_t g, int32_t n) {
        const NodeDef& nd = nodeDef(g, n);
        switch (nd.kind) {
        case NodeKind::Merge:  // 0x004bbaf0
        case NodeKind::BlendTreeReference:
        case NodeKind::StateMachineReference:
            sharedInputs(pass, g, n);
            break;
        case NodeKind::Switch:  // 0x004bbc00
            if (pass == 0) {
                const ParamType t = rt.operandType(*nd.parameter, nullptr);
                const uint32_t v = operandValue(*nd.parameter, g, n, -1, nullptr);
                if (t == ParamType::Bool) nd.selector = (v & 0xffu) ? 1 : 0;
                else if (t == ParamType::Integer) nd.selector = static_cast<int32_t>(v);
                else nd.selector = -1;
            }
            sharedInputs(pass, g, n);
            break;
        case NodeKind::Clip:  // 0x004bbda0
            if (pass == 0) advance(findOrCreate(g, n, -1), dt());
            clip(*nd.clip, pass, g, n);
            break;
        case NodeKind::MultiWayBlender:  // 0x004bc0d0
            if (pass == 0) sharedInputs(0, g, n);
            else multiWayPass1(g, n);
            break;
        case NodeKind::State:
            guard("a state node inside a blend tree");
        }
    }
    void evaluateGraph(int32_t g, int32_t pass) {
        const GraphDef& d = graphDef(g);
        if (d.kind == GraphKind::StateMachine) {
            stateMachine(g, pass);
            return;
        }
        // 0x004bb660: the output node's vtable +0x0c.
        if (d.entryNode < 0 || d.entryNode >= static_cast<int32_t>(d.nodes.size()))
            guard("output_node of '" + d.name + "' did not resolve");
        blendNode(pass, g, d.entryNode);
    }
};

}  // namespace

ParamType NodeGraphRuntime::operandType(const Operand& op, const Condition* cond) const {
    switch (op.type) {
    case OperandType::Boolean: return ParamType::Bool;
    case OperandType::Integer: return ParamType::Integer;
    case OperandType::Float: return ParamType::Float;
    case OperandType::ControlParameter: {
        const ParamRecord* r = registry_.record(op.id);
        return r ? r->type : ParamType::None;
    }
    case OperandType::ControlFilter: {
        const FilterDef* f = filters_.filter(op.id);
        return f ? f->type : ParamType::None;
    }
    case OperandType::LocalControlParameter: return (cond && cond->local) ? cond->local->type : ParamType::None;
    case OperandType::AnimationGroup: return ParamType::Integer;
    case OperandType::ClipTime:
    case OperandType::ClipTimeRemaining:
    case OperandType::ClipTimeAsPercent:
    case OperandType::TransitionLength: return ParamType::Float;
    case OperandType::Trigger: return ParamType::Bool;
    case OperandType::NodeActiveTime:  // 0: always compares false
    case OperandType::None: break;
    }
    return ParamType::None;
}

void NodeGraphRuntime::evaluate(GraphInstance& row, int32_t pass, GraphOutputList& out, GraphRuntimeServices& svc) const {
    if (row.animGroupTable30 == 0) return;  // 0x004cc6ef
    Eval ev{*this, lib_, registry_, filters_, row, &out, svc};
    ev.rootDef();  // validates the row's graph index
    if (pass == 0) {
        row.flags48 &= ~0x04u;
        ev.ensureFilterOutputs();
        for (int32_t i = 0; i < static_cast<int32_t>(row.filterOutputs.size()); ++i) ev.evaluateFilter(i);
        ev.collectGarbage();
        ev.evaluateGraph(row.graphIndex, 0);
        return;
    }
    ev.evaluateGraph(row.graphIndex, 1);
    // The alias toggle requests (U1 Sec6.10; inert, Sec8.1).
    for (ParamSlot& s : row.values) {
        if (!(s.dword1 & 0xffu)) continue;
        s.dword0 = (s.dword0 & ~0xffu) | ((s.dword0 & 0xffu) ? 0u : 1u);
        s.dword1 &= ~0xffu;
    }
    ++row.evalCounter44;
    row.timeStep3c = 0.0f;
}

bool NodeGraphRuntime::transitionCondition(const GraphInstance& row, int32_t graph, int32_t node, int32_t transition,
                                           GraphRuntimeServices& svc) const {
    // Reads only; the const_cast lets the shared evaluator run (no write path
    // is reachable from a condition evaluation).
    Eval ev{*this, lib_, registry_, filters_, const_cast<GraphInstance&>(row), nullptr, svc};
    return ev.transitionCondition(graph, node, transition);
}

// ---------------------------------------------------------------------------
// NodeGraphEvaluator
// ---------------------------------------------------------------------------
GraphStepOutput NodeGraphEvaluator::evaluateGraphAndPost(Controller& controller, int32_t slotIndex, GraphInstance& row,
                                                         ClipDedupList& clips) {
    ControllerGraphServices fallback(controller.environment().data, controller.environment().layers);
    GraphRuntimeServices& svc = services_ ? *services_ : fallback;
    last_ = GraphOutputList{};
    runtime_.evaluate(row, 0, last_, svc);
    runtime_.evaluate(row, 1, last_, svc);
    if (last_.size() == 0) return GraphStepOutput{};
    return postOutput(controller, slotIndex, row, last_, clips);
}

GraphStepOutput NodeGraphEvaluator::postOutput(Controller&, int32_t, GraphInstance&, const GraphOutputList&, ClipDedupList&) {
    throw OpenStateError("NOT IMPLEMENTED: the graph post step of 0x004b0b80 (output list -> clip layers)",
                         "spec-anim-runtime.md Sec6.10 step 5 / Sec8.9: 0x004cca40, 0x004bce30 / 0x004bce90, 0x004bdb10, "
                         "0x004bd9b0, 0x004bda10 are U4's");
}

int32_t NodeGraphEvaluator::findNamedClipNode(const GraphInstance& row, int32_t id, int32_t firstNode) {
    for (int32_t i = std::max(firstNode, 0); i < static_cast<int32_t>(row.nodeInstances.size()); ++i) {
        const NodeInstance& x = row.nodeInstances[static_cast<size_t>(i)];
        if (x.graph == -1 || x.node == -1 || x.transition != -1) continue;
        const GraphDef* g = runtime_.library().graph(x.graph);
        if (!g || x.node >= static_cast<int32_t>(g->nodes.size())) continue;
        const ClipBlock* cb = ClipBlockOf(g->nodes[static_cast<size_t>(x.node)]);
        if (cb && cb->aliasId == id) return i;
    }
    return -1;
}

}  // namespace sr3animruntime
