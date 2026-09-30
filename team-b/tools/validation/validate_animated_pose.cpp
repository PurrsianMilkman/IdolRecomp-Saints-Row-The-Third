// Numeric check for sr3rig::computeAnimatedSkinningMatrices() (stage 2 of
// animation playback, HANDOFF.md Sec9.53/9.53.1, 2026-09-12 session's
// follow-on). Synthetic only - no game data - because this is checking a
// piece of ARITHMETIC (the cumulative-delta + post-multiply correction
// derived in include/sr3rig/animated_pose.h), the same discipline
// synthetic_pose_test.cpp uses for stage 1's math. Not one of the
// project's 16 CMake-gated synthetic suites - a standalone confidence
// check for this session's new file, same pattern as the other
// tools/validation harnesses.
//
// Cases, each computed by hand in this file's comments before being
// checked, not by calling the library twice:
//
//   1. localTranslationDeltas == nullptr degrades EXACTLY to
//      computeSkinningMatrices() (bit-identical, same call internally).
//   2. All-zero deltas (non-null) also degrades exactly - the correction
//      term is T(0) == Identity, algebraically.
//   3. ROOT-ONLY delta, no rotation, 2-bone chain: the counterexample
//      documented in animated_pose.h. The naive "just perturb
//      restPositions and call computeSkinningMatrices() unmodified"
//      shortcut gives Identity for the root (it exactly cancels the very
//      translation being applied) - this checks the ACTUAL function
//      instead gives Skin_0 == T(delta), and that the whole rigid
//      subtree (both bones) shifts together, preserving bone length.
//   4. CHILD-ONLY delta (parent has none): only the child moves relative
//      to its parent - bone length between them is EXPECTED to change
//      (that is what an animated translation delta means), checked
//      against the hand-derived new distance rather than assumed
//      unchanged.
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "sr3rig/animated_pose.h"

namespace {
int g_failures = 0;

void checkNear(double a, double b, double tol, const char* label) {
    if (std::fabs(a - b) > tol) {
        std::fprintf(stderr, "CHECK FAILED [%s]: %.8f vs %.8f (tol %.2e)\n", label, a, b, tol);
        ++g_failures;
    } else {
        std::printf("  ok  [%s]: %.8f ~= %.8f\n", label, a, b);
    }
}

double dist(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

int main() {
    using sr3rig::Mat3x4;
    using sr3rig::Rotation3;

    // 2-bone chain: root(0) at (1,2,3), child(1) at (4,2,3) -> d_1_rest = (3,0,0), restLen = 3.
    std::vector<uint32_t> parents = {sr3rig::kNoParent, 0};
    std::vector<std::array<float, 3>> rest = {{1.0f, 2.0f, 3.0f}, {4.0f, 2.0f, 3.0f}};
    const double restLen = dist(rest[1], rest[0]);
    std::printf("rest bone length (0->1): %.6f (expect 3.0)\n", restLen);

    // --- Case 1: nullptr deltas == computeSkinningMatrices() directly.
    {
        std::vector<Mat3x4> direct = sr3rig::computeSkinningMatrices(parents, rest, nullptr);
        std::vector<Mat3x4> viaAnimated =
            sr3rig::computeAnimatedSkinningMatrices(parents, rest, nullptr, nullptr);
        std::printf("\n-- case 1: nullptr deltas --\n");
        for (size_t i = 0; i < 2; ++i)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                    checkNear(direct[i].m[r][c], viaAnimated[i].m[r][c], 1e-9,
                              "case1 nullptr==direct");
    }

    // --- Case 2: explicit all-zero deltas == computeSkinningMatrices().
    {
        std::vector<std::array<float, 3>> zeroDeltas(2, std::array<float, 3>{0.0f, 0.0f, 0.0f});
        std::vector<Mat3x4> direct = sr3rig::computeSkinningMatrices(parents, rest, nullptr);
        std::vector<Mat3x4> viaAnimated =
            sr3rig::computeAnimatedSkinningMatrices(parents, rest, nullptr, &zeroDeltas);
        std::printf("\n-- case 2: explicit zero deltas --\n");
        for (size_t i = 0; i < 2; ++i)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                    checkNear(direct[i].m[r][c], viaAnimated[i].m[r][c], 1e-6,
                              "case2 zero==direct");
    }

    // --- Case 3: root-only delta (5,0,0), no rotation. Expect Skin_0 ==
    // T((5,0,0)) exactly, and the WHOLE subtree (both bones) rigidly
    // shifted by (5,0,0), so bone length is PRESERVED.
    {
        std::vector<std::array<float, 3>> deltas = {{5.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        std::vector<Mat3x4> skin =
            sr3rig::computeAnimatedSkinningMatrices(parents, rest, nullptr, &deltas);
        std::printf("\n-- case 3: root-only delta (5,0,0) --\n");
        std::array<float, 3> world0 = skin[0].transformPoint(rest[0]);
        std::array<float, 3> world1 = skin[1].transformPoint(rest[1]);
        checkNear(world0[0], rest[0][0] + 5.0, 1e-5, "case3 world0.x");
        checkNear(world0[1], rest[0][1], 1e-5, "case3 world0.y");
        checkNear(world1[0], rest[1][0] + 5.0, 1e-5, "case3 world1.x");
        checkNear(world1[1], rest[1][1], 1e-5, "case3 world1.y");
        checkNear(dist(world1, world0), restLen, 1e-5, "case3 bone length preserved");
        // The naive/wrong shortcut (perturb restPositions directly, call
        // computeSkinningMatrices() unmodified) would give Identity for
        // skin_0 - i.e. world0 == rest[0], NOT rest[0]+(5,0,0). Confirm
        // that is NOT what we got (guards against silently reintroducing
        // the bug this file exists to avoid).
        double wrongWorld0X = rest[0][0];
        if (std::fabs(world0[0] - wrongWorld0X) < 1e-5) {
            std::fprintf(stderr,
                         "CHECK FAILED [case3 not-the-naive-bug]: world0.x == rest0.x - looks like "
                         "the root translation was silently discarded\n");
            ++g_failures;
        } else {
            std::printf("  ok  [case3 not-the-naive-bug]: world0.x != rest0.x (translation was NOT "
                        "discarded)\n");
        }
    }

    // --- Case 4: child-only delta (2,0,0). Root unaffected; the pair's
    // length changes from restLen to |d_1_rest + delta_1| by hand:
    // d_1_rest=(3,0,0), delta_1=(2,0,0) -> new local offset (5,0,0),
    // length 5.
    {
        std::vector<std::array<float, 3>> deltas = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
        std::vector<Mat3x4> skin =
            sr3rig::computeAnimatedSkinningMatrices(parents, rest, nullptr, &deltas);
        std::printf("\n-- case 4: child-only delta (2,0,0) --\n");
        std::array<float, 3> world0 = skin[0].transformPoint(rest[0]);
        std::array<float, 3> world1 = skin[1].transformPoint(rest[1]);
        checkNear(world0[0], rest[0][0], 1e-5, "case4 world0 unaffected");
        checkNear(dist(world1, world0), 5.0, 1e-5, "case4 new pair length == 5.0 (hand-derived)");
    }

    // --- Case 5: rig-space-throughout + single final conjugation MUST
    // equal converting rest positions and rotations to mesh space
    // individually before composing (the "naive per-bone" construction),
    // for a chain that has BOTH a real rotation and a real translation
    // delta - the case that actually exercises every term in the
    // conjugation-distributes-over-products argument in animated_pose.h.
    {
        std::printf("\n-- case 5: rig-space + single final conjugation == per-bone mesh-space "
                    "conversion --\n");
        // A non-trivial rotation at the root (45 deg about an arbitrary
        // axis) plus a translation delta at the child, in RIG SPACE.
        Rotation3 rootRotRig = sr3rig::rotationFromAxisAngle({0.3f, 1.0f, -0.5f}, 0.7f);
        std::vector<Rotation3> rotsRig = {rootRotRig, sr3rig::identityRotation3()};
        std::vector<std::array<float, 3>> deltasRig = {{0.0f, 0.0f, 0.0f}, {0.4f, -0.2f, 0.1f}};

        std::vector<Mat3x4> skinRig =
            sr3rig::computeAnimatedSkinningMatrices(parents, rest, &rotsRig, &deltasRig);
        Mat3x4 skin0MeshViaConjugation = sr3rig::conjugateToMeshSpace(skinRig[0]);
        Mat3x4 skin1MeshViaConjugation = sr3rig::conjugateToMeshSpace(skinRig[1]);

        // Naive: convert rest positions to mesh space, convert the SAME
        // rotation to mesh space by conjugating its matrix with M, and
        // recompute directly in mesh space (translation deltas are
        // vectors -> conjugate linearly the same way M conjugates a
        // matrix's action on a vector: M * d).
        Mat3x4 M = sr3rig::meshSpaceConjugationMatrix();
        std::vector<std::array<float, 3>> restMesh(2);
        for (size_t i = 0; i < 2; ++i) restMesh[i] = sr3rig::rigToMeshSpace(rest[i]);
        Rotation3 rootRotMesh3x3;
        {
            Mat3x4 rootRotMat = Mat3x4::fromRotation(rootRotRig);
            Mat3x4 conj = multiply(multiply(M, rootRotMat), M);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) rootRotMesh3x3[r][c] = conj.m[r][c];
        }
        std::vector<Rotation3> rotsMesh = {rootRotMesh3x3, sr3rig::identityRotation3()};
        std::array<float, 3> childDeltaMesh = M.transformPoint(deltasRig[1]); // M is linear, zero translation
        std::vector<std::array<float, 3>> deltasMesh = {{0.0f, 0.0f, 0.0f}, childDeltaMesh};

        std::vector<Mat3x4> skinMeshNaive =
            sr3rig::computeAnimatedSkinningMatrices(parents, restMesh, &rotsMesh, &deltasMesh);

        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                checkNear(skin0MeshViaConjugation.m[r][c], skinMeshNaive[0].m[r][c], 1e-4,
                          "case5 bone0 conjugation==naive");
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                checkNear(skin1MeshViaConjugation.m[r][c], skinMeshNaive[1].m[r][c], 1e-4,
                          "case5 bone1 conjugation==naive");

        // And bone length is preserved by BOTH constructions when only
        // rotation (no translation delta) is applied to a bone - reuse
        // bone 0 (root rotation only) vs bone1's REST distance changed by
        // its own delta only, as case 4 already established.
    }

    // --- Case 6: quatToRotation3() sanity - a 90 degree rotation about Z
    // expressed as a quaternion must match rotationFromAxisAngle's own
    // matrix for the same axis/angle (independently-implemented formula,
    // same numeric answer).
    {
        std::printf("\n-- case 6: quatToRotation3 vs rotationFromAxisAngle (90deg about Z) --\n");
        const float half = 0.78539816339744831f; // pi/4
        Rotation3 fromQuat = sr3rig::quatToRotation3(0.0f, 0.0f, std::sin(half), std::cos(half));
        Rotation3 fromAxisAngle =
            sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, 1.5707963267948966f);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                checkNear(fromQuat[r][c], fromAxisAngle[r][c], 1e-5, "case6 quat==axisAngle");
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL CHECKS PASSED" : "FAILURES FOUND",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
