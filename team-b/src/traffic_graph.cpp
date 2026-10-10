#include "sr3zone/traffic_graph.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace sr3zone {

namespace {

// Resolves a raw self-relative-pointer field against `payloadOffset`, per
// this file's own header-comment convention ("-1 -> null, else
// payloadOffset + storedValue"). Throws FormatError on a negative
// non-sentinel value (never a valid forward offset) or on a resolved
// offset past the end of `bufferSize` - both genuine structural
// violations, not OPEN fields.
size_t ResolveSelfRelativeOrThrow(size_t payloadOffset, int32_t storedValue, size_t bufferSize,
                                  const char* fieldDescription) {
    if (storedValue < 0) {
        throw FormatError(std::string("Traffic (0x2236) record: negative non-sentinel self-relative pointer in ") +
                           fieldDescription);
    }
    const size_t resolved = payloadOffset + static_cast<size_t>(storedValue);
    if (resolved > bufferSize) {
        throw FormatError(std::string("Traffic (0x2236) record: self-relative pointer in ") + fieldDescription +
                           " resolves past the end of the buffer");
    }
    return resolved;
}

float ReadF32LE(ByteView content, size_t offset) {
    ByteView four = content.subview(offset, 4);  // bounds-checked, throws std::out_of_range
    float v;
    std::memcpy(&v, four.data(), 4);
    return v;
}

int16_t ReadS16LE(ByteView content, size_t offset) { return static_cast<int16_t>(content.readU16LE(offset)); }

}  // namespace

TrafficPointPool::TrafficPointPool(ByteView content, TrafficSubArrayRef ref) : content_(content) {
    if (ref.present && ref.count > 0) {
        base_ = ref.resolvedOffset;
        count_ = static_cast<size_t>(ref.count);
        const size_t end = base_ + count_ * kPointPoolElementStride;
        if (end > content.size()) {
            throw FormatError(
                "Traffic (0x2236) record: point pool declares more elements than fit in the buffer "
                "(CHOSEN float3 stride - see traffic_graph.h's own file comment)");
        }
    }
}

TrafficPoint TrafficPointPool::at(size_t index) const {
    if (index >= count_) {
        throw std::out_of_range("TrafficPointPool::at: index out of range");
    }
    const size_t off = base_ + index * kPointPoolElementStride;
    TrafficPoint p;
    p.x = ReadF32LE(content_, off + 0);
    p.y = ReadF32LE(content_, off + 4);
    p.z = ReadF32LE(content_, off + 8);
    return p;
}

QuantizedSegmentPoints DecodeQuantizedSegmentPoints(const TrafficSegmentExtended& ext, const TrafficPointPool& pool,
                                                     double scale) {
    QuantizedSegmentPoints result;
    if (ext.anchorPointIndex >= pool.size()) return result;  // valid stays false - never throws
    const TrafficPoint anchor = pool.at(ext.anchorPointIndex);

    result.pointA.x = anchor.x + static_cast<float>(ext.quantizedOffsetA[0] * scale);
    result.pointA.y = anchor.y + static_cast<float>(ext.quantizedOffsetA[1] * scale);
    result.pointA.z = anchor.z + static_cast<float>(ext.quantizedOffsetA[2] * scale);

    result.pointB.x = anchor.x + static_cast<float>(ext.quantizedOffsetB[0] * scale);
    result.pointB.y = anchor.y + static_cast<float>(ext.quantizedOffsetB[1] * scale);
    result.pointB.z = anchor.z + static_cast<float>(ext.quantizedOffsetB[2] * scale);

    result.valid = true;
    return result;
}

SegmentPointRange SegmentCsrRange(const std::vector<TrafficSegmentExtended>& extendedSegments, size_t segmentIndex) {
    SegmentPointRange r;
    if (segmentIndex >= extendedSegments.size()) return r;  // {0,0}
    r.begin = (segmentIndex == 0) ? 0 : extendedSegments[segmentIndex - 1].cumulativePointCount;
    r.end = extendedSegments[segmentIndex].cumulativePointCount;
    if (r.end < r.begin) r.end = r.begin;  // malformed CSR - defensive, reported as empty, never throws
    return r;
}

std::vector<TrafficPoint> DecodeSegmentPointList(const std::vector<TrafficSegmentExtended>& extendedSegments,
                                                  size_t segmentIndex, const TrafficPointPool& pool) {
    std::vector<TrafficPoint> out;
    const SegmentPointRange range = SegmentCsrRange(extendedSegments, segmentIndex);
    out.reserve(range.end - range.begin);
    for (uint16_t i = range.begin; i < range.end; ++i) {
        if (i >= pool.size()) break;  // short list rather than a thrown error - see header comment
        out.push_back(pool.at(static_cast<size_t>(i)));
    }
    return out;
}

TrafficRecord TrafficGraph::parseAt(ByteView cznContent, size_t payloadOffset) {
    if (payloadOffset > cznContent.size() || payloadOffset + kTrafficHeaderSize > cznContent.size()) {
        throw FormatError("Traffic (0x2236) record: 0x70-byte header does not fit at offset " +
                           std::to_string(payloadOffset) + " (spec-zone-data-format.md Sec13.2)");
    }

    TrafficRecord rec;
    rec.payloadOffset = payloadOffset;

    for (size_t slot = 0; slot < kTrafficHeaderPairCount; ++slot) {
        const size_t pairOffset = payloadOffset + slot * 8;
        TrafficSubArrayRef ref;
        ref.count = static_cast<int32_t>(cznContent.readU32LE(pairOffset));
        ref.rawPointer = static_cast<int32_t>(cznContent.readU32LE(pairOffset + 4));
        ref.present = (ref.rawPointer != kSelfRelativeNullSentinel);
        if (ref.present) {
            ref.resolvedOffset = ResolveSelfRelativeOrThrow(payloadOffset, ref.rawPointer, cznContent.size(),
                                                             "a header sub-array pointer");
        }
        rec.rawPairs[slot] = ref;
    }

    const TrafficSubArrayRef segRef = rec.rawPairs[kSegmentArrayHeaderOffset / 8];
    const TrafficSubArrayRef extRef = rec.rawPairs[kExtendedSegmentArrayHeaderOffset / 8];
    const TrafficSubArrayRef pointRef = rec.rawPairs[kPointPoolHeaderOffset / 8];

    if (segRef.count != extRef.count) {
        throw FormatError(
            "Traffic (0x2236) record: segment array count (" + std::to_string(segRef.count) +
            ") != extended-segment array count (" + std::to_string(extRef.count) +
            ") - Sec13.2 confirms these two arrays are index-parallel over the same N");
    }

    const bool haveSegments = segRef.count > 0;
    if (haveSegments && (!segRef.present || !extRef.present)) {
        throw FormatError(
            "Traffic (0x2236) record: segment/extended-segment count is nonzero but the corresponding "
            "array pointer is the null sentinel");
    }

    const size_t n = haveSegments ? static_cast<size_t>(segRef.count) : 0;

    if (n > 0) {
        if (segRef.resolvedOffset + n * kSegmentRecordStride > cznContent.size()) {
            throw FormatError("Traffic (0x2236) record: segment array (count=" + std::to_string(n) +
                               ") does not fit in the buffer");
        }
        if (extRef.resolvedOffset + n * kExtendedSegmentRecordStride > cznContent.size()) {
            throw FormatError("Traffic (0x2236) record: extended-segment array (count=" + std::to_string(n) +
                               ") does not fit in the buffer");
        }
    }

    rec.segments.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t at = segRef.resolvedOffset + i * kSegmentRecordStride;
        TrafficSegmentEntry e;
        e.index = i;
        e.fieldAt0x00 = cznContent.readU32LE(at + 0x00);
        e.pointOffsetRaw = static_cast<int32_t>(cznContent.readU32LE(at + 0x04));
        e.hasPointOffset = (e.pointOffsetRaw != kSelfRelativeNullSentinel);
        if (e.hasPointOffset) {
            e.resolvedPointOffset =
                ResolveSelfRelativeOrThrow(payloadOffset, e.pointOffsetRaw, cznContent.size(),
                                            "a segment entry's +0x04 point-pool offset");
        }
        for (size_t b = 0; b < e.rawOpenBytes.size(); ++b) {
            e.rawOpenBytes[b] = cznContent.at(at + 0x08 + b);
        }
        rec.segments.push_back(e);
    }

    rec.extendedSegments.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t at = extRef.resolvedOffset + i * kExtendedSegmentRecordStride;
        TrafficSegmentExtended ext;
        ext.index = i;
        ext.anchorPointIndex = cznContent.readU16LE(at + 0x00);
        ext.rawFieldAt0x1c = cznContent.readU32LE(at + 0x1c);
        for (int k = 0; k < 3; ++k) ext.quantizedOffsetA[k] = ReadS16LE(cznContent, at + 0x24 + k * 2);
        for (int k = 0; k < 3; ++k) ext.quantizedOffsetB[k] = ReadS16LE(cznContent, at + 0x2a + k * 2);
        ext.cumulativePointCount = cznContent.readU16LE(at + 0x3e);
        rec.extendedSegments.push_back(ext);
    }

    rec.pointPool = TrafficPointPool(cznContent, pointRef);

    return rec;
}

std::vector<TrafficRecord> TrafficGraph::locate(ByteView cznContent) {
    std::vector<TrafficRecord> out;
    if (cznContent.size() < 12) return out;

    const uint32_t wantTag = 0x80000000u | kTrafficSectionTag;
    for (size_t pos = 0; pos + 12 <= cznContent.size(); pos += 4) {
        uint32_t tag;
        try {
            tag = cznContent.readU32LE(pos);
        } catch (const std::out_of_range&) {
            break;
        }
        if (tag != wantTag) continue;

        const size_t payloadOffset = pos + 12;  // past the 12-byte {tag,length,extra} chain header - see header comment
        try {
            out.push_back(parseAt(cznContent, payloadOffset));
        } catch (const FormatError&) {
            continue;
        }
    }
    return out;
}

}  // namespace sr3zone
