// Reader for `.rig_pc` skeletons (spec-rig-format.md v2).
//
// Standalone format - no c/g pairing. Gives the bone hierarchy, per-bone
// model-space rest positions, a rotation triple, the engine-hash lookup
// table, and the attachment/IK-target array.
//
// WHAT THIS IS FOR, STATED HONESTLY. The obvious next step after a mesh on
// screen is "skinning", and it is worth being precise about what bind-pose
// skinning actually buys, because it is easy to claim more than it does:
//
//   Skinning evaluates  v' = sum_i w_i * (Pose_i * InverseBind_i) * v.
//   In BIND POSE, Pose_i == Bind_i, so every Pose_i * InverseBind_i is the
//   IDENTITY, and v' == v exactly.
//
// So a mesh "skinned in bind pose" is pixel-identical to the same mesh
// drawn with no skinning at all. Wiring up a skinning shader and showing
// the same picture would be a fake milestone. What IS real here:
//
//   * the rig reader itself, validated against the spec's own population
//     statistics (22,274/22,274 bone-name hashes, parent-tree validity);
//   * a genuine CROSS-VALIDATION between two independently decoded
//     formats - the mesh's blend indices point into this bone array, so
//     vertices weighted to a bone should sit near that bone's rest
//     position. Nothing in either format's decoding forces that to be
//     true, which is what makes it evidence;
//   * a skeleton that can be drawn over the mesh and looked at.
//
// THERE IS NO ROTATION IN THE BONE RECORD. The spec's §4 "rest rotation"
// at `+0x14`, with its convention marked OPEN in §10 item 1, is not a
// rotation: it is the negated parent-relative offset, derivable from the
// positions already present (22,274/22,274 - see Bone::negatedParentOffset
// and HANDOFF.md §9.15). The whole 40-byte record is now accounted for and
// none of it carries orientation.
//
// The practical consequence is a good one. Rest positions are MODEL-SPACE
// (spec §4), so the skeleton places and draws with no rotation composed -
// and that is not a simplification awaiting a "real" answer later, it IS
// the answer. A bind pose built from pure translations is exact.
//
// Per-bone ORIENTATION does exist in this format, but in the attachment
// array, not the bone array: each attachment record carries a 3x3 ROTATION
// MATRIX (rows at +0x08/+0x14/+0x20) and a bone-local translation at +0x2C
// (spec-rig-format.md §13.4, corrected 2026-09-20 - this reader previously
// took +0x20 for a quaternion, which was the third row of the matrix plus the
// translation's x; see HANDOFF.md §9.70). Real per-frame animation still
// needs the `.anim_pc` keyframe payload, which is out of scope here.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3rig/errors.h"
#include "vpp/byte_view.h"

namespace sr3rig {

using vpp::ByteView;

constexpr size_t kHeaderSize = 0x50;
constexpr size_t kBoneRecordSize = 0x28;
constexpr size_t kAttachmentRecordSize = 0x40;

constexpr size_t kBoneCountOffset = 0x24;
constexpr size_t kTrailingGroupCountOffset = 0x28; // T (spec-rig-format.md §13.2)
constexpr size_t kLeadingGroupCountOffset = 0x2C;  // K (spec-rig-format.md §13.2)
constexpr size_t kAttachmentCountOffset = 0x30;

constexpr uint32_t kNoParent = 0xFFFFFFFFu;
constexpr uint32_t kNoName = 0xFFFFFFFFu;

struct Bone {
    std::string name;
    // MODEL-SPACE rest position (spec §4). Not parent-relative - the spec
    // rules that out by magnitude: child bones are larger than roots, which
    // parent-relative offsets could not produce.
    std::array<float, 3> restPosition{};
    // NOT A ROTATION. The spec calls `+0x14` a "rest rotation" with the
    // Euler-vs-axis-angle convention OPEN; it is neither. It is the bone's
    // parent-relative offset, negated - fully derivable from data already
    // in this record:
    //
    //     child : triple == -(restPosition - parent.restPosition)
    //     root  : triple == -restPosition
    //
    // Verified here over the whole population: 20,564/20,564 child bones
    // and 1,710/1,710 root bones agree to 1e-5 (max error 3.8e-06), against
    // a wrong-parent control of 2.61% / 0.41%. See HANDOFF.md §9.15.
    //
    // The spec's own 117 "exceptions" that exceed +/-pi - flagged there as
    // possible rotor bones "stored beyond one turn" - satisfy the identity
    // 117/117. They are not spin bones; they are long bones in wings and
    // aircraft (`avatar_wings/l-wingc`, `sp_vtol01_cs/engine_coverLB`),
    // where a metre-scale offset simply exceeds 3.14.
    //
    // Kept as stored rather than dropped, because a reader's job is to
    // report the bytes; use parentRelativeOffset() for the useful sign.
    std::array<float, 3> negatedParentOffset{};

    // The bone's offset from its parent (from the origin, for a root) -
    // i.e. negatedParentOffset with the sign undone.
    std::array<float, 3> parentRelativeOffset() const {
        return {-negatedParentOffset[0], -negatedParentOffset[1], -negatedParentOffset[2]};
    }
    uint32_t parentIndex = kNoParent; // kNoParent = root
    uint32_t nameHash = 0;            // from the +0x50 table, the lowercased-name engine hash
    bool isRoot() const { return parentIndex == kNoParent; }
};

// spec-rig-format.md §13.4 (12,480/12,480 records, orthonormal with
// determinant +1, shifted-offset controls 0/12,480). For the 68% of
// attachments named after their parent bone the translation is exactly zero
// and row 0 points along the bone.
struct Attachment {
    std::string name;
    // Three rows of three floats at +0x08 / +0x14 / +0x20, exactly as stored
    // (the engine's row-vector convention; this reader does not transpose).
    std::array<std::array<float, 3>, 3> rotationRows{};
    std::array<float, 3> translation{}; // +0x2C, bone-local
    uint32_t parentBoneIndex = 0;       // +0x38, < bone count
    // +0x3C. -1 in 12,480/12,480 shipped records; its only known consumer is
    // a tag-filtered by-name lookup, inert with shipped data.
    int32_t tag = -1;
};

class Rig {
public:
    // Parses a whole .rig_pc. Throws FormatError if the file is too small
    // for its declared counts, if any name offset fails to resolve, or if
    // the parent indices do not form a valid tree (parent < child, every
    // bone reachable from a root) - all properties the spec confirms hold
    // in 585/585 shipped rigs, so a violation means this is not the format.
    static Rig parse(ByteView content);

    const std::vector<Bone>& bones() const { return bones_; }
    const std::vector<Attachment>& attachments() const { return attachments_; }

    // Index of the FIRST bone whose name equals `name` ignoring case, or -1
    // (spec-rig-format.md §13.1: the engine's own by-name lookup is a plain
    // case-insensitive string compare over the bone array; it never uses the
    // per-bone hash table, which is loader-side data with no runtime reader
    // found).
    int findBone(const std::string& name) const;

    // Header +0x2C (K) and +0x28 (T): the bones split into a leading group
    // [0, K) and a trailing group [K, boneCount) of T bones, and
    // K + T == boneCount in 585/585 rigs (spec-rig-format.md §13.2). T is a
    // per-class constant (45 for the standard humanoid rigs). The MEANING of
    // the split is unsupported: no runtime reader of either field was found,
    // and the old "detail bones begin here" reading has no code behind it.
    // Exposed as the counts they are, not as a semantic boundary.
    uint32_t leadingGroupCount() const { return leadingGroupCount_; }
    uint32_t trailingGroupCount() const { return trailingGroupCount_; }
    bool groupPartitionHolds() const {
        return static_cast<uint64_t>(leadingGroupCount_) + trailingGroupCount_ == bones_.size();
    }

    // True if every bone's stored hash equals the engine hash of its
    // lowercased name - the spec's 22,274/22,274 check, re-run per file.
    bool hashTableMatchesNames() const;

private:
    std::vector<Bone> bones_;
    std::vector<Attachment> attachments_;
    uint32_t leadingGroupCount_ = 0;
    uint32_t trailingGroupCount_ = 0;
};

} // namespace sr3rig
