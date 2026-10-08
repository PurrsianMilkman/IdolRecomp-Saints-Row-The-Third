// Animation runtime U2: the node-graph file format. Source:
// spec-anim-runtime.md Sec7 only (plus the two registries' own specs in
// spec-tables-animation.md Sec1.3 / Sec5 / Sec9) - see
// include/sr3animruntime/node_graph.h.
#include "sr3animruntime/node_graph.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <set>
#include <string>

#include "sr3animruntime/controller.h"  // NamedClipNodeId
#include "sr3xtbl/xtbl.h"

namespace sr3animruntime {

using sr3xtbl::Children;
using sr3xtbl::ChildText;
using sr3xtbl::FindChild;
using sr3xtbl::NameEquals;
using sr3xtbl::Node;

// ---------------------------------------------------------------------------
// Sec7.3 lexical rules
// ---------------------------------------------------------------------------
std::string LexText(const Node* parent, std::string_view child, std::string_view dflt) {
    const std::string* t = ChildText(parent, child);
    return t ? *t : std::string(dflt);
}

float LexFloatText(const std::string& text) { return static_cast<float>(std::strtod(text.c_str(), nullptr)); }

int32_t LexIntText(const std::string& text) {
    // strtol with base 0; out-of-range text saturates (the C runtime's rule).
    return static_cast<int32_t>(std::strtol(text.c_str(), nullptr, 0));
}

bool LexBoolText(std::string_view text) {
    return NameEquals(text, "yes") || NameEquals(text, "true") || NameEquals(text, "1");
}

float LexFloat(const Node* parent, std::string_view child, float dflt) {
    const std::string* t = ChildText(parent, child);
    return t ? LexFloatText(*t) : dflt;
}

int32_t LexInt(const Node* parent, std::string_view child, int32_t dflt) {
    const std::string* t = ChildText(parent, child);
    return t ? LexIntText(*t) : dflt;
}

bool LexBool(const Node* parent, std::string_view child, bool dflt) {
    const std::string* t = ChildText(parent, child);
    return t ? LexBoolText(*t) : dflt;
}

// ---------------------------------------------------------------------------
// Registries
// ---------------------------------------------------------------------------
StateActionRegistry StateActionRegistry::build(const std::vector<std::string>& presets,
                                               const std::vector<std::string>& stateNames,
                                               const std::vector<std::string>& actionNames) {
    StateActionRegistry r;
    r.names_ = presets;
    r.presetCount_ = static_cast<int32_t>(presets.size());
    auto add = [&](const std::string& name) {
        // A preset match reuses the preset's index (the duplicate scan never
        // looks past the presets); anything else is appended.
        for (int32_t i = 0; i < r.presetCount_; ++i)
            if (sr3xtbl::NameHash(name) == sr3xtbl::NameHash(r.names_[static_cast<size_t>(i)]) &&
                NameEquals(name, r.names_[static_cast<size_t>(i)]))
                return;
        r.names_.push_back(name);
    };
    for (const std::string& n : stateNames) add(n);
    for (const std::string& n : actionNames) add(n);
    return r;
}

int32_t StateActionRegistry::lookup(std::string_view name) const {
    if (name.empty()) return -1;
    for (size_t i = 0; i < names_.size(); ++i)
        if (NameEquals(name, names_[i])) return static_cast<int32_t>(i);
    return -1;
}

uint32_t TriggerNameHash(std::string_view name) {
    uint32_t h = 0;
    for (char ch : name) {
        int32_t c = static_cast<int32_t>(static_cast<signed char>(ch));  // sign-extended
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        h = (h << 6) | (h >> 26);
        h ^= static_cast<uint32_t>(c);
    }
    return h;
}

TriggerRegistry TriggerRegistry::build(const std::vector<std::string>& names) {
    TriggerRegistry r;
    for (const std::string& n : names) {
        if (n.size() >= 32) continue;
        if (r.count() >= kCapacity) continue;
        const uint32_t h = TriggerNameHash(n);
        bool dup = false;
        for (uint32_t e : r.hashes_)
            if (e == h) dup = true;
        if (dup) continue;
        r.hashes_.push_back(h);
    }
    return r;
}

int32_t TriggerRegistry::lookup(std::string_view name) const {
    const uint32_t h = TriggerNameHash(name);
    for (size_t i = 0; i < hashes_.size(); ++i)
        if (hashes_[i] == h) return static_cast<int32_t>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// Sec7.10 control_filters.xtbl
// ---------------------------------------------------------------------------
ControlFilterTable ControlFilterTable::load(const sr3xtbl::Document& doc, const ControlParameterRegistry& registry) {
    static constexpr const char* kKinds[5] = {"", "multiplier", "delta multiplier", "linear map", "cap"};
    ControlFilterTable t;
    const std::vector<const Node*> rows = Children(FindChild(doc.root(), "control_filters"), "control_filter");
    // Pass 1.
    for (const Node* row : rows) {
        FilterDef f;
        f.name = LexText(row, "name", "none");
        const std::string type = LexText(row, "type", "none");
        for (int32_t k = 1; k <= 4; ++k)
            if (NameEquals(type, kKinds[k])) {
                f.kind = static_cast<FilterKind>(k);
                break;
            }
        switch (f.kind) {
        case FilterKind::Multiplier:
            f.basis = LexFloat(row, "basis", 0.0f);
            f.multiplier = LexFloat(row, "multiplier", 0.0f);
            break;
        case FilterKind::DeltaMultiplier:
            f.multiplier = LexFloat(row, "multiplier", 0.0f);
            break;
        case FilterKind::LinearMap:
            f.inputMin = LexFloat(row, "input_min", 0.0f);
            f.inputMax = LexFloat(row, "input_max", 0.0f);
            f.outputMin = LexFloat(row, "output_min", 0.0f);
            f.outputMax = LexFloat(row, "output_max", 0.0f);
            break;
        case FilterKind::Cap:
            f.outputMin = LexFloat(row, "output_min", 0.0f);
            f.outputMax = LexFloat(row, "output_max", 0.0f);
            break;
        case FilterKind::None:
            throw NodeGraphDataError("control_filters.xtbl: filter '" + f.name + "' has unknown type '" + type +
                                     "' (Sec7.10: the slot is left unset and the next call crashes)");
        }
        f.sourceName = LexText(row, "source_name", "none");
        t.filters_.push_back(std::move(f));
    }
    // Pass 2 (0x004d12d0), row order.
    for (FilterDef& f : t.filters_) {
        const int16_t pid = registry.resolve(f.sourceName);
        if (pid >= 0) {
            f.sourceId = pid;
            f.sourceIsFilter = false;
            f.type = registry.record(pid)->type;
            continue;
        }
        const int32_t fid = t.lookup(f.sourceName);
        if (fid < 0)
            throw NodeGraphDataError("control_filters.xtbl: filter '" + f.name + "' source '" + f.sourceName +
                                     "' is neither a parameter nor a filter (Sec7.10: null dereference)");
        f.sourceId = fid;
        f.sourceIsFilter = true;
        f.type = t.filters_[static_cast<size_t>(fid)].type;  // still 0 if that row is resolved later
    }
    return t;
}

int32_t ControlFilterTable::lookup(std::string_view name) const {
    for (size_t i = 0; i < filters_.size(); ++i)
        if (NameEquals(name, filters_[i].name)) return static_cast<int32_t>(i);
    return -1;
}

const FilterDef* ControlFilterTable::filter(int32_t id) const {
    if (id < 0 || id >= static_cast<int32_t>(filters_.size())) return nullptr;
    return &filters_[static_cast<size_t>(id)];
}

// ---------------------------------------------------------------------------
// Sec7.6 expressions
// ---------------------------------------------------------------------------
std::string ConvertExpressionText(std::string_view raw) {
    std::string s(raw);
    for (char& c : s) {
        if (c == '[') c = '<';
        else if (c == ']') c = '>';
    }
    return s;
}

namespace {

struct RawToken {
    std::string text;
    size_t end = 0;
};

// One raw token starting at `pos` (spaces skipped first); empty text at the end.
RawToken nextRaw(std::string_view s, size_t pos) {
    while (pos < s.size() && s[pos] == ' ') ++pos;
    RawToken t;
    if (pos >= s.size()) {
        t.end = pos;
        return t;
    }
    if (s[pos] == '(' || s[pos] == ')') {
        t.text = std::string(1, s[pos]);
        t.end = pos + 1;
        return t;
    }
    const size_t start = pos;
    while (pos < s.size() && s[pos] != ' ' && s[pos] != '(' && s[pos] != ')') ++pos;
    t.text = std::string(s.substr(start, pos - start));
    t.end = pos;
    return t;
}

// The operator strings, codes 0..10, compared case-insensitively.
constexpr const char* kOperatorTokens[11] = {"(", ")", "==", "!=", ">", "<", ">=", "<=", "and", "or", "xor"};

TokenCode classify(const std::string& tok) {
    for (int32_t i = 0; i < 11; ++i)
        if (NameEquals(tok, kOperatorTokens[i])) return static_cast<TokenCode>(i);
    if (!tok.empty() && std::isdigit(static_cast<unsigned char>(tok[0]))) return TokenCode::Number;
    return TokenCode::Name;
}

}  // namespace

std::vector<ExprToken> TokenizeExpression(std::string_view s) {
    std::vector<ExprToken> out;
    size_t pos = 0;
    for (;;) {
        RawToken r = nextRaw(s, pos);
        if (r.text.empty()) break;
        pos = r.end;
        ExprToken tok{classify(r.text), r.text};
        if (tok.code == TokenCode::Name) {
            // Look one token ahead; while it is also a name, append it after a
            // single space.
            for (;;) {
                RawToken peek = nextRaw(s, pos);
                if (peek.text.empty() || classify(peek.text) != TokenCode::Name) break;
                tok.text += ' ';
                tok.text += peek.text;
                pos = peek.end;
            }
        }
        out.push_back(std::move(tok));
    }
    return out;
}

void BuildExpressionTree(Expression& expr) {
    expr.nodes.clear();
    expr.root = -1;
    expr.unmatchedOperandTokens.clear();
    auto newNode = [&](bool group) {
        ExprNode n;
        n.group = group;
        expr.nodes.push_back(n);
        return static_cast<int32_t>(expr.nodes.size() - 1);
    };
    auto node = [&](int32_t i) -> ExprNode& { return expr.nodes[static_cast<size_t>(i)]; };
    // Attach `n` below / beside `cur` per the previous token.
    auto attach = [&](int32_t cur, int32_t n, bool asFirstChild) {
        if (asFirstChild) {
            node(cur).firstChild = n;
            node(n).parent = cur;
        } else {
            node(cur).next = n;
            node(n).prev = cur;
        }
    };
    int32_t cur = -1;
    int32_t prevCode = -1;
    for (const ExprToken& tok : expr.tokens) {
        const bool afterOpen = prevCode == static_cast<int32_t>(TokenCode::Open);
        switch (tok.code) {
        case TokenCode::Open: {
            const int32_t n = newNode(true);
            if (cur == -1) expr.root = n;
            else attach(cur, n, afterOpen);
            cur = n;
            break;
        }
        case TokenCode::Close: {
            if (cur == -1) {
                ++expr.hostGuardNoCurrentNode;
                break;
            }
            if (node(cur).parent != -1) {
                cur = node(cur).parent;
                break;
            }
            int32_t m = cur;
            int32_t found = -1;
            while (node(m).prev != -1) {
                m = node(m).prev;
                if (node(m).parent != -1) {
                    found = node(m).parent;
                    break;
                }
            }
            cur = (found != -1) ? found : m;  // no parent anywhere: the chain's first node
            break;
        }
        case TokenCode::Eq:
        case TokenCode::Ne:
        case TokenCode::Gt:
        case TokenCode::Lt:
        case TokenCode::Ge:
        case TokenCode::Le: {
            Comparison c = Comparison::None;
            switch (tok.code) {
            case TokenCode::Eq: c = Comparison::Equal; break;
            case TokenCode::Ne: c = Comparison::NotEqual; break;
            case TokenCode::Gt: c = Comparison::Greater; break;
            case TokenCode::Ge: c = Comparison::GreaterOrEqual; break;
            case TokenCode::Lt: c = Comparison::Less; break;
            default: c = Comparison::LessOrEqual; break;
            }
            if (cur == -1) ++expr.hostGuardNoCurrentNode;
            else if (node(cur).group) ++expr.hostGuardComparisonOnGroup;  // +0x20 of a 0x1c-byte object: skipped
            else node(cur).comparison = c;
            break;
        }
        case TokenCode::And:
        case TokenCode::Or:
            if (cur == -1) ++expr.hostGuardNoCurrentNode;
            else node(cur).connective = (tok.code == TokenCode::And) ? 0 : 1;
            break;
        case TokenCode::Xor:
            break;  // recognised, sets nothing
        case TokenCode::Name:
        case TokenCode::Number: {
            int32_t idx = -1;
            for (size_t p = 0; p < expr.parameters.size(); ++p)
                if (NameEquals(tok.text, expr.parameters[p].sourceName)) {
                    idx = static_cast<int32_t>(p);
                    break;
                }
            if (idx == -1) expr.unmatchedOperandTokens.push_back(tok.text);
            if (cur == -1) {
                const int32_t n = newNode(false);
                expr.root = n;
                node(n).operand1 = idx;
                cur = n;
            } else if (node(cur).group) {
                const int32_t n = newNode(false);
                attach(cur, n, afterOpen);
                node(n).operand1 = idx;
                cur = n;
            } else if (node(cur).operand1 == -1) {
                node(cur).operand1 = idx;
            } else if (node(cur).operand2 == -1) {
                node(cur).operand2 = idx;
            } else {
                const int32_t n = newNode(false);
                attach(cur, n, false);
                node(n).operand1 = idx;
                cur = n;
            }
            break;
        }
        }
        prevCode = static_cast<int32_t>(tok.code);
    }
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
int32_t FindNodeIndex(const GraphDef& graph, std::string_view name) {
    if (graph.nodes.empty()) return 0;
    for (size_t i = 0; i < graph.nodes.size(); ++i)
        if (NameEquals(name, graph.nodes[i].name)) return static_cast<int32_t>(i);
    return -1;
}

const ClipBlock* ClipBlockOf(const NodeDef& node) {
    if (node.kind != NodeKind::State && node.kind != NodeKind::Clip) return nullptr;
    return node.clip ? &*node.clip : nullptr;
}

const GraphDef* NodeGraphLibrary::graph(int32_t index) const {
    if (index < 0 || index >= static_cast<int32_t>(graphs.size())) return nullptr;
    return &graphs[static_cast<size_t>(index)];
}

int32_t NodeGraphLibrary::findGraph(std::string_view name) const {
    if (graphs.empty()) return 0;
    for (size_t i = 0; i < graphs.size(); ++i)
        if (NameEquals(name, graphs[i].name)) return static_cast<int32_t>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------
namespace {

struct Ctx {
    const NodeGraphLoadInputs& in;
    const std::string& file;
};

[[noreturn]] void dataError(const Ctx& c, const std::string& what) {
    throw NodeGraphDataError(c.file + ": " + what);
}

// 0x004d0260 + 0x004d0430: the operand record. Both readers are applied at
// once; every registry they consult is loaded before the graphs (Sec7.2).
Operand readOperand(const Ctx& c, const Node* e, const char* where) {
    Operand op;
    const std::string type = LexText(e, "type", "none");
    for (int32_t k = 0; k < 14; ++k)
        if (NameEquals(type, kOperandTypeNames[k])) {
            op.type = static_cast<OperandType>(k);
            break;
        }
    const std::string* src = ChildText(e, "source_name");
    if (!src) dataError(c, std::string(where) + ": operand with no source_name (Sec7.7: the copy reads through null)");
    op.sourceName = *src;
    switch (op.type) {
    case OperandType::Boolean:
        if (NameEquals(op.sourceName, "true")) op.literal = 1;
        else op.literal = 0;  // `false` and any other text leave 0
        break;
    case OperandType::Integer:
        op.literal = static_cast<uint32_t>(static_cast<int32_t>(std::strtol(op.sourceName.c_str(), nullptr, 10)));  // atoi
        break;
    case OperandType::Float:
        op.literal = FloatBits(static_cast<float>(std::strtod(op.sourceName.c_str(), nullptr)));  // atof
        break;
    case OperandType::ControlParameter:
        op.id = c.in.registry->resolve(op.sourceName);
        break;
    case OperandType::ControlFilter:
        op.id = c.in.filters->lookup(op.sourceName);
        break;
    case OperandType::ClipTime:
    case OperandType::ClipTimeRemaining:
    case OperandType::ClipTimeAsPercent: {
        std::string s = op.sourceName;
        if (op.type != OperandType::ClipTime && !s.empty()) s.pop_back();  // the unit mark
        if (s == "?") op.ownClip = true;
        else op.id = c.in.stateActions->lookup(s);
        break;
    }
    case OperandType::Trigger:
        op.triggerIndex = c.in.triggers->lookup(op.sourceName);
        break;
    default:
        break;  // none / local / animation group / transition length / node active time: nothing at load
    }
    return op;
}

ClipBlock readClipBlock(const Ctx& c, const Node* n) {
    ClipBlock b;
    b.tagName = LexText(n, "tag_name", "");
    b.tagIndex = b.tagName.empty() ? -1 : c.in.stateActions->lookup(b.tagName);
    b.clipName = LexText(n, "clip_name", "");
    b.aliasName = LexText(n, "alias_name", "");
    if (!b.aliasName.empty()) {
        b.aliasCrc = sr3xtbl::NameHash(b.aliasName);
        b.aliasId = NamedClipNodeId(b.aliasName);
    }
    b.matchName = LexText(n, "match_name", "");
    if (!b.matchName.empty()) b.matchCrc = sr3xtbl::NameHash(b.matchName);
    b.loop = NameEquals(LexText(n, "loop_flag", ""), "true");  // Sec8.6 polarity
    b.startTime = LexFloat(n, "start_time", 0.0f);
    b.layer = LexInt(n, "layer", 4);
    return b;
}

Condition readCondition(const Ctx& c, const Node* e) {
    Condition cond;
    if (const Node* cp = FindChild(e, "control_parameter")) cond.local = ReadControlParameterRow(cp);
    const std::string cmp = LexText(e, "comparison", "none");
    for (int32_t k = 1; k <= 6; ++k)
        if (NameEquals(cmp, kComparisonWords[k])) {
            cond.comparison = static_cast<Comparison>(k);
            break;
        }
    cond.parameter1 = readOperand(c, FindChild(e, "parameter1"), "condition parameter1");
    cond.parameter2 = readOperand(c, FindChild(e, "parameter2"), "condition parameter2");
    return cond;
}

Transition readTransitionPass1(const Ctx& c, const Node* t) {
    Transition tr;
    tr.name = LexText(t, "name", "none");
    tr.toNodeName = LexText(t, "to_node", "none");
    tr.length = LexFloat(t, "length", 0.0f);
    tr.cancelableAfter = LexFloat(t, "cancelable_after", -1.0f);
    tr.cancelableBefore = LexFloat(t, "cancelable_before", -1.0f);
    tr.cyclicPercent = LexFloat(t, "cyclic_percent", -1.0f);
    tr.expression.text = ConvertExpressionText(LexText(t, "start", ""));
    for (const Node* cond : Children(FindChild(t, "conditions"), "condition")) tr.conditions.push_back(readCondition(c, cond));
    tr.orForm = LexBool(t, "or", false);
    tr.syncControllerTransform = FindChild(t, "sync_controller_transform") != nullptr;
    return tr;
}

void readChildGraphName(NodeDef& nd, const Node* n) {
    if (FindChild(n, "child_node_graph")) nd.childGraphName = LexText(n, "child_node_graph", "none");
}

// State machine pass 1 (0x004b90a0 / 0x004b9ae0 / 0x004b9b40 / 0x004b8930).
void parseStateMachinePass1(const Ctx& c, GraphDef& g, const Node* elem) {
    g.name = LexText(elem, "name", "none");
    for (const Node* n : Children(FindChild(elem, "nodes"), "node")) {
        NodeDef nd;
        nd.kind = NodeKind::State;
        nd.name = LexText(n, "name", "none");
        nd.clip = readClipBlock(c, n);
        readChildGraphName(nd, n);
        for (const Node* t : Children(FindChild(n, "transitions"), "transition"))
            nd.transitions.push_back(readTransitionPass1(c, t));
        g.nodes.push_back(std::move(nd));
    }
    g.entryNodeName = LexText(elem, "default_node", "none");
    g.entryNode = FindNodeIndex(g, g.entryNodeName);
}

// Blend tree pass 1 (0x004bb270, the header 0x004bb7b0 and the kind slots).
void parseBlendTreePass1(const Ctx& c, GraphDef& g, const Node* elem) {
    g.name = LexText(elem, "name", "none");
    for (const Node* n : Children(FindChild(elem, "nodes"), "node")) {
        NodeDef nd;
        nd.name = LexText(n, "name", "none");
        const std::string type = LexText(n, "type", "none");
        int32_t code = 0;
        for (int32_t k = 1; k <= 6; ++k)
            if (NameEquals(type, kBlendNodeTypeNames[k])) {
                code = k;
                break;
            }
        if (code == 0)
            dataError(c, "blend-tree node '" + nd.name + "' has unknown type '" + type +
                             "' (Sec7.8: no object is created and the parser calls through an unset slot)");
        nd.kind = static_cast<NodeKind>(code);
        const std::vector<const Node*> inputs = Children(FindChild(n, "inputs"), "input");
        for (const Node* in : inputs) {
            BlendInput bi;
            bi.name = LexText(in, "name", "none");
            nd.inputs.push_back(bi);
        }
        switch (nd.kind) {
        case NodeKind::Merge:
            nd.constant18 = 0, nd.constant1a = 8;
            break;
        case NodeKind::Switch:
            nd.constant18 = 2, nd.constant1a = 8;
            nd.parameter = readOperand(c, FindChild(n, "parameter"), "switch parameter");
            break;
        case NodeKind::Clip:
            nd.clip = readClipBlock(c, n);
            break;
        case NodeKind::MultiWayBlender: {
            nd.constant18 = 2, nd.constant1a = 8;
            nd.parameter = readOperand(c, FindChild(n, "parameter"), "multi way blender parameter");
            // 0x004bbf40: a 6th control point makes the parser return at once.
            for (size_t i = 0; i < inputs.size(); ++i) {
                const std::vector<const Node*> pts = Children(inputs[i], "control_point");
                if (pts.size() > 5) {
                    nd.inputsTruncated = true;
                    break;  // this input and every later one stay unfilled
                }
                BlendInput& bi = nd.inputs[i];
                bi.maxBlendRate = LexFloat(inputs[i], "max_blend_rate", 0.0f);
                bi.pointCount = static_cast<int32_t>(pts.size());
                for (size_t p = 0; p < pts.size(); ++p) {
                    bi.range[p] = LexFloat(pts[p], "range", 0.0f);
                    bi.value[p] = LexFloat(pts[p], "value", 0.0f);
                }
                bi.filled = true;
            }
            break;
        }
        case NodeKind::BlendTreeReference:
        case NodeKind::StateMachineReference:
            readChildGraphName(nd, n);
            break;
        case NodeKind::State:
            break;
        }
        g.nodes.push_back(std::move(nd));
    }
}

// Pass 3 (0x004b91f0 / 0x004bb510): everything that names another node or graph.
void parsePass3(const Ctx& c, const NodeGraphLibrary& lib, GraphDef& g, const Node* elem) {
    const std::vector<const Node*> xmlNodes = Children(FindChild(elem, "nodes"), "node");
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        NodeDef& nd = g.nodes[i];
        const Node* xn = i < xmlNodes.size() ? xmlNodes[i] : nullptr;
        if (nd.childGraphName) nd.childGraph = lib.findGraph(*nd.childGraphName);
        if (g.kind == GraphKind::StateMachine) {
            const std::vector<const Node*> xts = Children(FindChild(xn, "transitions"), "transition");
            for (size_t k = 0; k < nd.transitions.size(); ++k) {
                Transition& tr = nd.transitions[k];
                tr.toNode = FindNodeIndex(g, tr.toNodeName);
                if (tr.expression.present()) {
                    const Node* xt = k < xts.size() ? xts[k] : nullptr;
                    for (const Node* p : Children(xt, "parameter"))
                        tr.expression.parameters.push_back(readOperand(c, p, "transition parameter"));
                    tr.expression.tokens = TokenizeExpression(tr.expression.text);
                    BuildExpressionTree(tr.expression);
                }
            }
        } else {
            for (BlendInput& bi : nd.inputs) bi.node = FindNodeIndex(g, bi.name);
        }
    }
    if (g.kind == GraphKind::BlendTree) {
        g.entryNodeName = LexText(elem, "output_node", "none");
        g.entryNode = FindNodeIndex(g, g.entryNodeName);
    }
    g.linked = true;
}

// Pass 4 (0x004cb200 / 0x004cb140 / 0x004cb170).
struct AliasCollector {
    const NodeGraphLibrary& lib;
    const ControlFilterTable& filters;
    std::vector<int16_t> aliases;
    std::vector<int16_t> filterIds;
    std::vector<int32_t> stack;

    static constexpr size_t kBuffer = 256;  // two 256-entry word buffers, no bound check in the engine

    void addAlias(int32_t id) {
        for (int16_t a : aliases)
            if (a == id) return;
        if (aliases.size() >= kBuffer)
            throw NodeGraphDataError("alias table exceeds the 256-entry buffer (Sec7.2 pass 4: no bound check)");
        aliases.push_back(static_cast<int16_t>(id));
    }
    // CHOSEN chain order: the filter itself, then its source, then the
    // source's source (Sec7.9 lists "each filter in the chain" without an
    // order; only the per-frame evaluation order depends on it, Sec8.8).
    void addFilter(int32_t id) {
        std::set<int32_t> seen;
        while (true) {
            if (!seen.insert(id).second) throw NodeGraphDataError("control filter source chain is cyclic");
            bool dup = false;
            for (int16_t f : filterIds)
                if (f == id) dup = true;
            if (!dup) {
                if (filterIds.size() >= kBuffer)
                    throw NodeGraphDataError("filter list exceeds the 256-entry buffer (Sec7.2 pass 4: no bound check)");
                filterIds.push_back(static_cast<int16_t>(id));
            }
            const FilterDef* f = filters.filter(id);
            if (!f) throw NodeGraphDataError("control filter id " + std::to_string(id) + " out of range");
            if (f->sourceIsFilter) {
                id = f->sourceId;
                continue;
            }
            addAlias(f->sourceId);
            return;
        }
    }
    void addOperand(const Operand& op) {
        if (op.type == OperandType::ControlParameter) addAlias(op.id);
        else if (op.type == OperandType::ControlFilter) {
            // An unresolved filter (-1) would be followed through a null
            // definition; shipped data has none (Sec7.12).
            if (op.id < 0) throw NodeGraphDataError("control filter operand '" + op.sourceName + "' did not resolve");
            addFilter(op.id);
        }
    }
    // CHOSEN walk order within a node: its own operands, its transitions in
    // order (expression parameters, then each condition's parameter1 and
    // parameter2), then its nested graph. The order fixes only the local
    // alias numbering, never a resolved value.
    void walk(int32_t graphIndex) {
        const GraphDef* g = lib.graph(graphIndex);
        if (!g) throw NodeGraphDataError("child_node_graph index " + std::to_string(graphIndex) + " out of range (Sec7.9)");
        for (int32_t s : stack)
            if (s == graphIndex)
                throw NodeGraphDataError("child_node_graph cycle through '" + g->name + "' (Sec7.9: endless recursion)");
        stack.push_back(graphIndex);
        for (const NodeDef& n : g->nodes) {
            if ((n.kind == NodeKind::Switch || n.kind == NodeKind::MultiWayBlender) && n.parameter) addOperand(*n.parameter);
            for (const Transition& t : n.transitions) {
                for (const Operand& p : t.expression.parameters) addOperand(p);
                for (const Condition& cd : t.conditions) {
                    addOperand(cd.parameter1);
                    addOperand(cd.parameter2);
                }
            }
            if (n.childGraph != -1) walk(n.childGraph);
        }
        stack.pop_back();
    }
};

}  // namespace

NodeGraphLibrary LoadNodeGraphs(const sr3xtbl::Document& nodeGraphFiles, const NodeGraphLoadInputs& in) {
    if (!in.registry || !in.filters || !in.stateActions || !in.triggers || !in.openFile)
        throw std::invalid_argument("LoadNodeGraphs: every input is required");
    NodeGraphLibrary lib;

    // Pass 1: names.
    for (const Node* row : Children(FindChild(nodeGraphFiles.root(), "node_graph_files"), "node_graph_file")) {
        const std::string* name = ChildText(row, "name");
        if (!name) throw NodeGraphDataError("node_graph_files.xtbl: a row has no name text (CHOSEN refusal)");
        if (name->size() > 250)
            throw NodeGraphDataError("node_graph_files.xtbl: name '" + *name +
                                     "' is longer than 250 characters (Sec7.2: runs into the next 256-byte slot)");
        lib.rowNames.push_back(*name);
    }
    if (lib.rowNames.size() > 383)
        throw NodeGraphDataError("node_graph_files.xtbl: more than 383 rows (Sec7.2: the 384th overwrites the stack cookie)");

    // Pass 2: headers.
    for (size_t row = 0; row < lib.rowNames.size(); ++row) {
        const std::string fileName = lib.rowNames[row] + ".xtbl";
        const std::optional<std::string> bytes = in.openFile(fileName);
        if (!bytes) {
            lib.aborted = true;
            lib.abortedPass = 2;
            lib.abortedFile = fileName;
            return lib;
        }
        const sr3xtbl::Document doc = sr3xtbl::ParseDocument(*bytes);
        GraphDef g;
        const Node* elem = FindChild(doc.root(), "state_machine");
        g.kind = GraphKind::StateMachine;
        if (!elem) {
            elem = FindChild(doc.root(), "blend_tree");
            g.kind = GraphKind::BlendTree;
        }
        if (!LexBool(elem, "validated", false)) {
            lib.skippedNotValidated.push_back(static_cast<int32_t>(row));
            continue;
        }
        g.fileName = fileName;
        g.fileNameCrc = sr3xtbl::NameHash(fileName);
        const Ctx c{in, fileName};
        if (g.kind == GraphKind::StateMachine) parseStateMachinePass1(c, g, elem);
        else parseBlendTreePass1(c, g, elem);
        lib.graphs.push_back(std::move(g));
        lib.graphRow.push_back(static_cast<int32_t>(row));
    }

    // Pass 3: links, against the reopened file.
    for (GraphDef& g : lib.graphs) {
        const std::optional<std::string> bytes = in.openFile(g.fileName);
        if (!bytes) {
            lib.aborted = true;
            lib.abortedPass = 3;
            lib.abortedFile = g.fileName;
            return lib;
        }
        const sr3xtbl::Document doc = sr3xtbl::ParseDocument(*bytes);
        const Node* elem = FindChild(doc.root(), g.kind == GraphKind::StateMachine ? "state_machine" : "blend_tree");
        const Ctx c{in, g.fileName};
        parsePass3(c, lib, g, elem);
    }

    // Pass 4: aliases and filters.
    for (size_t i = 0; i < lib.graphs.size(); ++i) {
        AliasCollector col{lib, *in.filters, {}, {}, {}};
        col.walk(static_cast<int32_t>(i));
        lib.graphs[i].aliases.present = true;
        lib.graphs[i].aliases.globalIds = col.aliases;
        lib.graphs[i].filterIds = col.filterIds;
    }
    return lib;
}

}  // namespace sr3animruntime
