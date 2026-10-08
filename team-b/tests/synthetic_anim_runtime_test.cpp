// Synthetic tests for the animation runtime units U0 (control-parameter
// contract) and U1 (controller state slots, play / query API, cross-fade),
// spec-anim-runtime.md Sec1-Sec6. Every expected value is a rule quoted from
// that spec (cited per block) or hand-computed from its formulas; the data
// (records, clips, layers, graph aliases) comes from in-test fakes, not from
// the game. The shipped-table checks are tests/real_anim_control_parameters_test.cpp.
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3animruntime/control_parameters.h"
#include "sr3animruntime/controller.h"
#include "sr3animruntime/crossfade.h"
#include "sr3luahost/open_state.h"

using namespace sr3animruntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool feq(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

template <class F>
bool throwsOpen(F&& f, const std::string& needle = {}) {
    try {
        f();
    } catch (const sr3luahost::OpenStateError& e) {
        return needle.empty() || std::string(e.what()).find(needle) != std::string::npos;
    } catch (...) {
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// A small registry exercising every row-reader rule of Sec1.2 / Sec1.3.
// ---------------------------------------------------------------------------
const char* kRegistryXml =
    "<root><control_parameters>"
    "<control_parameter><name>combat ready</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>"  // 0
    "<control_parameter><name>direction</name><data_type>float</data_type><default_value>0.000000</default_value>"
    "<min_value>0.000000</min_value><max_value>6.283185</max_value></control_parameter>"  // 1
    "<control_parameter><name>Climb Duration ms</name><data_type>integer</data_type><default_value>0</default_value>"
    "<min_value>0</min_value><max_value>10000</max_value></control_parameter>"  // 2
    "<control_parameter><name>Supported By Ground</name><data_type>bool</data_type><default_value>true</default_value></control_parameter>"  // 3
    "<control_parameter><name>shouty</name><data_type>BOOL</data_type><default_value>FALSE</default_value></control_parameter>"  // 4
    "<control_parameter><name>yes bool</name><data_type>bool</data_type><default_value>yes</default_value></control_parameter>"  // 5
    "<control_parameter><name>missing default</name><data_type>bool</data_type></control_parameter>"  // 6
    "<control_parameter><name>typeless</name><default_value>3</default_value></control_parameter>"  // 7
    "<control_parameter><name>weird type</name><data_type>double</data_type><default_value>2</default_value></control_parameter>"  // 8
    "<control_parameter><data_type>float</data_type><default_value>1.5</default_value></control_parameter>"  // 9
    "<control_parameter><name>neg int</name><data_type>Integer</data_type><default_value>-5</default_value>"
    "<min_value>-10</min_value><max_value>0x10</max_value></control_parameter>"  // 10
    "<control_parameter><name>Combat Ready</name><data_type>float</data_type></control_parameter>"  // 11
    "<control_parameter><name>expo</name><data_type>float</data_type><default_value>1e2</default_value>"
    "<min_value>.5</min_value><max_value>-2.25</max_value></control_parameter>"  // 12
    "</control_parameters></root>";

// ===================================================================
// U0 Sec1 - registry
// ===================================================================
void testRegistryLoad() {
    // Unloaded = manifest slot 12 returned nothing (Sec1.1).
    {
        ControlParameterRegistry r;
        CHECK(!r.loaded() && r.count() == 0);
        CHECK(r.resolve("combat ready") == -1);
        CHECK(r.record(0) == nullptr && r.record(-1) == nullptr);
    }
    const ControlParameterRegistry r = ControlParameterRegistry::loadXml(kRegistryXml);
    CHECK(r.loaded());
    CHECK(r.count() == 13);
    // Id = 0-based row index in document order (Sec1.1).
    CHECK(r.resolve("direction") == 1);
    CHECK(r.resolve("Climb Duration ms") == 2);
    // Case-insensitive, first match wins (Sec2.1): row 11 "Combat Ready" is shadowed.
    CHECK(r.resolve("COMBAT READY") == 0);
    CHECK(r.resolve("no such parameter") == -1);
    CHECK(r.record(-1) == nullptr && r.record(13) == nullptr && r.record(12) != nullptr);

    // Type codes (Sec1.3): case-insensitive table match; absent / unrecognised -> 0.
    CHECK(r.record(0)->type == ParamType::Bool);
    CHECK(r.record(1)->type == ParamType::Float);
    CHECK(r.record(2)->type == ParamType::Integer);
    CHECK(r.record(4)->type == ParamType::Bool);   // "BOOL"
    CHECK(r.record(10)->type == ParamType::Integer);  // "Integer"
    CHECK(r.record(7)->type == ParamType::None);   // no data_type
    CHECK(r.record(8)->type == ParamType::None);   // "double"
    // Missing name -> "none" (Sec1.2).
    CHECK(r.record(9)->name == "none");
    CHECK(r.resolve("none") == 9);

    // Bool defaults (Sec1.2): only the literal "false" (any case) gives 0.
    CHECK(r.record(0)->defaultBool() == false);
    CHECK(r.record(3)->defaultBool() == true);
    CHECK(r.record(4)->defaultBool() == false);  // "FALSE"
    CHECK(r.record(5)->defaultBool() == true);   // "yes" is not "false"
    CHECK(r.record(6)->defaultBool() == true);   // missing reads as "none"
    CHECK(r.record(0)->minBool() == false && r.record(0)->maxBool() == true);
    CHECK(r.record(3)->defaultDword == 1u);

    // Float rows (atof, 0.0 when absent).
    CHECK(r.record(1)->defaultFloat() == 0.0f && r.record(1)->minFloat() == 0.0f);
    CHECK(r.record(1)->maxFloat() == static_cast<float>(6.283185));
    CHECK(r.record(9)->defaultFloat() == 1.5f && r.record(9)->minFloat() == 0.0f && r.record(9)->maxFloat() == 0.0f);
    CHECK(r.record(12)->defaultFloat() == 100.0f);  // atof accepts an exponent
    CHECK(r.record(12)->minFloat() == 0.5f && r.record(12)->maxFloat() == -2.25f);
    // Integer rows.
    CHECK(r.record(2)->defaultInt() == 0 && r.record(2)->minInt() == 0 && r.record(2)->maxInt() == 10000);
    CHECK(r.record(10)->defaultInt() == -5 && r.record(10)->minInt() == -10 && r.record(10)->maxInt() == 16);

    // Type 0: nothing written -> OPEN; wrong-type typed view -> logic_error.
    CHECK(!r.record(7)->valuesWritten);
    CHECK(throwsOpen([&] { (void)r.record(7)->defaultInt(); }));
    bool logic = false;
    try { (void)r.record(1)->defaultBool(); } catch (const std::logic_error&) { logic = true; }
    CHECK(logic);

    // A missing control_parameters element -> empty registry (CHOSEN).
    const ControlParameterRegistry empty = ControlParameterRegistry::loadXml("<root><other/></root>");
    CHECK(empty.loaded() && empty.count() == 0);
}

// ===================================================================
// U0 Sec2.2 - graph-scoped resolver
// ===================================================================
void testGraphAliases() {
    const ControlParameterRegistry r = ControlParameterRegistry::loadXml(kRegistryXml);
    GraphAliasTable g;
    CHECK(g.resolveLocal("direction", r) == -1);  // +0x20 null
    g.present = true;
    g.globalIds = {3, -1, 99, 1, 2};  // -1 and 99 are invalid aliases: skipped, not dereferenced
    CHECK(g.resolveLocal("DIRECTION", r) == 3);
    CHECK(g.resolveLocal("Supported By Ground", r) == 0);
    CHECK(g.resolveLocal("combat ready", r) == -1);  // the graph does not use it
    CHECK(g.aliasToGlobal(3) == 1 && g.aliasToGlobal(5) == -1 && g.aliasToGlobal(-1) == -1);
    CHECK(g.globalToAlias(2) == 4 && g.globalToAlias(0) == -1);
    CHECK(ResolveGraphScoped(g, "Climb Duration ms", r) == 2);
    CHECK(ResolveGraphScoped(g, "combat ready", r) == -1);
}

// ===================================================================
// Fakes for the U1 boundaries
// ===================================================================
struct FakeData : AnimDataSource {
    std::map<int32_t, AnimRecordInfo> records;
    std::map<int32_t, int32_t> states;
    std::map<int32_t, ClipTiming> clips;
    std::map<int32_t, BlendTreeRowInfo> trees;
    std::map<int32_t, GraphAliasTable> graphs;
    std::optional<AnimRecordInfo> record(int32_t i) const override {
        auto it = records.find(i);
        if (it == records.end()) return std::nullopt;
        return it->second;
    }
    int32_t stateToRecord(int32_t s) const override {
        auto it = states.find(s);
        return it == states.end() ? -1 : it->second;
    }
    std::optional<ClipTiming> clipTiming(int32_t c) const override {
        auto it = clips.find(c);
        if (it == clips.end()) return std::nullopt;
        return it->second;
    }
    const BlendTreeRowInfo* blendTreeRow(int32_t row) const override {
        auto it = trees.find(row);
        return it == trees.end() ? nullptr : &it->second;
    }
    const GraphAliasTable* graphAliases(int32_t g) const override {
        auto it = graphs.find(g);
        return it == graphs.end() ? nullptr : &it->second;
    }
};

struct FakeLayers : LayerApi {
    struct Post { int32_t clip; uint32_t flags; float weight; float a, b; };
    struct Create { int32_t clip; uint32_t flags; float f1, f2, f3; int32_t handle; };
    int32_t serial = 0x1000;  // handles >= 7,603 so they land in the finder's last partition
    int bumps = 0;
    bool inFlight = false;
    std::optional<int32_t> held;
    std::set<int32_t> gone;
    std::map<int32_t, float> weights, fades, rates, phases;
    std::map<int32_t, int32_t> clipLayer;
    std::vector<Post> posts;
    std::vector<Create> creates;
    std::map<int32_t, bool> triggers;
    bool anyTrigger = false;
    int32_t next() { return (serial++) << 4; }
    void bumpInstance() override { ++bumps; }
    bool poseInFlight() const override { return inFlight; }
    int32_t createClipLayer(int32_t clip, uint32_t flags, float f1, float f2, float f3) override {
        const int32_t h = next();
        creates.push_back({clip, flags, f1, f2, f3, h});
        return h;
    }
    int32_t postClip(int32_t clip, uint32_t flags, float w, float a, float b) override {
        posts.push_back({clip, flags, w, a, b});
        auto it = clipLayer.find(clip);
        if (it == clipLayer.end()) it = clipLayer.emplace(clip, next()).first;
        return it->second;
    }
    void fadeOutLayer(int32_t h, float s) override { fades[h] = s; }
    void setLayerRate(int32_t h, float r) override { rates[h] = r; }
    void setLayerPhase(int32_t h, float p) override { phases[h] = p; }
    std::map<int32_t, float> aligned;
    float phaseReadBack = 0.25f;
    void alignLayerToPhase(int32_t h, float p) override { aligned[h] = p; }
    float readLayerPhase(int32_t) const override { return phaseReadBack; }
    float layerWeight(int32_t h) const override {
        auto it = weights.find(h);
        return it == weights.end() ? 0.0f : it->second;
    }
    bool layerGone(int32_t h) const override { return gone.count(h) != 0; }
    std::optional<int32_t> heldLayerHandle() const override { return held; }
    bool triggerFired(int32_t h, int32_t, int32_t) const override {
        auto it = triggers.find(h);
        return it != triggers.end() && it->second;
    }
    bool triggerFiredAnyLayer(int32_t, int32_t) const override { return anyTrigger; }
};

struct FakeEval : EvaluationBoundary {
    std::vector<std::vector<uint32_t>> seen;  // instance values at each evaluation
    bool produce = true;
    int32_t dominant = 0x7770;
    int collects = 0;
    bool bumpCounter = false;
    std::map<int32_t, int32_t> nodeForId;  // id -> node index
    int blendTreeCalls = 0;
    int snaps = 0;
    GraphStepOutput evaluateGraphAndPost(Controller&, int32_t, GraphInstance& row, ClipDedupList&) override {
        std::vector<uint32_t> v;
        for (const ParamSlot& p : row.values) v.push_back(p.dword0);
        seen.push_back(v);
        if (bumpCounter) ++row.evalCounter44;
        GraphStepOutput o;
        o.producedOutput = produce;
        o.dominantLayerHandle = dominant;
        return o;
    }
    void collectGraphClipNodes(GraphInstancePool&) override { ++collects; }
    int32_t findNamedClipNode(const GraphInstance&, int32_t id, int32_t firstNode) override {
        auto it = nodeForId.find(id);
        if (it == nodeForId.end() || it->second < firstNode) return -1;
        return it->second;
    }
    void evaluateBlendTree(Controller&, int32_t, ClipDedupList&) override { ++blendTreeCalls; }
    void snapBlendTreeStateWeights(Controller&, int32_t) override { ++snaps; }
};

// Records (index -> clip / row / graph), states and clips shared by the tests.
FakeData makeData() {
    FakeData d;
    auto rec = [&](int32_t i, uint32_t flags, int32_t idx08) {
        AnimRecordInfo r;
        r.flags = flags;
        r.index08 = idx08;
        d.records[i] = r;
    };
    rec(1, 0, 101);
    rec(2, 0, 102);
    rec(3, 0, 103);
    rec(4, kRecBlendTree, 5);
    rec(5, kRecManaged, 7);
    rec(6, 0, 106);                     // transition clip
    rec(7, kRecTransitionAction, 107);  // phase-synchronised action
    rec(8, kRecManaged | kRecAdditive, 7);
    rec(9, 0, 109);
    d.records[9].byte27 = 1;            // a re-trigger record (none ship, Sec6.8)
    rec(10, kRecBlendTree, 5);          // same blend-tree row as record 4
    rec(11, kRecManaged, 8);
    rec(12, 0, 101);                    // same clip as record 1
    rec(13, kRecAdditive | kRecNoInterpolation, 103);
    for (int32_t s = 1; s <= 13; ++s) d.states[s * 100] = s;
    d.clips[101] = ClipTiming{60, 2, 3};  // 2.0 s, blend-out 3 frames
    d.clips[102] = ClipTiming{30, 0, 0};  // 1.0 s, blend-out 0
    d.clips[103] = ClipTiming{90, 0, 0};
    d.clips[106] = ClipTiming{18, 0, 0};  // 0.6 s
    d.clips[107] = ClipTiming{30, 0, 6};  // blend-out 6 frames = 0.2 s
    d.clips[109] = ClipTiming{30, 0, 0};
    BlendTreeRowInfo bt;
    bt.controlInputLow = -1.0f;
    bt.controlInputHigh = 4.0f;
    bt.stateClips = {101, 102};
    bt.rampSpeed = 30.0f;  // the input moves 1.0 per update (Ramp_speed x 1/30)
    BlendTreeRowInfo::StateCurve s0, s1;
    s0.blendTime = 0.0f;  // snaps
    s0.pointCount = 2;
    s0.range[0] = -1, s0.range[1] = 4, s0.value[0] = 1, s0.value[1] = 0;
    s1.blendTime = 0.1f;  // (1/30) / 0.1 = 1/3 per update
    s1.pointCount = 2;
    s1.range[0] = -1, s1.range[1] = 4, s1.value[0] = 0, s1.value[1] = 1;
    bt.stateCurves = {s0, s1};
    d.trees[5] = bt;
    GraphAliasTable g7;
    g7.present = true;
    g7.globalIds = {3, 1, -1, 2};  // Supported By Ground, direction, unresolved, Climb Duration ms
    d.graphs[7] = g7;
    GraphAliasTable g8;
    g8.present = true;
    g8.globalIds = {1};
    d.graphs[8] = g8;
    return d;
}

struct Rig {
    ControlParameterRegistry reg = ControlParameterRegistry::loadXml(kRegistryXml);
    FakeData data = makeData();
    GraphInstancePool pool;
    FakeLayers layers;
    FakeEval eval;
    Controller::Environment env(bool withEval = true, bool withLayers = true) {
        Controller::Environment e;
        e.registry = &reg;
        e.data = &data;
        e.graphPool = &pool;
        e.layers = withLayers ? &layers : nullptr;
        e.instanceHandle = 42;
        e.evaluator = withEval ? &eval : nullptr;
        return e;
    }
};

// ===================================================================
// U0 Sec3.1-Sec3.4 - block, setters, child ring
// ===================================================================
void testSetters() {
    Rig rig;
    Controller parent(rig.env()), child(rig.env()), grandchild(rig.env()), lone(rig.env());
    parent.addChild(&child);
    child.addChild(&grandchild);

    // No block: the write is dropped BEFORE the ring (Sec3.2 step 1).
    child.allocateParameterBlock();
    parent.setFloat(1, 2.5f);
    CHECK(!parent.getFloat(1));
    CHECK(child.getFloat(1) && *child.getFloat(1) == 0.0f);

    // Lazy block is zero-filled, 8 bytes x count (Sec3.4).
    parent.allocateParameterBlock();
    CHECK(parent.parameterBlock()->size() == 13);
    for (const ParamSlot& s : *parent.parameterBlock()) CHECK(s.dword0 == 0 && s.dword1 == 0);
    // Defaults are NOT applied to the block (Sec3.4): Supported By Ground reads false.
    CHECK(parent.getBool(3) && *parent.getBool(3) == false);

    // Round trips per type.
    parent.setBool(0, true);
    parent.setFloat(1, 3.25f);
    parent.setInt(2, 7500);
    CHECK(*parent.getBool(0) == true && *parent.getFloat(1) == 3.25f && *parent.getInt(2) == 7500);
    CHECK((*parent.parameterBlock())[0].dword0 == 1u);
    CHECK((*parent.parameterBlock())[1].dword1 == 0u);  // bytes +4..+7 never written
    // No clamp to min/max (Sec3.2): 12345 > 10000, 99 > 2pi.
    parent.setInt(2, 12345);
    parent.setFloat(1, 99.0f);
    CHECK(*parent.getInt(2) == 12345 && *parent.getFloat(1) == 99.0f);

    // Child ring: depth-first replay (Sec3.2 step 2). The grandchild has no
    // block, so it drops the write (and would not forward to its own ring).
    CHECK(child.getBool(0) && *child.getBool(0) == true);
    CHECK(child.getInt(2) && *child.getInt(2) == 12345);
    CHECK(!grandchild.getBool(0));
    grandchild.allocateParameterBlock();
    parent.setBool(3, true);
    CHECK(*grandchild.getBool(3) == true && *child.getBool(3) == true);

    // Type-mismatched, -1 and out-of-range writes are silently dropped (step 3).
    parent.setFloat(0, 1.0f);   // float write to a bool row
    parent.setBool(1, true);    // bool write to a float row
    parent.setInt(1, 5);        // int write to a float row
    parent.setInt(7, 5);        // type-0 row: no setter accepts it
    parent.setBool(-1, true);
    parent.setBool(200, true);
    CHECK(*parent.getBool(0) == true && *parent.getFloat(1) == 99.0f);
    CHECK(!parent.getInt(7));
    CHECK((*parent.parameterBlock())[7].dword0 == 0u);

    // Resolve-then-set, as the engine writers do (Sec4.1).
    lone.allocateParameterBlock();
    lone.setInt(rig.reg.resolve("Climb Duration ms"), 250);
    CHECK(*lone.getInt(2) == 250);
}

// ===================================================================
// Sec6.7 step 2 - cross-fade as pure functions
// ===================================================================
void testCrossFadePure() {
    // 2.1 base rate.
    CHECK(CrossFadeBaseRate(0.0f) == 1.0f);
    CHECK(feq(CrossFadeBaseRate(0.5f), (1.0 / 30.0) / 0.5, 1e-7));
    CHECK(feq(CrossFadeBaseRate(1.0f), 1.0 / 30.0, 1e-7));
    CHECK(feq(CrossFadeBaseRate(0.25f), (1.0 / 30.0) / 0.25, 1e-7));
    CHECK(feq(CrossFadeBaseRate(2.0f), (1.0 / 30.0) / 2.0, 1e-7));

    // 2.3 crowding factor: x1.3 / x2.3 / x3.7 at n = 3 / 4 / 5; 1 below 3.
    CHECK(CrossFadeCrowdingFactor(1) == 1.0 && CrossFadeCrowdingFactor(2) == 1.0);
    CHECK(feq(CrossFadeCrowdingFactor(3), 1.3));
    CHECK(feq(CrossFadeCrowdingFactor(4), 2.3));
    CHECK(feq(CrossFadeCrowdingFactor(5), 3.7));
    // The constant is the double nearest float 0.2, not 0.2 itself.
    CHECK(CrossFadeCrowdingFactor(3) == 0.5 + static_cast<double>(0.2f) * 4.0);
    CHECK(feq(CrossFadeRate(1.0f, 3, std::nullopt), (1.0 / 30.0) * 1.3, 1e-7));
    CHECK(feq(CrossFadeRate(0.5f, 5, std::nullopt), (1.0 / 15.0) * 3.7, 1e-6));

    // 2.2 restart override (needs > 1 member), then crowding on top.
    CHECK(CrossFadeRate(0.5f, 2, 0.5f) == 0.0001f);
    CHECK(CrossFadeRate(0.5f, 2, 1.0f) == 100.0f);
    CHECK(feq(CrossFadeRate(0.5f, 1, 0.5f), (1.0 / 30.0) / 0.5, 1e-7));  // n = 1: no override
    CHECK(feq(CrossFadeRate(0.5f, 3, 0.2f), 0.0001 * 1.3, 1e-9));

    // 0x004afe50 order: descending, a later equal weight after an earlier one.
    CHECK((CrossFadeSortOrder({0.2f, 0.5f, 0.2f, 0.9f}) == std::vector<size_t>{3, 1, 0, 2}));

    auto sum = [](const std::vector<float>& w) { double s = 0; for (float x : w) s += x; return s; };

    // One other member: it takes the whole step.
    {
        std::vector<float> w = {0.5f, 0.5f};
        CrossFadeRedistribute(w, 1, 0.1f);
        CHECK(feq(w[1], 0.6) && feq(w[0], 0.4) && feq(sum(w), 1.0));
    }
    // Two others: the lighter takes r/2 (k = 1), the heavier the rest.
    {
        std::vector<float> w = {0.4f, 0.4f, 0.2f};  // current = 0
        CrossFadeRedistribute(w, 0, 0.1f);
        CHECK(feq(w[0], 0.5) && feq(w[1], 0.35) && feq(w[2], 0.15) && feq(sum(w), 1.0));
    }
    // Three others: the lightest takes all of r when it can.
    {
        std::vector<float> w = {0.25f, 0.35f, 0.25f, 0.15f};  // current = 0
        CrossFadeRedistribute(w, 0, 0.1f);
        CHECK(feq(w[0], 0.35) && feq(w[1], 0.35) && feq(w[2], 0.25) && feq(w[3], 0.05) && feq(sum(w), 1.0));
    }
    // Three others, lightest too light: it zeroes, then the halving at k = 1.
    {
        std::vector<float> w = {0.4f, 0.3f, 0.25f, 0.05f};
        CrossFadeRedistribute(w, 0, 0.2f);
        // k=2 (0.05): zeroed, b = 0.15; k=1 (0.25): s = 0.075 -> 0.175; k=0 (0.3): s = 0.075 -> 0.225.
        CHECK(feq(w[0], 0.6) && feq(w[3], 0.0) && feq(w[2], 0.175) && feq(w[1], 0.225) && feq(sum(w), 1.0));
    }
    // Four others: the halving is at k = 1, the SECOND HEAVIEST, not the
    // second lightest.
    {
        std::vector<float> w = {0.33f, 0.35f, 0.25f, 0.05f, 0.02f};
        CrossFadeRedistribute(w, 0, 0.1f);
        // k=3 (0.02) -> 0, b = 0.08; k=2 (0.05) -> 0 (0.081 > 0.05), b = 0.03;
        // k=1 (0.25): s = 0.015 -> 0.235; k=0 (0.35): s = 0.015 -> 0.335.
        CHECK(feq(w[0], 0.43) && feq(w[1], 0.335) && feq(w[2], 0.235) && w[3] == 0.0f && w[4] == 0.0f);
        CHECK(feq(sum(w), 1.0));
    }
    // The 0.001 snap: a member within 0.001 of the step is zeroed outright
    // (and conservation then holds only up to that margin).
    {
        std::vector<float> w = {0.8995f, 0.1005f};
        CrossFadeRedistribute(w, 0, 0.1f);
        CHECK(w[1] == 0.0f && feq(w[0], 0.9995));
    }
    // Current clamps at 1.0.
    {
        std::vector<float> w = {0.95f, 0.05f};
        CrossFadeRedistribute(w, 0, 0.1f);
        CHECK(w[0] == 1.0f && w[1] == 0.0f);
    }
}

// ===================================================================
// Sec6.5 / Sec6.7 - cross-fade driven through the controller over time
// ===================================================================
void testCrossFadeOverTime() {
    Rig rig;
    Controller c(rig.env());
    // First state on an empty controller snaps in at slot 0 (step 5).
    c.crossFadeToRecord(1, 7500, -1, 0, 0.5f, 0);
    CHECK(c.inUseCount() == 1 && c.currentSlot() == 0 && c.outgoingSlot() == -1);
    CHECK(c.slots()[0].weight == 1.0f && c.slots()[0].flags == (kSlotInUse | kSlotCrossFadeMember));
    CHECK(c.slots()[0].retriggerDeadline == -1 && c.crossFadeDuration() == 0.5f);

    c.crossFadeToRecord(2, 7501, -1, 0, 0.5f, 0);
    CHECK(c.inUseCount() == 2 && c.currentSlot() == 1 && c.outgoingSlot() == 0);
    CHECK(c.slots()[1].weight == 0.0f);

    c.update();
    const double r = (1.0 / 30.0) / 0.5;
    CHECK(feq(c.lastCrossFadeRate(), r, 1e-7));
    CHECK(feq(c.slots()[1].weight, r, 1e-6) && feq(c.slots()[0].weight, 1.0 - r, 1e-6));
    CHECK(rig.layers.bumps == 1);
    // Both members are posted, eased (Sec6.7 step 3.3): ease(t) = 2t^2 / 1 - 2(1-t)^2.
    CHECK(rig.layers.posts.size() == 2);
    CHECK(rig.layers.posts[0].clip == 101 && feq(rig.layers.posts[0].weight, 1.0 - 2.0 * r * r, 1e-5));
    CHECK(rig.layers.posts[1].clip == 102 && feq(rig.layers.posts[1].weight, 2.0 * r * r, 1e-6));
    CHECK(rig.layers.posts[0].flags == 1u && rig.layers.posts[0].a == 0.0f && rig.layers.posts[0].b == 1.0f);

    for (int i = 0; i < 14; ++i) c.update();  // 15 x (1/15) = the whole fade in 0.5 s of 30 Hz updates
    CHECK(feq(c.slots()[1].weight, 1.0, 1e-6) && c.slots()[0].weight == 0.0f);
    c.update();  // one more: the current clamps at exactly 1.0
    CHECK(c.slots()[1].weight == 1.0f && c.slots()[0].weight == 0.0f);

    // The reaper frees the exact-zero non-current member; the last in-use slot
    // moves into the hole and the cursors follow (Sec6.3).
    c.reap();
    CHECK(c.inUseCount() == 1 && c.slots()[0].record == 2 && c.currentSlot() == 0 && c.outgoingSlot() == -1);
    CHECK(c.slots()[1].record == -1 && c.slots()[1].graphRow == -1 && c.slots()[1].flags == 0);

    // Crowding inside the controller: three members, duration 1.0.
    Rig rig3;
    Controller c3(rig3.env());
    c3.crossFadeToRecord(1, 7500, -1, 0, 1.0f, 0);
    c3.crossFadeToRecord(2, 7501, -1, 0, 1.0f, 0);
    c3.update();
    c3.crossFadeToRecord(3, 7502, -1, 0, 1.0f, 0);
    c3.update();
    CHECK(feq(c3.lastCrossFadeRate(), (1.0 / 30.0) * 1.3, 1e-7));
    double total = 0;
    for (const Slot& s : c3.slots()) total += s.weight;
    CHECK(feq(total, 1.0, 1e-6));  // conserved
}

// ===================================================================
// Sec6.5 - 0x004b2280 no-op / retag / eviction / transition clip
// ===================================================================
void testCrossFadeEngine() {
    // Same record -> no-op; same +0x08 index with a blend-tree record -> retag only.
    {
        Rig rig;
        Controller c(rig.env());
        c.crossFadeToRecord(4, 7500, -1, 0, 0.3f, 0);
        CHECK((c.slots()[0].flags & kSlotBlendTree) && c.slots()[0].blendTreeRow == 5);
        c.crossFadeToRecord(4, 7509, -1, 0, 0.3f, 0);
        CHECK(c.slots()[0].tag == 7500 && c.inUseCount() == 1);
        c.crossFadeToRecord(10, 7555, -1, 0, 0.3f, 0);
        CHECK(c.inUseCount() == 1 && c.slots()[0].record == 4 && c.slots()[0].tag == 7555);
    }
    // Eviction of the lowest-weight cross-fade member (pool exhausted).
    {
        Rig rig;
        Controller c(rig.env());
        c.slotPoolLimit = 2;
        c.crossFadeToRecord(1, 7500, -1, 0, 1.0f, 0);
        c.crossFadeToRecord(2, 7501, -1, 0, 1.0f, 0);
        for (int i = 0; i < 3; ++i) c.update();  // slot1 = 0.1, slot0 = 0.9
        c.crossFadeToRecord(3, 7502, -1, 0, 1.0f, 0);
        CHECK(c.slots().size() == 2);
        CHECK(c.slots()[0].record == 1 && feq(c.slots()[0].weight, 0.9, 1e-5));
        CHECK(c.slots()[1].record == 3 && c.slots()[1].weight == 0.0f && c.currentSlot() == 1);
        CHECK(c.outgoingSlot() == 0);
    }
    // No member to evict -> nothing starts.
    {
        Rig rig;
        Controller c(rig.env());
        c.slotPoolLimit = 1;
        c.playRecord(1, 7500, 0, 1.0f, 1.0f, 1.0f);
        c.crossFadeToRecord(2, 7501, -1, 0, 1.0f, 0);
        CHECK(c.currentSlot() == -1 && c.inUseCount() == 1 && c.slots()[0].record == 1);
    }
    // Transition clip: plays through 0x004b1ef0 with flags 0x10 and (1, 1, -1),
    // is flagged 0x20, and the cross-fade lasts its length minus 0.1 s.
    {
        Rig rig;
        Controller c(rig.env());
        c.crossFadeToRecord(1, 7500, -1, 0, 0.5f, 0);
        c.crossFadeToRecord(2, 7501, 6, 7520, 0.9f, 0);
        CHECK(c.crossFadeDuration() == std::max(0.6f - 0.1f, 0.0f));
        CHECK(rig.layers.creates.size() == 1 && rig.layers.creates[0].clip == 106);
        CHECK(rig.layers.creates[0].flags == 0x10u && rig.layers.creates[0].f1 == 1.0f &&
              rig.layers.creates[0].f2 == 1.0f && rig.layers.creates[0].f3 == -1.0f);
        const int32_t ti = c.findSlot(0, rig.layers.creates[0].handle);
        CHECK(ti == 2 && (c.slots()[2].flags & kSlotTransitionClip) && c.slots()[2].stateId == -1 &&
              c.slots()[2].tag == 7520);
        // The next cross-fade retires it over 0.2 s.
        c.crossFadeToRecord(3, 7502, -1, 0, 0.5f, 0);
        CHECK(rig.layers.fades.count(rig.layers.creates[0].handle) &&
              rig.layers.fades[rig.layers.creates[0].handle] == 0.2f);
        CHECK(c.crossFadeDuration() == 0.5f);
    }
    // 0x004b21a0: state id written into the current slot; the transition
    // slot is labelled; replayed on the child ring.
    {
        Rig rig;
        Controller c(rig.env()), child(rig.env());
        c.addChild(&child);
        c.crossFadeToState(100, 7500, -1, 0, 0.5f, 0);
        c.crossFadeToState(200, 7501, 600, 7520, 0.5f, 1);
        CHECK(c.slots()[static_cast<size_t>(c.currentSlot())].stateId == 200);
        CHECK(c.slots()[2].record == 6 && c.slots()[2].stateId == 600);
        CHECK(child.inUseCount() == 3 && child.slots()[static_cast<size_t>(child.currentSlot())].stateId == 200);
        CHECK((c.flags1a4 & kCtlRestartAtZero) != 0);  // restart byte & 1
        CHECK(c.isPlaying(200) && c.isPlaying(600) && c.isPlaying(7501));
    }
}

// ===================================================================
// Sec6.5 - 0x004b1b90 / 0x004b1ef0 / 0x004b2a20-0x004b2b50
// ===================================================================
void testPlays() {
    Rig rig;
    Controller c(rig.env());
    // Plain play by state: Transition_Action -> slot flag 0x400.
    const int32_t h = c.playState(700, 7510, 0, 0.25f, 0.5f, 0.75f);
    CHECK(h == rig.layers.creates.back().handle && c.slots()[0].layerHandle == h);
    CHECK(c.slots()[0].record == 7 && c.slots()[0].stateId == 700 && c.slots()[0].tag == 7510);
    CHECK((c.slots()[0].flags & (kSlotInUse | kSlotPhaseSync)) == (kSlotInUse | kSlotPhaseSync));
    CHECK(!(c.slots()[0].flags & kSlotCrossFadeMember) && c.currentSlot() == -1);  // never touches the cross-fade
    CHECK(rig.layers.creates.back().f1 == 0.25f && rig.layers.creates.back().f3 == 0.75f);
    // Same state reuses its slot; play flag 0x400 forces a new one.
    c.playState(700, 7511, 0, 1, 1, 1);
    CHECK(c.inUseCount() == 1 && c.slots()[0].tag == 7511);
    c.playState(700, 7512, kPlayNoSameStateReuse, 1, 1, 1);
    CHECK(c.inUseCount() == 2);
    // Additive / No_Interpolation add 0x2000 / 0x10000 to +0x14.
    c.playState(1300, 7513, 0x4000, 1, 1, 1);
    CHECK(c.slots()[2].playFlags == (0x4000u | kPlayAdditiveLayer | kPlayNoInterpolationLayer));
    CHECK(c.slots()[2].flags & kSlotPhaseSync);  // play flag 0x4000
    // Unknown state: nothing starts.
    CHECK(c.playState(4242, 7500, 0, 1, 1, 1) == -1 && c.inUseCount() == 3);

    // Managed route: lazy block, graph instance seeded with defaults, weight
    // 1.0, returns -1 before +0x14 / +0x70.
    CHECK(!c.hasParameterBlock());
    CHECK(throwsOpen([&] { c.playState(500, 7520, 0x123, 2.0f, 0, 0); }, "+0x44"));  // controller +0x44 is OPEN
    Controller m(rig.env());
    m.skeletonSet44.set(3);
    CHECK(m.playState(500, 7520, 0x123, 2.0f, 0, 0) == -1);
    CHECK(m.hasParameterBlock() && m.parameterBlock()->size() == 13);
    const Slot& ms = m.slots()[0];
    CHECK((ms.flags & (kSlotInUse | kSlotManaged)) == (kSlotInUse | kSlotManaged));
    CHECK(ms.weight == 1.0f && ms.playFlags == 0u && ms.retriggerDeadline == 0);
    CHECK(m.managedSlotCount() == 1);
    const GraphInstance* g = rig.pool.row(ms.graphRow);
    CHECK(g && g->graphIndex == 7 && g->instanceHandle == 42 && g->rate40 == 2.0f && g->skeletonSet38 == 3u);
    CHECK(g->values.size() == 4);
    CHECK(g->values[0].dword0 == 1u);                      // Supported By Ground default true
    CHECK(g->values[1].dword0 == FloatBits(0.0f));         // direction default 0.0
    CHECK(g->values[2].dword0 == 0u);                      // alias -1: nothing seeded
    CHECK(g->animGroupTable30 == 0u && g->flags48 == 1u && g->timeStep3c == 0.0f && g->evalCounter44 == 0u);
    // Additive wins: an additive managed record is played as a plain clip.
    Controller a(rig.env());
    CHECK(a.playState(800, 7521, 0, 1, 1, 1) >= 0 && !(a.slots()[0].flags & kSlotManaged) && !a.hasParameterBlock());

    // playRecord: state id -1; no managed route; current pair (0x200000).
    Controller p(rig.env());
    const int32_t hp = p.playRecord(5, 7530, kPlaySetsCurrentPair, 1, 1, 1);
    CHECK(hp >= 0 && p.slots()[0].stateId == -1 && !(p.slots()[0].flags & kSlotManaged));
    CHECK(p.currentPairState() == -1 && p.currentPairLayer() == hp);
    CHECK(throwsOpen([&] { p.playRecord(1, 7500, kPlayRawClipIndex, 1, 1, 1); }, "0x80000"));
    // A record with sub-actions (byte +0x27 != 0): +0x70 arming is OPEN.
    CHECK(throwsOpen([&] { p.playRecord(9, 7500, 0, 1, 1, 1); }, "+0x70"));

    // Explicit-weight plays (0x004b2b50).
    Controller e(rig.env());
    const int32_t ei = e.setExplicitWeight(100, 7540, 0.5f, 0);
    CHECK(ei == 0 && e.slots()[0].weight == 0.5f && e.slots()[0].stateId == -1 &&
          e.slots()[0].flags == (kSlotInUse | kSlotExplicitWeight));
    CHECK(e.setExplicitWeight(100, 7540, 0.7f, 0) == 0 && e.slots()[0].weight == 0.7f && e.inUseCount() == 1);
    CHECK(e.setExplicitWeight(100, 7540, 0.0005f, 0) == -1 && e.slots()[0].weight == 0.7f);  // refused
    CHECK(e.setExplicitWeight(100, 7540, 0.0f, 0) == -1 && e.inUseCount() == 0);              // freed
}

// ===================================================================
// Sec6.3 - free / grow / reaper
// ===================================================================
void testSlotArray() {
    Rig rig;
    Controller c(rig.env());
    c.playRecord(1, 7500, 0, 1, 1, 1);
    c.playRecord(2, 7501, 0, 1, 1, 1);
    c.playRecord(3, 7502, 0, 1, 1, 1);
    // Links: slot 0 <-> slot 2 (slot 2 is the child).
    c.slot(0).linkedPartner = 2;
    c.slot(2).linkedPartner = 0;
    c.slot(2).flags |= kSlotLinkedChild;
    c.freeSlot(1);  // slot 2 moves into slot 1; links to it follow
    CHECK(c.inUseCount() == 2 && c.slots()[1].record == 3 && c.slots()[0].linkedPartner == 1);
    CHECK(c.slots()[2].record == -1 && c.slots()[2].graphRow == -1 && !c.slots()[2].inUse());
    // Freeing the non-child partner zeroes the child's weight and unlinks it.
    c.slot(1).weight = 0.6f;
    c.freeSlot(0);
    CHECK(c.inUseCount() == 1 && c.slots()[0].record == 3 && c.slots()[0].weight == 0.0f &&
          c.slots()[0].linkedPartner == -1);
    // 0x1000 clears controller +0x1a4 bit 0x10; 0x2000's routine is unread -> OPEN.
    c.flags1a4 = kCtl1a4Bit10;
    c.slot(0).flags |= kSlotClears1a4Bit10;
    c.freeSlot(0);
    CHECK(c.flags1a4 == 0u && c.inUseCount() == 0);
    c.playRecord(1, 7500, 0, 1, 1, 1);
    c.slot(0).flags |= kSlotFreeHook;
    CHECK(throwsOpen([&] { c.freeSlot(0); }, "0x004b7f80"));
    c.slot(0).flags &= ~kSlotFreeHook;

    // Growth has no cap; exhaustion is the only stop.
    Controller g(rig.env());
    for (int i = 0; i < 40; ++i) g.playRecord(1, 7500, kPlayNoSameStateReuse, 1, 1, 1);
    CHECK(g.inUseCount() == 40 && g.slots().size() == 40);
    g.slotPoolLimit = 40;
    CHECK(g.playRecord(1, 7500, kPlayNoSameStateReuse, 1, 1, 1) == -1);

    // Reaper: a plain slot is freed when its layer is gone, unless it is the
    // held layer; order is not preserved (the last slot moves into the hole).
    Rig r2;
    Controller q(r2.env());
    const int32_t h1 = q.playRecord(1, 7500, 0, 1, 1, 1);
    const int32_t h2 = q.playRecord(2, 7501, 0, 1, 1, 1);
    q.playRecord(3, 7502, 0, 1, 1, 1);
    r2.layers.gone = {h1, h2};
    r2.layers.held = h2;
    q.reap();
    CHECK(q.inUseCount() == 2 && q.slots()[0].record == 3 && q.slots()[1].record == 2);
    // A zero-weight managed slot is reaped and its graph row released.
    Controller mm(r2.env());
    mm.skeletonSet44.set(0);
    mm.playState(500, 7520, 0, 1.0f, 0, 0);
    const int32_t row = mm.slots()[0].graphRow;
    CHECK(r2.pool.row(row) != nullptr);
    mm.stop(500, 0.0f, false);  // managed: weight -> 0
    mm.reap();
    CHECK(mm.inUseCount() == 0 && r2.pool.row(row) == nullptr);
    // The current pair is cleared when its slot is freed.
    Controller cp(r2.env());
    cp.playState(100, 7500, kPlaySetsCurrentPair, 1, 1, 1);
    CHECK(cp.currentPairState() == 100);
    cp.freeSlot(0);
    CHECK(cp.currentPairState() == -1 && cp.currentPairLayer() == -1);
}

// ===================================================================
// Sec6.4 - slot finder
// ===================================================================
void testFinder() {
    Rig rig;
    Controller c(rig.env());
    c.crossFadeToRecord(1, 7550, -1, 0, 0.5f, 0);
    c.crossFadeToRecord(2, 7551, -1, 0, 0.5f, 0);
    const int32_t hp = c.playRecord(3, 7552, 0, 1, 1, 1);
    c.slot(0).stateId = 1234;
    CHECK(c.findSlot(0, 1234) == 0);
    CHECK(c.findSlot(1, 1234) == -1);           // from the first slot index
    CHECK(c.findSlot(0, 7551) == 1);            // tag partition
    CHECK(c.findSlot(0, kIdCurrentSlot) == 1);
    CHECK(c.findSlot(0, kIdOutgoingSlot) == 0);
    CHECK(c.findSlot(0, hp) == 2);              // layer-handle partition
    // -1 is always -1, even though slot 2 (a record-index play) holds +0x08 = -1.
    CHECK(c.slots()[2].stateId == -1 && c.findSlot(0, -1) == -1);
    int32_t node = 123;
    CHECK(c.findSlot(0, 9999, &node) == -1 && node == -1);  // out-dword set to -1 on entry

    // The state partition continues past a free slot 0; the tag partition stops.
    Controller q(rig.env());
    q.growSlots();
    q.growSlots();
    q.slot(1).flags = kSlotInUse;
    q.slot(1).stateId = 42;
    q.slot(1).tag = 7520;
    CHECK(q.findSlot(0, 42) == 1);
    CHECK(q.findSlot(0, 7520) == -1);

    // Named clip nodes (7,589..7,602).
    CHECK(NamedClipNodeId("transition") == 7593 && NamedClipNodeId("Motorcycle Stand") == 7602 &&
          NamedClipNodeId("Run") == -1);
    // No managed slot: no graph search at all, so no stub is reached.
    Controller plain(rig.env(false));
    plain.playRecord(1, 7500, 0, 1, 1, 1);
    CHECK(plain.findSlot(0, 7593) == -1);
    // A managed slot with the default (unimplemented) evaluator: OPEN.
    Controller md(rig.env(false));
    md.skeletonSet44.set(0);
    md.playState(500, 7520, 0, 1.0f, 0, 0);
    CHECK(throwsOpen([&] { md.findSlot(0, 7593); }, "NOT IMPLEMENTED"));
    // With a fake: the node index is reported through the out-dword.
    Controller mf(rig.env());
    mf.skeletonSet44.set(0);
    mf.playRecord(1, 7500, 0, 1, 1, 1);
    mf.playState(500, 7520, 0, 1.0f, 0, 0);
    rig.eval.nodeForId[7593] = 4;
    int32_t out = -1;
    CHECK(mf.findSlot(0, 7593, &out) == 1 && out == 4);
    CHECK(mf.findSlot(1, 7593, &out, 5) == -1);  // resumed past node 4
    CHECK(mf.isPlaying(7593) && !mf.isPlaying(7594));
}

// ===================================================================
// Sec6.6 - queries and blend inputs
// ===================================================================
void testQueries() {
    Rig rig;
    FakeLayers childLayers;
    Controller c(rig.env());
    Controller::Environment ce = rig.env();
    ce.layers = &childLayers;
    Controller child(ce);
    c.addChild(&child);

    const int32_t h = c.playState(100, 7500, 0, 1, 1, 1);
    const int32_t hc = child.slots()[0].layerHandle;
    // stop: freeze -> rate 0, then fade over the given seconds; replayed on the ring.
    c.stop(100, 0.75f, true);
    CHECK(rig.layers.rates[h] == 0.0f && rig.layers.fades[h] == 0.75f && childLayers.fades[hc] == 0.75f);
    // Pose in flight: no fade.
    rig.layers.fades.clear();
    rig.layers.inFlight = true;
    c.stop(100, 0.5f, false);
    CHECK(rig.layers.fades.empty());
    rig.layers.inFlight = false;

    // triggerFired.
    rig.layers.triggers[h] = true;
    CHECK(c.triggerFired(3, 100, -1) && !c.triggerFired(3, 4242, -1));
    rig.layers.anyTrigger = true;
    CHECK(c.triggerFired(3, -1, -1));

    // duration: plain = header +0x06 / 30.
    CHECK(c.duration(100) && *c.duration(100) == 2.0f);
    CHECK(!c.duration(4242));
    // managed = 0.0, and trigger queries on it are false.
    Controller m(rig.env());
    m.skeletonSet44.set(0);
    m.playState(500, 7520, 0, 1.0f, 0, 0);
    CHECK(m.duration(500) && *m.duration(500) == 0.0f);
    CHECK(!m.triggerFired(3, 500, -1));
    // blend tree = sum of State weight x clip duration.
    Controller b(rig.env());
    b.crossFadeToRecord(4, 7560, -1, 0, 0.5f, 0);
    b.slot(0).stateWeights[0] = 0.25f;
    b.slot(0).stateWeights[1] = 0.75f;
    CHECK(b.duration(7560) && feq(*b.duration(7560), 0.25 * 2.0 + 0.75 * 1.0));

    // Blend inputs (only on blend-tree slots): snap clamps into the row's range.
    b.setBlendInput(7560, 9.0f);
    const Slot& bs = b.slots()[0];
    CHECK(bs.blendInputInit == 4.0f && bs.blendInputTarget == 4.0f && bs.blendInputCurrent == 4.0f &&
          bs.blendInputInitialised && rig.eval.snaps == 1);
    b.setBlendInputTarget(7560, -3.0f);
    CHECK(bs.blendInputTarget == -1.0f && bs.blendInputCurrent == 4.0f && rig.eval.snaps == 1);
    // Not a blend-tree slot: ignored.
    c.setBlendInput(100, 1.0f);
    CHECK(rig.eval.snaps == 1);
    // Uninitialised target-set snaps instead.
    Controller b2(rig.env());
    b2.crossFadeToRecord(4, 7561, -1, 0, 0.5f, 0);
    b2.setBlendInputTarget(7561, 2.0f);
    CHECK(b2.slots()[0].blendInputCurrent == 2.0f && rig.eval.snaps == 2);
    // The default boundary's snap (Sec6.6 / Sec6.9, no longer a stub): every
    // State weight set to its curve value at the clamped input, no ramp.
    Controller b3(rig.env(false));
    b3.crossFadeToRecord(4, 7562, -1, 0, 0.5f, 0);
    b3.setBlendInput(7562, 1.0f);
    CHECK(feq(b3.slots()[0].stateWeights[0], 0.6, 1e-6) && feq(b3.slots()[0].stateWeights[1], 0.4, 1e-6));
    // The ring replay goes through STOP with (child, id, value, 0).
    Controller bp(rig.env());
    Controller::Environment be = rig.env();
    FakeLayers bLayers;
    be.layers = &bLayers;
    Controller bc(be);
    bp.addChild(&bc);
    const int32_t hb = bc.playState(200, 7563, 0, 1, 1, 1);
    bp.setBlendInputTarget(7563, 0.4f);
    CHECK(bLayers.fades.count(hb) && bLayers.fades[hb] == 0.4f && !bLayers.rates.count(hb));
}

// ===================================================================
// Sec6.7 step 3 + Sec6.10 - per-slot loop, graph step, block release
// ===================================================================
void testUpdateLoop() {
    // Default evaluator: a managed slot reaches the NOT IMPLEMENTED boundary.
    {
        Rig rig;
        Controller c(rig.env(false));
        c.skeletonSet44.set(0);
        c.animGroupTable30.set(0x11);
        c.animGroupTable34.set(0x22);
        c.playState(500, 7520, 0, 1.0f, 0, 0);
        CHECK(throwsOpen([&] { c.update(); }, "0x004cc6c0"));
    }
    // A blend-tree member through the default 1-D evaluator (Sec6.9).
    {
        Rig rig;
        Controller c(rig.env(false));
        c.crossFadeToRecord(4, 7500, -1, 0, 0.5f, 0);
        c.update();
        const Slot& s = c.slots()[0];
        // input initialised to 0 clamped into [-1, 4]; State 0 snaps (Blend_time
        // 0) to 0.8, State 1 reaches 0.2 within its 1/3 step
        CHECK(s.blendInputInitialised && s.blendInputCurrent == 0.0f);
        CHECK(feq(s.stateWeights[0], 0.8, 1e-6) && feq(s.stateWeights[1], 0.2, 1e-6));
        CHECK(rig.layers.posts.size() == 2);
        CHECK(rig.layers.posts[0].clip == 101 && feq(rig.layers.posts[0].weight, 0.8, 1e-6) && rig.layers.posts[0].flags == 1u &&
              rig.layers.posts[0].a == 0.0f && rig.layers.posts[0].b == 1.0f);  // not eased
        CHECK(rig.layers.posts[1].clip == 102 && feq(rig.layers.posts[1].weight, 0.2, 1e-6));
        CHECK(s.layerHandle == rig.layers.clipLayer[101]);  // the dominant State's layer
        CHECK(rig.layers.aligned.size() == 2 && rig.layers.aligned[rig.layers.clipLayer[102]] == 0.0f);
        CHECK(s.blendPhase == 0.25f);  // read back from the dominant layer
        // the input ramps 1.0 per update toward the target; State 1 ramps by 1/3
        c.setBlendInputTarget(7500, 4.0f);
        rig.layers.posts.clear();
        c.update();
        CHECK(c.slots()[0].blendInputCurrent == 1.0f);
        CHECK(feq(c.slots()[0].stateWeights[0], 0.6, 1e-6) && feq(c.slots()[0].stateWeights[1], 0.4, 1e-6));
        c.update();  // x = 2: State 0 -> 0.4, State 1 target 0.6 within its step
        CHECK(feq(c.slots()[0].stateWeights[0], 0.4, 1e-6) && feq(c.slots()[0].stateWeights[1], 0.6, 1e-6));
        // a row with Actions is OPEN (Sec6.9 step 2)
        rig.data.trees[5].actionClips = {109};
        CHECK(throwsOpen([&] { c.update(); }, "Action"));
    }
    // Value propagation into the graph instance (U0 Sec3.5 / Sec6.10).
    {
        Rig rig;
        Controller c(rig.env());
        c.skeletonSet44.set(9);
        c.playState(500, 7520, 0, 1.0f, 0, 0);
        // controller +0x30 / +0x34 are OPEN until something sets them.
        CHECK(throwsOpen([&] { c.update(); }, "+0x30"));
        c.animGroupTable30.set(0x1234);
        c.animGroupTable34.set(0x5678);
        c.setFloat(1, 1.25f);
        c.setInt(2, 300);
        rig.eval.seen.clear();
        rig.eval.collects = 0;
        c.update();
        // The evaluator sees the block, not the seeded defaults: Supported By
        // Ground's default true is overwritten with the block's 0 (Sec6.10:
        // "default_values ... are never observed").
        CHECK(rig.eval.seen.size() == 1);
        CHECK((rig.eval.seen[0] == std::vector<uint32_t>{0u, FloatBits(1.25f), 0u, 300u}));
        const GraphInstance* g = rig.pool.row(c.slots()[0].graphRow);
        CHECK(g->animGroupTable30 == 0x1234u && g->animGroupTable34 == 0x5678u);
        CHECK(g->skeletonSet38 == 9u);  // not a cross-fade member: +0x38 keeps the play's copy
        CHECK(c.slots()[0].layerHandle == rig.eval.dominant);
        CHECK(c.managedSlotCount() == 1 && rig.eval.collects == 1 && c.hasParameterBlock());
        // +0x198 / +0x19c are OPEN after the loop.
        CHECK(throwsOpen([&] { (void)c.recordFlagsAll198.get(); }));
        // A non-member graph that produces no output ends itself; the reaper
        // frees it; the next update releases the block (Sec6.7 step 5) and
        // later writes are discarded.
        rig.eval.produce = false;
        c.update();
        CHECK(c.slots()[0].weight == 0.0f);
        c.reap();
        CHECK(c.inUseCount() == 0 && rig.pool.liveCount() == 0);
        c.update();
        CHECK(!c.hasParameterBlock() && c.managedSlotCount() == 0);
        c.setFloat(1, 5.0f);
        CHECK(!c.getFloat(1));
    }
    // Freeze rule and time step: the outgoing graph state keeps its values.
    {
        Rig rig;
        rig.eval.bumpCounter = true;
        Controller c(rig.env());
        c.skeletonSet44.set(1);
        c.animGroupTable30.set(0xA);
        c.animGroupTable34.set(0xB);
        c.crossFadeToRecord(5, 7500, -1, 0, 0.5f, 0);  // graph 7, current, member
        c.setFloat(1, 1.0f);
        c.update();
        const GraphInstance* g0 = rig.pool.row(c.slots()[0].graphRow);
        CHECK(g0->values[1].dword0 == FloatBits(1.0f) && g0->evalCounter44 == 1u);
        CHECK(g0->skeletonSet38 == 1u);  // cross-fade member: +0x44 copied
        c.crossFadeToRecord(11, 7501, -1, 0, 0.5f, 0);  // graph 8 becomes current
        c.setFloat(1, 2.0f);
        CHECK(throwsOpen([&] { c.update(); }, "+0x2c"));  // evalCounter != 0 needs the time step
        c.setTimeStep(1.0f / 30.0f);
        c.update();
        g0 = rig.pool.row(c.slots()[0].graphRow);
        const GraphInstance* g1 = rig.pool.row(c.slots()[1].graphRow);
        CHECK(g0->values[1].dword0 == FloatBits(1.0f) && (g0->flags48 & 0x02u));   // frozen
        CHECK(g1->values[0].dword0 == FloatBits(2.0f) && !(g1->flags48 & 0x02u));  // copied
        CHECK(feq(g0->timeStep3c, 1.0 / 30.0, 1e-7));  // +0x3c = +0x40 (1.0) x +0x2c
        CHECK(c.managedSlotCount() == 2);
    }
    // Explicit-weight posting (not eased), de-duplication by clip, fade-out.
    {
        Rig rig;
        Controller c(rig.env());
        c.setExplicitWeightRecord(12, 7500, 0.5f, 0);   // clip 101
        c.setExplicitWeightRecord(1, 7501, 0.25f, 0);   // clip 101 again
        c.setExplicitWeightRecord(2, 7502, 0.3f, 0);    // clip 102
        c.update();
        CHECK(rig.layers.posts.size() == 3);
        CHECK(rig.layers.posts[0].weight == 0.5f && rig.layers.posts[1].weight == 0.75f &&
              rig.layers.posts[2].weight == 0.3f);
        // Explicit-weight fade-out (0x04 + 0x800): w -= 160 / trunc(B/30 x 4800).
        c.slot(1).flags |= kSlotChildFadingOut;  // clip 101, B = 3 -> T = 480 -> step 1/3
        c.slot(2).flags |= kSlotChildFadingOut;  // clip 102, B = 0 -> immediate 0
        c.update();
        CHECK(c.slots()[1].weight == 0.0f && c.slots()[2].weight == 0.0f);  // 0.25 - 1/3 floors at 0
        c.slot(0).flags |= kSlotChildFadingOut;  // 0.5 -> 0.5 - 1/3
        c.update();
        CHECK(feq(c.slots()[0].weight, 0.5 - 1.0 / 3.0, 1e-6));
        // Pose in flight: the handle becomes -1 and nothing is posted.
        rig.layers.posts.clear();
        rig.layers.inFlight = true;
        c.update();
        CHECK(rig.layers.posts.empty() && c.slots()[0].layerHandle == -1);
    }
    // Easing helper (0x00dad960 with (1.0, 0.5)).
    CHECK(EaseQuadraticInOut(0.0f) == 0.0f && EaseQuadraticInOut(1.0f) == 1.0f);
    CHECK(EaseQuadraticInOut(0.25f) == 0.125f && EaseQuadraticInOut(0.5f) == 0.5f && EaseQuadraticInOut(0.75f) == 0.875f);
    // Re-trigger: a slot whose record has byte +0x27 != 0 reaches the OPEN step.
    {
        Rig rig;
        rig.data.records[14] = rig.data.records[9];  // byte27 = 1
        Controller c(rig.env());
        c.growSlots();
        Slot& s = c.slot(0);
        s.flags = kSlotInUse;
        s.record = 14;
        CHECK(throwsOpen([&] { c.update(); }, "0x004b2d70"));
    }
}

// ===================================================================
// Sec6.7 step 2.2 / step 6 - restart override and phase handling
// ===================================================================
void testRestartAndPhase() {
    Rig rig;
    Controller c(rig.env());
    c.crossFadeToRecord(1, 7500, -1, 0, 0.5f, 0);
    c.crossFadeToRecord(2, 7501, -1, 0, 0.5f, 0);
    const int32_t ha = c.playState(700, 7510, 0, 1, 1, 1);  // Transition_Action -> slot flag 0x400
    rig.layers.weights[ha] = 0.5f;
    c.update();
    CHECK(c.lastCrossFadeRate() == 0.0001f && feq(c.slots()[1].weight, 0.0001, 1e-9));
    rig.layers.weights[ha] = 1.0f;
    c.update();
    CHECK(c.lastCrossFadeRate() == 100.0f && c.slots()[1].weight == 1.0f && c.slots()[0].weight == 0.0f);

    // No 0x400 slot: bit 0x40 is cleared by the cross-fade step.
    Controller d(rig.env());
    d.crossFadeToRecord(1, 7500, -1, 0, 0.5f, 0);
    d.flags1a4 |= kCtlPhaseSyncPending;
    d.update();
    CHECK(!(d.flags1a4 & kCtlPhaseSyncPending));

    // Restart at zero: the current layer's phase is set to 0 and 0x200/0x40 cleared.
    Controller r(rig.env());
    r.crossFadeToRecord(1, 7500, -1, 0, 0.5f, 1);
    r.update();
    const int32_t hr = r.slots()[0].layerHandle;
    CHECK(rig.layers.phases.count(hr) && rig.layers.phases[hr] == 0.0f);
    CHECK(!(r.flags1a4 & (kCtlRestartAtZero | kCtlPhaseSyncPending)));

    // Phase sync: p = +0x1a0 - D / L, wrapped into [0, 1).
    Controller p(rig.env());
    p.crossFadeToRecord(1, 7500, -1, 0, 0.5f, 0);  // current, clip 101 (L = 2.0 s)
    const int32_t hs = p.playState(700, 7510, 0, 1, 1, 1);  // clip 107, D = 6/30 = 0.2 s
    rig.layers.weights[hs] = 1.0f;
    p.flags1a4 |= kCtlPhaseSyncPending;
    CHECK(throwsOpen([&] { p.update(); }, "+0x1a0"));  // the phase offset is OPEN
    p.phaseOffset1a0.set(0.5f);
    p.update();
    const int32_t hc = p.slots()[0].layerHandle;
    CHECK(feq(rig.layers.phases[hc], 0.5 - 0.2 / 2.0, 1e-6));
    CHECK(p.flags1a4 & kCtlPhaseSyncPending);  // not cleared: repeats each update
    p.phaseOffset1a0.set(0.0f);
    p.update();
    CHECK(feq(rig.layers.phases[hc], 0.9, 1e-6));  // -0.1 wrapped
    // A qualifying slot still fading in blocks the sync.
    rig.layers.phases.clear();
    rig.layers.weights[hs] = 0.5f;
    p.update();
    CHECK(!rig.layers.phases.count(hc));
}

}  // namespace

int main() {
    const auto run = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << name << ": unexpected exception: " << e.what() << "\n";
            ++g_failures;
        }
    };
    run("registry", testRegistryLoad);
    run("graphAliases", testGraphAliases);
    run("setters", testSetters);
    run("crossFadePure", testCrossFadePure);
    run("crossFadeOverTime", testCrossFadeOverTime);
    run("crossFadeEngine", testCrossFadeEngine);
    run("plays", testPlays);
    run("slotArray", testSlotArray);
    run("finder", testFinder);
    run("queries", testQueries);
    run("updateLoop", testUpdateLoop);
    run("restartAndPhase", testRestartAndPhase);
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_anim_runtime_test: all checks passed\n";
    return 0;
}
