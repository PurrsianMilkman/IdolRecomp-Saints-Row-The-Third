#include "sr3texture/texture_pair.h"

#include <stdexcept>

namespace sr3texture {

namespace {

// The spec's Sec11.1 table, one row per accepted code. `blockEdge` is the
// clamp used by the level walk (Sec10.5); `shipped` mirrors the "Records in
// the shipped population" column being non-zero.
struct FormatRow {
    uint32_t code;
    uint32_t blockEdge;
    const char* d3d9Name;
    bool shipped;
};

constexpr FormatRow kFormats[] = {
    {kFormatDxt1, 4, "D3DFMT_DXT1", true},
    {kFormatDxt3, 4, "D3DFMT_DXT3", true},
    {kFormatDxt5, 4, "D3DFMT_DXT5", true},
    {kFormatR5G6B5, 1, "D3DFMT_R5G6B5", true},
    {kFormatA1R5G5B5, 1, "D3DFMT_A1R5G5B5", false},
    {kFormatA4R4G4B4, 1, "D3DFMT_A4R4G4B4", false},
    {kFormatR8G8B8, 1, "D3DFMT_R8G8B8", false},
    {kFormatA8R8G8B8, 1, "D3DFMT_A8R8G8B8", true},
    {kFormatV8U8, 1, "D3DFMT_V8U8", false},
    {kFormatCxV8U8, 1, "D3DFMT_CxV8U8", false},
    {kFormatA8, 1, "D3DFMT_A8", true},
    {kFormatDxt5Alt, 4, "D3DFMT_DXT5", false},
};

} // namespace

bool lookupPixelFormat(uint32_t code, PixelFormatInfo& out) {
    for (const FormatRow& row : kFormats) {
        if (row.code == code) {
            out.code = row.code;
            out.blockEdge = row.blockEdge;
            out.d3d9Name = row.d3d9Name;
            out.shipped = row.shipped;
            return true;
        }
    }
    return false;
}

uint32_t sourceRowBytes(uint32_t code, uint32_t w) {
    switch (code) {
        case kFormatDxt1:
            return (w >> 1) * 4; // spec Sec11.1, taken literally: 8 bytes per 4x4 block == 2 bytes per pixel column
        case kFormatDxt3:
        case kFormatDxt5:
        case kFormatDxt5Alt:
            return w * 4;
        case kFormatR5G6B5:
        case kFormatA1R5G5B5:
        case kFormatA4R4G4B4:
        case kFormatV8U8:
        case kFormatCxV8U8:
            return w * 2;
        case kFormatR8G8B8:
            return w * 3;
        case kFormatA8R8G8B8:
            return w * 4;
        case kFormatA8:
            return w;
        default:
            return 0;
    }
}

bool computeLevelLayout(uint32_t code, uint32_t width, uint32_t height, uint32_t levelCount,
                        bool cube, LevelLayout& out, std::string& why) {
    out = LevelLayout{};
    PixelFormatInfo info;
    if (!lookupPixelFormat(code, info)) {
        why = "unknown pixel-format code " + std::to_string(code) + " (spec Sec11.1)";
        return false;
    }
    uint32_t faceHeight = height;
    if (cube) {
        if (height % 6 != 0) {
            why = "cube record height " + std::to_string(height) + " is not a multiple of 6 (spec Sec10.5)";
            return false;
        }
        faceHeight = height / 6;
        out.faceCount = 6;
    }

    const uint32_t edge = info.blockEdge;
    uint32_t w = width;
    uint32_t h = faceHeight;
    uint64_t offset = 0;
    out.levels.reserve(levelCount);
    for (uint32_t k = 0; k < levelCount; ++k) {
        LevelInfo lv;
        lv.width = w;
        lv.height = h;
        lv.rowBytes = sourceRowBytes(code, w);
        lv.rowCount = (h + edge - 1) / edge;
        lv.bytes = static_cast<uint64_t>(h) * lv.rowBytes / edge;
        lv.offset = offset;
        out.levels.push_back(lv);
        offset += lv.bytes;
        // Spec Sec10.5: each dimension halves only while the halved value
        // stays >= the block edge. This clamp - not a stop rule - is why DXT
        // chains "terminate at 4x4".
        if ((w >> 1) >= edge) w >>= 1;
        if ((h >> 1) >= edge) h >>= 1;
    }
    out.faceBytes = offset;
    out.totalBytes = offset * out.faceCount;
    return true;
}

void expandR5G6B5ToRgba8(const uint8_t* src, size_t pixelCount, uint8_t* dstRgba) {
    for (size_t i = 0; i < pixelCount; ++i) {
        uint32_t v = static_cast<uint32_t>(src[i * 2]) | (static_cast<uint32_t>(src[i * 2 + 1]) << 8);
        uint32_t r5 = (v >> 11) & 0x1F;
        uint32_t g6 = (v >> 5) & 0x3F;
        uint32_t b5 = v & 0x1F;
        dstRgba[i * 4 + 0] = static_cast<uint8_t>((r5 * 255 + 15) / 31);
        dstRgba[i * 4 + 1] = static_cast<uint8_t>((g6 * 255 + 31) / 63);
        dstRgba[i * 4 + 2] = static_cast<uint8_t>((b5 * 255 + 15) / 31);
        dstRgba[i * 4 + 3] = 255;
    }
}

TexturePair TexturePair::parse(ByteView cpegBytes) {
    if (cpegBytes.size() < kHeaderSize) {
        throw FormatError("content too small to contain the .cpeg_pc 24-byte header (spec Sec10.2)");
    }
    uint32_t magic = cpegBytes.readU32LE(0x00);
    if (magic != kMagic) {
        throw FormatError("bad magic number: expected 0x564B4547 / \"GEKV\" (spec Sec10.2)");
    }

    TexturePair t;
    t.version_ = cpegBytes.readU16LE(0x04);
    t.platformIndex_ = cpegBytes.readU16LE(0x06);
    if (t.version_ != kSupportedVersion) {
        throw FormatError("unsupported version " + std::to_string(t.version_) +
                          " at +0x04: the game's validator accepts only 13 (spec Sec10.2)");
    }
    t.ownSize_ = cpegBytes.readU32LE(0x08);
    t.pairedSize_ = cpegBytes.readU32LE(0x0C);
    t.recordCountCopy_ = cpegBytes.readU16LE(0x10);
    t.registeredFlag_ = cpegBytes.readU16LE(0x12);
    // +0x14 is THE record count (spec Sec10.2): a signed 16-bit value that
    // every loop in the game's loader is bounded by. +0x10 is a second copy.
    int16_t recordCount = static_cast<int16_t>(cpegBytes.readU16LE(0x14));
    t.headerConstant_ = cpegBytes.readU16LE(0x16);

    if (t.ownSize_ != cpegBytes.size()) {
        throw FormatError(
            "header's own-size field (+0x08) does not match the actual content "
            "size - spec Sec10.2/Sec12.3 has them equal on 70,524/70,524 files, so a "
            "mismatch here means this isn't a well-formed .cpeg_pc");
    }
    if (recordCount < 0) {
        throw FormatError("negative record count at +0x14 (spec Sec10.2 makes it a signed 16-bit count)");
    }

    const size_t count = static_cast<size_t>(recordCount);
    const size_t recordArrayEnd = kHeaderSize + count * kRecordStride;
    if (recordArrayEnd > cpegBytes.size()) {
        throw FormatError("declared record count's record array runs past end of content");
    }

    t.records_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = kHeaderSize + i * kRecordStride;
        TextureRecord r;
        r.gpegOffset = cpegBytes.readU32LE(base + 0x00);
        // +0x04 is the high half of an 8-byte data-pointer slot the game
        // overwrites at load (spec Sec10.3, class RW) - never an input.
        r.width = cpegBytes.readU16LE(base + 0x08);
        r.height = cpegBytes.readU16LE(base + 0x0A);
        r.pixelFormatRaw = cpegBytes.readU32LE(base + 0x0C);
        r.pixelFormat = r.pixelFormatRaw & 0xFFFFu;
        r.gridColumns = cpegBytes.readU16LE(base + 0x10);
        r.gridRows = cpegBytes.readU16LE(base + 0x12);
        r.groupCount = cpegBytes.readU16LE(base + 0x14);
        r.flags = cpegBytes.readU16LE(base + 0x16);
        // +0x18..+0x1F: name-pointer slot (RW). +0x20: merge-only field
        // (MERGE, zero in every shipped file) - neither read.
        r.animationRate = cpegBytes.at(base + 0x22);
        r.levelCount = cpegBytes.at(base + 0x23);
        r.compressedSize = cpegBytes.readU32LE(base + 0x24);
        // +0x28..+0x47: runtime storage (RT), zero on disk - not read.
        t.records_.push_back(std::move(r));
    }

    // Name table (spec Sec10.4): one NUL-terminated name per record GROUP,
    // not per record. The group_count records starting at record i share
    // the one name, and the walk resumes at i + group_count. A group count
    // of 0 would never advance, so >= 1 is a hard precondition.
    size_t pos = recordArrayEnd;
    try {
        size_t i = 0;
        while (i < count) {
            const size_t group = t.records_[i].groupCount;
            if (group == 0) {
                throw FormatError("record " + std::to_string(i) +
                                  " has group count 0 (+0x14); spec Sec10.4 makes >= 1 a hard precondition");
            }
            if (i + group > count) {
                throw FormatError("record " + std::to_string(i) + "'s group of " + std::to_string(group) +
                                  " runs past the record array (not addressed by spec Sec10.4; rejected)");
            }
            size_t len = cpegBytes.cStringLength(pos);
            ByteView nameBytes = cpegBytes.subview(pos, len);
            std::string name(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
            for (size_t k = 0; k < group; ++k) t.records_[i + k].name = name;
            pos += len + 1; // + the NUL terminator
            i += group;
        }
    } catch (const std::out_of_range&) {
        throw FormatError("name table ran off the end of the content before the last name was terminated (spec Sec10.4)");
    }
    if (pos != cpegBytes.size()) {
        throw FormatError(
            "name table did not exactly fill the remaining content - spec Sec10.4 has it "
            "consuming to EOF with no padding and no trailer on 70,524/70,524 files");
    }

    return t;
}

void TexturePair::validateAgainstGpeg(ByteView gpegBytes) const {
    if (pairedSize_ != gpegBytes.size()) {
        throw FormatError(
            "paired-size header field (+0x0C) does not match the actual paired "
            ".gpeg_pc content size - spec Sec10.2 has these equal on 70,471/70,471 pairs");
    }
    for (size_t i = 0; i < records_.size(); ++i) {
        const TextureRecord& r = records_[i];
        if (!r.hasData()) continue;
        uint64_t end = static_cast<uint64_t>(r.gpegOffset) + r.compressedSize;
        if (end > gpegBytes.size()) {
            throw FormatError(
                "texture '" + r.name + "' (index " + std::to_string(i) +
                ")'s declared [gpegOffset, gpegOffset+size) range runs "
                "past the end of the paired .gpeg_pc content");
        }
    }
}

ByteView TexturePair::pixelBytes(size_t index, ByteView gpegBytes) const {
    if (index >= records_.size()) {
        throw FormatError("TexturePair::pixelBytes: index out of range");
    }
    const TextureRecord& r = records_[index];
    if (!r.hasData()) {
        throw FormatError("texture '" + r.name + "' has the no-data offset sentinel 0xFFFFFFFF (spec Sec10.2)");
    }
    uint64_t end = static_cast<uint64_t>(r.gpegOffset) + r.compressedSize;
    if (end > gpegBytes.size()) {
        throw FormatError(
            "texture '" + r.name +
            "'s declared [gpegOffset, gpegOffset+size) range runs "
            "past the end of the paired .gpeg_pc content");
    }
    return gpegBytes.subview(r.gpegOffset, r.compressedSize);
}

LevelLayout TexturePair::levelLayout(size_t index) const {
    if (index >= records_.size()) {
        throw FormatError("TexturePair::levelLayout: index out of range");
    }
    const TextureRecord& r = records_[index];
    LevelLayout layout;
    std::string why;
    if (!computeLevelLayout(r.pixelFormat, r.width, r.height, r.levelCount, r.isCubeMap(), layout, why)) {
        throw FormatError("texture '" + r.name + "': " + why);
    }
    return layout;
}

} // namespace sr3texture
