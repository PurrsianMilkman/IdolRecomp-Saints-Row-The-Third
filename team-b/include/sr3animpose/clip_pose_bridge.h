// sr3animpose/clip_pose_bridge.h - wires sr3animruntime's real node-graph
// output (GraphOutputList/OutputEntry, spec-anim-runtime.md Sec8) to the
// EXISTING clip sampler (sr3anim::sampleClipAtTime(), sr3anim/sample.h) and
// this unit's own pose_compose.h, producing one posed skeleton per frame -
// the "U4: Time, layers and the pose hand-off" bridge spec-anim-runtime.md
// Sec11 specifies and this project never implemented (see pose_compose.h's
// own file banner for the full background).
//
// WHAT THIS FILE DOES NOT DO: decode a clip's bytes (sr3anim/payload.h +
// sample.h, UNMODIFIED), or run the skinning matrices (sr3rig/
// animated_pose.h, UNMODIFIED). It only (a) resolves a graph record index
// to the real clip data a caller supplies (ClipSource, OPEN by default -
// this project has no persistent clip-record registry anywhere), (b)
// recovers a usable absolute sample TIME for a looping clip (neither
// OutputEntry nor PostedClip carries one - see LoopPhaseTracker's own
// comment), and (c) calls pose_compose.h's ComposeLayerWeights()/
// ComposePoses() with the result.
//
// TIME UNITS - the one genuinely CONFIRMED numeric conversion this file
// introduces, cited from two already-independent places in this codebase
// rather than invented:
//   * spec-anim-runtime.md Sec6.1: "Duration, in seconds, of what the slot
//     plays... 0x004bd8c0(record +0x08) = clip header word +0x06 / 30".
//   * sr3anim/sample.h's own "timeUnits" contract: sampleClipAtTime()'s
//     `timeUnits` parameter is "the same unidentified unit as
//     Animation::durationTotal()" - the SAME header field (+0x06) the
//     passage above divides by 30 to get seconds.
//   Together: durationSeconds = durationTotal()/30, so
//   timeUnits = durationTotal() = durationSeconds * 30. This file's
//   ClipTimeUnitsFromSeconds() is exactly that multiply, nothing more.
#pragma once

#include <cstdint>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3animpose/pose_compose.h"
#include "sr3animruntime/node_graph_runtime.h"
#include "vpp/byte_view.h"

namespace sr3animpose {

// A real, already-parsed+walked clip, as sr3anim::sampleClipAtTime() needs
// it. Ownership stays with whatever built the ClipSource (e.g. a registry
// that keeps the archive bytes/Animation/Payload alive for the demo's own
// lifetime) - this is a non-owning view.
struct ResolvedClip {
    const sr3anim::Animation* animation = nullptr;
    const sr3anim::Payload* payload = nullptr;
    vpp::ByteView bytes;
};

// Resolves a graph/animation RECORD index (OutputEntry::record) to real
// clip data. OPEN-shaped by convention (no OpenStateError thrown here -
// an unresolved record is "skip this layer", matching OutputEntry's own
// "no output" handling, spec-anim-runtime.md Sec8.9) rather than an error:
// this project has no persistent clip-record/clip-cache registry anywhere
// (spec-anim-runtime.md's own `DAT_030f0c68` clip-cache entries stay OPEN
// throughout that spec), so a caller that wants real animation supplies
// its own mapping - see tools/sr3_viewer.cpp's RealClipRegistry for the
// walk-demo's own concrete instantiation against real preload_anim.vpp_pc
// clips.
class ClipSource {
public:
    virtual ~ClipSource() = default;
    // Returns nullptr for an unresolved record.
    virtual const ResolvedClip* lookup(int32_t record) = 0;
};

constexpr float kClipTimeUnitsPerSecond = 30.0f;  // see file banner's "TIME UNITS"
inline float ClipTimeUnitsFromSeconds(float seconds) { return seconds * kClipTimeUnitsPerSecond; }

// spec-anim-runtime.md Sec8.6's "the post step maps 1 -> layer-group flag
// 0x40, 2 -> 0x20, 3 -> 0x10, and anything else (the default 4) -> none",
// read together with Sec5.4.3 step 3's "group (0-4, from post flags
// 0x10/0x20/0x40/0x80 -> 1/2/3/4)": OutputEntry::layer's real priority
// group, for pose_compose.h's ComposeLayerWeights(). CONFIRMED by full
// listing (both passages cite disassembly); reused for the pose path by
// the same structural inference pose_compose.h's own file banner documents
// for the weight formula as a whole.
int32_t PriorityGroupFromOutputLayer(int32_t outputEntryLayer);

// Owns the ONE shared, phase-synchronised clock a looping clip's own
// absolute sample time needs. Neither sr3animruntime::OutputEntry nor
// sr3playeranim::PostedClip carries a usable absolute time for a looping
// clip - both are "0 for a looping entry", BY DESIGN (see PostedClip's own
// doc comment in sr3playeranim/bridge.h: it mirrors OutputEntry's
// convention exactly). The real engine instead gives every phase-
// synchronised layer a SHARED clock (spec-player-runtime.md Sec5.4.3 step
// 3, confirmed by full listing of `0x004c3d20`): "advances one shared
// normalised phase (instance +0x60) at the instance rate +0x68 over the
// weight-blended duration of the highest group present, wraps it modulo 1,
// and sets every member's time to phase x its own duration." This class is
// that missing clock - this project has no animation-instance object to
// put +0x60/+0x68 on (sr3playeranim/bridge.h's own file banner), so it
// lives here, owned by whatever drives the per-frame bridge (e.g.
// alongside a sr3playeranim::PlayerAnimBridge).
class LoopPhaseTracker {
public:
    // `rate` = the SAME playback rate Sec5.4.3 step 7 writes to instance
    // +0x68 (sr3playeranim::PlayerAnimBridge::playbackRate()). `dt` is in
    // seconds. `blendedDurationUnits` = the weight-blended duration (this
    // file's own "timeUnits") of the active looping layers in the highest
    // group present - see BlendedLoopingDurationUnits() below. A
    // non-positive duration or dt leaves the phase unchanged (nothing to
    // advance against - CHOSEN, the spec does not describe this
    // complement either, same "hold" convention bridge.h's
    // PlaybackRateTracker::update() already uses).
    void advance(double dt, double rate, float blendedDurationUnits);
    float phaseUnit() const { return phase_; }  // [0, 1)
    float absoluteTimeUnits(float clipDurationUnits) const { return phase_ * clipDurationUnits; }

private:
    float phase_ = 0.0f;
};

// The weight-blended duration (Sec5.4.3's own phrase) of the active
// phase-synchronised (kOutLoop) entries in the highest priority group
// present among them, weighted by their own OutputEntry::weight. Uses the
// SAME weight threshold Sec8.9 item 6 / PostOutputList already apply.
// Returns 0 when no looping entry is active (LoopPhaseTracker::advance()
// then leaves the phase unchanged).
float BlendedLoopingDurationUnits(const sr3animruntime::GraphOutputList& out, ClipSource& clips);

// One active layer, fully resolved and time-stamped, ready to sample.
struct ActiveLayer {
    int32_t record = -1;
    float weight = 0.0f;    // final per Sec8.9 items 3-6 (same threshold PostOutputList already applies)
    int32_t group = 0;
    bool additive = false;  // OPEN - see BuildActiveLayers()'s own comment; CHOSEN false
    float clipTimeUnits = 0.0f;
};

// Builds the active-layer list straight from a frame's GraphOutputList,
// replicating PostOutputList's own threshold handling (player_anim_bridge.cpp,
// Sec8.9 item 6) rather than going through PostStepResult/PostedClip:
// PostedClip's own CHOSEN SIMPLIFICATION (sr3playeranim/bridge.h) collapses
// both the priority-group field (OutputEntry::layer) and a usable looping-
// clip time to a constant, which this bridge needs back - going straight to
// GraphOutputList recovers both without touching that already-real, already-
// tested file. `tracker` must already have been advanced for THIS frame
// (see LoopPhaseTracker/BlendedLoopingDurationUnits) before calling this.
// Non-looping entries use their own real OutputEntry::clipTime directly
// (Sec8.6 - already correct, untouched). `additive` is OPEN: no per-record
// Additive-flag registry exists in this project (bridge.h's own established
// gap - no real record/duration resolution pipeline), so every entry is
// CHOSEN non-additive here; see this unit's own report for why that does
// not understate the walk-demo's or the population test's own real graphs.
std::vector<ActiveLayer> BuildActiveLayers(const sr3animruntime::GraphOutputList& out,
                                           const LoopPhaseTracker& tracker, ClipSource& clips);

// The full U4 bridge result.
struct PoseComposeResult {
    std::vector<sr3anim::BoneSample> bones;  // size == boneCount; feed straight into sr3rig::computeAnimatedSkinningMatrices
    size_t layersActive = 0;
    size_t layersSkippedNoClip = 0;
    ComposeGuardStats guard;
};

// Samples every active layer's clip at its own time (sr3anim::sampleClipAtTime,
// UNMODIFIED), computes ComposeLayerWeights(), and blends with
// ComposePoses(). A layer whose record does not resolve via `clips` is
// skipped (not an error - matches OutputEntry's own "no output" handling,
// Sec8.9). `boneCount` must match the target rig.
PoseComposeResult ComposeClipPose(const std::vector<ActiveLayer>& layers, ClipSource& clips, size_t boneCount);

}  // namespace sr3animpose
