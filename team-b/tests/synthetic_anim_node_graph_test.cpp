// Synthetic tests for sr3animruntime U2 (spec-anim-runtime.md Sec7): the
// lexical rules, the expression tokenizer / tree builder, the parsers' edge
// rules and the loader's abort / skip paths, on hand-written graph files.
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3animruntime/controller.h"
#include "sr3animruntime/node_graph.h"
#include "sr3animruntime/node_graph_runtime.h"
#include "sr3xtbl/xtbl.h"

using namespace sr3animruntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

template <class F>
bool throwsDataError(F f) {
    try {
        f();
    } catch (const NodeGraphDataError&) {
        return true;
    }
    return false;
}

const char* kParams = R"(<root><control_parameters>
<control_parameter><name>A Bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>B Bool</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>
<control_parameter><name>speed</name><data_type>float</data_type><default_value>0</default_value></control_parameter>
<control_parameter><name>count</name><data_type>integer</data_type><default_value>0</default_value></control_parameter>
</control_parameters></root>)";

const char* kFilters = R"(<root><control_filters>
<control_filter><name>Speed Map</name><type>linear map</type><source_name>speed</source_name><input_min>0</input_min><input_max>10</input_max><output_min>0</output_min><output_max>1</output_max></control_filter>
<control_filter><name>Speed Smooth</name><type>delta multiplier</type><source_name>Speed Map</source_name><multiplier>15</multiplier></control_filter>
<control_filter><name>Speed Cap</name><type>cap</type><source_name>Speed Smooth</source_name><output_min>0</output_min><output_max>0.5</output_max></control_filter>
</control_filters></root>)";

struct Env {
    ControlParameterRegistry registry = ControlParameterRegistry::loadXml(kParams);
    ControlFilterTable filters;
    StateActionRegistry stateActions = StateActionRegistry::build({"preset one"}, {"run", "walk", "Preset One"}, {"jump", "run"});
    TriggerRegistry triggers = TriggerRegistry::build({"tag start", "foot down"});
    std::map<std::string, std::string> files;
    int opens = 0;
    Env() {
        const sr3xtbl::Document d = sr3xtbl::ParseDocument(std::string_view(kFilters));
        filters = ControlFilterTable::load(d, registry);
    }
    NodeGraphLoadInputs inputs() {
        NodeGraphLoadInputs in;
        in.registry = &registry;
        in.filters = &filters;
        in.stateActions = &stateActions;
        in.triggers = &triggers;
        in.openFile = [this](const std::string& f) -> std::optional<std::string> {
            ++opens;
            auto it = files.find(f);
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
        return in;
    }
    NodeGraphLibrary load(const std::vector<std::string>& rows) {
        std::string list = "<root><node_graph_files>";
        for (const std::string& r : rows) list += "<node_graph_file><name>" + r + "</name></node_graph_file>";
        list += "</node_graph_files></root>";
        const sr3xtbl::Document d = sr3xtbl::ParseDocument(std::string_view(list));
        return LoadNodeGraphs(d, inputs());
    }
};

Expression expr(const std::string& start, const std::vector<std::string>& params) {
    Expression e;
    e.text = ConvertExpressionText(start);
    for (const std::string& p : params) {
        Operand op;
        op.type = OperandType::ControlParameter;
        op.sourceName = p;
        e.parameters.push_back(op);
    }
    e.tokens = TokenizeExpression(e.text);
    BuildExpressionTree(e);
    return e;
}

}  // namespace

int main() {
    // ---- Sec7.3 lexical rules ------------------------------------------------
    {
        CHECK(LexBoolText("yes") && LexBoolText("TRUE") && LexBoolText("1"));
        CHECK(!LexBoolText("no") && !LexBoolText("false") && !LexBoolText("2") && !LexBoolText(""));
        CHECK(LexIntText("0x10") == 16);   // base 0: hex
        CHECK(LexIntText("010") == 8);     // base 0: octal
        CHECK(LexIntText("12abc") == 12);
        CHECK(LexIntText("abc") == 0);
        CHECK(LexFloatText("0.5") == 0.5f && LexFloatText("-1e1") == -10.0f && LexFloatText("x") == 0.0f);
        const sr3xtbl::Document d = sr3xtbl::ParseDocument(std::string_view("<r><a>v </a><b></b></r>"));
        CHECK(LexText(d.root(), "A", "none") == "v");      // case-insensitive lookup; D-16: text is trimmed (bytes 1-32) by the document parser
        CHECK(LexText(d.root(), "b", "none") == "none");   // no text -> default
        CHECK(LexText(d.root(), "c", "") == "");
        CHECK(LexInt(d.root(), "c", 4) == 4);
        CHECK(LexFloat(d.root(), "c", -1.0f) == -1.0f);
    }

    // ---- registries --------------------------------------------------------
    {
        Env env;
        // presets first; a file name equal to a preset reuses it; file-file duplicates append
        CHECK(env.stateActions.names().size() == 5);  // preset one, run, walk, jump, run
        CHECK(env.stateActions.lookup("PRESET ONE") == 0);
        CHECK(env.stateActions.lookup("run") == 1);
        CHECK(env.stateActions.lookup("jump") == 3);
        CHECK(env.stateActions.lookup("missing") == -1);
        CHECK(env.stateActions.lookup("") == -1);
        CHECK(env.triggers.lookup("Tag Start") == 0 && env.triggers.lookup("foot down") == 1 && env.triggers.lookup("x") == -1);
        CHECK(TriggerNameHash("ab") == ((static_cast<uint32_t>('a') << 6) ^ 'b'));
        // the 31-char bound and the 110 cap
        std::vector<std::string> many;
        many.push_back(std::string(32, 'x'));
        for (int i = 0; i < 120; ++i) many.push_back("t" + std::to_string(i));
        const TriggerRegistry tr = TriggerRegistry::build(many);
        CHECK(tr.count() == 110 && tr.lookup(std::string(32, 'x')) == -1 && tr.lookup("t109") == 109 && tr.lookup("t110") == -1);
        // filters: source resolution order
        CHECK(env.filters.filters().size() == 3);
        CHECK(!env.filters.filters()[0].sourceIsFilter && env.filters.filters()[0].sourceId == 2);
        CHECK(env.filters.filters()[1].sourceIsFilter && env.filters.filters()[1].sourceId == 0);
        CHECK(env.filters.filters()[2].type == ParamType::Float);
        CHECK(env.filters.lookup("speed cap") == 2);
        // a later-row filter source keeps type 0; an unknown type / source is refused
        const char* later = R"(<root><control_filters>
<control_filter><name>F1</name><type>cap</type><source_name>F2</source_name></control_filter>
<control_filter><name>F2</name><type>cap</type><source_name>speed</source_name></control_filter>
</control_filters></root>)";
        const sr3xtbl::Document dl = sr3xtbl::ParseDocument(std::string_view(later));
        const ControlFilterTable lt = ControlFilterTable::load(dl, env.registry);
        CHECK(lt.filters()[0].type == ParamType::None && lt.filters()[1].type == ParamType::Float);
        const sr3xtbl::Document bad1 = sr3xtbl::ParseDocument(std::string_view(
            "<root><control_filters><control_filter><name>F</name><type>median</type><source_name>speed</source_name></control_filter></control_filters></root>"));
        CHECK(throwsDataError([&] { ControlFilterTable::load(bad1, env.registry); }));
        const sr3xtbl::Document bad2 = sr3xtbl::ParseDocument(std::string_view(
            "<root><control_filters><control_filter><name>F</name><type>cap</type><source_name>nothing</source_name></control_filter></control_filters></root>"));
        CHECK(throwsDataError([&] { ControlFilterTable::load(bad2, env.registry); }));
    }

    // ---- Sec7.6 tokenizer ----------------------------------------------------
    {
        const std::vector<ExprToken> t = TokenizeExpression(ConvertExpressionText("(Jump  Start Land Bool == true)and x ]= 0.5 XOR y [ -0.785"));
        std::vector<std::pair<TokenCode, std::string>> got;
        for (const ExprToken& k : t) got.push_back({k.code, k.text});
        const std::vector<std::pair<TokenCode, std::string>> want = {
            {TokenCode::Open, "("},     {TokenCode::Name, "Jump Start Land Bool"}, {TokenCode::Eq, "=="},
            {TokenCode::Name, "true"},  {TokenCode::Close, ")"},  {TokenCode::And, "and"}, {TokenCode::Name, "x"},
            {TokenCode::Ge, ">="},      {TokenCode::Number, "0.5"}, {TokenCode::Xor, "XOR"}, {TokenCode::Name, "y"},
            {TokenCode::Lt, "<"},       {TokenCode::Name, "-0.785"},
        };
        CHECK(got == want);
        // a name followed by a number is not merged
        const std::vector<ExprToken> t2 = TokenizeExpression("Climb Duration ms >= 750");
        CHECK(t2.size() == 3 && t2[0].text == "Climb Duration ms" && t2[2].code == TokenCode::Number);
    }

    // ---- Sec7.6 tree builder + Sec8.5 fold -------------------------------------
    {
        // a or b and c == (a or b) and c
        Expression e = expr("a == t or b == t and c == t", {"a", "b", "c", "t"});
        CHECK(e.root == 0 && e.nodes.size() == 3);
        CHECK(e.nodes[0].connective == 1 && e.nodes[1].connective == 0 && e.nodes[2].connective == -1);
        CHECK(e.nodes[0].operand1 == 0 && e.nodes[0].operand2 == 3 && e.nodes[0].comparison == Comparison::Equal);
        auto fold = [&](const Expression& x, std::vector<bool> v) {
            return FoldExpression(x, [&](int32_t i) { return static_cast<bool>(v[static_cast<size_t>(i)]); });
        };
        CHECK(fold(e, {true, false, false}) == false);  // precedence would give true
        CHECK(fold(e, {true, false, true}) == true);
        // xor sets nothing: the connective keeps its previous value (-1: replace)
        Expression x = expr("a == t xor b == t", {"a", "b", "t"});
        CHECK(x.nodes.size() == 2 && x.nodes[0].connective == -1);
        CHECK(fold(x, {true, false}) == false && fold(x, {false, true}) == true);
        // the jump_sm shape: (A and B) or (C) and (D) and E
        Expression j = expr("(A == t and B == t) or (C == t) and (D == t) and E >= t", {"A", "B", "C", "D", "E", "t"});
        // groups G1 -or- G2 -and- G3 -and- C5
        int32_t groups = 0;
        for (const ExprNode& n : j.nodes) groups += n.group;
        CHECK(groups == 3);
        CHECK(j.nodes[static_cast<size_t>(j.root)].group && j.nodes[static_cast<size_t>(j.root)].connective == 1);
        // values by comparison order A,B,C,D,E
        std::vector<int32_t> cmps;
        for (size_t i = 0; i < j.nodes.size(); ++i)
            if (!j.nodes[i].group) cmps.push_back(static_cast<int32_t>(i));
        CHECK(cmps.size() == 5);
        auto foldJ = [&](std::vector<bool> abcde) {
            return FoldExpression(j, [&](int32_t i) {
                for (size_t k = 0; k < cmps.size(); ++k)
                    if (cmps[k] == i) return static_cast<bool>(abcde[k]);
                return false;
            });
        };
        // Sec8.10: Jump Left Ground Bool and Supported By Ground true, the rest false -> false (precedence: true)
        CHECK(foldJ({true, true, false, false, false}) == false);
        CHECK(foldJ({true, true, false, true, true}) == true);
        // an unmatched operand is dropped (-1) and the next one takes its slot
        Expression u = expr("zz == a", {"a"});
        CHECK(u.unmatchedOperandTokens == std::vector<std::string>{"zz"} && u.nodes[0].operand1 == 0 && u.nodes[0].operand2 == -1);
        // a comparison operator after `)` would write past the group: skipped and counted
        Expression g = expr("(a) == a", {"a"});
        CHECK(g.hostGuardComparisonOnGroup == 1);
        // an extra `(` is accepted
        Expression o = expr("((a == a) and (a == a)", {"a"});
        CHECK(o.root >= 0 && o.hostGuardNoCurrentNode == 0);
        // `)` with no parent anywhere stays at the chain's first node
        Expression c = expr("a == a ) or a == a", {"a"});
        CHECK(c.nodes.size() == 2 && c.nodes[0].connective == 1 && c.nodes[0].next == 1);
    }

    // ---- loader: a state machine and a blend tree ----------------------------
    {
        Env env;
        env.files["sm_one.xtbl"] = R"(<root><state_machine><validated>yes</validated><name>SM One</name>
<default_node>idle</default_node><nodes>
<node><name>Idle</name><tag_name>run</tag_name><loop_flag>TRUE</loop_flag><alias_name>fire</alias_name><match_name>walk_fire</match_name>
<transitions><transition><name>t0</name><to_node>MOVE</to_node><length>0.25</length><cancelable_before>0.1</cancelable_before>
<start>(A Bool == true or B Bool == true) and speed ]= 0.5</start>
<parameter><type>control parameter</type><source_name>a bool</source_name></parameter>
<parameter><type>Control Parameter</type><source_name>B Bool</source_name></parameter>
<parameter><type>boolean</type><source_name>TRUE</source_name></parameter>
<parameter><type>control parameter</type><source_name>speed</source_name></parameter>
<parameter><type>float</type><source_name>0.5</source_name></parameter>
<conditions><condition><comparison>less</comparison><parameter1><type>trigger</type><source_name>tag start</source_name></parameter1>
<parameter2><type>local control parameter</type><source_name>local</source_name></parameter2></condition></conditions>
<sync_controller_transform>false</sync_controller_transform><or>1</or>
</transition></transitions></node>
<node><name>Move</name><child_node_graph>bt one</child_node_graph><layer>abc</layer><loop_flag>false</loop_flag>
<transitions><transition><name>back</name><to_node>Idle</to_node>
<conditions><condition><control_parameter><name>local</name><data_type>integer</data_type><default_value>0x3</default_value></control_parameter>
<comparison>Greater Or Equal</comparison><parameter1><type>clip time as percent</type><source_name>jump%</source_name></parameter1>
<parameter2><type>clip time remaining</type><source_name>?@</source_name></parameter2></condition></conditions>
</transition></transitions></node>
</nodes></state_machine></root>)";
        env.files["bt_one.xtbl"] = R"(<root><blend_tree><validated>true</validated><name>BT One</name><output_node>mix</output_node><nodes>
<node><name>Mix</name><type>Multi Way Blender</type><parameter><type>control filter</type><source_name>speed cap</source_name></parameter>
<inputs><input><name>a</name><max_blend_rate>0.4</max_blend_rate>
<control_point><range>0</range><value>1</value></control_point><control_point><range>1</range><value>0</value></control_point></input>
<input><name>b</name><control_point><range>0</range><value>0</value></control_point></input></inputs></node>
<node><name>a</name><type>clip</type><clip_name>some_anim</clip_name></node>
<node><name>b</name><type>state machine reference</type><child_node_graph>SM ONE</child_node_graph></node>
</nodes></blend_tree></root>)";
        env.files["skipme.xtbl"] = "<root><state_machine><validated>false</validated><name>Skipped</name></state_machine></root>";
        env.files["nothing.xtbl"] = "<root><other/></root>";
        // a cycle SM One -> BT One -> SM One is refused at the alias walk
        CHECK(throwsDataError([&] { env.load({"sm_one", "skipme", "bt_one", "nothing"}); }));
        // break the cycle and load again
        env.files["bt_one.xtbl"].replace(env.files["bt_one.xtbl"].find("SM ONE"), 6, "Absent");
        env.opens = 0;
        const NodeGraphLibrary lib = env.load({"sm_one", "skipme", "bt_one", "nothing"});
        CHECK(!lib.aborted && lib.graphs.size() == 2 && lib.graphRow == (std::vector<int32_t>{0, 2}));
        CHECK(lib.skippedNotValidated == (std::vector<int32_t>{1, 3}));
        CHECK(env.opens == 4 + 2);  // pass 2 every row, pass 3 every accepted graph
        CHECK(lib.findGraph("bt one") == 1 && lib.findGraph("skipped") == -1);
        const GraphDef& sm = lib.graphs[0];
        CHECK(sm.kind == GraphKind::StateMachine && sm.name == "SM One" && sm.entryNode == 0);
        CHECK(sm.fileNameCrc == sr3xtbl::NameHash("sm_one.xtbl"));
        const NodeDef& idle = sm.nodes[0];
        CHECK(idle.clip->tagIndex == 1);  // after the one preset
        CHECK(idle.clip->loop);  // Sec8.6: bit 0 set when the text equals `true`
        CHECK(idle.clip->aliasId == kIdClipNodeFirst + 12 && idle.clip->matchCrc && *idle.clip->matchCrc == sr3xtbl::NameHash("walk_fire"));
        CHECK(idle.clip->layer == 4 && idle.clip->clipNameRecord == -1 && idle.childGraph == -1);
        const Transition& t0 = idle.transitions[0];
        CHECK(t0.toNode == 1 && t0.length == 0.25f && t0.cancelableAfter == -1.0f && t0.cancelableBefore == 0.1f &&
              t0.cyclicPercent == -1.0f);
        CHECK(t0.syncControllerTransform);  // presence, whatever the text
        CHECK(t0.orForm);                   // `1` is true
        CHECK(t0.expression.parameters.size() == 5 && t0.expression.unmatchedOperandTokens.empty());
        CHECK(t0.expression.parameters[0].id == env.registry.resolve("A Bool"));
        CHECK(t0.expression.parameters[2].literal == 1);
        CHECK(t0.expression.parameters[4].literal == FloatBits(0.5f));
        CHECK(t0.conditions.size() == 1 && t0.conditions[0].comparison == Comparison::Less && !t0.conditions[0].local);
        CHECK(t0.conditions[0].parameter1.triggerIndex == 0);
        const NodeDef& move = sm.nodes[1];
        CHECK(move.childGraph == 1 && move.clip->layer == 0 && !move.clip->loop);  // present non-numeric layer -> 0
        const Condition& back = move.transitions[0].conditions[0];
        CHECK(back.local && back.local->type == ParamType::Integer && back.local->defaultInt() == 3);
        CHECK(back.comparison == Comparison::GreaterOrEqual);
        CHECK(back.parameter1.type == OperandType::ClipTimeAsPercent && back.parameter1.id == env.stateActions.lookup("jump") &&
              !back.parameter1.ownClip);
        CHECK(back.parameter2.type == OperandType::ClipTimeRemaining && back.parameter2.ownClip);
        CHECK(!move.transitions[0].expression.present());
        const GraphDef& bt = lib.graphs[1];
        CHECK(bt.kind == GraphKind::BlendTree && bt.entryNode == 0);
        const NodeDef& mix = bt.nodes[0];
        CHECK(mix.kind == NodeKind::MultiWayBlender && mix.constant18 == 2 && mix.constant1a == 8 && mix.selector == -1);
        CHECK(mix.inputs.size() == 2 && mix.inputs[0].node == 1 && mix.inputs[1].node == 2);
        CHECK(mix.inputs[0].filled && mix.inputs[0].maxBlendRate == 0.4f && mix.inputs[0].pointCount == 2);
        CHECK(mix.inputs[1].maxBlendRate == 0.0f && mix.inputs[1].pointCount == 1);
        CHECK(mix.parameter->type == OperandType::ControlFilter && mix.parameter->id == 2);
        CHECK(bt.nodes[1].kind == NodeKind::Clip && bt.nodes[1].clip->clipName == "some_anim" && bt.nodes[1].clip->tagIndex == -1);
        CHECK(bt.nodes[2].childGraph == -1);  // `Absent` resolves to nothing
        // Sec7.9: aliases cover the nested graph; the filter chain feeds both lists
        std::vector<int16_t> smAliases = sm.aliases.globalIds;
        CHECK(sm.aliases.present);
        CHECK(smAliases == (std::vector<int16_t>{0, 1, 2}));  // A Bool, B Bool, speed (twice -> once), + the nested filter source
        CHECK(sm.filterIds == (std::vector<int16_t>{2, 1, 0}));  // Speed Cap, its source, the source's source
        CHECK(bt.aliases.globalIds == (std::vector<int16_t>{2}) && bt.filterIds == (std::vector<int16_t>{2, 1, 0}));
        // Sec7.4: 0 when the graph has no nodes
        GraphDef empty;
        CHECK(FindNodeIndex(empty, "x") == 0);
    }

    // ---- loader: abort, truncation and crash refusals ------------------------
    {
        Env env;
        env.files["a.xtbl"] = "<root><blend_tree><validated>true</validated><name>A</name><output_node>c</output_node><nodes>"
                              "<node><name>c</name><type>clip</type></node></nodes></blend_tree></root>";
        // a missing file aborts at once: later rows never load and earlier ones stay unlinked
        const NodeGraphLibrary l1 = env.load({"a", "missing", "a2"});
        CHECK(l1.aborted && l1.abortedPass == 2 && l1.abortedFile == "missing.xtbl" && l1.graphs.size() == 1);
        CHECK(!l1.graphs[0].linked && !l1.graphs[0].aliases.present && l1.graphs[0].entryNode == -1);
        // a 6th control point abandons that input and every later input
        std::string pts;
        for (int i = 0; i < 6; ++i) pts += "<control_point><range>" + std::to_string(i) + "</range><value>1</value></control_point>";
        env.files["b.xtbl"] = "<root><blend_tree><validated>true</validated><name>B</name><output_node>m</output_node><nodes>"
                              "<node><name>m</name><type>multi way blender</type><parameter><type>control parameter</type><source_name>speed</source_name></parameter>"
                              "<inputs><input><name>c</name><control_point><range>0</range><value>1</value></control_point></input>"
                              "<input><name>c</name>" + pts + "</input><input><name>c</name></input></inputs></node>"
                              "<node><name>c</name><type>clip</type></node></nodes></blend_tree></root>";
        const NodeGraphLibrary l2 = env.load({"b"});
        const NodeDef& m = l2.graphs[0].nodes[0];
        CHECK(m.inputsTruncated && m.inputs.size() == 3 && m.inputs[0].filled && !m.inputs[1].filled && !m.inputs[2].filled);
        CHECK(m.inputs[2].node == 1);  // input names still resolve in pass 3
        // an unknown blend-node type / a missing source_name are refused
        env.files["c.xtbl"] = "<root><blend_tree><validated>true</validated><name>C</name><nodes><node><name>x</name><type>blend</type></node></nodes></blend_tree></root>";
        CHECK(throwsDataError([&] { env.load({"c"}); }));
        env.files["d.xtbl"] = "<root><blend_tree><validated>true</validated><name>D</name><nodes><node><name>x</name><type>switch</type>"
                              "<parameter><type>control parameter</type></parameter></node></nodes></blend_tree></root>";
        CHECK(throwsDataError([&] { env.load({"d"}); }));
        // name length / row count bounds
        CHECK(throwsDataError([&] { env.load({std::string(251, 'n')}); }));
        CHECK(throwsDataError([&] { env.load(std::vector<std::string>(384, "a")); }));
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_anim_node_graph_test: all checks passed\n";
    return 0;
}
