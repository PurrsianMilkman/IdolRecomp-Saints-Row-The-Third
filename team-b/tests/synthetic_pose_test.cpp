// Synthetic tests for the hierarchical skinning library (sr3rig/pose.h).
//
// Self-contained - no game data, no .rig_pc/.ccmesh_pc parsing. Everything
// here is a small hand-built bone hierarchy and hand-built Vertex records,
// with expected results computed independently in this file's comments
// (not by calling the library twice), the same discipline
// synthetic_rig_test.cpp uses for the reader itself.
//
// This is the SECOND, independent line of evidence for the derivation in
// pose.h's header comment. The FIRST line is
// tools/validation/validate_pose.cpp, which runs the same library against
// real .rig_pc/.ccmesh_pc data and cross-checks it against that harness's
// original (pre-library) hand-rolled math - see its "HIERARCHICAL vs
// DIRECT" and "BIND-POSE identity" output. This file instead checks the
// library against hand arithmetic on a trivial synthetic skeleton, so a
// bug shared by both the library AND a coincidentally-matching real-data
// oracle would still be caught here.

#include <cmath>
#include <iostream>
#include <vector>

#include "sr3rig/pose.h"
#include "sr3rig/rig.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        double av = static_cast<double>(a), bv = static_cast<double>(b);     \
        if (std::fabs(av - bv) > (tol)) {                                    \
            std::cerr << "CHECK_NEAR FAILED: " #a " (" << av << ") vs " #b   \
                      << " (" << bv << "), tol " << (tol) << " at "          \
                      __FILE__ ":" << __LINE__ << "\n";                      \
            ++g_failures;                                                   \
        }                                                                    \
    } while (0)

constexpr float kPi = 3.14159265f;

// A 4-bone hierarchy: root(0) -> child(1) -> grandchild(2), plus an
// unrelated second root(3) (multi-root rigs are normal - 275/585 shipped
// rigs have several, spec-rig-format.md Sec4). Positions are arbitrary,
// non-zero, and chosen by hand so d_i = rest_i - rest_parent(i) is a clean
// number to re-derive expected results from.
std::vector<uint32_t> sampleParents() {
    return {sr3rig::kNoParent, 0, 1, sr3rig::kNoParent};
}
std::vector<std::array<float, 3>> sampleRest() {
    return {
        {2.0f, 3.0f, -1.0f},  // 0 root
        {3.0f, 3.0f, -1.0f},  // 1 child, d = (1,0,0)
        {3.0f, 4.0f, -1.0f},  // 2 grandchild, d = (0,1,0)
        {5.0f, 5.0f, 5.0f},   // 3 second root, unrelated
    };
}

void testMat3x4Basics() {
    // identity().transformPoint(p) == p.
    sr3rig::Mat3x4 id = sr3rig::Mat3x4::identity();
    std::array<float, 3> p{1.5f, -2.0f, 0.25f};
    auto pt = id.transformPoint(p);
    CHECK_NEAR(pt[0], 1.5f, 1e-6);
    CHECK_NEAR(pt[1], -2.0f, 1e-6);
    CHECK_NEAR(pt[2], 0.25f, 1e-6);

    // translation(t).transformPoint(p) == p + t.
    sr3rig::Mat3x4 tr = sr3rig::Mat3x4::translation({1.0f, 2.0f, 3.0f});
    auto pt2 = tr.transformPoint(p);
    CHECK_NEAR(pt2[0], 2.5f, 1e-6);
    CHECK_NEAR(pt2[1], 0.0f, 1e-6);
    CHECK_NEAR(pt2[2], 3.25f, 1e-6);

    // multiply(A, B).transformPoint(p) == A.transformPoint(B.transformPoint(p)).
    sr3rig::Mat3x4 a = sr3rig::Mat3x4::translation({10.0f, 0.0f, 0.0f});
    sr3rig::Mat3x4 b = sr3rig::Mat3x4::translation({0.0f, 20.0f, 0.0f});
    sr3rig::Mat3x4 ab = sr3rig::multiply(a, b);
    auto direct = a.transformPoint(b.transformPoint(p));
    auto composed = ab.transformPoint(p);
    CHECK_NEAR(direct[0], composed[0], 1e-6);
    CHECK_NEAR(direct[1], composed[1], 1e-6);
    CHECK_NEAR(direct[2], composed[2], 1e-6);

    // A 90deg rotation about Z sends +X to +Y.
    sr3rig::Rotation3 r90 = sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f);
    auto rotated = sr3rig::rotate(r90, {1.0f, 0.0f, 0.0f});
    CHECK_NEAR(rotated[0], 0.0f, 1e-5);
    CHECK_NEAR(rotated[1], 1.0f, 1e-5);
    CHECK_NEAR(rotated[2], 0.0f, 1e-5);
}

// R_i == identity for every bone must reduce every Skin_i to Identity
// exactly (pose.h's header comment, and rig.h's bind-pose argument). With
// these particular rest positions (differences of clean integers/halves),
// the running sum has no rounding at all, so the tolerance here can be
// tight.
void testBindPoseIsIdentity() {
    auto parents = sampleParents();
    auto rest = sampleRest();
    std::vector<sr3rig::Mat3x4> skin = sr3rig::computeSkinningMatrices(parents, rest, nullptr);
    CHECK(skin.size() == 4);
    for (size_t i = 0; i < skin.size(); ++i) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                float expect = (r == c) ? 1.0f : 0.0f;
                CHECK_NEAR(skin[i].m[static_cast<size_t>(r)][static_cast<size_t>(c)], expect, 1e-6);
            }
        }
    }
}

// Rotate ONLY the root by 90deg about Z, every other bone identity. Per
// the induction in pose.h's header comment, every bone in the root's
// subtree - which here is all of bones 0,1,2 - must end up with the SAME
// skin matrix: T(rest_root) * R * T(-rest_root). Bone 3 (unrelated second
// root) must stay identity.
void testSingleRootRotationSharedAcrossSubtree() {
    auto parents = sampleParents();
    auto rest = sampleRest();
    sr3rig::Rotation3 r90 = sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f);
    std::vector<sr3rig::Rotation3> rotations(4, sr3rig::identityRotation3());
    rotations[0] = r90;

    std::vector<sr3rig::Mat3x4> skin = sr3rig::computeSkinningMatrices(parents, rest, &rotations);

    sr3rig::Mat3x4 expected = sr3rig::multiply(
        sr3rig::multiply(sr3rig::Mat3x4::translation(rest[0]), sr3rig::Mat3x4::fromRotation(r90)),
        sr3rig::Mat3x4::translation({-rest[0][0], -rest[0][1], -rest[0][2]}));

    for (size_t i : {size_t(0), size_t(1), size_t(2)}) {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(skin[i].m[static_cast<size_t>(r)][static_cast<size_t>(c)],
                           expected.m[static_cast<size_t>(r)][static_cast<size_t>(c)], 1e-5);
    }
    // Bone 3 is untouched: identity.
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) {
            float expect = (r == c) ? 1.0f : 0.0f;
            CHECK_NEAR(skin[3].m[static_cast<size_t>(r)][static_cast<size_t>(c)], expect, 1e-6);
        }

    // Hand-derived expected WORLD position of the grandchild (bone 2,
    // rest (3,4,-1)): rotate that point rigidly by 90deg about Z around
    // the root's rest position (2,3,-1). Relative offset (1,1,0); rotating
    // (1,1,0) by +90deg about Z gives (-1,1,0) (cos90=0,sin90=1: x'=x*0-y*1=-1,
    // y'=x*1+y*0=1); plus the pivot: (1,4,-1).
    auto grandchildWorld = skin[2].transformPoint(rest[2]);
    CHECK_NEAR(grandchildWorld[0], 1.0f, 1e-5);
    CHECK_NEAR(grandchildWorld[1], 4.0f, 1e-5);
    CHECK_NEAR(grandchildWorld[2], -1.0f, 1e-5);
}

// parentIndices must be parent-before-child; a violation is a caller bug
// (sr3rig::Rig::parse never produces one - spec-rig-format.md Sec4,
// 22,274/22,274), not real data, so this checks the defensive rejection
// rather than a real path.
void testRejectsChildBeforeParent() {
    std::vector<uint32_t> badParents = {1, sr3rig::kNoParent}; // 0's parent is 1, but 1 > 0
    std::vector<std::array<float, 3>> rest = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    bool threw = false;
    try {
        sr3rig::computeSkinningMatrices(badParents, rest, nullptr);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

sr3mesh::Vertex makeVertex(std::array<float, 3> pos, std::array<uint8_t, 4> idx,
                           std::array<float, 4> w) {
    sr3mesh::Vertex v;
    v.position = pos;
    v.blendIndices = idx;
    v.blendWeights = w;
    return v;
}

// Edge-case handling of skinVertexPosition/skinVertices - the same rules
// validate_pose.cpp's original runPose() established and this library now
// carries as the single implementation: 255/non-positive-weight lanes
// skipped, out-of-range indices dropped and counted, near-zero total
// weight passes the original position through unchanged, and anything
// else is renormalized by the weight actually used.
void testVertexSkinningEdgeCases() {
    // Two bones: 0 translates by (10,0,0), 1 is identity.
    std::vector<sr3rig::Mat3x4> skin = {sr3rig::Mat3x4::translation({10.0f, 0.0f, 0.0f}),
                                        sr3rig::Mat3x4::identity()};

    // (a) Fully weighted to bone 0: result is p + (10,0,0).
    {
        sr3rig::SkinningStats stats;
        sr3mesh::Vertex v = makeVertex({1.0f, 2.0f, 3.0f}, {0, 255, 255, 255}, {1.0f, 0, 0, 0});
        auto out = sr3rig::skinVertexPosition(v, skin, &stats);
        CHECK_NEAR(out[0], 11.0f, 1e-6);
        CHECK_NEAR(out[1], 2.0f, 1e-6);
        CHECK_NEAR(out[2], 3.0f, 1e-6);
        CHECK(stats.zeroWeightPassthrough == 0);
        CHECK(stats.lanesDropped == 0);
        CHECK(stats.maxOutOfRangeIndex == -1);
    }

    // (b) 50/50 split between bone 0 (+10,0,0) and bone 1 (identity):
    // result is the WEIGHTED AVERAGE OF THE TWO TRANSFORMED POINTS, i.e.
    // p + (5,0,0) - not "transform the averaged point", which for two
    // affine transforms happens to coincide here but is the classic SSD
    // formula being exercised, not assumed.
    {
        sr3mesh::Vertex v = makeVertex({0.0f, 0.0f, 0.0f}, {0, 1, 255, 255}, {0.5f, 0.5f, 0, 0});
        auto out = sr3rig::skinVertexPosition(v, skin);
        CHECK_NEAR(out[0], 5.0f, 1e-6);
        CHECK_NEAR(out[1], 0.0f, 1e-6);
        CHECK_NEAR(out[2], 0.0f, 1e-6);
    }

    // (c) An out-of-range index (2, but skin.size() == 2) is dropped and
    // counted; the remaining lane (bone 0, full weight 0.4) is
    // renormalized up to full weight, so the result is still exactly
    // p + (10,0,0), and lanesDropped is incremented because the weight
    // actually used (0.4) is short of kFullWeightThreshold.
    {
        sr3rig::SkinningStats stats;
        sr3mesh::Vertex v = makeVertex({1.0f, 1.0f, 1.0f}, {0, 2, 255, 255}, {0.4f, 0.6f, 0, 0});
        auto out = sr3rig::skinVertexPosition(v, skin, &stats);
        CHECK_NEAR(out[0], 11.0f, 1e-6);
        CHECK_NEAR(out[1], 1.0f, 1e-6);
        CHECK_NEAR(out[2], 1.0f, 1e-6);
        CHECK(stats.maxOutOfRangeIndex == 2);
        CHECK(stats.lanesDropped == 1);
    }

    // (d) 255 sentinel and non-positive weight lanes are simply skipped,
    // even when their index would otherwise be in range.
    {
        sr3mesh::Vertex v = makeVertex({2.0f, 0.0f, 0.0f}, {255, 0, 1, 0}, {1.0f, 0.0f, 0.0f, -0.5f});
        auto out = sr3rig::skinVertexPosition(v, skin);
        // Only lane 1 (bone 0, weight 0.0) and lane 3 (bone 0, weight
        // -0.5) are excluded by w<=0; lane 2 (bone 1, weight 0.0) is also
        // excluded. Every lane is excluded here (255, or w<=0), so this is
        // really the zero-weight passthrough case - see (e) for the
        // explicit stats check of that path.
        CHECK_NEAR(out[0], 2.0f, 1e-6);
        CHECK_NEAR(out[1], 0.0f, 1e-6);
        CHECK_NEAR(out[2], 0.0f, 1e-6);
    }

    // (e) Every lane unused (all 255): near-zero total weight passes the
    // original position through unchanged and is counted as such, not as
    // a dropped lane.
    {
        sr3rig::SkinningStats stats;
        sr3mesh::Vertex v = makeVertex({7.0f, -3.0f, 0.5f}, {255, 255, 255, 255}, {0, 0, 0, 0});
        auto out = sr3rig::skinVertexPosition(v, skin, &stats);
        CHECK_NEAR(out[0], 7.0f, 1e-6);
        CHECK_NEAR(out[1], -3.0f, 1e-6);
        CHECK_NEAR(out[2], 0.5f, 1e-6);
        CHECK(stats.zeroWeightPassthrough == 1);
        CHECK(stats.lanesDropped == 0);
    }

    // skinVertices() over a small buffer just maps the single-vertex
    // function - checked once here so a future divergence between the two
    // entry points is caught.
    {
        std::vector<sr3mesh::Vertex> verts = {
            makeVertex({0.0f, 0.0f, 0.0f}, {0, 255, 255, 255}, {1.0f, 0, 0, 0}),
            makeVertex({1.0f, 1.0f, 1.0f}, {1, 255, 255, 255}, {1.0f, 0, 0, 0}),
        };
        sr3rig::SkinningStats stats;
        auto out = sr3rig::skinVertices(verts, skin, &stats);
        CHECK(out.size() == 2);
        CHECK_NEAR(out[0][0], 10.0f, 1e-6);
        CHECK_NEAR(out[1][0], 1.0f, 1e-6);
        CHECK(stats.verticesProcessed == 2);
    }
}

void testRigToMeshSpace() {
    // HANDOFF.md Sec9.13: mesh = (rig.x, -rig.y, -rig.z).
    auto m = sr3rig::rigToMeshSpace({1.0f, 2.0f, -3.0f});
    CHECK_NEAR(m[0], 1.0f, 1e-6);
    CHECK_NEAR(m[1], -2.0f, 1e-6);
    CHECK_NEAR(m[2], 3.0f, 1e-6);
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
    run("testMat3x4Basics", testMat3x4Basics);
    run("testBindPoseIsIdentity", testBindPoseIsIdentity);
    run("testSingleRootRotationSharedAcrossSubtree", testSingleRootRotationSharedAcrossSubtree);
    run("testRejectsChildBeforeParent", testRejectsChildBeforeParent);
    run("testVertexSkinningEdgeCases", testVertexSkinningEdgeCases);
    run("testRigToMeshSpace", testRigToMeshSpace);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic pose-library tests passed.\n";
    return 0;
}
