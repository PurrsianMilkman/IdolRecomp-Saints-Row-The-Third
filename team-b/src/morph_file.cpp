#include "sr3morph/morph_file.h"

#include <cstring>
#include <string>

namespace sr3morph {

namespace {

// CONDITIONAL alignment - a no-op when `value` is already aligned. spec
// Sec3 states this explicitly for this format, in contrast to the
// mandatory-minimum pad .ccmesh_pc uses at its material/geometry
// boundary. Getting this backwards breaks the exact-size replay.
constexpr size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

float readF32LE(ByteView bytes, size_t offset) {
    uint32_t raw = bytes.readU32LE(offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "f32 must be 4 bytes");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

} // namespace

MorphFile MorphFile::parse(ByteView bytes) {
    if (bytes.size() < kDirectoryOffset) {
        throw FormatError("content too small to contain the outer and Morph block headers (" +
                           std::to_string(bytes.size()) + " bytes, need at least " +
                           std::to_string(kDirectoryOffset) + ")");
    }

    if (bytes.readU32LE(0x00) != kOuterMagic) {
        throw FormatError("bad outer magic: expected 0x1337BEEF (spec Sec3.1, CONFIRMED)");
    }
    uint32_t outerVersion = bytes.readU32LE(0x04);
    if (outerVersion != kOuterVersion) {
        throw FormatError("unsupported outer version " + std::to_string(outerVersion) +
                           ": the format requires exactly " + std::to_string(kOuterVersion) +
                           " (spec Sec3.1, CONFIRMED - the loader requires it)");
    }
    // +0x08 (8 bytes) is a pair of runtime pointer slots, zero on disk.

    if (bytes.readU32LE(0x10) != kBlockMagic) {
        throw FormatError("bad \"Morph\" block magic: expected 0x0BADBEEF (spec Sec3.2, CONFIRMED)");
    }
    uint32_t blockVersion = bytes.readU32LE(0x14);
    if (blockVersion != kBlockVersion) {
        throw FormatError("unsupported Morph block version " + std::to_string(blockVersion) +
                           ": the format requires exactly " + std::to_string(kBlockVersion) +
                           " (spec Sec3.2, CONFIRMED - the loader requires it)");
    }

    MorphFile m;
    m.mode_ = bytes.readU32LE(0x18);
    if (m.mode_ > 2) {
        throw FormatError("invalid Morph block mode " + std::to_string(m.mode_) +
                           ": the loader accepts only 0, 1 or 2 (spec Sec3.2, CONFIRMED)");
    }
    if (m.mode_ != kMode1Inline) {
        // Real loader capability, but no shipped file uses it and the bulk
        // would live in a .gmorph_pc, of which the game ships exactly zero
        // (spec Sec6). Refusing beats implementing an unverifiable path.
        throw FormatError(
            "Morph block mode " + std::to_string(m.mode_) +
            " stores its bulk in a secondary .gmorph_pc buffer. That mode is real loader "
            "capability but is used by no shipped file (spec Sec6: 2,946 .cmorph_pc, 0 "
            ".gmorph_pc), so it is deliberately not implemented rather than written against "
            "data that cannot be verified");
    }

    uint32_t targetCount = bytes.readU32LE(0x1C);
    // +0x20 (8 bytes) is a runtime directory-pointer slot, zero on disk.

    // --- Directory: targetCount x 0x10 at +0x28 (Sec3.3) ---
    size_t pos = kDirectoryOffset;
    size_t directoryBytes = static_cast<size_t>(targetCount) * kDirectoryEntrySize;
    if (pos + directoryBytes > bytes.size()) {
        throw FormatError("target directory (" + std::to_string(targetCount) +
                           " entries) runs past end of content");
    }
    m.targets_.reserve(targetCount);
    uint64_t totalRecords = 0;
    for (uint32_t i = 0; i < targetCount; ++i) {
        size_t base = pos + static_cast<size_t>(i) * kDirectoryEntrySize;
        DirectoryEntry e;
        e.id = bytes.readU32LE(base + 0x00);
        e.recordCount = bytes.readU32LE(base + 0x04);
        // +0x08 runtime pointer slot and +0x0C zero - not read.
        totalRecords += e.recordCount;
        m.targets_.push_back(e);
    }
    pos += directoryBytes;
    // 8-byte alignment applies within the header/directory region (Sec3).
    // Every stride here is a multiple of 8, so this is always a no-op in
    // practice; applied anyway to follow the format rather than an
    // observation about it.
    pos = alignUp(pos, 8);

    // --- Descriptor records: immediately after the directory (Sec3.4) ---
    size_t descriptorBytes = static_cast<size_t>(totalRecords) * kDescriptorSize;
    if (pos + descriptorBytes > bytes.size()) {
        throw FormatError("descriptor records (" + std::to_string(totalRecords) +
                           ") run past end of content");
    }
    m.descriptors_.reserve(static_cast<size_t>(totalRecords));
    size_t descriptorBase = pos;
    size_t recordIndex = 0;
    for (size_t t = 0; t < m.targets_.size(); ++t) {
        for (uint32_t r = 0; r < m.targets_[t].recordCount; ++r, ++recordIndex) {
            size_t base = descriptorBase + recordIndex * kDescriptorSize;
            Descriptor d;
            d.targetIndex = t;
            // +0x00 is zero in every record inspected (OPEN) - not read.
            d.affectedVertexCount = bytes.readU16LE(base + 0x04);
            d.maxVertexIndex = bytes.readU16LE(base + 0x06);
            for (int k = 0; k < 3; ++k) {
                d.paramsA[k] = readF32LE(bytes, base + 0x08 + static_cast<size_t>(k) * 4);
                d.paramsB[k] = readF32LE(bytes, base + 0x14 + static_cast<size_t>(k) * 4);
            }
            // +0x20 is a runtime bulk-pointer slot, zero on disk.
            m.descriptors_.push_back(d);
        }
    }
    pos += descriptorBytes;

    // --- Bulk runs: for each descriptor in order, align 16, N x 12 bytes,
    // align 16 (Sec3.5) ---
    for (auto& d : m.descriptors_) {
        pos = alignUp(pos, 16);
        d.bulkOffset = pos;
        d.bulkByteLength = static_cast<size_t>(d.affectedVertexCount) * kElementSize;
        if (pos + d.bulkByteLength > bytes.size()) {
            throw FormatError("a descriptor's bulk run (" +
                               std::to_string(d.affectedVertexCount) +
                               " elements) runs past end of content");
        }
        pos += d.bulkByteLength;
        pos = alignUp(pos, 16);
    }

    // --- Trailer: align 4, then the 0x0BADBEEF bookend sentinel (Sec3.6) ---
    pos = alignUp(pos, 4);
    m.trailerOffset_ = pos;
    if (pos + 4 > bytes.size()) {
        throw FormatError("predicted trailer position runs past end of content");
    }
    if (bytes.readU32LE(pos) != kBlockMagic) {
        throw FormatError(
            "no 0x0BADBEEF sentinel at the predicted end of the structural walk - the "
            "layout model does not fit this file (spec Sec3.6/Sec4)");
    }

    // The exact-size replay (spec Sec4): the walk must land precisely on
    // the end of the file. This holds for 1,541/1,541 real files with zero
    // residual, so anything else means the model is wrong for this input -
    // and a residual is a measurement worth surfacing, not swallowing
    // (Sec10 item 3).
    size_t predictedEnd = pos + 4;
    if (predictedEnd != bytes.size()) {
        throw FormatError(
            "structural walk ended at " + std::to_string(predictedEnd) + " but the file is " +
            std::to_string(bytes.size()) +
            " bytes - the exact-size replay (spec Sec4, 1541/1541 with zero residual) does not "
            "hold for this file; residual " +
            std::to_string(static_cast<long long>(bytes.size()) -
                           static_cast<long long>(predictedEnd)));
    }

    return m;
}

ByteView MorphFile::bulkBytes(size_t descriptorIndex, ByteView content) const {
    if (descriptorIndex >= descriptors_.size()) {
        throw FormatError("MorphFile::bulkBytes: descriptor index out of range");
    }
    const Descriptor& d = descriptors_[descriptorIndex];
    return content.subview(d.bulkOffset, d.bulkByteLength);
}

Element MorphFile::elementAt(size_t descriptorIndex, size_t elementIndex, ByteView content) const {
    if (descriptorIndex >= descriptors_.size()) {
        throw FormatError("MorphFile::elementAt: descriptor index out of range");
    }
    const Descriptor& d = descriptors_[descriptorIndex];
    if (elementIndex >= d.affectedVertexCount) {
        throw FormatError("MorphFile::elementAt: element index out of range");
    }
    size_t base = d.bulkOffset + elementIndex * kElementSize;
    Element e;
    e.component0 = content.readU16LE(base + 0);
    e.component1 = content.readU16LE(base + 2);
    e.component2 = content.readU16LE(base + 4);
    e.vertexIndex = content.readU16LE(base + kVertexIndexOffsetInElement);
    e.field_8 = content.readU16LE(base + 8);
    e.field_10 = content.readU16LE(base + 10);
    return e;
}

size_t MorphFile::totalElementCount() const {
    size_t total = 0;
    for (const auto& d : descriptors_) {
        total += d.affectedVertexCount;
    }
    return total;
}

} // namespace sr3morph
