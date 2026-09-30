// Animated-translation composition layered ON TOP OF sr3rig/pose.h,
// WITHOUT modifying it. Stage 2 of animation playback (HANDOFF.md "RESUME
// HERE" / Sec9.53, Sec9.53.1, 2026-09-12 session): stage 1's
// computeSkinningMatrices() was built and triple-checked for the
// ROTATION-ONLY case (every bone's local translation offset d_i is always
// the REST offset; only R_i varies). Stage 2 needs a SPARSE MINORITY of
// bones (HANDOFF Sec9.42: 90.5% of tracks carry no translation data at
// all) to also carry an animated ADDITIVE delta on d_i. This file adds
// that without touching pose.h/pose.cpp's confirmed, re-verified-twice
// core math, per this session's explicit instruction not to.
//
// WHY THIS NEEDS ITS OWN FUNCTION RATHER THAN JUST PERTURBING
// restPositions BEFORE CALLING computeSkinningMatrices(): that is the
// "obvious" shortcut, and it is WRONG, not merely imprecise - worked
// counterexample below. computeSkinningMatrices() uses restPositions[i]
// for TWO DIFFERENT ROLES inside the same loop:
//
//   1. d_i = restPositions[i] - restPositions[parent(i)]   (builds World_i)
//   2. skin_i = World_i * T(-restPositions[i])             (inverse bind)
//
// Role 2 MUST stay pinned to the true, un-animated rest position - that is
// what the mesh's vertices were originally skinned against, and it does
// not move just because the animation moves this bone. Role 1 is exactly
// where an animated delta SHOULD land. Passing a single perturbed array
// for both roles gets role 1 right and role 2 wrong at the same time.
//
// COUNTEREXAMPLE (root bone only, no rotation, translation delta d):
// desired Skin_0 = Pose_0 * InverseBind_0 = T(rest_0 + d) * T(-rest_0) =
// T(d) - the root should visibly translate by d. Perturbing restPositions
// to (rest_0 + d) and calling computeSkinningMatrices() unmodified gives
// World_0 = T(rest_0 + d) (role 1, correct) but then skin_0 =
// World_0 * T(-(rest_0 + d)) = IDENTITY (role 2, using the SAME perturbed
// value) - the translation cancels itself out completely. The shortcut
// does not merely lose precision here, it silently discards 100% of a
// root bone's animated translation. Verified by direct computation, not
// asserted - see tools/validation/validate_animated_pose.cpp.
//
// THE FIX (caller-side only, built entirely from pose.h's PUBLIC API -
// Mat3x4::translation(), multiply(), computeSkinningMatrices() - so it is
// additive to pose.h, not a modification of it):
//
//   cumDelta_i = cumDelta_parent(i) + delta_i        (root: cumDelta_i = delta_i)
//   restAnimated_i = restPositions_i + cumDelta_i
//   skinWrong = computeSkinningMatrices(parents, restAnimated, rotations)
//   skin_i = skinWrong_i * T(cumDelta_i)
//
// Derivation: restAnimated_i - restAnimated_parent(i) telescopes to
// d_i_rest + delta_i exactly (the cumulative parent term cancels in the
// subtraction), so skinWrong's WORLD chain (role 1) is exactly what is
// wanted. Its inverse-bind term (role 2) is wrong by a knowable, constant
// factor: skinWrong_i = World_i_correct * T(-(rest_i + cumDelta_i)) =
// [World_i_correct * T(-rest_i)] * T(-cumDelta_i) = skin_i_desired *
// T(-cumDelta_i) (translations commute, so T(-(a+b)) = T(-a)*T(-b) in
// either order) - so right-multiplying skinWrong_i by T(cumDelta_i)
// recovers skin_i_desired exactly. At cumDelta == 0 everywhere (no
// translation deltas at all, or this parameter omitted) this reduces,
// term for term, to calling computeSkinningMatrices() directly - verified
// in validate_animated_pose.cpp, not just claimed by the algebra above.
#pragma once

#include "sr3rig/pose.h"

namespace sr3rig {

// Computes skinning matrices exactly as computeSkinningMatrices() does,
// with an additional PER-BONE ADDITIVE translation delta on each bone's
// local d_i (see this header's derivation above). `localTranslationDeltas`
// may be null or shorter than parentIndices - missing/absent entries are
// treated as zero delta, so a bone with no animated translation track
// (the common case) is unaffected. When `localTranslationDeltas` is null,
// this degrades to calling computeSkinningMatrices() directly (same
// result, not merely a similar one).
std::vector<Mat3x4> computeAnimatedSkinningMatrices(
    const std::vector<uint32_t>& parentIndices,
    const std::vector<std::array<float, 3>>& restPositions,
    const std::vector<Rotation3>* localRotations,
    const std::vector<std::array<float, 3>>* localTranslationDeltas);

// --- Single-final-conjugation rig-space -> mesh-space bridge ---
//
// HANDOFF Sec9.53's open question, restated: `.anim_pc` rotations are
// decoded in the SAME file/space as the rest of the rig format (bone
// hierarchy, rest positions) - call it RIG SPACE. rigToMeshSpace()
// (pose.h) is a CONFIRMED proper rotation (180 deg about X, determinant
// +1): mesh = (rig.x, -rig.y, -rig.z). The principled way to combine
// hierarchy + real per-frame rotations + this coordinate transform is to
// do the ENTIRE pose computation in rig space - rig-space rest positions,
// rotations exactly as decoded, no conjugation in the middle - and
// convert only the FINAL per-bone Skin_i matrix to mesh space ONCE:
// Skin_i^mesh = M * Skin_i^rig * M, where M is rigToMeshSpace()'s linear
// part and M^{-1} == M (diag(1,-1,-1) is an involution).
//
// PROVEN EQUIVALENT, not merely plausible, to converting every rest
// position and rotation to mesh space individually before composing the
// hierarchy the "naive" way: conjugation distributes over a matrix
// product whenever M*M == Identity (insert M*M between every adjacent
// factor), so M * (World_parent * T(d_i) * Rot(R_i)) * M telescopes into
// (M*World_parent*M) * (M*T(d_i)*M) * (M*Rot(R_i)*M) - i.e. exactly the
// per-bone-converted construction, term for term. Checked numerically in
// validate_animated_pose.cpp (case 5), not just algebraically.
Mat3x4 meshSpaceConjugationMatrix();
Mat3x4 conjugateToMeshSpace(const Mat3x4& skinRig);

// Unit quaternion (x,y,z,w) -> the Rotation3 matrix computeSkinningMatrices
// expects for localRotations. Standard active-rotation formula (Shoemake);
// makes no assumption about which coordinate space the quaternion is
// expressed in - it is a literal conversion, not a fix for the rig/mesh
// question above.
Rotation3 quatToRotation3(float x, float y, float z, float w);

} // namespace sr3rig
