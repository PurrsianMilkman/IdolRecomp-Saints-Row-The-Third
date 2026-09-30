// Reader for the .cpeg_pc / .gpeg_pc paired texture container format
// (spec-texture-format.md). .cpeg_pc holds a 24-byte header, one 72-byte
// record per texture, and a trailing name table (spec Sec10.2-10.4);
// .gpeg_pc is its paired file - just the raw, back-to-back pixel data for
// every record, each region starting on a 16-byte boundary, with NO header
// of its own (spec Sec1, Sec10.5). .cvbm_pc/.gvbm_pc is the identical format
// under a different extension (spec Sec7) - see content_validation.h's
// filename matcher, which accepts both.
//
// REWRITTEN against spec-texture-format.md Sec10-Sec12 (the code-derived
// layout, validated by the spec's authors against 70,524 c-files). The
// previous revision of this file was built from the black-box Sec2-Sec6 and
// carried three defects the newer spec names explicitly:
//   * the record's size field (+0x24) was read as 16 bits; it is 32 bits
//     (23,749 shipped records exceed 65,535 bytes) - Sec3/Sec10.3;
//   * the name table was read as one name per record; it is one name per
//     record GROUP (group size = record +0x14) - Sec10.4;
//   * pixel-format code 403 was left "unresolved"; it is R5G6B5 - Sec11.
//
// Every field below is read strictly BY OFFSET from the tables in Sec10.2
// and Sec10.3. Fields the spec classes RW (overwritten at load) or RT
// (runtime storage, zero on disk) are neither read nor exposed.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3texture/errors.h"
#include "vpp/byte_view.h"

namespace sr3texture {

using vpp::ByteView;

constexpr uint32_t kMagic = 0x564B4547u;        // "GEKV" on disk, spec Sec10.2
constexpr size_t kHeaderSize = 0x18;            // record array starts here, spec Sec10.2
constexpr size_t kRecordStride = 0x48;          // 72 bytes/record, spec Sec10.3
constexpr uint16_t kSupportedVersion = 13;      // the only value the game's own validator accepts, spec Sec10.2
constexpr uint32_t kGSideAlignment = 16;        // PC g-side region alignment, spec Sec10.5
constexpr uint32_t kNoDataOffset = 0xFFFFFFFFu; // record +0x00 sentinel meaning "no data", spec Sec10.2 (never shipped)

// Record flag bits (record +0x16, spec Sec10.7). Only the bits the spec
// says the code reads are named.
constexpr uint16_t kFlagCubeMap = 0x0008;
constexpr uint16_t kFlagFlipbookGrid = 0x0100;

// Pixel-format codes (record +0x0C, low 16 bits; spec Sec11.1).
constexpr uint32_t kFormatDxt1 = 400;
constexpr uint32_t kFormatDxt3 = 401;
constexpr uint32_t kFormatDxt5 = 402;
constexpr uint32_t kFormatR5G6B5 = 403;
constexpr uint32_t kFormatA1R5G5B5 = 404;
constexpr uint32_t kFormatA4R4G4B4 = 405;
constexpr uint32_t kFormatR8G8B8 = 406;
constexpr uint32_t kFormatA8R8G8B8 = 407;
constexpr uint32_t kFormatV8U8 = 408;
constexpr uint32_t kFormatCxV8U8 = 409;
constexpr uint32_t kFormatA8 = 410;
constexpr uint32_t kFormatDxt5Alt = 701; // second code for DXT5, accepted by the game, never shipped

// What spec Sec11.1 says about one pixel-format code.
struct PixelFormatInfo {
    uint32_t code = 0;
    uint32_t blockEdge = 1;      // 4 for the DXT family, 1 otherwise (spec Sec10.5 / Sec11.1)
    const char* d3d9Name = "";   // the Direct3D 9 format the game requests for this code
    bool shipped = false;        // occurs in the spec's shipped population (Sec11.1 "Records" column > 0)
};

// Looks `code` up in the spec's Sec11.1 table. Returns false for a code the
// game's upload path does not accept (any code outside 400-410 and 701).
bool lookupPixelFormat(uint32_t code, PixelFormatInfo& out);

// Source row bytes for width `w` (spec Sec11.1 "Source row bytes" column,
// taken literally, including the DXT1 `(w >> 1) * 4` form). For the DXT
// codes this is the byte length of one ROW OF BLOCKS (4 pixel rows); for
// every other code it is one pixel row. Returns 0 for an unknown code.
uint32_t sourceRowBytes(uint32_t code, uint32_t w);

// One level of a record's mip chain (spec Sec10.5).
struct LevelInfo {
    uint32_t width = 0;
    uint32_t height = 0;    // for a cube record this is the PER-FACE height
    uint32_t rowBytes = 0;  // sourceRowBytes(code, width)
    uint32_t rowCount = 0;  // ceil(height / blockEdge)
    uint64_t bytes = 0;     // height * rowBytes / blockEdge (integer), the spec's "per-level source advance"
    uint64_t offset = 0;    // byte offset of this level from the start of ITS FACE's data (0 for the first level)
};

struct LevelLayout {
    std::vector<LevelInfo> levels; // ONE face's chain (the only chain for a non-cube record)
    uint32_t faceCount = 1;        // 6 for a cube record, else 1
    uint64_t faceBytes = 0;        // sum of levels[].bytes
    uint64_t totalBytes = 0;       // faceCount * faceBytes; must equal the record's size field (spec Sec10.5, Sec12.3)
};

// Computes the level chain for a record of `width` x `height` pixels in
// format `code` with `levelCount` levels, by the arithmetic of spec Sec10.5:
// after each level the width halves only while (width >> 1) >= blockEdge
// and the height halves only while (height >> 1) >= blockEdge. A cube
// record (spec Sec10.5 "Cube records") divides `height` by 6 for the
// per-face height and lays out all levels of face 0, then face 1, ...
// Returns false with `why` set for an unknown format code, or a cube record
// whose height is not a multiple of 6.
bool computeLevelLayout(uint32_t code, uint32_t width, uint32_t height, uint32_t levelCount,
                        bool cube, LevelLayout& out, std::string& why);

// Expands `pixelCount` little-endian R5G6B5 words (spec Sec11.2: R = bits
// 15-11, G = 10-5, B = 4-0) into `pixelCount * 4` RGBA8 bytes (alpha 255).
// Channels are widened by exact UNORM rescale (v * 255 / max, rounded), the
// conversion Direct3D itself applies to a 5/6-bit UNORM channel.
void expandR5G6B5ToRgba8(const uint8_t* src, size_t pixelCount, uint8_t* dstRgba);

// One texture's metadata (spec Sec10.3, 72-byte on-disk record) plus its
// name from the trailing name table (spec Sec10.4).
struct TextureRecord {
    // +0x00: byte offset of this record's data region within the paired
    // .gpeg_pc (spec Sec10.3/Sec10.5). kNoDataOffset means "no data".
    uint32_t gpegOffset = 0;
    uint16_t width = 0;   // +0x08 (a cube's edge length)
    uint16_t height = 0;  // +0x0A (a cube's is 6 x edge: six faces stacked)
    // +0x0C: the pixel-format code. The game reads only the low 16 bits
    // (spec Sec10.3), so `pixelFormat` is that low half and
    // `pixelFormatRaw` the whole dword as stored.
    uint32_t pixelFormat = 0;
    uint32_t pixelFormatRaw = 0;
    uint16_t gridColumns = 0;  // +0x10 flipbook columns; meaningful only when flag 0x100 is set
    uint16_t gridRows = 0;     // +0x12 flipbook rows; same rule
    uint16_t groupCount = 0;   // +0x14 number of consecutive records sharing one name
    uint16_t flags = 0;        // +0x16 (spec Sec10.7)
    uint8_t animationRate = 0; // +0x22 (a single byte; inert when groupCount == 1)
    uint8_t levelCount = 0;    // +0x23 number of mip levels stored - the explicit count, not "until the size runs out"
    // +0x24, a 32-BIT value (the old reader took 16 bits): total byte size
    // of this record's data region including all levels, before the 16-byte
    // alignment pad. Kept under the name earlier callers use.
    uint32_t compressedSize = 0;
    std::string name; // from the name table, spec Sec10.4

    bool isCubeMap() const { return (flags & kFlagCubeMap) != 0; }
    bool hasFlipbookGrid() const { return (flags & kFlagFlipbookGrid) != 0; }
    bool hasData() const { return gpegOffset != kNoDataOffset; }
};

class TexturePair {
public:
    // Parses `cpegBytes` (a .cpeg_pc's or .cvbm_pc's full content). Throws
    // FormatError if any of these hard invariants (spec Sec10.2, Sec10.4,
    // Sec12.1) fails: the magic; the version being 13; the header's
    // own-size field (+0x08) equalling the content size; the record count
    // being non-negative and the record array fitting; a record's group
    // count being >= 1 and not running past the record array; the name
    // table being terminated and ending exactly at the end of the content
    // (no padding, no trailer).
    static TexturePair parse(ByteView cpegBytes);

    uint32_t version() const { return version_; }               // +0x04 (u16), always 13 once parse() returns
    uint32_t platformIndex() const { return platformIndex_; }   // +0x06 (u16), 0 = PC
    uint32_t ownSize() const { return ownSize_; }                // +0x08
    uint32_t pairedSize() const { return pairedSize_; }          // +0x0C, the paired .gpeg_pc's total size
    uint32_t recordCountCopy() const { return recordCountCopy_; } // +0x10 (u16), the second copy of the count
    uint32_t registeredFlag() const { return registeredFlag_; }  // +0x12 (u16), a runtime flag, zero on disk
    uint32_t headerConstant() const { return headerConstant_; }  // +0x16 (u16), 16 on every shipped file, use unknown
    const std::vector<TextureRecord>& records() const { return records_; }

    // Cross-checks `gpegBytes` (the paired .gpeg_pc's full content): throws
    // FormatError if its size doesn't match the `pairedSize` header field,
    // or if any record that has data has [gpegOffset, gpegOffset +
    // compressedSize) running past the end of `gpegBytes`.
    void validateAgainstGpeg(ByteView gpegBytes) const;

    // Returns the record's data region ([gpegOffset, gpegOffset +
    // compressedSize)) sliced from the caller's own `gpegBytes` buffer (not
    // owned/cached by this class). Throws FormatError for an out-of-range
    // index, a record with no data, or a range past the end of `gpegBytes`.
    ByteView pixelBytes(size_t index, ByteView gpegBytes) const;

    // The level chain of record `index` (spec Sec10.5) computed from its
    // width/height/format/level count/cube flag. Throws FormatError if the
    // record's format code is unknown or its cube shape is impossible.
    LevelLayout levelLayout(size_t index) const;

private:
    uint32_t version_ = 0;
    uint32_t platformIndex_ = 0;
    uint32_t ownSize_ = 0;
    uint32_t pairedSize_ = 0;
    uint32_t recordCountCopy_ = 0;
    uint32_t registeredFlag_ = 0;
    uint32_t headerConstant_ = 0;
    std::vector<TextureRecord> records_;
};

} // namespace sr3texture
