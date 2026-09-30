#include "sr3zone/zone_header.h"

#include <cstring>

namespace sr3zone {

namespace {

// The material block's MANDATORY trailing pad (spec-geometry-format.md
// Sec3.1.1): always advances at least 1 byte and at most 16, including a
// full extra 16 when `end` is already 16-aligned. Deliberately NOT a plain
// round-up-to-16 (that silently reads the pad's own zero bytes as the next
// block's magic on an already-aligned file) - restated here rather than
// imported from sr3geometry, since the rule belongs to the material block
// itself, not to any one of its several carriers.
size_t mandatoryPad16(size_t end) {
    return end + (16 - (end % 16));
}

// Little-endian s16/f32 readers over a plain byte buffer. Not part of
// vpp::ByteView's own API (it offers u16/u32 only), and these are only
// ever used on the already-bounds-checked SR3Z fixed header / a 14-byte
// ZoneHeaderRecord, so no separate bounds check is done here.
int16_t readS16LE(const uint8_t* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

float readF32LE(const uint8_t* p) {
    uint32_t bits = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                    (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

} // namespace

std::array<float, 3> RecordPosition(const ZoneHeaderRecord& record) {
    return {
        static_cast<float>(readS16LE(&record[0])) * (1.0f / 64.0f),
        static_cast<float>(readS16LE(&record[2])) * (1.0f / 64.0f),
        static_cast<float>(readS16LE(&record[4])) * (1.0f / 64.0f),
    };
}

uint16_t RecordNameIndex(const ZoneHeaderRecord& record) {
    return static_cast<uint16_t>(static_cast<uint16_t>(record[12]) | (static_cast<uint16_t>(record[13]) << 8));
}

int16_t RecordFieldSixRaw(const ZoneHeaderRecord& record) {
    return readS16LE(&record[6]);
}

float RecordFieldSixAsHypothesizedRadians(const ZoneHeaderRecord& record) {
    return static_cast<float>(RecordFieldSixRaw(record)) * (1.0f / 4096.0f); // 2^-12
}

ZoneContainerKind ContainerKindForZoneType(uint16_t zoneType) {
    // spec-world-streaming.md Sec10.7 bullet 1, quoted precisely:
    //   {1, 3, 8, 10, 11, 13} -> 0x1B Level Always Loaded
    //   {5, 6}                -> 0x21 mission
    //   7                     -> 0x1F Interior zone
    //   9                     -> 0x20 Large interior zone
    //   12                    -> 0x22 large mission
    //   anything else (incl. 2) -> 0x1D Zone
    switch (zoneType) {
        case 1: case 3: case 8: case 10: case 11: case 13:
            return ZoneContainerKind::LevelAlwaysLoaded;
        case 5: case 6:
            return ZoneContainerKind::Mission;
        case 7:
            return ZoneContainerKind::InteriorZone;
        case 9:
            return ZoneContainerKind::LargeInteriorZone;
        case 12:
            return ZoneContainerKind::LargeMission;
        default:
            return ZoneContainerKind::Zone;
    }
}

ZoneHeader ZoneHeader::parse(ByteView content) {
    ZoneHeader h;

    // --- 1. Shared material-reference block. Propagates its own
    // FormatError unchanged on failure - not re-wrapped, so the real cause
    // (bad magic, name-table length mismatch) is never hidden. ---
    h.materialBlock_ = sr3geometry::MaterialBlock::parse(content);

    // --- 2. Mandatory pad, then the SR3Z block. ---
    h.sr3zOffset_ = mandatoryPad16(h.materialBlock_.totalSize);
    if (h.sr3zOffset_ + kSR3ZFixedHeaderSize > content.size()) {
        throw FormatError("content too small to contain the SR3Z fixed header at offset " +
                          std::to_string(h.sr3zOffset_));
    }

    uint32_t magic = content.readU32LE(h.sr3zOffset_ + 0x00);
    if (magic != kSR3ZMagic) {
        throw FormatError("bad SR3Z magic at offset " + std::to_string(h.sr3zOffset_) +
                          ": expected 0x5A335253 (spec-ctorless-types.md Sec5)");
    }

    h.version_ = content.readU32LE(h.sr3zOffset_ + 0x04);
    if (h.version_ < kSR3ZVersionMin || h.version_ > kSR3ZVersionMax) {
        throw FormatError("SR3Z version " + std::to_string(h.version_) +
                          " outside the accepted range 27-29 (spec-ctorless-types.md Sec5, "
                          "the real loader's own rejection range)");
    }

    // +0x08..+0x0B: NOT read. spec-zone-data-format.md Sec4 documents +0x08
    // as where the LOADER stores its own in-memory material-block pointer
    // after construction - a runtime write, not a value shipped on disk.

    // +0x0C..+0x17: three f32, the world-space origin the record array's
    // position words are relative to (spec-world-streaming.md Sec10.7
    // bullet 2 - see the header comment). Always read; zero here on a
    // file with zero records is an expected, non-error value, not
    // enforced either way by parse() itself.
    h.headerOrigin_ = {
        readF32LE(content.data() + h.sr3zOffset_ + 0x0C),
        readF32LE(content.data() + h.sr3zOffset_ + 0x10),
        readF32LE(content.data() + h.sr3zOffset_ + 0x14),
    };

    h.runtimePointerSlotOnDisk_ = content.readU32LE(h.sr3zOffset_ + 0x18); // diagnostic only, see header.h
    h.recordCount_ = content.readU16LE(h.sr3zOffset_ + 0x1C);
    h.fieldAt0x1E_ = content.readU16LE(h.sr3zOffset_ + 0x1E);

    // --- 3. The record array: end of fixed header, align4 (a no-op - 0x40
    // is already 4-aligned), then recordCount() x 14 opaque bytes. Must
    // land EXACTLY on the end of `content` - spec-ctorless-types.md Sec5's
    // "replay is exact" invariant, 2,971/2,971, zero residual. ---
    size_t recordsStart = h.sr3zOffset_ + kSR3ZFixedHeaderSize;
    recordsStart = (recordsStart + 3) / 4 * 4; // align4, stated explicitly though a no-op here
    size_t recordsBytes = static_cast<size_t>(h.recordCount_) * kSR3ZRecordSize;

    if (recordsStart + recordsBytes != content.size()) {
        throw FormatError(
            "SR3Z record array does not land exactly on end of content: header ends at " +
            std::to_string(recordsStart) + ", " + std::to_string(h.recordCount_) +
            " x 14 = " + std::to_string(recordsBytes) + " bytes, content is " +
            std::to_string(content.size()) +
            " bytes (spec-ctorless-types.md Sec5: CONFIRMED exact in 2,971/2,971 shipped files)");
    }

    h.records_.reserve(h.recordCount_);
    for (uint16_t i = 0; i < h.recordCount_; ++i) {
        size_t at = recordsStart + static_cast<size_t>(i) * kSR3ZRecordSize;
        ZoneHeaderRecord rec{};
        for (size_t b = 0; b < kSR3ZRecordSize; ++b) rec[b] = content.at(at + b);
        h.records_.push_back(rec);
    }

    return h;
}

} // namespace sr3zone
