#pragma once

// sr3animruntime/controller.h - animation runtime unit U1: the controller's
// state slots and the play / query API (spec-anim-runtime.md Sec6, SPEC TEAM
// 2026-10-03), plus the U0 per-controller parameter block, typed setters and
// child ring (Sec3), which are controller state. The spec is the ONLY source;
// every address or offset in a comment is a cross-reference to its text.
//
// WHAT IS REAL HERE
//   * U0 Sec3.1-Sec3.4: the 8-byte-per-parameter block (+0x28), lazy zero-filled
//     allocation by the managed plays, the three typed setters with their
//     exact order (null block -> drop BEFORE the child ring; ring replay; id /
//     type check; store), no clamp.
//   * U0 Sec3.5 / Sec6.10 steps 1-3: graph instancing seeds registry defaults;
//     the per-slot graph step's guards, freeze rule, alias-wise copy of the
//     block into the instance, and the time-step write.
//   * Sec6.2 slot layout (the fields that matter functionally) and flag table.
//   * Sec6.3 slot-array management: contiguous in-use prefix, free by moving
//     the last in-use slot into the hole (with link / cursor fix-ups), growth
//     by one with no cap, the reaper.
//   * Sec6.4 id space and slot finder, all seven partitions.
//   * Sec6.5 play family: 0x004b1b90, 0x004b21a0, 0x004b2280 (with its
//     lowest-weight cross-fade-member eviction), 0x004b1ef0, 0x004b2a20 /
//     0x004b2b50.
//   * Sec6.6 query family: 0x004b4ce0, 0x004b41b0, 0x004b4320, 0x004b44a0, and
//     the blend-input setters 0x004b72a0 / 0x004b7320 up to the evaluator.
//   * Sec6.7 the controller update 0x004b3720: instance bump, cross-fade
//     (crossfade.h), the per-slot loop (re-trigger gate, weighting step with
//     explicit-weight fade-out, clip de-duplication, link scaling, quadratic
//     easing, posting), graph step, parameter-block release, phase handling.
//
// BOUNDARIES (injected; nothing below guesses their bodies)
//   * AnimDataSource - the loaded-animation records (spec-tables-animation.md
//     Sec3.3 record +0x08 / +0x2c / +0x24..+0x27), the state-id -> record map
//     0x004b1630, clip header timing, blend-tree rows, and the U2 graph alias
//     tables. Data, not engine code.
//   * LayerApi - the controller's animation instance and its layers (+0x20):
//     U4's subject (Sec5.1 item 14). A null LayerApi is "+0x20 invalid".
//   * EvaluationBoundary - NOT YET IMPLEMENTED pieces, each a clearly
//     labelled call-through whose default implementation THROWS
//     sr3luahost::OpenStateError (this project's OPEN-state discipline; never
//     a silent no-op):
//       - the node-graph evaluation + output posting of Sec6.10 steps 4-5
//         (0x004b8850, 0x004cc6c0 x2, layer posting) - U3;
//       - the graph clip-node collection 0x004cc000 (Sec6.7 step 4) - U3;
//       - the named-clip-node search of the finder's 7,589..7,602 partition
//         (node instances live in the graph instance) - U3;
//       - (formerly stubbed, now REAL in the defaults) the 1-D blend-tree
//         evaluator 0x004b0080 (Sec6.9) and the blend-input snap's "set every
//         State weight to its curve value"; only their Action branch is OPEN,
//         and their phase lock calls the LayerApi phase hooks (U4, OPEN).
//     Tests inject a fake to exercise everything around them.
//     U3's implementation is NodeGraphEvaluator (node_graph_runtime.h): the
//     node-graph evaluation (both passes of 0x004cc6c0) and the named
//     clip-node search are real there; the post step (Sec6.10 step 5, U4)
//     and 0x004cc000's collection remain OPEN.
//
// OPEN (reads throw OpenStateError; see each member's comment): the controller
// fields +0x2c, +0x30, +0x34, +0x44 and +0x1a0 whose writers/initial values are
// not specced for U1, and the record-flag summaries +0x198 / +0x19c (the rules
// of 0x004b3570 are summarised, not specified, in Sec6.1 / Sec6.7 step 3.2).
// The re-trigger 0x004b2d70 and +0x70 arming for records with byte +0x27 != 0
// throw too: Sec6.8 finds that machinery inert in shipped data (+0x27 = 0 for
// every record) and its arming formula is cited from Q13.2, which is not a
// spec.
//
// 2026-10-07: U5/U4/U6/U7 (Sec9-Sec12) checked against this file, no new
// ground found here. All four new units describe what happens to a SAMPLED
// POSE (bone matrices, a rig's own hierarchy, an animation-instance pool with
// a real per-layer array) after this evaluator's output list is produced:
// Sec9/Sec12 are the human spine-bend/look chain and motion-warp corrections
// (bone rotations); Sec10 is the vehicle/object_rig/held-weapon pose clients
// (same, plus a per-bone matrix overlay); Sec11 is the pose-request queue and
// the bone-hierarchy compose pipeline. This project has no rig/skeleton model
// and does not decode `.anim_pc` keyframe tracks (sr3anim's own README), so
// none of that is implementable here without inventing the missing object -
// exactly the boundary LayerApi already exists to inject rather than guess.
// The one LayerApi-relevant finding from this pass is the Sec6-vs-Sec11.7
// address contradiction on setLayerRate/setLayerPhase, flagged at each
// virtual below rather than resolved by guessing. Sec8's two precision fixes
// (output-list margin 30->31, the 0.4 s ramp 12->13 updates) were both
// already exactly what this project's own code and tests produce; see
// NodeGraphRuntime::multiWayPass1's ramp comment (anim_node_graph_runtime.cpp)
// and the output-list-bound test in real_anim_node_graph_test.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3animruntime/control_parameters.h"
#include "sr3luahost/open_state.h"

namespace sr3anim {
class Animation;
}

namespace sr3animruntime {

using sr3luahost::OpenStateError;
using sr3luahost::OpenValue;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Animation-record flag word +0x2c bits used by U1 (spec-tables-animation.md
// Sec3.2 / Sec3.3 as quoted by Sec6.1).
inline constexpr uint32_t kRecAdditive = 1u << 0;
inline constexpr uint32_t kRecTransitionAction = 1u << 3;
inline constexpr uint32_t kRecNoInterpolation = 1u << 6;
inline constexpr uint32_t kRecTransitionTo50 = 1u << 27;
inline constexpr uint32_t kRecBlendTree = 1u << 30;
inline constexpr uint32_t kRecManaged = 1u << 31;

// Slot flags +0x80 (Sec6.2 table).
inline constexpr uint32_t kSlotInUse = 0x01;
inline constexpr uint32_t kSlotCrossFadeMember = 0x02;
inline constexpr uint32_t kSlotExplicitWeight = 0x04;
inline constexpr uint32_t kSlotBlendTree = 0x08;
inline constexpr uint32_t kSlotManaged = 0x10;
inline constexpr uint32_t kSlotTransitionClip = 0x20;
inline constexpr uint32_t kSlotLinkedChild = 0x40;
inline constexpr uint32_t kSlotReplacePartner = 0x80;
inline constexpr uint32_t kSlotChildOwnLayer = 0x100;
inline constexpr uint32_t kSlotChildFadeInPending = 0x200;
inline constexpr uint32_t kSlotPhaseSync = 0x400;
inline constexpr uint32_t kSlotChildFadingOut = 0x800;
inline constexpr uint32_t kSlotClears1a4Bit10 = 0x1000;
inline constexpr uint32_t kSlotFreeHook = 0x2000;

// Play flags (Sec6.2 +0x14, Sec6.5, Sec6.7).
inline constexpr uint32_t kPlayNoSameStateReuse = 0x400;    // 0x004b1b90 / 0x004b1ef0: do not reuse the same-key slot
inline constexpr uint32_t kPlayAdditiveLayer = 0x2000;      // added for Additive records
inline constexpr uint32_t kPlayPhaseSync = 0x4000;          // -> slot flag 0x400
inline constexpr uint32_t kPlayNoInterpolationLayer = 0x10000;  // added for No_Interpolation records
inline constexpr uint32_t kPlayRawClipIndex = 0x80000;      // 0x004b1ef0: first argument is a clip index
inline constexpr uint32_t kPlaySetsCurrentPair = 0x200000;  // +0x60/+0x64

// Controller +0x1a4 bits (Sec6.1).
inline constexpr uint32_t kCtl1a4Bit10 = 0x10;
inline constexpr uint32_t kCtlPhaseSyncPending = 0x40;
inline constexpr uint32_t kCtlRestartAtZero = 0x200;

// Slot-finder id space (Sec6.4). 7,589 and 7,603 are game-init constants
// (registry fields +0x100c / +0x1024), CONFIRMED by static read.
inline constexpr int32_t kIdTagFirst = 7500;
inline constexpr int32_t kIdClipNodeFirst = 7589;
inline constexpr int32_t kIdClipNodeEnd = 7603;
inline constexpr int32_t kIdCurrentSlot = 10001;
inline constexpr int32_t kIdOutgoingSlot = 10002;
// The 14 named clip-node aliases, id = 7,589 + index (Sec6.4).
inline constexpr const char* kNamedClipNodes[14] = {
    "Default", "Stand To Run", "Run 180", "Sprint 180", "Transition", "Climb", "VTOL Jet",
    "VTOL Hover", "Ride Face Forward", "Ride Face Back", "Ride Forward to Back", "Ride Back to Forward",
    "Fire", "Motorcycle Stand",
};
// 0x004b84f0's name -> id half (Sec6.4): case-insensitive compare with the 14
// names; 7,589 + k or -1. (The hashing step it does first is an
// implementation detail with no observable effect on the result.)
int32_t NamedClipNodeId(const std::string& aliasName);

// ---------------------------------------------------------------------------
// Data boundary
// ---------------------------------------------------------------------------

// The fields of the 0x44-byte loaded-animation record U1 reads.
struct AnimRecordInfo {
    uint32_t flags = 0;    // +0x2c
    int32_t index08 = -1;  // +0x08: clip (secondary-cache) index; blend-tree row or graph index for those kinds
    uint8_t byte24 = 0;    // +0x24 / +0x25: re-trigger delay [min, max] x 180 ms (Sec6.8)
    uint8_t byte25 = 0;
    uint8_t byte26 = 0;    // +0x26: sub-action probability / 255
    uint8_t byte27 = 0;    // +0x27: sub-action entry count; 0 for every shipped record (Sec6.8)
};

// Clip header timing (Sec6.11 / Sec5.3): word +0x06 = duration in 30 Hz
// frames (CONFIRMED, 0x004bd8c0); bytes +0x08 / +0x09 = blend-in / blend-out
// lengths in 30 Hz frames (HIGH, from use).
struct ClipTiming {
    uint16_t durationFrames = 0;
    uint8_t blendInFrames = 0;
    uint8_t blendOutFrames = 0;
    float durationSeconds() const { return static_cast<float>(durationFrames) / 30.0f; }
};
// The hook into this project's existing clip reader: sr3anim::Animation
// already exposes header +0x06 / +0x08 / +0x09 (include/sr3anim/animation.h).
ClipTiming ClipTimingFromAnimation(const sr3anim::Animation& clip);

// The blend-tree row fields U1 itself reads (spec-tables-animation.md Sec7.3).
struct BlendTreeRowInfo {
    float controlInputLow = 0.0f;   // Control_input_low (row +0x04)
    float controlInputHigh = 0.0f;  // Control_input_high (row +0x08)
    float rampSpeed = 0.0f;         // Ramp_speed (row +0x0c; Sec6.9 step 1)
    std::vector<int32_t> stateClips;   // per State, its clip index (at most 5, Sec6.2)
    std::vector<int32_t> actionClips;  // per Action (none shipped, Sec6.11; at most 2)
    // Per State, parallel to stateClips (the 0x3c-byte State entry: +0x0c
    // Blend_time, +0x10 Control_point count, +0x14 five Range, +0x28 five
    // Value floats). Read by the 1-D evaluator (Sec6.9 step 3).
    struct StateCurve {
        float blendTime = 0.0f;
        int32_t pointCount = 0;
        float range[5] = {0, 0, 0, 0, 0};
        float value[5] = {0, 0, 0, 0, 0};
    };
    std::vector<StateCurve> stateCurves;
};

class AnimDataSource {
public:
    virtual ~AnimDataSource() = default;
    virtual std::optional<AnimRecordInfo> record(int32_t recordIndex) const = 0;
    // 0x004b1630: state/action id -> record index, or -1.
    virtual int32_t stateToRecord(int32_t stateId) const = 0;
    virtual std::optional<ClipTiming> clipTiming(int32_t clipIndex) const = 0;
    virtual const BlendTreeRowInfo* blendTreeRow(int32_t row) const = 0;
    // The alias table of node-graph definition `graphIndex` (U2's file
    // format; nullptr = the definition has no alias table).
    virtual const GraphAliasTable* graphAliases(int32_t graphIndex) const = 0;
};

// ---------------------------------------------------------------------------
// Layer boundary (U4)
// ---------------------------------------------------------------------------
class LayerApi {
public:
    virtual ~LayerApi() = default;
    virtual void bumpInstance() = 0;                 // 0x004c64d0(instance, 0)
    virtual bool poseInFlight() const = 0;           // instance +0x6c bit 0x02
    // 0x004bce30: create a plain clip layer for a play; returns its handle
    // (serial << 4). f1..f3 are consumed here (Q13.2's OPEN, Sec5.1 item 14).
    virtual int32_t createClipLayer(int32_t clipIndex, uint32_t playFlags, float f1, float f2, float f3) = 0;
    // 0x004c6590(instance, clip, layerFlags, weight, a, b): find or create the
    // clip's layer and raise its posted weight to `weight` if larger; handle.
    virtual int32_t postClip(int32_t clipIndex, uint32_t layerFlags, float weight, float a, float b) = 0;
    virtual void fadeOutLayer(int32_t handle, float seconds) = 0;  // 0x004c6d90 / 0x004c6c70
    // Addresses per Sec6.6 (stop-with-fade: "the layer's rate is set to 0.0
    // (0x004be510, layer +0x18)"), Sec6.7 ("set the current layer's phase...
    // to 0.0 / to p (0x004c7510)") and Sec6.8 (a child's rate "scaled by...
    // (0x004be510)") - three independent, mutually consistent Sec6 passages,
    // matching these call sites' own semantics (setLayerRate(h, 0.0f) to
    // freeze; setLayerPhase(h, 0.0f) / setLayerPhase(h, p) to restart/sync a
    // phase). NOT CHANGED to match U4 Sec11.7, which names the SAME two
    // addresses with the roles swapped (0x004c7510 "the rate setter", storing
    // at a newly-named layer +0x1c; 0x004be510 "the explicit-phase setter",
    // storing at layer +0x18) and does not cross-reference or acknowledge
    // Sec6 at all. This is an unresolved internal contradiction between two
    // CONFIRMED-labelled passages of the same spec file, found while checking
    // U4 against this project's existing code - relayed, not silently
    // resolved either way (no clean-room way to re-disassemble and settle it
    // here). Behaviour is unaffected either way: the real address is a
    // citation on an abstract virtual, not something this port calls by
    // address.
    virtual void setLayerRate(int32_t handle, float rate) = 0;     // 0x004be510 per Sec6.6/6.8; Sec11.7 says 0x004c7510
    virtual void setLayerPhase(int32_t handle, float phase) = 0;   // 0x004c7510 per Sec6.7; Sec11.7 says 0x004be510
    virtual float layerWeight(int32_t handle) const = 0;           // 0x004bdb50 (layer +0x24)
    // 0x004bdd40: a THIRD reading, not yet reconciled with the other two.
    // Original: instance invalid, or layer missing / inactive with zero
    // weight. Changelog 2026-10-03: "the second condition is no play time
    // left (0x004c6350), not zero weight." Sec11.7 (2026-10-07, "CONFIRMED by
    // full listing past its first ret"): "returns true iff the handle is -1,
    // out of pool bounds, or fails the self-index check" - an instance-handle
    // validity test with no second, layer-specific condition at all. All
    // three may be describing different call sites/signatures of the same
    // address rather than one function; not resolved here, flagged for relay.
    virtual bool layerGone(int32_t handle) const = 0;
    // Instance +0x5c x 16, or nullopt when the instance holds no layer.
    virtual std::optional<int32_t> heldLayerHandle() const = 0;
    // 0x004c7090 on one layer / on every layer.
    virtual bool triggerFired(int32_t handle, int32_t triggerId, int32_t occurrence) const = 0;
    virtual bool triggerFiredAnyLayer(int32_t triggerId, int32_t occurrence) const = 0;
    // The 1-D blend tree's phase lock (Sec6.9 step 3 "Phase"): 0x004c75c0
    // aligns a posted State layer to the slot's shared phase, 0x004be6e0 reads
    // a layer's phase back. U4's; the defaults THROW OpenStateError.
    virtual void alignLayerToPhase(int32_t handle, float sharedPhase);
    virtual float readLayerPhase(int32_t handle) const;
};

// ---------------------------------------------------------------------------
// Graph instances (U0 Sec3.5, U1 Sec6.10)
// ---------------------------------------------------------------------------
// One 0x5c-byte node instance (U3, Sec8.2): every piece of per-instance graph
// state. Keyed by (graph, node, transition) words; graph -1 = free. A fresh
// or freed instance holds each field's "none" value from Sec8.2's table.
struct NodeInstance {
    int16_t graph = -1;          // +0x00 key (-1 = free)
    int16_t node = -1;           // +0x02 (-1: a state machine's own record)
    int16_t transition = -1;     // +0x04 (-1: not a transition)
    float activeTime = 0.0f;     // +0x08 seconds; a transition's elapsed time
    uint32_t lastEvaluated = 0;  // +0x0c row +0x44 when last advanced
    int32_t current = -1;        // +0x10 state machine: current state; state node: transition in progress
    float phaseOffset = -1.0f;   // +0x14 (-1.0 = none), from cyclic_percent
    int32_t active[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // +0x18 a state node's active-transition list
    float clipTime = 0.0f;       // +0x28
    float prevClipTime = 0.0f;   // +0x2c
    float layerPhase = 0.0f;     // +0x30 (0.0 = none)
    int32_t record = -1;         // +0x34 animation record the clip plays
    int32_t clipIndex = -1;      // +0x38 written by the post step (U1 Sec6.10 step 5)
    int32_t layerHandle = -1;    // +0x3c written by the post step; -1 = none
    uint32_t flags40 = 0;        // +0x40 (bit 0 cleared every frame by 0x004cca80)
    float blendWeights[5] = {0, 0, 0, 0, 0};  // +0x48 multi-way blender input weights (at most 5 inputs)
    bool isFree() const { return graph == -1; }
};

// One row of the 128-row pool 0x03133a90 (stride 0x4c). The fields U0/U1
// read or write, plus U3's per-row state (node instances, filter outputs,
// the root-motion sync fields).
struct GraphInstance {
    int32_t graphIndex = -1;                   // +0x04
    int32_t instanceHandle = -1;               // +0x08 (controller +0x20)
    const GraphAliasTable* aliases = nullptr;  // the definition's alias table (+0x1c / +0x20)
    std::vector<ParamSlot> values;             // +0x0c: 8 x aliases; dword +0 value, byte +4 toggle request
    uint32_t animGroupTable30 = 0;  // +0x30: 0 at instancing (Sec6.10: evaluation refuses until the copy sets it)
    uint32_t animGroupTable34 = 0;  // +0x34
    uint32_t skeletonSet38 = 0;     // +0x38 (controller +0x44)
    float timeStep3c = 0.0f;        // +0x3c = 0.0 at instancing
    float rate40 = 1.0f;            // +0x40 = 1.0 (0x0135101c)
    uint32_t evalCounter44 = 0;     // +0x44 cleared
    uint32_t flags48 = 1;           // +0x48 = 1; bit 0x02 = parameters frozen (Sec6.10); bit 0x04 = a match_name clip emitted (Sec8.6)
    // U3 (Sec8.1 / Sec8.2 / Sec8.3 / Sec8.8):
    std::vector<NodeInstance> nodeInstances;  // +0x14 count / +0x18 array (0 at instancing)
    // The filter instances' outputs (+0x04 of each 8-byte object), one per
    // entry of the ROOT graph's filter list, raw dwords zeroed at instancing
    // (Sec8.8). CHOSEN: sized lazily by the evaluator on the row's first
    // evaluation (the pool does not see the definition); no evaluator reads
    // them before that, so the zero start is the same.
    std::vector<uint32_t> filterOutputs;
    uint8_t rootMotionSync1c = 0;     // +0x1c (Sec8.3 step 6)
    float rootMotionElapsed20 = 0.0f;  // +0x20
    float rootMotionLength24 = 0.0f;   // +0x24
};

class GraphInstancePool {
public:
    static constexpr int32_t kCapacity = 128;
    // 0x004cbd20: take a free row, store the graph index and instance handle,
    // allocate one 8-byte slot per alias and seed every alias whose global id
    // has a registry record with that record's DEFAULT dword (byte +4
    // cleared). An alias with an invalid id is left as allocated - CHOSEN zero
    // stand-in (the pool 0x03519890's contents are not specced). Throws
    // OpenStateError when all 128 rows are live (exhaustion is not specced).
    int32_t instantiate(int32_t graphIndex, int32_t instanceHandle, const GraphAliasTable* aliases,
                        const ControlParameterRegistry& registry);
    void release(int32_t row);  // 0x004cc590
    GraphInstance* row(int32_t row);
    const GraphInstance* row(int32_t row) const;
    int32_t liveCount() const;

private:
    std::vector<std::optional<GraphInstance>> rows_ = std::vector<std::optional<GraphInstance>>(kCapacity);
};

// ---------------------------------------------------------------------------
// The frame-local clip list (Sec6.7 step 3.3): up to 32 (clip, weight) pairs
// shared by the plain, blend-tree and graph steps of one update.
// ---------------------------------------------------------------------------
class ClipDedupList {
public:
    static constexpr size_t kCapacity = 32;
    // A clip already listed gets min(listed + w, 1) and that sum is returned
    // (and kept); a new one is appended with w. A 33rd distinct clip throws
    // OpenStateError (overflow behaviour is not specced).
    float add(int32_t clip, float weight);
    const std::vector<std::pair<int32_t, float>>& entries() const { return entries_; }

private:
    std::vector<std::pair<int32_t, float>> entries_;
};

// 0x00dad960(t, 1.0, 0.5) on t in [0, 1]: 2t^2 for t <= 1/2, 1 - 2(1 - t)^2
// above (Sec6.7 step 3.3, CONFIRMED by evaluating the listing).
float EaseQuadraticInOut(float t);

// ---------------------------------------------------------------------------
// The 0x84-byte state slot (Sec6.2). Pointers are kept as slot indices
// (-1 = null); "rebased by grow and by compaction" is then automatic for
// grow and done explicitly by free.
// ---------------------------------------------------------------------------
struct ActionRecord {  // +0x3c + 0x14 k (HIGH layout)
    int32_t countdown = 0;
    float weight = 0.0f;
    float progress = 0.0f;
    int32_t layerHandle = 0;
    uint32_t flags = 0;  // bit 0 active, bit 1 ending
};

struct Slot {
    int32_t record = -1;          // +0x00 animation-record index; -1 in an empty slot
    int32_t layerHandle = 0;      // +0x04
    int32_t stateId = 0;          // +0x08 state/action id; -1 when started by record index
    int32_t tag = 0;              // +0x0c caller tag
    float weight = 0.0f;          // +0x10
    uint32_t playFlags = 0;       // +0x14
    float blendInputInit = 0.0f;     // +0x18
    float blendInputTarget = 0.0f;   // +0x1c
    float blendInputCurrent = 0.0f;  // +0x20
    float blendPhase = 0.0f;         // +0x24
    float stateWeights[5] = {0, 0, 0, 0, 0};  // +0x28..+0x3b
    ActionRecord actions[2];                  // +0x3c..+0x63
    bool blendInputInitialised = false;       // +0x64
    int32_t blendTreeRow = -1;     // +0x68 (port: row index; -1 = null pointer)
    int32_t graphRow = -1;         // +0x6c
    int32_t retriggerDeadline = 0;   // +0x70 (-1 = disarmed; a zeroed slot holds 0)
    int32_t subActionDeadline = 0;   // +0x74
    int32_t linkedPartner = -1;      // +0x78 (port: slot index; -1 = null)
    uint32_t flags = 0;              // +0x80

    bool inUse() const { return (flags & kSlotInUse) != 0; }
};

// Result of the graph evaluation + posting boundary (Sec6.10 step 5).
struct GraphStepOutput {
    bool producedOutput = false;       // did the evaluation produce any output entry
    int32_t dominantLayerHandle = -1;  // the highest-weighted layer posted (becomes slot +0x04)
};

class Controller;

// The not-yet-implemented evaluation boundary. Every default THROWS
// OpenStateError naming the missing unit; see the file banner.
class EvaluationBoundary {
public:
    virtual ~EvaluationBoundary() = default;
    // Sec6.10 steps 4-5. U3.
    virtual GraphStepOutput evaluateGraphAndPost(Controller& controller, int32_t slotIndex, GraphInstance& row,
                                                 ClipDedupList& clips);
    // Sec6.7 step 4, 0x004cc000 over every live graph row. U3.
    virtual void collectGraphClipNodes(GraphInstancePool& pool);
    // Sec6.4 node search: the first node index >= firstNode of `row` whose
    // clip-node definition carries alias id `id`, or -1. U3.
    virtual int32_t findNamedClipNode(const GraphInstance& row, int32_t id, int32_t firstNode);
    // Sec6.9, 0x004b0080: REAL in the default (no longer a stub). Input
    // ramp, per-State curve target and Blend_time ramp, posting (slot weight
    // x State weight, de-duplicated, NOT eased) through LayerApi::postClip,
    // the dominant layer into slot +0x04, then the phase lock through
    // LayerApi::alignLayerToPhase / readLayerPhase. A row with `Actions` is
    // OPEN (Sec6.9 step 2: summarised, not specified; none shipped).
    virtual void evaluateBlendTree(Controller& controller, int32_t slotIndex, ClipDedupList& clips);
    // Sec6.6 snap: "sets every State weight directly to its curve value
    // (Sec6.9)" at the slot's current input +0x20. REAL in the default.
    virtual void snapBlendTreeStateWeights(Controller& controller, int32_t slotIndex);
};

// ---------------------------------------------------------------------------
// The controller
// ---------------------------------------------------------------------------
class Controller {
public:
    struct Environment {
        const ControlParameterRegistry* registry = nullptr;  // required
        const AnimDataSource* data = nullptr;                // required for plays / update
        GraphInstancePool* graphPool = nullptr;              // required for managed plays
        LayerApi* layers = nullptr;    // null == instance handle +0x20 invalid
        int32_t instanceHandle = -1;   // +0x20
        EvaluationBoundary* evaluator = nullptr;  // null -> the throwing default
    };
    explicit Controller(const Environment& env);
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // ---- child ring (+0x08) ------------------------------------------------
    // Children are visited in insertion order, depth-first (Sec3.2 step 2).
    // What attaches children in the game is OPEN (Sec5.1 item 4).
    void addChild(Controller* child) { children_.push_back(child); }
    const std::vector<Controller*>& children() const { return children_; }

    // ---- U0: parameter block (+0x28) and setters (Sec3) --------------------
    bool hasParameterBlock() const { return block_.has_value(); }
    const std::vector<ParamSlot>* parameterBlock() const { return block_ ? &*block_ : nullptr; }
    // Sec3.4: allocate 8 x count bytes, zero-filled, only when +0x28 is null.
    // Called by the managed plays; public so a test or a later writer pass
    // can set up a controller state directly.
    void allocateParameterBlock();
    // 0x004b1410 / 0x004b1490 / 0x004b1510. Order (Sec3.2): no block ->
    // return BEFORE the ring; replay on every child; id -1 -> return; no
    // record or a different type -> return (type-mismatched writes are
    // silently dropped); store. No clamp, no change detection.
    void setBool(int16_t id, bool value);
    void setInt(int16_t id, int32_t value);
    void setFloat(int16_t id, float value);
    // Port-side readers (the engine has none, Sec3.2): the block slot as the
    // per-frame copy would read it. nullopt when there is no block, the id
    // has no record, or the record is of another type. Bools test the low
    // byte only.
    std::optional<bool> getBool(int16_t id) const;
    std::optional<int32_t> getInt(int16_t id) const;
    std::optional<float> getFloat(int16_t id) const;
    // 0x004b1590: controller +0x2c, no child-ring step (Sec3.6).
    void setTimeStep(float seconds) { timeStep2c.set(seconds); }

    // ---- controller fields with no U1-specced writer / initial value -------
    OpenValue<float> timeStep2c{"anim controller +0x2c (time step)",
                                "spec-anim-runtime.md Sec3.6: written by 0x004b1590 before the update"};
    OpenValue<uint32_t> animGroupTable30{"anim controller +0x30 (anim-group table)",
                                         "spec-anim-runtime.md Sec3.5: copied to instance +0x30; writer not specced"};
    OpenValue<uint32_t> animGroupTable34{"anim controller +0x34 (anim-group table)",
                                         "spec-anim-runtime.md Sec3.5: copied to instance +0x34; writer not specced"};
    OpenValue<uint32_t> skeletonSet44{"anim controller +0x44 (skeleton-set id)",
                                      "spec-anim-runtime.md Sec3.5 / Sec6.5: copied to instance +0x38; writer not specced"};
    OpenValue<float> phaseOffset1a0{"anim controller +0x1a0 (phase offset)",
                                    "spec-anim-runtime.md Sec6.1: 0.0 or 0.5; writer not specced"};
    // +0x198 / +0x19c: the record-flag summaries. Reset to OPEN by every
    // update's slot loop and by a reaper pass that frees a slot; never set,
    // because 0x004b3570's per-kind rules ("current or non-blending ones",
    // the 0x004bddb0 override mask) are not specified to implementation level.
    OpenValue<uint32_t> recordFlagsAll198{"anim controller +0x198 (record-flag summary)",
                                          "spec-anim-runtime.md Sec6.1 / Sec6.7 step 3.2: 0x004b3570 not specified"};
    OpenValue<uint32_t> recordFlagsCurrent19c{"anim controller +0x19c (record-flag summary)",
                                              "spec-anim-runtime.md Sec6.1 / Sec6.7 step 3.2: 0x004b3570 not specified"};

    // ---- slot array (Sec6.1 / Sec6.2 / Sec6.3) -----------------------------
    const std::vector<Slot>& slots() const { return slots_; }
    Slot& slot(int32_t index) { return slots_.at(static_cast<size_t>(index)); }
    int32_t inUseCount() const;                       // length of the in-use prefix
    int32_t currentSlot() const { return current_; }    // +0x50 (-1 = null)
    int32_t outgoingSlot() const { return outgoing_; }  // +0x54
    float crossFadeDuration() const { return duration58_; }  // +0x58
    float lastCrossFadeRate() const { return rate5c_; }      // +0x5c
    uint8_t managedSlotCount() const { return managed24_; }  // +0x24
    int32_t currentPairState() const { return pair60_; }     // +0x60
    int32_t currentPairLayer() const { return pair64_; }     // +0x64
    // +0x1a4. CHOSEN: 0 on construction (the controller constructor is not
    // specced; every bit has a specced clearer).
    uint32_t flags1a4 = 0;
    // Test hook standing in for pool 0x03424868 exhaustion: growth fails
    // once the array holds this many slots. Unset = no cap (Sec6.3: "only pool
    // exhaustion stops growth").
    std::optional<size_t> slotPoolLimit;

    // 0x004afc70 (Sec6.3).
    void freeSlot(int32_t index);
    // 0x004b11a0: grow by exactly one zeroed slot; false on pool exhaustion.
    bool growSlots();
    // 0x004b4030, the reaper; called by clients after update().
    void reap();

    // ---- Sec6.4 ------------------------------------------------------------
    // 0x004afa20(controller, firstSlot, id, outNode, firstNode).
    int32_t findSlot(int32_t firstSlot, int32_t id, int32_t* outNode = nullptr, int32_t firstNode = 0);

    // ---- Sec6.5 play family -------------------------------------------------
    int32_t playState(int32_t stateId, int32_t tag, uint32_t playFlags, float f1, float f2, float f3);   // 0x004b1b90
    void crossFadeToState(int32_t stateId, int32_t tag, int32_t transitionStateId, int32_t transitionTag,
                          float blendSeconds, uint8_t restart);                                         // 0x004b21a0
    void crossFadeToRecord(int32_t record, int32_t tag, int32_t transitionRecord, int32_t transitionTag,
                           float blendSeconds, uint8_t restart);                                        // 0x004b2280
    int32_t playRecord(int32_t record, int32_t tag, uint32_t playFlags, float f1, float f2, float f3);  // 0x004b1ef0
    int32_t setExplicitWeight(int32_t stateId, int32_t tag, float weight, uint32_t playFlags);          // 0x004b2a20
    int32_t setExplicitWeightRecord(int32_t record, int32_t tag, float weight, uint32_t playFlags);     // 0x004b2b50

    // ---- Sec6.6 query family ------------------------------------------------
    bool isPlaying(int32_t id);                                   // 0x004b4ce0
    void stop(int32_t id, float seconds, bool freeze);            // 0x004b41b0
    bool triggerFired(int32_t triggerId, int32_t id, int32_t occurrence);  // 0x004b4320
    std::optional<float> duration(int32_t id);                    // 0x004b44a0
    void setBlendInput(int32_t id, float value);                  // 0x004b72a0 (snap)
    void setBlendInputTarget(int32_t id, float value);            // 0x004b7320

    // ---- Sec6.7 -------------------------------------------------------------
    void update();  // 0x004b3720

    const Environment& environment() const { return env_; }

private:
    LayerApi& requireLayers(const char* what) const;
    const AnimDataSource& data() const;
    EvaluationBoundary& evaluator();
    AnimRecordInfo requireRecord(int32_t record) const;
    ClipTiming requireClip(int32_t clipIndex) const;
    int32_t acquireSlot(int32_t matchIndex);
    int32_t firstFreeOrGrow();
    int32_t instantiateGraph(int32_t graphIndex);
    static uint32_t layerFlagsFor(uint32_t playFlags, const AnimRecordInfo& rec);
    int32_t armRetrigger(const AnimRecordInfo& rec) const;
    void startPhaseSyncFlag(Slot& s, uint32_t playFlags, const AnimRecordInfo& rec);
    void snapBlendInput(int32_t index, float value);
    bool blendInputSlot(int32_t index) const;

    void crossFadeStep();
    void retrigger(int32_t index);
    void weightingStep(int32_t index, ClipDedupList& clips);
    void graphStep(int32_t index, ClipDedupList& clips);
    void phaseHandling();

    Environment env_;
    std::vector<Controller*> children_;
    std::optional<std::vector<ParamSlot>> block_;
    std::vector<Slot> slots_;
    int32_t current_ = -1;
    int32_t outgoing_ = -1;
    float duration58_ = 0.0f;
    float rate5c_ = 0.0f;
    uint8_t managed24_ = 0;
    int32_t pair60_ = -1;
    int32_t pair64_ = -1;
};

}  // namespace sr3animruntime
