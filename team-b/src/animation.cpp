#include "sr3anim/animation.h"

#include <cstring>
#include <string>

namespace sr3anim {

namespace {

// Offsets of the optional (length-dependent) fields, from file start.
constexpr size_t kTrailingOffsetField = 0x30;   // spec Sec2: a confirmed in-file offset; target reported upstream as the keyframe-payload end, not verified here (see animation.h)
constexpr size_t kOptionalSectionField = 0x40;  // spec Sec2/Sec3: gated by flags bit 0x10
constexpr size_t kOptionalSectionFieldEnd = 0x48; // +0x40 and its paired word at +0x44

// Reads a little-endian IEEE-754 f32. ByteView only exposes integer
// reads, so pull the bits and memcpy them across - memcpy rather than a
// reinterpret_cast to stay clear of strict-aliasing UB.
float readF32LE(ByteView bytes, size_t offset) {
    uint32_t raw = bytes.readU32LE(offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "f32 must be 4 bytes");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

} // namespace

Animation Animation::parse(ByteView bytes) {
    if (bytes.size() < kCoreHeaderSize) {
        throw FormatError("content too small to contain the confirmed .anim_pc core header (" +
                           std::to_string(bytes.size()) + " bytes, need at least " +
                           std::to_string(kCoreHeaderSize) + ")");
    }

    uint32_t magic = bytes.readU32LE(0x00);
    if (magic != kMagic) {
        throw FormatError("bad magic number: expected 'ANIM' / 0x4D494E41 (spec Sec2, CONFIRMED)");
    }

    Animation a;
    a.version_ = bytes.at(0x04);
    if (a.version_ != kRequiredVersion) {
        // The real loader rejects anything not exactly 14 (spec Sec2,
        // CONFIRMED via disassembly and 4209/4209 empirically), so this is
        // a genuine hard rule of the format, not a tolerance choice here.
        throw FormatError("unsupported .anim_pc version " + std::to_string(a.version_) +
                           ": the format requires exactly " + std::to_string(kRequiredVersion) +
                           " (spec Sec2, CONFIRMED - the real loader rejects any other value)");
    }

    a.flags_ = bytes.at(0x05);
    a.field_0x06_ = bytes.readU16LE(0x06);
    a.field_0x08_ = bytes.at(0x08);
    a.field_0x09_ = bytes.at(0x09);
    a.field_0x0A_ = bytes.at(0x0A);
    a.field_0x0B_ = bytes.at(0x0B);

    // Root-motion rotation: a contiguous 4-float quaternion at +0x0C.
    // spec Sec2 tabulates x/y/z (+0x0C, 12 bytes) and w (+0x18) as
    // separate rows, but they are adjacent and spec Sec4 reads them as one
    // (x,y,z,w) at +0x0C - which is the reading its 4209/4209
    // normalization test confirms.
    a.rootRotation_.x = readF32LE(bytes, 0x0C);
    a.rootRotation_.y = readF32LE(bytes, 0x10);
    a.rootRotation_.z = readF32LE(bytes, 0x14);
    a.rootRotation_.w = readF32LE(bytes, 0x18);

    a.rootTranslation_.x = readF32LE(bytes, 0x1C);
    a.rootTranslation_.y = readF32LE(bytes, 0x20);
    a.rootTranslation_.z = readF32LE(bytes, 0x24);

    // +0x28 was documented here as "observed zero everywhere and OPEN".
    // THAT WAS WRONG, and measuring it is what showed so: it is non-zero in
    // 394 of 4,209 shipped clips (`tools/validation/validate_anim_field28.cpp`).
    // It is a file-relative offset to a TRACK -> BONE index table, and it
    // takes only two values, 0x38 (382) and 0x48 (12) - always exactly the
    // header size, so the table begins immediately after the header.
    //
    // Guarded on length like every other post-core field: kCoreHeaderSize
    // is 0x28, so reading a u32 AT 0x28 needs four bytes the core does not
    // guarantee. All 4,209 shipped files are long enough, but the format's
    // rule is that it never declares a field it lacks room for, and the 12
    // short static-prop poses are exactly why this reader checks instead of
    // assuming.
    if (bytes.size() >= kTrackBoneTableField + 4) {
        a.trackBoneTableOffset_ = bytes.readU32LE(kTrackBoneTableField);
    }

    if (bytes.size() >= kTrailingOffsetField + 4) {
        a.hasTrailingOffset_ = true;
        a.trailingOffset_ = bytes.readU32LE(kTrailingOffsetField);
    }

    // +0x34 (12 bytes) is uncharacterised and OPEN - not read.

    // The optional section exists only when flags bit 0x10 says so (spec
    // Sec3, CONFIRMED - disassembly). The length check is belt-and-braces:
    // the format never declares a field it lacks room for, so on real data
    // a set flag always comes with the bytes to back it.
    if (a.hasOptionalSection() && bytes.size() >= kOptionalSectionFieldEnd) {
        a.hasOptionalSectionOffset_ = true;
        a.optionalSectionOffset_ = bytes.readU32LE(kOptionalSectionField);
    }
    // The loader's own fixup - rewriting +0x40 to an absolute pointer and
    // zeroing +0x44 - is a runtime concern, deliberately not done here.

    return a;
}

float Animation::rootRotationNormSquared() const {
    const Quaternion& q = rootRotation_;
    return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
}

} // namespace sr3anim
