#include "sr3animpose/pose_compose.h"

#include <algorithm>
#include <cmath>

namespace sr3animpose {

std::vector<float> ComposeLayerWeights(const std::vector<LayerGroupInput>& layers) {
    std::vector<float> w(layers.size(), 0.0f);
    for (size_t i = 0; i < layers.size(); ++i) {
        if (!(layers[i].weight > 0.0f)) continue;
        // spec-player-runtime.md Sec5.4.3 step 3: "Sigma over the LATER
        // layers of a strictly higher group". The real instance layer
        // array is kept sorted by ascending group (Sec5.4.3: "kept sorted
        // by group, insertion in 0x004c60e0"), so for a correctly-sorted
        // array "later AND strictly higher group" is exactly equivalent to
        // "strictly higher group" alone - this loop does not depend on the
        // CALLER's own array order, matching that equivalence rather than
        // re-deriving a sort this header has no reason to impose.
        float maskSum = 0.0f;
        for (size_t j = 0; j < layers.size(); ++j) {
            if (j == i || layers[j].additive) continue;
            if (layers[j].group > layers[i].group) maskSum += layers[j].weight;
        }
        if (maskSum > 1.0f) maskSum = 1.0f;
        const float fi = 1.0f;  // override scalar (0x004ca620) - CHOSEN, see pose_compose.h banner
        const float wi = fi * layers[i].weight * (1.0f - maskSum);
        w[i] = wi > 0.0f ? wi : 0.0f;  // defensive: a fraction of a non-negative weight is never negative
    }
    return w;
}

namespace {
bool IsFiniteVec3(const std::array<float, 3>& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
bool IsFiniteQuat(const sr3anim::Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}
}  // namespace

std::vector<BoneSample> ComposePoses(const std::vector<float>& layerWeights,
                                     const std::vector<std::vector<BoneSample>>& perLayerBoneSamples,
                                     size_t boneCount, ComposeGuardStats* guardStats) {
    std::vector<BoneSample> out(boneCount);  // default: identity rotation, zero delta (bind pose)
    const size_t numLayers = layerWeights.size();

    for (size_t b = 0; b < boneCount; ++b) {
        float sumW = 0.0f;
        std::array<float, 3> accT{0.0f, 0.0f, 0.0f};
        float accQ[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bool haveRef = false;
        float ref[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool anyRotTrack = false, anyTransTrack = false;

        for (size_t l = 0; l < numLayers; ++l) {
            const float wl = layerWeights[l];
            if (!(wl > 1e-6f)) continue;
            if (l >= perLayerBoneSamples.size() || b >= perLayerBoneSamples[l].size()) continue;
            const BoneSample& s = perLayerBoneSamples[l][b];
            if (!IsFiniteQuat(s.rotation) || !IsFiniteVec3(s.translationDelta)) {
                if (guardStats) ++guardStats->nonFiniteContributorsSkipped;
                continue;  // HOST-SAFETY GUARD: exclude rather than poison the whole bone
            }
            float q[4] = {s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w};
            // Hemisphere correction: q and -q represent the identical
            // rotation, but summing mismatched hemispheres cancels instead
            // of blending - standard practice for N-way quaternion
            // blending (see file banner's "ROTATION COMBINE").
            if (!haveRef) {
                ref[0] = q[0]; ref[1] = q[1]; ref[2] = q[2]; ref[3] = q[3];
                haveRef = true;
            } else {
                const float dot = q[0] * ref[0] + q[1] * ref[1] + q[2] * ref[2] + q[3] * ref[3];
                if (dot < 0.0f) { q[0] = -q[0]; q[1] = -q[1]; q[2] = -q[2]; q[3] = -q[3]; }
            }
            accQ[0] += wl * q[0];
            accQ[1] += wl * q[1];
            accQ[2] += wl * q[2];
            accQ[3] += wl * q[3];
            accT[0] += wl * s.translationDelta[0];
            accT[1] += wl * s.translationDelta[1];
            accT[2] += wl * s.translationDelta[2];
            sumW += wl;
            if (s.hasRotationTrack) anyRotTrack = true;
            if (s.hasTranslationTrack) anyTransTrack = true;
        }

        if (sumW > 1e-6f) {
            BoneSample& ob = out[b];
            const float lenSq = accQ[0] * accQ[0] + accQ[1] * accQ[1] + accQ[2] * accQ[2] + accQ[3] * accQ[3];
            if (lenSq > 1e-12f) {
                const float invLen = 1.0f / std::sqrt(lenSq);
                ob.rotation = sr3anim::Quat{accQ[0] * invLen, accQ[1] * invLen, accQ[2] * invLen, accQ[3] * invLen};
            } else {
                // HOST-SAFETY GUARD: opposing-weight quaternions cancelled
                // to (numerically) zero - no well-defined direction to
                // normalise. Not real engine behaviour (see file banner);
                // substitute identity and count it rather than emit NaN.
                if (guardStats) ++guardStats->degenerateQuaternionBones;
                ob.rotation = sr3anim::Quat{0.0f, 0.0f, 0.0f, 1.0f};
            }
            ob.translationDelta = accT;  // un-normalised - see file banner's "TRANSLATION COMBINE"
            ob.hasRotationTrack = anyRotTrack;
            ob.hasTranslationTrack = anyTransTrack;
        }
        // else: no layer contributed usable weight for this bone - leave
        // at BoneSample's own default (identity/zero), exactly the
        // existing sampler's own "untracked bone" convention
        // (sr3anim/sample.h: "Identity when the bone has no track").
    }
    return out;
}

}  // namespace sr3animpose
