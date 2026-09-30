// Hierarchical skinning math: bone-hierarchy pose evaluation and CPU vertex
// skinning. Stage 1 of animation playback (see HANDOFF.md "RESUME HERE",
// 2026-09-12 session) - the CPU-math library only. Nothing here touches
// DirectX/rendering (`sr3render`) or `.anim_pc` keyframe decode; both are
// separate, later steps.
//
// WHERE THIS COMES FROM. `sr3rig/rig.h` already established, over the full
// population (20,564/20,564 child bones + 1,710/1,710 roots), that a
// `.rig_pc` bind pose is PURE TRANSLATION - there is no bind rotation in
// the format at all. `tools/validation/validate_pose.cpp` then confirmed,
// synthetically (rotate one bone's subtree rigidly about that bone's own
// rest position, scrambled-index control included), that the resulting
// skinning formula is structurally correct. This header generalizes that
// confirmed special case into ordinary hierarchical skinning with a
// per-bone LOCAL rotation, and PROVES the generalization reproduces the
// confirmed special case exactly - see
// tools/validation/validate_pose.cpp's "HIERARCHICAL vs DIRECT" and
// "BIND-POSE identity" checks, run against real .rig_pc/.ccmesh_pc data,
// for the numbers this claim rests on.
//
// THE DERIVATION, restated precisely (bones ordered parent-before-child,
// guaranteed by Rig::parse - spec-rig-format.md Sec4, 22,274/22,274):
//
//   d_i = restPosition_i - restPosition_parent(i)      (root: d_i = restPosition_i)
//   World_i = World_parent(i) * T(d_i) * Rot(R_i)      (root: World_i = T(d_i) * Rot(R_i))
//   Skin_i  = World_i * T(-restPosition_i)             (= Pose_i * InverseBind_i)
//
// At R_i == identity for every bone, World_i == T(restPosition_i) exactly
// (a telescoping sum of the d_i's), so Skin_i == Identity: bind pose,
// mesh unchanged. See computeSkinningMatrices()'s doc comment for the
// tolerance that floating-point summation costs this exact statement.
//
// COORDINATE SPACE IS THE CALLER'S CONCERN. This module does not know
// about the rig<->mesh axis flip (HANDOFF Sec9.13: mesh = (rig.x, -rig.y,
// -rig.z), used already by validate_pose.cpp). `restPositions` must be
// supplied in whatever space the caller wants `Skin_i` expressed in - pass
// mesh-space positions (see rigToMeshSpace()) to skin mesh-space vertices
// directly, as this project's harnesses do. Mixing spaces between
// `restPositions` and the vertices passed to skinVertices() will silently
// produce a wrong-but-plausible pose; there is no way to detect that from
// inside this module.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sr3mesh/mesh_block.h"
#include "sr3rig/rig.h"

namespace sr3rig {

// A row-major 3x4 affine transform: rows 0..2, columns 0..2 are the linear
// part M, column 3 is the translation t. Applied as p' = M*p + t. Chosen
// over a full 4x4 because every consumer here (CPU skinning, matrix
// composition under an implicit affine group) only ever needs that; the
// omitted bottom row is always [0 0 0 1].
struct Mat3x4 {
    std::array<std::array<float, 4>, 3> m{};

    static Mat3x4 identity();
    static Mat3x4 translation(const std::array<float, 3>& t);
    // `rot`'s 3x3 linear part, zero translation.
    static Mat3x4 fromRotation(const std::array<std::array<float, 3>, 3>& rot);

    // p' = M*p + t (p is a point, not a direction - translation applies).
    std::array<float, 3> transformPoint(const std::array<float, 3>& p) const;
};

// Composes two affine transforms so that, for every point p:
//     multiply(a, b).transformPoint(p) == a.transformPoint(b.transformPoint(p))
// i.e. `b` is applied first ("a after b"), matching ordinary matrix product
// order for column vectors: multiply(a, b) corresponds to the product a*b.
Mat3x4 multiply(const Mat3x4& a, const Mat3x4& b);

// A pure rotation: 3x3, row-major, r[row][col].
using Rotation3 = std::array<std::array<float, 3>, 3>;

Rotation3 identityRotation3();

// Rodrigues' rotation formula about `axis` (need not be pre-normalized) by
// `angleRadians`. Extracted from validate_pose.cpp's axisAngle()/Mat3 -
// same formula, same operation order, so it reproduces that harness's
// numbers bit-for-bit rather than merely "closely".
Rotation3 rotationFromAxisAngle(std::array<float, 3> axis, float angleRadians);

// r * v.
std::array<float, 3> rotate(const Rotation3& r, const std::array<float, 3>& v);

// HANDOFF.md Sec9.13's confirmed rig<->mesh axis convention: mesh space is
// (rig.x, -rig.y, -rig.z). A small, established fact, not a new derivation
// - surfaced here so callers converting a rig's rest positions to mesh
// space (to skin mesh-space vertices) do not re-derive or re-type it.
//
// SUPERSEDED FOR SKINNING BY HANDOFF Sec9.62 (2026-09-13), NOT CHANGED
// HERE: Sec9.13's fit was made with blend indices read as direct rig
// indices, and they are not - they index the mesh's own bone palette
// (sr3mesh::MeshBlock::bonePalette()). With the palette applied, the
// rig->mesh transform that puts every bone's vertices at its rest position
// is (-x, -y, -z), not (x, -y, -z); the two readings' errors cancelled on
// exactly the l-/r- bones that could have told them apart. This function
// is kept as-is so every pre-Sec9.62 harness and render reproduces its
// recorded numbers; the palette path uses sr3rig::rigToMeshSpaceInverted()
// from sr3rig/bone_palette.h and is opt-in (`--bone-palette`).
std::array<float, 3> rigToMeshSpace(const std::array<float, 3>& rigSpacePosition);

// Computes one skinning matrix per bone for a hierarchical skeleton.
//
//   parentIndices[i]   : index of bone i's parent, or sr3rig::kNoParent for
//                        a root. MUST be parent-before-child (parentIndices[i]
//                        < i for every non-root i) - Rig::parse enforces
//                        exactly this over the whole shipped population
//                        (spec-rig-format.md Sec4), so any sr3rig::Rig's
//                        bones() satisfies it already.
//   restPositions[i]   : bone i's rest position, in whatever space the
//                        caller wants Skin_i expressed in (see the header
//                        comment above - NOT necessarily rig-space).
//                        Must be the same length as parentIndices.
//   localRotations     : per-bone LOCAL rotation R_i. May be null (every
//                        bone identity - bind pose). A vector shorter than
//                        parentIndices is allowed; missing trailing entries
//                        default to identity.
//
// Returns skin[i] = World_i * T(-restPositions[i]) per the derivation in
// the header comment. At every R_i == identity (localRotations == nullptr,
// or all-identity entries), every returned matrix equals Identity to
// within ordinary float summation error - not exactly zero, because
// World_i is built by summing the d_i chain rather than reading
// restPositions[i] back directly, and float addition is not perfectly
// associative. Measured on real data in
// tools/validation/validate_pose.cpp: max deviation from identity well
// under 1e-5 (see that harness's printed "bind-pose identity" figures for
// the exact number on the run that produced them).
std::vector<Mat3x4> computeSkinningMatrices(
    const std::vector<uint32_t>& parentIndices,
    const std::vector<std::array<float, 3>>& restPositions,
    const std::vector<Rotation3>* localRotations = nullptr);

// Diagnostics from applying skin matrices to a vertex buffer - mirrors
// exactly what validate_pose.cpp's runPose() counted before this was
// extracted, so nothing about the edge-case accounting is new here.
struct SkinningStats {
    long long verticesProcessed = 0;
    // totalWeight <= kZeroWeightThreshold: original position passed through
    // unchanged rather than divided by a near-zero denominator.
    long long zeroWeightPassthrough = 0;
    // totalWeight in (kZeroWeightThreshold, kFullWeightThreshold): at least
    // one lane was dropped (unused slot, or an out-of-range index) and the
    // result was renormalized by the weight actually used.
    long long lanesDropped = 0;
    // Highest blend index seen that was >= the bone count passed to
    // skin[], or -1 if none. Such lanes are dropped (see lanesDropped).
    long long maxOutOfRangeIndex = -1;
};

constexpr float kZeroWeightThreshold = 1e-6f;
constexpr float kFullWeightThreshold = 0.999f;

// v' = sum_k w_k * skin[bone(k)] * v.position, over the up to 4 blend
// lanes in `v` (index 255 or non-positive weight = unused lane), dropping
// and counting any lane whose index is >= skin.size(), renormalizing by
// the weight actually used, and passing the original position through
// unchanged when that total weight is <= kZeroWeightThreshold. Identical
// edge-case handling to validate_pose.cpp's original runPose() - see that
// file's "BUG FOUND ON THE FIRST RUN" comment for why each of these rules
// exists.
std::array<float, 3> skinVertexPosition(const sr3mesh::Vertex& v,
                                         const std::vector<Mat3x4>& skin,
                                         SkinningStats* stats = nullptr);

std::vector<std::array<float, 3>> skinVertices(const std::vector<sr3mesh::Vertex>& verts,
                                                const std::vector<Mat3x4>& skin,
                                                SkinningStats* stats = nullptr);

} // namespace sr3rig
