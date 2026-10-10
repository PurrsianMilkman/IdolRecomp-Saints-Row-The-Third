#include "sr3animpose/clip_pose_bridge.h"

#include <cmath>

#include "sr3anim/sample.h"

namespace sr3animpose {

void LoopPhaseTracker::advance(double dt, double rate, float blendedDurationUnits) {
    if (!(blendedDurationUnits > 0.0f) || !(dt > 0.0)) return;  // nothing to advance against - CHOSEN hold
    const float dtUnits = ClipTimeUnitsFromSeconds(static_cast<float>(dt * rate));
    phase_ = std::fmod(phase_ + dtUnits / blendedDurationUnits, 1.0f);
    if (phase_ < 0.0f) phase_ += 1.0f;
}

int32_t PriorityGroupFromOutputLayer(int32_t outputEntryLayer) {
    switch (outputEntryLayer) {
        case 3: return 1;   // Sec8.6: 3 -> flag 0x10; Sec5.4.3: 0x10 -> group 1
        case 2: return 2;   // 2 -> 0x20 -> group 2
        case 1: return 3;   // 1 -> 0x40 -> group 3
        default: return 0;  // the default 4 (or any other value) -> no flag -> group 0
    }
}

namespace {
bool PassesOutputThreshold(const sr3animruntime::OutputEntry& e) {
    const bool zeroedInTransition = (e.flags & sr3animruntime::kOutZeroedInTransition) != 0;
    return e.weight >= 0.0001f || zeroedInTransition;  // Sec8.9 item 6, same threshold PostOutputList applies
}
}  // namespace

float BlendedLoopingDurationUnits(const sr3animruntime::GraphOutputList& out, ClipSource& clips) {
    int32_t maxGroup = -1;
    for (const sr3animruntime::OutputEntry& e : out.entries()) {
        if ((e.flags & sr3animruntime::kOutLoop) == 0) continue;
        if (!PassesOutputThreshold(e)) continue;
        const int32_t g = PriorityGroupFromOutputLayer(e.layer);
        if (g > maxGroup) maxGroup = g;
    }
    if (maxGroup < 0) return 0.0f;

    double sumW = 0.0, sumWD = 0.0;
    for (const sr3animruntime::OutputEntry& e : out.entries()) {
        if ((e.flags & sr3animruntime::kOutLoop) == 0) continue;
        if (!PassesOutputThreshold(e)) continue;
        if (PriorityGroupFromOutputLayer(e.layer) != maxGroup) continue;
        const ResolvedClip* rc = clips.lookup(e.record);
        if (rc == nullptr) continue;
        const double d = rc->animation->durationTotal();
        sumW += e.weight;
        sumWD += static_cast<double>(e.weight) * d;
    }
    return sumW > 0.0 ? static_cast<float>(sumWD / sumW) : 0.0f;
}

std::vector<ActiveLayer> BuildActiveLayers(const sr3animruntime::GraphOutputList& out,
                                           const LoopPhaseTracker& tracker, ClipSource& clips) {
    std::vector<ActiveLayer> layers;
    layers.reserve(out.size());
    for (const sr3animruntime::OutputEntry& e : out.entries()) {
        if (!PassesOutputThreshold(e)) continue;
        const ResolvedClip* rc = clips.lookup(e.record);
        if (rc == nullptr) continue;  // unresolved record - skip, not an error (Sec8.9's "no output")

        ActiveLayer al;
        al.record = e.record;
        al.weight = e.weight;
        al.group = PriorityGroupFromOutputLayer(e.layer);
        al.additive = false;  // OPEN - see clip_pose_bridge.h's own comment

        const bool loop = (e.flags & sr3animruntime::kOutLoop) != 0;
        if (loop) {
            const float durationUnits = static_cast<float>(rc->animation->durationTotal());
            al.clipTimeUnits = tracker.absoluteTimeUnits(durationUnits);
        } else {
            al.clipTimeUnits = ClipTimeUnitsFromSeconds(e.clipTime);
        }
        layers.push_back(al);
    }
    return layers;
}

PoseComposeResult ComposeClipPose(const std::vector<ActiveLayer>& layers, ClipSource& clips, size_t boneCount) {
    PoseComposeResult result;
    result.bones.assign(boneCount, sr3anim::BoneSample{});

    std::vector<LayerGroupInput> groupInputs;
    std::vector<std::vector<sr3anim::BoneSample>> perLayerSamples;
    groupInputs.reserve(layers.size());
    perLayerSamples.reserve(layers.size());

    for (const ActiveLayer& al : layers) {
        const ResolvedClip* rc = clips.lookup(al.record);
        if (rc == nullptr) {
            ++result.layersSkippedNoClip;
            continue;
        }
        groupInputs.push_back(LayerGroupInput{al.weight, al.group, al.additive});
        perLayerSamples.push_back(
            sr3anim::sampleClipAtTime(rc->bytes, *rc->animation, *rc->payload, boneCount, al.clipTimeUnits));
    }
    if (groupInputs.empty()) return result;

    const std::vector<float> weights = ComposeLayerWeights(groupInputs);
    result.bones = ComposePoses(weights, perLayerSamples, boneCount, &result.guard);
    result.layersActive = groupInputs.size();
    return result;
}

}  // namespace sr3animpose
