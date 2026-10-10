// sr3animpose/pose_compose.h - animation runtime unit U4's own missing
// half: the LAYER-SAMPLER -> COMPOSE pose hand-off (spec-anim-runtime.md
// Sec11, "U4: Time, layers and the pose hand-off"). This is the bridge the
// 2026-10-07 walk-demo investigation (main c665c28/a5e084f) found missing:
// sr3animruntime's node-graph evaluator and sr3playeranim's post step
// already decide WHICH clips play and at WHAT weight/time every frame
// (GraphOutputList/OutputEntry, PostStepResult/PostedClip), but nothing in
// this project ever turned that into a single posed skeleton - the walk
// demo's render was an unconditional rigid bind-pose skin at every frame.
//
// SCOPE OF THIS FILE: the pure, library-agnostic BLEND MATH only (no clip
// decode, no record resolution - that is clip_pose_bridge.h, which calls
// the EXISTING sr3anim::sampleClipAtTime() unmodified and feeds its output
// here). Two stages, matching spec-anim-runtime.md Sec11.0/Sec11.5's own
// split:
//   1. ComposeLayerWeights() - the per-layer EFFECTIVE weight, before any
//      per-bone math.
//   2. ComposePoses() - blends N already-sampled per-bone poses (one
//      sr3anim::BoneSample per bone per layer, produced by the EXISTING
//      sr3anim/sample.h sampler) into ONE final per-bone pose.
//
// THE WEIGHT FORMULA - cited, not invented, but at a documented confidence
// gap this file states plainly rather than hiding:
//
//   spec-anim-runtime.md Sec11.0 says the layer SAMPLER itself
//   (`0x004c21a0`, the function that would contain the real per-track
//   cross-layer blend arithmetic) is "cited, not re-derived" here -
//   spec-anim-format.md Sec6c/Sec10 only traces its PER-TRACK OVERRIDE
//   lookup (the turn-in-place root-orientation mechanism, clip flags bit
//   0x40), not its main per-bone blend-across-layers math. Neither spec
//   gives a confirmed, instruction-level formula for "how do N active
//   layers' sampled poses combine for the SAME bone" at the sampler itself.
//
//   What IS CONFIRMED by full listing, independently, is
//   spec-player-runtime.md Sec5.4.3 step 3's "Per-layer weight" bullet for
//   the ROOT-MOTION layer walk `0x004c58c0` (consumes the SAME per-instance
//   layer array - count/group/weight fields the pose sampler also reads,
//   spec-anim-runtime.md Sec11.8's field map):
//
//     w_i = f_i * weight_i * (1 - min(1, sum_{j: group_j > group_i, j not additive} weight_j))
//
//   i.e. a priority-GROUP masking rule (spec-anim-runtime.md Sec8.6's
//   "layer 1/2/3/(default 4) -> group flag 0x40/0x20/0x10/none" plus
//   Sec5.4.3's "group 0-4, from post flags 0x10/0x20/0x40/0x80 -> 1/2/3/4"):
//   a higher-priority group's own total weight masks away however much of
//   it is already "spent", and whatever's left over is what the lower
//   group still contributes. f_i (the per-track override scalar,
//   `0x004ca620`) is CHOSEN 1.0 here - the override case (clip flags bit
//   0x40) is the same narrow, clip-specific turn-in-place mechanism
//   spec-anim-format.md Sec10 traces, not modelled by this bridge (OPEN,
//   see clip_pose_bridge.h).
//
//   THIS FILE REUSES THAT FORMULA FOR THE POSE BLEND BY STRUCTURAL
//   INFERENCE, NOT BY INDEPENDENT DISASSEMBLY OF THE SAMPLER ITSELF - HIGH
//   confidence (same instance layer array, same group/weight fields, no
//   evidence anywhere of a second, different formula), not CONFIRMED for
//   this specific call site. It specialises EXACTLY to "use the weight
//   unchanged" whenever every active layer shares one group (no layer has
//   a strictly higher group than another) - which is every concrete
//   example either spec gives (a plain cross-fade, Sec8.3 pass 1's
//   "multiply by 1-p / p"; a multi-way blend, Sec8.7's "inputs are not
//   normalised, the authored curves are responsible for summing to 1") and
//   is what this unit's own population test (multiple real locomotion
//   graphs/clips) exercises. The genuinely multi-group case (e.g. a real
//   additive override layer stacked on a locomotion base) is handled by
//   the general formula but UNEXERCISED by any real graph this unit found -
//   see the population-test report for the actual count.
//
// ROTATION COMBINE - NOT a spec-cited formula at all (neither document's
// disassembly reaches this arithmetic); HIGH-confidence standard technique,
// used deliberately rather than guessed-and-hidden: hemisphere-corrected
// weighted quaternion sum, renormalised. This is the conventional way every
// engine of this era blends more than two poses (a pairwise slerp chain and
// a weighted-sum-then-normalise agree exactly at N=2 and the weighted sum
// generalises cleanly to N>2, which a slerp chain does not do associatively).
//
// TRANSLATION COMBINE - a plain weighted sum, deliberately NOT divided by
// the sum of weights. This mirrors spec-player-runtime.md Sec5.4.3 step 3's
// own routing for T (the phase-synchronised/looping-group root-motion sum):
// "T is not normalised" - stated outright, in contrast to E (the other
// bucket), which the SAME passage says IS divided by its own weight sum.
// The pose blend is architecturally closer to T's own case (a single
// skeleton assembled from whichever locomotion-cycle layers are currently
// active) than to E's (a renormalised non-looping remainder), and every
// concrete weight set either spec gives already sums to ~1 by construction
// (see the weight-formula note above), so this choice is moot in every
// case this unit's own tests and population pass actually exercise.
//
// HOST-SAFETY GUARDS (not engine behaviour - this project's own convention,
// e.g. GraphOutputList::overflowGuardCount()): a per-bone weighted
// quaternion sum that cancels to (numerically) zero, or a non-finite
// (NaN/Inf) contributor, is substituted with identity/zero and counted in
// ComposeGuardStats rather than propagated - see ComposePoses()'s own
// comment. Real engines do not need this because real authored weights
// never cancel a legitimate rotation to exactly zero; a reimplementation
// driven by this project's own synthetic/population inputs can.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3anim/sample.h"  // sr3anim::BoneSample, sr3anim::Quat

namespace sr3animpose {

using sr3anim::BoneSample;

// One active layer's inputs to ComposeLayerWeights() - see this header's
// own "THE WEIGHT FORMULA" section above for the exact citation.
struct LayerGroupInput {
    float weight = 0.0f;    // the layer's own final weight (spec-anim-runtime.md Sec8.9 items 3-6)
    int32_t group = 0;      // Sec8.6 + Sec5.4.3's confirmed layer-group mapping (0 = default/no flag)
    bool additive = false;  // layer flag 0x2000 (Additive record, controller.h's kPlayAdditiveLayer) - excluded from the masking sum
};

// spec-player-runtime.md Sec5.4.3 step 3's confirmed "Per-layer weight"
// formula (see file banner), reused here for the pose blend. Returns one
// effective weight per input layer, same order, same size.
std::vector<float> ComposeLayerWeights(const std::vector<LayerGroupInput>& layers);

// HOST-SAFETY GUARD counters from a ComposePoses() call - see file banner.
struct ComposeGuardStats {
    size_t degenerateQuaternionBones = 0;  // weighted quaternion sum too small to normalise safely -> identity substituted
    size_t nonFiniteContributorsSkipped = 0;  // a NaN/Inf BoneSample contributor was excluded from the blend
};

// Blends `perLayerBoneSamples[layer][bone]` (one sr3anim::BoneSample per
// bone, per active layer - EXACTLY the shape sr3anim::sampleClipAtTime()
// already returns, see clip_pose_bridge.h) into one BoneSample per bone,
// using `layerWeights` (ComposeLayerWeights()'s own output, or any
// caller-supplied weights of the same shape). A bone with no layer
// contributing a usable weight keeps BoneSample's own default (identity
// rotation, zero translation delta) - i.e. the bind pose for that bone,
// not a fabricated value.
std::vector<BoneSample> ComposePoses(const std::vector<float>& layerWeights,
                                     const std::vector<std::vector<BoneSample>>& perLayerBoneSamples,
                                     size_t boneCount, ComposeGuardStats* guardStats = nullptr);

}  // namespace sr3animpose
