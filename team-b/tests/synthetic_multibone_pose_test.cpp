// Multi-bone composition regression for sr3rig/pose.h and
// sr3rig/animated_pose.h - added while investigating HANDOFF.md Sec9.56
// ("real .anim_pc playback with ALL bones animated simultaneously
// explodes; isolating one bone at a time looks clean").
//
// THE GAP THIS CLOSES. Before this file, no test in the project - not
// this file's siblings (synthetic_pose_test.cpp), not
// tools/validation/validate_pose.cpp, not tools/validation/
// validate_animated_pose.cpp - had ever exercised
// computeSkinningMatrices() or computeAnimatedSkinningMatrices() with TWO
// OR MORE bones simultaneously carrying REAL, DIFFERENT, non-identity
// rotations. Every confirmatory test to date rotated exactly one bone (or
// one bone's whole rigid subtree) at a time, which cannot expose a bug in
// how DIFFERENT bones' rotations compose down the hierarchy relative to
// each other - only in how a single rotation propagates to its
// descendants.
//
// RESULT (see HANDOFF Sec9.56.1): both functions are EXACT for the
// general multi-bone case, to ordinary float tolerance. This suite is the
// permanent record of that finding, not a reaction to a bug found here -
// the actual root cause of the reported fragmentation turned out to be
// elsewhere entirely (near-degenerate rest-pose seams between
// independently-animated body parts, e.g. two hands close together in
// the bind pose - see that section). This suite exonerates
// computeSkinningMatrices()/computeAnimatedSkinningMatrices() for the
// multi-bone case specifically, which is what it is for.
//
// Every expected value below was hand-derived in the comments (not by
// calling the library twice) before being checked, the same discipline
// synthetic_pose_test.cpp and tests/synthetic_rig_test.cpp use.
#include <cmath>
#include <iostream>
#include <vector>

#include "sr3rig/animated_pose.h"
#include "sr3rig/pose.h"

namespace {

int g_failures = 0;

#define CHECK_NEAR(a, b, tol)                                                                    \
    do {                                                                                         \
        double av = static_cast<double>(a), bv = static_cast<double>(b);                         \
        if (std::fabs(av - bv) > (tol)) {                                                        \
            std::cerr << "CHECK_NEAR FAILED: " #a " (" << av << ") vs " #b << " (" << bv          \
                      << "), tol " << (tol) << " at " __FILE__ ":" << __LINE__ << "\n";           \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

constexpr float kPi = 3.14159265f;

void checkPoint(const std::array<float, 3>& got, const std::array<float, 3>& expect, double tol,
                const char* label) {
    (void)label;
    CHECK_NEAR(got[0], expect[0], tol);
    CHECK_NEAR(got[1], expect[1], tol);
    CHECK_NEAR(got[2], expect[2], tol);
}

// --- Test 1: computeSkinningMatrices(), THREE simultaneous non-identity
// rotations (two about the SAME axis on a parent/child pair, one about a
// DIFFERENT axis on a second child), across a chain-plus-branch
// hierarchy.
//
//   bone0 root                 rest (0,0,0)
//   bone1 child of 0            rest (1,0,0)   d1 = (1,0,0)
//   bone2 child of 1 (branch A) rest (2,0,0)   d2 = (1,0,0)
//   bone3 child of 1 (branch B) rest (1,1,0)   d3 = (0,1,0)
//
//   R0 = 90deg about +Z, R1 = 90deg about +Z (SAME axis, DIFFERENT bone -
//        the case a single-bone/single-subtree test can never exercise),
//   R2 = identity, R3 = 90deg about +X (a third, differently-axised
//        rotation on a second branch off bone1).
//
// Rotation convention (established by synthetic_pose_test.cpp's
// testMat3x4Basics: 90deg about +Z sends +X -> +Y):
//   about +Z, 90deg: (x,y,z) -> (-y, x, z)
//   about +X, 90deg: (x,y,z) -> (x, -z, y)
//
// Derivation (World_i = World_parent * T(d_i) * R_i; root: World_i =
// T(d_i)*R_i; Skin_i.transform(p) = World_i.transform(p - rest_i)):
//   World_0.transform(v) = R0(v)                         [d0 = rest0 = 0]
//   World_1.transform(v) = R0(d1) + R0(R1(v))            [R0(d1)=R0(1,0,0)=(0,1,0)]
//   World_2.transform(v) = World_1.transform(d2 + v)     [R2 = I]
//   World_3.transform(v) = World_1.transform(d3 + R3(v))
//
//   Skin_0(rest0=(0,0,0)) = World_0(0) = (0,0,0)
//   Skin_1(rest1=(1,0,0)) = World_1(0) = R0(d1) = (0,1,0)
//   Skin_2(rest2=(2,0,0)) = World_1(d2+0=(1,0,0))
//       = R0(d1) + R0(R1((1,0,0)));  R1((1,0,0))=(0,1,0);  R0((0,1,0))=(-1,0,0)
//       = (0,1,0)+(-1,0,0) = (-1,1,0)
//   Skin_2((2,1,0)) [p-rest2=(0,1,0)] = World_1(d2+(0,1,0)=(1,1,0))
//       = R0(d1) + R0(R1((1,1,0)));  R1((1,1,0))=(-1,1,0);  R0((-1,1,0))=(-1,-1,0)
//       = (0,1,0)+(-1,-1,0) = (-1,0,0)
//   Skin_3(rest3=(1,1,0)) = World_1(d3+R3(0)=(0,1,0))
//       = R0(d1) + R0(R1((0,1,0)));  R1((0,1,0))=(-1,0,0);  R0((-1,0,0))=(0,-1,0)
//       = (0,1,0)+(0,-1,0) = (0,0,0)
//   Skin_3((1,1,1)) [p-rest3=(0,0,1)] = World_1(d3+R3((0,0,1)))
//       R3((0,0,1))=(0,-1,0);  d3+(0,-1,0) = (0,1,0)+(0,-1,0) = (0,0,0)
//       = World_1((0,0,0)) = R0(d1) = (0,1,0)
void testMultiBoneRotationOnly() {
    std::vector<uint32_t> parents = {sr3rig::kNoParent, 0, 1, 1};
    std::vector<std::array<float, 3>> rest = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};

    sr3rig::Rotation3 r90Z = sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f);
    sr3rig::Rotation3 r90X = sr3rig::rotationFromAxisAngle({1.0f, 0.0f, 0.0f}, kPi / 2.0f);
    std::vector<sr3rig::Rotation3> rots = {r90Z, r90Z, sr3rig::identityRotation3(), r90X};

    std::vector<sr3rig::Mat3x4> skin = sr3rig::computeSkinningMatrices(parents, rest, &rots);

    checkPoint(skin[0].transformPoint(rest[0]), {0.0f, 0.0f, 0.0f}, 1e-5, "skin0(rest0)");
    checkPoint(skin[1].transformPoint(rest[1]), {0.0f, 1.0f, 0.0f}, 1e-5, "skin1(rest1)");
    checkPoint(skin[2].transformPoint(rest[2]), {-1.0f, 1.0f, 0.0f}, 1e-5, "skin2(rest2)");
    checkPoint(skin[2].transformPoint({2.0f, 1.0f, 0.0f}), {-1.0f, 0.0f, 0.0f}, 1e-5, "skin2((2,1,0))");
    checkPoint(skin[3].transformPoint(rest[3]), {0.0f, 0.0f, 0.0f}, 1e-5, "skin3(rest3)");
    checkPoint(skin[3].transformPoint({1.0f, 1.0f, 1.0f}), {0.0f, 1.0f, 0.0f}, 1e-5, "skin3((1,1,1))");
}

// --- Test 2: computeAnimatedSkinningMatrices(), TWO bones simultaneously
// carrying BOTH a real non-identity rotation AND a real translation
// delta - not just one bone rotated (validate_animated_pose.cpp case 5)
// with a translation delta elsewhere, but both at once, on more than one
// bone.
//
//   3-bone chain: root(0) -> child(1) -> grandchild(2).
//   rest0=(0,0,0), rest1=(2,0,0) [d1_rest=(2,0,0)], rest2=(2,3,0) [d2_rest=(0,3,0)]
//   R0 = 90deg about +Z, R1 = 90deg about +X, R2 = identity
//   delta0=(1,0,0), delta1=(0,2,0), delta2=(0,0,0)
//
// Ground truth (independent of computeAnimatedSkinningMatrices's own
// cumDelta/restAnimated/correction internals - this is just "what an
// animated hierarchical pose means"):
//   d_i_local = (rest_i - rest_parent(i)) + delta_i   (root: rest_i + delta_i)
//   World_i = World_parent * T(d_i_local) * R_i       (root: T(d_i_local)*R_i)
//   Skin_i.transform(p) = World_i.transform(p - rest_i)   (rest_i = TRUE rest)
//
//   d0_local = (0,0,0)+(1,0,0) = (1,0,0)
//   d1_local = (2,0,0)+(0,2,0) = (2,2,0)
//   d2_local = (0,3,0)+(0,0,0) = (0,3,0)
//
//   World_0.transform(v) = (1,0,0) + R0(v)
//   World_1.transform(v) = (1,0,0) + R0( d1_local + R1(v) )
//   World_2.transform(v) = World_1.transform( d2_local + v )      [R2 = I]
//
//   Skin_0(rest0=(0,0,0)) = World_0(0) = (1,0,0)
//
//   Skin_1(rest1=(2,0,0)) = World_1(0) = (1,0,0) + R0(d1_local)
//       R0((2,2,0)) = (-2,2,0)  =>  (1,0,0)+(-2,2,0) = (-1,2,0)
//
//   Skin_1((2,0,1)) [p-rest1=(0,0,1)]
//       R1((0,0,1)) = (0,-1,0); d1_local+(0,-1,0) = (2,1,0); R0((2,1,0)) = (-1,2,0)
//       = (1,0,0)+(-1,2,0) = (0,2,0)
//
//   Skin_2(rest2=(2,3,0)) = World_2(0) = World_1(d2_local+0=(0,3,0))
//       R1((0,3,0)) = (0,0,3); d1_local+(0,0,3) = (2,2,3); R0((2,2,3)) = (-2,2,3)
//       = (1,0,0)+(-2,2,3) = (-1,2,3)
//
//   Skin_2((3,3,0)) [p-rest2=(1,0,0)]
//       World_1(d2_local+(1,0,0)=(1,3,0))
//       R1((1,3,0)) = (1,0,3); d1_local+(1,0,3) = (3,2,3); R0((3,2,3)) = (-2,3,3)
//       = (1,0,0)+(-2,3,3) = (-1,3,3)
void testMultiBoneRotationAndTranslation() {
    std::vector<uint32_t> parents = {sr3rig::kNoParent, 0, 1};
    std::vector<std::array<float, 3>> rest = {
        {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 0.0f}};
    sr3rig::Rotation3 r90Z = sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f);
    sr3rig::Rotation3 r90X = sr3rig::rotationFromAxisAngle({1.0f, 0.0f, 0.0f}, kPi / 2.0f);
    std::vector<sr3rig::Rotation3> rots = {r90Z, r90X, sr3rig::identityRotation3()};
    std::vector<std::array<float, 3>> deltas = {
        {1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

    std::vector<sr3rig::Mat3x4> skin =
        sr3rig::computeAnimatedSkinningMatrices(parents, rest, &rots, &deltas);

    checkPoint(skin[0].transformPoint(rest[0]), {1.0f, 0.0f, 0.0f}, 1e-4, "skin0(rest0)");
    checkPoint(skin[1].transformPoint(rest[1]), {-1.0f, 2.0f, 0.0f}, 1e-4, "skin1(rest1)");
    checkPoint(skin[1].transformPoint({2.0f, 0.0f, 1.0f}), {0.0f, 2.0f, 0.0f}, 1e-4, "skin1((2,0,1))");
    checkPoint(skin[2].transformPoint(rest[2]), {-1.0f, 2.0f, 3.0f}, 1e-4, "skin2(rest2)");
    checkPoint(skin[2].transformPoint({3.0f, 3.0f, 0.0f}), {-1.0f, 3.0f, 3.0f}, 1e-4, "skin2((3,3,0))");
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    run("testMultiBoneRotationOnly", testMultiBoneRotationOnly);
    run("testMultiBoneRotationAndTranslation", testMultiBoneRotationAndTranslation);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic multi-bone pose tests passed.\n";
    return 0;
}
