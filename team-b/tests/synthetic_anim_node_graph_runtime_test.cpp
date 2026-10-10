// Synthetic tests for sr3animruntime U3 (spec-anim-runtime.md Sec8): the
// node-graph runtime on hand-written graphs - the output-list guard, the
// state-machine cross-fade / transition codes, blend nodes, filters, clip
// blocks, operands, garbage collection, and the Controller integration.
#include <cmath>
#include <iostream>
#include <map>
#include <set>
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

bool feq(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol; }

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

struct Services : GraphRuntimeServices {
    std::set<int32_t> fired;
    int clipNameLookups = 0;
    std::vector<int32_t> stopped;
    float phase = 0.0f;
    int32_t recordForTag(const GraphInstance&, int32_t tagIndex) override { return 1000 + tagIndex; }
    int32_t recordForClipName(const std::string&) override {
        ++clipNameLookups;
        return 777;
    }
    float recordDuration(int32_t) override { return 1.0f; }
    float clipDuration(int32_t) override { return 1.0f; }
    bool triggerFiredAnyLayer(const GraphInstance&, int32_t t) override { return fired.count(t) != 0; }
    float animationInstancePhase(const GraphInstance&) override { return phase; }
    void stopLayer(const GraphInstance&, int32_t h) override { stopped.push_back(h); }
};

struct World {
    ControlParameterRegistry registry = ControlParameterRegistry::loadXml(kParams);
    ControlFilterTable filters;
    StateActionRegistry stateActions = StateActionRegistry::build({}, {"run", "walk", "jump", "idle"}, {});
    TriggerRegistry triggers = TriggerRegistry::build({"tag start"});
    std::map<std::string, std::string> files;
    std::vector<std::string> rows;
    NodeGraphLibrary lib;
    World() {
        const sr3xtbl::Document d = sr3xtbl::ParseDocument(std::string_view(kFilters));
        filters = ControlFilterTable::load(d, registry);
    }
    void add(const std::string& name, const std::string& xml) {
        files[name + ".xtbl"] = xml;
        rows.push_back(name);
    }
    void load() {
        std::string list = "<root><node_graph_files>";
        for (const std::string& r : rows) list += "<node_graph_file><name>" + r + "</name></node_graph_file>";
        list += "</node_graph_files></root>";
        const sr3xtbl::Document d = sr3xtbl::ParseDocument(std::string_view(list));
        NodeGraphLoadInputs in;
        in.registry = &registry;
        in.filters = &filters;
        in.stateActions = &stateActions;
        in.triggers = &triggers;
        in.openFile = [this](const std::string& f) -> std::optional<std::string> {
            auto it = files.find(f);
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
        lib = LoadNodeGraphs(d, in);
    }
    int32_t graph(const std::string& name) const { return lib.findGraph(name); }
    GraphInstance row(int32_t g) const {
        GraphInstance r;
        r.graphIndex = g;
        r.aliases = &lib.graphs[static_cast<size_t>(g)].aliases;
        r.values.resize(r.aliases->globalIds.size());
        r.animGroupTable30 = 1;
        return r;
    }
    void set(GraphInstance& r, const char* name, uint32_t bits) const {
        const int16_t a = lib.graphs[static_cast<size_t>(r.graphIndex)].aliases.globalToAlias(registry.resolve(name));
        if (a < 0) throw std::logic_error(std::string("parameter not aliased: ") + name);
        r.values[static_cast<size_t>(a)].dword0 = bits;
    }
    int32_t nodeIndex(int32_t g, const std::string& name) const { return FindNodeIndex(lib.graphs[static_cast<size_t>(g)], name); }
};

// TEST-SIDE STAND-IN for U4's post step (U1 Sec6.10 step 5, OPEN in the
// library): it writes each emitting clip instance's +0x38 clip index and
// +0x3c layer handle, which the clip-time operands read (Sec8.5).
void emulatePost(GraphInstance& row, const GraphOutputList& out) {
    for (const OutputEntry& e : out.entries())
        for (NodeInstance& x : row.nodeInstances)
            if (!x.isFree() && x.graph == e.graph && x.node == e.node && x.transition == -1) {
                x.clipIndex = 5;
                x.layerHandle = 16;
            }
}

// One evaluation (both passes) with `dt` as the row's time step, then the
// post-step stand-in.
GraphOutputList frame(const NodeGraphRuntime& rt, GraphInstance& row, Services& svc, float dt, bool post = true) {
    row.timeStep3c = dt;
    GraphOutputList out;
    rt.evaluate(row, 0, out, svc);
    rt.evaluate(row, 1, out, svc);
    if (post) emulatePost(row, out);
    return out;
}

std::string clipState(const char* name, const char* tag, const std::string& transitions = "", const char* loop = "false") {
    return std::string("<node><name>") + name + "</name><tag_name>" + tag + "</tag_name><loop_flag>" + loop + "</loop_flag>" +
           (transitions.empty() ? "" : "<transitions>" + transitions + "</transitions>") + "</node>";
}
std::string boolCond(const char* param, bool value) {
    return std::string("<condition><control_parameter><name>local</name><data_type>bool</data_type><default_value>") +
           (value ? "true" : "false") +
           "</default_value></control_parameter><comparison>equal</comparison><parameter1><type>control parameter</type>"
           "<source_name>" + param + "</source_name></parameter1><parameter2><type>local control parameter</type>"
           "<source_name>local</source_name></parameter2></condition>";
}
std::string transition(const char* name, const char* to, float length, const std::string& conditions, const std::string& extra = "") {
    return std::string("<transition><name>") + name + "</name><to_node>" + to + "</to_node><length>" + std::to_string(length) +
           "</length>" + extra + (conditions.empty() ? "" : "<conditions>" + conditions + "</conditions>") + "</transition>";
}
std::string stateMachine(const char* name, const char* deflt, const std::string& nodes) {
    return std::string("<root><state_machine><validated>true</validated><name>") + name + "</name><default_node>" + deflt +
           "</default_node><nodes>" + nodes + "</nodes></state_machine></root>";
}
std::string blendTree(const char* name, const char* output, const std::string& nodes) {
    return std::string("<root><blend_tree><validated>true</validated><name>") + name + "</name><output_node>" + output +
           "</output_node><nodes>" + nodes + "</nodes></blend_tree></root>";
}
std::string clipNode(const std::string& name, const char* tag, const std::string& extra = "") {
    return "<node><name>" + name + "</name><type>clip</type><tag_name>" + tag + "</tag_name>" + extra + "</node>";
}

const OutputEntry* entryFor(const World& w, const GraphOutputList& out, int32_t g, const char* node) {
    for (const OutputEntry& e : out.entries())
        if (e.graph == g && e.node == w.nodeIndex(g, node)) return &e;
    return nullptr;
}

}  // namespace

int realMain();
int main() {
    try {
        return realMain();
    } catch (const std::exception& e) {
        std::cerr << "uncaught exception: " << e.what() << "\n";
        return 1;
    }
}

int realMain() {
    // ---- the output-list guard (Sec8.2 hazard) ----------------------------------
    {
        World w;
        std::string nodes;
        for (int i = 0; i < 32; ++i)
            nodes += "<node><name>m" + std::to_string(i) + "</name><type>merge</type><inputs><input><name>c" + std::to_string(i) +
                     "</name></input><input><name>" + (i == 31 ? std::string("c32") : "m" + std::to_string(i + 1)) +
                     "</name></input></inputs></node>";
        for (int i = 0; i < 33; ++i) nodes += clipNode("c" + std::to_string(i), "run");
        w.add("wide", blendTree("Wide", "m0", nodes));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        GraphInstance row = w.row(w.graph("Wide"));
        GraphOutputList out = frame(rt, row, svc, 0.0f);
        CHECK(out.size() == GraphOutputList::kCapacity);  // the 33rd clip is dropped, not written past the end
        CHECK(out.overflowGuardCount() == 1);
        CHECK(out.peak() == 32);
        // the guard on the list itself
        GraphOutputList l;
        for (int i = 0; i < 40; ++i) l.append(OutputEntry{});
        CHECK(l.size() == 32 && l.overflowGuardCount() == 8);
    }

    // ---- state machine: start, cross-fade, completion (Sec8.3 / Sec8.4) -------
    {
        World w;
        w.add("seq", stateMachine("Seq", "A",
                                  clipState("A", "run",
                                            transition("A to B", "B", 0.2f, boolCond("A Bool", true)) +
                                                transition("A to C", "C", 0.2f, boolCond("A Bool", true))) +
                                      clipState("B", "walk") + clipState("C", "jump")));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        const int32_t g = w.graph("Seq");
        GraphInstance row = w.row(g);
        GraphOutputList out = frame(rt, row, svc, 0.0f);
        CHECK(out.size() == 1 && entryFor(w, out, g, "A") && entryFor(w, out, g, "A")->weight == 1.0f);
        CHECK(entryFor(w, out, g, "A")->record == 1000 + w.stateActions.lookup("run"));
        CHECK(entryFor(w, out, g, "A")->layer == 4 && entryFor(w, out, g, "A")->flags == 0);  // not looping
        // frame 1: the condition holds -> A to B starts (the FIRST in document
        // order; A to C holds too but cannot start)
        w.set(row, "A Bool", 1);
        out = frame(rt, row, svc, 0.1f);
        CHECK(out.size() == 2);
        const OutputEntry* a = entryFor(w, out, g, "A");
        const OutputEntry* b = entryFor(w, out, g, "B");
        CHECK(a && b && !entryFor(w, out, g, "C"));
        CHECK(a->weight == 1.0f && (a->flags & kOutOutgoing) && a->inputTag == 0);
        CHECK(b->weight == 0.0f && (b->flags & kOutZeroedInTransition) && b->inputTag == 1);  // p = 0 on the start frame
        // frame 2: halfway
        out = frame(rt, row, svc, 0.1f);
        a = entryFor(w, out, g, "A");
        b = entryFor(w, out, g, "B");
        CHECK(a && b && feq(a->weight, 0.5f) && feq(b->weight, 0.5f));
        CHECK(feq(b->clipTime, 0.1f));  // the target's clock started with the transition
        // frame 3: complete; B current at full weight; A's instance freed
        out = frame(rt, row, svc, 0.1f);
        CHECK(out.size() == 1);
        b = entryFor(w, out, g, "B");
        CHECK(b && b->weight == 1.0f && b->flags == 0 && feq(b->clipTime, 0.2f) && feq(b->prevClipTime, 0.1f));
        bool aLive = false;
        for (const NodeInstance& x : row.nodeInstances) aLive |= (!x.isFree() && x.node == w.nodeIndex(g, "A"));
        CHECK(!aLive);
        // a frozen row (U1 freeze bit) starts nothing
        GraphInstance frozen = w.row(g);
        frozen.flags48 |= 0x02u;
        w.set(frozen, "A Bool", 1);
        frame(rt, frozen, svc, 0.0f);
        out = frame(rt, frozen, svc, 0.1f);
        CHECK(out.size() == 1 && entryFor(w, out, g, "A"));
    }

    // ---- zero-length transitions chain within one frame; root-motion sync -----
    {
        World w;
        w.add("chain", stateMachine("Chain", "P",
                                    clipState("P", "run", transition("P to Q", "Q", 0.0f, boolCond("A Bool", true))) +
                                        clipState("Q", "walk", transition("Q to R", "R", 0.0f, boolCond("A Bool", true))) +
                                        clipState("R", "jump", transition("R to P", "P", 0.5f, boolCond("B Bool", true),
                                                                          "<sync_controller_transform>x</sync_controller_transform>"))));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        const int32_t g = w.graph("Chain");
        GraphInstance row = w.row(g);
        frame(rt, row, svc, 0.0f);
        w.set(row, "A Bool", 1);
        GraphOutputList out = frame(rt, row, svc, 0.1f);
        CHECK(out.size() == 1 && entryFor(w, out, g, "R") && entryFor(w, out, g, "R")->weight == 1.0f);
        CHECK(row.rootMotionSync1c == 0);
        w.set(row, "A Bool", 0);
        w.set(row, "B Bool", 1);
        out = frame(rt, row, svc, 0.1f);  // R to P starts
        CHECK(row.rootMotionSync1c == 1 && row.rootMotionLength24 == 0.5f && row.rootMotionElapsed20 == 0.0f);
        out = frame(rt, row, svc, 0.1f);
        CHECK(row.rootMotionSync1c == 1 && feq(row.rootMotionElapsed20, 0.1f));
    }

    // ---- cancel window, rewind, removal; fire-and-forget; the wrap window ------
    {
        World w;
        const std::string cancelable = "<cancelable_before>0.15</cancelable_before>";
        w.add("cancel", stateMachine("Cancel", "X",
                                     clipState("X", "run", transition("X to Y", "Y", 0.3f, boolCond("A Bool", true), cancelable)) +
                                         clipState("Y", "walk")));
        w.add("forget", stateMachine("Forget", "X",
                                     clipState("X", "run", transition("X to Y", "Y", 0.3f, boolCond("A Bool", true))) +
                                         clipState("Y", "walk")));
        w.add("wrap", stateMachine("Wrap", "X",
                                   clipState("X", "run", transition("X to Y", "Y", 0.3f, boolCond("A Bool", true),
                                                                    "<cancelable_after>0.2</cancelable_after><cancelable_before>0.05</cancelable_before>")) +
                                       clipState("Y", "walk")));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        {
            const int32_t g = w.graph("Cancel");
            GraphInstance row = w.row(g);
            frame(rt, row, svc, 0.0f);
            w.set(row, "A Bool", 1);
            frame(rt, row, svc, 0.1f);  // starts, elapsed 0
            frame(rt, row, svc, 0.1f);  // elapsed 0.1
            w.set(row, "A Bool", 0);
            GraphOutputList out = frame(rt, row, svc, 0.1f);  // elapsed 0.2 > 0.15: outside the window, keeps running
            CHECK(feq(entryFor(w, out, g, "Y")->weight, 2.0f / 3.0f));
            out = frame(rt, row, svc, 0.1f);  // completes
            CHECK(out.size() == 1 && entryFor(w, out, g, "Y"));
        }
        {
            const int32_t g = w.graph("Cancel");
            GraphInstance row = w.row(g);
            frame(rt, row, svc, 0.0f);
            w.set(row, "A Bool", 1);
            frame(rt, row, svc, 0.1f);  // starts
            w.set(row, "A Bool", 0);
            GraphOutputList out = frame(rt, row, svc, 0.1f);  // elapsed 0.1 in [0, 0.15]: cancelled (code 3)
            CHECK(out.size() == 2 && feq(entryFor(w, out, g, "Y")->weight, 1.0f / 3.0f));
            const int32_t xi = w.nodeIndex(g, "X");
            for (const NodeInstance& x : row.nodeInstances)
                if (!x.isFree() && x.node == xi && x.transition == -1) CHECK(x.current == -1 && x.active[0] == 0);
            out = frame(rt, row, svc, 0.1f);  // rewinds to 0 -> code 4: removed, target reset
            CHECK(out.size() == 1 && entryFor(w, out, g, "X") && entryFor(w, out, g, "X")->weight == 1.0f);
            bool yLive = false;
            for (const NodeInstance& x : row.nodeInstances) yLive |= (!x.isFree() && x.node == w.nodeIndex(g, "Y"));
            CHECK(!yLive);
        }
        {
            // restartable from where it stands
            const int32_t g = w.graph("Cancel");
            GraphInstance row = w.row(g);
            frame(rt, row, svc, 0.0f);
            w.set(row, "A Bool", 1);
            frame(rt, row, svc, 0.1f);
            frame(rt, row, svc, 0.1f);  // 0.1
            w.set(row, "A Bool", 0);
            row.timeStep3c = 0.0f;
            GraphOutputList out;
            // a zero-dt frame cancels at 0.1 without moving the clock
            rt.evaluate(row, 0, out, svc);
            rt.evaluate(row, 1, out, svc);
            w.set(row, "A Bool", 1);
            out = frame(rt, row, svc, 0.05f);  // rewinds 0.1 -> 0.05, then restarts (code 1)
            CHECK(feq(entryFor(w, out, g, "Y")->weight, 0.05f / 0.3f, 1e-4f));
        }
        {
            const int32_t g = w.graph("Forget");
            GraphInstance row = w.row(g);
            frame(rt, row, svc, 0.0f);
            w.set(row, "A Bool", 1);
            frame(rt, row, svc, 0.1f);
            w.set(row, "A Bool", 0);  // no window: fire-and-forget
            frame(rt, row, svc, 0.1f);
            frame(rt, row, svc, 0.1f);
            GraphOutputList out = frame(rt, row, svc, 0.1f);
            CHECK(out.size() == 1 && entryFor(w, out, g, "Y"));
        }
        {
            // b < a: cancelled when e <= b or e >= a
            const int32_t g = w.graph("Wrap");
            GraphInstance row = w.row(g);
            frame(rt, row, svc, 0.0f);
            w.set(row, "A Bool", 1);
            frame(rt, row, svc, 0.1f);   // starts at 0
            frame(rt, row, svc, 0.1f);   // 0.1
            w.set(row, "A Bool", 0);
            GraphOutputList out = frame(rt, row, svc, 0.05f);  // 0.15: between b and a -> keeps running
            CHECK(feq(entryFor(w, out, g, "Y")->weight, 0.5f, 1e-4f));
            out = frame(rt, row, svc, 0.05f);  // 0.2 >= a -> cancelled at 0.2
            CHECK(feq(entryFor(w, out, g, "Y")->weight, 0.2f / 0.3f, 1e-4f));
            out = frame(rt, row, svc, 0.05f);  // rewinding
            CHECK(feq(entryFor(w, out, g, "Y")->weight, 0.15f / 0.3f, 1e-4f));
        }
    }

    // ---- condition forms, local parameters, clip-time operands (Sec8.5) --------
    {
        World w;
        const std::string localFloat =
            "<condition><control_parameter><name>local</name><data_type>float</data_type><default_value>0.5</default_value>"
            "<min_value>0</min_value><max_value>0.1</max_value></control_parameter><comparison>greater</comparison>"
            "<parameter1><type>control parameter</type><source_name>speed</source_name></parameter1>"
            "<parameter2><type>local control parameter</type><source_name>local</source_name></parameter2></condition>";
        const std::string remaining =
            "<start>?@ [= 0.5</start><parameter><type>clip time remaining</type><source_name>?@</source_name></parameter>"
            "<parameter><type>float</type><source_name>0.5</source_name></parameter>";
        const std::string percent =
            "<start>?% == 1.0</start><parameter><type>clip time as percent</type><source_name>?%</source_name></parameter>"
            "<parameter><type>float</type><source_name>1.0</source_name></parameter>";
        const std::string boolOrdering =
            "<start>A Bool ] false</start><parameter><type>control parameter</type><source_name>A Bool</source_name></parameter>"
            "<parameter><type>boolean</type><source_name>false</source_name></parameter>";
        w.add("forms",
              stateMachine("Forms", "S",
                           clipState("S", "run",
                                     transition("and none", "T", 0.1f, "") + transition("or none", "T", 0.1f, "", "<or>true</or>") +
                                         transition("local", "T", 0.1f, localFloat) + transition("remaining", "T", 0.1f, "", remaining) +
                                         transition("bool order", "T", 0.1f, "", boolOrdering)) +
                               "<node><name>T</name><transitions>" + transition("percent none", "S", 0.1f, "", percent) +
                               "</transitions></node>"));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        const int32_t g = w.graph("Forms");
        const int32_t s = w.nodeIndex(g, "S"), t = w.nodeIndex(g, "T");
        GraphInstance row = w.row(g);
        CHECK(rt.transitionCondition(row, g, s, 0, svc));   // AND form with no conditions: true
        CHECK(!rt.transitionCondition(row, g, s, 1, svc));  // OR form with no conditions: false
        w.set(row, "speed", FloatBits(0.6f));
        CHECK(rt.transitionCondition(row, g, s, 2, svc));  // 0.6 > the private default 0.5 (its max 0.1 is not read)
        w.set(row, "speed", FloatBits(0.4f));
        CHECK(!rt.transitionCondition(row, g, s, 2, svc));
        w.set(row, "A Bool", 1);
        CHECK(!rt.transitionCondition(row, g, s, 4, svc));  // an ordering comparison of bools is always false
        // clip time remaining of the node's own clip (duration 1.0 - clip time);
        // with no live clip it is 0.0, so `?@ <= 0.5` holds
        CHECK(rt.transitionCondition(row, g, s, 3, svc));
        CHECK(rt.transitionCondition(row, g, t, 0, svc));  // no live clip under T: percent gives 1.0
        // S's first evaluation tests `?@` in pass 0 of the frame that creates
        // its clip instance, before any post step has written +0x38: the
        // remaining time (stale-clip duration - 0, Sec5.1 item 29) is OPEN.
        auto throwsOpen = [](auto f) {
            try {
                f();
            } catch (const OpenStateError&) {
                return true;
            }
            return false;
        };
        GraphInstance fr = w.row(g);
        fr.flags48 |= 0x02u;
        CHECK(throwsOpen([&] { frame(rt, fr, svc, 0.0f); }));
        // The operand values themselves, on a clip instance the post step has
        // written (duration 1.0 from clip index 5): remaining = 1.0 - clip time.
        GraphInstance live = w.row(g);
        NodeInstance x;
        x.graph = static_cast<int16_t>(g);
        x.node = static_cast<int16_t>(s);
        x.record = 1001;
        x.clipIndex = 5;
        x.clipTime = 0.25f;
        live.nodeInstances.push_back(x);
        CHECK(!rt.transitionCondition(live, g, s, 3, svc));  // 0.75 <= 0.5: no
        live.nodeInstances[0].clipTime = 0.5f;
        CHECK(rt.transitionCondition(live, g, s, 3, svc));   // 0.5 <= 0.5
        live.nodeInstances[0].clipIndex = -1;                // never posted
        CHECK(throwsOpen([&] { rt.transitionCondition(live, g, s, 3, svc); }));
    }

    // ---- switch: selector in the definition (Sec8.7) ---------------------------
    {
        World w;
        w.add("sw", blendTree("Sw", "S",
                              "<node><name>S</name><type>switch</type><parameter><type>control parameter</type><source_name>count</source_name>"
                              "</parameter><inputs><input><name>c0</name></input><input><name>c1</name></input><input><name>c2</name></input>"
                              "</inputs></node>" +
                                  clipNode("c0", "run") + clipNode("c1", "walk") + clipNode("c2", "jump")));
        w.add("swb", blendTree("SwB", "S",
                               "<node><name>S</name><type>switch</type><parameter><type>control parameter</type><source_name>A Bool</source_name>"
                               "</parameter><inputs><input><name>c0</name></input><input><name>c1</name></input></inputs></node>" +
                                   clipNode("c0", "run") + clipNode("c1", "walk")));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        const int32_t g = w.graph("Sw");
        GraphInstance row = w.row(g);
        w.set(row, "count", 1);
        GraphOutputList out = frame(rt, row, svc, 0.0f);
        CHECK(out.size() == 1 && entryFor(w, out, g, "c1") && entryFor(w, out, g, "c1")->inputTag == 1);
        w.set(row, "count", static_cast<uint32_t>(-1));
        CHECK(frame(rt, row, svc, 0.1f).size() == 3);  // -1 selects everything
        w.set(row, "count", 5);
        CHECK(frame(rt, row, svc, 0.1f).size() == 0);  // out of range selects nothing
        // the selector lives in the DEFINITION: shared by every instance
        GraphInstance r1 = w.row(g), r2 = w.row(g);
        w.set(r1, "count", 2);
        w.set(r2, "count", 0);
        GraphOutputList o1, o2;
        rt.evaluate(r1, 0, o1, svc);
        rt.evaluate(r2, 0, o2, svc);  // overwrites the shared selector
        rt.evaluate(r1, 1, o1, svc);
        // r1's pass 1 follows r2's choice (input 0), for which r1's pass 0 made
        // no instance or record, so it emits nothing at all - the reason the
        // engine's two passes of one row must run back to back (Sec8.7).
        CHECK(o1.size() == 0);
        CHECK(w.lib.graphs[static_cast<size_t>(g)].nodes[0].selector == 0);
        // a bool parameter selects 1 or 0
        const int32_t gb = w.graph("SwB");
        GraphInstance rb = w.row(gb);
        w.set(rb, "A Bool", 1);
        out = frame(rt, rb, svc, 0.0f);
        CHECK(out.size() == 1 && entryFor(w, out, gb, "c1"));
        // garbage collection: c1's instance is freed after two evaluations without it
        w.set(rb, "A Bool", 0);
        frame(rt, rb, svc, 0.1f);
        auto live = [&](const char* n) {
            for (const NodeInstance& x : rb.nodeInstances)
                if (!x.isFree() && x.node == w.nodeIndex(gb, n)) return true;
            return false;
        };
        CHECK(live("c1"));  // one evaluation old: kept
        frame(rt, rb, svc, 0.1f);
        CHECK(!live("c1") && live("c0"));
    }

    // ---- multi way blender ramp (Sec8.7 / Sec8.10) ------------------------------
    {
        World w;
        for (const char* rate : {"0.1", "0.4", "1.0"}) {
            w.add(std::string("mwb") + rate,
                  blendTree((std::string("Mwb ") + rate).c_str(), "M",
                            std::string("<node><name>M</name><type>multi way blender</type><parameter><type>control parameter</type>"
                                        "<source_name>speed</source_name></parameter><inputs><input><name>c</name><max_blend_rate>") +
                                rate +
                                "</max_blend_rate><control_point><range>0</range><value>0</value></control_point>"
                                "<control_point><range>1</range><value>1</value></control_point></input></inputs></node>" +
                                clipNode("c", "run")));
        }
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        // Sec8.10 (as of 2026-10-07): 3, 13 and 30-31 updates for 0.1, 0.4 and
        // 1.0 s (originally "12" for 0.4 s, corrected by Team A after this
        // project reported the float-vs-double mismatch; 12 x (1/30) / 0.4 is
        // exactly 1 only in double precision - with the float weight the 12th
        // update lands within float rounding of 1.0 (asserted) and the 13th
        // snaps it, now the spec's own number too).
        std::map<std::string, int> updates;
        std::map<std::string, float> afterNominal;
        const std::map<std::string, int> nominal = {{"0.1", 3}, {"0.4", 12}, {"1.0", 30}};
        for (const char* rate : {"0.1", "0.4", "1.0"}) {
            const int32_t g = w.graph(std::string("Mwb ") + rate);
            GraphInstance row = w.row(g);
            CHECK(frame(rt, row, svc, 0.0f)[0].weight == 0.0f);  // fresh: snaps to 0
            w.set(row, "speed", FloatBits(1.0f));
            int n = 0;
            float weight = 0.0f;
            while (n < 100 && weight != 1.0f) {
                weight = frame(rt, row, svc, 1.0f / 30.0f)[0].weight;
                ++n;
                if (n == nominal.at(rate)) afterNominal[rate] = weight;
            }
            updates[rate] = n;
        }
        std::cout << "  ramp 0 -> 1 at 1/30 s: " << updates["0.1"] << " / " << updates["0.4"] << " / " << updates["1.0"]
                  << " updates (weight after 3 / 12 / 30: " << afterNominal["0.1"] << " / " << afterNominal["0.4"] << " / "
                  << afterNominal["1.0"] << ")\n";
        CHECK(updates["0.1"] == 3);
        CHECK(updates["0.4"] == 12 || updates["0.4"] == 13);
        CHECK(feq(afterNominal["0.4"], 1.0f, 1e-6f));
        CHECK(updates["1.0"] == 30 || updates["1.0"] == 31);
        CHECK(feq(afterNominal["1.0"], 1.0f, 1e-6f));
        // a fresh instance snaps to the target at once
        const int32_t g = w.graph("Mwb 1.0");
        GraphInstance row = w.row(g);
        w.set(row, "speed", FloatBits(0.25f));
        CHECK(feq(frame(rt, row, svc, 0.0f)[0].weight, 0.25f));
        // curve: data order, first containing segment, 0 outside
        BlendInput in;
        in.pointCount = 5;
        const float r[5] = {-20, 0, 20, -100, 100}, v[5] = {1, 0, 0, 1, 0};
        for (int i = 0; i < 5; ++i) in.range[i] = r[i], in.value[i] = v[i];
        CHECK(feq(MultiWayCurve(in, -60), 0.8f) && feq(MultiWayCurve(in, -10), 0.5f) && MultiWayCurve(in, 150) == 0.0f);
        in.pointCount = 1;
        CHECK(MultiWayCurve(in, -20) == 0.0f);  // a single point gives 0
    }

    // ---- filters: the double evaluation of a listed source (Sec8.8) ------------
    {
        World w;
        auto mwbOn = [](const char* name, const char* filter) {
            return blendTree(name, "M",
                             std::string("<node><name>M</name><type>multi way blender</type><parameter><type>control filter</type>"
                                         "<source_name>") + filter +
                                 "</source_name></parameter><inputs><input><name>c</name><control_point><range>0</range><value>1</value>"
                                 "</control_point></input></inputs></node>" + clipNode("c", "run"));
        };
        w.add("smooth", mwbOn("Smooth", "Speed Smooth"));
        w.add("capped", mwbOn("Capped", "Speed Cap"));
        w.load();
        CHECK(w.lib.graphs[0].filterIds == (std::vector<int16_t>{1, 0}));
        CHECK(w.lib.graphs[1].filterIds == (std::vector<int16_t>{2, 1, 0}));
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        GraphInstance once = w.row(0), twice = w.row(1);
        w.set(once, "speed", FloatBits(10.0f));  // Speed Map -> 1.0: a unit step
        w.set(twice, "speed", FloatBits(10.0f));
        const float wantOnce[3] = {0.5f, 0.75f, 0.875f}, wantTwice[3] = {0.75f, 0.9375f, 0.984375f};
        for (int f = 0; f < 3; ++f) {
            frame(rt, once, svc, 1.0f / 30.0f);
            frame(rt, twice, svc, 1.0f / 30.0f);
            CHECK(feq(BitsFloat(once.filterOutputs[0]), wantOnce[f], 1e-6f));
            CHECK(feq(BitsFloat(twice.filterOutputs[1]), wantTwice[f], 1e-6f));
        }
        CHECK(BitsFloat(twice.filterOutputs[0]) == 0.5f);  // the cap
        // the kinds' math
        FilterDef lm;
        lm.kind = FilterKind::LinearMap;
        lm.inputMin = 2, lm.inputMax = 1, lm.outputMin = 0, lm.outputMax = 10;  // input_max < input_min: c = input_max
        CHECK(FilterMath(lm, 100.0f, 0, 0) == FilterMath(lm, -100.0f, 0, 0));
        FilterDef cap;
        cap.kind = FilterKind::Cap;
        cap.outputMin = 5, cap.outputMax = 1;  // output_min > output_max -> output_max
        CHECK(FilterMath(cap, 3.0f, 0, 0) == 1.0f);
        FilterDef mul;
        mul.kind = FilterKind::Multiplier;
        mul.basis = 1, mul.multiplier = 3;
        CHECK(FilterMath(mul, 2.0f, 0, 0) == 4.0f);
    }

    // ---- clip blocks (Sec8.6) and the named clip-node search (U1 Sec6.4) ------
    {
        World w;
        w.add("clips", blendTree("Clips", "m",
                                 "<node><name>m</name><type>merge</type><inputs><input><name>named</name></input>"
                                 "<input><name>m2</name></input></inputs></node>"
                                 "<node><name>m2</name><type>merge</type><inputs><input><name>empty</name></input>"
                                 "<input><name>matched</name></input></inputs></node>"
                                 "<node><name>named</name><type>clip</type><clip_name>some_anim</clip_name><alias_name>Fire</alias_name>"
                                 "<loop_flag>true</loop_flag></node>"
                                 "<node><name>empty</name><type>clip</type><tag_name>no such state</tag_name></node>" +
                                     clipNode("matched", "run", "<match_name>walk_fire</match_name><layer>2</layer>")));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        Services svc;
        const int32_t g = w.graph("Clips");
        GraphInstance r1 = w.row(g), r2 = w.row(g);
        GraphOutputList out = frame(rt, r1, svc, 0.0f);
        CHECK(out.size() == 2);  // `empty` (unresolved tag, no clip_name) emits nothing in either pass
        const OutputEntry* named = entryFor(w, out, g, "named");
        CHECK(named && named->record == 777 && named->tagIndex == -1 && (named->flags & kOutLoop) && named->inputTag == 0);
        const OutputEntry* matched = entryFor(w, out, g, "matched");
        CHECK(matched && matched->layer == 2 && matched->inputTag == 1);
        CHECK(r1.flags48 & 0x04u);  // a match_name clip was emitted
        frame(rt, r2, svc, 0.0f);
        CHECK(svc.clipNameLookups == 1);  // the clip_name record is cached per DEFINITION
        // clip time is clamped to the duration (1.0) and carried for non-looping clips only
        for (int f = 0; f < 15; ++f) out = frame(rt, r1, svc, 0.1f);
        CHECK(feq(entryFor(w, out, g, "matched")->clipTime, 1.0f) && entryFor(w, out, g, "named")->clipTime == 0.0f);
        // named clip-node search: alias Fire = 7,589 + 12
        NodeGraphEvaluator ev(rt, &svc);
        const int32_t found = ev.findNamedClipNode(r1, kIdClipNodeFirst + 12, 0);
        CHECK(found >= 0 && r1.nodeInstances[static_cast<size_t>(found)].node == w.nodeIndex(g, "named"));
        CHECK(ev.findNamedClipNode(r1, kIdClipNodeFirst + 12, found + 1) == -1);
        CHECK(ev.findNamedClipNode(r1, kIdClipNodeFirst + 4, 0) == -1);
        // nothing at all while row +0x30 is 0
        GraphInstance r3 = w.row(g);
        r3.animGroupTable30 = 0;
        CHECK(frame(rt, r3, svc, 0.1f).size() == 0 && r3.nodeInstances.empty() && r3.evalCounter44 == 0);
    }

    // ---- the Controller integration: U1's graph step through the evaluator ----
    {
        World w;
        w.add("seq", stateMachine("Seq", "A",
                                  clipState("A", "run", transition("A to B", "B", 0.0f, boolCond("A Bool", true))) + clipState("B", "walk")));
        w.load();
        const NodeGraphRuntime rt(w.lib, w.registry, w.filters);
        struct Data : AnimDataSource {
            const NodeGraphLibrary* lib;
            std::optional<AnimRecordInfo> record(int32_t i) const override {
                if (i != 0) return std::nullopt;
                AnimRecordInfo r;
                r.flags = kRecManaged;
                r.index08 = 0;  // graph 0
                return r;
            }
            int32_t stateToRecord(int32_t s) const override { return s == 5 ? 0 : -1; }
            std::optional<ClipTiming> clipTiming(int32_t) const override { return ClipTiming{30, 0, 0}; }
            const BlendTreeRowInfo* blendTreeRow(int32_t) const override { return nullptr; }
            const GraphAliasTable* graphAliases(int32_t g) const override { return &lib->graphs[static_cast<size_t>(g)].aliases; }
        } data;
        data.lib = &w.lib;
        struct Layers : LayerApi {
            void bumpInstance() override {}
            bool poseInFlight() const override { return false; }
            int32_t createClipLayer(int32_t, uint32_t, float, float, float) override { return 16; }
            int32_t postClip(int32_t, uint32_t, float, float, float) override { return 16; }
            void fadeOutLayer(int32_t, float) override {}
            void setLayerRate(int32_t, float) override {}
            void setLayerPhase(int32_t, float) override {}
            float layerWeight(int32_t) const override { return 1.0f; }
            bool layerGone(int32_t) const override { return false; }
            std::optional<int32_t> heldLayerHandle() const override { return std::nullopt; }
            bool triggerFired(int32_t, int32_t, int32_t) const override { return false; }
            bool triggerFiredAnyLayer(int32_t, int32_t) const override { return false; }
        } layers;
        struct Eval : NodeGraphEvaluator {
            using NodeGraphEvaluator::NodeGraphEvaluator;
            std::vector<std::vector<int32_t>> posted;  // the records of each posted list
            int collects = 0;
            GraphStepOutput postOutput(Controller&, int32_t, GraphInstance&, const GraphOutputList& out, ClipDedupList&) override {
                std::vector<int32_t> recs;
                for (const OutputEntry& e : out.entries()) recs.push_back(e.record);
                posted.push_back(recs);
                return GraphStepOutput{true, 48};
            }
            void collectGraphClipNodes(GraphInstancePool&) override { ++collects; }
        };
        Services svc;
        Eval ev(rt, &svc);
        GraphInstancePool pool;
        Controller::Environment env;
        env.registry = &w.registry;
        env.data = &data;
        env.graphPool = &pool;
        env.layers = &layers;
        env.instanceHandle = 3;
        env.evaluator = &ev;
        Controller c(env);
        c.animGroupTable30.set(1);
        c.animGroupTable34.set(0);
        c.skeletonSet44.set(0);
        c.timeStep2c.set(1.0f / 30.0f);
        c.playState(5, 7500, 0, 1.0f, 0.0f, 0.0f);  // managed route: graph row instanced
        CHECK(pool.liveCount() == 1);
        c.update();
        CHECK(ev.posted.size() == 1 && ev.posted[0] == std::vector<int32_t>{1000 + w.stateActions.lookup("run")});
        CHECK(c.slots()[0].layerHandle == 48 && c.slots()[0].weight == 1.0f);
        // the setter reaches the graph through U1's per-frame copy
        c.setBool(w.registry.resolve("A Bool"), true);
        c.update();
        CHECK(ev.posted.size() == 2 && ev.posted[1] == std::vector<int32_t>{1000 + w.stateActions.lookup("walk")});
        CHECK(ev.collects == 2);
        // the default post step is OPEN
        NodeGraphEvaluator plain(rt, &svc);
        GraphInstance r = w.row(0);
        ClipDedupList clips;
        bool threw = false;
        try {
            plain.evaluateGraphAndPost(c, 0, r, clips);
        } catch (const OpenStateError&) {
            threw = true;
        }
        CHECK(threw);
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_anim_node_graph_runtime_test: all checks passed\n";
    return 0;
}
