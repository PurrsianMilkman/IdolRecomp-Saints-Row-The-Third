#include "sr3geometry/material_block.h"

namespace sr3geometry {

MaterialBlock MaterialBlock::parse(ByteView bytes) {
    if (bytes.size() < kMaterialBlockHeaderSize) {
        throw FormatError("content too small to contain the material-block fixed header");
    }
    uint32_t magic = bytes.readU32LE(0x00);
    if (magic != kMaterialBlockMagic) {
        throw FormatError("bad magic number: expected 0x00043854 (spec-texture-format.md Appendix)");
    }

    uint32_t nameTableLength = bytes.readU32LE(0x04);
    // +0x08 is a confirmed-zero field, not read.
    MaterialBlock m;
    m.textureSlotCount = bytes.readU32LE(0x0C);
    // +0x10-+0x1F is confirmed zero padding, not read.

    size_t pos = kMaterialBlockHeaderSize;
    m.textureNames.reserve(m.textureSlotCount);
    for (uint32_t i = 0; i < m.textureSlotCount; ++i) {
        size_t len = bytes.cStringLength(pos);
        ByteView nameBytes = bytes.subview(pos, len);
        m.textureNames.emplace_back(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
        pos += len + 1;
    }

    size_t actualNameTableLength = pos - kMaterialBlockHeaderSize;
    if (actualNameTableLength != nameTableLength) {
        throw FormatError(
            "declared name-table length (+0x04) does not match the actual "
            "null-terminated name bytes found - spec confirms these match "
            "exactly on every real sample checked");
    }

    m.totalSize = pos;
    return m;
}

} // namespace sr3geometry
