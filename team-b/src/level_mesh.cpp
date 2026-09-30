#include "sr3clmesh/level_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <string>

#include "sr3geometry/material_block.h"

namespace sr3clmesh {

namespace {

// Places one array at the cursor with NO alignment of its own. The align
// belongs to the sub-parser's entry, not to each array: the functions that
// lay down two arrays (FUN_007499d0's `+0x48`/`+0x58` pair, FUN_00749e80's
// `+0xd8`/`+0xe8` pair) put them back to back, and FUN_00749e80 is the one
// that realigns between its own two. See notes 0 and 1 in level_mesh.h.
RawArray placeArray(size_t& cursor, size_t count, size_t stride) {
    RawArray a;
    a.count = count;
    a.stride = stride;
    if (count == 0) {
        a.offset = 0;
        return a;
    }
    a.offset = cursor;
    cursor += count * stride;
    return a;
}

// Every step below is bounds-checked against `limit` (already clamped to the
// buffer by the caller) and reports failure rather than throwing: a walk that
// runs off the end of a malformed entry is a measurement, not an exception.
struct Reader {
    ByteView content;
    size_t limit = 0;

    bool has(size_t at, size_t n) const { return at <= limit && limit - at >= n; }
    bool u32(size_t at, uint32_t& out) const {
        if (!has(at, 4)) return false;
        out = content.readU32LE(at);
        return true;
    }
    bool u16(size_t at, uint16_t& out) const {
        if (!has(at, 2)) return false;
        out = content.readU16LE(at);
        return true;
    }
    // `cursor + count * stride`, refusing anything that leaves the buffer.
    bool advance(size_t& cursor, size_t count, size_t stride) const {
        if (cursor > limit) return false;
        if (count == 0 || stride == 0) return true;
        if (count > (limit - cursor) / stride) return false;
        cursor += count * stride;
        return true;
    }
    bool alignTo(size_t& cursor, size_t n) const {
        if (cursor > limit) return false;
        const size_t a = (cursor + n - 1) / n * n;
        if (a > limit) return false;
        cursor = a;
        return true;
    }
};

// FUN_00e401d0, the generic index-list block: 8-align, a `0x10`-byte header
// whose `+0x08` is the element count, then count x 4 bytes.
// spec-physics-format.md Sec4.4.6(b), CONFIRMED - disassembly.
bool indexListStep(const Reader& r, size_t cursor, IndexListBlock& out) {
    if (!r.alignTo(cursor, 8)) return false;
    uint32_t n = 0;
    if (!r.has(cursor, kIndexListHeaderSize)) return false;
    if (!r.u32(cursor + kIndexListCountAt, n)) return false;
    out.offset = cursor;
    out.count = n;
    out.payloadOffset = cursor + kIndexListHeaderSize;
    size_t end = cursor + kIndexListHeaderSize;
    if (!r.advance(end, n, kIndexListStride)) return false;
    out.end = end;
    return true;
}

// FUN_00e71410's c-side, the shared "Mesh" sub-block step. Self-validating:
// the version word is 9, and the block's own check value is repeated in the
// last 4 bytes of its declared c-length. The advance is `cLength` EXACTLY -
// no rounding on either side (note 6 in level_mesh.h). `roundUp` is the
// control that reverts that to roundUp16 and must cost the walk its landing.
bool meshStep(const Reader& r, size_t cursor, bool roundUp, MeshStep& out) {
    uint16_t ver = 0;
    if (!r.u16(cursor, ver) || ver != 9) return false;
    const size_t q = roundUp4(cursor + 2);
    uint32_t chk = 0, len = 0;
    if (!r.u32(q, chk) || !r.u32(q + 4, len)) return false;
    const int32_t cLen = static_cast<int32_t>(len);
    if (cLen < 8) return false;
    const size_t end = cursor + static_cast<size_t>(cLen);
    if (!r.has(cursor, static_cast<size_t>(cLen))) return false;
    uint32_t bookend = 0;
    if (!r.u32(end - 4, bookend) || bookend != chk) return false;
    out.offset = cursor;
    out.checkValue = chk;
    out.cLength = static_cast<size_t>(cLen);
    out.end = roundUp ? roundUp16(end) : end;
    return out.end <= r.limit;
}

// FUN_00e3c4e0, one render-mesh group: a `0x60`-byte header at a 16-aligned
// cursor, an index-list block, an embedded Mesh sub-block, then a u16 array
// and a u8 array, both `indexList.count` long with no alignment between them.
bool renderGroupStep(const Reader& r, size_t cursor, const WalkOptions& opt, RenderGroup& out) {
    if (!r.alignTo(cursor, 16)) return false;
    out.offset = cursor;
    if (!r.has(cursor, kRenderGroupHeaderSize)) return false;
    cursor += kRenderGroupHeaderSize;
    if (!indexListStep(r, cursor, out.indexList)) return false;
    cursor = out.indexList.end;
    if (!meshStep(r, cursor, opt.meshStepRoundUp16, out.mesh)) return false;
    cursor = out.mesh.end;
    if (!r.alignTo(cursor, 8)) return false;
    out.u16ArrayOffset = cursor;
    if (!r.advance(cursor, out.indexList.count, 2)) return false;
    out.u8ArrayOffset = cursor;
    if (!r.advance(cursor, out.indexList.count, 1)) return false;
    out.end = cursor;
    return true;
}

// FUN_00e40210, the material-set block.
bool materialSetStep(const Reader& r, size_t cursor, const WalkOptions& opt, MiddleLayout& m,
                     size_t& outEnd) {
    if (!r.alignTo(cursor, 8)) return false;
    const size_t H = cursor;
    if (!r.has(H, kMatSetHeaderSize)) return false;
    uint32_t materialCount = 0, nameTableLength = 0;
    if (!r.u32(H + kMatSetCount, materialCount)) return false;
    if (!r.u32(H + kMatSetNameTable, nameTableLength)) return false;
    m.materialSetOffset = H;
    m.materialCount = materialCount;

    // The name table is 16-aligned from the end of the `0x20`-byte header and
    // then skips ONE byte - the leading-NUL convention spec-tree-format.md
    // Sec6 item 2 already documents for this same shared block.
    cursor = roundUp16(H + kMatSetHeaderSize) + (opt.nameTableNulByte ? 1 : 0);
    if (cursor > r.limit) return false;
    m.nameTableOffset = cursor;
    m.nameTableLength = nameTableLength;
    if (nameTableLength != 0) {
        if (!r.advance(cursor, nameTableLength, 1)) return false;
    }

    // One 8-byte slot per material record, then the records themselves, each
    // one declaring its own byte size at its `+0x00` (FUN_00e71090).
    if (!r.alignTo(cursor, 8)) return false;
    m.materialSlots = placeArray(cursor, materialCount, kStrideMatSetSlot);
    if (cursor > r.limit) return false;
    m.materialRecordOffsets.clear();
    m.materialRecordOffsets.reserve(materialCount);
    for (uint32_t i = 0; i < materialCount; ++i) {
        uint32_t size = 0;
        if (!r.u32(cursor, size)) return false;
        if (size < 4) return false;
        m.materialRecordOffsets.push_back(cursor);
        if (!r.advance(cursor, size, 1)) return false;
    }
    outEnd = cursor;
    return true;
}

} // namespace

BoundingVolume boundingVolumeFromExtents(const std::array<float, 3>& min,
                                         const std::array<float, 3>& max) {
    BoundingVolume bv;
    bv.min = min;
    bv.max = max;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < 3; ++i) {
        // FUN_0074a110, exactly: half-extent per axis, centre as
        // min + half-extent (NOT (min+max)/2 - the same value, but this is
        // the operation the disassembly shows), radius as the Euclidean
        // length of the half-extent vector.
        bv.halfExtent[i] = (max[i] - min[i]) * 0.5f;
        bv.center[i] = min[i] + bv.halfExtent[i];
        sumSquares += bv.halfExtent[i] * bv.halfExtent[i];
    }
    bv.radius = std::sqrt(sumSquares);
    bv.valid = true;
    return bv;
}

BoundingVolume computeBoundingVolume(const sr3mesh::MeshBlock& mesh) {
    std::array<float, 3> lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
    std::array<float, 3> hi{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                            -std::numeric_limits<float>::max()};
    size_t seen = 0;
    for (size_t c = 0; c < mesh.channels().size(); ++c) {
        std::vector<sr3mesh::Vertex> verts;
        try {
            verts = mesh.decodeChannel(c);
        } catch (const std::exception&) {
            // An unprobed layout code, or a channel running past its buffer.
            // Skipped, not guessed at - sr3mesh refuses to decode at offsets
            // nobody has verified and this reader does not second-guess it.
            continue;
        }
        for (const sr3mesh::Vertex& v : verts) {
            for (size_t i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], v.position[i]);
                hi[i] = std::max(hi[i], v.position[i]);
            }
            ++seen;
        }
    }
    if (seen == 0) {
        BoundingVolume bv;
        bv.valid = false;
        return bv;
    }
    BoundingVolume bv = boundingVolumeFromExtents(lo, hi);
    bv.vertexCount = seen;
    return bv;
}

// ==========================================================================
// HEAD - FUN_007499d0's two slot arrays, the embedded Mesh sub-blocks, and
// array 2's index-list blocks (spec-physics-format.md Sec4.4.1 as corrected
// by Sec4.4.6).
// ==========================================================================
HeadWalk walkHead(ByteView content, size_t bodyStart, const uint32_t* headerWords, size_t limit,
                  const WalkOptions& opt) {
    HeadWalk h;
    Reader r{content, std::min(limit, content.size())};
    size_t cursor = bodyStart;
    if (cursor > r.limit) return h;

    const size_t countA = headerWords[kCountRefA / 4];
    const size_t countB = headerWords[kCountRefB / 4];

    // FUN_007499d0 aligns ONCE on entry and then lays both slot arrays back
    // to back - not once per array (note 4 in level_mesh.h).
    if (countA != 0 || countB != 0) {
        if (!r.alignTo(cursor, 16)) return h;
    }
    h.refA = placeArray(cursor, countA, kStrideRef);
    if (cursor > r.limit) return h;
    h.refB = placeArray(cursor, countB, kStrideRef);
    if (cursor > r.limit) return h;

    // Array 1's entries resolve to embedded Mesh sub-blocks, laid down
    // immediately after both arrays, one after another with no padding.
    h.meshBlockStart = cursor;
    h.meshes.reserve(countA);
    for (size_t i = 0; i < countA; ++i) {
        MeshStep step;
        if (!meshStep(r, cursor, opt.meshStepRoundUp16, step)) return h;
        cursor = step.end;
        h.meshes.push_back(step);
    }
    // Array 2's entries resolve through FUN_00e401d0, and those blocks come
    // AFTER every Mesh sub-block - the term Sec4.4.1's published head formula
    // omitted, and which this reader shipped without.
    if (opt.headIndexLists) {
        h.indexLists.reserve(countB);
        for (size_t i = 0; i < countB; ++i) {
            IndexListBlock block;
            if (!indexListStep(r, cursor, block)) return h;
            cursor = block.end;
            h.indexLists.push_back(block);
        }
    }
    h.end = cursor;
    h.ok = true;
    return h;
}

// ==========================================================================
// MIDDLE - thunk_FUN_00e3e590 -> FUN_00e3e590, in full
// (spec-physics-format.md Sec4.4.6(b)). This is the region the first version
// of this reader could not size and had to search past.
// ==========================================================================
MiddleLayout walkMiddle(ByteView content, size_t start, size_t limit, const WalkOptions& opt) {
    MiddleLayout m;
    Reader r{content, std::min(limit, content.size())};
    size_t cursor = start;
    if (!r.alignTo(cursor, 16)) return m;

    // The `0x50`-byte record itself. Header `+0xb8` receives its address,
    // which is why that slot reads zero on disk in every shipped entry: it is
    // an output for a pointer INTO the file's own bytes.
    m.offset = cursor;
    m.recordOffset = cursor;
    const size_t R = cursor;
    if (!r.has(cursor, kMidRecordSize)) return m;
    cursor += kMidRecordSize;

    uint32_t matSetSlot = 0;
    if (!r.u32(R + kMidMatSetSlot, matSetSlot)) return m;
    if (matSetSlot == 0xFFFFFFFFu) {
        // The abort path. Never taken in shipped data (0 / 100,384), kept
        // because the disassembly has it and a file that took it would
        // otherwise be silently mis-walked.
        m.aborted = true;
        m.end = cursor;
        m.ok = true;
        return m;
    }

    size_t afterMaterials = 0;
    if (!materialSetStep(r, cursor, opt, m, afterMaterials)) return m;
    cursor = afterMaterials;

    // R[+0x08]'s array: one 8-byte entry per material. This is the table
    // FUN_0074a110 indexes, bounded by the material count (Sec4.4.6(c)).
    if (opt.lookupTableArray) {
        if (!r.alignTo(cursor, 8)) return m;
        m.lookupTable = placeArray(cursor, m.materialCount, kStrideLookupEntry);
        if (cursor > r.limit) return m;
    } else {
        m.lookupTable.count = m.materialCount;
        m.lookupTable.stride = kStrideLookupEntry;
    }

    // FUN_00e3c6e0: render group 1 always, render group 2 unless R[+0x18] is
    // -1 (which no shipped file is, so every `.clmesh_pc` has exactly two).
    if (!renderGroupStep(r, cursor, opt, m.renderGroups[0])) return m;
    cursor = m.renderGroups[0].end;
    m.renderGroupCount = 1;
    uint32_t gate = 0;
    if (!r.u32(R + kMidGroup2Gate, gate)) return m;
    if (gate != 0xFFFFFFFFu && opt.secondRenderGroup) {
        if (!renderGroupStep(r, cursor, opt, m.renderGroups[1])) return m;
        cursor = m.renderGroups[1].end;
        m.renderGroupCount = 2;
    }

    // Two trailing record arrays, each `0x10` bytes per entry with its own
    // variable-length sub-list. The asymmetry is real and load-bearing: the
    // first holds its per-entry count at `+0x08` with the pointer slot at
    // `+0x00`, the second the other way round.
    uint32_t countA = 0, countB = 0, countTrailer = 0;
    if (!r.u32(R + kMidGroupACount, countA)) return m;
    if (!r.alignTo(cursor, 8)) return m;
    m.groupARecords = placeArray(cursor, countA, kStrideGroupRecord);
    if (cursor > r.limit) return m;
    for (uint32_t i = 0; i < countA; ++i) {
        uint32_t n = 0;
        if (!r.u32(m.groupARecords.offset + i * kStrideGroupRecord + kGroupARecordCountAt, n)) {
            return m;
        }
        if (!r.alignTo(cursor, 8)) return m;
        NestedEntry detail;
        detail.outerOffset = m.groupARecords.offset + i * kStrideGroupRecord;
        detail.innerCount = n;
        detail.innerOffset = cursor;
        detail.innerStride = kStrideGroupASub;
        m.groupADetail.push_back(detail);
        if (!r.advance(cursor, n, kStrideGroupASub)) return m;
    }

    if (!r.u32(R + kMidGroupBCount, countB)) return m;
    if (!r.alignTo(cursor, 8)) return m;
    m.groupBRecords = placeArray(cursor, countB, kStrideGroupRecord);
    if (cursor > r.limit) return m;
    for (uint32_t i = 0; i < countB; ++i) {
        uint32_t n = 0;
        if (!r.u32(m.groupBRecords.offset + i * kStrideGroupRecord + kGroupBRecordCountAt, n)) {
            return m;
        }
        if (!r.alignTo(cursor, 8)) return m;
        NestedEntry detail;
        detail.outerOffset = m.groupBRecords.offset + i * kStrideGroupRecord;
        detail.innerCount = n;
        detail.innerOffset = cursor;
        detail.innerStride = kStrideGroupBSub;
        m.groupBDetail.push_back(detail);
        if (!r.advance(cursor, n, kStrideGroupBSub)) return m;
    }

    if (!r.u32(R + kMidTrailerCount, countTrailer)) return m;
    if (countTrailer != 0) {
        if (!r.alignTo(cursor, 16)) return m;
        m.trailer34 = placeArray(cursor, countTrailer, kStrideMidTrailer);
        if (cursor > r.limit) return m;
    } else {
        m.trailer34.stride = kStrideMidTrailer;
    }

    m.end = cursor;
    m.ok = true;
    return m;
}

// ==========================================================================
// TAIL - FUN_00749b40 .. FUN_00749e80
// ==========================================================================
TailWalk walkTail(ByteView content, size_t start, const uint32_t* headerWords, size_t limit,
                  const WalkOptions& opt) {
    TailWalk w;
    Reader r{content, std::min(limit, content.size())};
    size_t cursor = start;
    if (cursor > r.limit) return w;

    auto field = [&](size_t off) -> size_t { return headerWords[off / 4]; };

    // --- FUN_00749b40: one flat array, 56-byte stride. It 16-aligns
    // UNCONDITIONALLY - only the array advance is conditional (note 0).
    if (!r.alignTo(cursor, 16)) return w;
    w.array70 = placeArray(cursor, field(kCount70), kStride70);
    if (cursor > r.limit) return w;

    // --- FUN_00748c70 / FUN_00749ba0: the gated `+0x78`/`+0x7c` pair. ----
    // Three outcomes, two of which consume real file-resident bytes
    // (Sec4.4.6(f)); note 3 in level_mesh.h has the whole story, including
    // which bit of the flags byte gates the hull and how that was settled.
    GatedPair& gp = w.gatedPair;
    const int32_t pair = static_cast<int32_t>(field(kPairFirst));
    if (pair != kPairSentinel) {
        gp.branch = GatedPairBranch::ListBlock;
        size_t at = cursor;
        if (!r.alignTo(at, 16)) return w;
        gp.offset = at;
        if (opt.gatedPairZeroCost) {
            gp.length = at - cursor;
            cursor = at;
        } else {
            uint32_t size = 0, count = 0;
            if (!r.u32(at + 0x08, size) || !r.u32(at + 0x0c, count)) return w;
            if (size < 0x10) return w;
            gp.blockSize = size;
            gp.blockCount = count;
            if (!r.advance(at, size, 1)) return w;
            gp.length = at - cursor;
            cursor = at;
        }
    } else if ((field(kFlagsByte) & opt.hullGateMask) != 0) {
        gp.branch = GatedPairBranch::CollisionHull;
        size_t at = cursor;
        if (!r.alignTo(at, 16)) return w;
        gp.offset = at;
        CollisionHull& hull = gp.hull;
        hull.offset = at;
        uint32_t n = 0;
        if (!r.u32(at, n)) return w;
        at += 4;
        hull.vertexCount = n;
        hull.vertexOffset = at;
        if (!r.advance(at, n, kHullVertexStride)) return w;
        if (!r.alignTo(at, 4)) return w;
        if (!r.u32(at, n)) return w;
        at += 4;
        hull.indexCount = n;
        hull.indexOffset = at;
        if (!r.advance(at, n, kHullIndexStride)) return w;
        if (!r.alignTo(at, 16)) return w;
        if (!r.u32(at, n)) return w;
        at += 4;
        hull.blobLength = n;
        if (!r.alignTo(at, 16)) return w;
        hull.blobOffset = at;
        if (!r.advance(at, n, 1)) return w;
        if (!r.alignTo(at, 4)) return w;
        hull.vec3aOffset = at;
        if (!r.advance(at, 12, 1)) return w;
        hull.vec3bOffset = at;
        if (!r.advance(at, 12, 1)) return w;
        hull.end = at;
        gp.length = at - cursor;
        cursor = at;
    } else {
        // Neither branch does anything - not even an alignment. Asserting one
        // here costs 24 of the 4,232 DLC entries their exact landing, which is
        // why `gatedPairNoneBranchAligns` exists as a control rather than as
        // an assumption either way.
        gp.branch = GatedPairBranch::None;
        if (opt.gatedPairNoneBranchAligns) {
            if (!r.alignTo(cursor, 16)) return w;
        }
        gp.offset = cursor;
    }

    // --- FUN_00749c40: nested group, 16-byte outer, 12-byte inner. -------
    if (field(kCount88) != 0) {
        if (!r.alignTo(cursor, 16)) return w;
    }
    w.group88Outer = placeArray(cursor, field(kCount88), kStride88Outer);
    if (cursor > r.limit) return w;
    for (size_t i = 0; i < w.group88Outer.count; ++i) {
        NestedEntry e;
        e.outerOffset = w.group88Outer.offset + i * kStride88Outer;
        e.innerStride = kStride88Inner;
        uint32_t inner = 0;
        if (!r.u32(e.outerOffset + 0x00, inner)) return w;
        e.innerCount = inner;
        // Sec4.2: "the inner region immediately following in the file is
        // innerCount consecutive 12-byte elements" - no realignment. Now
        // confirmed on real bytes: all 258 carriers land exactly on EOF.
        e.innerOffset = e.innerCount != 0 ? cursor : 0;
        if (!r.advance(cursor, e.innerCount, kStride88Inner)) return w;
        w.group88.push_back(e);
    }

    // --- FUN_00749cf0 / FUN_00749d40: two more flat arrays, one each. ----
    if (field(kCount98) != 0) {
        if (!r.alignTo(cursor, 16)) return w;
    }
    w.array98 = placeArray(cursor, field(kCount98), kStride98);
    if (cursor > r.limit) return w;
    if (field(kCountA8) != 0) {
        if (!r.alignTo(cursor, 16)) return w;
    }
    w.arrayA8 = placeArray(cursor, field(kCountA8), kStrideA8);
    if (cursor > r.limit) return w;

    // --- FUN_00749e80. ---------------------------------------------------
    // Sec4.2: if count(+0xd8) == 0 the four downstream slots are zeroed and
    // the function skips STRAIGHT to its final section - so the `+0xe8`
    // array is gated on `+0xd8`, not on its own count.
    if (field(kCountD8) != 0) {
        if (!r.alignTo(cursor, 16)) return w;
        w.arrayD8 = placeArray(cursor, field(kCountD8), kStrideD8);
        if (cursor > r.limit) return w;
        // FUN_00749e80 DOES realign between its two arrays, where
        // FUN_007499d0 does not between its two - note 0 in level_mesh.h.
        // Measured: dropping this realign costs 37 of 846 files in dlc1.
        if (field(kCountE8) != 0) {
            if (!r.alignTo(cursor, 16)) return w;
        }
        w.arrayE8 = placeArray(cursor, field(kCountE8), kStrideE8);
        if (cursor > r.limit) return w;
    } else {
        w.arrayD8.stride = kStrideD8;
        w.arrayE8.stride = kStrideE8;
    }

    // The alignment-discipline switch: 16-byte up to here, 8-byte from here
    // on (Sec4.2, the same trailer convention `.ccmesh_pc` uses).
    if (!r.alignTo(cursor, 8)) return w;

    const size_t fcCount = field(kCountFc);
    // Primary array FIRST, then the parallel count array - see note 1 in
    // level_mesh.h. Both are laid down unconditionally at 8-byte alignment.
    // `trailingGroupProseOrder` is the control that builds the spec's own
    // prose order instead, and must cost the walk its landing.
    w.fcPrimary.count = fcCount;
    w.fcPrimary.stride = kStrideFcPrimary;
    w.fcParallel.count = fcCount;
    w.fcParallel.stride = kStrideFcParallel;
    size_t parallelAt = 0;
    if (opt.trailingGroupProseOrder) {
        w.fcParallel.offset = fcCount != 0 ? cursor : 0;
        parallelAt = cursor;
        if (!r.advance(cursor, fcCount, kStrideFcParallel)) return w;
        w.fcPrimary.offset = fcCount != 0 ? cursor : 0;
        if (!r.advance(cursor, fcCount, kStrideFcPrimary)) return w;
    } else {
        w.fcPrimary.offset = fcCount != 0 ? cursor : 0;
        if (!r.advance(cursor, fcCount, kStrideFcPrimary)) return w;
        w.fcParallel.offset = fcCount != 0 ? cursor : 0;
        parallelAt = cursor;
        if (!r.advance(cursor, fcCount, kStrideFcParallel)) return w;
    }

    for (size_t i = 0; i < fcCount; ++i) {
        NestedEntry e;
        e.innerStride = kStrideFcInner;
        uint32_t inner = 0;
        if (!r.u32(parallelAt + i * kStrideFcParallel, inner)) return w;
        e.innerCount = inner;
        if (!r.alignTo(cursor, 8)) return w;
        e.innerOffset = e.innerCount != 0 ? cursor : 0;
        if (!r.advance(cursor, e.innerCount, kStrideFcInner)) return w;
        w.groupFc.push_back(e);
    }

    w.ok = true;
    w.end = cursor;
    return w;
}

LevelMesh LevelMesh::parse(ByteView content, const WalkOptions& opt) {
    size_t naive = 0;
    try {
        sr3geometry::MaterialBlock mat =
            sr3geometry::MaterialBlock::parse(sr3geometry::ByteView(content.data(), content.size()));
        naive = sr3geometry::roundUp16(mat.totalSize);
    } catch (const std::exception& ex) {
        throw FormatError(std::string("`.clmesh_pc` does not open with the shared material "
                                      "reference block (spec-geometry-format.md Sec4.2, "
                                      "CONFIRMED): ") +
                          ex.what());
    }
    // Same search, and for the same reason, as sr3geometry::GeometryBlock:
    // the pad after the material block is a mandatory minimum gap, not a
    // plain round-up, so the position is searched rather than computed. This
    // is the shared material block's rule and the only search left anywhere
    // in this reader - the `.clmesh_pc` chain itself is fully computed.
    for (size_t delta = 0; delta <= 64; delta += 16) {
        const size_t candidate = naive + delta;
        if (candidate + 4 <= content.size() && content.readU32LE(candidate) == kLevelMeshMagic) {
            return parseAt(content, candidate, opt);
        }
    }
    throw FormatError("bad Level_Mesh magic: expected 0x4fe66afa (spec-geometry-format.md Sec4.2, "
                      "CONFIRMED) at or near roundUp16(material block end) = " +
                      std::to_string(naive));
}

LevelMesh LevelMesh::parseAt(ByteView content, size_t headerOffset, const WalkOptions& opt) {
    if (headerOffset + 4 > content.size() ||
        content.readU32LE(headerOffset) != kLevelMeshMagic) {
        throw FormatError("no 0x4fe66afa Level_Mesh magic at the given header offset " +
                          std::to_string(headerOffset) + " - the caller supplied the wrong position");
    }
    if (headerOffset + kLevelMeshHeaderSize > content.size()) {
        throw FormatError("content too small to contain the fixed 0x140-byte Level_Mesh header at " +
                          std::to_string(headerOffset));
    }

    LevelMesh m;
    m.headerOffset_ = headerOffset;
    m.contentSize_ = content.size();
    m.version_ = content.readU32LE(headerOffset + 0x04);
    if (m.version_ != kLevelMeshVersion) {
        throw FormatError("Level_Mesh version is " + std::to_string(m.version_) +
                          ", expected exactly " + std::to_string(kLevelMeshVersion) +
                          " (spec-geometry-format.md Sec4.2, CONFIRMED - an exact value, not a range)");
    }
    for (size_t i = 0; i < m.headerWords_.size(); ++i) {
        m.headerWords_[i] = content.readU32LE(headerOffset + i * 4);
    }

    m.pairFirst_ = static_cast<int32_t>(m.headerWords_[kPairFirst / 4]);
    m.pairSecond_ = static_cast<int32_t>(m.headerWords_[kPairSecond / 4]);

    // ---- ONE cursor, three parts, nothing searched for. -----------------
    const size_t limit = content.size();
    HeadWalk head = walkHead(content, m.bodyStart(), m.headerWords_.data(), limit, opt);
    m.refA_ = head.refA;
    m.refB_ = head.refB;
    m.meshBlockStart_ = head.meshBlockStart;
    m.headMeshes_ = std::move(head.meshes);
    m.headIndexLists_ = std::move(head.indexLists);
    m.headEnd_ = head.end;
    if (!head.ok) return m;

    m.middle_ = walkMiddle(content, m.headEnd_, limit, opt);
    if (!m.middle_.ok) return m;

    m.tailStart_ = m.middle_.end;
    TailWalk tail = walkTail(content, m.tailStart_, m.headerWords_.data(), limit, opt);
    m.gatedPair_ = tail.gatedPair;
    m.array70_ = tail.array70;
    m.group88Outer_ = tail.group88Outer;
    m.group88_ = std::move(tail.group88);
    m.array98_ = tail.array98;
    m.arrayA8_ = tail.arrayA8;
    m.arrayD8_ = tail.arrayD8;
    m.arrayE8_ = tail.arrayE8;
    m.fcPrimary_ = tail.fcPrimary;
    m.fcParallel_ = tail.fcParallel;
    m.groupFc_ = std::move(tail.groupFc);
    m.tailEnd_ = tail.end;
    m.walkComplete_ = tail.ok;
    return m;
}

ByteView LevelMesh::headerBytes(ByteView content) const {
    return content.subview(headerOffset_, kLevelMeshHeaderSize);
}

uint32_t LevelMesh::headerField(size_t fieldOffset) const {
    if (fieldOffset + 4 > kLevelMeshHeaderSize || (fieldOffset % 4) != 0) {
        throw FormatError("LevelMesh::headerField: offset out of the 0x140-byte header, or unaligned");
    }
    return headerWords_[fieldOffset / 4];
}

ByteView LevelMesh::arrayBytes(const RawArray& a, ByteView content) const {
    if (a.count == 0) return ByteView();
    return content.subview(a.offset, a.byteLength());
}

// Note 7 at the top of level_mesh.h: the `+0x88` group's 12-byte inner
// element is 3 little-endian float32s (x, y, z), CONFIRMED empirically
// against the whole shipped population (HANDOFF Sec9.60) - not the
// 3-index / plane-normal readings spec-physics-format.md Sec4.2 left open.
std::vector<Vec3> LevelMesh::group88Positions(const NestedEntry& entry, ByteView content) const {
    if (entry.innerStride != kStride88Inner) {
        throw FormatError(
            "LevelMesh::group88Positions: entry.innerStride is " + std::to_string(entry.innerStride) +
            ", expected " + std::to_string(kStride88Inner) +
            " (kStride88Inner) - this looks like a groupFc() entry (4-byte inner stride), not a "
            "group88() one, and decoding it as a `+0x88` position triple would silently misread it");
    }
    std::vector<Vec3> out;
    out.reserve(entry.innerCount);
    for (size_t i = 0; i < entry.innerCount; ++i) {
        const size_t at = entry.innerOffset + i * entry.innerStride;
        const uint32_t ux = content.readU32LE(at + 0);
        const uint32_t uy = content.readU32LE(at + 4);
        const uint32_t uz = content.readU32LE(at + 8);
        Vec3 v;
        std::memcpy(&v.x, &ux, sizeof(float));
        std::memcpy(&v.y, &uy, sizeof(float));
        std::memcpy(&v.z, &uz, sizeof(float));
        out.push_back(v);
    }
    return out;
}

// Sec4.4.6(h) part 1 - CONFIRMED, transfers directly from
// spec-foliage-format.md Sec6 via the shared parser `FUN_00e71090`.
// `materialRecordOffsets[i]` points at the record's own leading `u32 size`
// (the whole record's byte length, size field included - `materialSetStep`
// above already relies on this to advance past it); the decoded header
// starts 8-aligned after that 4-byte prefix.
std::vector<MaterialRecord> LevelMesh::materialRecords(ByteView content) const {
    std::vector<MaterialRecord> out;
    out.reserve(middle_.materialRecordOffsets.size());
    for (size_t recOffset : middle_.materialRecordOffsets) {
        const size_t at = roundUp8(recOffset + kMatRecordSizePrefix);
        if (at + kMatRecordHeaderSize > content.size()) {
            throw FormatError("LevelMesh::materialRecords: header at " + std::to_string(at) +
                              " runs past end of content - the walk should have already rejected "
                              "this file");
        }
        MaterialRecord rec;
        rec.offset = at;
        rec.hash0 = content.readU32LE(at + kMatRecordHash0);
        rec.hash1 = content.readU32LE(at + kMatRecordHash1);
        rec.flags = content.at(at + kMatRecordFlags);
        rec.textureBindingCount = content.readU16LE(at + kMatRecordTexCount);
        rec.constantNameCount = content.at(at + kMatRecordConstCount);
        rec.vec4ConstantCount = content.at(at + kMatRecordVec4Count);
        out.push_back(rec);
    }
    return out;
}

// Measured this session (2026-09-30) - see the doc comment on the
// declaration in level_mesh.h for the full population figures (143/143
// bindings, 22/22 real files reachable from `1018h0.str2_pc`). The
// texture-binding run for materialRecords()[materialIndex] sits at a FIXED
// offset past that record's own decoded header - `rec.offset +
// kMatRecordHeaderSize` - exactly the same "run starts at header_start +
// 0x30" placement spec-vertex-format.md Sec12.8 already establishes for
// vehicles/characters (that section's own `run_start = header_start + 0x30`
// formula), since materialRecords() decodes `rec.offset` as this record's
// OWN header_start already (`roundUp8(recordOffset + kMatRecordSizePrefix)`
// - see materialRecords() above). What differs for this carrier, measured
// rather than assumed, is only WHICH name table `+0x00` resolves against -
// middle_.nameTableOffset/nameTableLength, not the outer shared MaterialBlock
// and not an absolute file offset (both tested and refuted, see the header
// comment).
sr3geometry::MaterialBinding LevelMesh::materialTextureBinding(size_t materialIndex,
                                                                ByteView content) const {
    sr3geometry::MaterialBinding out;
    if (middle_.nameTableLength == 0) return out; // no located name table - defensive, not expected on real data
    std::vector<MaterialRecord> recs = materialRecords(content);
    if (materialIndex >= recs.size()) return out;
    const MaterialRecord& rec = recs[materialIndex];
    if (rec.textureBindingCount == 0) return out;

    const size_t runStart = rec.offset + kMatRecordHeaderSize; // header_start + 0x30
    const size_t tableStart = middle_.nameTableOffset;
    const size_t tableEnd = middle_.nameTableOffset + middle_.nameTableLength;
    for (uint16_t bi = 0; bi < rec.textureBindingCount; ++bi) {
        const size_t at = runStart + static_cast<size_t>(bi) * 12;
        if (at + 12 > content.size()) break; // truncated file - stop, do not guess the rest
        const uint32_t nameOffset = content.readU32LE(at + 0);
        const uint32_t paramHash = content.readU32LE(at + 4);
        const uint32_t slotIdx = content.readU32LE(at + 8);

        // Same validity guard spec-vertex-format.md Sec8.4.2 / material_binding.h
        // state for the shared mechanism: a real parameter hash is a real
        // 32-bit constant (>= 256, not -1).
        if (paramHash < 256 || paramHash == 0xFFFFFFFFu) continue;

        const size_t abs = tableStart + static_cast<size_t>(nameOffset);
        if (abs < tableStart || abs >= tableEnd) continue;
        // A real name boundary: the table's own start, or immediately after
        // a NUL - rejects a name carved out of the middle of another (same
        // discipline sr3geometry::MaterialBindings::parse() already uses).
        const bool boundary = (abs == tableStart) || (content.at(abs - 1) == 0);
        if (!boundary) continue;

        std::string name;
        size_t p = abs;
        while (p < tableEnd) {
            const uint8_t c = content.at(p);
            if (c == 0) break;
            name.push_back(static_cast<char>(c));
            ++p;
        }
        if (name.empty()) continue;

        sr3geometry::TextureRef ref;
        ref.slot = slotIdx;
        ref.paramHash = paramHash;
        ref.name = name;
        out.textures.push_back(std::move(ref));
    }
    return out;
}

// Measured this session (2026-09-30) - see the doc comment on the
// declaration in level_mesh.h for the full population figures (24/24 real
// B hashes matched a real CTAB constant name, airport_controltower
// .clmesh_pc's own 19 materials). Same run-start as materialTextureBinding()
// (`rec.offset + kMatRecordHeaderSize`); B/C simply sit after array A,
// 4- then 16-aligned respectively, per spec-foliage-format.md Sec6.
LevelMesh::ShaderConstants LevelMesh::materialShaderConstants(size_t materialIndex,
                                                                ByteView content) const {
    ShaderConstants out;
    std::vector<MaterialRecord> recs = materialRecords(content);
    if (materialIndex >= recs.size()) return out;
    const MaterialRecord& rec = recs[materialIndex];

    const size_t runStart = rec.offset + kMatRecordHeaderSize; // header_start + 0x30, same as materialTextureBinding()
    const size_t aEnd = runStart + static_cast<size_t>(rec.textureBindingCount) * 12;
    const size_t bStart = roundUp4(aEnd);
    const size_t bEnd = bStart + static_cast<size_t>(rec.constantNameCount) * 4;
    const size_t cStart = roundUp16(bEnd);
    const size_t cEnd = cStart + static_cast<size_t>(rec.vec4ConstantCount) * 16;
    if (cEnd > content.size()) return out; // truncated/out of range - defensive, not expected on real data

    std::vector<uint32_t> bHashes;
    bHashes.reserve(rec.constantNameCount);
    for (uint16_t bi = 0; bi < rec.constantNameCount; ++bi) {
        bHashes.push_back(content.readU32LE(bStart + static_cast<size_t>(bi) * 4));
    }
    std::vector<std::array<float, 4>> cValues;
    cValues.reserve(rec.vec4ConstantCount);
    for (uint16_t ci = 0; ci < rec.vec4ConstantCount; ++ci) {
        std::array<float, 4> v{};
        const size_t at = cStart + static_cast<size_t>(ci) * 16;
        for (int k = 0; k < 4; ++k) {
            uint32_t u = content.readU32LE(at + static_cast<size_t>(k) * 4);
            std::memcpy(&v[static_cast<size_t>(k)], &u, sizeof(float));
        }
        cValues.push_back(v);
    }
    const size_t namedCount = bHashes.size() < cValues.size() ? bHashes.size() : cValues.size();
    out.named.reserve(namedCount);
    for (size_t i = 0; i < namedCount; ++i) out.named.push_back({bHashes[i], cValues[i]});
    for (size_t i = namedCount; i < cValues.size(); ++i) out.trailingUnnamed.push_back(cValues[i]);
    return out;
}

// Sec4.4.6(h) part 3 - CONFIRMED. `+0x2C` is the RAW on-disk index; the
// runtime REMAPS it through a temporary per-parse table
// (`materialHandleTable`, a stack `alloca` this reader has no on-disk
// equivalent for - see note 8, top of file) before it becomes a real
// material handle, so only the pre-remap index is decoded here.
std::vector<TrailerRecord> LevelMesh::trailerRecords(ByteView content) const {
    std::vector<TrailerRecord> out;
    out.reserve(middle_.trailer34.count);
    for (size_t i = 0; i < middle_.trailer34.count; ++i) {
        const size_t at = middle_.trailer34.offset + i * middle_.trailer34.stride;
        TrailerRecord rec;
        rec.offset = at;
        rec.materialSlotRaw = content.readU32LE(at + kTrailerMaterialSlotRaw);
        rec.lodByte = content.at(at + kTrailerLodByte);
        out.push_back(rec);
    }
    return out;
}

// Sec4.4.6(h) part 2 - CONFIRMED. `entry` must be one of middle().groupADetail's
// own entries (innerStride == kStrideGroupASub distinguishes it from a
// groupBDetail entry, same discipline group88Positions() uses against groupFc()).
std::vector<uint16_t> LevelMesh::groupAFeatureIndices(const NestedEntry& entry,
                                                       ByteView content) const {
    if (entry.innerStride != kStrideGroupASub) {
        throw FormatError(
            "LevelMesh::groupAFeatureIndices: entry.innerStride is " + std::to_string(entry.innerStride) +
            ", expected " + std::to_string(kStrideGroupASub) +
            " (kStrideGroupASub) - this looks like a groupBDetail entry, not a groupADetail one");
    }
    std::vector<uint16_t> out;
    out.reserve(entry.innerCount);
    for (size_t i = 0; i < entry.innerCount; ++i) {
        const size_t at = entry.innerOffset + i * entry.innerStride;
        out.push_back(content.readU16LE(at + kGroupASubFeatureIndex));
    }
    return out;
}

// Sec4.4.6(h) part 2 (group 2's own inner element) - CONFIRMED, same index
// namespace as groupAFeatureIndices() above (cross-referenced by the same
// runtime consumer, `glmesh_ctor_FUN_0074c530`).
std::vector<uint16_t> LevelMesh::groupBFeatureIndices(const NestedEntry& entry,
                                                       ByteView content) const {
    if (entry.innerStride != kStrideGroupBSub) {
        throw FormatError(
            "LevelMesh::groupBFeatureIndices: entry.innerStride is " + std::to_string(entry.innerStride) +
            ", expected " + std::to_string(kStrideGroupBSub) +
            " (kStrideGroupBSub) - this looks like a groupADetail entry, not a groupBDetail one");
    }
    std::vector<uint16_t> out;
    out.reserve(entry.innerCount);
    for (size_t i = 0; i < entry.innerCount; ++i) {
        const size_t at = entry.innerOffset + i * entry.innerStride;
        out.push_back(content.readU16LE(at + kGroupBInnerFeatureIndex));
    }
    return out;
}

// FUN_00749ba0's hull, at the two element sizes spec-physics-format.md
// Sec4.4.6(f) states outright ("12 = 3 x float32", "2 = u16"). The opaque
// precomputed blob and the two trailing vec3s are located but NOT decoded -
// Sec4.4.6 does not say what the blob is, and guessing would be inventing.
std::vector<Vec3> LevelMesh::hullVertices(ByteView content) const {
    std::vector<Vec3> out;
    if (gatedPair_.branch != GatedPairBranch::CollisionHull) return out;
    const CollisionHull& h = gatedPair_.hull;
    out.reserve(h.vertexCount);
    for (uint32_t i = 0; i < h.vertexCount; ++i) {
        const size_t at = h.vertexOffset + static_cast<size_t>(i) * kHullVertexStride;
        if (at + kHullVertexStride > content.size()) break;
        const uint32_t ux = content.readU32LE(at + 0);
        const uint32_t uy = content.readU32LE(at + 4);
        const uint32_t uz = content.readU32LE(at + 8);
        Vec3 v;
        std::memcpy(&v.x, &ux, sizeof(float));
        std::memcpy(&v.y, &uy, sizeof(float));
        std::memcpy(&v.z, &uz, sizeof(float));
        out.push_back(v);
    }
    return out;
}

std::vector<uint16_t> LevelMesh::hullIndices(ByteView content) const {
    std::vector<uint16_t> out;
    if (gatedPair_.branch != GatedPairBranch::CollisionHull) return out;
    const CollisionHull& h = gatedPair_.hull;
    out.reserve(h.indexCount);
    for (uint32_t i = 0; i < h.indexCount; ++i) {
        const size_t at = h.indexOffset + static_cast<size_t>(i) * kHullIndexStride;
        if (at + kHullIndexStride > content.size()) break;
        out.push_back(content.readU16LE(at));
    }
    return out;
}

std::vector<sr3mesh::MeshBlock> LevelMesh::resolveReferencedMeshes(ByteView content,
                                                                   ByteView gContent) const {
    std::vector<sr3mesh::MeshBlock> out;
    size_t cursor = meshBlockStart_;
    size_t gCursor = 0;
    for (size_t i = 0; i < refA_.count; ++i) {
        if (cursor + 4 > content.size()) break;
        try {
            // The pre-header (version, pad, checkValue, cLength, gLength) is
            // `roundUp8(cursor + 16)` wide, not an unconditional `+0x10` -
            // `sr3mesh::kHeaderStart`'s fixed 0x10 default is only the
            // `cursor % 8 == 0` case. `src/zone_geometry.cpp`'s own
            // `headerDisplacementFor()` already established and MEASURED
            // this exact rule for `.gzn_pc`'s own additive, non-8-guaranteed
            // chain (HANDOFF Sec9.55.2: 2,596 anchors at +0x10, 247 at
            // +0x14, zero ambiguous) - this reference chain is the same
            // shape (index 0 always lands 16-aligned via meshBlockStart(),
            // but `cursor += block.cLength()` gives no such guarantee for
            // index >= 1) and had simply never been wired to the same rule.
            // Real-data consequence, found and fixed 2026-09-14 (HANDOFF
            // Sec9.66): every reference this omission mis-parsed was at
            // chain index >= 1, landing on a `cursor % 8 == 4` position.
            const size_t headerDisplacement = roundUp8(cursor + 16) - cursor;
            sr3mesh::MeshBlock block = sr3mesh::MeshBlock::parse(content, cursor, gContent, gCursor,
                                                                 headerDisplacement);
            // BOTH sides advance by the block's own declared length, with NO
            // rounding on either. The g-side is the same additive chain
            // `.gzn_pc` uses (spec-zone-data-format.md Sec10.4, and
            // src/zone_geometry.cpp). The c-side was MEASURED here first (an
            // earlier revision 16-aligned it, which is the natural guess and
            // is wrong) and is now also read straight out of FUN_00e71410's
            // own closing self-check, spec-physics-format.md Sec4.4.6(b).
            // NOTE: `cLength()`/`gLength()` are read at fixed offsets from
            // `cursor` itself (before `headerDisplacement` is even applied),
            // so correcting `headerDisplacement` does not change this step.
            cursor += block.cLength();
            if (block.bulkInGFile()) gCursor += block.gLength();
            out.push_back(std::move(block));
        } catch (const std::exception&) {
            break; // stop rather than guess where the next one starts
        }
    }
    return out;
}

} // namespace sr3clmesh
