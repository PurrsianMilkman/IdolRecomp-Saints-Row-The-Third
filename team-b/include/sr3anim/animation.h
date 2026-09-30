// Reader for .anim_pc animation files (spec-anim-format.md).
//
// SCOPE, deliberately partial - this reader covers the CONFIRMED header
// and nothing else:
//   * the `ANIM` magic and the version-must-be-14 rule (spec Sec2, a rule
//     the real loader itself enforces),
//   * the flags byte, of which only bit 0x10 has a confirmed meaning,
//   * the root-motion transform: a unit quaternion at +0x0C and a
//     translation vector at +0x1C (spec Sec4 - the most decisively
//     confirmed structure in that document: 4209/4209 real files against
//     0/4209 and 0/12177 on two independent controls),
//   * the optional section at +0x40 that flags bit 0x10 gates (spec Sec3).
//
// It deliberately does NOT attempt the per-keyframe/per-track payload.
// spec Sec6 is explicit that this is unresolved AND that the obvious
// uniform frames x tracks x bytes-per-key model is measurably wrong
// (median 0.55 bytes per frame-track, sd 0.34 - i.e. no consistent value,
// and a nominal half-byte per key that cannot be a literal uncompressed
// key). Reaching it needs the runtime animation-sampling subsystem, not
// more parsing. Guessing at it here would be exactly the kind of
// unfounded extrapolation this project treats as worse than a gap.
//
// This mirrors how sr3save handles the save-snapshot format: take the
// confirmed fields, expose everything uncertain as raw values named by
// offset rather than by a guessed meaning, and leave the large unmapped
// region alone.

#pragma once

#include <cstdint>

#include "sr3anim/errors.h"
#include "vpp/byte_view.h"

namespace sr3anim {

using vpp::ByteView;

// "ANIM" - on-disk bytes 41 4E 49 4D, so 0x4D494E41 read little-endian.
// CONFIRMED - empirical, 4209/4209 (spec Sec2).
constexpr uint32_t kMagic = 0x4D494E41u;

// CONFIRMED both ways (spec Sec2): the loader rejects anything not exactly
// 14, and all 4,209 real files read exactly 14.
constexpr uint8_t kRequiredVersion = 14;

// CONFIRMED - disassembly (spec Sec3): the only bit of the flags byte
// whose meaning is known. It gates the optional section at +0x40.
constexpr uint8_t kOptionalSectionFlag = 0x10;

// Bytes needed for every CONFIRMED core field, magic through the
// root-motion translation vector (which ends at +0x28). Deliberately NOT
// the full 0x48 header: 12 real files are shorter than that (spec Sec8
// item 6, static prop poses) and are valid - the format simply never
// declares a field it lacks room for (spec Sec3).
constexpr size_t kCoreHeaderSize = 0x28;

// A file-relative offset field reads this to mean null (spec Sec2, +0x40).
constexpr uint32_t kNullOffset = 0xFFFFFFFFu;

// Header +0x28: offset to the track-to-bone index table; 0 = identity mapping.
constexpr size_t kTrackBoneTableField = 0x28;

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class Animation {
public:
    // Parses `bytes` (one whole .anim_pc file). Throws FormatError if the
    // magic isn't `ANIM` or the version isn't exactly 14 - the two rules
    // the real loader enforces - or if `bytes` is too short to hold the
    // confirmed core fields (kCoreHeaderSize). Fields past the core are
    // read only when the file is actually long enough to contain them, so
    // the short real files parse rather than being rejected.
    static Animation parse(ByteView bytes);

    uint8_t version() const { return version_; } // always kRequiredVersion; validated at parse

    // Raw flags byte (+0x05). Only bit kOptionalSectionFlag has a
    // confirmed meaning - see hasOptionalSection(). Bits 0x01, 0x04,
    // 0x08, 0x40 and 0x80 all occur in real files in stable combinations
    // but are undecoded (spec Sec8 item 2), so they are exposed raw here
    // rather than given invented names.
    uint8_t flags() const { return flags_; }

    // CONFIRMED - disassembly (spec Sec3): whether the optional section
    // at +0x40 is present at all. The loader only touches +0x40 when this
    // is set. Independently corroborated empirically: 167/167 of the real
    // files that set this bit hold a valid in-file offset at +0x40,
    // against 3.4% (the by-chance rate) among those that don't.
    bool hasOptionalSection() const { return (flags_ & kOptionalSectionFlag) != 0; }

    // The root-motion delta transform (spec Sec4): the net rotation and
    // translation the animation applies to its root over its duration.
    // The quaternion is CONFIRMED (see the class comment); reading the
    // paired translation as specifically ROOT translation is the spec's
    // own HIGH CONFIDENCE inference, from its being exactly zero in the
    // 1,954 files that shouldn't move (in-place animations) and large in
    // the ones that should.
    const Quaternion& rootRotation() const { return rootRotation_; }
    const Vector3& rootTranslation() const { return rootTranslation_; }

    // Squared norm of rootRotation(). A real .anim_pc has this within
    // 1e-4 of 1.0 - 4209/4209 across the whole shipped population, versus
    // 0/4209 and 0/12177 on two independent controls (spec Sec4). That
    // makes it a genuinely self-validating structure: arbitrary float
    // quadruples essentially never normalize to 1. Exposed so callers can
    // use it as a cheap corruption check; deliberately NOT enforced by
    // parse(), which sticks to the two rules the real loader enforces.
    // (If .anim_pc ever turns up as a non-first entry of a mode-(a)
    // container - the OkUnconfirmedContent shape - this is exactly the
    // check a content-validation corroborator would use; see
    // vpp/content_validation.h for that pattern. No such real case has
    // been found, so none is built.)
    float rootRotationNormSquared() const;

    // True if the file was long enough to contain the +0x30 field.
    bool hasTrailingOffset() const { return hasTrailingOffset_; }

    // +0x30. CONFIRMED - empirical (spec Sec2) to be a valid in-file
    // offset (<= file size) in 4209/4209 real files. This reader surfaces
    // the value and does not follow it.
    //
    // On what it POINTS AT: spec Sec6c.1 now reports it as the END OF THE
    // KEYFRAME PAYLOAD - a per-track walk lands on it exactly in 3687/4086
    // clips (90.2%). That is NOT verified here, and deliberately so:
    // confirming it requires performing the per-track walk, which IS the
    // keyframe payload decode, and that work is PARKED pending the user.
    // So the honest state is neither "OPEN" nor "confirmed" but: reported
    // upstream with a strong oracle test, not independently reproduced by
    // this reader. Nothing here depends on it either way.
    //
    // Only meaningful when hasTrailingOffset().
    uint32_t trailingOffset() const { return trailingOffset_; }

    // True if the file both sets flags bit 0x10 AND is long enough to
    // contain +0x40. The format never declares a field it lacks room for
    // (spec Sec3: 0/12 of the short files set the flag, and every file
    // that does is at least 284 bytes), so in real data this matches
    // hasOptionalSection() exactly - the length check is belt-and-braces.
    bool hasOptionalSectionOffset() const { return hasOptionalSectionOffset_; }

    // +0x40, the optional section's FILE-RELATIVE offset, valid only when
    // hasOptionalSectionOffset(). kNullOffset (-1) means null - see
    // optionalSectionIsNull(). The real loader converts this into an
    // absolute pointer at load time by adding the buffer base (and zeroes
    // the paired word at +0x44); that fixup is a runtime concern and is
    // deliberately not performed here - this reader never writes to the
    // file bytes.
    uint32_t optionalSectionOffset() const { return optionalSectionOffset_; }

    // True when the optional section offset is present but explicitly
    // null (-1), which the loader treats as "no section".
    bool optionalSectionIsNull() const {
        return hasOptionalSectionOffset_ && optionalSectionOffset_ == kNullOffset;
    }

    // --- Fields CONFIRMED present and varying, but whose MEANING is
    // OPEN or only hypothesised (spec Sec2, Sec8 item 4). Named by offset
    // on purpose: spec Sec6 actively undermines the obvious "frame count"
    // reading of +0x06, and the per-bone-track-count reading of
    // +0x0A/+0x0B is explicitly flagged HIGH CONFIDENCE but not
    // confirmed. Giving these interpreted names would assert something
    // the spec declines to. ---
    // Header +0x28: a file-relative offset to a TRACK -> BONE index table,
    // or 0 when tracks map to bones by identity (track i animates bone i).
    //
    // This field was previously documented in this reader as "observed zero
    // everywhere and OPEN". That was wrong — measured over the shipped
    // population it is non-zero in **394 of 4,209** clips, taking only the
    // values 0x38 (382) and 0x48 (12), always exactly the header size, so
    // the table starts immediately after the header. The bytes there pass a
    // permutation test in 355/394 against a 35/394 control, and one reads
    // `[0,1,3,4,2,6,7,5,9,10,8,14,15,17]`.
    //
    // That last detail is why it was missed twice: it is a PERMUTATION, and
    // the original pass searched for an ASCENDING run here, found none, and
    // recorded "no such table exists". A scan assuming an internal ordering
    // rules out that ordering, not the structure (HANDOFF §3).
    //
    // Consequence for anyone computing payload extents: the keyframe data
    // does NOT begin at a fixed 0x48. It begins after the header AND this
    // table when present. This reader does not touch the payload itself -
    // that is `sr3anim/payload.h` (HANDOFF §9.37, built from spec-anim-
    // format.md §6c's resolved stream layout/translation decode, rotation
    // confirmed by two independent replay methods, §9.37.2) - but the
    // offset is surfaced so nobody re-derives it. STALE UNTIL 2026-09-29:
    // this comment used to say the payload encoding was "still OPEN and
    // animation remains parked" - both closed the same day, one section
    // later, by the file this comment sits next to (rule 16).
    bool hasTrackBoneTable() const { return trackBoneTableOffset_ != 0; }
    uint32_t trackBoneTableOffset() const { return trackBoneTableOffset_; }

    // +0x06: the clip's TOTAL DURATION. Identified 2026-09-12 (HANDOFF
    // Sec9.47) after standing as [HYPOTHESIS - plausibly a frame or key
    // count] since the header was first tabulated.
    //
    // Two independently-derived measurements, neither assuming the other:
    //   * the extra byte per translation key is a PER-KEY duration - every
    //     track in a clip must sum to the same total, and does (within-clip
    //     spread 0.0057 against 64x and 246x controls, Sec9.43);
    //   * that per-clip total equals this field EXACTLY - 81.46% of the
    //     1,483 clips where the total is itself reliably estimated, against
    //     a shuffled control at 0.10%. Every near-control (track count, bone
    //     count, file size, declared end) sits at chance.
    //
    // This does NOT revive spec Sec6's refuted `+0x06 x tracks x
    // bytes_per_key` payload model. That model is still wrong, and for the
    // same reason this reading is right: keys are sparse and each HOLDS for
    // a variable number of frames, so a frames-by-tracks array over-counts
    // by roughly the mean gap between keys (median 2.2).
    //
    // [CONFIRMED - empirical, two independent lines.] Not disassembly
    // checked. The UNIT is unidentified - a median of 240 is consistent
    // with e.g. 8 seconds at 30Hz, but nothing measured establishes a rate,
    // so the accessor keeps a neutral name rather than saying "frames".
    uint16_t durationTotal() const { return field_0x06_; }

    // Retained: this field was named by offset for the whole period it was
    // unidentified, and callers may still use that name.
    uint16_t field_0x06_rawCount() const { return field_0x06_; }
    uint8_t field_0x08_rawCount() const { return field_0x08_; }
    uint8_t field_0x09_rawCount() const { return field_0x09_; }
    uint8_t field_0x0A_rawCount() const { return field_0x0A_; }
    uint8_t field_0x0B_rawCount() const { return field_0x0B_; }

private:
    uint8_t version_ = 0;
    uint8_t flags_ = 0;
    uint16_t field_0x06_ = 0;
    uint8_t field_0x08_ = 0;
    uint8_t field_0x09_ = 0;
    uint8_t field_0x0A_ = 0;
    uint8_t field_0x0B_ = 0;
    Quaternion rootRotation_;
    Vector3 rootTranslation_;
    bool hasTrailingOffset_ = false;
    uint32_t trailingOffset_ = 0;
    uint32_t trackBoneTableOffset_ = 0;
    bool hasOptionalSectionOffset_ = false;
    uint32_t optionalSectionOffset_ = 0;
};

} // namespace sr3anim
