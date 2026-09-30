// Timeline sampling for the `.anim_pc` keyframe payload (sr3anim::Payload).
// Stage 2 of animation playback (HANDOFF.md "RESUME HERE" / Sec9.53,
// Sec9.53.1, 2026-09-12 session). Turns a Payload + a point in time into
// one rotation and one translation delta PER BONE, ready to feed
// sr3rig::computeSkinningMatrices()'s localRotations parameter (after a
// trivial quaternion -> 3x3 matrix conversion the caller performs - kept
// out of this header so sr3anim does not need to depend on sr3rig) and
// sr3rig::computeAnimatedSkinningMatrices()'s localTranslationDeltas
// parameter (see include/sr3rig/animated_pose.h for why that needs its
// own composition step rather than being folded directly into
// restPositions).
//
// TIME MODEL - stated honestly, because the two halves are NOT at the same
// confidence tier and flattening that would misrepresent what is measured
// versus assumed:
//
//   * TRANSLATION key timing is REAL data. Each translation key carries a
//     trailing per-key duration byte (payload.h's TrackBlock /
//     translationSampleOffset comment; HANDOFF Sec9.43/9.47,
//     [CONFIRMED - empirical]: within-clip relative spread of the summed
//     bytes is 0.0057 against 64x/246x controls, and the per-clip sum
//     matches header +0x06 exactly in 81.46% of the reliably-estimated
//     stratum). This reader turns those bytes into a cumulative time per
//     key (key 0 at t=0; key k at the running sum of keys 0..k-1's
//     duration bytes) and interpolates translation LINEARLY between the
//     two bracketing keys' decoded positions.
//
//   * ROTATION key timing is an ASSUMPTION, not confirmed. Nothing in the
//     confirmed payload layout (spec-anim-format.md Sec6c.1, payload.h's
//     TrackBlock) carries a per-key duration for rotation - only the
//     control record's run SPAN exists there, and that is a base/step
//     run-length (a compression grouping), not a time value. The one
//     format feature that WOULD add a per-key rotation byte (spec
//     Sec6c.3 item 3, a second runtime mode) is confirmed by disassembly
//     to be switched OFF by flags bit 0x01, which is set in nearly every
//     shipped file (spec Sec2), so it is unavailable here. Lacking any
//     better information, this reader ASSUMES rotation keys are spaced
//     UNIFORMLY across the clip's total duration (header +0x06 /
//     Animation::durationTotal()): key k of n keys sits at
//     t = k * duration / (n - 1). This is a REASONABLE DEFAULT, not a
//     finding - see this session's HANDOFF write-up for a real-data
//     sanity check (rope_run.anim_pc: ~90 rotation keys over a 186-unit
//     clip, ~2.07 units/key - CONSISTENT with the independently-measured
//     population median gap of ~2.2 units/key from Sec9.47, which is
//     necessary but nowhere near sufficient to confirm uniform spacing).
//
// WHAT THIS DOES NOT DO: root motion (Animation::rootRotation()/
// rootTranslation()) is not applied - out of scope for this stage, which
// only samples PER-BONE local tracks. A bone with a track whose rotation
// or translation key COUNT is zero is reported as untracked for that
// component (identity rotation / zero delta) rather than guessed.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "vpp/byte_view.h"

namespace sr3anim {

using vpp::ByteView;

// One bone's sampled local pose at a point in time.
struct BoneSample {
    // Local rotation, exactly as Payload::rotations() decodes it (RIG
    // SPACE, no coordinate-space conversion - that is deferred to a
    // single final conjugation by the caller; see HANDOFF Sec9.53 and
    // include/sr3rig/animated_pose.h). Identity when the bone has no
    // track, or its track has zero rotation keys.
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    // Additive delta for this bone's own local d_i (see
    // computeSkinningMatrices's derivation comment in sr3rig/pose.h).
    // Zero when the bone has no track, or its track has zero translation
    // keys - the common case (HANDOFF Sec9.42: 90.5% of tracks carry no
    // translation data at all).
    std::array<float, 3> translationDelta{0.0f, 0.0f, 0.0f};
    bool hasRotationTrack = false;
    bool hasTranslationTrack = false;
};

// Samples every bone of a `boneCount`-bone rig at time `timeUnits` (same
// unidentified unit as Animation::durationTotal(); clamped to
// [0, durationTotal()], or to key 0 if durationTotal() == 0).
//
// Track -> bone mapping: uses Animation::trackBoneTableOffset() (one byte
// per track) when Animation::hasTrackBoneTable() is true, else identity
// (track i -> bone i) per animation.h's documented convention. A track
// whose mapped bone index is >= boneCount is skipped (defensive only -
// the caller is expected to have already checked the clip is compatible
// with the rig, e.g. via tools/validation/match_anim_rig.cpp).
//
// Returns a vector of exactly `boneCount` BoneSample entries, bones with
// no corresponding track left at the default (identity rotation, zero
// delta).
std::vector<BoneSample> sampleClipAtTime(ByteView bytes, const Animation& a, const Payload& payload,
                                          size_t boneCount, float timeUnits);

} // namespace sr3anim
