// Synthetic tests for sr3rig/bone_palette.h (HANDOFF Sec9.62): the
// blend-index remap through a Mesh sub-block's bone palette, and the
// (-x,-y,-z) rig->mesh conjugation that the palette reading implies.
//
// Every expected value is hand-derived in the comments, never obtained by
// calling the library twice. Each load-bearing rule carries a deliberately
// WRONG case that must produce a DIFFERENT answer, so a regression that
// quietly degrades the remap to identity, or the inversion to Sec9.13's
// (x,-y,-z) conjugation, fails loudly rather than passing by coincidence.
#include <cmath>
#include <iostream>
#include <vector>

#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

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

void checkPoint(const std::array<float, 3>& got, const std::array<float, 3>& expect, double tol) {
    CHECK_NEAR(got[0], expect[0], tol);
    CHECK_NEAR(got[1], expect[1], tol);
    CHECK_NEAR(got[2], expect[2], tol);
}

double dist(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

sr3mesh::Vertex makeVertex(std::array<float, 3> pos, std::array<uint8_t, 4> idx,
                           std::array<float, 4> w) {
    sr3mesh::Vertex v;
    v.position = pos;
    v.blendIndices = idx;
    v.blendWeights = w;
    return v;
}

// --- Test 1: the remap itself, lane by lane, with both drop cases and a
// mutation check that an identity "remap" would fail.
void testRemapLanes() {
    // palette: slot 0 -> rig 5, slot 1 -> rig 0, slot 2 -> rig 9, slot 3 -> rig 12
    // (12 is OUT OF RANGE for a 10-bone rig - the rig-range drop case).
    std::vector<uint8_t> palette = {5, 0, 9, 12};
    const size_t rigBones = 10;

    std::vector<sr3mesh::Vertex> in = {
        // v0: two live lanes, both mappable -> {5, 0}
        makeVertex({1, 2, 3}, {0, 1, 255, 255}, {0.5f, 0.5f, 0.0f, 0.0f}),
        // v1: one live lane -> {9}
        makeVertex({4, 5, 6}, {2, 255, 255, 255}, {1.0f, 0.0f, 0.0f, 0.0f}),
        // v2: lane 0 index 7 has NO palette slot (palette has 4) -> dropped;
        //     lane 1 index 1 -> rig 0
        makeVertex({7, 8, 9}, {7, 1, 255, 255}, {0.6f, 0.4f, 0.0f, 0.0f}),
        // v3: lane 0 index 3 -> palette says rig 12 >= 10 -> dropped;
        //     lane 1 index 0 -> rig 5
        makeVertex({0, 0, 1}, {3, 0, 255, 255}, {0.3f, 0.7f, 0.0f, 0.0f}),
    };

    sr3rig::BlendIndexRemapStats stats;
    std::vector<sr3mesh::Vertex> out =
        sr3rig::remapBlendIndicesThroughPalette(in, palette, rigBones, &stats);
    CHECK(out.size() == 4);

    CHECK(out[0].blendIndices[0] == 5);
    CHECK(out[0].blendIndices[1] == 0);
    CHECK(out[0].blendIndices[2] == 255);
    CHECK(out[0].blendIndices[3] == 255);
    CHECK_NEAR(out[0].blendWeights[0], 0.5f, 1e-7);
    CHECK_NEAR(out[0].blendWeights[1], 0.5f, 1e-7);
    // Position copied through untouched.
    checkPoint(out[0].position, {1, 2, 3}, 0.0);

    CHECK(out[1].blendIndices[0] == 9);
    CHECK(out[1].blendIndices[1] == 255);

    CHECK(out[2].blendIndices[0] == 255);              // palette out of range -> dropped
    CHECK_NEAR(out[2].blendWeights[0], 0.0f, 0.0);     // ... and its weight zeroed
    CHECK(out[2].blendIndices[1] == 0);
    CHECK_NEAR(out[2].blendWeights[1], 0.4f, 1e-7);   // untouched

    CHECK(out[3].blendIndices[0] == 255);              // rig out of range -> dropped
    CHECK_NEAR(out[3].blendWeights[0], 0.0f, 0.0);
    CHECK(out[3].blendIndices[1] == 5);

    CHECK(stats.verticesProcessed == 4);
    CHECK(stats.lanesRemapped == 5);                   // v0:2, v1:1, v2:1, v3:1
    CHECK(stats.lanesDroppedPaletteOutOfRange == 1);   // v2 lane 0
    CHECK(stats.lanesDroppedRigOutOfRange == 1);       // v3 lane 0

    // Input must not be modified (the function returns a copy).
    CHECK(in[0].blendIndices[0] == 0);

    // MUTATION: an identity remap (a regression that forgot to look up the
    // palette) would leave v0 lane 0 at 0, not 5. Assert the two differ so
    // this test cannot pass on identity.
    CHECK(out[0].blendIndices[0] != in[0].blendIndices[0]);
}

// --- Test 2: rigToMeshSpaceInverted vs pose.h's rigToMeshSpace. They must
// agree on y and z and DISAGREE on x - the whole point of Sec9.62's
// coordinate finding.
void testInvertedRestTransform() {
    std::array<float, 3> rig = {1.0f, 2.0f, 3.0f};
    std::array<float, 3> inv = sr3rig::rigToMeshSpaceInverted(rig);
    std::array<float, 3> rot = sr3rig::rigToMeshSpace(rig);
    checkPoint(inv, {-1.0f, -2.0f, -3.0f}, 0.0);
    checkPoint(rot, {1.0f, -2.0f, -3.0f}, 0.0);
    CHECK(inv[0] != rot[0]);
    CHECK(inv[1] == rot[1]);
    CHECK(inv[2] == rot[2]);
}

// --- Test 3: conjugation by -I. Rig-space skin = T(t) * Rot(R) with
// R = 90deg about +Z ((x,y,z) -> (-y,x,z), the convention
// synthetic_pose_test.cpp / synthetic_multibone_pose_test.cpp establish)
// and t = (1,2,3).
//
// Hand derivation: M = -I, so M*(R*(M*q) + t) = M*(-R*q + t) = R*q - t.
// The mesh-space matrix is (R, -t). For q = (1,0,0): R*q = (0,1,0);
// minus t -> (-1, -1, -3).
//
// Under Sec9.13's conjugation (M = diag(1,-1,-1)) the same input gives
// M*(R*(M*q) + t): M*q = (1,0,0); R -> (0,1,0); + t -> (1,3,3);
// M -> (1,-3,-3). Asserted as the WRONG answer so the two conjugations
// cannot be confused by a future edit.
void testInversionConjugation() {
    sr3rig::Rotation3 R = sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f);
    sr3rig::Mat3x4 skinRig =
        sr3rig::multiply(sr3rig::Mat3x4::translation({1.0f, 2.0f, 3.0f}), sr3rig::Mat3x4::fromRotation(R));

    sr3rig::Mat3x4 skinMesh = sr3rig::conjugateToMeshSpaceInverted(skinRig);
    // Linear part unchanged (R), translation negated (-t).
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK_NEAR(skinMesh.m[r][c], R[r][c], 1e-6);
    CHECK_NEAR(skinMesh.m[0][3], -1.0f, 1e-6);
    CHECK_NEAR(skinMesh.m[1][3], -2.0f, 1e-6);
    CHECK_NEAR(skinMesh.m[2][3], -3.0f, 1e-6);

    std::array<float, 3> got = skinMesh.transformPoint({1.0f, 0.0f, 0.0f});
    checkPoint(got, {-1.0f, -1.0f, -3.0f}, 1e-6);

    // MUTATION: Sec9.13's conjugation on the same input.
    std::array<float, 3> wrong = sr3rig::conjugateToMeshSpace(skinRig).transformPoint({1.0f, 0.0f, 0.0f});
    checkPoint(wrong, {1.0f, -3.0f, -3.0f}, 1e-6);
    CHECK(dist(got, wrong) > 1.0);
}

// --- Test 4: end to end - a 2-bone rig, a palette, a real hierarchical
// pose through computeSkinningMatrices(), the inverted conjugation, and
// skinVertexPosition(). Hand-derived expected position, plus BOTH
// single-error mutations (no remap; remap but Sec9.13 conjugation), each
// of which must land somewhere else.
//
// Rig (RIG space):  bone0 root at (0,0,0);  bone1 child of 0 at (1,-1,0).
// Rotations:        R0 = identity;  R1 = 90deg about +Z.
// Mesh space under -I: bone1 rest at (-1, 1, 0).
// Palette: slot 0 -> rig bone 1, slot 1 -> rig bone 0.
// Vertex A: mesh position (-1, 1.5, 0), blend index 0 weight 1.0
//           -> rig bone 1 (the rotated child).
//
// computeSkinningMatrices (pose.h derivation):
//   skin0 = I.
//   skin1 = T(d1) * Rot(R1) * T(-rest1) with d1 = rest1 = (1,-1,0):
//           skin1(p) = R1*(p - (1,-1,0)) + (1,-1,0)
//   as (R, t): t = (1,-1,0) - R1*(1,-1,0) = (1,-1,0) - (1,1,0) = (0,-2,0).
// Inverted conjugation: (R1, -t) = (R1, (0,2,0)).
//   A = (-1, 1.5, 0): R1*A = (-1.5, -1, 0); + (0,2,0) = (-1.5, 1, 0).
// Sanity: that is a 90deg rotation of A about the MESH-space pivot
// (-1,1,0): A - pivot = (0, 0.5, 0) -> (-0.5, 0, 0) -> + pivot = (-1.5, 1, 0).
//
// Mutation (a): no remap - blend index 0 read as rig bone 0 (identity):
//   A unchanged = (-1, 1.5, 0).
// Mutation (b): remap, but Sec9.13's M = diag(1,-1,-1):
//   M*R1*M = rotation by -90deg about Z ((x,y,z) -> (y,-x,z)); M*t = (0,2,0).
//   A -> (1.5, 1, 0) + (0,2,0) = (1.5, 3, 0).
void testEndToEndPaletteAndInversion() {
    std::vector<uint32_t> parents = {sr3rig::kNoParent, 0};
    std::vector<std::array<float, 3>> restRig = {{0.0f, 0.0f, 0.0f}, {1.0f, -1.0f, 0.0f}};
    std::vector<sr3rig::Rotation3> rots = {
        sr3rig::identityRotation3(),
        sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f)};
    std::vector<sr3rig::Mat3x4> skinRig = sr3rig::computeSkinningMatrices(parents, restRig, &rots);

    std::vector<uint8_t> palette = {1, 0};
    std::vector<sr3mesh::Vertex> verts = {
        makeVertex({-1.0f, 1.5f, 0.0f}, {0, 255, 255, 255}, {1.0f, 0.0f, 0.0f, 0.0f})};

    // The correct path.
    std::vector<sr3mesh::Vertex> remapped =
        sr3rig::remapBlendIndicesThroughPalette(verts, palette, parents.size(), nullptr);
    CHECK(remapped[0].blendIndices[0] == 1);
    std::vector<sr3rig::Mat3x4> skinMeshInv(2);
    for (size_t b = 0; b < 2; ++b) skinMeshInv[b] = sr3rig::conjugateToMeshSpaceInverted(skinRig[b]);
    std::array<float, 3> got = sr3rig::skinVertexPosition(remapped[0], skinMeshInv, nullptr);
    checkPoint(got, {-1.5f, 1.0f, 0.0f}, 1e-5);

    // Mutation (a): no remap.
    std::array<float, 3> noRemap = sr3rig::skinVertexPosition(verts[0], skinMeshInv, nullptr);
    checkPoint(noRemap, {-1.0f, 1.5f, 0.0f}, 1e-5);
    CHECK(dist(noRemap, got) > 0.5);

    // Mutation (b): remap, Sec9.13 conjugation.
    std::vector<sr3rig::Mat3x4> skinMeshRot(2);
    for (size_t b = 0; b < 2; ++b) skinMeshRot[b] = sr3rig::conjugateToMeshSpace(skinRig[b]);
    std::array<float, 3> wrongConj = sr3rig::skinVertexPosition(remapped[0], skinMeshRot, nullptr);
    checkPoint(wrongConj, {1.5f, 3.0f, 0.0f}, 1e-5);
    CHECK(dist(wrongConj, got) > 0.5);

    // Bind-pose identity survives the inversion: all-identity rotations
    // must leave the vertex exactly where it is, remapped or not.
    std::vector<sr3rig::Mat3x4> bindRig = sr3rig::computeSkinningMatrices(parents, restRig, nullptr);
    std::vector<sr3rig::Mat3x4> bindMesh(2);
    for (size_t b = 0; b < 2; ++b) bindMesh[b] = sr3rig::conjugateToMeshSpaceInverted(bindRig[b]);
    checkPoint(sr3rig::skinVertexPosition(remapped[0], bindMesh, nullptr), {-1.0f, 1.5f, 0.0f}, 1e-6);
}

// --- Test 5 (HANDOFF Sec9.63.10): the PER-SET remap. A vertex drawn by a
// range that names set k indexes THAT set's sub-list, not the whole
// palette. Every expectation below is computed by hand from the palette
// and the set descriptors written two lines above it.
void testPerSetRemap() {
    // palette:  slot   0  1  2  3 | 4  5  6
    //           rig    5  0  9  2 | 7  1  3
    // sets:     {count 4, start 0}, {count 3, start 4}
    // So under set 0, lane index 1 -> rig 0; under set 1, lane index 1 ->
    // palette[4 + 1] = rig 1. Different bone from the SAME lane value -
    // which is exactly what the multi-set case is about.
    std::vector<uint8_t> palette = {5, 0, 9, 2, 7, 1, 3};
    std::vector<sr3mesh::MeshBlock::BonePaletteSet> sets(2);
    sets[0].count = 4; sets[0].start = 0;
    sets[1].count = 3; sets[1].start = 4;
    const size_t rigBones = 10;

    std::vector<sr3mesh::Vertex> in = {
        makeVertex({0, 0, 0}, {1, 2, 255, 255}, {0.5f, 0.5f, 0.0f, 0.0f}), // set 0
        makeVertex({1, 0, 0}, {1, 2, 255, 255}, {0.5f, 0.5f, 0.0f, 0.0f}), // set 1
        makeVertex({2, 0, 0}, {3, 255, 255, 255}, {1.0f, 0.0f, 0.0f, 0.0f}), // set 1: 3 >= count 3
    };
    std::vector<uint8_t> setOfVertex = {0, 1, 1};

    sr3rig::BlendIndexRemapStats stats;
    std::vector<sr3mesh::Vertex> out = sr3rig::remapBlendIndicesThroughPaletteSets(
        in, palette, sets, setOfVertex, rigBones, &stats);
    CHECK(out.size() == 3);
    // v0, set 0: palette[0+1] = 0, palette[0+2] = 9.
    CHECK(out[0].blendIndices[0] == 0);
    CHECK(out[0].blendIndices[1] == 9);
    // v1, set 1: palette[4+1] = 1, palette[4+2] = 3. SAME lane values as
    // v0, DIFFERENT bones - a regression that ignored the set would give
    // {0, 9} here too, so assert the disagreement explicitly.
    CHECK(out[1].blendIndices[0] == 1);
    CHECK(out[1].blendIndices[1] == 3);
    CHECK(out[1].blendIndices[0] != out[0].blendIndices[0]);
    CHECK(out[1].blendIndices[1] != out[0].blendIndices[1]);
    // v2: lane index 3 is out of range for set 1 (count 3, slots 0..2),
    // even though palette[4+3] = palette[7] would be a valid BYTE if the
    // bound were taken from the palette rather than the set. Dropped.
    CHECK(out[2].blendIndices[0] == 255);
    CHECK_NEAR(out[2].blendWeights[0], 0.0f, 0.0);
    CHECK(stats.lanesRemapped == 4);
    CHECK(stats.lanesDroppedPaletteOutOfRange == 1);

    // EQUIVALENCE: one set covering the whole palette, all vertices in it,
    // must give exactly what remapBlendIndicesThroughPalette() gives. If
    // these two ever drift apart, the multi-set path has stopped being a
    // generalisation of the single-set one.
    std::vector<sr3mesh::MeshBlock::BonePaletteSet> single(1);
    single[0].count = static_cast<uint8_t>(palette.size());
    single[0].start = 0;
    std::vector<uint8_t> allZero(in.size(), 0);
    std::vector<sr3mesh::Vertex> viaSets =
        sr3rig::remapBlendIndicesThroughPaletteSets(in, palette, single, allZero, rigBones, nullptr);
    std::vector<sr3mesh::Vertex> viaFlat =
        sr3rig::remapBlendIndicesThroughPalette(in, palette, rigBones, nullptr);
    CHECK(viaSets.size() == viaFlat.size());
    for (size_t i = 0; i < viaSets.size() && i < viaFlat.size(); ++i)
        for (size_t k = 0; k < 4; ++k) {
            CHECK(viaSets[i].blendIndices[k] == viaFlat[i].blendIndices[k]);
            CHECK_NEAR(viaSets[i].blendWeights[k], viaFlat[i].blendWeights[k], 0.0);
        }
    // ... and the equivalence must not be vacuous: the fixture really does
    // remap something (v0 lane 0: 1 -> 0 is a no-op by value, so check a
    // lane where it is not).
    CHECK(viaFlat[0].blendIndices[1] == 9 && in[0].blendIndices[1] == 2);
}

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures == before) {
        std::cout << "  ok    " << name << "\n";
    } else {
        std::cout << "  FAIL  " << name << " (" << (g_failures - before) << " check(s))\n";
    }
}

} // namespace

int main() {
    run("testRemapLanes", testRemapLanes);
    run("testInvertedRestTransform", testInvertedRestTransform);
    run("testInversionConjugation", testInversionConjugation);
    run("testEndToEndPaletteAndInversion", testEndToEndPaletteAndInversion);
    run("testPerSetRemap", testPerSetRemap);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic bone-palette tests passed.\n";
    return 0;
}
