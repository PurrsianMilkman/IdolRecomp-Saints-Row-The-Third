#include "sr3rig/rig.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "vpp/hash.h"

namespace sr3rig {

namespace {

float readF32LE(ByteView bytes, size_t offset) {
    uint32_t raw = bytes.readU32LE(offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "f32 must be 4 bytes");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::string lowercased(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Reads a null-terminated name at `base + offset`, bounded by the buffer.
bool readName(ByteView content, size_t base, uint32_t offset, std::string& out) {
    if (offset == kNoName) {
        out.clear();
        return true;
    }
    size_t at = base + offset;
    if (at >= content.size()) return false;
    size_t length = content.cStringLength(at);
    ByteView s = content.subview(at, length);
    out.assign(reinterpret_cast<const char*>(s.data()), s.size());
    return true;
}

} // namespace

Rig Rig::parse(ByteView content) {
    Rig rig;

    if (content.size() < kHeaderSize) {
        throw FormatError("content too small to contain the 0x50-byte .rig_pc header");
    }

    uint32_t boneCount = content.readU32LE(kBoneCountOffset);
    uint32_t attachmentCount = content.readU32LE(kAttachmentCountOffset);
    rig.trailingGroupCount_ = content.readU32LE(kTrailingGroupCountOffset);
    rig.leadingGroupCount_ = content.readU32LE(kLeadingGroupCountOffset);

    // Layout (spec §3): hash table at +0x50, pad to 8, bone array, then
    // attachment array, then the name region.
    const size_t hashTableAt = kHeaderSize;
    const size_t hashTableBytes = static_cast<size_t>(boneCount) * 4;
    if (hashTableAt + hashTableBytes > content.size()) {
        throw FormatError("hash table (" + std::to_string(boneCount) +
                          " x 4 bytes) runs past the end of the file");
    }
    const size_t boneArrayAt = alignUp(hashTableAt + hashTableBytes, 8);
    const size_t boneArrayBytes = static_cast<size_t>(boneCount) * kBoneRecordSize;
    if (boneArrayAt + boneArrayBytes > content.size()) {
        throw FormatError("bone array (" + std::to_string(boneCount) +
                          " x 0x28 bytes at " + std::to_string(boneArrayAt) +
                          ") runs past the end of the file");
    }
    const size_t attachmentArrayAt = boneArrayAt + boneArrayBytes;
    const size_t attachmentArrayBytes =
        static_cast<size_t>(attachmentCount) * kAttachmentRecordSize;
    if (attachmentArrayAt + attachmentArrayBytes > content.size()) {
        throw FormatError("attachment array (" + std::to_string(attachmentCount) +
                          " x 0x40 bytes) runs past the end of the file");
    }
    // The name region begins immediately after both arrays, and the name
    // offsets in both records are relative to it (spec §3).
    const size_t nameRegionAt = attachmentArrayAt + attachmentArrayBytes;

    rig.bones_.reserve(boneCount);
    for (uint32_t i = 0; i < boneCount; ++i) {
        const size_t at = boneArrayAt + static_cast<size_t>(i) * kBoneRecordSize;
        Bone bone;
        uint32_t nameOffset = content.readU32LE(at + 0x00);
        if (!readName(content, nameRegionAt, nameOffset, bone.name)) {
            throw FormatError("bone " + std::to_string(i) + "'s name offset " +
                              std::to_string(nameOffset) +
                              " does not resolve inside the name region - spec §3 has these "
                              "resolving 22,274/22,274, so a miss means the layout is wrong");
        }
        for (size_t c = 0; c < 3; ++c) {
            bone.restPosition[c] = readF32LE(content, at + 0x08 + c * 4);
            bone.negatedParentOffset[c] = readF32LE(content, at + 0x14 + c * 4);
        }
        bone.parentIndex = content.readU32LE(at + 0x20);
        bone.nameHash = content.readU32LE(hashTableAt + static_cast<size_t>(i) * 4);
        rig.bones_.push_back(std::move(bone));
    }

    // Parent tree validity: parent < child in 22,274/22,274, and every
    // bone reachable from a root in 585/585 (spec §4). Both are CONFIRMED,
    // so a violation means this is not a .rig_pc.
    for (uint32_t i = 0; i < boneCount; ++i) {
        uint32_t parent = rig.bones_[i].parentIndex;
        if (parent == kNoParent) continue;
        if (parent >= boneCount) {
            throw FormatError("bone " + std::to_string(i) + " has parent index " +
                              std::to_string(parent) + ", which is out of range");
        }
        if (parent >= i) {
            throw FormatError("bone " + std::to_string(i) + "'s parent (" +
                              std::to_string(parent) +
                              ") does not precede it; spec §4 has parent < child in "
                              "22,274/22,274 bones");
        }
    }

    rig.attachments_.reserve(attachmentCount);
    for (uint32_t i = 0; i < attachmentCount; ++i) {
        const size_t at = attachmentArrayAt + static_cast<size_t>(i) * kAttachmentRecordSize;
        Attachment attachment;
        uint32_t nameOffset = content.readU32LE(at + 0x00);
        if (!readName(content, nameRegionAt, nameOffset, attachment.name)) {
            throw FormatError("attachment " + std::to_string(i) +
                              "'s name offset does not resolve inside the name region");
        }
        for (size_t row = 0; row < 3; ++row) {
            for (size_t c = 0; c < 3; ++c) {
                attachment.rotationRows[row][c] =
                    readF32LE(content, at + 0x08 + row * 12 + c * 4);
            }
        }
        for (size_t c = 0; c < 3; ++c) {
            attachment.translation[c] = readF32LE(content, at + 0x2C + c * 4);
        }
        attachment.parentBoneIndex = content.readU32LE(at + 0x38);
        if (attachment.parentBoneIndex >= boneCount) {
            throw FormatError("attachment " + std::to_string(i) + " has parent bone index " +
                              std::to_string(attachment.parentBoneIndex) +
                              ", out of range; spec §6 has these valid 12,480/12,480");
        }
        attachment.tag = static_cast<int32_t>(content.readU32LE(at + 0x3C));
        rig.attachments_.push_back(std::move(attachment));
    }

    return rig;
}

bool Rig::hashTableMatchesNames() const {
    for (const Bone& bone : bones_) {
        if (bone.name.empty()) continue;
        if (vpp::hashFilename(lowercased(bone.name)) != bone.nameHash) return false;
    }
    return true;
}

int Rig::findBone(const std::string& name) const {
    // First case-insensitive string match in bone order (spec §13.1). The
    // per-bone hash is not consulted: a hash comparison could accept a
    // colliding name the engine's own compare would reject.
    const std::string wanted = lowercased(name);
    for (size_t i = 0; i < bones_.size(); ++i) {
        if (lowercased(bones_[i].name) == wanted) return static_cast<int>(i);
    }
    return -1;
}

} // namespace sr3rig
