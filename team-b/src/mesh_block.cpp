#include "sr3mesh/mesh_block.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace sr3mesh {

namespace {

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

float readF32LE(ByteView bytes, size_t offset) {
    uint32_t raw = bytes.readU32LE(offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "f32 must be 4 bytes");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

// UBYTE4N: each byte mapped to [-1,1] as (b/255)*2-1 (spec §6.2). Only
// the low three components form the unit vector; the fourth is returned
// separately by the caller since its meaning differs between the normal
// (open) and the tangent (a handedness sign).
std::array<float, 3> decodeUbyte4nXyz(ByteView bytes, size_t at) {
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3; ++i) {
        out[i] = (static_cast<float>(bytes.at(at + i)) / 255.0f) * 2.0f - 1.0f;
    }
    return out;
}

// IEEE 754 binary16 ("half float") -> binary32, for layout codes 11/12/13's
// Position field (spec-vertex-format.md §12.12 - HIGH CONFIDENCE, empirical,
// NOT CONFIRMED; see mesh_block.h's LayoutInfo::hasPositionFloat16 comment
// for the full caveat). Nothing in this codebase decoded float16 before this
// (checked: readF32LE()/decodeUbyte4nXyz() just above were the only numeric
// decoders here) - this is the standard, textbook sign/exponent/mantissa
// bit-manipulation algorithm, handling zero, subnormals, normals and
// infinity/NaN per the IEEE 754 spec exactly like any other correct half->
// float conversion (e.g. the widely-used public-domain routines this
// project's own zlib/Lua third-party code sits alongside) - nothing
// project-specific or invented about the bit math itself.
float halfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exponent = (h & 0x7C00u) >> 10;
    uint32_t mantissa = h & 0x03FFu;
    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign; // signed zero
        } else {
            // Subnormal half -> normalize into an equivalent normal float.
            int shift = 0;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x03FFu; // drop the implicit leading bit just found
            const uint32_t biasedExp = 113u - static_cast<uint32_t>(shift); // 127-15+1-shift
            bits = sign | (biasedExp << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13); // Inf (mantissa==0) or NaN
    } else {
        const uint32_t biasedExp = exponent + (127u - 15u);
        bits = sign | (biasedExp << 23) | (mantissa << 13);
    }
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits), "f32 must be 4 bytes");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Three consecutive half-floats, little-endian (spec §12.12: FLOAT16x3 at
// offset `+0`, codes 11/12/13's Position field).
std::array<float, 3> decodeFloat16Xyz(ByteView bytes, size_t at) {
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3; ++i) {
        out[i] = halfToFloat(bytes.readU16LE(at + i * 2));
    }
    return out;
}

} // namespace

LayoutInfo layoutInfoFor(uint8_t layoutCode) {
    LayoutInfo info;
    switch (layoutCode) {
        case 0:   info = {true, 16, true,  false, false, false}; break; // position + normal
        case 1:   info = {true, 24, true,  false, true,  false}; break; // + skinning
        case 2:   info = {true, 20, true,  true,  false, false}; break; // + tangent
        case 3:   info = {true, 28, true,  true,  true,  false}; break; // + tangent + skinning
        case 4:   info = {true, 12, false, false, false, false}; break; // position only
        case 100: info = {true, 20, true,  false, false, true};  break; // vehicles: rigid part index
        case 101: info = {true, 24, true,  true,  false, true};  break; // vehicles + tangent
        // Trees (spec-vertex-format.md §12.9/§12.10/§12.12). Codes 11, 12
        // and 13 share one identical field layout - three separate layout
        // codes, all tree-exclusive, same measured 32-byte base, same
        // normal/tangent byte offsets (§12.10.2/§12.10.5's own summary
        // table) - so one shared block covers all three rather than
        // repeating it per code. Named field assignment used deliberately,
        // exactly like code 24 below, not a positional bool list -
        // HANDOFF.md's own "rule 17" lesson (a fixed structural slot
        // misread by POSITION, not name) is specifically about this
        // mistake.
        //
        // Byte map within the 32-byte base (36 total once the single real
        // texcoord set is added, stride law: base + 4*texcoordCount):
        //   +0  ( 6 bytes) Position, FLOAT16x3       - HIGH CONFIDENCE
        //                                               (see hasPositionFloat16
        //                                               and this struct's own
        //                                               comment for the caveats)
        //   +6  ( 2 bytes) UNKNOWN                    - reserved, unread
        //   +8  ( 4 bytes) Normal,  UBYTE4N xyz+w     - CONFIRMED (§12.9.5/§12.10.2)
        //   +12 ( 4 bytes) Tangent, UBYTE4N xyz+sign  - CONFIRMED (§12.9.5/§12.10.2)
        //   +16 ( 6 bytes) Position, FLOAT16x3 AGAIN  - reserved, unread
        //                                               (the ~70%-byte-identical
        //                                               duplicate of +0, §12.9.7 -
        //                                               NEVER read into a 2nd
        //                                               Vertex field, same
        //                                               treatment as code 24's
        //                                               own +16 slot)
        //   +22 (10 bytes) UNKNOWN                    - reserved, unread
        //   +32 ( 4 bytes) Texcoord set 0             - existing generic logic
        //
        // 2 + 6 + 10 = 18 reserved/unread bytes, split into the two gaps
        // LayoutInfo can express: 2 BEFORE Normal
        // (reservedBytesAfterPosition) and 16 AFTER Tangent (reservedBytes,
        // covering both the +16 duplicate and the +22 unknown run in one
        // step, since neither is read into any field either way). This
        // gap arithmetic was independently re-derived from the real
        // 36-byte stride and the real +8/+12 field positions, not assumed
        // from this task's own brief - it matches exactly.
        //
        // Position is HIGH CONFIDENCE, not CONFIRMED (spec §12.12): real,
        // empirical, full-population, non-denormal, correctly-scaled
        // against each tree's own bbox (28,423/28,457 code 11 across
        // 22/22 channels; 609/609 code 12 across 4/4; 1,172/1,172 code 13
        // across 4/4) - but no CPU/GPU consumer of this byte range has
        // been found, and +0/+16 are ~70% byte-identical cross-channel
        // duplicates with an open, unresolved hedge that this pair could
        // be runtime-populated/stale rather than authored per-vertex data.
        // `known=true` is set anyway (matching this reader's own existing
        // convention for code 24, whose own +16 slot is similarly OPEN)
        // because Position/Normal/Tangent - the only three fields this
        // reader decodes for these codes - are the best real information
        // available, stated at their real, honestly-different confidence
        // tiers rather than withheld entirely.
        case 11:
        case 12:
        case 13: {
            LayoutInfo tree;
            tree.known = true;
            tree.base = 32;
            tree.hasNormal = true;
            tree.hasTangent = true;
            tree.hasSkinning = false;
            tree.hasRigidPart = false;
            tree.hasPositionFloat16 = true;
            tree.reservedBytesAfterPosition = 2;  // +6..+8, unknown
            tree.reservedBytes = 16;              // +16..+32: dup FLOAT16x3 (6) + unknown (10)
            info = tree;
            break;
        }
        case 24: {
            // Zone geometry (.czn_pc/.gzn_pc). Position (+0, FLOAT3) and
            // normal (+12, UBYTE4N xyz) are independently CONFIRMED at full
            // population (spec-vertex-format.md Sec12.3: 581,647/581,647
            // finite float3 at +0; 581,647/581,647 unit-length at +12) -
            // normal sits at the SAME +12 offset code 2 already uses (it is
            // codes 11/12/13 that put normal at the different, unrelated
            // +8 - do not conflate the two). `known=true` reflects that
            // those two fields, and ONLY those two, are decodable.
            //
            // The next 4 bytes (+16) were tested as the code-2-style
            // tangent and REFUTED (Sec12.3: pooled unit-vector rate only
            // 8.0% across 448 real channels, far below the ~100% a real
            // tangent shows everywhere else in this format) - its real
            // content is OPEN, not merely unconfirmed, and this reader
            // deliberately never reads it into any Vertex field.
            // `reservedBytes=4` reserves exactly that slot in
            // decodeChannel()'s cursor walk so texcoords still land at the
            // real +20 rather than 4 bytes early, at +16, WITHOUT ever
            // interpreting what +16 holds. Named fields used deliberately
            // here, not a positional bool list - see HANDOFF.md "rule 17"
            // (a fixed structural slot misread by position, not name, is
            // this project's own recorded lesson).
            LayoutInfo zone;
            zone.known = true;
            zone.base = 20;
            zone.hasNormal = true;
            zone.hasTangent = false;
            zone.hasSkinning = false;
            zone.hasRigidPart = false;
            zone.reservedBytes = 4; // the open, unread +16..+20 slot
            info = zone;
            break;
        }
        default:  info = {false, 0,  false, false, false, false}; break;
    }
    return info;
}

MeshBlock MeshBlock::parse(ByteView cContent, size_t meshOffset, ByteView gContent,
                           size_t gSegmentOffset, size_t headerDisplacement) {
    MeshBlock block;

    if (meshOffset + headerDisplacement + kHeaderSize > cContent.size()) {
        throw FormatError("content too small to contain the Mesh sub-block header at offset " +
                          std::to_string(meshOffset));
    }

    uint32_t version = cContent.readU32LE(meshOffset + kVersionOffset);
    if (version != kMeshVersion) {
        throw FormatError("Mesh sub-block version is " + std::to_string(version) +
                          ", expected " + std::to_string(kMeshVersion) +
                          " (spec-vertex-format.md Sec2, CONFIRMED)");
    }

    block.checkValue_ = cContent.readU32LE(meshOffset + kCheckValueOffset);
    block.cLength_ = cContent.readU32LE(meshOffset + kCLengthOffset);
    block.gLength_ = cContent.readU32LE(meshOffset + kGLengthOffset);

    const size_t header = meshOffset + headerDisplacement;
    block.flags_ = cContent.at(header + kFlagsOffset);
    uint32_t channelCount = cContent.readU32LE(header + kChannelCountOffset);
    block.indexCount_ = cContent.readU32LE(header + kIndexCountOffset);
    block.indexElementSize_ = cContent.at(header + kIndexSizeOffset);

    if ((block.flags_ & kFlagMultiStream) != 0) {
        throw FormatError("Mesh sub-block sets flags bit 2 (the alternative multi-stream "
                          "representation), which spec-vertex-format.md Sec2 does not cover and "
                          "this reader does not attempt");
    }

    // --- Channel records, immediately after the 0x70 header (spec §3). ---
    const size_t recordsAt = header + kHeaderSize;
    if (recordsAt + static_cast<size_t>(channelCount) * kChannelRecordSize > cContent.size()) {
        throw FormatError("channel records (" + std::to_string(channelCount) + " x 24 bytes at " +
                          std::to_string(recordsAt) + ") run past the end of the content");
    }
    block.channels_.reserve(channelCount);
    for (uint32_t i = 0; i < channelCount; ++i) {
        size_t at = recordsAt + static_cast<size_t>(i) * kChannelRecordSize;
        Channel channel;
        channel.elementCount = cContent.readU32LE(at + 0x00);
        channel.sizeA = cContent.at(at + 0x04);
        channel.layoutCode = cContent.at(at + 0x05);
        channel.texcoordCount = cContent.at(at + 0x06);
        channel.sizeB = cContent.at(at + 0x07);

        // Cross-check the declared stride against the spec's law. The
        // declared value is authoritative either way; this only makes a
        // disagreement visible.
        LayoutInfo info = layoutInfoFor(channel.layoutCode);
        channel.predictedStride =
            info.base + 4u * static_cast<size_t>(channel.texcoordCount);
        channel.strideMatchesLaw =
            (info.base != 0 || channel.layoutCode == 4) &&
            channel.predictedStride == channel.stride();

        block.channels_.push_back(channel);
    }

    // --- Bone palette (HANDOFF Sec9.62/9.63). The u16-count-gated array at
    // header +0x38 that spec-geometry-format.md Sec4.1.1 confirms the
    // loader walks: `count` u8 rig-bone indices, immediately after the
    // channel records. Only read for g-backed blocks - an inline-mode
    // block's bulk segment starts at this same cursor, and no inline-mode
    // carrier has a skinning layout to index with. Non-fatal on a
    // truncated declaration (see the header comment on bonePalette() for
    // why): the palette is left empty and the declared count is kept so a
    // consumer can see the disagreement.
    //
    // SCOPE, tightened 2026-09-13 after a cross-team check: this meaning
    // ("bone palette") is CONFIRMED only for skinned character meshes
    // (318/318 in the validated population, all with a skinning layout).
    // The same two header slots are a DIFFERENT field on at least one
    // other carrier - Team A confirmed +0x48/+0x50 on VEHICLE meshes is
    // an unrelated u16 group-pointer table their own draw-range groups
    // point into, not a palette-set descriptor list. This parser is
    // shared by six carrier formats, so populating bonePalette_ from
    // whatever bytes happen to sit at +0x38 on a non-skinned carrier
    // would silently hand a consumer semantically wrong data. Gated on
    // hasSkinning below - additive only: this does not change cursor
    // math for anything downstream (the bulk-segment base a few lines on
    // is computed from channelCount alone, not from whether the palette
    // was read), so it cannot regress any of the five other carriers. ---
    block.bonePaletteDeclaredCount_ = cContent.readU16LE(header + kBonePaletteCountOffset);
    bool hasAnySkinningChannel = false;
    for (const auto& ch : block.channels_) {
        if (layoutInfoFor(ch.layoutCode).hasSkinning) { hasAnySkinningChannel = true; break; }
    }
    if (block.bulkInGFile() && block.bonePaletteDeclaredCount_ > 0 && hasAnySkinningChannel) {
        const size_t paletteAt = recordsAt + static_cast<size_t>(channelCount) * kChannelRecordSize;
        const size_t paletteBytes = block.bonePaletteDeclaredCount_;
        if (paletteAt + paletteBytes <= cContent.size()) {
            block.bonePalette_.resize(paletteBytes);
            for (size_t i = 0; i < paletteBytes; ++i) {
                block.bonePalette_[i] = cContent.at(paletteAt + i);
            }
            // The set descriptors: [u8 count][u8 start] x (header +0x48),
            // right after the palette bytes (unaligned - brad's sits at
            // 0x5c0, angel's at 0x5a6, brute's at 0x63d).
            block.bonePaletteSetCountDeclared_ = cContent.readU16LE(header + kBonePaletteSetCountOffset);
            const size_t setsAt = paletteAt + paletteBytes;
            const size_t setBytes = static_cast<size_t>(block.bonePaletteSetCountDeclared_) * 2;
            if (block.bonePaletteSetCountDeclared_ > 0 && setsAt + setBytes <= cContent.size()) {
                block.bonePaletteSets_.resize(block.bonePaletteSetCountDeclared_);
                for (size_t i = 0; i < block.bonePaletteSets_.size(); ++i) {
                    block.bonePaletteSets_[i].count = cContent.at(setsAt + i * 2);
                    block.bonePaletteSets_[i].start = cContent.at(setsAt + i * 2 + 1);
                }
            }
        }
    }

    // --- The bulk segment. Bit 0 set: the g-file. Clear: inline in the
    // c-file at the same cursor (spec §9 step 5). ---
    ByteView segment = block.bulkInGFile() ? gContent : cContent;
    size_t segmentBase = 0;
    if (!block.bulkInGFile()) {
        // Inline mode: the segment follows the channel records.
        segmentBase = recordsAt + static_cast<size_t>(channelCount) * kChannelRecordSize;
    } else {
        // g-backed: the caller says where inside `gContent` this block's
        // segment starts. 0 for every single-segment carrier; non-zero
        // only for a chained `.gzn_pc` (spec-zone-data-format.md Sec10.4).
        segmentBase = gSegmentOffset;
    }

    // The 16-byte grid the segment's internal padding is measured against.
    //
    // g-backed: the ABSOLUTE grid of the g-file, so a segment that starts
    // at 4 (mod 16) carries 4 fewer pad bytes than the same segment would
    // at a 16-aligned start - which is exactly why the declared g-length
    // is only reproducible from the true start offset. With the default
    // gSegmentOffset == 0 this is identical to the segment-relative grid
    // used before this parameter existed, so no existing carrier moves.
    //
    // Inline: left segment-relative deliberately. Nothing has measured an
    // absolute grid for the inline case (spec Sec10.4's evidence is
    // entirely g-side) and changing it would be a guess, so the anchor is
    // pinned to 0 there rather than to segmentBase.
    const size_t alignAnchor = block.bulkInGFile() ? segmentBase : 0;
    auto alignSegment16 = [alignAnchor](size_t cursorIn) {
        return alignUp(alignAnchor + cursorIn, 16) - alignAnchor;
    };

    if (segmentBase + block.gLength_ > segment.size()) {
        throw FormatError("declared segment length " + std::to_string(block.gLength_) +
                          " runs past the end of the " +
                          (block.bulkInGFile() ? std::string("g-file") : std::string("c-file")) +
                          " (" + std::to_string(segment.size()) + " bytes)");
    }

    // --- The walk (spec §4). Every step is checked; spec §9 step 8 is
    // explicit that a mismatch means stop, not guess. ---
    size_t cursor = 0;
    uint32_t leadingCheck = segment.readU32LE(segmentBase + cursor);
    if (leadingCheck != block.checkValue_) {
        std::ostringstream foundHex, expectedHex;
        foundHex << std::hex << leadingCheck;
        expectedHex << std::hex << block.checkValue_;
        throw FormatError("segment does not open with the block's check value: found 0x" +
                          foundHex.str() + ", expected 0x" + expectedHex.str());
    }
    cursor += 4;
    cursor = alignSegment16(cursor);

    // Index buffer.
    size_t indexBytes = static_cast<size_t>(block.indexCount_) * block.indexElementSize_;
    if (cursor + indexBytes > block.gLength_) {
        throw FormatError("index buffer overruns the declared segment length");
    }
    if (block.indexElementSize_ == 2) {
        block.indices_.reserve(block.indexCount_);
        for (uint32_t i = 0; i < block.indexCount_; ++i) {
            block.indices_.push_back(
                segment.readU16LE(segmentBase + cursor + static_cast<size_t>(i) * 2));
        }
    }
    cursor += indexBytes;

    // Channel data, each 16-byte aligned, in record order.
    for (Channel& channel : block.channels_) {
        cursor = alignSegment16(cursor);
        channel.dataBytes = static_cast<size_t>(channel.elementCount) * channel.stride();
        if (cursor + channel.dataBytes > block.gLength_) {
            throw FormatError("channel data overruns the declared segment length");
        }
        channel.dataOffset = segmentBase + cursor;
        cursor += channel.dataBytes;
    }

    cursor = alignUp(cursor, 4);
    if (cursor + 4 != block.gLength_) {
        throw FormatError(
            "segment walk ended at " + std::to_string(cursor + 4) +
            " but the block declares a length of " + std::to_string(block.gLength_) +
            " (residual " +
            std::to_string(static_cast<long long>(block.gLength_) -
                           static_cast<long long>(cursor + 4)) +
            ") - spec-vertex-format.md Sec9 step 8: stop rather than guess");
    }
    uint32_t trailingCheck = segment.readU32LE(segmentBase + cursor);
    if (trailingCheck != block.checkValue_) {
        throw FormatError("segment does not end on the check-value bookend");
    }

    // --- Draw groups and ranges (spec §8.2). The spec places them "after
    // the channel array (and after the flag-bit-1 arrays when present)",
    // 16-byte aligned, without stating the size of those arrays - so the
    // start is SEARCHED over aligned candidates and accepted only if the
    // spec's own invariants hold. Those invariants are strong: the ranges
    // must start at index 0, be contiguous, together cover the index buffer
    // exactly, and satisfy minVertex <= maxVertex < vertex count. A wrong
    // offset does not pass all four by accident.
    //
    // The vertex bound is checked against the LARGEST channel, not channel
    // 0. The spec flags this explicitly as a trap that produced 22 false
    // failures out of 1,831 when checked against channel 0 alone. ---
    {
        uint32_t groupCount = cContent.readU32LE(header + 0x04);
        uint32_t largestChannelVertices = 0;
        for (const Channel& channel : block.channels_) {
            if (channel.elementCount > largestChannelVertices) {
                largestChannelVertices = channel.elementCount;
            }
        }

        const size_t searchStart = recordsAt + static_cast<size_t>(channelCount) * kChannelRecordSize;
        const size_t searchLimit = std::min(cContent.size(), searchStart + 4096);
        for (size_t candidate = alignUp(searchStart, 16); candidate + 4 <= searchLimit;
             candidate += 16) {
            if (groupCount == 0) break;
            std::vector<uint32_t> rangeCounts;
            size_t totalRanges = 0;
            bool shapeOk = true;
            for (uint32_t g = 0; g < groupCount; ++g) {
                size_t at = candidate + static_cast<size_t>(g) * 0x30;
                if (at + 0x30 > cContent.size()) { shapeOk = false; break; }
                uint32_t n = cContent.readU32LE(at);
                if (n == 0 || n > 4096) { shapeOk = false; break; }
                rangeCounts.push_back(n);
                totalRanges += n;
            }
            if (!shapeOk || totalRanges == 0) continue;

            const size_t rangesAt = candidate + static_cast<size_t>(groupCount) * 0x30;
            if (rangesAt + totalRanges * 20 > cContent.size()) continue;

            std::vector<std::vector<DrawRange>> groups;
            size_t rangeCursor = rangesAt;
            bool valid = true;
            // Contiguity runs across ALL ranges in group order, not
            // restarting per group: the spec's invariant is that the ranges
            // together partition the whole index buffer from 0. Resetting
            // this per group rejects every multi-group mesh.
            uint32_t expectedStart = 0;
            for (uint32_t g = 0; g < groupCount && valid; ++g) {
                std::vector<DrawRange> group;
                for (uint32_t r = 0; r < rangeCounts[g]; ++r) {
                    DrawRange range;
                    const uint32_t packed = cContent.readU32LE(rangeCursor + 0x00);
                    range.materialId = packed & 0xFFFFu;   // see DrawRange
                    range.submeshIndex = packed >> 16;
                    range.startIndex = cContent.readU32LE(rangeCursor + 0x04);
                    range.indexCount = cContent.readU32LE(rangeCursor + 0x08);
                    range.minVertex = cContent.readU32LE(rangeCursor + 0x0C);
                    range.maxVertex = cContent.readU32LE(rangeCursor + 0x10);
                    rangeCursor += 20;

                    if (range.startIndex != expectedStart) { valid = false; break; }
                    if (range.indexCount == 0) { valid = false; break; }
                    if (range.minVertex > range.maxVertex) { valid = false; break; }
                    if (range.maxVertex >= largestChannelVertices) { valid = false; break; }
                    expectedStart = range.startIndex + range.indexCount;
                    group.push_back(range);
                }
                if (!valid) break;
                if (expectedStart > block.indexCount_) { valid = false; break; }
                groups.push_back(std::move(group));
            }
            // The ranges must together cover the index buffer exactly -
            // the spec's strongest structural invariant here.
            if (valid && expectedStart != block.indexCount_) valid = false;
            if (valid && groups.size() == groupCount) {
                block.drawGroups_ = std::move(groups);

                // --- Per-draw-range BONE PALETTE SET selector (HANDOFF
                // Sec9.63.10). Immediately after the 20-byte range records,
                // with no alignment and no gap, `totalRanges` 8-byte
                // records; the u32 at each +0x00 names the palette set that
                // range's blend indices address (+0x04 is zero in every
                // shipped record and is not decoded). `rangeCursor` is
                // already sitting exactly at the array's start.
                //
                // Read only when this block actually has a bone palette to
                // index - the same hasSkinning gate the palette itself uses
                // (see above), so no non-character carrier can be handed
                // semantically wrong data. Measured: the structure is
                // present on 549/549 character meshes (the check value
                // repeats in the 4 bytes right after the array, and
                // meshOffset+cLength lands there) and on 0/372 vehicles.
                //
                // Non-fatal in every failure mode - left empty rather than
                // throwing, because this parser is shared by six carriers
                // whose populations were validated before this field was
                // understood. Populated only if EVERY entry is a valid set
                // index, so a consumer never has to filter it.
                if (!block.bonePaletteSets_.empty()) {
                    const size_t tailAt = rangeCursor; // == rangesAt + totalRanges*20
                    const size_t tailBytes = totalRanges * 8;
                    if (tailAt + tailBytes <= cContent.size()) {
                        std::vector<uint32_t> sets(totalRanges);
                        bool allValid = true;
                        for (size_t r = 0; r < totalRanges; ++r) {
                            sets[r] = cContent.readU32LE(tailAt + r * 8);
                            if (sets[r] >= block.bonePaletteSets_.size()) { allValid = false; break; }
                        }
                        if (allValid) block.drawRangePaletteSets_ = std::move(sets);
                    }
                }
                break;
            }
        }
    }

    // Keep a private copy of the segment so decodeChannel() does not
    // depend on the caller's buffer outliving this object.
    block.segment_.assign(segment.data() + segmentBase,
                          segment.data() + segmentBase + block.gLength_);
    for (Channel& channel : block.channels_) {
        channel.dataOffset -= segmentBase;
    }

    return block;
}

std::vector<uint32_t> MeshBlock::triangleListForRange(const DrawRange& range) const {
    std::vector<uint32_t> out;
    const size_t start = range.startIndex;
    const size_t count = range.indexCount;
    if (count < 3 || start + count > indices_.size()) return out;
    out.reserve((count - 2) * 3);

    for (size_t k = 0; k + 2 < count; ++k) {
        uint16_t a = indices_[start + k];
        uint16_t b = indices_[start + k + 1];
        uint16_t c = indices_[start + k + 2];
        if (a == b || b == c || a == c) continue; // stitch degenerate
        if ((k & 1) == 0) {
            out.push_back(a); out.push_back(b); out.push_back(c);
        } else {
            out.push_back(a); out.push_back(c); out.push_back(b);
        }
    }
    return out;
}

std::vector<uint32_t> MeshBlock::triangleListForGroup(size_t groupIndex) const {
    std::vector<uint32_t> out;
    if (groupIndex >= drawGroups_.size()) return out;
    // Each range is expanded independently and the strip RESTARTED between
    // them. Concatenating across a boundary is exactly what fabricates the
    // bridge triangles (spec §8.2).
    for (const DrawRange& range : drawGroups_[groupIndex]) {
        std::vector<uint32_t> part = triangleListForRange(range);
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

std::vector<uint32_t> MeshBlock::triangleListIndices() const {
    std::vector<uint32_t> out;
    if (indices_.size() < 3) return out;
    out.reserve((indices_.size() - 2) * 3);

    for (size_t k = 0; k + 2 < indices_.size(); ++k) {
        uint16_t a = indices_[k];
        uint16_t b = indices_[k + 1];
        uint16_t c = indices_[k + 2];

        // Stitch triangles: separate strips are joined by repeating an
        // index, which produces a zero-area triangle. Any repeated index
        // in the window means this step is a join, not a face (spec §8.1).
        if (a == b || b == c || a == c) continue;

        // Strips alternate winding each step; flipping the odd ones keeps
        // the emitted list consistently wound.
        if ((k & 1) == 0) {
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        } else {
            out.push_back(a);
            out.push_back(c);
            out.push_back(b);
        }
    }
    return out;
}

bool MeshBlock::allStridesMatchLaw() const {
    for (const Channel& channel : channels_) {
        if (!channel.strideMatchesLaw) return false;
    }
    return true;
}

std::vector<Vertex> MeshBlock::decodeChannel(size_t index) const {
    if (index >= channels_.size()) {
        throw FormatError("channel index " + std::to_string(index) + " out of range (" +
                          std::to_string(channels_.size()) + " channels)");
    }
    const Channel& channel = channels_[index];
    LayoutInfo info = layoutInfoFor(channel.layoutCode);
    if (!info.known) {
        throw FormatError(
            "layout code " + std::to_string(channel.layoutCode) +
            " has a confirmed stride but its element fields were never probed "
            "(spec-vertex-format.md Sec5 / open item 1) - refusing to decode at guessed offsets");
    }

    ByteView segment(segment_.data(), segment_.size());

    // Field order is fixed (spec §6): position, normal, [tangent],
    // [weights, indices] or [rigid part], then texcoords.
    //
    // Position is 12 bytes (plain FLOAT32x3) for every layout code except
    // 11/12/13, where it is 6 bytes (FLOAT16x3, spec §12.12 - see
    // mesh_block.h's LayoutInfo::hasPositionFloat16 comment for the
    // HIGH-CONFIDENCE-not-CONFIRMED caveat). `reservedBytesAfterPosition`
    // is those same three codes' own +6..+8 unknown gap - 0 (a no-op) for
    // every other layout code.
    size_t cursor = info.hasPositionFloat16 ? 6 : 12;
    cursor += info.reservedBytesAfterPosition;
    const size_t normalAt = info.hasNormal ? cursor : 0;
    if (info.hasNormal) cursor += 4;
    const size_t tangentAt = info.hasTangent ? cursor : 0;
    if (info.hasTangent) cursor += 4;
    size_t weightsAt = 0, indicesAt = 0, rigidAt = 0;
    if (info.hasSkinning) {
        weightsAt = cursor;
        indicesAt = cursor + 4;
        cursor += 8;
    } else if (info.hasRigidPart) {
        rigidAt = cursor;
        cursor += 4;
    }
    // Reserved/unread byte range - code 24's +16..+20 slot, OR codes
    // 11/12/13's +16..+32 slot (the duplicate FLOAT16x3 position plus 10
    // further unknown bytes; see layoutInfoFor()'s own case comments for
    // each). This is a real, stride-consuming structural gap, not a bug:
    // the cursor must step over it for texcoords to land at the right
    // offset, but its CONTENT is never decoded into any Vertex field.
    // `reservedBytes` is 0 for every OTHER layout code (0/1/2/3/4/100/101),
    // so this line is a provable no-op for all of them.
    cursor += info.reservedBytes;
    const size_t texcoordsAt = cursor;

    std::vector<Vertex> out;
    out.reserve(channel.elementCount);
    for (uint32_t v = 0; v < channel.elementCount; ++v) {
        const size_t base = channel.dataOffset + static_cast<size_t>(v) * channel.stride();
        Vertex vertex;
        if (info.hasPositionFloat16) {
            // Codes 11/12/13 only - FLOAT16x3 at +0 (spec §12.12, HIGH
            // CONFIDENCE not CONFIRMED; see mesh_block.h's
            // LayoutInfo::hasPositionFloat16 comment for the full caveat).
            vertex.position = decodeFloat16Xyz(segment, base + 0);
        } else {
            vertex.position = {readF32LE(segment, base + 0), readF32LE(segment, base + 4),
                               readF32LE(segment, base + 8)};
        }
        if (info.hasNormal) {
            vertex.normal = decodeUbyte4nXyz(segment, base + normalAt);
            vertex.normalW = segment.at(base + normalAt + 3);
        }
        if (info.hasTangent) {
            vertex.tangent = decodeUbyte4nXyz(segment, base + tangentAt);
            vertex.tangentW = segment.at(base + tangentAt + 3);
            // Exactly two values ship, 0 and 255 (spec §6.2). Anything
            // else would mean the field is not what the spec measured, so
            // split on the midpoint rather than testing == 255 and
            // silently treating an unexpected value as one of the two.
            vertex.tangentHandedness = vertex.tangentW >= 128 ? -1.0f : 1.0f;
        }
        if (info.hasSkinning) {
            for (size_t i = 0; i < 4; ++i) {
                vertex.blendWeights[i] =
                    static_cast<float>(segment.at(base + weightsAt + i)) / 255.0f;
                vertex.blendIndices[i] = segment.at(base + indicesAt + i);
            }
        } else if (info.hasRigidPart) {
            vertex.rigidPartIndex = segment.at(base + rigidAt);
        }

        // SIGNED 16-bit fixed point, 1024 = 1.0 (spec §6.5). The signed
        // reinterpretation is the point: 2.79% of raw values exceed 32767
        // and are small negatives, not coordinates near 64.
        vertex.texcoords.reserve(channel.texcoordCount);
        vertex.texcoordsRaw.reserve(channel.texcoordCount);
        for (uint8_t t = 0; t < channel.texcoordCount; ++t) {
            size_t at = base + texcoordsAt + static_cast<size_t>(t) * 4;
            int16_t rawU = static_cast<int16_t>(segment.readU16LE(at));
            int16_t rawV = static_cast<int16_t>(segment.readU16LE(at + 2));
            vertex.texcoordsRaw.push_back({rawU, rawV});
            vertex.texcoords.push_back(
                {static_cast<float>(rawU) / kTexcoordScale,
                 static_cast<float>(rawV) / kTexcoordScale});
        }
        out.push_back(std::move(vertex));
    }
    return out;
}

} // namespace sr3mesh
