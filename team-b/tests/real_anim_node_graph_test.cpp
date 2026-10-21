// Real-data population test for sr3animruntime U2 (spec-anim-runtime.md Sec7):
// loads every graph file node_graph_files.xtbl lists out of misc_tables.vpp_pc
// (and the patched free_fall_bt.xtbl out of patch_compressed.vpp_pc) from
// CRREISH_GAME_CACHE_DIR - exit 77 (ctest SKIPPED, never a pass) when the
// archives are absent; nothing from them is copied into the repository - runs
// LoadNodeGraphs over them and checks Sec7.12's validation claims and the
// Sec7.1 / Sec7.4-Sec7.8 / Sec7.12 census numbers. Per-file pass/fail counts
// are printed ("N/249 files pass").
//
// The State/Action registry is built from anim_states.xtbl + anim_actions.xtbl
// only: the 1,318 compiled-in preset names are OPEN (spec-tables-animation.md
// Sec5.3), so the test asserts that EXACTLY Sec7.12's nine shipped tag_names
// fail to resolve against the file-sourced names - a name resolvable only as a
// preset would show up as an extra failure and fail the test.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "sr3animruntime/controller.h"
#include "sr3animruntime/node_graph.h"
#include "sr3animruntime/node_graph_runtime.h"
#include "sr3tables_animation/tables.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

using namespace sr3animruntime;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)
#define CHECK_EQ(a, b)                                                                                         \
    do {                                                                                                       \
        const auto va_ = (a);                                                                                  \
        const auto vb_ = (b);                                                                                  \
        if (!(va_ == vb_)) {                                                                                   \
            std::cerr << "CHECK_EQ FAILED: " #a " == " #b " (" << va_ << " vs " << vb_ << ") at line " << __LINE__ \
                      << "\n";                                                                                 \
            ++g_failures;                                                                                      \
        }                                                                                                      \
    } while (0)

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Archive {
    // Through the persistent decoded-asset cache (2026-10-08 task):
    // `opened` keeps the mapped original archive bytes alive (what `c`'s
    // ByteView aliases); `c` is a COPY of opened->container (Container is
    // cheaply copyable - a few vectors plus a non-owning cache_ pointer
    // and a sourceKey_ string, see vpp/container.h), kept as its own
    // unique_ptr<vpp::Container> purely so every existing `archive.c->...`
    // call site elsewhere in this file (entries()/decompressEntry()/
    // rawEntryBytes()) keeps compiling unchanged. The copy still shares
    // the same cache_/sourceKey_, so decompressEntry() on it still hits
    // the disk cache exactly like the original.
    std::shared_ptr<vppcache::OpenedArchive> opened;
    std::unique_ptr<vpp::Container> c;
    std::map<std::string, size_t> byName;  // lower-cased
    bool load(const fs::path& p) {
        if (!fs::exists(p)) return false;
        opened = vppcache::openArchive(p);
        c = std::make_unique<vpp::Container>(opened->container);
        for (size_t i = 0; i < c->entries().size(); ++i) byName[lower(c->entries()[i].name)] = i;
        return true;
    }
    std::optional<std::string> read(const std::string& name) const {
        auto it = byName.find(lower(name));
        if (it == byName.end()) return std::nullopt;
        const size_t i = it->second;
        if (c->entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            vpp::ByteView r = c->rawEntryBytes(i);
            return std::string(reinterpret_cast<const char*>(r.data()), r.size());
        }
        vpp::DecompressResult r = c->decompressEntry(i);
        if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
            r.status != vpp::DecodeStatus::ContentValidated)
            return std::nullopt;
        return std::string(reinterpret_cast<const char*>(r.data.data()), r.data.size());
    }
};

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && lower(s.substr(s.size() - suf.size())) == suf;
}

// Every operand of a graph, with where it came from.
struct OperandRef {
    const Operand* op;
    enum Where { ExprParam, Cond1, Cond2, NodeParam } where;
};
void forEachOperand(const GraphDef& g, const std::function<void(const OperandRef&)>& f) {
    for (const NodeDef& n : g.nodes) {
        if (n.parameter) f({&*n.parameter, OperandRef::NodeParam});
        for (const Transition& t : n.transitions) {
            for (const Operand& p : t.expression.parameters) f({&p, OperandRef::ExprParam});
            for (const Condition& c : t.conditions) {
                f({&c.parameter1, OperandRef::Cond1});
                f({&c.parameter2, OperandRef::Cond2});
            }
        }
    }
}

// Multi-way-blend inputs whose control points are not in ascending range order.
int countOutOfOrderInputs(const NodeGraphLibrary& lib) {
    int n = 0;
    for (const GraphDef& g : lib.graphs)
        for (const NodeDef& nd : g.nodes)
            if (nd.kind == NodeKind::MultiWayBlender)
                for (const BlendInput& bi : nd.inputs)
                    for (int p = 0; p + 1 < bi.pointCount; ++p)
                        if (bi.range[p + 1] < bi.range[p]) {
                            ++n;
                            break;
                        }
    return n;
}

// Conventional-precedence evaluation (and binds tighter than or) of the same
// tree, for the Sec7.12 / Sec8.10 fold-divergence census. A chain is split at
// `or` connectives into and-runs.
bool precedenceChain(const Expression& e, int32_t first, const std::vector<bool>& cmpValue);
bool precedenceNode(const Expression& e, int32_t i, const std::vector<bool>& cmpValue) {
    const ExprNode& n = e.nodes[static_cast<size_t>(i)];
    return n.group ? precedenceChain(e, n.firstChild, cmpValue) : cmpValue[static_cast<size_t>(i)];
}
bool precedenceChain(const Expression& e, int32_t first, const std::vector<bool>& cmpValue) {
    bool orAcc = false, andAcc = true;
    for (int32_t i = first; i != -1; i = e.nodes[static_cast<size_t>(i)].next) {
        andAcc = andAcc && precedenceNode(e, i, cmpValue);
        const int32_t conn = e.nodes[static_cast<size_t>(i)].connective;
        if (conn == 1 || e.nodes[static_cast<size_t>(i)].next == -1) {
            orAcc = orAcc || andAcc;
            andAcc = true;
        }
    }
    return orAcc;
}

}  // namespace

int main() {
    const fs::path cache(CRREISH_GAME_CACHE_DIR);
    Archive misc, patch;
    if (!misc.load(cache / "misc_tables.vpp_pc") || !patch.load(cache / "patch_compressed.vpp_pc")) {
        std::cout << "SKIP: misc_tables.vpp_pc / patch_compressed.vpp_pc not present in " << cache.string() << "\n";
        return 77;
    }
    auto docOf = [&](const Archive& a, const std::string& name) {
        std::optional<std::string> b = a.read(name);
        if (!b) {
            std::cerr << name << " not found\n";
            std::exit(1);
        }
        return sr3xtbl::ParseDocument(*b);
    };

    // ---- registries --------------------------------------------------------
    const sr3xtbl::Document paramsDoc = docOf(misc, "control_parameters.xtbl");
    const ControlParameterRegistry registry = ControlParameterRegistry::load(paramsDoc);
    const sr3xtbl::Document filtersDoc = docOf(misc, "control_filters.xtbl");
    const ControlFilterTable filters = ControlFilterTable::load(filtersDoc, registry);
    std::vector<std::string> states, actions, triggerNames;
    {
        const sr3xtbl::Document d1 = docOf(misc, "anim_states.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimStatesTable(d1))
            if (s.name) states.push_back(*s.name);
        const sr3xtbl::Document d2 = docOf(misc, "anim_actions.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimActionsTable(d2))
            if (s.name) actions.push_back(*s.name);
        const sr3xtbl::Document d3 = docOf(misc, "anim_triggers.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimTriggersTable(d3))
            if (s.name) triggerNames.push_back(*s.name);
    }
    const StateActionRegistry stateActions = StateActionRegistry::build({}, states, actions);
    const TriggerRegistry triggers = TriggerRegistry::build(triggerNames);
    CHECK_EQ(registry.count(), 87);
    CHECK_EQ(triggers.count(), 105);

    // ---- Sec7.10 filters ---------------------------------------------------
    CHECK_EQ(filters.filters().size(), static_cast<size_t>(22));
    {
        int nCap = 0, nLin = 0, nDelta = 0, nMul = 0;
        for (size_t i = 0; i < filters.filters().size(); ++i) {
            const FilterDef& f = filters.filters()[i];
            nCap += f.kind == FilterKind::Cap;
            nLin += f.kind == FilterKind::LinearMap;
            nDelta += f.kind == FilterKind::DeltaMultiplier;
            nMul += f.kind == FilterKind::Multiplier;
            CHECK(f.sourceId >= 0);
            if (f.sourceIsFilter) CHECK(f.sourceId < static_cast<int32_t>(i));  // none sources a later row
            CHECK(f.type == ParamType::Float);  // Sec8.10: every filter source is float
        }
        CHECK_EQ(nCap, 9);
        CHECK_EQ(nLin, 7);
        CHECK_EQ(nDelta, 6);
        CHECK_EQ(nMul, 0);
    }

    // ---- Sec7.1 population ---------------------------------------------------
    const sr3xtbl::Document listDoc = docOf(misc, "node_graph_files.xtbl");
    int opens = 0;
    NodeGraphLoadInputs in;
    in.registry = &registry;
    in.filters = &filters;
    in.stateActions = &stateActions;
    in.triggers = &triggers;
    in.openFile = [&](const std::string& f) {
        ++opens;
        return misc.read(f);
    };
    const NodeGraphLibrary lib = LoadNodeGraphs(listDoc, in);
    CHECK(!lib.aborted);
    CHECK_EQ(lib.rowNames.size(), static_cast<size_t>(249));
    CHECK_EQ(lib.graphs.size(), static_cast<size_t>(249));
    CHECK(lib.skippedNotValidated.empty());
    CHECK_EQ(opens, 2 * 249);  // pass 2 opens every row, pass 3 reopens every accepted graph
    {
        std::set<std::string> distinct;
        for (const std::string& n : lib.rowNames) distinct.insert(lower(n));
        CHECK_EQ(distinct.size(), static_cast<size_t>(249));
        std::set<std::string> graphNames;
        for (const GraphDef& g : lib.graphs) graphNames.insert(lower(g.name));
        CHECK_EQ(graphNames.size(), static_cast<size_t>(249));
    }

    // Per-file pass/fail: every Sec7.12 check that is local to one file.
    int filesPass = 0;
    std::vector<std::string> failing;
    int nSm = 0, nBt = 0, btSuffixBt = 0, smSuffixSm = 0, noSuffixSm = 0, noSuffixBt = 0, wrongSuffix = 0;
    int childRefs = 0, caseOnlyParams = 0, maxAlias = 0, maxFilters = 0;
    std::string maxAliasGraph;
    std::set<int16_t> reachableFilters;
    std::map<std::string, int> unresolvedTags;  // graph -> count
    std::set<std::string> unresolvedTagNames;
    for (size_t gi = 0; gi < lib.graphs.size(); ++gi) {
        const GraphDef& g = lib.graphs[gi];
        bool ok = true;
        auto fail = [&](const std::string& why) {
            if (ok) failing.push_back(g.fileName + ": " + why);
            ok = false;
        };
        const std::optional<std::string> bytes = misc.read(g.fileName);
        const sr3xtbl::Document doc = sr3xtbl::ParseDocument(*bytes);
        // Shape (Sec7.1 / Sec7.4): <root> holds exactly one graph element with
        // exactly the four header elements once each; no attributes; no
        // element mixes text with children.
        if (doc.root()->children().size() != 1) fail("root does not hold exactly one element");
        const sr3xtbl::Node* elem = doc.root()->children().empty() ? nullptr : doc.root()->children()[0];
        const bool isSm = g.kind == GraphKind::StateMachine;
        if (!elem || !sr3xtbl::NameEquals(elem->name(), isSm ? "state_machine" : "blend_tree")) fail("root element");
        if (elem && elem->children().size() != 4) fail("graph element does not have exactly four children");
        for (const char* h : {"validated", "name", isSm ? "default_node" : "output_node", "nodes"})
            if (sr3xtbl::CountChildren(elem, h) != 1) fail(std::string("header element ") + h);
        if (doc.mixedContentElements() != 0) fail("mixed content");
        if (doc.warningCount() != 0) fail("xml recoveries");
        std::function<bool(const sr3xtbl::Node*)> anyAttr = [&](const sr3xtbl::Node* n) {
            if (!n->attributes().empty()) return true;
            for (const sr3xtbl::Node* ch : n->children())
                if (anyAttr(ch)) return true;
            return false;
        };
        if (anyAttr(doc.root())) fail("attributes");
        if (!sr3xtbl::NameEquals(LexText(elem, "validated", ""), "true")) fail("validated text");

        (isSm ? nSm : nBt)++;
        const std::string row = lib.rowNames[static_cast<size_t>(lib.graphRow[gi])];
        if (endsWith(row, "_bt")) (isSm ? wrongSuffix : btSuffixBt)++;
        else if (endsWith(row, "_sm")) (isSm ? smSuffixSm : wrongSuffix)++;
        else (isSm ? noSuffixSm : noSuffixBt)++;
        if (isSm && (g.nodes.size() < 1 || g.nodes.size() > 18)) fail("state machine node count");
        if (!isSm && (g.nodes.size() < 1 || g.nodes.size() > 39)) fail("blend tree node count");

        // Node references resolve, by name (exact up to case).
        auto nodeOk = [&](int32_t idx, const std::string& name) {
            return idx >= 0 && idx < static_cast<int32_t>(g.nodes.size()) &&
                   sr3xtbl::NameEquals(g.nodes[static_cast<size_t>(idx)].name, name);
        };
        if (!nodeOk(g.entryNode, g.entryNodeName)) fail("default/output node");
        std::set<std::string> nodeNames;
        for (const NodeDef& n : g.nodes) {
            if (!nodeNames.insert(lower(n.name)).second) fail("duplicate node name " + n.name);
            for (const Transition& t : n.transitions)
                if (!nodeOk(t.toNode, t.toNodeName)) fail("to_node " + t.toNodeName);
            for (const BlendInput& bi : n.inputs)
                if (!nodeOk(bi.node, bi.name)) fail("input " + bi.name);
            if (n.inputsTruncated) fail("a 6th control point");
            if (n.childGraphName) {
                ++childRefs;
                const GraphDef* child = lib.graph(n.childGraph);
                if (!child || !sr3xtbl::NameEquals(child->name, *n.childGraphName)) fail("child graph " + *n.childGraphName);
                else if (n.kind == NodeKind::BlendTreeReference && child->kind != GraphKind::BlendTree) fail("reference kind");
                else if (n.kind == NodeKind::StateMachineReference && child->kind != GraphKind::StateMachine)
                    fail("reference kind");
            }
            if (const ClipBlock* cb = ClipBlockOf(n)) {
                if (!cb->tagName.empty() && cb->tagIndex < 0) {
                    ++unresolvedTags[g.name];
                    unresolvedTagNames.insert(cb->tagName);
                }
            }
            for (const Transition& t : n.transitions) {
                const Expression& e = t.expression;
                if (!e.present()) continue;
                if (!e.unmatchedOperandTokens.empty()) fail("start token '" + e.unmatchedOperandTokens[0] + "' matches no parameter");
                if (e.hostGuardComparisonOnGroup || e.hostGuardNoCurrentNode) fail("expression builder guard fired");
                if (e.root < 0) fail("empty expression tree");
                std::vector<bool> used(e.parameters.size(), false);
                for (const ExprNode& en : e.nodes) {
                    if (en.group) {
                        if (en.firstChild < 0) fail("empty group");
                        continue;
                    }
                    if (en.operand1 < 0 || en.operand2 < 0 || en.comparison == Comparison::None) fail("incomplete comparison");
                    if (en.operand1 >= 0) used[static_cast<size_t>(en.operand1)] = true;
                    if (en.operand2 >= 0) used[static_cast<size_t>(en.operand2)] = true;
                }
                for (bool u : used)
                    if (!u) fail("unused parameter in '" + t.name + "'");
            }
        }
        // Operands.
        forEachOperand(g, [&](const OperandRef& r) {
            const Operand& op = *r.op;
            if (op.type == OperandType::None) fail("operand type none/unknown");
            switch (op.type) {
            case OperandType::ControlParameter: {
                if (op.id < 0) fail("control parameter '" + op.sourceName + "'");
                else if (registry.record(op.id)->name != op.sourceName) ++caseOnlyParams;
                break;
            }
            case OperandType::ControlFilter:
                if (op.id < 0) fail("control filter '" + op.sourceName + "'");
                break;
            case OperandType::Trigger:
                if (op.triggerIndex < 0) fail("trigger '" + op.sourceName + "'");
                break;
            case OperandType::ClipTime:
            case OperandType::ClipTimeRemaining:
            case OperandType::ClipTimeAsPercent:
                if (!op.ownClip && op.id < 0) fail("clip-time name '" + op.sourceName + "'");
                break;
            default:
                break;
            }
        });
        if (static_cast<int>(g.aliases.globalIds.size()) > maxAlias) {
            maxAlias = static_cast<int>(g.aliases.globalIds.size());
            maxAliasGraph = g.name;
        }
        maxFilters = std::max(maxFilters, static_cast<int>(g.filterIds.size()));
        for (int16_t f : g.filterIds) reachableFilters.insert(f);
        if (ok) ++filesPass;
    }
    std::cout << "U2 population: " << filesPass << "/" << lib.graphs.size() << " files pass every per-file check\n";
    for (const std::string& f : failing) std::cout << "  FAIL " << f << "\n";
    CHECK_EQ(filesPass, 249);
    CHECK_EQ(nSm, 122);
    CHECK_EQ(nBt, 127);
    CHECK_EQ(btSuffixBt, 103);
    CHECK_EQ(smSuffixSm, 88);
    CHECK_EQ(noSuffixSm, 34);
    CHECK_EQ(noSuffixBt, 24);
    CHECK_EQ(wrongSuffix, 0);
    CHECK_EQ(childRefs, 155);
    CHECK_EQ(caseOnlyParams, 16);
    CHECK_EQ(maxAlias, 10);
    CHECK(lower(maxAliasGraph) == "driving sm" || lower(maxAliasGraph) == "driving_sm");
    CHECK_EQ(maxFilters, 2);
    std::cout << "  largest alias table: " << maxAlias << " (" << maxAliasGraph << "), largest filter list: " << maxFilters
              << ", filters reachable from some graph: " << reachableFilters.size() << "/22\n";
    CHECK_EQ(reachableFilters.size(), static_cast<size_t>(13));

    // ---- Sec7.12: the nine unresolved tag_names, and only they -------------
    {
        int total = 0;
        for (const auto& kv : unresolvedTags) total += kv.second;
        std::cout << "  unresolved tag_names: " << total << " in " << unresolvedTags.size() << " graphs\n";
        for (const auto& kv : unresolvedTags) std::cout << "    " << kv.first << ": " << kv.second << "\n";
        CHECK_EQ(total, 7);  // D-16: the document parser trims text, so the two trailing-space names of cower_test resolve (spec-anim-runtime 7.12 said nine)
        std::map<std::string, int> byFile;
        for (size_t gi = 0; gi < lib.graphs.size(); ++gi) {
            auto it = unresolvedTags.find(lib.graphs[gi].name);
            if (it != unresolvedTags.end()) byFile[lib.rowNames[static_cast<size_t>(lib.graphRow[gi])]] = it->second;
        }
        CHECK((byFile == std::map<std::string, int>{{"spinslower_sm", 1}, {"tiger_ride_sm", 6}}));
        CHECK((unresolvedTagNames == std::set<std::string>{"SpinSlower", "tiger calm",
                                                           "tiger stumble left", "tiger stumble right"}));
    }

    // ---- Sec7.5 / Sec7.6 / Sec7.7 / Sec7.12 census --------------------------
    {
        int transitions = 0, smWithTransitions = 0, withStart = 0, withConditions = 0, both = 0, neither = 0;
        int bothClimb = 0, bothCrouch = 0, syncCount = 0, orCount = 0, cAfter = 0, cBefore = 0, cyclic = 0, zeroLen = 0;
        int maxPerNode = 0, minPerNodeWithAny = 1000, stateNodesWithout = 0;
        std::set<std::string> startFiles, condFiles;
        int names = 0, numbers = 0, openParens = 0, closes = 0, ands = 0, ors = 0, xors = 0;
        std::map<Comparison, int> exprCmp, condCmp;
        int single = 0, maxCmp = 0, withParens = 0, unbalancedClimb = 0;
        int conditions = 0, localConds = 0, localFloat = 0, localBool = 0, localInt = 0, twoParam = 0;
        std::map<OperandType, int> p1Types;
        std::set<std::string> twoParamNames;
        int ctRemaining = 0, ctRemainingQAt = 0, ctPercent = 0, ctPercentQ = 0, ctPercentNamed = 0, ctTime = 0, ctTimeQ = 0;
        for (const GraphDef& g : lib.graphs) {
            const std::string row = lib.rowNames[static_cast<size_t>(&g - &lib.graphs[0])];
            bool any = false;
            for (const NodeDef& n : g.nodes) {
                if (g.kind == GraphKind::StateMachine) {
                    if (n.transitions.empty()) ++stateNodesWithout;
                    else minPerNodeWithAny = std::min(minPerNodeWithAny, static_cast<int>(n.transitions.size()));
                    maxPerNode = std::max(maxPerNode, static_cast<int>(n.transitions.size()));
                }
                for (const Transition& t : n.transitions) {
                    any = true;
                    ++transitions;
                    const bool s = t.expression.present(), c = !t.conditions.empty();
                    if (s) {
                        ++withStart;
                        startFiles.insert(row);
                    }
                    if (c) {
                        ++withConditions;
                        condFiles.insert(row);
                    }
                    if (s && c) {
                        ++both;
                        if (row == "climb_fence_sm") ++bothClimb;
                        if (row == "crouch_aim_movement") ++bothCrouch;
                    }
                    if (!s && !c) ++neither;
                    syncCount += t.syncControllerTransform;
                    orCount += t.orForm;
                    cAfter += t.cancelableAfter != -1.0f;
                    cBefore += t.cancelableBefore != -1.0f;
                    cyclic += t.cyclicPercent != -1.0f;
                    zeroLen += t.length == 0.0f;
                    if (s) {
                        int cmpN = 0;
                        bool parens = false;
                        int o = 0, cl = 0;
                        for (const ExprToken& tk : t.expression.tokens) {
                            switch (tk.code) {
                            case TokenCode::Name: ++names; break;
                            case TokenCode::Number: ++numbers; break;
                            case TokenCode::Open: ++openParens, ++o, parens = true; break;
                            case TokenCode::Close: ++closes, ++cl; break;
                            case TokenCode::And: ++ands; break;
                            case TokenCode::Or: ++ors; break;
                            case TokenCode::Xor: ++xors; break;
                            default: break;
                            }
                        }
                        for (const ExprNode& en : t.expression.nodes)
                            if (!en.group) {
                                ++cmpN;
                                ++exprCmp[en.comparison];
                            }
                        if (cmpN == 1) ++single;
                        maxCmp = std::max(maxCmp, cmpN);
                        withParens += parens;
                        if (row == "climb_fence_sm" && o == cl + 1) ++unbalancedClimb;
                    }
                    for (const Condition& cd : t.conditions) {
                        ++conditions;
                        ++condCmp[cd.comparison];
                        ++p1Types[cd.parameter1.type];
                        if (cd.local && cd.local->name == "local" && cd.parameter2.type == OperandType::LocalControlParameter &&
                            cd.parameter2.sourceName == "local") {
                            ++localConds;
                            localFloat += cd.local->type == ParamType::Float;
                            localBool += cd.local->type == ParamType::Bool;
                            localInt += cd.local->type == ParamType::Integer;
                        }
                        if (cd.parameter2.type == OperandType::ControlParameter) {
                            ++twoParam;
                            twoParamNames.insert(cd.parameter2.sourceName);
                        }
                    }
                }
            }
            smWithTransitions += any;
            forEachOperand(g, [&](const OperandRef& r) {
                const Operand& op = *r.op;
                if (op.type == OperandType::ClipTimeRemaining) {
                    ++ctRemaining;
                    ctRemainingQAt += op.sourceName == "?@";
                } else if (op.type == OperandType::ClipTimeAsPercent) {
                    ++ctPercent;
                    if (op.sourceName == "?%") ++ctPercentQ;
                    else if (!op.sourceName.empty() && op.sourceName.back() == '%' && op.id >= 0) ++ctPercentNamed;
                } else if (op.type == OperandType::ClipTime) {
                    ++ctTime;
                    ctTimeQ += op.sourceName == "?";
                }
            });
        }
        CHECK_EQ(transitions, 530);
        CHECK_EQ(smWithTransitions, 97);
        CHECK_EQ(withStart, 304);
        CHECK_EQ(startFiles.size(), static_cast<size_t>(61));
        CHECK_EQ(withConditions, 171);
        CHECK_EQ(condFiles.size(), static_cast<size_t>(38));
        CHECK_EQ(both, 7);
        CHECK_EQ(bothClimb, 5);
        CHECK_EQ(bothCrouch, 2);
        CHECK_EQ(neither, 62);
        CHECK_EQ(syncCount, 226);
        CHECK_EQ(orCount, 8);
        CHECK_EQ(cAfter, 7);
        CHECK_EQ(cBefore, 5);
        CHECK_EQ(cyclic, 6);
        CHECK_EQ(zeroLen, 54);
        CHECK_EQ(maxPerNode, 7);
        CHECK_EQ(minPerNodeWithAny, 1);
        CHECK_EQ(stateNodesWithout, 94);
        CHECK_EQ(names, 843);
        CHECK_EQ(numbers, 85);
        CHECK_EQ(exprCmp[Comparison::Equal], 260);
        CHECK_EQ(exprCmp[Comparison::LessOrEqual], 109);
        CHECK_EQ(exprCmp[Comparison::Greater], 40);
        CHECK_EQ(exprCmp[Comparison::Less], 35);
        CHECK_EQ(exprCmp[Comparison::GreaterOrEqual], 20);
        CHECK_EQ(exprCmp[Comparison::NotEqual], 0);
        CHECK_EQ(ands, 126);
        CHECK_EQ(ors, 34);
        CHECK_EQ(xors, 0);
        CHECK_EQ(openParens, 89);
        CHECK_EQ(closes, 83);
        CHECK_EQ(single, 209);
        CHECK_EQ(maxCmp, 7);
        CHECK_EQ(withParens, 33);
        CHECK_EQ(unbalancedClimb, 6);
        CHECK_EQ(conditions, 249);
        CHECK_EQ(localConds, 243);
        CHECK_EQ(localFloat, 138);
        CHECK_EQ(localBool, 97);
        CHECK_EQ(localInt, 8);
        CHECK_EQ(twoParam, 6);
        CHECK((twoParamNames == std::set<std::string>{"Vehicle Tilt Abs", "Vehicle Vert Lean Abs", "Vehicle Forward Lean Abs"}));
        CHECK_EQ(p1Types[OperandType::ControlParameter], 180);
        CHECK_EQ(p1Types[OperandType::ControlFilter], 32);
        CHECK_EQ(p1Types[OperandType::ClipTimeAsPercent], 29);
        CHECK_EQ(p1Types[OperandType::Trigger], 7);
        CHECK_EQ(p1Types[OperandType::ClipTime], 1);
        CHECK_EQ(condCmp[Comparison::Equal], 105);
        CHECK_EQ(condCmp[Comparison::Greater], 62);
        CHECK_EQ(condCmp[Comparison::GreaterOrEqual], 37);
        CHECK_EQ(condCmp[Comparison::Less], 22);
        CHECK_EQ(condCmp[Comparison::LessOrEqual], 22);
        CHECK_EQ(condCmp[Comparison::NotEqual], 1);
        CHECK_EQ(ctRemaining, 90);
        CHECK_EQ(ctRemainingQAt, 90);
        CHECK_EQ(ctPercent, 29);
        CHECK_EQ(ctPercentQ, 10);
        CHECK_EQ(ctPercentNamed, 19);
        CHECK_EQ(ctTime, 1);
        CHECK_EQ(ctTimeQ, 1);
    }

    // ---- Sec7.4 / Sec7.8 / Sec7.12 node census -------------------------------
    {
        std::map<NodeKind, int> kinds;
        std::map<NodeKind, std::set<int>> kindFiles;
        int nested = 0, clipStates = 0, neitherStates = 0, bothStates = 0, nestedBt = 0, nestedSm = 0;
        std::set<int> nestedFiles;
        std::map<std::string, int> inputCounts;  // "merge:2" -> nodes
        std::map<int, int> pointsPerInput;
        int switchCower = 0, switchIdle = 0, mwbParam = 0, mwbFilter = 0, outOfOrder = 0;
        std::map<int, int> layers;
        int loops = 0, noLoops = 0, nonZeroStart = 0, clipNameStates = 0, clipNameOther = 0, aliasSet = 0, aliasFire = 0;
        std::set<std::string> aliasNames;
        std::map<std::string, int> matchNames;
        int blocks = 0;
        for (size_t gi = 0; gi < lib.graphs.size(); ++gi) {
            for (const NodeDef& n : lib.graphs[gi].nodes) {
                ++kinds[n.kind];
                kindFiles[n.kind].insert(static_cast<int>(gi));
                if (n.kind == NodeKind::State) {
                    const bool hasClip = n.clip && (!n.clip->tagName.empty() || !n.clip->clipName.empty());
                    const bool hasChild = n.childGraphName.has_value();
                    if (hasChild) {
                        ++nested;
                        nestedFiles.insert(static_cast<int>(gi));
                        const GraphDef* c = lib.graph(n.childGraph);
                        if (c) (c->kind == GraphKind::BlendTree ? nestedBt : nestedSm)++;
                    }
                    if (hasClip && !hasChild) ++clipStates;
                    if (!hasClip && !hasChild) ++neitherStates;
                    if (hasClip && hasChild) ++bothStates;
                }
                if (n.kind == NodeKind::Merge || n.kind == NodeKind::Switch || n.kind == NodeKind::MultiWayBlender)
                    ++inputCounts[std::to_string(static_cast<int>(n.kind)) + ":" + std::to_string(n.inputs.size())];
                if (n.kind == NodeKind::Switch && n.parameter && n.parameter->type == OperandType::ControlParameter) {
                    switchCower += n.parameter->sourceName == "cower type";
                    switchIdle += n.parameter->sourceName == "idle type";
                }
                if (n.kind == NodeKind::MultiWayBlender) {
                    mwbParam += n.parameter->type == OperandType::ControlParameter;
                    mwbFilter += n.parameter->type == OperandType::ControlFilter;
                    for (const BlendInput& bi : n.inputs) {
                        ++pointsPerInput[bi.pointCount];
                        for (int p = 0; p + 1 < bi.pointCount; ++p)
                            if (bi.range[p + 1] < bi.range[p]) {
                                ++outOfOrder;
                                break;
                            }
                    }
                }
                if (const ClipBlock* cb = ClipBlockOf(n)) {
                    ++blocks;
                    ++layers[cb->layer];
                    (cb->loop ? loops : noLoops)++;
                    nonZeroStart += cb->startTime != 0.0f;
                    if (!cb->clipName.empty()) (n.kind == NodeKind::State ? clipNameStates : clipNameOther)++;
                    if (!cb->aliasName.empty()) {
                        ++aliasSet;
                        CHECK(cb->aliasId >= kIdClipNodeFirst && cb->aliasId < kIdClipNodeEnd);
                        aliasNames.insert(kNamedClipNodes[cb->aliasId - kIdClipNodeFirst]);
                        aliasFire += cb->aliasId == kIdClipNodeFirst + 12;
                    }
                    if (cb->matchCrc) ++matchNames[cb->matchName];
                }
            }
        }
        CHECK_EQ(kinds[NodeKind::State], 424);
        CHECK_EQ(kindFiles[NodeKind::State].size(), static_cast<size_t>(122));
        CHECK_EQ(nested, 128);
        CHECK_EQ(nestedFiles.size(), static_cast<size_t>(64));
        CHECK_EQ(clipStates, 236);
        CHECK_EQ(neitherStates, 60);
        CHECK_EQ(bothStates, 0);
        CHECK_EQ(nestedBt, 98);
        CHECK_EQ(nestedSm, 30);
        CHECK_EQ(kinds[NodeKind::Clip], 405);
        CHECK_EQ(kindFiles[NodeKind::Clip].size(), static_cast<size_t>(120));
        CHECK_EQ(kinds[NodeKind::MultiWayBlender], 180);
        CHECK_EQ(kindFiles[NodeKind::MultiWayBlender].size(), static_cast<size_t>(93));
        CHECK_EQ(kinds[NodeKind::Merge], 33);
        CHECK_EQ(kindFiles[NodeKind::Merge].size(), static_cast<size_t>(32));
        CHECK_EQ(kinds[NodeKind::StateMachineReference], 19);
        CHECK_EQ(kindFiles[NodeKind::StateMachineReference].size(), static_cast<size_t>(17));
        CHECK_EQ(kinds[NodeKind::BlendTreeReference], 8);
        CHECK_EQ(kindFiles[NodeKind::BlendTreeReference].size(), static_cast<size_t>(5));
        CHECK_EQ(kinds[NodeKind::Switch], 7);
        CHECK_EQ(kindFiles[NodeKind::Switch].size(), static_cast<size_t>(7));
        CHECK_EQ(inputCounts["1:2"], 33);
        CHECK_EQ(inputCounts["2:3"], 6);
        CHECK_EQ(inputCounts["2:4"], 1);
        CHECK_EQ(inputCounts["4:2"], 109);
        CHECK_EQ(inputCounts["4:3"], 65);
        CHECK_EQ(inputCounts["4:4"], 6);
        CHECK_EQ(pointsPerInput[1], 1);
        CHECK_EQ(pointsPerInput[2], 34);
        CHECK_EQ(pointsPerInput[3], 237);
        CHECK_EQ(pointsPerInput[4], 97);
        CHECK_EQ(pointsPerInput[5], 68);
        CHECK_EQ(switchCower, 6);
        CHECK_EQ(switchIdle, 1);
        CHECK_EQ(mwbParam, 108);
        CHECK_EQ(mwbFilter, 72);
        // Sec7.12 counts 18 over the EFFECTIVE population, i.e. with the
        // patched free_fall_bt (its 12 wrap-arounds + 6 trailing-0 inputs,
        // Sec8.7). This library loads the BASE copy, whose three
        // `Multi Way Blend 1` inputs (-20, 0, 20, -100, 100) are the other 3;
        // the patched load below gives 18.
        CHECK_EQ(outOfOrder, 21);
        CHECK_EQ(countOutOfOrderInputs(lib), 21);
        CHECK_EQ(blocks, 829);
        CHECK_EQ(layers[4], 735);
        CHECK_EQ(layers[3], 62);
        CHECK_EQ(layers[2], 29);
        CHECK_EQ(layers[1], 3);
        CHECK_EQ(loops, 416);
        CHECK_EQ(noLoops, 413);
        CHECK_EQ(nonZeroStart, 0);
        CHECK_EQ(clipNameStates, 5);
        CHECK_EQ(clipNameOther, 0);
        CHECK_EQ(aliasSet, 82);
        CHECK_EQ(aliasNames.size(), static_cast<size_t>(12));
        CHECK(!aliasNames.count("Default") && !aliasNames.count("Sprint 180"));
        CHECK_EQ(aliasFire, 48);
        int matchTotal = 0;
        for (const auto& kv : matchNames) matchTotal += kv.second;
        CHECK_EQ(matchTotal, 76);
        CHECK_EQ(matchNames["walk_fire"], 24);
        CHECK_EQ(matchNames["walk_reload"], 24);
        CHECK_EQ(matchNames["ride_fire"], 18);
        CHECK_EQ(matchNames["drive_fire"], 10);
    }

    // ---- Sec7.12 / Sec8.10: the four fold-order divergences ------------------
    // Every expression tree evaluated over every assignment of its comparisons
    // with the engine's left-to-right fold (FoldExpression, Sec8.5) and with
    // conventional precedence. Exactly four transitions differ.
    {
        std::set<std::string> differ;
        int evaluated = 0;
        for (size_t gi = 0; gi < lib.graphs.size(); ++gi) {
            for (const NodeDef& n : lib.graphs[gi].nodes)
                for (const Transition& t : n.transitions) {
                    const Expression& e = t.expression;
                    if (!e.present()) continue;
                    ++evaluated;
                    std::vector<int32_t> cmps;
                    for (size_t i = 0; i < e.nodes.size(); ++i)
                        if (!e.nodes[i].group) cmps.push_back(static_cast<int32_t>(i));
                    int diff = 0;
                    for (uint32_t m = 0; m < (1u << cmps.size()); ++m) {
                        std::vector<bool> v(e.nodes.size(), false);
                        for (size_t k = 0; k < cmps.size(); ++k) v[static_cast<size_t>(cmps[k])] = (m >> k) & 1u;
                        const bool fold = FoldExpression(e, [&](int32_t i) { return static_cast<bool>(v[static_cast<size_t>(i)]); });
                        const bool prec = precedenceChain(e, e.root, v);
                        diff += fold != prec;
                    }
                    if (diff) {
                        differ.insert(lib.rowNames[static_cast<size_t>(lib.graphRow[gi])] + " / " + t.name + " / " +
                                      std::to_string(diff) + " of " + std::to_string(1u << cmps.size()));
                    }
                }
        }
        CHECK_EQ(evaluated, 304);
        for (const std::string& d : differ) std::cout << "  fold differs from precedence: " << d << "\n";
        CHECK((differ == std::set<std::string>{
                             "climb_fence_sm / 3m Fence to 3m Fence Land 0 / 2 of 16",
                             "climb_fence_sm / 3m Fence to 3m Fence Land High 0 / 2 of 16",
                             "jump_sm / State 1 to Jump Land 0 / 6 of 32",
                             "jump_sm / Jump Run Start to Jump Run Land High 0 / 6 of 32",
                         }));
    }

    // ---- Sec7.1: the patched free_fall_bt ------------------------------------
    {
        const std::optional<std::string> patched = patch.read("free_fall_bt.xtbl");
        CHECK(patched.has_value());
        // The patch archive holds no other graph file.
        int graphFilesInPatch = 0;
        for (const auto& e : patch.c->entries()) {
            if (!endsWith(e.name, ".xtbl")) continue;
            const std::optional<std::string> b = patch.read(e.name);
            if (!b) continue;
            try {
                const sr3xtbl::Document d = sr3xtbl::ParseDocument(*b);
                if (!d.root()->children().empty() &&
                    (sr3xtbl::NameEquals(d.root()->children()[0]->name(), "state_machine") ||
                     sr3xtbl::NameEquals(d.root()->children()[0]->name(), "blend_tree")))
                    ++graphFilesInPatch;
            } catch (const std::exception&) {
            }
        }
        CHECK_EQ(graphFilesInPatch, 1);
        NodeGraphLoadInputs pin = in;
        pin.openFile = [&](const std::string& f) {
            if (sr3xtbl::NameEquals(f, "free_fall_bt.xtbl")) return patched;
            return misc.read(f);
        };
        const NodeGraphLibrary plib = LoadNodeGraphs(listDoc, pin);
        CHECK(!plib.aborted && plib.graphs.size() == 249);
        CHECK_EQ(countOutOfOrderInputs(plib), 18);  // Sec7.12, effective population
        // Same graph slot; differs only in the order of Multi Way Blend 1's
        // control points, with the same (range, value) pairs.
        const int32_t gi = lib.findGraph("Free Fall BT");
        CHECK(gi >= 0 && plib.findGraph("Free Fall BT") == gi);
        const GraphDef& a = lib.graphs[static_cast<size_t>(gi)];
        const GraphDef& b = plib.graphs[static_cast<size_t>(gi)];
        CHECK_EQ(a.nodes.size(), b.nodes.size());
        int differingInputs = 0;
        for (size_t i = 0; i < a.nodes.size() && i < b.nodes.size(); ++i) {
            CHECK(a.nodes[i].name == b.nodes[i].name && a.nodes[i].kind == b.nodes[i].kind);
            for (size_t k = 0; k < a.nodes[i].inputs.size(); ++k) {
                const BlendInput& x = a.nodes[i].inputs[k];
                const BlendInput& y = b.nodes[i].inputs[k];
                std::multiset<std::pair<float, float>> px, py;
                bool sameOrder = x.pointCount == y.pointCount;
                for (int p = 0; p < x.pointCount; ++p) px.insert({x.range[p], x.value[p]});
                for (int p = 0; p < y.pointCount; ++p) py.insert({y.range[p], y.value[p]});
                for (int p = 0; sameOrder && p < x.pointCount; ++p)
                    sameOrder = x.range[p] == y.range[p] && x.value[p] == y.value[p];
                CHECK(px == py);
                if (!sameOrder) {
                    ++differingInputs;
                    CHECK(a.nodes[i].name == "Multi Way Blend 1");
                    CHECK((std::vector<float>(x.range, x.range + 5) == std::vector<float>{-20, 0, 20, -100, 100}));
                    CHECK((std::vector<float>(y.range, y.range + 5) == std::vector<float>{-100, -20, 0, 20, 100}));
                }
            }
        }
        CHECK_EQ(differingInputs, 3);
    }

    // =======================================================================
    // U3 (Sec8.10) replays on the shipped graphs.
    // =======================================================================
    const NodeGraphRuntime rt(lib, registry, filters);
    auto graphByRow = [&](const NodeGraphLibrary& L, const std::string& row) -> int32_t {
        for (size_t i = 0; i < L.graphs.size(); ++i)
            if (L.rowNames[static_cast<size_t>(L.graphRow[i])] == row) return static_cast<int32_t>(i);
        return -1;
    };

    // ---- typing (Sec8.10): every comparison that runs, operand 2 same type --
    {
        int exprN = 0, condN = 0, nBool = 0, nInt = 0, nFloat = 0, mismatched = 0, mwbFloat = 0, switchInt = 0;
        auto count = [&](ParamType t) {
            nBool += t == ParamType::Bool;
            nInt += t == ParamType::Integer;
            nFloat += t == ParamType::Float;
        };
        for (const GraphDef& g : lib.graphs)
            for (const NodeDef& n : g.nodes) {
                if (n.kind == NodeKind::MultiWayBlender) mwbFloat += rt.operandType(*n.parameter, nullptr) == ParamType::Float;
                if (n.kind == NodeKind::Switch) switchInt += rt.operandType(*n.parameter, nullptr) == ParamType::Integer;
                for (const Transition& t : n.transitions) {
                    if (t.expression.present()) {
                        for (const ExprNode& en : t.expression.nodes) {
                            if (en.group) continue;
                            ++exprN;
                            const ParamType a = rt.operandType(t.expression.parameters[static_cast<size_t>(en.operand1)], nullptr);
                            const ParamType b = rt.operandType(t.expression.parameters[static_cast<size_t>(en.operand2)], nullptr);
                            count(a);
                            mismatched += a != b || a == ParamType::None;
                        }
                        continue;  // the conditions of a transition with `start` never run
                    }
                    for (const Condition& c : t.conditions) {
                        ++condN;
                        const ParamType a = rt.operandType(c.parameter1, &c), b = rt.operandType(c.parameter2, &c);
                        count(a);
                        mismatched += a != b || a == ParamType::None;
                    }
                }
            }
        std::cout << "  comparisons that run: " << exprN << " in expressions + " << condN << " in conditions (" << nBool
                  << " bool, " << nInt << " integer, " << nFloat << " float), mismatched types: " << mismatched << "\n";
        CHECK_EQ(exprN, 464);
        CHECK_EQ(condN, 233);
        CHECK_EQ(nBool, 340);
        CHECK_EQ(nInt, 12);
        CHECK_EQ(nFloat, 345);
        CHECK_EQ(mismatched, 0);
        CHECK_EQ(mwbFloat, 180);
        CHECK_EQ(switchInt, 7);
    }

    // ---- transitions (Sec8.10) ----------------------------------------------
    {
        int zero = 0, zeroChains = 0, self = 0;
        for (const GraphDef& g : lib.graphs)
            for (size_t ni = 0; ni < g.nodes.size(); ++ni)
                for (const Transition& t : g.nodes[ni].transitions) {
                    self += t.toNode == static_cast<int32_t>(ni);
                    if (t.length != 0.0f) continue;
                    ++zero;
                    for (const Transition& u : g.nodes[static_cast<size_t>(t.toNode)].transitions) zeroChains += u.length == 0.0f;
                }
        CHECK_EQ(zero, 54);
        CHECK_EQ(zeroChains, 0);
        CHECK_EQ(self, 0);
        // every delta multiplier is stable at 1/30 s; no linear map is degenerate
        for (const FilterDef& f : filters.filters()) {
            if (f.kind == FilterKind::DeltaMultiplier) CHECK(f.multiplier * (1.0f / 30.0f) <= 0.5f + 1e-6f);
            if (f.kind == FilterKind::LinearMap) CHECK(f.inputMax != f.inputMin);
        }
    }

    // ---- output-list bound (Sec8.10) ------------------------------------------
    // Upper bound on clips per evaluation: one per clip state or `clip` node;
    // merges and blenders summed; switches maxed; nested graphs included; the
    // evaluated graph's own state machine with one transition in flight (a
    // zero-length transition never reaches pass 1, Sec8.3) and every OTHER
    // cancelable transition of the same state rewinding at once.
    // Two readings of "nested graphs included":
    //   * SPEC: a nested graph counts at its STEADY bound (no transition of its
    //     own in flight). This reproduces every figure Sec8.10 gives: 30 for
    //     ride_left_ready_sm / ride_right_ready_sm (20 steady), 27 for
    //     drive_non-ready_sm, 26 for driving_sm (= drive_non-ready_sm's steady
    //     24 + 2), and 30 as the population maximum.
    //   * RECURSIVE: every nested state machine also has one transition in
    //     flight. Stricter; it gives 31 for ride_left_sm / ride_right_sm (their
    //     `Ready` state nests ride_*_ready_sm at 30, plus one in-flight clip
    //     state) - still below the 33rd entry, but the margin is one, not two.
    //   2026-10-07: Sec8.10 was corrected from "30, margin two" to exactly
    //   this two-reading split (format worst case 31, as-shipped peak 30),
    //   and Team A's own changelog entry says it was "re-derived from Team
    //   B's own real-data test ... swept over all 249 shipped files under
    //   both the steady-bound and recursive readings" - i.e. this very test.
    //   Both CHECK_EQ(..., 31) below already matched before that correction
    //   landed; nothing here changed as a result of it.
    {
        std::map<std::tuple<int32_t, int, bool>, int> memo;
        std::function<int(int32_t, int, bool)> boundGraph;
        // mode: 0 = steady, 1 = with transitions
        std::function<int(int32_t, int32_t, int, bool)> boundNode = [&](int32_t g, int32_t n, int mode, bool recursive) -> int {
            const NodeDef& nd = lib.graphs[static_cast<size_t>(g)].nodes[static_cast<size_t>(n)];
            const int nestedMode = recursive ? mode : 0;
            switch (nd.kind) {
            case NodeKind::State:
                if (nd.childGraph != -1) return boundGraph(nd.childGraph, nestedMode, recursive);
                return (!nd.clip->tagName.empty() || !nd.clip->clipName.empty()) ? 1 : 0;
            case NodeKind::Clip: return 1;
            case NodeKind::Merge:
            case NodeKind::MultiWayBlender: {
                int s = 0;
                for (const BlendInput& bi : nd.inputs) s += boundNode(g, bi.node, mode, recursive);
                return s;
            }
            case NodeKind::Switch: {
                int m = 0;
                for (const BlendInput& bi : nd.inputs) m = std::max(m, boundNode(g, bi.node, mode, recursive));
                return m;
            }
            default: return nd.childGraph != -1 ? boundGraph(nd.childGraph, nestedMode, recursive) : 0;
            }
        };
        boundGraph = [&](int32_t g, int mode, bool recursive) -> int {
            auto it = memo.find({g, mode, recursive});
            if (it != memo.end()) return it->second;
            const GraphDef& d = lib.graphs[static_cast<size_t>(g)];
            int best = 0;
            if (d.kind == GraphKind::BlendTree) {
                best = boundNode(g, d.entryNode, mode, recursive);
            } else {
                for (size_t s = 0; s < d.nodes.size(); ++s) {
                    const int base = boundNode(g, static_cast<int32_t>(s), mode, recursive);
                    if (mode == 0) {
                        best = std::max(best, base);
                        continue;
                    }
                    const std::vector<Transition>& ts = d.nodes[s].transitions;
                    auto cancelable = [](const Transition& t) { return t.cancelableAfter != -1.0f || t.cancelableBefore != -1.0f; };
                    int rewindAll = 0;
                    for (const Transition& t : ts)
                        if (cancelable(t)) rewindAll += boundNode(g, t.toNode, mode, recursive);
                    int extra = rewindAll;  // nothing in flight, every cancelable one rewinding
                    for (const Transition& t : ts) {
                        if (t.length == 0.0f) continue;
                        const int target = boundNode(g, t.toNode, mode, recursive);
                        extra = std::max(extra, target + rewindAll - (cancelable(t) ? target : 0));
                    }
                    best = std::max(best, base + extra);
                }
            }
            memo[{g, mode, recursive}] = best;
            return best;
        };
        for (const bool recursive : {false, true}) {
            std::vector<std::pair<int, std::string>> bounds;
            for (size_t g = 0; g < lib.graphs.size(); ++g)
                bounds.push_back({boundGraph(static_cast<int32_t>(g), 1, recursive), lib.rowNames[static_cast<size_t>(lib.graphRow[g])]});
            std::sort(bounds.rbegin(), bounds.rend());
            std::cout << "  output-list bound (" << (recursive ? "recursive" : "Sec8.10") << " reading): ";
            for (size_t i = 0; i < 5; ++i) std::cout << bounds[i].second << " " << bounds[i].first << (i < 4 ? ", " : "\n");
            CHECK(bounds[0].first <= static_cast<int>(GraphOutputList::kCapacity));  // never reaches the 33rd entry
            if (!recursive) CHECK_EQ(bounds[0].first, 30);
            else CHECK_EQ(bounds[0].first, 31);
        }
        const int32_t left = graphByRow(lib, "ride_left_ready_sm"), right = graphByRow(lib, "ride_right_ready_sm");
        CHECK_EQ(boundGraph(left, 1, false), 30);
        CHECK_EQ(boundGraph(right, 1, false), 30);
        CHECK_EQ(boundGraph(left, 0, false), 20);
        CHECK_EQ(boundGraph(right, 0, false), 20);
        CHECK_EQ(boundGraph(graphByRow(lib, "drive_non-ready_sm"), 1, false), 27);
        CHECK_EQ(boundGraph(graphByRow(lib, "driving_sm"), 1, false), 26);
        CHECK_EQ(boundGraph(graphByRow(lib, "ride_left_sm"), 1, true), 31);
        CHECK_EQ(boundGraph(graphByRow(lib, "ride_right_sm"), 1, true), 31);
    }
    // ---- fake services for full evaluations -----------------------------------
    struct FakeServices : GraphRuntimeServices {
        std::set<int32_t> fired;
        int32_t recordForTag(const GraphInstance&, int32_t tagIndex) override { return 100000 + tagIndex; }
        int32_t recordForClipName(const std::string&) override { return 99999; }
        float recordDuration(int32_t) override { return 1.0f; }
        float clipDuration(int32_t) override { return 1.0f; }
        bool triggerFiredAnyLayer(const GraphInstance&, int32_t t) override { return fired.count(t) != 0; }
        float animationInstancePhase(const GraphInstance&) override { return 0.0f; }
        void stopLayer(const GraphInstance&, int32_t) override {}
    };
    auto makeRow = [&](const NodeGraphLibrary& L, int32_t g) {
        GraphInstance row;
        row.graphIndex = g;
        row.aliases = &L.graphs[static_cast<size_t>(g)].aliases;
        row.values.resize(row.aliases->globalIds.size());
        row.animGroupTable30 = 1;
        return row;
    };
    auto setParam = [&](const NodeGraphLibrary& L, GraphInstance& row, const char* name, uint32_t bits) {
        const int16_t id = registry.resolve(name);
        const int16_t a = L.graphs[static_cast<size_t>(row.graphIndex)].aliases.globalToAlias(id);
        CHECK(a >= 0);
        if (a >= 0) row.values[static_cast<size_t>(a)].dword0 = bits;
    };

    // ---- the four fold traps, through the runtime's own condition evaluator --
    {
        // The "High" variants compare `tag start == false`, the others `== true`;
        // "the comparison is false" therefore means the trigger fired for the
        // former and did not for the latter.
        FakeServices svc;
        struct Trap {
            const char* row;
            const char* transition;
            bool tagStartTrue;  // the expression's `tag start == <this>`
        };
        const int32_t tagStart = triggers.lookup("tag start");
        auto tagStartComparison = [&](bool want, bool tagStartTrue) {
            // fire the trigger so that `tag start == <tagStartTrue>` evaluates to `want`
            if (want == tagStartTrue) svc.fired.insert(tagStart);
            else svc.fired.erase(tagStart);
        };
        auto locate = [&](int32_t g, const char* name, int32_t& node, int32_t& k) {
            node = k = -1;
            const GraphDef& d = lib.graphs[static_cast<size_t>(g)];
            for (size_t n = 0; n < d.nodes.size(); ++n)
                for (size_t t = 0; t < d.nodes[n].transitions.size(); ++t)
                    if (d.nodes[n].transitions[t].name == name) node = static_cast<int32_t>(n), k = static_cast<int32_t>(t);
            CHECK(node >= 0);
        };
        for (const Trap& trap : {Trap{"jump_sm", "State 1 to Jump Land 0", true},
                                 Trap{"jump_sm", "Jump Run Start to Jump Run Land High 0", false}}) {
            const int32_t g = graphByRow(lib, trap.row);
            int32_t node, k;
            locate(g, trap.transition, node, k);
            GraphInstance row = makeRow(lib, g);
            // Sec8.10: Jump Left Ground Bool and Supported By Ground true, the
            // rest false -> engine false (precedence would say true).
            setParam(lib, row, "Jump Left Ground Bool", 1);
            setParam(lib, row, "Supported By Ground", 1);
            svc.fired.clear();
            tagStartComparison(false, trap.tagStartTrue);
            CHECK(!rt.transitionCondition(row, g, node, k, svc));
            // With the trailing `and` terms true as well, it holds.
            tagStartComparison(true, trap.tagStartTrue);
            setParam(lib, row, "desired speed", FloatBits(0.75f));
            CHECK(rt.transitionCondition(row, g, node, k, svc));
        }
        for (const Trap& trap : {Trap{"climb_fence_sm", "3m Fence to 3m Fence Land 0", true},
                                 Trap{"climb_fence_sm", "3m Fence to 3m Fence Land High 0", false}}) {
            const int32_t g = graphByRow(lib, trap.row);
            int32_t node, k;
            locate(g, trap.transition, node, k);
            GraphInstance row = makeRow(lib, g);
            // Sec8.10: climb fall start and Supported by Ground true, the other
            // two false -> engine false (precedence: true). These transitions
            // also carry `conditions`, which never run.
            svc.fired = {triggers.lookup("climb fall start")};
            setParam(lib, row, "Supported By Ground", 1);
            tagStartComparison(false, trap.tagStartTrue);
            CHECK(!rt.transitionCondition(row, g, node, k, svc));
            tagStartComparison(true, trap.tagStartTrue);
            CHECK(rt.transitionCondition(row, g, node, k, svc));
        }
    }

    // ---- filters (Sec8.10) ------------------------------------------------------
    {
        auto f = [&](const char* name) { return *filters.filter(filters.lookup(name)); };
        CHECK(std::fabs(FilterMath(f("direction degrees"), 3.14159f, 0, 0) - 180.0f) < 0.01f);
        CHECK(FilterMath(f("direction degrees"), 7.0f, 0, 0) == 360.0f);
        CHECK(FilterMath(f("speed linear map filter"), 2.5f, 0, 0) == 0.5f);
        CHECK(FilterMath(f("speed linear map filter"), 9.0f, 0, 0) == 1.0f);
        CHECK(std::fabs(FilterMath(f("Turn Angle Cap"), 0.3f, 0, 0) - 0.05f) < 1e-7f);
        // Turn Angle Delta Multi run once per frame: 0.5, 0.75, 0.875 ...
        float y = 0.0f;
        const float once[3] = {0.5f, 0.75f, 0.875f};
        for (float want : once) {
            y = FilterMath(f("Turn Angle Delta Multi"), 1.0f, y, 1.0f / 30.0f);
            CHECK(std::fabs(y - want) < 1e-6f);
        }
        // ... and as the engine runs it in free_fall_sm and parachute_bt (also
        // the source of the listed Turn Angle Cap): twice per evaluation.
        FakeServices svc;
        const int32_t dmId = filters.lookup("Turn Angle Delta Multi");
        for (const char* rowName : {"free_fall_sm", "parachute_bt"}) {
            const int32_t g = graphByRow(lib, rowName);
            CHECK(g >= 0);
            GraphInstance row = makeRow(lib, g);
            setParam(lib, row, "turn angle", FloatBits(1.0f));
            int32_t dmIndex = -1;
            const std::vector<int16_t>& fl = lib.graphs[static_cast<size_t>(g)].filterIds;
            for (size_t i = 0; i < fl.size(); ++i)
                if (fl[i] == dmId) dmIndex = static_cast<int32_t>(i);
            CHECK(dmIndex >= 0 && fl.size() == 2);
            const double twice[3] = {0.75, 0.9375, 0.984375};
            for (double want : twice) {
                row.timeStep3c = 1.0f / 30.0f;
                GraphOutputList out;
                rt.evaluate(row, 0, out, svc);
                rt.evaluate(row, 1, out, svc);
                const float got = BitsFloat(row.filterOutputs[static_cast<size_t>(dmIndex)]);
                CHECK(std::fabs(got - want) < 1e-6);
                std::cout << "  " << rowName << " Turn Angle Delta Multi after a frame: " << got << "\n";
            }
        }
    }

    // ---- multi way blend replay: free_fall_bt base vs patch (Sec8.10) ---------
    {
        const std::optional<std::string> patched = patch.read("free_fall_bt.xtbl");
        NodeGraphLoadInputs pin = in;
        pin.openFile = [&](const std::string& f) {
            if (sr3xtbl::NameEquals(f, "free_fall_bt.xtbl")) return patched;
            return misc.read(f);
        };
        const NodeGraphLibrary plib = LoadNodeGraphs(listDoc, pin);
        const NodeGraphRuntime prt(plib, registry, filters);
        struct Row {
            float x;
            float base[3];
            float patch[3];
        };
        const Row table[4] = {
            {-60, {0.8f, 0, 0.2f}, {1, 0, 0}},
            {-10, {0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}},
            {60, {0.2f, 0, 0.8f}, {0, 0, 1}},
            {150, {0, 0, 0}, {0, 0, 0}},
        };
        FakeServices svc;
        for (int which = 0; which < 2; ++which) {
            const NodeGraphLibrary& L = which ? plib : lib;
            const NodeGraphRuntime& R = which ? prt : rt;
            const int32_t g = graphByRow(L, "free_fall_bt");
            for (const Row& r : table) {
                GraphInstance row = makeRow(L, g);  // fresh: the weights snap
                setParam(L, row, "Free Fall Vert Control", FloatBits(-1.0f));  // all of Multi Way Blend 1, no Dive
                setParam(L, row, "Speed Along rvec", FloatBits(r.x));
                GraphOutputList out;
                R.evaluate(row, 0, out, svc);
                R.evaluate(row, 1, out, svc);
                CHECK_EQ(out.size(), static_cast<size_t>(4));  // inputs at zero weight are still emitted
                std::map<std::string, float> w;
                for (const OutputEntry& e : out.entries())
                    w[L.graphs[static_cast<size_t>(e.graph)].nodes[static_cast<size_t>(e.node)].name] = e.weight;
                const float* want = which ? r.patch : r.base;
                CHECK(std::fabs(w["Turn Left"] - want[0]) < 1e-6f);
                CHECK(std::fabs(w["Normal"] - want[1]) < 1e-6f);
                CHECK(std::fabs(w["Turn Right"] - want[2]) < 1e-6f);
                CHECK(w["Dive"] == 0.0f);
            }
        }
        // crouch_walk_blend_forward: 305, 360, 0, 90 / 0, 90, 145 on direction degrees.
        const GraphDef& cw = lib.graphs[static_cast<size_t>(graphByRow(lib, "crouch_walk_blend_forward"))];
        const NodeDef& m = cw.nodes[static_cast<size_t>(cw.entryNode)];
        CHECK(MultiWayCurve(m.inputs[0], 330) == 1.0f && MultiWayCurve(m.inputs[1], 330) == 0.0f);
        CHECK(MultiWayCurve(m.inputs[0], 0) == 1.0f && MultiWayCurve(m.inputs[1], 0) == 0.0f);
        CHECK(MultiWayCurve(m.inputs[0], 45) == 0.5f && MultiWayCurve(m.inputs[1], 45) == 0.5f);
        for (float x : {146.0f, 200.0f, 304.0f}) CHECK(MultiWayCurve(m.inputs[0], x) == 0.0f && MultiWayCurve(m.inputs[1], x) == 0.0f);
    }

    // ---- unresolved tags (Sec8.6): the default states emit nothing -------------
    {
        FakeServices svc;
        for (const char* rowName : {"spinslower_sm", "tiger_ride_sm"}) {
            const int32_t g = graphByRow(lib, rowName);
            GraphInstance row = makeRow(lib, g);
            row.flags48 |= 0x02u;  // frozen: no transition starts, only the default state runs
            GraphOutputList out;
            rt.evaluate(row, 0, out, svc);
            rt.evaluate(row, 1, out, svc);
            CHECK_EQ(out.size(), static_cast<size_t>(0));
        }
    }

    // ---- Sec5.1 item 29 exposure ------------------------------------------------
    // A state tests its transitions in pass 0 of the frame that creates its
    // clip instances, before the post step has written +0x38. Only states
    // entered without an in-flight period can do so: a graph's default state
    // and the target of a zero-length transition. Count those whose own
    // transitions use `clip time remaining` (OPEN then) or `clip time as
    // percent` (0 then).
    {
        int exposedRemaining = 0, exposedPercent = 0;
        for (const GraphDef& g : lib.graphs) {
            std::set<int32_t> entered{g.entryNode};
            for (const NodeDef& n : g.nodes)
                for (const Transition& t : n.transitions)
                    if (t.length == 0.0f) entered.insert(t.toNode);
            if (g.kind != GraphKind::StateMachine) continue;
            for (int32_t s : entered) {
                bool rem = false, pct = false;
                for (const Transition& t : g.nodes[static_cast<size_t>(s)].transitions) {
                    auto look = [&](const Operand& op) {
                        rem |= op.type == OperandType::ClipTimeRemaining;
                        pct |= op.type == OperandType::ClipTimeAsPercent;
                    };
                    if (t.expression.present())
                        for (const Operand& p : t.expression.parameters) look(p);
                    else
                        for (const Condition& c : t.conditions) look(c.parameter1), look(c.parameter2);
                }
                exposedRemaining += rem;
                exposedPercent += pct;
            }
        }
        std::cout << "  item-29 exposure: " << exposedRemaining << " creation-frame states test `clip time remaining`, "
                  << exposedPercent << " test `clip time as percent`\n";
        CHECK_EQ(exposedRemaining, 0);  // the OPEN refusal is unreachable from these entry paths in shipped data
        CHECK_EQ(exposedPercent, 1);    // numerator 0 on that frame: answers 0 (item 29)
    }

    // ---- every shipped graph evaluates (frozen, default parameters) ------------
    // A smoke replay: each of the 249 graphs runs 3 frames on its default
    // state(s) without reaching any guard, and no list exceeds the bound.
    {
        FakeServices svc;
        int ran = 0;
        size_t peak = 0;
        for (size_t g = 0; g < lib.graphs.size(); ++g) {
            GraphInstance row = makeRow(lib, static_cast<int32_t>(g));
            try {
                for (int frame = 0; frame < 3; ++frame) {
                    row.timeStep3c = frame ? 1.0f / 30.0f : 0.0f;
                    GraphOutputList out;
                    rt.evaluate(row, 0, out, svc);
                    rt.evaluate(row, 1, out, svc);
                    // test-side stand-in for U4's post step: +0x38 / +0x3c of
                    // every emitting clip instance (read by the clip-time operands)
                    for (const OutputEntry& e : out.entries())
                        for (NodeInstance& x : row.nodeInstances)
                            if (!x.isFree() && x.graph == e.graph && x.node == e.node && x.transition == -1)
                                x.clipIndex = 5, x.layerHandle = 16;
                    peak = std::max(peak, out.peak());
                    CHECK_EQ(out.overflowGuardCount(), 0);
                }
                ++ran;
            } catch (const std::exception& e) {
                std::cerr << "  evaluation of " << lib.graphs[g].fileName << " failed: " << e.what() << "\n";
            }
        }
        std::cout << "U3 replay: " << ran << "/" << lib.graphs.size() << " graphs evaluate 3 frames (largest list "
                  << peak << ")\n";
        CHECK_EQ(ran, 249);
        CHECK(peak <= 30);
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real_anim_node_graph_test: all checks passed (249/249 graph files)\n";
    return 0;
}
