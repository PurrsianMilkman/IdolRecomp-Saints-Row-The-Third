// common.cpp - harness globals and shared helpers/fixtures.
//
// Split out of tests/synthetic_clmesh_test.cpp (Team C refactor, 2026-10-17); the code
// moved verbatim. Same executable, same test order (main.cpp).

#include "common.h"

int g_failures = 0;

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendCString(std::vector<uint8_t>& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0x00);
}

void putU16At(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void putF32At(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    putU32At(b, off, u);
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// spec-geometry-format.md Sec3.1.1: the pad after the material block is a
// mandatory MINIMUM gap, so a block already ending 16-aligned still gets a
// full extra 16 bytes.
size_t mandatoryPad16(size_t end) { return end / 16 * 16 + 16; }

vpp::ByteView view(const std::vector<uint8_t>& b) {
    return vpp::ByteView(b.data(), b.size());
}

// A minimal but real Mesh sub-block (version 9, g-backed); defined below,
// used by the HEAD when a test wants sr3mesh::MeshBlock to have something
// real to resolve.
void appendMeshSubBlock(std::vector<uint8_t>& c, std::vector<uint8_t>& g, uint32_t checkValue,
                        const std::vector<std::array<float, 3>>& positions);

// The c-side-only form of the same block, which is all the walk itself ever
// looks at: 0x00e71410 checks the version word 9, reads the check value and
// the c-length, and requires the check value repeated in the block's LAST 4
// bytes (spec-physics-format.md Sec4.4.6(b)). `payloadBytes` must be a
// multiple of 4 so consecutive blocks stay 4-aligned.
size_t appendMeshCBlock(std::vector<uint8_t>& b, uint32_t checkValue, size_t payloadBytes) {
    const size_t at = b.size();
    appendU32(b, 9);          // version word
    appendU32(b, checkValue);
    appendU32(b, 0);          // c-length, patched below
    appendU32(b, 0);          // g-length
    b.insert(b.end(), payloadBytes, 0x9E);
    appendU32(b, checkValue); // the bookend 0x00e71410 checks
    putU32At(b, at + 0x08, static_cast<uint32_t>(b.size() - at));
    return at;
}

// One 0x00e401d0 index-list block: 8-align, a 0x10-byte header whose
// `+0x08` is the count, then count x 4 bytes.
size_t appendIndexList(std::vector<uint8_t>& b, uint32_t count) {
    b.resize(alignUp(b.size(), 8), 0x00);
    const size_t at = b.size();
    b.resize(at + 0x10, 0x00);
    putU32At(b, at + 0x08, count);
    b.resize(b.size() + count * 4, 0x00);
    return at;
}

std::vector<uint8_t> buildClmesh(const ClmeshOptions& opt, ClmeshLayout& out,
                                 std::vector<uint8_t>* gOut /* = nullptr */) {
    std::vector<uint8_t> b;

    // --- Shared material-reference block (spec-geometry-format.md Sec3.1). --
    appendU32(b, sr3geometry::kMaterialBlockMagic);
    appendU32(b, 0); // name-table length, patched below
    appendU32(b, 0);
    appendU32(b, static_cast<uint32_t>(opt.materialNames.size()));
    b.insert(b.end(), 16, 0x00);
    const size_t nameTableStart = b.size();
    for (const std::string& n : opt.materialNames) appendCString(b, n);
    putU32At(b, 0x04, static_cast<uint32_t>(b.size() - nameTableStart));

    // --- The mandatory pad, then the fixed 0x140 header. -------------------
    const size_t headerAt = mandatoryPad16(b.size());
    b.resize(headerAt + sr3clmesh::kLevelMeshHeaderSize, 0x00);
    out.headerAt = headerAt;
    putU32At(b, headerAt + 0x00, sr3clmesh::kLevelMeshMagic);
    putU32At(b, headerAt + 0x04, opt.version);
    putU32At(b, headerAt + sr3clmesh::kCountRefA, opt.countRefA);
    putU32At(b, headerAt + sr3clmesh::kCountRefB, opt.countRefB);
    putU32At(b, headerAt + sr3clmesh::kPairFirst, static_cast<uint32_t>(opt.pairFirst));
    putU32At(b, headerAt + sr3clmesh::kPairSecond, static_cast<uint32_t>(opt.pairSecond));
    putU32At(b, headerAt + sr3clmesh::kCount70, opt.count70);
    putU32At(b, headerAt + sr3clmesh::kCount88, opt.count88);
    putU32At(b, headerAt + sr3clmesh::kCount98, opt.count98);
    putU32At(b, headerAt + sr3clmesh::kCountA8, opt.countA8);
    putU32At(b, headerAt + sr3clmesh::kCountD8, opt.countD8);
    putU32At(b, headerAt + sr3clmesh::kCountE8, opt.countE8);
    putU32At(b, headerAt + sr3clmesh::kCountFc, opt.countFc);
    putU32At(b, headerAt + sr3clmesh::kFlagsByte, opt.headerFlags);

    out.bodyAt = b.size();

    // --- HEAD: the two 8-byte-stride slot arrays, back to back. ------------
    if (opt.countRefA != 0 || opt.countRefB != 0) b.resize(alignUp(b.size(), 16), 0x00);
    out.refAAt = b.size();
    b.resize(b.size() + opt.countRefA * sr3clmesh::kStrideRef, 0x00);
    if (opt.mutateRealignBetweenHeadArrays) b.resize(alignUp(b.size(), 16), 0x00);
    out.refBAt = b.size();
    b.resize(b.size() + opt.countRefB * sr3clmesh::kStrideRef, 0x00);

    // ... then array 1's embedded Mesh sub-blocks, back to back with NO
    // padding between consecutive blocks ...
    out.meshBlockStart = b.size();
    for (uint32_t i = 0; i < opt.countRefA; ++i) {
        if (opt.mutateMeshStepRoundUp16 && i != 0) {
            // MUTATION: the natural guess - 16-align between blocks. It is
            // silent on any file with only one block to advance past, which
            // is exactly why it survived in this reader until a two-block
            // file turned up (HANDOFF Sec9.59).
            b.resize(alignUp(b.size(), 16), 0x00);
        }
        if (i < opt.headRealMeshes.size() && gOut != nullptr) {
            out.atHeadMesh.push_back(b.size());
            appendMeshSubBlock(b, *gOut, 0xC0FFEE11u + i, opt.headRealMeshes[i]);
        } else {
            out.atHeadMesh.push_back(appendMeshCBlock(b, 0xD0D00000u + i, 16));
        }
    }
    // ... and THEN array 2's index-list blocks, which Sec4.4.1's published
    // head formula left out entirely.
    if (!opt.mutateHeadNoIndexLists) {
        for (uint32_t i = 0; i < opt.countRefB; ++i) {
            out.atHeadIndexList.push_back(appendIndexList(b, opt.headIndexCount));
        }
    }
    out.headEnd = b.size();

    // --- MIDDLE: 0x00e3e590, built for real (Sec4.4.6(b)). ---------
    b.resize(alignUp(b.size(), 16), 0x00);
    out.middleAt = b.size();
    const size_t R = b.size();
    out.middleRecordAt = R;
    b.resize(R + sr3clmesh::kMidRecordSize, 0x00);
    // `+0x00` and `+0x18` stay zero: zero is "not -1", i.e. neither the abort
    // path nor the skip-the-second-render-group path, which is what every
    // shipped file carries.
    putU32At(b, R + sr3clmesh::kMidGroupACount,
             static_cast<uint32_t>(opt.midGroupASub.size()));
    putU32At(b, R + sr3clmesh::kMidGroupBCount,
             static_cast<uint32_t>(opt.midGroupBSub.size()));
    putU32At(b, R + sr3clmesh::kMidTrailerCount, opt.midTrailer34);
    // `+0x24` and `+0x34` are non-zero in 100,384/100,384 shipped entries and
    // read by NOTHING (Sec4.4.6(d)) - stale authoring-tool addresses. They are
    // written here precisely so a reader that started consuming them would
    // break this fixture.
    putU32At(b, R + 0x24, 0x01234567u);
    putU32At(b, R + 0x34, 0x89abcdefu);

    // 0x00e40210, the material-set block.
    b.resize(alignUp(b.size(), 8), 0x00);
    const size_t H = b.size();
    out.matSetAt = H;
    b.resize(H + sr3clmesh::kMatSetHeaderSize, 0x00);
    const uint32_t materialCount = static_cast<uint32_t>(opt.midRecordSizes.size());
    putU32At(b, H + sr3clmesh::kMatSetCount, materialCount);
    // The name table: 16-aligned from the end of the header, then ONE extra
    // byte - the leading-NUL convention. Dropping it is a real mutation.
    b.resize(alignUp(H + sr3clmesh::kMatSetHeaderSize, 16) +
                 (opt.mutateMiddleNoNulByte ? 0u : 1u),
             0x00);
    const size_t midNameStart = b.size();
    for (const std::string& n : opt.midMaterialNames) appendCString(b, n);
    putU32At(b, H + sr3clmesh::kMatSetNameTable, static_cast<uint32_t>(b.size() - midNameStart));
    // One 8-byte slot per record, then the records, each declaring its own
    // byte size at its `+0x00` (0x00e71090).
    b.resize(alignUp(b.size(), 8), 0x00);
    b.resize(b.size() + materialCount * sr3clmesh::kStrideMatSetSlot, 0x00);
    for (uint32_t size : opt.midRecordSizes) {
        const size_t at = b.size();
        b.resize(at + size, 0xA5);
        putU32At(b, at, size);
    }
    // R[+0x08]'s materialCount x 8 array - the table 0x0074a110 indexes.
    if (!opt.mutateMiddleNoLookupArray) {
        b.resize(alignUp(b.size(), 8), 0x00);
        b.resize(b.size() + materialCount * sr3clmesh::kStrideLookupEntry, 0x00);
    }

    // 0x00e3c6e0: two render-mesh groups, each a 0x60-byte header, an
    // index-list block, an embedded Mesh sub-block, then a u16 array and a u8
    // array of the index list's own length with no alignment between them.
    auto appendRenderGroup = [&](uint32_t indexCount, uint32_t checkValue) {
        b.resize(alignUp(b.size(), 16), 0x00);
        out.atRenderGroup.push_back(b.size());
        b.resize(b.size() + sr3clmesh::kRenderGroupHeaderSize, 0x00);
        appendIndexList(b, indexCount);
        appendMeshCBlock(b, checkValue, 16);
        b.resize(alignUp(b.size(), 8), 0x00);
        b.resize(b.size() + indexCount * 2 + indexCount, 0x00);
    };
    appendRenderGroup(opt.midIndexCountA, 0xA1A1A1A1u);
    if (!opt.mutateMiddleOneRenderGroup) appendRenderGroup(opt.midIndexCountB, 0xB2B2B2B2u);

    // The two trailing 0x10-byte record arrays. The asymmetry is real: the
    // first holds its per-record count at `+0x08`, the second at `+0x00`.
    b.resize(alignUp(b.size(), 8), 0x00);
    const size_t midA = b.size();
    b.resize(midA + opt.midGroupASub.size() * sr3clmesh::kStrideGroupRecord, 0x00);
    for (size_t i = 0; i < opt.midGroupASub.size(); ++i) {
        putU32At(b, midA + i * sr3clmesh::kStrideGroupRecord + sr3clmesh::kGroupARecordCountAt,
                 opt.midGroupASub[i]);
    }
    for (uint32_t n : opt.midGroupASub) {
        b.resize(alignUp(b.size(), 8), 0x00);
        b.resize(b.size() + n * sr3clmesh::kStrideGroupASub, 0x00);
    }
    b.resize(alignUp(b.size(), 8), 0x00);
    const size_t midB = b.size();
    b.resize(midB + opt.midGroupBSub.size() * sr3clmesh::kStrideGroupRecord, 0x00);
    for (size_t i = 0; i < opt.midGroupBSub.size(); ++i) {
        putU32At(b, midB + i * sr3clmesh::kStrideGroupRecord + sr3clmesh::kGroupBRecordCountAt,
                 opt.midGroupBSub[i]);
    }
    for (uint32_t n : opt.midGroupBSub) {
        b.resize(alignUp(b.size(), 8), 0x00);
        b.resize(b.size() + n * sr3clmesh::kStrideGroupBSub, 0x00);
    }
    if (opt.midTrailer34 != 0) {
        b.resize(alignUp(b.size(), 16), 0x00);
        b.resize(b.size() + opt.midTrailer34 * sr3clmesh::kStrideMidTrailer, 0x00);
    }
    out.middleEnd = b.size();

    // --- TAIL. 0x00749b40 16-aligns UNCONDITIONALLY on entry. ------------
    b.resize(alignUp(b.size(), 16), 0x00);
    out.tailAt = b.size();

    auto maybeAlign16IfEmptyArraysAlign = [&]() {
        if (opt.mutateEmptyArraysAlign) b.resize(alignUp(b.size(), 16), 0x00);
    };

    if (opt.count70 != 0) {
        b.resize(alignUp(b.size(), 16), 0x00);
        out.at70 = b.size();
        for (uint32_t i = 0; i < opt.count70 * sr3clmesh::kStride70; ++i) {
            b.push_back(static_cast<uint8_t>(0x70 + (i & 0x0F)));
        }
    } else {
        maybeAlign16IfEmptyArraysAlign();
    }

    // Sec4.4.6(f): THREE outcomes, two of which consume real bytes.
    if (opt.pairFirst != sr3clmesh::kPairSentinel) {
        // 0x00748c70 -> 0x007616c0: 16-align, then a block that declares
        // its OWN total byte size at `+0x08` and its entry count (< 3) at
        // `+0x0c`. Per-file, not the constant this reader used to assume.
        b.resize(alignUp(b.size(), 16), 0x00);
        out.atGatedBlock = b.size();
        b.resize(out.atGatedBlock + opt.gatedListBlockSize, 0x7E);
        putU32At(b, out.atGatedBlock + 0x04, 7); // version, must be >= 7
        putU32At(b, out.atGatedBlock + 0x08, opt.gatedListBlockSize);
        putU32At(b, out.atGatedBlock + 0x0c,
                 opt.pairSecond == sr3clmesh::kPairSentinel ? 1u : 2u);
    } else if ((opt.headerFlags & sr3clmesh::kHullGateMask) != 0) {
        // 0x00749ba0 -> 0x007a79f0 -> 0x0077a3e0: a real collision
        // hull, NOT the "builds a default, consumes nothing" reading Sec4.2
        // originally posed.
        b.resize(alignUp(b.size(), 16), 0x00);
        out.atHull = b.size();
        appendU32(b, opt.hullVertexCount);
        const size_t vertsAt = b.size();
        b.resize(vertsAt + opt.hullVertexCount * sr3clmesh::kHullVertexStride, 0x00);
        for (uint32_t i = 0; i < opt.hullVertexCount; ++i) {
            putF32At(b, vertsAt + i * 12 + 0, 1.0f + static_cast<float>(i));
            putF32At(b, vertsAt + i * 12 + 4, -2.5f * static_cast<float>(i));
            putF32At(b, vertsAt + i * 12 + 8, 7.25f);
        }
        b.resize(alignUp(b.size(), 4), 0x00);
        appendU32(b, opt.hullIndexCount);
        const size_t idxAt = b.size();
        b.resize(idxAt + opt.hullIndexCount * sr3clmesh::kHullIndexStride, 0x00);
        for (uint32_t i = 0; i < opt.hullIndexCount; ++i) {
            putU16At(b, idxAt + i * 2, static_cast<uint16_t>(100 + i));
        }
        b.resize(alignUp(b.size(), 16), 0x00);
        appendU32(b, opt.hullBlobLength);
        b.resize(alignUp(b.size(), 16), 0x00);
        b.insert(b.end(), opt.hullBlobLength, 0xB0);
        b.resize(alignUp(b.size(), 4), 0x00);
        b.resize(b.size() + 24, 0x00); // the two trailing 3 x float32 values
    } else if (opt.mutateSentinelBranchConsumesBytes) {
        // MUTATION: the third outcome made to consume bytes it does not.
        b.insert(b.end(), 8, 0x7E);
    }

    if (opt.count88 != 0) {
        b.resize(alignUp(b.size(), 16), 0x00);
        out.at88Outer = b.size();
        b.resize(b.size() + opt.count88 * sr3clmesh::kStride88Outer, 0x00);
        for (uint32_t i = 0; i < opt.count88; ++i) {
            const uint32_t inner = i < opt.inner88.size() ? opt.inner88[i] : 0;
            putU32At(b, out.at88Outer + i * sr3clmesh::kStride88Outer, inner);
        }
        // Sec4.2: "the inner region immediately following in the file is
        // innerCount consecutive 12-byte elements" - taken literally.
        for (uint32_t i = 0; i < opt.count88; ++i) {
            const uint32_t inner = i < opt.inner88.size() ? opt.inner88[i] : 0;
            b.insert(b.end(), inner * sr3clmesh::kStride88Inner, 0x88);
        }
    } else {
        maybeAlign16IfEmptyArraysAlign();
    }

    if (opt.count98 != 0) {
        b.resize(alignUp(b.size(), 16), 0x00);
        out.at98 = b.size();
        b.insert(b.end(), opt.count98 * sr3clmesh::kStride98, 0x98);
    } else {
        maybeAlign16IfEmptyArraysAlign();
    }

    if (opt.countA8 != 0) {
        b.resize(alignUp(b.size(), 16), 0x00);
        out.atA8 = b.size();
        b.insert(b.end(), opt.countA8 * sr3clmesh::kStrideA8, 0xA8);
    } else {
        maybeAlign16IfEmptyArraysAlign();
    }

    if (opt.countD8 != 0) {
        b.resize(alignUp(b.size(), 16), 0x00);
        out.atD8 = b.size();
        b.insert(b.end(), opt.countD8 * sr3clmesh::kStrideD8, 0xD8);
        if (opt.countE8 != 0 && !opt.mutateNoRealignBeforeE8) {
            b.resize(alignUp(b.size(), 16), 0x00);
        }
        out.atE8 = b.size();
        b.insert(b.end(), opt.countE8 * sr3clmesh::kStrideE8, 0xE8);
    } else {
        maybeAlign16IfEmptyArraysAlign();
    }

    // --- 0x00749e80's trailing nested group, which ends exactly at EOF. --
    b.resize(alignUp(b.size(), 8), 0x00);
    const size_t innerStride =
        opt.mutateTwelveByteTrailingInner ? sr3clmesh::kStride88Inner : sr3clmesh::kStrideFcInner;

    if (opt.mutateProseTrailingGroupOrder) {
        out.atFcParallel = b.size();
        b.resize(b.size() + opt.countFc * sr3clmesh::kStrideFcParallel, 0x00);
        out.atFcPrimary = b.size();
        b.resize(b.size() + opt.countFc * sr3clmesh::kStrideFcPrimary, 0x00);
    } else {
        out.atFcPrimary = b.size();
        b.resize(b.size() + opt.countFc * sr3clmesh::kStrideFcPrimary, 0x00);
        out.atFcParallel = b.size();
        b.resize(b.size() + opt.countFc * sr3clmesh::kStrideFcParallel, 0x00);
    }
    for (uint32_t i = 0; i < opt.countFc; ++i) {
        const uint32_t inner = i < opt.innerFc.size() ? opt.innerFc[i] : 0;
        putU32At(b, out.atFcParallel + i * sr3clmesh::kStrideFcParallel, inner);
    }
    out.atFcInner.clear();
    for (uint32_t i = 0; i < opt.countFc; ++i) {
        const uint32_t inner = i < opt.innerFc.size() ? opt.innerFc[i] : 0;
        b.resize(alignUp(b.size(), 8), 0x00);
        out.atFcInner.push_back(b.size());
        b.insert(b.end(), inner * innerStride, 0xFC);
    }
    return b;
}

// A minimal but real Mesh sub-block (version 9, g-backed, one position-only
// channel), laid out exactly as spec-vertex-format.md Sec2/Sec3/Sec4 give it.
// Appended at the CURRENT end of `c`, i.e. the caller places it.
void appendMeshSubBlock(std::vector<uint8_t>& c, std::vector<uint8_t>& g, uint32_t checkValue,
                        const std::vector<std::array<float, 3>>& positions) {
    const size_t anchor = c.size();
    // `roundUp8(anchor + 16) - anchor`, not an unconditional `0x10` - a
    // second (or later) call in a back-to-back chain lands `anchor` at
    // whatever the previous block's own byte size put it at, which is not
    // always 8-aligned. Mirrors `src/level_mesh.cpp`'s own fix (HANDOFF
    // Sec9.66) so this fixture can actually exercise the case that fix
    // covers, not just the always-aligned single-mesh case.
    const size_t headerAt = alignUp(anchor + 16, 8) - anchor;
    const size_t headerSize = 0x70;
    const size_t recordsAt = headerAt + headerSize;
    const uint32_t channelCount = 1;
    const size_t stride = 12; // layout code 4, position only

    std::vector<uint8_t> block(recordsAt + channelCount * 24, 0x00);
    putU32At(block, 0x00, 9);
    putU32At(block, 0x04, checkValue);
    block[headerAt + 0x00] = 0x01; // bulk in the g-file
    putU32At(block, headerAt + 0x10, channelCount);
    putU32At(block, headerAt + 0x20, 3); // index count
    block[headerAt + 0x30] = 2;
    putU32At(block, recordsAt + 0x00, static_cast<uint32_t>(positions.size()));
    block[recordsAt + 0x04] = static_cast<uint8_t>(stride);
    block[recordsAt + 0x05] = 4;
    block[recordsAt + 0x06] = 0;
    block[recordsAt + 0x07] = 0;

    // The true absolute start of THIS g-segment, i.e. exactly what the
    // resolver's own `gCursor` will be at this point (`resolveReferencedMeshes()`
    // is purely additive on the g-side too, no rounding - same comment
    // there). NOT 16-aligned: for a second-or-later mesh in a chain, the
    // previous segment's own g-length rarely lands on a 16-boundary, and
    // padding here would desync this fixture from what the resolver
    // predicts, exactly the gap that made a 2-mesh chain fail here until
    // this was found (HANDOFF Sec9.66).
    const size_t gStart = g.size();
    auto segAlign16 = [gStart](size_t cur) { return alignUp(gStart + cur, 16) - gStart; };

    std::vector<uint8_t> seg;
    appendU32(seg, checkValue);
    seg.resize(segAlign16(seg.size()), 0x00);
    for (int i = 0; i < 3; ++i) {
        seg.resize(seg.size() + 2);
        putU16At(seg, seg.size() - 2, static_cast<uint16_t>(i));
    }
    seg.resize(segAlign16(seg.size()), 0x00);
    const size_t channelAt = seg.size();
    seg.resize(seg.size() + positions.size() * stride, 0x00);
    for (size_t v = 0; v < positions.size(); ++v) {
        for (size_t k = 0; k < 3; ++k) {
            uint32_t bits = 0;
            std::memcpy(&bits, &positions[v][k], 4);
            putU32At(seg, channelAt + v * stride + k * 4, bits);
        }
    }
    seg.resize(alignUp(seg.size(), 4), 0x00);
    appendU32(seg, checkValue);

    // The c-side bookend 0x00e71410 checks: the block's own check value
    // repeated in its LAST 4 bytes (spec-physics-format.md Sec4.4.6(b)).
    block.resize(block.size() + 4, 0x00);
    putU32At(block, block.size() - 4, checkValue);

    putU32At(block, 0x08, static_cast<uint32_t>(block.size()));
    putU32At(block, 0x0C, static_cast<uint32_t>(seg.size()));

    c.insert(c.end(), block.begin(), block.end());
    g.resize(gStart, 0x00);
    g.insert(g.end(), seg.begin(), seg.end());
    (void)anchor;
}
