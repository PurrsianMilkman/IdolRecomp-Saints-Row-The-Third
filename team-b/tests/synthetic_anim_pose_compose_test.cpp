// Synthetic tests for sr3animpose (animation runtime U4 - the layer-sampler
// -> compose pose hand-off, spec-anim-runtime.md Sec11). Self-contained -
// no game data, no real .anim_pc/.rig_pc. Hand-built BoneSample/OutputEntry
// records with expected results computed independently in this file's own
// comments, the same discipline synthetic_pose_test.cpp uses for the
// hierarchical skinning library this unit feeds.

#include <cmath>
#include <iostream>
#include <vector>

#include "sr3animpose/clip_pose_bridge.h"
#include "sr3animpose/pose_compose.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                         \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n";                      \
            ++g_failures;                                                                                    \
        }                                                                                                     \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                                \
    do {                                                                                                     \
        double av = static_cast<double>(a), bv = static_cast<double>(b);                                     \
        if (std::fabs(av - bv) > (tol)) {                                                                     \
            std::cerr << "CHECK_NEAR FAILED: " #a " (" << av << ") vs " #b << " (" << bv << "), tol " << (tol) \
                      << " at " __FILE__ ":" << __LINE__ << "\n";                                              \
            ++g_failures;                                                                                    \
        }                                                                                                     \
    } while (0)

using sr3anim::BoneSample;
using sr3anim::Quat;
using namespace sr3animpose;

// ---------------------------------------------------------------------------
// ComposeLayerWeights
// ---------------------------------------------------------------------------

void testSingleLayerWeightUnchanged() {
    // One layer, group 0 (the default - no explicit layer override):
    // w_i = f_i * weight_i * (1 - min(1, 0)) = 1.0 * 1.0 * 1.0 = 1.0.
    std::vector<LayerGroupInput> in = {{1.0f, 0, false}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK(w.size() == 1);
    CHECK_NEAR(w[0], 1.0f, 1e-6);
}

void testCrossFadePairSumsUnchanged() {
    // The dominant real case (spec-anim-runtime.md Sec8.3 pass 1's linear
    // cross-fade): two entries, SAME group, weights 0.3/0.7 (sum to 1).
    // Neither masks the other (same group, not "strictly higher"), so both
    // pass through unchanged.
    std::vector<LayerGroupInput> in = {{0.3f, 0, false}, {0.7f, 0, false}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK_NEAR(w[0], 0.3f, 1e-6);
    CHECK_NEAR(w[1], 0.7f, 1e-6);
}

void testHigherGroupMasksLowerGroup() {
    // spec-player-runtime.md Sec5.4.3 step 3: a higher group with full
    // weight masks the lower one entirely. Layer 0 (group 0, weight 1.0)
    // should be masked down to 0 by layer 1 (group 1, weight 1.0):
    // w_0 = 1.0 * 1.0 * (1 - min(1, 1.0)) = 0. w_1 = 1.0 (nothing strictly
    // higher than group 1 here).
    std::vector<LayerGroupInput> in = {{1.0f, 0, false}, {1.0f, 1, false}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK_NEAR(w[0], 0.0f, 1e-6);
    CHECK_NEAR(w[1], 1.0f, 1e-6);
}

void testHigherGroupPartialMask() {
    // Partial mask: higher group at weight 0.4 leaves 0.6 of the lower
    // group's own weight. Lower group weight 1.0:
    // w_0 = 1.0 * (1 - 0.4) = 0.6. w_1 = 0.4.
    std::vector<LayerGroupInput> in = {{1.0f, 0, false}, {0.4f, 1, false}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK_NEAR(w[0], 0.6f, 1e-6);
    CHECK_NEAR(w[1], 0.4f, 1e-6);
}

void testAdditiveLayerExcludedFromMask() {
    // An additive higher-group layer (flag 0x2000) does NOT mask the lower
    // group - it layers on top instead (spec-anim-runtime.md Sec5.4.3's
    // masking sum explicitly excludes additive layers). Both should pass
    // through unchanged despite the group difference.
    std::vector<LayerGroupInput> in = {{1.0f, 0, false}, {1.0f, 1, /*additive=*/true}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK_NEAR(w[0], 1.0f, 1e-6);
    CHECK_NEAR(w[1], 1.0f, 1e-6);
}

void testMaskSumClampedAtOne() {
    // Two higher-group layers summing past 1.0 still clamp the mask at
    // 1.0 (a group cannot be masked below zero weight).
    std::vector<LayerGroupInput> in = {{1.0f, 0, false}, {0.7f, 1, false}, {0.7f, 1, false}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK_NEAR(w[0], 0.0f, 1e-6);
    CHECK_NEAR(w[1], 0.7f, 1e-6);
    CHECK_NEAR(w[2], 0.7f, 1e-6);
}

void testZeroWeightLayerStaysZero() {
    std::vector<LayerGroupInput> in = {{0.0f, 0, false}, {1.0f, 0, false}};
    std::vector<float> w = ComposeLayerWeights(in);
    CHECK_NEAR(w[0], 0.0f, 1e-6);
    CHECK_NEAR(w[1], 1.0f, 1e-6);
}

// ---------------------------------------------------------------------------
// ComposePoses
// ---------------------------------------------------------------------------

BoneSample MakeSample(Quat q, std::array<float, 3> t) {
    BoneSample s;
    s.rotation = q;
    s.translationDelta = t;
    s.hasRotationTrack = true;
    s.hasTranslationTrack = true;
    return s;
}

void testSingleLayerPassthrough() {
    // One bone, one fully-weighted layer: the output must equal the input
    // exactly (weighted sum by 1.0, normalised - a unit quaternion
    // renormalised to itself).
    Quat q{0.0f, 0.0f, 0.70710678f, 0.70710678f};  // 90 deg about Z, already unit
    std::vector<std::vector<BoneSample>> perLayer = {{MakeSample(q, {1.0f, 2.0f, 3.0f})}};
    std::vector<float> weights = {1.0f};
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 1);
    CHECK(out.size() == 1);
    CHECK_NEAR(out[0].rotation.x, q.x, 1e-5);
    CHECK_NEAR(out[0].rotation.y, q.y, 1e-5);
    CHECK_NEAR(out[0].rotation.z, q.z, 1e-5);
    CHECK_NEAR(out[0].rotation.w, q.w, 1e-5);
    CHECK_NEAR(out[0].translationDelta[0], 1.0f, 1e-5);
    CHECK_NEAR(out[0].translationDelta[1], 2.0f, 1e-5);
    CHECK_NEAR(out[0].translationDelta[2], 3.0f, 1e-5);
}

void testTwoLayerTranslationIsWeightedSumNotAverage() {
    // Translation combine is a plain weighted SUM, not divided by the
    // weight sum (file banner's "TRANSLATION COMBINE", mirrors T's own
    // "not normalised" treatment, spec-player-runtime.md Sec5.4.3 step 3).
    // Two layers, weights 0.3 and 0.7 (sum to 1, the dominant real case):
    // result = 0.3*(10,0,0) + 0.7*(0,10,0) = (3,7,0).
    Quat identity{0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<std::vector<BoneSample>> perLayer = {
        {MakeSample(identity, {10.0f, 0.0f, 0.0f})},
        {MakeSample(identity, {0.0f, 10.0f, 0.0f})},
    };
    std::vector<float> weights = {0.3f, 0.7f};
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 1);
    CHECK_NEAR(out[0].translationDelta[0], 3.0f, 1e-5);
    CHECK_NEAR(out[0].translationDelta[1], 7.0f, 1e-5);
    CHECK_NEAR(out[0].translationDelta[2], 0.0f, 1e-5);
}

void testRotationBlendIsUnitQuaternion() {
    // Two different, non-trivial rotations blended at non-trivial weights
    // must still come out as a UNIT quaternion (the core correctness
    // property a weighted sum does not give for free - it must be
    // renormalised).
    Quat qa{0.0f, 0.0f, 0.38268343f, 0.92387953f};  // 45 deg about Z
    Quat qb{0.0f, 0.70710678f, 0.0f, 0.70710678f};  // 90 deg about Y
    std::vector<std::vector<BoneSample>> perLayer = {
        {MakeSample(qa, {0, 0, 0})},
        {MakeSample(qb, {0, 0, 0})},
    };
    std::vector<float> weights = {0.4f, 0.6f};
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 1);
    const double lenSq = double(out[0].rotation.x) * out[0].rotation.x + double(out[0].rotation.y) * out[0].rotation.y +
                         double(out[0].rotation.z) * out[0].rotation.z + double(out[0].rotation.w) * out[0].rotation.w;
    CHECK_NEAR(lenSq, 1.0, 1e-5);
}

void testHemisphereCorrectionAvoidsCancellation() {
    // q and -q are the SAME rotation. Without hemisphere correction,
    // averaging q with -q (at equal weight) cancels to zero instead of
    // reproducing q - this is exactly the degenerate case the HOST-SAFETY
    // GUARD exists for if correction is skipped. With correction, blending
    // a rotation with its own negated-but-identical self at any weight
    // split must reproduce that SAME rotation (up to sign), not identity.
    Quat q{0.0f, 0.0f, 0.60f, 0.80f};  // some unit quaternion
    Quat negQ{-q.x, -q.y, -q.z, -q.w};  // bit-for-bit the same rotation
    std::vector<std::vector<BoneSample>> perLayer = {
        {MakeSample(q, {0, 0, 0})},
        {MakeSample(negQ, {0, 0, 0})},
    };
    std::vector<float> weights = {0.5f, 0.5f};
    ComposeGuardStats guard;
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 1, &guard);
    // Must reproduce q (up to overall sign) - NOT the degenerate-guard
    // identity fallback, and the guard must not have fired.
    CHECK(guard.degenerateQuaternionBones == 0);
    const double dot = double(out[0].rotation.x) * q.x + double(out[0].rotation.y) * q.y +
                       double(out[0].rotation.z) * q.z + double(out[0].rotation.w) * q.w;
    CHECK(std::fabs(dot) > 0.999);  // same rotation, either sign
}

void testDegenerateGuardIsUnreachableByConstruction() {
    // The degenerateQuaternionBones guard (pose_compose.h's own "HOST-
    // SAFETY GUARDS") exists for a weighted quaternion sum that cancels to
    // (numerically) zero - but hemisphere correction against the FIRST
    // finite contributor makes this provably unreachable whenever that
    // contributor's own weight is positive: dot(weightedSum, ref) =
    // sum_i(w_i * dot(v_i, ref)) is a sum of NON-NEGATIVE terms (every
    // v_i was corrected so dot(v_i, ref) >= 0) that includes the
    // reference's own term w_ref * dot(ref, ref) = w_ref * 1 = w_ref > 0 -
    // so the sum's dot with ref is strictly positive, hence the sum itself
    // is never exactly zero. This test exercises several widely-separated
    // real rotations at skewed weights (an adversarial case by ordinary
    // standards) and confirms the guard correctly does NOT fire and the
    // result stays a finite unit quaternion - the guard is defence-in-
    // depth against float-precision edge cases this construction cannot
    // reach, not dead code kept "just in case" without reason.
    Quat q0{0.0f, 0.0f, 0.0f, 1.0f};              // identity
    Quat q1{1.0f, 0.0f, 0.0f, 0.0f};              // 180 deg about X
    Quat q2{0.0f, 0.70710678f, 0.0f, 0.70710678f}; // 90 deg about Y
    Quat q3{0.5f, 0.5f, 0.5f, 0.5f};               // 120 deg about (1,1,1)/sqrt3
    std::vector<std::vector<BoneSample>> perLayer = {
        {MakeSample(q0, {0, 0, 0})},
        {MakeSample(q1, {0, 0, 0})},
        {MakeSample(q2, {0, 0, 0})},
        {MakeSample(q3, {0, 0, 0})},
    };
    std::vector<float> weights = {0.01f, 0.33f, 0.33f, 0.33f};  // reference (q0) deliberately near-zero weight
    ComposeGuardStats guard;
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 1, &guard);
    CHECK(guard.degenerateQuaternionBones == 0);
    const double lenSq = double(out[0].rotation.x) * out[0].rotation.x + double(out[0].rotation.y) * out[0].rotation.y +
                         double(out[0].rotation.z) * out[0].rotation.z + double(out[0].rotation.w) * out[0].rotation.w;
    CHECK(std::isfinite(lenSq));
    CHECK_NEAR(lenSq, 1.0, 1e-4);
}

void testNonFiniteContributorIsSkipped() {
    Quat nanQ{std::nanf(""), 0.0f, 0.0f, 1.0f};
    Quat good{0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<std::vector<BoneSample>> perLayer = {
        {MakeSample(nanQ, {1.0f, 0.0f, 0.0f})},
        {MakeSample(good, {0.0f, 0.0f, 0.0f})},
    };
    std::vector<float> weights = {1.0f, 1.0f};
    ComposeGuardStats guard;
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 1, &guard);
    CHECK(guard.nonFiniteContributorsSkipped == 1);
    CHECK(std::isfinite(out[0].rotation.x));
    CHECK(std::isfinite(out[0].translationDelta[0]));
}

void testNoActiveLayerLeavesBindPose() {
    std::vector<std::vector<BoneSample>> perLayer;  // no layers at all
    std::vector<float> weights;
    std::vector<BoneSample> out = ComposePoses(weights, perLayer, 3);
    CHECK(out.size() == 3);
    for (const BoneSample& b : out) {
        CHECK_NEAR(b.rotation.x, 0.0f, 1e-6);
        CHECK_NEAR(b.rotation.y, 0.0f, 1e-6);
        CHECK_NEAR(b.rotation.z, 0.0f, 1e-6);
        CHECK_NEAR(b.rotation.w, 1.0f, 1e-6);
        CHECK_NEAR(b.translationDelta[0], 0.0f, 1e-6);
    }
}

// ---------------------------------------------------------------------------
// PriorityGroupFromOutputLayer / LoopPhaseTracker
// ---------------------------------------------------------------------------

void testPriorityGroupMapping() {
    // spec-anim-runtime.md Sec8.6 + Sec5.4.3's confirmed mapping.
    CHECK(PriorityGroupFromOutputLayer(3) == 1);
    CHECK(PriorityGroupFromOutputLayer(2) == 2);
    CHECK(PriorityGroupFromOutputLayer(1) == 3);
    CHECK(PriorityGroupFromOutputLayer(4) == 0);  // the default
    CHECK(PriorityGroupFromOutputLayer(999) == 0);  // anything else -> no flag -> group 0
}

void testClipTimeUnitsConversion() {
    // durationSeconds = durationTotal()/30 => timeUnits = seconds*30
    // (clip_pose_bridge.h's own "TIME UNITS" citation).
    CHECK_NEAR(ClipTimeUnitsFromSeconds(1.0f), 30.0f, 1e-6);
    CHECK_NEAR(ClipTimeUnitsFromSeconds(0.5f), 15.0f, 1e-6);
}

void testLoopPhaseTrackerAdvancesAndWraps() {
    LoopPhaseTracker tracker;
    CHECK_NEAR(tracker.phaseUnit(), 0.0f, 1e-6);
    // duration = 30 units (1 second @ 30Hz-frame units), rate = 1.0,
    // dt = 0.5s -> advance 15 units -> phase 0.5.
    tracker.advance(0.5, 1.0, 30.0f);
    CHECK_NEAR(tracker.phaseUnit(), 0.5f, 1e-5);
    CHECK_NEAR(tracker.absoluteTimeUnits(30.0f), 15.0f, 1e-4);
    // Another 0.75s at rate 1.0 -> +22.5 units -> phase (0.5*30+22.5)/30 mod 1 = 37.5/30 mod 1 = 1.25 mod 1 = 0.25.
    tracker.advance(0.75, 1.0, 30.0f);
    CHECK_NEAR(tracker.phaseUnit(), 0.25f, 1e-4);
}

void testLoopPhaseTrackerHoldsWithNoDuration() {
    LoopPhaseTracker tracker;
    tracker.advance(0.5, 1.0, 30.0f);
    const float before = tracker.phaseUnit();
    tracker.advance(0.5, 1.0, 0.0f);  // no active looping layer this frame - hold
    CHECK_NEAR(tracker.phaseUnit(), before, 1e-6);
}

void run(const char* name, void (*fn)()) {
    const int before = g_failures;
    fn();
    if (g_failures == before) std::cout << "  ok: " << name << "\n";
}

}  // namespace

int main() {
    run("testSingleLayerWeightUnchanged", testSingleLayerWeightUnchanged);
    run("testCrossFadePairSumsUnchanged", testCrossFadePairSumsUnchanged);
    run("testHigherGroupMasksLowerGroup", testHigherGroupMasksLowerGroup);
    run("testHigherGroupPartialMask", testHigherGroupPartialMask);
    run("testAdditiveLayerExcludedFromMask", testAdditiveLayerExcludedFromMask);
    run("testMaskSumClampedAtOne", testMaskSumClampedAtOne);
    run("testZeroWeightLayerStaysZero", testZeroWeightLayerStaysZero);
    run("testSingleLayerPassthrough", testSingleLayerPassthrough);
    run("testTwoLayerTranslationIsWeightedSumNotAverage", testTwoLayerTranslationIsWeightedSumNotAverage);
    run("testRotationBlendIsUnitQuaternion", testRotationBlendIsUnitQuaternion);
    run("testHemisphereCorrectionAvoidsCancellation", testHemisphereCorrectionAvoidsCancellation);
    run("testDegenerateGuardIsUnreachableByConstruction", testDegenerateGuardIsUnreachableByConstruction);
    run("testNonFiniteContributorIsSkipped", testNonFiniteContributorIsSkipped);
    run("testNoActiveLayerLeavesBindPose", testNoActiveLayerLeavesBindPose);
    run("testPriorityGroupMapping", testPriorityGroupMapping);
    run("testClipTimeUnitsConversion", testClipTimeUnitsConversion);
    run("testLoopPhaseTrackerAdvancesAndWraps", testLoopPhaseTrackerAdvancesAndWraps);
    run("testLoopPhaseTrackerHoldsWithNoDuration", testLoopPhaseTrackerHoldsWithNoDuration);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic sr3animpose tests passed.\n";
    return 0;
}
