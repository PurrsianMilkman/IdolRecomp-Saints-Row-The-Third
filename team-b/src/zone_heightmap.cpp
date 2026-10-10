#include "sr3zone/zone_heightmap.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace sr3zone {

HeightmapRecord Heightmap::parseAt(ByteView cznContent, size_t payloadOffset, HeightmapContainerKind containerKind) {
    if (payloadOffset > cznContent.size() || payloadOffset + kHeightmapMiniHeaderSize > cznContent.size()) {
        throw FormatError("Heightmap (0x2244) record: 16-byte mini-header does not fit at offset " +
                           std::to_string(payloadOffset) + " (spec-zone-data-format.md Sec13.5)");
    }

    HeightmapRecord rec;
    rec.payloadOffset = payloadOffset;
    rec.originX = static_cast<int32_t>(cznContent.readU32LE(payloadOffset + 0x00));
    rec.originZ = static_cast<int32_t>(cznContent.readU32LE(payloadOffset + 0x04));
    rec.gridW = cznContent.readU16LE(payloadOffset + 0x08);
    rec.unusedAt0x0a = cznContent.readU16LE(payloadOffset + 0x0a);
    rec.gridH = cznContent.readU16LE(payloadOffset + 0x0c);
    rec.unusedAt0x0e = cznContent.readU16LE(payloadOffset + 0x0e);
    rec.containerKind = containerKind;

    const size_t sampleCount = static_cast<size_t>(rec.gridW) * static_cast<size_t>(rec.gridH);
    const size_t samplesStart = payloadOffset + kHeightmapMiniHeaderSize;
    const size_t samplesBytes = sampleCount * kHeightmapSampleStride;
    if (samplesStart + samplesBytes > cznContent.size()) {
        throw FormatError("Heightmap (0x2244) record: declared " + std::to_string(rec.gridW) + "x" +
                           std::to_string(rec.gridH) + " height-sample array does not fit in the buffer");
    }

    rec.heights.resize(sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) {
        rec.heights[i] = static_cast<int16_t>(cznContent.readU16LE(samplesStart + i * kHeightmapSampleStride));
    }

    return rec;
}

std::vector<HeightmapRecord> Heightmap::locate(ByteView cznContent, HeightmapContainerKind containerKind) {
    std::vector<HeightmapRecord> out;
    if (cznContent.size() < 12) return out;

    const uint32_t wantTag = 0x80000000u | kHeightmapSectionTag;
    for (size_t pos = 0; pos + 12 <= cznContent.size(); pos += 4) {
        uint32_t tag;
        try {
            tag = cznContent.readU32LE(pos);
        } catch (const std::out_of_range&) {
            break;
        }
        if (tag != wantTag) continue;

        const size_t payloadOffset = pos + 12;  // past the 12-byte {tag,length,extra} chain header
        try {
            out.push_back(parseAt(cznContent, payloadOffset, containerKind));
        } catch (const FormatError&) {
            continue;
        }
    }
    return out;
}

HeightmapCell ResolveHeightmapCell(const HeightmapRecord& rec, double worldX, double worldZ) {
    HeightmapCell cell;
    const double spacing = rec.spacing();
    if (!(spacing > 0.0)) return cell;  // defensive; never true for either CONFIRMED spacing value

    // CONFIRMED formula (Sec13.5, the real grid-cell resolver 0x00b412c0) -
    // a literal `(int)` cast of a double division, i.e. C++ truncation
    // toward zero, reproduced exactly as spec's own pseudocode states it
    // (NOT reinterpreted as floor() - see this function's own header
    // comment).
    const double rawCol = (worldX - static_cast<double>(rec.originX)) / spacing;
    const double rawRow = (worldZ - static_cast<double>(rec.originZ)) / spacing;
    const int32_t col = static_cast<int32_t>(rawCol);
    const int32_t row = static_cast<int32_t>(rawRow);

    // CHOSEN safety addition (see header comment): reject negative first,
    // since this reader's own array index type is unsigned.
    if (col < 0 || row < 0) return cell;
    // CONFIRMED bound, boundary-inclusive: col==gridW / row==gridH is
    // accepted; only col>gridW / row>gridH fails.
    if (col > static_cast<int32_t>(rec.gridW) || row > static_cast<int32_t>(rec.gridH)) return cell;

    cell.valid = true;
    cell.col = col;
    cell.row = row;
    return cell;
}

bool SampleHeightmapHeight(const HeightmapRecord& rec, double worldX, double worldZ, float& outHeight) {
    if (rec.gridW == 0 || rec.gridH == 0) return false;
    const HeightmapCell cell = ResolveHeightmapCell(rec, worldX, worldZ);
    if (!cell.valid) return false;

    // CHOSEN boundary clamp (see header comment on SampleHeightmapHeight()):
    // the confirmed resolver accepts col==gridW/row==gridH, one past this
    // array's own last zero-based index - clamp rather than read past it.
    const int32_t maxCol = static_cast<int32_t>(rec.gridW) - 1;
    const int32_t maxRow = static_cast<int32_t>(rec.gridH) - 1;
    const size_t col = static_cast<size_t>(std::min(cell.col, maxCol));
    const size_t row = static_cast<size_t>(std::min(cell.row, maxRow));
    const size_t index = row * static_cast<size_t>(rec.gridW) + col;
    if (index >= rec.heights.size()) return false;  // defensive; cannot happen given the checks above

    // CHOSEN direct read (see header comment): the plain stored value, in
    // metres, no further transform.
    outHeight = static_cast<float>(rec.heights[index]);
    return true;
}

}  // namespace sr3zone
