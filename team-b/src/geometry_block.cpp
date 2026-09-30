#include "sr3geometry/geometry_block.h"

namespace sr3geometry {

namespace {

constexpr uint16_t kVersionBelowWhichArray5Defaults = 0x2A; // CONFIRMED (spec Sec4.1 table, array 5 row)
constexpr uint32_t kMeshSubBlockExpectedVersion = 9;        // CONFIRMED - disassembly (spec Sec4.1.1)

// Lays out all six arrays from `arraySpaceStart` per spec Sec4.1's
// confirmed alignment rules, optionally filling `out`, and returns the
// trailer offset - where the Mesh sub-block (spec Sec4.1.1) must start.
// ONE function does both the real placement and the validation walk, so
// the two can never drift apart (they did in an earlier revision, which
// is exactly the kind of bug that silently yields wrong array offsets).
//
// Two rules here are easy to get wrong, and getting either wrong is
// invisible on simple meshes but breaks every richer one:
//   * An EMPTY array occupies nothing AND triggers no alignment padding -
//     it is skipped entirely, not "placed with zero bytes then padded."
//   * The final step, from the last populated array to the trailer, is
//     8-byte aligned, NOT 16 - spec Sec4.1 says exactly this ("an 8-byte
//     alignment for the final trailer step"); it just happens to be
//     indistinguishable from 16 whenever the last array ends on a
//     16-boundary anyway, which is the case on every simple sample.
size_t layOutArrays(size_t arraySpaceStart, const size_t (&counts)[6],
                    std::array<GeometryBlock::RawArray, 6>* out) {
    size_t pos = arraySpaceStart;
    for (size_t i = 0; i < 6; ++i) {
        size_t bytes = counts[i] * kArrayStride[i];
        // An empty array has no real location at all, so report the plain
        // cursor for it rather than the 16-aligned position it *would*
        // have taken - otherwise a trailing run of empty arrays reports
        // offsets sitting past the Mesh sub-block, which reads like a bug
        // in real dumps even though the zero length makes it harmless.
        size_t offset = bytes != 0 ? roundUp16(pos) : pos;
        if (out) {
            (*out)[i].offset = offset;
            (*out)[i].count = counts[i];
            (*out)[i].stride = kArrayStride[i];
        }
        if (bytes != 0) {
            pos = offset + bytes;
        }
    }
    return roundUp8(pos);
}

// Locating the geometry block's magic by a single roundUp16(totalSize) is
// NOT reliable: real data showed a sample where the material block's own
// end (materialBlockEnd) was ALREADY an exact multiple of 16, yet the
// real magic sat a FULL extra 16-byte block further out - i.e. the
// confirmed "16-byte alignment pad" (spec Sec4.1) is sometimes a
// mandatory minimum gap, not a no-op when already aligned, and this
// session found no way to predict which case applies from the material
// block's own fields alone. Rather than guess at a padding rule, search
// nearby 16-byte-aligned candidates for the real magic directly - a
// 4-byte exact match is an extremely strong anchor on its own (far
// stronger than the coincidental zlib-header-shaped false positives this
// project has had to guard against elsewhere), so this is safe even
// though it means "no exact padding formula was found," not a weaker
// fallback. Returns false (rather than throwing) if no candidate matches,
// so callers can distinguish "genuinely not found" (parse() throws) from
// "checking whether it's there at all" (looksLikeNoGeometryPlaceholder()).
bool findGeometryMagic(ByteView content, const MaterialBlock& material, size_t& outOffset) {
    size_t naiveGeomOffset = roundUp16(material.totalSize);
    for (size_t delta = 0; delta <= 64; delta += 16) {
        size_t candidate = naiveGeomOffset + delta;
        if (candidate + 4 <= content.size() && content.readU32LE(candidate) == kGeometryBlockMagic) {
            outOffset = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

GeometryBlock GeometryBlock::parse(ByteView content, const MaterialBlock& material) {
    size_t geomOffset = 0;
    if (!findGeometryMagic(content, material, geomOffset)) {
        throw FormatError("bad geometry-block magic number: expected 0x424BD00D (spec Sec4.1, CONFIRMED) "
                           "at or near roundUp16(material block end)");
    }
    return parseAt(content, geomOffset);
}

GeometryBlock GeometryBlock::parseAt(ByteView content, size_t geomOffset) {
    // Same walk as parse(), for carriers that state the sub-header's
    // position instead of implying it from a material block. Vehicles are
    // the reason this exists: `.ccar_pc` gives the offset outright at its
    // header +0x04, and has NO material block at all (absent in 393/393),
    // so the search parse() performs has nothing to anchor on.
    if (geomOffset + 4 > content.size() ||
        content.readU32LE(geomOffset) != kGeometryBlockMagic) {
        throw FormatError("no 0x424BD00D geometry magic at the given sub-header offset " +
                          std::to_string(geomOffset) +
                          " - the caller supplied the wrong position");
    }

    if (geomOffset + 0x81 > content.size()) {
        throw FormatError(
            "content too small to contain the geometry block's fixed header "
            "and gating fields (up to +0x80)");
    }

    GeometryBlock g;
    g.offset_ = geomOffset;
    g.version_ = content.readU16LE(geomOffset + 0x04);
    g.flags_ = content.readU16LE(geomOffset + 0x06);

    uint16_t count1 = content.readU16LE(geomOffset + 0x08);
    uint16_t count4 = content.readU16LE(geomOffset + 0x0A);
    uint16_t f10 = content.readU16LE(geomOffset + 0x10);
    uint16_t rawCount5 = content.readU16LE(geomOffset + 0x12);
    uint16_t count2 = content.readU16LE(geomOffset + 0x38);
    uint16_t count3 = content.readU16LE(geomOffset + 0x3A);
    uint32_t f70 = content.readU32LE(geomOffset + 0x70);
    // Gate widths for +0x78/+0x80 are NOT confirmed by the spec text (only
    // that they exist and gate arrays 5/6 "separately" from their own
    // count fields) - read as u32 (a safe superset: every real sample
    // checked this session had these at 0, so a narrower true width would
    // still read as 0 here). No real sample with a non-zero gate has been
    // found yet - if a future one contradicts this, revisit the width.
    uint32_t gate5 = content.readU32LE(geomOffset + 0x78);
    uint32_t gate6 = content.readU32LE(geomOffset + 0x80);

    size_t effectiveCount5 =
        gate5 == 0 ? 0
                   : (g.version_ < kVersionBelowWhichArray5Defaults ? static_cast<size_t>(f70)
                                                                     : static_cast<size_t>(rawCount5));
    size_t effectiveCount6 = gate6 == 0 ? 0 : static_cast<size_t>(f10) * static_cast<size_t>(f70);

    size_t counts[6] = {count1, count2, count3, count4, effectiveCount5, effectiveCount6};

    // BlobA (see the anonymous-namespace comment above): a real,
    // authoritative length field at geomOffset+0x88 tells us exactly how
    // many content bytes to skip - no need to walk/guess at the embedded
    // name copy's own internal shape at all (the previous approach tried
    // to re-derive this length by walking N null-terminated names, which
    // is what broke on richer meshes - see geometry_block.h's file
    // comment for the full story of that bug and this fix).
    if (geomOffset + kBlobALengthFieldOffset + 4 > content.size()) {
        throw FormatError("content too small to contain BlobA's length field (+0x88)");
    }
    uint32_t blobALength = content.readU32LE(geomOffset + kBlobALengthFieldOffset);
    // BlobA's content does NOT begin immediately after its length field. It
    // begins one byte past the next 16-byte boundary - a leading NUL that is
    // a convention in this format family - and the declared length counts
    // from THERE. The end is then 8-aligned, not 16-aligned.
    //
    //     dataStart = roundUp16(lengthField + 4) + 1
    //     end       = roundUp8(dataStart + declaredLength)
    //
    // Scored against an independent oracle (the Mesh check value, which
    // equals the paired g-file's leading word - so the target is located
    // with no array arithmetic at all) over 372 vehicles:
    //
    //     this rule                       372 / 372
    //     same rule without the final align8    0 / 372
    //     the previous roundUp16(blobEnd)     205 / 372
    //
    // WHY THIS HID FOR SO LONG. The walk re-aligns before every array it
    // actually visits, so a one-byte error in the blob is absorbed by the
    // next populated array. Vehicles have arrays 1-4 entirely empty, so
    // nothing re-aligns between the blob and the Mesh sub-block and the
    // error propagates to the landing site. The bug was never
    // vehicle-specific - vehicles are just the one population that cannot
    // hide it.
    // Pass the blob's end UNALIGNED. layOutArrays already 16-aligns every
    // POPULATED array and returns roundUp8(pos) for the trailer, so
    // pre-aligning here was both redundant and wrong: it forced the array
    // space onto a 16-boundary that the format does not require, which is
    // exactly the 8 bytes the vehicles were losing.
    //
    //   populated arrays -> roundUp16(blobEnd), unchanged from before
    //   all arrays empty -> trailer = roundUp8(blobEnd), which is the case
    //                       the old pre-alignment silently rounded past
    const size_t blobDataStart = geomOffset + kBlobALengthFieldOffset + 4 + kBlobAContentPad;
    size_t arraySpaceStart = blobDataStart + blobALength;

    // Lay the arrays out and get the trailer position in one pass, then
    // validate against the Mesh-sub-block version-9 marker (spec Sec4.1.1)
    // as a real sanity check on all the arithmetic above rather than
    // trusting it blind - if BlobA's length field or the alignment rules
    // ever don't hold on some future file, this catches it loudly instead
    // of silently mis-locating every array.
    size_t trailerOffset = layOutArrays(arraySpaceStart, counts, &g.arrays_);
    if (trailerOffset + 4 > content.size() ||
        content.readU32LE(trailerOffset) != kMeshSubBlockExpectedVersion) {
        throw FormatError(
            "computed array layout did not land on a Mesh sub-block version-9 "
            "marker after the six arrays - the BlobA length field or the "
            "array alignment rules (see geometry_block.h) may not hold for "
            "this file");
    }

    g.hasMeshSubBlock_ = true;
    g.meshSubBlockOffset_ = trailerOffset;

    return g;
}

ByteView GeometryBlock::arrayBytes(size_t index, ByteView content) const {
    if (index >= arrays_.size()) {
        throw FormatError("GeometryBlock::arrayBytes: index out of range");
    }
    const RawArray& a = arrays_[index];
    return content.subview(a.offset, a.byteLength());
}

} // namespace sr3geometry
