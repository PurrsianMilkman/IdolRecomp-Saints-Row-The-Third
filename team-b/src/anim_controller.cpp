// Animation runtime U1 (and the controller half of U0): state slots, the
// play / query API and the controller update. Source: spec-anim-runtime.md
// Sec3 and Sec6 only - see include/sr3animruntime/controller.h.
#include "sr3animruntime/controller.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "sr3anim/animation.h"
#include "sr3animruntime/crossfade.h"
#include "sr3xtbl/xtbl.h"

namespace sr3animruntime {

namespace {

[[noreturn]] void notImplemented(const std::string& what, const std::string& spec) {
    throw OpenStateError("NOT IMPLEMENTED: " + what, spec);
}

// 0x012a2d38 (Sec6.5 step 9): subtracted from the transition clip's length.
constexpr float kTransitionShortening = 0.1f;
// 0x0111f718 (Sec6.5 step 8): fade-out time of a retired transition clip.
constexpr float kTransitionRetireSeconds = 0.2f;
// 0x01351c34 (Sec6.3): eviction considers members with weight below this.
constexpr float kEvictionWeightBound = 50.0f;

}  // namespace

int32_t NamedClipNodeId(const std::string& aliasName) {
    for (int32_t k = 0; k < 14; ++k)
        if (sr3xtbl::NameEquals(aliasName, kNamedClipNodes[k])) return kIdClipNodeFirst + k;
    return -1;
}

ClipTiming ClipTimingFromAnimation(const sr3anim::Animation& clip) {
    ClipTiming t;
    t.durationFrames = clip.durationTotal();
    t.blendInFrames = clip.field_0x08_rawCount();
    t.blendOutFrames = clip.field_0x09_rawCount();
    return t;
}

float EaseQuadraticInOut(float t) {
    if (t <= 0.5f) return 2.0f * t * t;
    const float u = 1.0f - t;
    return 1.0f - 2.0f * u * u;
}

// ---------------------------------------------------------------------------
// GraphInstancePool
// ---------------------------------------------------------------------------
int32_t GraphInstancePool::instantiate(int32_t graphIndex, int32_t instanceHandle, const GraphAliasTable* aliases,
                                       const ControlParameterRegistry& registry) {
    for (int32_t i = 0; i < kCapacity; ++i) {
        if (rows_[static_cast<size_t>(i)]) continue;
        GraphInstance g;
        g.graphIndex = graphIndex;
        g.instanceHandle = instanceHandle;
        g.aliases = aliases;
        if (aliases) {
            g.values.resize(aliases->globalIds.size());
            for (size_t a = 0; a < aliases->globalIds.size(); ++a) {
                const ParamRecord* r = registry.record(aliases->globalIds[a]);
                if (!r) continue;
                g.values[a].dword0 = r->defaultDword;
                g.values[a].dword1 &= ~0xffu;  // byte +4 cleared
            }
        }
        rows_[static_cast<size_t>(i)] = std::move(g);
        return i;
    }
    throw OpenStateError("node-graph instance pool 0x03133a90 exhausted (128 rows live)",
                         "spec-anim-runtime.md Sec3.5: 0x004cbd20's behaviour with no free row is not specced");
}

void GraphInstancePool::release(int32_t row) {
    if (row >= 0 && row < kCapacity) rows_[static_cast<size_t>(row)].reset();
}

GraphInstance* GraphInstancePool::row(int32_t row) {
    if (row < 0 || row >= kCapacity || !rows_[static_cast<size_t>(row)]) return nullptr;
    return &*rows_[static_cast<size_t>(row)];
}

const GraphInstance* GraphInstancePool::row(int32_t row) const {
    if (row < 0 || row >= kCapacity || !rows_[static_cast<size_t>(row)]) return nullptr;
    return &*rows_[static_cast<size_t>(row)];
}

int32_t GraphInstancePool::liveCount() const {
    int32_t n = 0;
    for (const auto& r : rows_)
        if (r) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// ClipDedupList
// ---------------------------------------------------------------------------
float ClipDedupList::add(int32_t clip, float weight) {
    for (auto& e : entries_) {
        if (e.first != clip) continue;
        e.second = std::min(e.second + weight, 1.0f);
        return e.second;
    }
    if (entries_.size() >= kCapacity)
        throw OpenStateError("frame-local clip list overflow (more than 32 distinct clips in one update)",
                             "spec-anim-runtime.md Sec6.7 step 3.3: capacity 32; overflow behaviour not specced");
    entries_.emplace_back(clip, weight);
    return weight;
}

// ---------------------------------------------------------------------------
// EvaluationBoundary defaults: explicit not-yet-implemented markers.
// ---------------------------------------------------------------------------
GraphStepOutput EvaluationBoundary::evaluateGraphAndPost(Controller&, int32_t, GraphInstance&, ClipDedupList&) {
    notImplemented("node-graph evaluation 0x004cc6c0 + output posting (per-slot graph step)",
                   "spec-anim-runtime.md Sec6.10 steps 4-5; unit U3");
}
void EvaluationBoundary::collectGraphClipNodes(GraphInstancePool&) {
    notImplemented("graph clip-node collection 0x004cc000", "spec-anim-runtime.md Sec6.7 step 4 / Sec6.10; unit U3");
}
int32_t EvaluationBoundary::findNamedClipNode(const GraphInstance&, int32_t, int32_t) {
    notImplemented("named clip-node search in a graph instance (finder ids 7,589..7,602)",
                   "spec-anim-runtime.md Sec6.4 node search; node instances are unit U3");
}
// ---------------------------------------------------------------------------
// Sec6.9: the 1-D blend-tree evaluator 0x004b0080.
// ---------------------------------------------------------------------------
namespace {

// Sec6.9 step 3 curve rule (= Sec8.7's): the first i with Range[i] <= x <=
// Range[i+1] over count - 1 segments, linear between; value[i] for a
// zero-width segment (Sec8.7: "This is U1 Sec6.9's curve rule"); 0 when no
// segment contains x, and for a single point.
float blendTreeCurve(const BlendTreeRowInfo::StateCurve& c, float x) {
    for (int32_t i = 0; i + 1 < c.pointCount && i + 1 < 5; ++i) {
        const float r0 = c.range[i], r1 = c.range[i + 1];
        if (r0 <= x && x <= r1) {
            if (r1 == r0) return c.value[i];
            return c.value[i] + (c.value[i + 1] - c.value[i]) * (x - r0) / (r1 - r0);
        }
    }
    return 0.0f;
}

const BlendTreeRowInfo& blendTreeRowOf(Controller& c, const Slot& s) {
    if (!c.environment().data) throw std::logic_error("Controller: no AnimDataSource");
    const BlendTreeRowInfo* row = c.environment().data->blendTreeRow(s.blendTreeRow);
    if (!row) throw std::out_of_range("blend-tree row " + std::to_string(s.blendTreeRow));
    if (row->stateClips.size() > 5)
        throw OpenStateError("HOST-SAFETY GUARD: a blend-tree row with more than 5 States (the slot holds 5 weights)",
                             "spec-anim-runtime.md Sec6.2 / Sec6.11 (none shipped)");
    if (row->stateCurves.size() != row->stateClips.size())
        throw std::logic_error("BlendTreeRowInfo: stateCurves must parallel stateClips");
    return *row;
}

}  // namespace

void EvaluationBoundary::evaluateBlendTree(Controller& c, int32_t slotIndex, ClipDedupList& clips) {
    Slot& s = c.slot(slotIndex);
    const BlendTreeRowInfo& row = blendTreeRowOf(c, s);
    // 1. Input ramp.
    if (!s.blendInputInitialised) {
        const float v = std::min(std::max(0.0f, row.controlInputLow), row.controlInputHigh);
        s.blendInputInit = s.blendInputTarget = s.blendInputCurrent = v;
        s.blendInputInitialised = true;
    }
    {
        const float maxStep = row.rampSpeed * (1.0f / 30.0f);
        const float d = s.blendInputTarget - s.blendInputCurrent;
        if (std::fabs(d) <= maxStep) s.blendInputCurrent = s.blendInputTarget;
        else s.blendInputCurrent += (d > 0.0f) ? maxStep : -maxStep;
    }
    // 2. Action start rules.
    if (!row.actionClips.empty())
        notImplemented("1-D blend tree Action branch (0x004afee0 and the Action loop)",
                       "spec-anim-runtime.md Sec6.9 step 2: summarised, not specified; no shipped row has Actions (Sec6.11)");
    // 3. State weights, posting, dominant layer.
    if (!c.environment().layers)
        throw OpenStateError("animation instance (+0x20) for the 1-D blend tree",
                             "spec-anim-runtime.md Sec6: the controller has no valid instance; its behaviour here is not specced");
    LayerApi& L = *c.environment().layers;
    const float x = s.blendInputCurrent;
    std::vector<int32_t> posted;
    int32_t dominant = -1;
    float dominantEff = 0.0f;
    for (size_t k = 0; k < row.stateClips.size(); ++k) {
        const BlendTreeRowInfo::StateCurve& curve = row.stateCurves[k];
        const float target = blendTreeCurve(curve, x);
        const float step = (1.0f / 30.0f) / curve.blendTime;  // Blend_time 0: an infinite step, i.e. a snap
        float& w = s.stateWeights[k];
        if (std::fabs(target - w) <= step) w = target;
        else w += (target > w) ? step : -step;
        float eff = s.weight * w;
        if (std::fabs(eff) < 0.0001f) continue;  // posts nothing this update
        eff = clips.add(row.stateClips[k], eff);
        if (s.linkedPartner != -1) {
            const Slot& p = c.slots().at(static_cast<size_t>(s.linkedPartner));
            if (p.flags & kSlotReplacePartner) eff = (1.0f - p.weight) * eff;  // inert in shipped data (Sec6.8)
        }
        const int32_t h = L.postClip(row.stateClips[k], s.playFlags | 1u, eff, 0.0f, 1.0f);  // not eased
        posted.push_back(h);
        if (dominant == -1 || eff > dominantEff) {
            dominant = h;
            dominantEff = eff;
        }
    }
    if (dominant != -1) s.layerHandle = dominant;
    // Phase lock (no Action-imposed phase): align every posted State layer to
    // the shared phase +0x24, then read +0x24 back from the dominant layer.
    for (int32_t h : posted) L.alignLayerToPhase(h, s.blendPhase);
    if (dominant != -1) s.blendPhase = L.readLayerPhase(dominant);
}

void EvaluationBoundary::snapBlendTreeStateWeights(Controller& c, int32_t slotIndex) {
    Slot& s = c.slot(slotIndex);
    const BlendTreeRowInfo& row = blendTreeRowOf(c, s);
    for (size_t k = 0; k < row.stateClips.size(); ++k) s.stateWeights[k] = blendTreeCurve(row.stateCurves[k], s.blendInputCurrent);
}

void LayerApi::alignLayerToPhase(int32_t, float) {
    notImplemented("0x004c75c0: align a blend-tree State layer to the shared phase", "spec-anim-runtime.md Sec6.9 step 3 (U4)");
}
float LayerApi::readLayerPhase(int32_t) const {
    notImplemented("0x004be6e0: read a layer's phase", "spec-anim-runtime.md Sec6.9 step 3 (U4)");
}

// ---------------------------------------------------------------------------
// Controller: environment helpers
// ---------------------------------------------------------------------------
Controller::Controller(const Environment& env) : env_(env) {
    if (!env_.registry) throw std::invalid_argument("Controller: a ControlParameterRegistry is required");
}

LayerApi& Controller::requireLayers(const char* what) const {
    if (!env_.layers)
        throw OpenStateError(std::string("animation instance (+0x20) for ") + what,
                             "spec-anim-runtime.md Sec6: the controller has no valid instance; its behaviour here is not specced");
    return *env_.layers;
}

const AnimDataSource& Controller::data() const {
    if (!env_.data) throw std::logic_error("Controller: no AnimDataSource");
    return *env_.data;
}

EvaluationBoundary& Controller::evaluator() {
    static EvaluationBoundary defaultBoundary;
    return env_.evaluator ? *env_.evaluator : defaultBoundary;
}

AnimRecordInfo Controller::requireRecord(int32_t record) const {
    std::optional<AnimRecordInfo> r = data().record(record);
    // The engine indexes the record array unchecked (e.g. 0x004b45df reads
    // address 8 for an out-of-range index); a port refuses instead.
    if (!r) throw std::out_of_range("animation record " + std::to_string(record) + " does not exist");
    return *r;
}

ClipTiming Controller::requireClip(int32_t clipIndex) const {
    std::optional<ClipTiming> t = data().clipTiming(clipIndex);
    if (!t) throw std::out_of_range("clip " + std::to_string(clipIndex) + " has no header timing");
    return *t;
}

uint32_t Controller::layerFlagsFor(uint32_t playFlags, const AnimRecordInfo& rec) {
    uint32_t f = playFlags;
    if (rec.flags & kRecAdditive) f |= kPlayAdditiveLayer;
    if (rec.flags & kRecNoInterpolation) f |= kPlayNoInterpolationLayer;
    return f;
}

int32_t Controller::armRetrigger(const AnimRecordInfo& rec) const {
    if (rec.byte27 == 0) return -1;  // Sec6.2 / Sec6.8: +0x70 only for records with byte +0x27 != 0
    notImplemented("re-trigger deadline +0x70 arming from record bytes +0x24/+0x25/+0x27",
                   "spec-anim-runtime.md Sec6.5 step 6 cites Q13.2 (not a spec); Sec6.8 finds +0x27 = 0 in all shipped records");
}

// Slot flag 0x400 is cleared on every new play into the slot, then set by
// 0x004b1b90 / 0x004b1ef0 for Transition_Action records or play flag 0x4000.
void Controller::startPhaseSyncFlag(Slot& s, uint32_t playFlags, const AnimRecordInfo& rec) {
    s.flags &= ~kSlotPhaseSync;
    if ((rec.flags & kRecTransitionAction) || (playFlags & kPlayPhaseSync)) s.flags |= kSlotPhaseSync;
}

int32_t Controller::instantiateGraph(int32_t graphIndex) {
    if (!env_.graphPool) throw std::logic_error("Controller: no GraphInstancePool");
    return env_.graphPool->instantiate(graphIndex, env_.instanceHandle, data().graphAliases(graphIndex), *env_.registry);
}

// ---------------------------------------------------------------------------
// U0: parameter block and setters (Sec3)
// ---------------------------------------------------------------------------
void Controller::allocateParameterBlock() {
    if (block_) return;
    block_.emplace(static_cast<size_t>(std::max<int16_t>(env_.registry->count(), 0)));
}

void Controller::setBool(int16_t id, bool value) {
    if (!block_) return;
    for (Controller* c : children_) c->setBool(id, value);
    if (id == -1) return;
    const ParamRecord* r = env_.registry->record(id);
    if (!r || r->type != ParamType::Bool) return;
    uint32_t& d = (*block_)[static_cast<size_t>(id)].dword0;
    d = (d & ~0xffu) | (value ? 1u : 0u);  // byte store
}

void Controller::setInt(int16_t id, int32_t value) {
    if (!block_) return;
    for (Controller* c : children_) c->setInt(id, value);
    if (id == -1) return;
    const ParamRecord* r = env_.registry->record(id);
    if (!r || r->type != ParamType::Integer) return;
    (*block_)[static_cast<size_t>(id)].dword0 = static_cast<uint32_t>(value);
}

void Controller::setFloat(int16_t id, float value) {
    if (!block_) return;
    for (Controller* c : children_) c->setFloat(id, value);
    if (id == -1) return;
    const ParamRecord* r = env_.registry->record(id);
    if (!r || r->type != ParamType::Float) return;
    (*block_)[static_cast<size_t>(id)].dword0 = FloatBits(value);
}

std::optional<bool> Controller::getBool(int16_t id) const {
    const ParamRecord* r = env_.registry->record(id);
    if (!block_ || !r || r->type != ParamType::Bool) return std::nullopt;
    return ((*block_)[static_cast<size_t>(id)].dword0 & 0xffu) != 0;
}

std::optional<int32_t> Controller::getInt(int16_t id) const {
    const ParamRecord* r = env_.registry->record(id);
    if (!block_ || !r || r->type != ParamType::Integer) return std::nullopt;
    return static_cast<int32_t>((*block_)[static_cast<size_t>(id)].dword0);
}

std::optional<float> Controller::getFloat(int16_t id) const {
    const ParamRecord* r = env_.registry->record(id);
    if (!block_ || !r || r->type != ParamType::Float) return std::nullopt;
    return BitsFloat((*block_)[static_cast<size_t>(id)].dword0);
}

// ---------------------------------------------------------------------------
// Slot array management (Sec6.3)
// ---------------------------------------------------------------------------
int32_t Controller::inUseCount() const {
    int32_t n = 0;
    while (n < static_cast<int32_t>(slots_.size()) && slots_[static_cast<size_t>(n)].inUse()) ++n;
    return n;
}

bool Controller::growSlots() {
    if (slotPoolLimit && slots_.size() >= *slotPoolLimit) return false;
    slots_.emplace_back();  // zeroed, +0x00 = +0x6c = -1
    return true;
}

void Controller::freeSlot(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(slots_.size())) throw std::out_of_range("freeSlot: index");
    Slot& s = slots_[static_cast<size_t>(index)];
    // Side effects of 0x2000 / 0x1000, before the move.
    if (s.flags & kSlotFreeHook)
        notImplemented("0x004b7f80(controller) on freeing a flag-0x2000 slot",
                       "spec-anim-runtime.md Sec6.2 / Sec5.1 item 13 (routine not read)");
    if (s.flags & kSlotClears1a4Bit10) flags1a4 &= ~kCtl1a4Bit10;
    // Unlink a +0x78 partner (zeroing its weight) unless this slot is the
    // child. CHOSEN reading of "unlinks": the partner's own link back to this
    // slot is cleared too.
    if (s.linkedPartner != -1 && !(s.flags & kSlotLinkedChild)) {
        Slot& p = slots_.at(static_cast<size_t>(s.linkedPartner));
        p.weight = 0.0f;
        if (p.linkedPartner == index) p.linkedPartner = -1;
    }
    if (pair60_ != -1 && s.stateId == pair60_) {
        pair60_ = -1;
        pair64_ = -1;
    }
    if (s.graphRow != -1 && env_.graphPool) env_.graphPool->release(s.graphRow);

    // Fill the hole with the last in-use slot of the run that follows it.
    const int32_t last = inUseCount() - 1;
    if (last > index) {
        slots_[static_cast<size_t>(index)] = slots_[static_cast<size_t>(last)];
        for (int32_t j = 0; j < static_cast<int32_t>(slots_.size()); ++j)
            if (slots_[static_cast<size_t>(j)].linkedPartner == last) slots_[static_cast<size_t>(j)].linkedPartner = index;
    }
    // Cursor fix-ups: a pointer to the freed slot becomes null; one to the
    // moved slot follows it.
    auto fix = [&](int32_t& cursor) {
        if (cursor == index) cursor = -1;
        else if (last > index && cursor == last) cursor = index;
    };
    fix(current_);
    fix(outgoing_);
    const int32_t vacated = (last > index) ? last : index;
    slots_[static_cast<size_t>(vacated)] = Slot{};
}

void Controller::reap() {
    bool freedAny = false;
    int32_t i = 0;
    while (i < static_cast<int32_t>(slots_.size()) && slots_[static_cast<size_t>(i)].inUse()) {
        const Slot& s = slots_[static_cast<size_t>(i)];
        if (s.record == -1) {
            ++i;
            continue;
        }
        bool free = false;
        if (s.flags & (kSlotCrossFadeMember | kSlotExplicitWeight)) {
            free = (s.weight == 0.0f && i != current_);
        } else {
            const std::optional<int32_t> held = env_.layers ? env_.layers->heldLayerHandle() : std::nullopt;
            if (!(held && *held == s.layerHandle)) {
                if (s.flags & kSlotManaged) free = (s.weight == 0.0f);
                else free = env_.layers ? env_.layers->layerGone(s.layerHandle) : true;  // invalid instance = gone
            }
        }
        if (free) {
            freeSlot(i);
            freedAny = true;  // re-examine the same index
        } else {
            ++i;
        }
    }
    if (freedAny) {
        recordFlagsAll198.forget();
        recordFlagsCurrent19c.forget();
    }
}

int32_t Controller::firstFreeOrGrow() {
    const int32_t n = inUseCount();
    if (n < static_cast<int32_t>(slots_.size())) return n;
    if (!growSlots()) return -1;
    return static_cast<int32_t>(slots_.size()) - 1;
}

int32_t Controller::acquireSlot(int32_t matchIndex) {
    if (matchIndex >= 0) return matchIndex;
    return firstFreeOrGrow();
}

// ---------------------------------------------------------------------------
// Sec6.4 slot finder
// ---------------------------------------------------------------------------
int32_t Controller::findSlot(int32_t firstSlot, int32_t id, int32_t* outNode, int32_t firstNode) {
    if (outNode) *outNode = -1;
    // The partitions are tested in the spec's order. For "-1 always gives -1"
    // to hold in that order the range tests must be unsigned (a signed
    // id < 7,500 test would catch -1 first): HIGH inference, flagged in the
    // report. So any other negative id falls to the layer-handle partition.
    const uint32_t uid = static_cast<uint32_t>(id);
    const int32_t count = static_cast<int32_t>(slots_.size());
    const int32_t start = std::max(firstSlot, 0);
    if (uid < static_cast<uint32_t>(kIdTagFirst)) {
        for (int32_t i = start; i < count; ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            if (!s.inUse()) {
                if (i == 0) continue;  // the 0x004afa65 quirk: continues past a free slot 0
                break;
            }
            if (s.stateId == id) return i;
        }
        return -1;
    }
    if (uid < static_cast<uint32_t>(kIdClipNodeFirst)) {
        for (int32_t i = start; i < count && slots_[static_cast<size_t>(i)].inUse(); ++i)
            if (slots_[static_cast<size_t>(i)].tag == id) return i;
        return -1;
    }
    if (id == kIdCurrentSlot) return current_;
    if (id == kIdOutgoingSlot) return outgoing_;
    if (id == -1) return -1;
    if (uid < static_cast<uint32_t>(kIdClipNodeEnd)) {
        // CHOSEN: firstNode applies to the first slot examined; later slots
        // start at node 0 (Sec6.4 "a caller can resume the search").
        int32_t node0 = firstNode;
        for (int32_t i = start; i < count && slots_[static_cast<size_t>(i)].inUse(); ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            if (!(s.flags & kSlotManaged)) continue;
            const GraphInstance* row = env_.graphPool ? env_.graphPool->row(s.graphRow) : nullptr;
            if (!row) continue;
            const int32_t node = evaluator().findNamedClipNode(*row, id, node0);
            node0 = 0;
            if (node >= 0) {
                if (outNode) *outNode = node;
                return i;
            }
        }
        return -1;
    }
    for (int32_t i = start; i < count && slots_[static_cast<size_t>(i)].inUse(); ++i)
        if (slots_[static_cast<size_t>(i)].layerHandle == id) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Sec6.5 play family
// ---------------------------------------------------------------------------
int32_t Controller::playState(int32_t stateId, int32_t tag, uint32_t playFlags, float f1, float f2, float f3) {
    for (Controller* c : children_) c->playState(stateId, tag, playFlags, f1, f2, f3);
    const int32_t record = data().stateToRecord(stateId);
    if (record < 0) return -1;  // CHOSEN: no record -> nothing started (Q13.2 detail, not specced)
    const AnimRecordInfo rec = requireRecord(record);

    int32_t match = -1;
    if (!(playFlags & kPlayNoSameStateReuse)) {
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i)
            if (slots_[static_cast<size_t>(i)].stateId == stateId) { match = i; break; }
    }
    const int32_t idx = acquireSlot(match);
    if (idx < 0) return -1;  // pool exhaustion: nothing started

    // CHOSEN (Q13.2 ordering, not specced): identity fields and the in-use /
    // 0x400 flags are written before the route split.
    Slot& s = slots_[static_cast<size_t>(idx)];
    s.record = record;
    s.stateId = stateId;
    s.tag = tag;
    s.flags |= kSlotInUse;
    startPhaseSyncFlag(s, playFlags, rec);

    if ((rec.flags & kRecManaged) && !(rec.flags & kRecAdditive)) {
        // Managed route (Sec6.5): Additive wins, so an additive managed
        // record is played as a plain clip.
        allocateParameterBlock();  // Sec3.4 site 0x004b1c86 (no-op when present)
        s.flags |= kSlotManaged;
        if (s.graphRow != -1) env_.graphPool->release(s.graphRow);
        s.graphRow = -1;
        const int32_t row = instantiateGraph(rec.index08);
        slots_[static_cast<size_t>(idx)].graphRow = row;
        GraphInstance* g = env_.graphPool->row(row);
        g->rate40 = f1;
        g->skeletonSet38 = skeletonSet44.get();
        ++managed24_;
        slots_[static_cast<size_t>(idx)].weight = 1.0f;
        // Returns before +0x14 and +0x70 are touched.
        return -1;
    }

    s.playFlags = layerFlagsFor(playFlags, rec);
    s.retriggerDeadline = armRetrigger(rec);
    s.layerHandle = requireLayers("0x004b1b90 clip creation").createClipLayer(rec.index08, s.playFlags, f1, f2, f3);
    if (playFlags & kPlaySetsCurrentPair) {  // CHOSEN placement: Sec6.1 names the flag, not the point
        pair60_ = s.stateId;
        pair64_ = s.layerHandle;
    }
    return s.layerHandle;
}

int32_t Controller::playRecord(int32_t record, int32_t tag, uint32_t playFlags, float f1, float f2, float f3) {
    if (playFlags & kPlayRawClipIndex)
        throw OpenStateError("0x004b1ef0 with play flag 0x80000 (raw clip index): slot +0x00 contents",
                             "spec-anim-runtime.md Sec6.5: the clip-index form is named, its slot fields are not");
    const std::optional<AnimRecordInfo> recOpt = data().record(record);
    if (!recOpt) return -1;  // CHOSEN: no record -> nothing started
    const AnimRecordInfo rec = *recOpt;

    int32_t match = -1;
    if (!(playFlags & kPlayNoSameStateReuse)) {
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i)
            if (slots_[static_cast<size_t>(i)].record == record) { match = i; break; }
    }
    const int32_t idx = acquireSlot(match);
    if (idx < 0) return -1;

    Slot& s = slots_[static_cast<size_t>(idx)];
    s.record = record;
    s.stateId = -1;  // Sec6.2: -1 when started by record index
    s.tag = tag;
    s.flags |= kSlotInUse;
    startPhaseSyncFlag(s, playFlags, rec);
    // No managed route (Sec6.5): a managed record is played as a plain clip.
    s.playFlags = layerFlagsFor(playFlags, rec);
    s.retriggerDeadline = armRetrigger(rec);
    s.layerHandle = requireLayers("0x004b1ef0 clip creation").createClipLayer(rec.index08, s.playFlags, f1, f2, f3);
    if (playFlags & kPlaySetsCurrentPair) {
        pair60_ = s.stateId;
        pair64_ = s.layerHandle;
    }
    return s.layerHandle;
}

void Controller::crossFadeToState(int32_t stateId, int32_t tag, int32_t transitionStateId, int32_t transitionTag,
                                  float blendSeconds, uint8_t restart) {
    for (Controller* c : children_)
        c->crossFadeToState(stateId, tag, transitionStateId, transitionTag, blendSeconds, restart);
    const int32_t record = data().stateToRecord(stateId);
    if (record == -1) return;
    // CHOSEN: a transition state id of -1 means "none" (the spec calls it optional).
    const int32_t transitionRecord = (transitionStateId == -1) ? -1 : data().stateToRecord(transitionStateId);
    crossFadeToRecord(record, tag, transitionRecord, transitionTag, blendSeconds, restart);
    if (current_ < 0)
        throw std::logic_error("0x004b21a0: no current slot after 0x004b2280 (the engine faults at 0x004b222b)");
    slots_[static_cast<size_t>(current_)].stateId = stateId;
    if (transitionRecord != -1) {
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i) {
            if (slots_[static_cast<size_t>(i)].record == transitionRecord) {
                slots_[static_cast<size_t>(i)].stateId = transitionStateId;
                break;
            }
        }
    }
}

void Controller::crossFadeToRecord(int32_t record, int32_t tag, int32_t transitionRecord, int32_t transitionTag,
                                   float blendSeconds, uint8_t restart) {
    // 1. No-op cases.
    if (record == -1) return;
    const AnimRecordInfo rec = requireRecord(record);
    if (current_ >= 0) {
        Slot& cur = slots_[static_cast<size_t>(current_)];
        if (cur.record == record) return;
        const std::optional<AnimRecordInfo> curRec = data().record(cur.record);
        if (curRec && curRec->index08 == rec.index08 && (rec.flags & (kRecBlendTree | kRecManaged))) {
            cur.tag = tag;
            return;
        }
    }

    // 2. Choose the slot: same record with no graph instance; else first
    // free; else grow; else evict the lowest-weight cross-fade member.
    int32_t idx = -1;
    {
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            if (s.record == record && s.graphRow == -1) { idx = i; break; }
        }
    }
    if (idx < 0) idx = firstFreeOrGrow();
    if (idx < 0) {
        int32_t victim = -1;
        float best = kEvictionWeightBound;
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            if ((s.flags & kSlotCrossFadeMember) && s.weight < best) {
                best = s.weight;
                victim = i;
            }
        }
        if (victim < 0) return;  // no member: start nothing
        // CHOSEN deviation, per the spec's own advice (Sec6.5 step 2: "A port
        // should free the victim first"): the engine overwrites the victim in
        // place, OR-ing its flags and leaking any graph row it held.
        freeSlot(victim);
        idx = firstFreeOrGrow();
        if (idx < 0) return;
    }
    const bool heldDifferent = slots_[static_cast<size_t>(idx)].record != record;

    // 3. Lazy parameter block for a managed record (Sec3.4 site 0x004b23f3).
    if (rec.flags & kRecManaged) allocateParameterBlock();

    // 4. Make it current.
    Slot& s = slots_[static_cast<size_t>(idx)];
    current_ = idx;
    s.flags |= kSlotInUse | kSlotCrossFadeMember;
    s.flags &= ~kSlotPhaseSync;  // cleared on every new play into the slot (Sec6.2)
    s.tag = tag;
    flags1a4 = (flags1a4 & ~kCtlRestartAtZero) | ((restart & 1) ? kCtlRestartAtZero : 0u);

    // 5. Starting weight.
    if (heldDifferent) {
        s.weight = 0.0f;
        s.record = record;
    }
    if (idx == 0) s.weight = 1.0f;

    // 6. Kind.
    if (rec.flags & kRecAdditive) s.playFlags |= kPlayAdditiveLayer;
    if (rec.flags & kRecBlendTree) {
        s.flags |= kSlotBlendTree;
        s.blendTreeRow = rec.index08;
    } else if (rec.flags & kRecManaged) {
        s.flags |= kSlotManaged;
        const int32_t row = instantiateGraph(rec.index08);
        slots_[static_cast<size_t>(idx)].graphRow = row;
        ++managed24_;
    }
    slots_[static_cast<size_t>(idx)].retriggerDeadline = armRetrigger(rec);

    // 7. Outgoing = the highest-weight member other than the new current.
    {
        std::vector<int32_t> members;
        std::vector<float> w;
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i)
            if (slots_[static_cast<size_t>(i)].flags & kSlotCrossFadeMember) {
                members.push_back(i);
                w.push_back(slots_[static_cast<size_t>(i)].weight);
            }
        outgoing_ = -1;
        for (size_t k : CrossFadeSortOrder(w))
            if (members[k] != current_) { outgoing_ = members[k]; break; }
    }

    // 8. Retire the previous transition clip(s).
    {
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i) {
            const Slot& t = slots_[static_cast<size_t>(i)];
            if (!(t.flags & kSlotTransitionClip)) continue;
            LayerApi& L = requireLayers("0x004b2280 transition-clip retirement");
            if (!L.poseInFlight()) L.fadeOutLayer(t.layerHandle, kTransitionRetireSeconds);
        }
    }

    // 9. Transition clip and the cross-fade duration.
    if (transitionRecord != -1) {
        const int32_t h = playRecord(transitionRecord, transitionTag, 0x10, 1.0f, 1.0f, -1.0f);
        const int32_t ti = findSlot(0, h);  // last partition (see the report: a small handle hits another partition)
        if (ti >= 0) slots_[static_cast<size_t>(ti)].flags |= kSlotTransitionClip;
        const AnimRecordInfo trec = requireRecord(transitionRecord);
        duration58_ = std::max(requireClip(trec.index08).durationSeconds() - kTransitionShortening, 0.0f);
    } else {
        duration58_ = blendSeconds;
    }
}

int32_t Controller::setExplicitWeight(int32_t stateId, int32_t tag, float weight, uint32_t playFlags) {
    for (Controller* c : children_) c->setExplicitWeight(stateId, tag, weight, playFlags);
    const int32_t record = data().stateToRecord(stateId);
    if (record < 0) return -1;
    return setExplicitWeightRecord(record, tag, weight, playFlags);
}

int32_t Controller::setExplicitWeightRecord(int32_t record, int32_t tag, float weight, uint32_t playFlags) {
    int32_t match = -1;
    {
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            if ((s.flags & kSlotExplicitWeight) && s.record == record) { match = i; break; }
        }
    }
    // JUDGEMENT CALL on ordering (Sec6.8 lists both rules without an order):
    // exactly 0.0 frees the matching slot; any other weight below 0.001 is
    // refused.
    if (weight == 0.0f) {
        if (match >= 0) freeSlot(match);
        return -1;
    }
    if (weight < 0.001f) return -1;
    const AnimRecordInfo rec = requireRecord(record);
    const int32_t idx = acquireSlot(match);
    if (idx < 0) return -1;

    if (rec.flags & kRecBlendTree) {
        slots_[static_cast<size_t>(idx)].flags |= kSlotBlendTree;
        slots_[static_cast<size_t>(idx)].blendTreeRow = rec.index08;
    } else if (rec.flags & kRecManaged) {
        allocateParameterBlock();  // Sec3.4 site 0x004b2c6a
        // CHOSEN deviation: release an existing row first. The engine
        // allocates a new row without releasing (0x004b2c9e), leaking one of
        // the 128 rows per re-weight (Sec6.8); mirrors the spec's own "free
        // the victim first" advice for the analogous eviction hazard.
        if (slots_[static_cast<size_t>(idx)].graphRow != -1) env_.graphPool->release(slots_[static_cast<size_t>(idx)].graphRow);
        slots_[static_cast<size_t>(idx)].graphRow = -1;
        const int32_t row = instantiateGraph(rec.index08);
        slots_[static_cast<size_t>(idx)].flags |= kSlotManaged;
        slots_[static_cast<size_t>(idx)].graphRow = row;
        ++managed24_;
    }
    Slot& s = slots_[static_cast<size_t>(idx)];
    s.record = record;
    s.weight = weight;
    s.stateId = -1;
    s.playFlags = layerFlagsFor(playFlags, rec);
    s.tag = tag;
    s.flags &= ~kSlotPhaseSync;
    s.flags |= kSlotInUse | kSlotExplicitWeight;
    return idx;
}

// ---------------------------------------------------------------------------
// Sec6.6 query family
// ---------------------------------------------------------------------------
bool Controller::isPlaying(int32_t id) { return findSlot(0, id, nullptr, 0) >= 0; }

void Controller::stop(int32_t id, float seconds, bool freeze) {
    for (Controller* c : children_) c->stop(id, seconds, freeze);
    const int32_t i = findSlot(0, id, nullptr, 0);
    if (i < 0) return;
    Slot& s = slots_[static_cast<size_t>(i)];
    LayerApi& L = requireLayers("0x004b41b0 stop");
    if (freeze) L.setLayerRate(s.layerHandle, 0.0f);
    if (s.flags & kSlotManaged) {
        s.weight = 0.0f;  // the reaper frees it
        return;
    }
    if (!L.poseInFlight()) L.fadeOutLayer(s.layerHandle, seconds);
}

bool Controller::triggerFired(int32_t triggerId, int32_t id, int32_t occurrence) {
    if (id != -1) {
        const int32_t i = findSlot(0, id, nullptr, 0);
        if (i < 0) return false;  // CHOSEN: no slot -> false
        const Slot& s = slots_[static_cast<size_t>(i)];
        if (s.flags & kSlotManaged) return false;
        return requireLayers("0x004b4320 trigger query").triggerFired(s.layerHandle, triggerId, occurrence);
    }
    return requireLayers("0x004b4320 trigger query").triggerFiredAnyLayer(triggerId, occurrence);
}

std::optional<float> Controller::duration(int32_t id) {
    const int32_t i = findSlot(0, id, nullptr, 0);
    if (i < 0) return std::nullopt;  // CHOSEN: the no-slot result is not specced
    const Slot& s = slots_[static_cast<size_t>(i)];
    if (s.flags & kSlotManaged) return 0.0f;
    if (s.flags & kSlotBlendTree) {
        const BlendTreeRowInfo* row = data().blendTreeRow(s.blendTreeRow);
        if (!row) throw std::out_of_range("blend-tree row " + std::to_string(s.blendTreeRow));
        bool anyAction = false;
        for (size_t k = 0; k < row->actionClips.size() && k < 2; ++k)
            if (s.actions[k].weight > 0.0f) anyAction = true;
        float sum = 0.0f;
        if (anyAction) {
            for (size_t k = 0; k < row->actionClips.size() && k < 2; ++k)
                sum += s.actions[k].weight * requireClip(row->actionClips[k]).durationSeconds();
        } else {
            for (size_t k = 0; k < row->stateClips.size() && k < 5; ++k)
                sum += s.stateWeights[k] * requireClip(row->stateClips[k]).durationSeconds();
        }
        return sum;
    }
    return requireClip(requireRecord(s.record).index08).durationSeconds();
}

bool Controller::blendInputSlot(int32_t index) const {
    const uint32_t f = slots_[static_cast<size_t>(index)].flags;
    return (f & kSlotBlendTree) && !(f & kSlotManaged);
}

void Controller::snapBlendInput(int32_t index, float value) {
    Slot& s = slots_[static_cast<size_t>(index)];
    const BlendTreeRowInfo* row = data().blendTreeRow(s.blendTreeRow);
    if (!row) throw std::out_of_range("blend-tree row " + std::to_string(s.blendTreeRow));
    const float v = std::min(std::max(value, row->controlInputLow), row->controlInputHigh);
    s.blendInputInit = s.blendInputTarget = s.blendInputCurrent = v;
    s.blendInputInitialised = true;
    if (!row->actionClips.empty())
        notImplemented("Action start rules 0x004afee0", "spec-anim-runtime.md Sec6.9 step 2 / Sec5.1 item 15 (no shipped row has Actions)");
    evaluator().snapBlendTreeStateWeights(*this, index);
}

void Controller::setBlendInput(int32_t id, float value) {
    // Replays on the ring through the STOP routine with (child, id, value, 0)
    // - CONFIRMED quirk (0x004b72be).
    for (Controller* c : children_) c->stop(id, value, false);
    const int32_t i = findSlot(0, id, nullptr, 0);
    if (i < 0 || !blendInputSlot(i)) return;
    snapBlendInput(i, value);
}

void Controller::setBlendInputTarget(int32_t id, float value) {
    for (Controller* c : children_) c->stop(id, value, false);  // 0x004b733e, same quirk
    const int32_t i = findSlot(0, id, nullptr, 0);
    if (i < 0 || !blendInputSlot(i)) return;
    Slot& s = slots_[static_cast<size_t>(i)];
    if (!s.blendInputInitialised) {
        snapBlendInput(i, value);
        return;
    }
    const BlendTreeRowInfo* row = data().blendTreeRow(s.blendTreeRow);
    if (!row) throw std::out_of_range("blend-tree row " + std::to_string(s.blendTreeRow));
    s.blendInputTarget = std::min(std::max(value, row->controlInputLow), row->controlInputHigh);
}

// ---------------------------------------------------------------------------
// Sec6.7 the update
// ---------------------------------------------------------------------------
void Controller::update() {
    // 1. Instance bump.
    if (env_.layers) env_.layers->bumpInstance();
    // 2. Cross-fade.
    crossFadeStep();
    // 3. Per-slot loop.
    if (env_.layers) {
        managed24_ = 0;
        recordFlagsAll198.forget();      // 0x004b3570 summary: OPEN (see header)
        recordFlagsCurrent19c.forget();
        ClipDedupList clips;
        for (int32_t i = 0; i < static_cast<int32_t>(slots_.size()) && slots_[static_cast<size_t>(i)].inUse(); ++i) {
            {
                const Slot& s = slots_[static_cast<size_t>(i)];
                if (!((s.flags & kSlotCrossFadeMember) && s.retriggerDeadline == -1 && s.linkedPartner == -1)) retrigger(i);
            }
            {
                const Slot& s = slots_[static_cast<size_t>(i)];
                if (((s.flags & kSlotCrossFadeMember) && s.weight > 0.0f) || (s.flags & kSlotExplicitWeight))
                    weightingStep(i, clips);
            }
            if (slots_[static_cast<size_t>(i)].flags & kSlotManaged) {
                graphStep(i, clips);
                ++managed24_;
            }
        }
    }
    // 4. Graph clip collection, once, over all live graph rows (CHOSEN: with
    // no live row there is nothing to collect, so the boundary is skipped).
    if (env_.graphPool && env_.graphPool->liveCount() > 0) evaluator().collectGraphClipNodes(*env_.graphPool);
    // 5. Parameter-block release when no managed slot was processed.
    if (block_ && managed24_ == 0) block_.reset();
    // 6. Phase handling.
    phaseHandling();
}

void Controller::crossFadeStep() {
    if (current_ < 0) return;
    std::vector<int32_t> members;
    const int32_t n = inUseCount();
    for (int32_t i = 0; i < n; ++i)
        if (slots_[static_cast<size_t>(i)].flags & kSlotCrossFadeMember) members.push_back(i);
    if (members.empty()) return;

    int32_t restartSlot = -1;
    for (int32_t i = 0; i < n; ++i)
        if (slots_[static_cast<size_t>(i)].flags & kSlotPhaseSync) { restartSlot = i; break; }
    std::optional<float> restartWeight;
    if (restartSlot < 0) flags1a4 &= ~kCtlPhaseSyncPending;
    else if (members.size() > 1)
        restartWeight = requireLayers("cross-fade restart override layer weight")
                            .layerWeight(slots_[static_cast<size_t>(restartSlot)].layerHandle);

    const float r = CrossFadeRate(duration58_, members.size(), restartWeight);
    rate5c_ = r;

    std::vector<float> w;
    size_t currentIdx = members.size();
    for (size_t k = 0; k < members.size(); ++k) {
        w.push_back(slots_[static_cast<size_t>(members[k])].weight);
        if (members[k] == current_) currentIdx = k;
    }
    // Only 0x004b2280 sets +0x50, and it always sets flag 0x02 with it.
    if (currentIdx == members.size()) throw std::logic_error("cross-fade: the current slot is not a cross-fade member");
    CrossFadeRedistribute(w, currentIdx, r);
    for (size_t k = 0; k < members.size(); ++k) slots_[static_cast<size_t>(members[k])].weight = w[k];
}

void Controller::retrigger(int32_t index) {
    const Slot& s = slots_[static_cast<size_t>(index)];
    const std::optional<AnimRecordInfo> rec = data().record(s.record);
    if (!rec || rec->byte27 == 0) return;  // returns at once (Sec6.8) - every shipped record
    notImplemented("re-trigger step 0x004b2d70 for a record with byte +0x27 != 0",
                   "spec-anim-runtime.md Sec6.8 (inert in shipped data; Sec5.1 item 10)");
}

void Controller::weightingStep(int32_t index, ClipDedupList& clips) {
    // Explicit-weight fade-out (flags 0x04 + 0x800, not 0x40).
    {
        Slot& s = slots_[static_cast<size_t>(index)];
        if ((s.flags & kSlotExplicitWeight) && (s.flags & kSlotChildFadingOut) && !(s.flags & kSlotLinkedChild)) {
            const uint8_t b = requireClip(requireRecord(s.record).index08).blendOutFrames;
            if (b == 0) {
                s.weight = 0.0f;
            } else {
                const int32_t t = static_cast<int32_t>(static_cast<float>(b) / 30.0f * 4800.0f);
                s.weight = std::max(s.weight - 160.0f / static_cast<float>(t), 0.0f);
            }
        }
    }
    const uint32_t flags = slots_[static_cast<size_t>(index)].flags;
    if (flags & kSlotBlendTree) {
        evaluator().evaluateBlendTree(*this, index, clips);
        return;
    }
    if (flags & kSlotManaged) return;  // nothing here; the graph step follows

    // Plain clip.
    Slot& s = slots_[static_cast<size_t>(index)];
    const AnimRecordInfo rec = requireRecord(s.record);
    float eff = clips.add(rec.index08, s.weight);
    if (s.linkedPartner != -1) {
        const Slot& p = slots_.at(static_cast<size_t>(s.linkedPartner));
        if (!p.inUse()) {
            s.weight = 0.0f;
            return;
        }
        if (p.flags & kSlotReplacePartner) eff = (1.0f - p.weight) * eff;
        if (s.flags & kSlotLinkedChild) eff = p.weight * eff;
    }
    if (eff != 1.0f && !(s.flags & kSlotExplicitWeight))
        eff = EaseQuadraticInOut(std::min(std::max(eff, 0.0f), 1.0f));
    if (std::fabs(eff) < 0.0001f) {
        s.layerHandle = -1;
        return;
    }
    LayerApi& L = requireLayers("plain-clip posting 0x004c6590");
    s.layerHandle = L.poseInFlight() ? -1 : L.postClip(rec.index08, s.playFlags | 1u, eff, 0.0f, 1.0f);
}

void Controller::graphStep(int32_t index, ClipDedupList& clips) {
    // 1. Guards.
    if (!env_.graphPool || !block_) return;
    GraphInstance* row = env_.graphPool->row(slots_[static_cast<size_t>(index)].graphRow);
    if (!row) return;
    const uint32_t flags = slots_[static_cast<size_t>(index)].flags;
    // 2. Freeze or copy.
    if (index != current_ && (flags & kSlotCrossFadeMember) && row->animGroupTable30 != 0) {
        row->flags48 |= 0x02u;
    } else {
        row->animGroupTable30 = animGroupTable30.get();
        row->animGroupTable34 = animGroupTable34.get();
        if (flags & kSlotCrossFadeMember) row->skeletonSet38 = skeletonSet44.get();
        if (row->aliases) {
            for (size_t a = 0; a < row->aliases->globalIds.size() && a < row->values.size(); ++a) {
                const int16_t id = row->aliases->globalIds[a];
                if (id == -1) continue;
                // The engine does not bound-check here (0x004b0c41) and reads
                // past the block; a port refuses instead (CHOSEN).
                if (id < 0 || static_cast<size_t>(id) >= block_->size())
                    throw std::out_of_range("graph alias id " + std::to_string(id) + " outside the parameter block");
                row->values[a].dword0 = (*block_)[static_cast<size_t>(id)].dword0;
            }
        }
        row->flags48 &= ~0x02u;
    }
    // 3. Time step.
    if (row->evalCounter44 != 0) row->timeStep3c = row->rate40 * timeStep2c.get();
    // 4-5. Evaluate and post: U3 boundary.
    const GraphStepOutput out = evaluator().evaluateGraphAndPost(*this, index, *row, clips);
    Slot& s = slots_[static_cast<size_t>(index)];
    if (out.producedOutput) s.layerHandle = out.dominantLayerHandle;
    // A finished graph state ends itself.
    if (!out.producedOutput && !(s.flags & kSlotCrossFadeMember)) s.weight = 0.0f;
}

void Controller::phaseHandling() {
    if ((flags1a4 & kCtlRestartAtZero) && current_ >= 0) {
        requireLayers("restart-at-zero phase").setLayerPhase(slots_[static_cast<size_t>(current_)].layerHandle, 0.0f);
        flags1a4 &= ~(kCtlRestartAtZero | kCtlPhaseSyncPending);
    }
    if ((flags1a4 & kCtlPhaseSyncPending) && current_ >= 0) {
        LayerApi& L = requireLayers("phase sync");
        std::vector<int32_t> qualifying;
        const int32_t n = inUseCount();
        for (int32_t i = 0; i < n; ++i) {
            if (i == current_) continue;
            const Slot& s = slots_[static_cast<size_t>(i)];
            if ((s.playFlags & kPlayPhaseSync) || (s.flags & kSlotPhaseSync)) qualifying.push_back(i);
        }
        bool applies = true;
        for (int32_t i : qualifying)
            if (L.layerWeight(slots_[static_cast<size_t>(i)].layerHandle) < 1.0f) applies = false;
        if (!applies) return;
        if (qualifying.empty())
            throw OpenStateError("phase sync with no qualifying slot (D undefined)",
                                 "spec-anim-runtime.md Sec6.7 step 6: D comes from 'the last such slot'");
        const Slot& lastSlot = slots_[static_cast<size_t>(qualifying.back())];
        const float d = static_cast<float>(requireClip(requireRecord(lastSlot.record).index08).blendOutFrames) / 30.0f;
        const std::optional<float> l = duration(kIdCurrentSlot);
        float p = phaseOffset1a0.get();
        if (l && *l > 0.0f) {
            p = p - d / *l;
            if (p < 0.0f) p -= std::floor(p);
        }
        L.setLayerPhase(slots_[static_cast<size_t>(current_)].layerHandle, p);
    }
}

}  // namespace sr3animruntime
