// Keyframe payload reader for .anim_pc (spec-anim-format.md Sec6c).
//
// SCOPE AND CONFIDENCE - the two halves are NOT at the same tier, and this
// header says so because flattening that is the main way this decode goes
// wrong for someone downstream:
//
//   * STREAM LAYOUT (Sec6c.1) - CONFIRMED, disassembly + empirical. The
//     payload is a sequence of self-delimiting per-track blocks; there is
//     no offset table, so track i+1 is reachable only by walking track i.
//     Verified by an oracle THE FILE ITSELF declares: the walk must land
//     on the offset at header +0x30.
//
//   * TRANSLATION (Sec6c.5) - CONFIRMED, disassembly + empirical, with a
//     numeric replay against real bytes. `axis = base/64 + delta/4000`,
//     both scales read from the image as float32 SIMD constants.
//
//   * ROTATION (Sec6c.2) - CONFIRMED by replay, after this reader got it
//     wrong twice. The assembly is:
//
//       component = (base * 64 + mult * delta) * 4 * SCALE
//
//     with base and delta signed 6-bit, mult from {1,2,4,16} selected by
//     the top 2 bits of control bytes 0-2, and SCALE =
//     8.6327287135645750e-05. Ceiling (32*64 + 16*32) * 4 * SCALE = 0.884
//     per component; the data reaches it, giving the full 180-degree range.
//
//     Verified over 2,963,687 samples by three checks that CAN fail and
//     mostly do not: smallest-three holds 99.01%, clamping occurs 37 times,
//     and temporal continuity reads 0.9993 against a cross-track control of
//     0.9614.
//
//     HISTORY, kept deliberately. This reader first computed
//     `(base + mult*delta) * SCALE` - no *64, no *4 - capping components at
//     0.181 and rotation at 36.5 degrees, and that was written up as
//     REFUTING the spec. It refuted nothing but itself. The warning sign
//     was that the same three checks scored PERFECTLY: 100.00%, 0.00%, and
//     a continuity control that beat the real reading. With every component
//     capped at 0.181 the reconstructed one is always >= 0.950, so
//     smallest-three CANNOT fail and clamping CANNOT occur - the checks
//     were vacuous, and a vacuous check is indistinguishable from a
//     triumphant one unless you ask what its failing case would look like.
//
// What that asymmetry means in practice: a consumer of this reader gets
// bone POSITIONS on confirmed arithmetic and bone ORIENTATIONS on inferred
// arithmetic. Translation errors are the kind you would see immediately;
// rotation errors look like a slightly odd animation. Callers that cannot
// tolerate that should check `rotationConfidence()` and gate accordingly.
//
// A NOTE ON VERIFYING ROTATION, because the obvious test is vacuous: the
// fourth component is DEFINED as sqrt(K - (x^2+y^2+z^2)) with clamping, so
// every quaternion this reader emits is unit BY CONSTRUCTION, whatever the
// field widths are. Checking `length == 1` cannot fail and proves nothing.
// The tests that can fail are in tools/validation/validate_anim_payload.cpp:
// the smallest-three property (the reconstructed component must be the
// largest), the clamp rate, and temporal continuity against a shuffled
// control.

#pragma once

#include <cstdint>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/errors.h"
#include "vpp/byte_view.h"

namespace sr3anim {

using vpp::ByteView;

// Sec6c.1: flags bit 0x80 widens the per-track key counts from u8 to u16.
constexpr uint8_t kWideCountsFlag = 0x80;
// Sec6c.1/Sec6c.5: flags bit 0x20 widens translation control records from
// 8 to 16 bytes. CONFIRMED by disassembly, but UNEXERCISED in shipped data
// (0 clips in preload_anim.vpp_pc), so this reader implements it and says
// plainly that no real file has ever proven the branch.
constexpr uint8_t kWideTranslationFlag = 0x20;
// Sec6c.4: perfectly separates clips whose walk lands on the +0x30 oracle
// (0/518 with the bit) from those that do (3,687/3,691 without). WHAT it
// adds to the payload is OPEN - this reader refuses to claim a complete
// walk on a clip that carries it.
constexpr uint8_t kExtraPayloadFlag = 0x40;

// Sec6c.5: exact, read from the image as float32 SIMD broadcast constants.
constexpr float kTranslationBaseScale = 1.0f / 64.0f;    // 0.015625
constexpr float kTranslationDeltaScale = 1.0f / 4000.0f; // 0.00025

// Sec6c.2: read from the binary. K is exactly 1.0 (unit reconstruction);
// the component scale is recorded as the measured double, its closed form
// deliberately NOT guessed (spec marks it OPEN).
constexpr double kRotationK = 1.0;
constexpr double kRotationScale = 8.6327287135645750e-05;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

// How far a decoded quaternion can be trusted, surfaced per sample because
// the clamp is the one place the inferred arithmetic visibly strains.
struct RotationSample {
    Quat value;
    // True when sqrt()'s argument went negative and was clamped to zero.
    // Under a CORRECT assembly this should be rare: the three stored
    // components of a unit quaternion cannot have norm > 1. A high clamp
    // rate is evidence the field widths are wrong - it is the failure this
    // reader can actually detect.
    bool clamped = false;
    // Which lane the reconstructed component was inserted into (0-3).
    uint8_t reconstructedLane = 3;
};

// One track's stream, located by the walk. Offsets are file-relative.
struct TrackBlock {
    uint32_t rotationKeys = 0;
    uint32_t translationKeys = 0;

    size_t rotationControlOffset = 0;
    size_t rotationControlCount = 0;   // 4-byte records
    size_t rotationSampleOffset = 0;   // 3 bytes per key

    size_t translationControlOffset = 0;
    size_t translationControlCount = 0; // 8- or 16-byte records
    size_t translationControlStride = 8;
    // 3 bytes per key (the signed axis deltas), then ONE MORE BYTE PER KEY
    // beginning at translationSampleOffset + translationKeys * 3.
    //
    // That trailing byte is a PER-KEY DURATION - how long this key holds
    // before the next. Established empirically (HANDOFF Sec9.43), not from
    // disassembly, by an oracle the format supplies: every track in a clip
    // spans the same animation, so a per-key duration must sum to the same
    // total across all of that clip's tracks. It does - within-clip relative
    // spread of sum(byte) is 0.0057 over 2,950 clips, against 0.3670 for a
    // cross-clip control (64x) and 1.4041 for a same-shape sum(|dx|) control
    // (246x). Half of all clips agree across every track to within 1%.
    //
    // Not yet surfaced as a decoded value by this reader: the UNIT is
    // unidentified (frames? ticks?), so exposing it as a number would invite
    // callers to treat it as seconds. Read the raw bytes from the offset
    // above if you need them, and see Sec9.43 before assigning a unit.
    size_t translationSampleOffset = 0;

    size_t blockStart = 0;
    size_t blockEnd = 0;
};

// The result of walking one clip's payload.
class Payload {
public:
    // Walks the payload of `bytes` for the already-parsed header `a`.
    // Throws FormatError only on a structurally impossible read (running
    // past the end); a clip whose walk simply does not reach the declared
    // track count comes back with walkComplete() == false rather than an
    // exception, because that is a KNOWN population (Sec6c.4) and not a
    // defect in the file.
    static Payload walk(ByteView bytes, const Animation& a);

    bool walkComplete() const { return walkComplete_; }
    // True when the cursor finished exactly on the offset the file itself
    // declares at +0x30. This is the strong oracle - the file states it, so
    // it was not chosen to make the walk look right.
    bool landedOnDeclaredEnd() const { return landedOnEnd_; }
    size_t endCursor() const { return endCursor_; }

    // Sec6c.4: clips carrying flags bit 0x40 have payload BEYOND the
    // declared tracks whose meaning is OPEN. The walk of the declared
    // tracks is still performed; this says the clip is not fully accounted
    // for, so callers do not mistake a partial walk for a complete one.
    bool hasUnaccountedPayload() const { return hasExtra_; }

    const std::vector<TrackBlock>& tracks() const { return tracks_; }

    // Decoded values. Both take the file bytes again rather than caching,
    // so a Payload stays a cheap description of layout.
    // IMPORTANT FOR CALLERS (measured, HANDOFF Sec9.42): most tracks have
    // NO translation data. Over 3,687 clean clips, 150,466 of 166,297
    // tracks - 90.5% - carry ZERO translation keys, and this returns empty
    // for them. Translation is stored only for bones that actually
    // translate, not as a per-bone restatement of the rest offset.
    //
    // So a renderer must take a bone's local translation from the RIG's
    // rest offset when the clip has no translation track for that bone, and
    // from here only when it does. Expecting a translation per bone per
    // frame is wrong for nine bones in ten.
    //
    // Corollary, also measured: the magnitudes this returns are NOT
    // constant within a track (median relative spread 1.27 over 10,122
    // tracks, looser than an axis-shuffled control) - because the tracks
    // that exist are exactly the ones whose translation changes. Do not
    // treat a translation track as a rigid bone length.
    std::vector<Vec3> translations(ByteView bytes, size_t trackIndex) const;

    // True since the assembly was corrected and replayed (see above).
    // Retained as an explicit gate rather than removed: it was false for
    // good reason for part of a day, and a caller that wants to assert the
    // decode is trustworthy should have something to assert against.
    static constexpr bool rotationsUsable() { return true; }
    std::vector<RotationSample> rotations(ByteView bytes, size_t trackIndex) const;

private:
    std::vector<TrackBlock> tracks_;
    bool walkComplete_ = false;
    bool landedOnEnd_ = false;
    bool hasExtra_ = false;
    size_t endCursor_ = 0;
};

} // namespace sr3anim
