#include "sr3foliage/foliage_mesh.h"

#include <cstring>
#include <string>

namespace sr3foliage {

namespace {

// Outer-block field offsets, relative to the block's own magic (spec Sec4).
constexpr size_t kFieldMeshOffset = 0x08;
constexpr size_t kFieldRuntimeSlots = 0x18;
constexpr size_t kFieldMaterialCount = 0x20;
constexpr size_t kFieldTextureNames = 0x28;
constexpr size_t kFieldLodCount = 0x30;  // record count of the LOD/fade table (spec Sec4, Sec11.2)
constexpr size_t kFieldField34 = 0x34;   // OPEN - surfaced raw, never interpreted
constexpr size_t kFieldLodTable = 0x38;  // SIGNED offset relative to the outer-block base (spec Sec11.2)
constexpr size_t kOuterFieldsEnd = 0x40; // the material sub-record stream follows the runtime structures

// The MANDATORY pad: round up to the next 16-byte boundary, ALWAYS
// advancing at least one byte (spec Sec3, citing spec-geometry-format.md
// Sec3.1.1). This is a strictly-greater rounding, not the conditional
// no-op-when-aligned form that .cmorph_pc uses - the two formats really do
// differ here, so don't unify them.
//
// Worth noting: this is the same rule sr3geometry had to discover
// empirically (21 of 549 real .ccmesh_pc files sit at naive+16 because
// their material block ended exactly on a boundary). Seeing it stated
// outright here confirms that finding independently.
constexpr size_t padStrictlyTo16(size_t value) {
    return value / 16 * 16 + 16;
}

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

// Resolves a name-table byte offset to its string, for binding convenience.
std::string nameAt(ByteView content, size_t tableStart, size_t tableEnd, uint32_t nameOffset) {
    size_t at = tableStart + nameOffset;
    if (at >= tableEnd) return std::string();
    size_t len = content.cStringLength(at);
    ByteView s = content.subview(at, len);
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

} // namespace

FoliageMesh FoliageMesh::parse(ByteView content) {
    FoliageMesh f;

    // --- Shared material-reference block at +0x00. Required here: spec
    // Sec3 notes its validator failing aborts the load for this format
    // (unlike vehicles, where it is optional), so a throw is correct.
    //
    // sr3geometry::MaterialBlock::parse() throws sr3geometry::FormatError,
    // a DIFFERENT (unrelated) type from this namespace's own FormatError -
    // caught and rewrapped here so this function's own documented contract
    // ("Throws FormatError...", foliage_mesh.h) actually holds for every
    // failure mode, not just the ones this file itself detects. Found via
    // this reader's own synthetic test suite: a `catch (const FormatError&)`
    // written against sr3foliage::FormatError silently missed every
    // material-block failure before this fix, since the real exception
    // sr3geometry::MaterialBlock::parse() throws was a same-named-but-
    // unrelated sibling type, not a subclass. ---
    try {
        f.materialBlock_ = sr3geometry::MaterialBlock::parse(content);
    } catch (const sr3geometry::FormatError& ex) {
        throw FormatError(std::string("material block failed to validate: ") + ex.what());
    }

    // --- Mandatory pad to the outer block. ---
    size_t base = padStrictlyTo16(f.materialBlock_.totalSize);
    f.outerBlockOffset_ = base;
    if (base + kOuterFieldsEnd > content.size()) {
        throw FormatError("content too small to contain the outer 0x0FF1C1A1 block's fields");
    }

    if (content.readU32LE(base + 0x00) != kOuterMagic) {
        throw FormatError("bad outer block magic: expected 0x0FF1C1A1 (spec Sec4, CONFIRMED) at the "
                           "mandatory-padded end of the material block");
    }
    f.version_ = content.readU32LE(base + 0x04);
    if (f.version_ != kOuterVersion) {
        throw FormatError("unsupported .cfmesh_pc outer version " + std::to_string(f.version_) +
                           ": the format requires exactly " + std::to_string(kOuterVersion) +
                           " (spec Sec4, CONFIRMED - the loader requires it)");
    }

    uint32_t meshOffset = content.readU32LE(base + kFieldMeshOffset);
    uint32_t runtimeSlotsOffset = content.readU32LE(base + kFieldRuntimeSlots);
    f.materialCount_ = content.readU32LE(base + kFieldMaterialCount);
    uint32_t textureNamesOffset = content.readU32LE(base + kFieldTextureNames);
    f.lodRecordCount_ = content.readU32LE(base + kFieldLodCount);
    f.outerField34_ = content.readU32LE(base + kFieldField34);
    uint32_t lodTableWord = content.readU32LE(base + kFieldLodTable);

    // --- LOD/fade table location. The word at +0x38 is a SIGNED offset
    // relative to the outer-block base `base` (spec Sec11.2, wording
    // clarified 2026-09-20): the table starts at base + int32(word), NOT at
    // base + 0x38 itself. -1 (0xFFFFFFFF) means "no table" - the same
    // idiom as every other offset field in this block. ---
    std::memcpy(&f.lodTableOffsetRaw_, &lodTableWord, sizeof(lodTableWord));
    f.hasLodTable_ = lodTableWord != kNullOffset;
    if (f.hasLodTable_) {
        if (f.lodTableOffsetRaw_ < 0) {
            throw FormatError("LOD/fade table offset (+0x38) is negative (" +
                               std::to_string(f.lodTableOffsetRaw_) +
                               ") and not the -1 'absent' marker: it would put the table before the "
                               "outer block, which no real file does (spec Sec11.2)");
        }
        f.lodTableOffset_ = base + static_cast<size_t>(f.lodTableOffsetRaw_);
        if (f.lodTableOffset_ > content.size()) {
            throw FormatError("LOD/fade table offset (+0x38 relative to the outer block) runs past "
                               "end of content");
        }
        f.lodTableTailBytes_ = content.size() - f.lodTableOffset_;
    }
    const size_t nameTableLimit = f.hasLodTable_ ? f.lodTableOffset_ : content.size();

    // --- The embedded "Mesh" sub-block: locate and version-check only.
    // Its internals stay unparsed on purpose (see foliage_mesh.h). ---
    if (meshOffset != kNullOffset) {
        size_t meshAt = base + meshOffset;
        if (meshAt + 4 > content.size()) {
            throw FormatError("embedded Mesh sub-block offset (+0x08) runs past end of content");
        }
        if (content.readU32LE(meshAt) != kMeshSubBlockVersion) {
            throw FormatError(
                "embedded Mesh sub-block at the +0x08 target does not read version 9 - spec Sec1 "
                "confirms it does in 19/19 real files, so this file does not match the model");
        }
        f.hasMeshSubBlock_ = true;
        f.meshSubBlockOffset_ = meshAt;
    }

    // --- Material sub-records. The runtime slot array is `count x 8` at
    // the +0x18 target, and the sub-record stream starts at the next
    // 16-byte boundary after it - the alignment step spec Sec9 records as
    // the one the first replay got wrong (8 held in 6/19, 16 in 19/19). ---
    if (runtimeSlotsOffset == kNullOffset || textureNamesOffset == kNullOffset) {
        throw FormatError("a required outer offset (+0x18 runtime slots, or +0x28 texture names) is "
                           "null; spec Sec4 shows both present in every real file");
    }
    size_t runtimeSlotsEnd =
        base + runtimeSlotsOffset + static_cast<size_t>(f.materialCount_) * 8;
    size_t materialStreamEnd = base + textureNamesOffset;
    if (runtimeSlotsEnd > content.size() || materialStreamEnd > content.size()) {
        throw FormatError("runtime slot array or texture-name table runs past end of content");
    }

    // --- Material-handle table (spec Sec3, Sec11.3): a 0x10-byte header
    // {u32 ptr-slot, 0, count, ?} + `count` u32s that ends EXACTLY where the
    // runtime slot array (this +0x18 target) begins (spec: 19/19). It is
    // located by backing up from the slot array. The u32s are load-time
    // placeholders - the loader overwrites each with a handle from the
    // material cache - so they are decoded and exposed, never used as keys,
    // and a non-identity table is NOT an error here (it is a checkable
    // property: MaterialHandleTable::isIdentity()). ---
    {
        const size_t slotsStart = base + runtimeSlotsOffset;
        const size_t tableBytes =
            kMaterialHandleHeaderSize + static_cast<size_t>(f.materialCount_) * 4;
        if (slotsStart >= base + kOuterFieldsEnd + tableBytes) {
            MaterialHandleTable& t = f.materialHandleTable_;
            t.offset = slotsStart - tableBytes;
            t.headerWord0 = content.readU32LE(t.offset + 0x00);
            t.headerWord1 = content.readU32LE(t.offset + 0x04);
            t.headerCount = content.readU32LE(t.offset + 0x08);
            t.headerWord3 = content.readU32LE(t.offset + 0x0C);
            t.handles.reserve(f.materialCount_);
            for (uint32_t i = 0; i < f.materialCount_; ++i) {
                t.handles.push_back(content.readU32LE(t.offset + kMaterialHandleHeaderSize +
                                                       static_cast<size_t>(i) * 4));
            }
            f.hasMaterialHandleTable_ = true;
        }
    }

    size_t pos = alignUp(runtimeSlotsEnd, 16);
    f.materials_.reserve(f.materialCount_);
    for (uint32_t i = 0; i < f.materialCount_; ++i) {
        if (pos + 4 > content.size()) {
            throw FormatError("material sub-record " + std::to_string(i) +
                               "'s size prefix runs past end of content");
        }
        Material m;
        m.offset = pos;
        m.declaredSize = content.readU32LE(pos);
        size_t recordEnd = pos + m.declaredSize;
        if (m.declaredSize == 0 || recordEnd > content.size()) {
            throw FormatError("material sub-record " + std::to_string(i) + " declares size " +
                               std::to_string(m.declaredSize) +
                               ", which is zero or runs past end of content (spec Sec9 notes a zero "
                               "size prefix is the signature of misreading a runtime slot array as "
                               "data - check the preceding alignment)");
        }

        // u32 size, then 8-align, then the 0x30 header (spec Sec6).
        size_t hdr = alignUp(pos + 4, 8);
        if (hdr + kMaterialHeaderSize > content.size()) {
            throw FormatError("material sub-record " + std::to_string(i) +
                               "'s header runs past end of content");
        }
        m.shaderHash = content.readU32LE(hdr + 0x00);
        m.variantHash = content.readU32LE(hdr + 0x04);
        m.flags = content.readU32LE(hdr + 0x08);
        uint16_t bindingCount = content.readU16LE(hdr + 0x0C);
        uint8_t constantCount = content.at(hdr + 0x0E);
        uint8_t vec4Count = content.at(hdr + 0x0F);
        // +0x10/+0x18/+0x20 are runtime pointers, zero on disk.

        // Arrays in order, with the alignments the loader applies:
        // A x 12 (4-aligned), B x 4 (4-aligned), C x 16 (16-aligned).
        size_t cursor = alignUp(hdr + kMaterialHeaderSize, 4);
        if (cursor + static_cast<size_t>(bindingCount) * kTextureBindingSize > content.size()) {
            throw FormatError("material sub-record " + std::to_string(i) +
                               "'s texture bindings run past end of content");
        }
        m.textureBindings.reserve(bindingCount);
        for (uint16_t b = 0; b < bindingCount; ++b) {
            size_t at = cursor + static_cast<size_t>(b) * kTextureBindingSize;
            TextureBinding tb;
            tb.nameOffset = content.readU32LE(at + 0);
            tb.slotHash = content.readU32LE(at + 4);
            tb.resolvedIndex = content.readU16LE(at + 8);
            tb.flags = content.readU16LE(at + 10);
            tb.name = nameAt(content, base + textureNamesOffset, nameTableLimit, tb.nameOffset);
            m.textureBindings.push_back(std::move(tb));
        }
        cursor += static_cast<size_t>(bindingCount) * kTextureBindingSize;

        cursor = alignUp(cursor, 4);
        if (cursor + static_cast<size_t>(constantCount) * 4 > content.size()) {
            throw FormatError("material sub-record " + std::to_string(i) +
                               "'s constant-name hashes run past end of content");
        }
        m.constantNameHashes.reserve(constantCount);
        for (uint8_t c = 0; c < constantCount; ++c) {
            m.constantNameHashes.push_back(content.readU32LE(cursor + static_cast<size_t>(c) * 4));
        }
        cursor += static_cast<size_t>(constantCount) * 4;

        cursor = alignUp(cursor, 16);
        if (cursor + static_cast<size_t>(vec4Count) * 16 > content.size()) {
            throw FormatError("material sub-record " + std::to_string(i) +
                               "'s vec4 constants run past end of content");
        }
        m.constantValues.reserve(vec4Count);
        for (uint8_t c = 0; c < vec4Count; ++c) {
            size_t at = cursor + static_cast<size_t>(c) * 16;
            std::array<float, 4> v{};
            for (int k = 0; k < 4; ++k) v[static_cast<size_t>(k)] = readF32LE(content, at + static_cast<size_t>(k) * 4);
            m.constantValues.push_back(v);
        }

        f.materials_.push_back(std::move(m));
        pos = recordEnd;
    }

    // The chain must end EXACTLY at the file's own +0x28 target - spec
    // Sec3/Sec9's oracle, which held 19/19 once the 16-byte alignment was
    // right. Checked rather than assumed: a mismatch means the model is
    // wrong for this file, and the residual localises where.
    if (pos != materialStreamEnd) {
        throw FormatError(
            "material sub-record chain ended at " + std::to_string(pos) + " but the outer block's "
            "+0x28 target is " + std::to_string(materialStreamEnd) + " (residual " +
            std::to_string(static_cast<long long>(materialStreamEnd) -
                           static_cast<long long>(pos)) +
            ") - spec Sec3 has this landing exactly in 19/19 real files");
    }

    // --- Texture-name table: from the +0x28 target up to the LOD table
    // (or EOF if absent). ---
    size_t nameTableStart = base + textureNamesOffset;
    size_t nameTableEnd = nameTableLimit;
    if (nameTableEnd > content.size() || nameTableEnd < nameTableStart) {
        throw FormatError("texture-name table bounds are inconsistent with the LOD table offset");
    }
    size_t at = nameTableStart;
    while (at < nameTableEnd) {
        size_t len = content.cStringLength(at);
        if (len > 0) {
            ByteView s = content.subview(at, len);
            f.textureNames_.emplace_back(reinterpret_cast<const char*>(s.data()), s.size());
        }
        at += len + 1;
    }

    // --- LOD/fade table (spec Sec7, Sec11.2): `count` records of 0x18 bytes
    // starting at base + int32(+0x38), and the tail from there to EOF is
    // EXACTLY count x 24 bytes - the spec's 19/19 invariant, enforced here
    // per file rather than tolerated (no trailing slack, no silently
    // dropped partial record). ---
    if (f.hasLodTable_) {
        const uint64_t expectedTail =
            static_cast<uint64_t>(f.lodRecordCount_) * static_cast<uint64_t>(kLodRecordSize);
        if (static_cast<uint64_t>(f.lodTableTailBytes_) != expectedTail) {
            throw FormatError(
                "LOD/fade table tail is " + std::to_string(f.lodTableTailBytes_) +
                " bytes from base+int32(+0x38) to end of file, but the record count at +0x30 (" +
                std::to_string(f.lodRecordCount_) + ") x " + std::to_string(kLodRecordSize) + " = " +
                std::to_string(expectedTail) + " - spec Sec11.2 has these equal in 19/19 real files");
        }
        f.lodRecords_.reserve(f.lodRecordCount_);
        for (uint32_t i = 0; i < f.lodRecordCount_; ++i) {
            const size_t recAt = f.lodTableOffset_ + static_cast<size_t>(i) * kLodRecordSize;
            LodRecord r;
            for (size_t k = 0; k < kLodRecordFloats; ++k) {
                r.values[k] = readF32LE(content, recAt + k * 4);
            }
            r.fadeInStart = r.values[0];
            r.fadeInEnd = r.values[1];
            r.fadeOutStart = r.values[2];
            r.fadeOutEnd = r.values[3];
            r.drawGroupIndex = content.readU32LE(recAt + 0x10);
            r.billboardFlag = content.readU32LE(recAt + 0x14);
            f.lodRecords_.push_back(r);
        }
    }

    return f;
}

} // namespace sr3foliage
