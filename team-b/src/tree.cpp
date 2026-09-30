#include "sr3tree/tree.h"

#include <cmath>
#include <cstring>

namespace sr3tree {

namespace {

float readF32LE(ByteView bytes, size_t offset) {
    uint32_t raw = bytes.readU32LE(offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "f32 must be 4 bytes");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::array<float, 3> readF32x3(ByteView bytes, size_t offset) {
    return {readF32LE(bytes, offset + 0), readF32LE(bytes, offset + 4), readF32LE(bytes, offset + 8)};
}

// Resolves a material-set-block name-table byte offset to its string
// (spec Sec6 item 2: offsets are relative to the table's own +1 base, past
// the leading NUL).
std::string nameAt(ByteView content, size_t tableStart, size_t tableEnd, uint32_t nameOffset) {
    size_t at = tableStart + nameOffset;
    if (at >= tableEnd) return std::string();
    size_t len = content.cStringLength(at);
    ByteView s = content.subview(at, len);
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

} // namespace

// ---- Wind-state object, spec Sec13.2 ----

WindParameters windParametersFromFloats(const std::array<float, kWindFileLoadedFloatCount>& k) {
    WindParameters p;
    p.strengthResponseTime = k[0x00];
    p.directionResponseTime = k[0x01];
    p.lengthLikeReciprocalToC2z = k[0x02];
    p.copiedToC2w = k[0x03];
    p.copiedToC6x = k[0x04];
    for (size_t g = 0; g < kWindBandCount; ++g) {
        WindOscillatorBand& b = p.bands[g];
        b.amplitudeAtLevel0 = k[5 + 4 * g];
        b.amplitudeAtLevel1 = k[6 + 4 * g];
        b.frequencyAtLevel0 = k[7 + 4 * g];
        b.frequencyAtLevel1 = k[8 + 4 * g];
        b.exponent = k[0x20 + g];
    }
    p.gustOnsetRate = k[0x15];
    p.scaleOfBand0ResponseToC5y = k[0x16];
    p.copiedToC5z = k[0x17];
    p.gustTargetAmplitudeLo = k[0x18];
    p.gustTargetAmplitudeHi = k[0x19];
    p.gustPlateauDurationLo = k[0x1A];
    p.gustPlateauDurationHi = k[0x1B];
    p.scaleOfLevelToC6y = k[0x1C];
    p.scaleOfLevelToC6z = k[0x1D];
    p.copiedToC4y = k[0x1E];
    p.copiedToC4z = k[0x1F];
    p.copiedToC3y = k[0x24];
    p.copiedToC3z = k[0x25];
    return p;
}

std::optional<WindSteadyState> windSteadyState(const WindParameters& p, float level) {
    if (!(level >= 0.0f && level <= 1.0f)) return std::nullopt; // also rejects NaN
    WindSteadyState out;
    for (size_t g = 0; g < kWindBandCount; ++g) {
        const WindOscillatorBand& b = p.bands[g];
        const double response = std::pow(static_cast<double>(level), static_cast<double>(b.exponent));
        out[g].response = static_cast<float>(response);
        out[g].amplitude = static_cast<float>(
            b.amplitudeAtLevel0 + (static_cast<double>(b.amplitudeAtLevel1) - b.amplitudeAtLevel0) * response);
        out[g].frequency = static_cast<float>(
            b.frequencyAtLevel0 + (static_cast<double>(b.frequencyAtLevel1) - b.frequencyAtLevel0) * response);
    }
    return out;
}

// ---- LOD distances, spec Sec13.5.2 ----

bool lodDistancesValid(const LodDistances& d) {
    return d[0] < d[1] && d[1] < d[2] && d[2] < d[3];
}

namespace {

std::optional<float> lodParameterFromSquared(const LodDistances& d, double q) {
    if (!lodDistancesValid(d)) return std::nullopt;
    for (float v : d) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    if (d[0] < 0.0f) return std::nullopt;
    if (!(q >= 0.0)) return std::nullopt; // NaN or negative
    const double a2 = static_cast<double>(d[0]) * d[0];
    const double b2 = static_cast<double>(d[1]) * d[1];
    const double c2 = static_cast<double>(d[2]) * d[2];
    const double d2 = static_cast<double>(d[3]) * d[3];
    double t;
    if (q < a2) {
        t = 1.0;
    } else if (q < b2) {
        t = 1.0 - (q - a2) / (b2 - a2);
    } else if (q < c2) {
        t = 0.0;
    } else if (q < d2) {
        t = -(q - c2) / (d2 - c2);
    } else {
        t = -1.0;
    }
    return static_cast<float>(t);
}

} // namespace

std::optional<float> lodParameterForSquaredDistance(const LodDistances& d, float q) {
    return lodParameterFromSquared(d, static_cast<double>(q));
}

std::optional<float> lodParameterForDistance(const LodDistances& d, float distance) {
    if (!(distance >= 0.0f)) return std::nullopt; // NaN or negative
    return lodParameterFromSquared(d, static_cast<double>(distance) * distance);
}

Tree Tree::parse(ByteView content, ByteView gContent) {
    Tree t;
    t.contentSize_ = content.size();

    // --- Shared material-reference block at +0x00 (spec Sec3). ---
    try {
        t.materialRefBlock_ = sr3geometry::MaterialBlock::parse(content);
    } catch (const sr3geometry::FormatError& ex) {
        throw FormatError(std::string("material-reference block failed to validate: ") + ex.what());
    }

    // --- Mandatory pad to 16 (spec-geometry-format.md Sec3.1.1: strictly
    // advances, even when already aligned). ---
    size_t cursor = padStrictlyTo16(t.materialRefBlock_.totalSize);

    // --- 'TREE' block, fixed 0x208 bytes (spec Sec4). ---
    if (cursor + kTreeBlockSize > content.size()) {
        throw FormatError("content too small to contain the fixed 0x208-byte 'TREE' block at " +
                          std::to_string(cursor));
    }
    t.treeBlock_.offset = cursor;
    if (content.readU32LE(cursor) != kTreeMagic) {
        throw FormatError("bad 'TREE' magic: expected 0x54524545 (spec Sec4, CONFIRMED) at the "
                          "mandatory-padded end of the material-reference block, offset " +
                          std::to_string(cursor));
    }
    t.treeBlock_.version = content.readU32LE(cursor + 0x04);
    if (t.treeBlock_.version <= kTreeMinVersionExclusive) {
        throw FormatError("'TREE' block version " + std::to_string(t.treeBlock_.version) +
                          " is not greater than " + std::to_string(kTreeMinVersionExclusive) +
                          " (spec Sec4, CONFIRMED - the loader requires it; shipped value is 0xCC "
                          "in 11/11)");
    }
    t.treeBlock_.collisionCapsuleCount = content.readU32LE(cursor + kTreeCapsuleCountOffset);
    // Wind-state object (spec Sec4 / Sec13.2): float index k at block offset
    // 0x38 + 4k; k = 0x00..0x25 are file-loaded, k >= 0x26 (block +0xD0..) is
    // runtime state, zero on disk (an observation, not enforced here).
    for (size_t k = 0; k < kWindFileLoadedFloatCount; ++k) {
        t.treeBlock_.windFloats[k] = readF32LE(content, cursor + kTreeWindObjectOffset + 4 * k);
    }
    t.treeBlock_.wind = windParametersFromFloats(t.treeBlock_.windFloats);
    for (size_t at = kWindRuntimeRegionOffset; at + 4 <= kTreeBlockSize; at += 4) {
        if (content.readU32LE(cursor + at) != 0) ++t.treeBlock_.windRuntimeNonZeroDwords;
    }
    cursor = roundUp16(cursor + kTreeBlockSize);

    // --- Geometry sub-structure, fixed 0x170 bytes (spec Sec5). ---
    if (cursor + kGeometrySize > content.size()) {
        throw FormatError("content too small to contain the fixed 0x170-byte geometry "
                          "sub-structure at " + std::to_string(cursor));
    }
    t.geometry_.offset = cursor;
    t.geometry_.boundsMin = readF32x3(content, cursor + kGeoBoundsMinOffset);
    t.geometry_.boundsMax = readF32x3(content, cursor + kGeoBoundsMaxOffset);
    t.geometry_.lodSlotCount = content.readU32LE(cursor + kGeoLodSlotCountOffset);
    t.geometry_.materialCount = content.readU32LE(cursor + kGeoMaterialCountOffset);
    t.geometry_.sixteenByteRecordCount = content.readU32LE(cursor + kGeoSixteenByteCountOffset);
    t.geometry_.openScaleLike = readF32LE(content, cursor + kGeoOpenScaleOffset);
    t.geometry_.openHeightLike = readF32LE(content, cursor + kGeoOpenHeightOffset);
    t.geometry_.openMinYLike = readF32LE(content, cursor + kGeoOpenMinYOffset);
    for (size_t i = 0; i < t.geometry_.lodDistances.size(); ++i) {
        t.geometry_.lodDistances[i] = readF32LE(content, cursor + kGeoLodDistancesOffset + 4 * i);
    }
    for (size_t i = 0; i < t.geometry_.lodDerivedOnDisk.size(); ++i) {
        t.geometry_.lodDerivedOnDisk[i] = readF32LE(content, cursor + kGeoLodDerivedOffset + 4 * i);
    }
    if (t.geometry_.lodSlotCount != kLodSlotCount) {
        throw FormatError("geometry sub-structure's LOD slot count is " +
                          std::to_string(t.geometry_.lodSlotCount) + ", expected exactly " +
                          std::to_string(kLodSlotCount) + " (spec Sec5, CONFIRMED 11/11)");
    }
    cursor = roundUp8(cursor + kGeometrySize);

    // --- Material-set block, FUN_00e40210 (spec Sec6). ---
    const size_t H = cursor;
    if (H + kMatSetHeaderSize > content.size()) {
        throw FormatError("content too small to contain the material-set block's 0x20-byte header "
                          "at " + std::to_string(H));
    }
    t.materialCount_ = content.readU32LE(H + kMatSetCountOffset);
    uint32_t nameTableLength = content.readU32LE(H + kMatSetNameTableLengthOffset);
    if (t.materialCount_ != t.geometry_.materialCount) {
        throw FormatError("material-set block's own record count (" +
                          std::to_string(t.materialCount_) +
                          ") disagrees with the geometry sub-structure's +0x40 material count (" +
                          std::to_string(t.geometry_.materialCount) +
                          ") - spec Sec5 has these equal in 11/11");
    }

    // Names: header end, padded to 16, plus ONE byte (leading-NUL
    // convention - spec Sec6 item 2, same rule this project's other
    // readers of this shared block already apply).
    size_t nameTableStart = roundUp16(H + kMatSetHeaderSize) + 1;
    if (nameTableStart > content.size() || nameTableStart + nameTableLength > content.size()) {
        throw FormatError("material-set block's name table runs past end of content");
    }
    size_t nameTableEnd = nameTableStart + nameTableLength;
    {
        size_t at = nameTableStart;
        while (at < nameTableEnd) {
            size_t len = content.cStringLength(at);
            if (len > 0) {
                ByteView s = content.subview(at, len);
                t.materialSetNames_.emplace_back(reinterpret_cast<const char*>(s.data()), s.size());
            }
            at += len + 1;
        }
    }

    // count x 8 pointer slots, zero on disk (spec Sec6 item 3).
    cursor = roundUp8(nameTableEnd);
    size_t slotsEnd = cursor + static_cast<size_t>(t.materialCount_) * kMatSetSlotStride;
    if (slotsEnd > content.size()) {
        throw FormatError("material-set block's pointer-slot array runs past end of content");
    }
    cursor = slotsEnd;

    // count inline material definitions (spec Sec6 item 4).
    t.materials_.reserve(t.materialCount_);
    for (uint32_t i = 0; i < t.materialCount_; ++i) {
        if (cursor + 4 > content.size()) {
            throw FormatError("material record " + std::to_string(i) +
                              "'s size prefix runs past end of content");
        }
        Material m;
        m.offset = cursor;
        m.declaredSize = content.readU32LE(cursor);
        size_t recordEnd = cursor + m.declaredSize;
        if (m.declaredSize == 0 || recordEnd > content.size()) {
            throw FormatError("material record " + std::to_string(i) + " declares size " +
                              std::to_string(m.declaredSize) +
                              ", which is zero or runs past end of content");
        }

        size_t hdr = roundUp8(cursor + 4);
        if (hdr + kMatRecordHeaderSize > content.size()) {
            throw FormatError("material record " + std::to_string(i) +
                              "'s 0x30-byte header runs past end of content");
        }
        m.shaderHash = content.readU32LE(hdr + kMatRecordHashOffset);
        m.variantHash = content.readU32LE(hdr + kMatRecordVariantOffset);
        m.flags = content.readU32LE(hdr + kMatRecordFlagsOffset);
        uint16_t bindingCount = content.readU16LE(hdr + kMatRecordTexCountOffset);
        uint8_t constantCount = content.at(hdr + kMatRecordConstCountOffset);
        uint8_t vec4Count = content.at(hdr + kMatRecordVec4CountOffset);

        size_t arrCursor = hdr + kMatRecordHeaderSize;
        // Texture bindings: A x 12 bytes, 4-aligned.
        arrCursor = (arrCursor + 3) / 4 * 4;
        if (arrCursor + static_cast<size_t>(bindingCount) * kTextureBindingSize > content.size()) {
            throw FormatError("material record " + std::to_string(i) +
                              "'s texture bindings run past end of content");
        }
        m.textureBindings.reserve(bindingCount);
        for (uint16_t b = 0; b < bindingCount; ++b) {
            size_t bAt = arrCursor + static_cast<size_t>(b) * kTextureBindingSize;
            TextureBinding tb;
            tb.nameOffset = content.readU32LE(bAt + 0);
            tb.slotHash = content.readU32LE(bAt + 4);
            tb.resolvedIndex = content.readU16LE(bAt + 8);
            tb.flags = content.readU16LE(bAt + 10);
            tb.name = nameAt(content, nameTableStart, nameTableEnd, tb.nameOffset);
            m.textureBindings.push_back(std::move(tb));
        }
        arrCursor += static_cast<size_t>(bindingCount) * kTextureBindingSize;

        // Constant-name hashes: B x 4 bytes, 4-aligned.
        arrCursor = (arrCursor + 3) / 4 * 4;
        if (arrCursor + static_cast<size_t>(constantCount) * 4 > content.size()) {
            throw FormatError("material record " + std::to_string(i) +
                              "'s constant-name hashes run past end of content");
        }
        m.constantNameHashes.reserve(constantCount);
        for (uint8_t c = 0; c < constantCount; ++c) {
            m.constantNameHashes.push_back(content.readU32LE(arrCursor + static_cast<size_t>(c) * 4));
        }
        arrCursor += static_cast<size_t>(constantCount) * 4;

        // Vec4 constant values: C x 16 bytes, 16-aligned.
        arrCursor = roundUp16(arrCursor);
        if (arrCursor + static_cast<size_t>(vec4Count) * 16 > content.size()) {
            throw FormatError("material record " + std::to_string(i) +
                              "'s vec4 constants run past end of content");
        }
        m.constantValues.reserve(vec4Count);
        for (uint8_t c = 0; c < vec4Count; ++c) {
            size_t vAt = arrCursor + static_cast<size_t>(c) * 16;
            std::array<float, 4> v{readF32LE(content, vAt + 0), readF32LE(content, vAt + 4),
                                   readF32LE(content, vAt + 8), readF32LE(content, vAt + 12)};
            m.constantValues.push_back(v);
        }

        t.materials_.push_back(std::move(m));
        cursor = recordEnd;
    }

    // --- Index list, FUN_00e401d0 (spec Sec7). ---
    cursor = roundUp8(cursor);
    if (cursor + kIndexListHeaderSize > content.size()) {
        throw FormatError("content too small to contain the index list's 0x10-byte header at " +
                          std::to_string(cursor));
    }
    uint32_t idxCount = content.readU32LE(cursor + kIndexListCountOffset);
    cursor += kIndexListHeaderSize;
    if (cursor + static_cast<size_t>(idxCount) * kIndexListElementStride > content.size()) {
        throw FormatError("index list's payload (" + std::to_string(idxCount) +
                          " x 4 bytes) runs past end of content");
    }
    t.materialIndexList_.reserve(idxCount);
    for (uint32_t i = 0; i < idxCount; ++i) {
        t.materialIndexList_.push_back(
            content.readU32LE(cursor + static_cast<size_t>(i) * kIndexListElementStride));
    }
    cursor += static_cast<size_t>(idxCount) * kIndexListElementStride;

    // --- LOD presence entries: 4 x 8 bytes (spec Sec3). ---
    cursor = roundUp8(cursor);
    size_t presenceBase = cursor;
    if (presenceBase + kLodSlotCount * kPresenceEntrySize > content.size()) {
        throw FormatError("content too small to contain the 4 x 8-byte LOD presence entries at " +
                          std::to_string(presenceBase));
    }
    for (size_t i = 0; i < kLodSlotCount; ++i) {
        size_t at = presenceBase + i * kPresenceEntrySize;
        t.lodSlots_[i].presenceEntryOffset = at;
        t.lodSlots_[i].present = content.readU32LE(at) != 0;
    }
    cursor = presenceBase + kLodSlotCount * kPresenceEntrySize;

    // --- Mesh sub-blocks: one per present slot, each advanced by its own
    // length field - no alignment between them (spec Sec3's table has none
    // listed). See the file header comment: headerDisplacement is computed
    // per block, not assumed 0x10, exactly like src/zone_geometry.cpp
    // already does for the identical shape. ---
    size_t gCursor = 0;
    for (size_t i = 0; i < kLodSlotCount; ++i) {
        if (!t.lodSlots_[i].present) continue;
        if (cursor + 4 > content.size()) {
            throw FormatError("LOD slot " + std::to_string(i) +
                              "'s Mesh sub-block runs past end of content before its header");
        }
        const size_t headerDisplacement = roundUp8(cursor + 16) - cursor;
        try {
            t.lodSlots_[i].mesh =
                sr3mesh::MeshBlock::parse(content, cursor, gContent, gCursor, headerDisplacement);
        } catch (const sr3mesh::FormatError& ex) {
            throw FormatError("LOD slot " + std::to_string(i) +
                              "'s Mesh sub-block failed to parse: " + ex.what());
        }
        if (!t.lodSlots_[i].mesh.bulkInGFile()) {
            throw FormatError("LOD slot " + std::to_string(i) +
                              "'s Mesh sub-block is not g-backed - spec Sec3 has flags bit 0 set "
                              "25/25 real slots");
        }
        cursor += t.lodSlots_[i].mesh.cLength();
        gCursor += t.lodSlots_[i].mesh.gLength();
    }

    // --- Runtime arrays: materialCount x 8, then 4 x 8, then
    // sixteenByteRecordCount x 16 - located, not decoded (spec Sec3). ---
    cursor = roundUp8(cursor);
    t.runtimeArraysOffset_ = cursor;
    t.runtimeArraysLength_ = static_cast<size_t>(t.materialCount_) * 8 + kLodSlotCount * 8 +
                             static_cast<size_t>(t.geometry_.sixteenByteRecordCount) * 16;
    if (cursor + t.runtimeArraysLength_ > content.size()) {
        throw FormatError("runtime arrays (materialCount x 8 + 4 x 8 + sixteenByteRecordCount x 16) "
                          "run past end of content");
    }
    cursor += t.runtimeArraysLength_;

    // --- Collision capsules: collisionCapsuleCount x 0x60 (spec Sec8). ---
    cursor = roundUp16(cursor);
    size_t capBase = cursor;
    size_t capsulesLength = static_cast<size_t>(t.treeBlock_.collisionCapsuleCount) * kCapsuleSize;
    if (capBase + capsulesLength > content.size()) {
        throw FormatError("collision capsules (" + std::to_string(t.treeBlock_.collisionCapsuleCount) +
                          " x 0x60 bytes) run past end of content");
    }
    t.collisionCapsules_.reserve(t.treeBlock_.collisionCapsuleCount);
    for (uint32_t i = 0; i < t.treeBlock_.collisionCapsuleCount; ++i) {
        size_t at = capBase + static_cast<size_t>(i) * kCapsuleSize;
        CollisionCapsule c;
        c.offset = at;
        for (size_t k = 0; k < kCapsuleHeaderBytes; ++k) {
            c.headerBytes[k] = content.at(at + k);
        }
        c.start = readF32x3(content, at + kCapsuleStartOffset);
        c.end = readF32x3(content, at + kCapsuleEndOffset);
        c.radius = readF32LE(content, at + kCapsuleRadiusOffset);
        t.collisionCapsules_.push_back(c);
    }
    cursor = capBase + capsulesLength;

    // --- The oracle: the whole computed walk must land EXACTLY on
    // end-of-file (spec Sec3, replay-verified 11/11). ---
    if (cursor != content.size()) {
        throw FormatError("computed walk ended at " + std::to_string(cursor) +
                          " but content is " + std::to_string(content.size()) +
                          " bytes (residual " +
                          std::to_string(static_cast<long long>(content.size()) -
                                         static_cast<long long>(cursor)) +
                          ") - spec Sec3 has this landing exactly in 11/11 real files");
    }

    return t;
}

} // namespace sr3tree
