// Synthetic tests for the `.clmesh_pc` "Level_Mesh" reader (sr3clmesh).
//
// Every fixture here is built from the LAYOUT RULES, written out by hand -
// the shared material block's mandatory pad, the `0x4fe66afa` header's magic
// and exact version 20, and the per-sub-parser offsets/strides
// spec-physics-format.md Sec4.2 gives - never by calling into
// src/level_mesh.cpp. A fixture derived from the parser only proves the
// parser agrees with itself.
//
// Where the spec text and real shipped bytes disagree, these tests encode
// what the BYTES do and say so at the test that covers it; the divergences
// are itemised in include/sr3clmesh/level_mesh.h and HANDOFF Sec9.58.
//
// Every load-bearing structural rule gets a mutation case: revert the rule
// in the fixture and a specific check must fail. The rules under mutation
// here are
//   * the two head slot arrays are contiguous (no realign between them),
//   * an empty array consumes AND aligns nothing,
//   * `FUN_00749e80` DOES realign between its `+0xd8` and `+0xe8` arrays,
//   * the trailing group is primary-then-parallel, not the reverse,
//   * the trailing group's inner lists are 4-byte elements at 8-byte
//     alignment,
//   * the `+0x78` sentinel selects the branch that reads NO cursor bytes.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3clmesh/level_mesh.h"
#include "sr3geometry/material_block.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                                   \
    do {                                                                     \
        bool threw = false;                                                  \
        try { expr; }                                                        \
        catch (const std::exception&) { threw = true; }                      \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

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

// ==========================================================================
// Fixture builder
// ==========================================================================

struct ClmeshOptions {
    std::vector<std::string> materialNames = {"prop_a_d.tga", "prop_a_n.tga"};
    uint32_t version = sr3clmesh::kLevelMeshVersion;

    uint32_t countRefA = 0;
    uint32_t countRefB = 0;
    int32_t pairFirst = sr3clmesh::kPairSentinel;
    int32_t pairSecond = sr3clmesh::kPairSentinel;
    uint32_t count70 = 0;
    uint32_t count88 = 0;
    std::vector<uint32_t> inner88; // one per `+0x88` outer record
    uint32_t count98 = 0;
    uint32_t countA8 = 0;
    uint32_t countD8 = 0;
    uint32_t countE8 = 0;
    uint32_t countFc = 3;                        // 3 in every shipped file
    std::vector<uint32_t> innerFc = {1, 1, 1};   // one per trailing-group entry

    // The flags byte at header `+0x08`. Bit 2 (mask 0x04) is what selects
    // FUN_00749ba0's collision-hull parse when `+0x78` is the sentinel.
    uint32_t headerFlags = 0;

    // --- the HEAD's own payloads ------------------------------------------
    // Each count(+0x48) entry is an embedded Mesh sub-block; each
    // count(+0x58) entry is a FUN_00e401d0 index-list block laid down AFTER
    // all of them (spec-physics-format.md Sec4.4.6's correction to Sec4.4.1).
    uint32_t headIndexCount = 1;    // elements in each array-2 index list
    // When non-empty, the HEAD's Mesh sub-blocks are built as REAL g-backed
    // blocks (so sr3mesh::MeshBlock has something to resolve) instead of
    // c-side-only ones.
    std::vector<std::vector<std::array<float, 3>>> headRealMeshes;

    // --- the MIDDLE (thunk_FUN_00e3e590), built for real ------------------
    // Sec4.4.6(b): a 0x50-byte record, a material-set block, the
    // materialCount x 8 lookup table, then two render-mesh groups.
    // The name table's TOTAL byte length decides whether the leading-NUL
    // rule is observable at all: the very next step is an align-to-8, so a
    // one-byte shift is invisible unless the table ends on an 8-boundary.
    // These two names are 8 bytes each with their terminators, i.e. 16 - and
    // that is exactly why the same control only scores 29% on the shipped
    // population (Sec4.4.6(e): 71% still land without the NUL).
    std::vector<std::string> midMaterialNames = {"mat_aaa", "mat_bbb"};
    std::vector<uint32_t> midRecordSizes = {24, 40}; // each record's own `+0x00`
    uint32_t midIndexCountA = 1;    // render group 1's index list
    uint32_t midIndexCountB = 2;    // render group 2's
    std::vector<uint32_t> midGroupASub;  // R[+0x20] records, each count x 0x20
    std::vector<uint32_t> midGroupBSub;  // R[+0x30] records, each count x 4
    uint32_t midTrailer34 = 0;           // R[+0x48] x 0x34

    // FUN_00748c70's list block (non-sentinel `+0x78` only): its own declared
    // total byte size at `+0x08`.
    uint32_t gatedListBlockSize = 32;
    // FUN_00749ba0's collision hull (sentinel `+0x78` + flags bit 2 only).
    uint32_t hullVertexCount = 3;
    uint32_t hullIndexCount = 3;
    uint32_t hullBlobLength = 20;

    // Mutations. Each one reverts a measured rule back to what a naive
    // reading of the spec would produce; the reader must then stop landing
    // exactly on end-of-file.
    bool mutateRealignBetweenHeadArrays = false;
    bool mutateEmptyArraysAlign = false;
    bool mutateNoRealignBeforeE8 = false;
    bool mutateProseTrailingGroupOrder = false;
    bool mutateTwelveByteTrailingInner = false;
    bool mutateSentinelBranchConsumesBytes = false;
    bool mutateHeadNoIndexLists = false;      // Sec4.4.1's published (incomplete) head formula
    bool mutateMeshStepRoundUp16 = false;     // 16-align between consecutive Mesh sub-blocks
    bool mutateMiddleNoNulByte = false;       // name table without its leading NUL
    bool mutateMiddleOneRenderGroup = false;  // only one render group in the file
    bool mutateMiddleNoLookupArray = false;   // no materialCount x 8 array at R[+0x08]
};

// Offsets the builder hands back so tests can assert against absolute
// positions rather than recomputing the layout a second time.
struct ClmeshLayout {
    size_t headerAt = 0;
    size_t bodyAt = 0;
    size_t refAAt = 0;
    size_t refBAt = 0;
    size_t meshBlockStart = 0;
    std::vector<size_t> atHeadMesh;
    std::vector<size_t> atHeadIndexList;
    size_t headEnd = 0;
    size_t middleAt = 0, middleRecordAt = 0, matSetAt = 0, middleEnd = 0;
    std::vector<size_t> atRenderGroup;
    size_t atGatedBlock = 0, atHull = 0;
    size_t tailAt = 0;
    size_t at70 = 0, at88Outer = 0, at98 = 0, atA8 = 0, atD8 = 0, atE8 = 0;
    size_t atFcPrimary = 0, atFcParallel = 0;
    std::vector<size_t> atFcInner;
};

// A minimal but real Mesh sub-block (version 9, g-backed); defined below,
// used by the HEAD when a test wants sr3mesh::MeshBlock to have something
// real to resolve.
void appendMeshSubBlock(std::vector<uint8_t>& c, std::vector<uint8_t>& g, uint32_t checkValue,
                        const std::vector<std::array<float, 3>>& positions);

// The c-side-only form of the same block, which is all the walk itself ever
// looks at: FUN_00e71410 checks the version word 9, reads the check value and
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
    appendU32(b, checkValue); // the bookend FUN_00e71410 checks
    putU32At(b, at + 0x08, static_cast<uint32_t>(b.size() - at));
    return at;
}

// One FUN_00e401d0 index-list block: 8-align, a 0x10-byte header whose
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
                                 std::vector<uint8_t>* gOut = nullptr) {
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

    // --- MIDDLE: thunk_FUN_00e3e590, built for real (Sec4.4.6(b)). ---------
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

    // FUN_00e40210, the material-set block.
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
    // byte size at its `+0x00` (FUN_00e71090).
    b.resize(alignUp(b.size(), 8), 0x00);
    b.resize(b.size() + materialCount * sr3clmesh::kStrideMatSetSlot, 0x00);
    for (uint32_t size : opt.midRecordSizes) {
        const size_t at = b.size();
        b.resize(at + size, 0xA5);
        putU32At(b, at, size);
    }
    // R[+0x08]'s materialCount x 8 array - the table FUN_0074a110 indexes.
    if (!opt.mutateMiddleNoLookupArray) {
        b.resize(alignUp(b.size(), 8), 0x00);
        b.resize(b.size() + materialCount * sr3clmesh::kStrideLookupEntry, 0x00);
    }

    // FUN_00e3c6e0: two render-mesh groups, each a 0x60-byte header, an
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

    // --- TAIL. FUN_00749b40 16-aligns UNCONDITIONALLY on entry. ------------
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
        // FUN_00748c70 -> FUN_007616c0: 16-align, then a block that declares
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
        // FUN_00749ba0 -> FUN_007a79f0 -> FUN_0077a3e0: a real collision
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

    // --- FUN_00749e80's trailing nested group, which ends exactly at EOF. --
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

    // The c-side bookend FUN_00e71410 checks: the block's own check value
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

// ==========================================================================
// Part A: the fixed 0x140 header
// ==========================================================================

void testHeaderLocatedAfterMandatoryPad() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.headerOffset() == lay.headerAt);
    CHECK(m.version() == 20);
    CHECK(m.bodyStart() == lay.headerAt + 0x140);
    CHECK(m.bodyStart() == lay.bodyAt);
    // The pad really is a minimum gap, not a round-up: the material block in
    // this fixture ends unaligned, but the header still sits at
    // end/16*16 + 16 rather than at end rounded up.
    CHECK(m.headerOffset() % 16 == 0);
}

void testHeaderMandatoryPadWhenAlreadyAligned() {
    // Zero material names makes the block exactly 0x20 bytes - already
    // 16-aligned - and the header must STILL sit a full 16 further out.
    ClmeshOptions opt;
    opt.materialNames.clear();
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    CHECK(lay.headerAt == sr3geometry::kMaterialBlockHeaderSize + 16);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.headerOffset() == lay.headerAt);
}

void testHeaderRejections() {
    {   // wrong magic
        ClmeshOptions opt;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        putU32At(b, lay.headerAt + 0x00, 0x424BD00Du); // `.ccmesh_pc`'s magic
        CHECK_THROWS(sr3clmesh::LevelMesh::parse(view(b)));
    }
    {   // version 19 and 21 - the requirement is EXACTLY 20, not a range
        for (uint32_t v : {19u, 21u, 0u}) {
            ClmeshOptions opt;
            opt.version = v;
            ClmeshLayout lay;
            std::vector<uint8_t> b = buildClmesh(opt, lay);
            CHECK_THROWS(sr3clmesh::LevelMesh::parse(view(b)));
        }
    }
    {   // truncated before the fixed header ends
        ClmeshOptions opt;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        b.resize(lay.headerAt + 0x100);
        CHECK_THROWS(sr3clmesh::LevelMesh::parse(view(b)));
    }
    {   // parseAt pointed somewhere with no magic
        ClmeshOptions opt;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        CHECK_THROWS(sr3clmesh::LevelMesh::parseAt(view(b), lay.headerAt + 16));
    }
}

// ==========================================================================
// Part B: HEAD - FUN_007499d0's two slot arrays
// ==========================================================================

void testHeadTwoSlotArraysAreContiguous() {
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));

    CHECK(m.referenceArrayA().count == 1);
    CHECK(m.referenceArrayA().stride == 8);
    CHECK(m.referenceArrayB().count == 1);
    CHECK(m.referenceArrayA().offset == m.bodyStart());
    // The load-bearing bit: array B starts 8 bytes after array A, NOT at the
    // next 16-byte boundary.
    CHECK(m.referenceArrayB().offset == m.bodyStart() + 8);
    // The embedded Mesh sub-blocks start immediately after BOTH arrays...
    CHECK(m.meshBlockStart() == m.bodyStart() + 16);
    CHECK(m.meshBlockStart() == lay.meshBlockStart);
    CHECK(m.headMeshBlocks().size() == 1);
    // ... and array 2's index-list blocks come after those, not before.
    CHECK(m.headIndexLists().size() == 1);
    if (!m.headMeshBlocks().empty() && !m.headIndexLists().empty()) {
        CHECK(m.headIndexLists()[0].offset >= m.headMeshBlocks()[0].end);
        CHECK(m.headIndexLists()[0].offset == lay.atHeadIndexList[0]);
        CHECK(m.headIndexLists()[0].count == 1);
    }
    CHECK(m.headEnd() == lay.headEnd);
    CHECK(m.landsOnEof());
}

// Sec4.4.6's correction to Sec4.4.1: the published head formula
// (`8*count(+0x48) + 8*count(+0x58) + sum of Mesh sub-block sizes`) is
// INCOMPLETE - array 2's own FUN_00e401d0 index-list payloads follow every
// Mesh sub-block and this reader shipped without them. Building the file the
// way the published formula describes must cost the exact landing.
void testHeadMutationNoIndexListPayloads() {
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 2;
    opt.headIndexCount = 3;
    opt.mutateHeadNoIndexLists = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

// The same rule the other way round: with the payloads present, the walk
// lands, and each block's count comes out of its own `+0x08`.
void testHeadIndexListPayloadsAreWalked() {
    ClmeshOptions opt;
    opt.countRefA = 2;
    opt.countRefB = 2;
    opt.headIndexCount = 3;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.headMeshBlocks().size() == 2);
    CHECK(m.headIndexLists().size() == 2);
    if (m.headMeshBlocks().size() == 2) {
        // Consecutive Mesh sub-blocks abut with ZERO padding - the c-side
        // step is the declared c-length exactly.
        CHECK(m.headMeshBlocks()[1].offset == m.headMeshBlocks()[0].end);
        CHECK(m.headMeshBlocks()[0].offset == lay.atHeadMesh[0]);
        CHECK(m.headMeshBlocks()[1].offset == lay.atHeadMesh[1]);
    }
    if (m.headIndexLists().size() == 2) {
        CHECK(m.headIndexLists()[0].count == 3);
        CHECK(m.headIndexLists()[1].count == 3);
        CHECK(m.headIndexLists()[1].offset == alignUp(m.headIndexLists()[0].end, 8));
    }
}

// MUTATION of the same step: 16-align between consecutive Mesh sub-blocks,
// the natural guess that is wrong and that is SILENT on any file with only
// one block (HANDOFF Sec9.59 - it cost every reference after the first).
void testHeadMutationMeshStepRoundUp16() {
    ClmeshOptions opt;
    opt.countRefA = 2;
    opt.countRefB = 0;
    opt.mutateMeshStepRoundUp16 = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
    CHECK(m.headMeshBlocks().size() < 2);
}

void testHeadMutationRealignBetweenSlotArrays() {
    // MUTATION: build the file the way a per-array 16-byte align would. The
    // reader must then report a headEnd that does NOT match where the fixture
    // actually put the end of array B - the exact failure that hid a real bug
    // in this reader until the Mesh-sub-block resolution stopped working.
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 1;
    opt.mutateRealignBetweenHeadArrays = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(lay.refBAt == m.bodyStart() + 16); // the fixture really did realign
    CHECK(m.referenceArrayB().offset != lay.refBAt);
    CHECK(m.headEnd() != lay.headEnd);
}

void testHeadEmptyWhenBothCountsZero() {
    ClmeshOptions opt; // both counts default to 0
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.referenceArrayA().empty());
    CHECK(m.referenceArrayB().empty());
    CHECK(m.headEnd() == m.bodyStart());
}

// ==========================================================================
// Part C: TAIL - the exact end-of-file landing
// ==========================================================================

void testTailAllCountsZeroLandsExactly() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.groupFcPrimary().count == 3);
    CHECK(m.groupFcPrimary().stride == 8);
    CHECK(m.groupFcParallel().count == 3);
    CHECK(m.groupFcParallel().stride == 4);
    // Primary FIRST, parallel SECOND - the spec's own output-slot order, not
    // its prose order.
    CHECK(m.groupFcPrimary().offset < m.groupFcParallel().offset);
    CHECK(m.groupFcParallel().offset == m.groupFcPrimary().offset + 3 * 8);
    CHECK(m.groupFc().size() == 3);
    for (const sr3clmesh::NestedEntry& e : m.groupFc()) {
        CHECK(e.innerCount == 1);
        CHECK(e.innerStride == 4);
        CHECK(e.innerOffset % 8 == 0);
    }
}

void testTailFiftySixByteArray() {
    ClmeshOptions opt;
    opt.count70 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.array70().count == 1);
    CHECK(m.array70().stride == 56);
    CHECK(m.array70().offset == lay.at70);
    CHECK(m.array70().offset % 16 == 0);
    // 56 is 8 mod 16, so the empty arrays that follow leave the cursor where
    // the 56-byte array ended - the "empty aligns nothing" rule, live.
    CHECK(m.groupFcPrimary().offset == lay.at70 + 56);
    // The array's bytes come back raw, uninterpreted.
    vpp::ByteView raw = m.arrayBytes(m.array70(), view(b));
    CHECK(raw.size() == 56);
    CHECK(raw.at(0) == 0x70);
}

void testTailMutationEmptyArraysAlign() {
    // MUTATION: the fixture pads every empty array up to 16, which is what
    // Sec4.1's align-before-count-read would imply. The reader must then fail
    // to land on EOF from the real tail start.
    ClmeshOptions opt;
    opt.count70 = 1;
    opt.mutateEmptyArraysAlign = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailFiftyTwoAndNinetySixByteArrays() {
    ClmeshOptions opt;
    opt.count98 = 2;
    opt.countA8 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.array98().count == 2);
    CHECK(m.array98().stride == 52);
    CHECK(m.array98().offset == lay.at98);
    CHECK(m.arrayA8().count == 1);
    CHECK(m.arrayA8().stride == 96);
    CHECK(m.arrayA8().offset == lay.atA8);
    CHECK(m.arrayA8().offset % 16 == 0);
}

void testTailNestedGroupAtEightyEight() {
    ClmeshOptions opt;
    opt.count88 = 2;
    opt.inner88 = {3, 1};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.group88Outer().count == 2);
    CHECK(m.group88Outer().stride == 16);
    CHECK(m.group88Outer().offset == lay.at88Outer);
    CHECK(m.group88().size() == 2);
    if (m.group88().size() == 2) {
        CHECK(m.group88()[0].innerCount == 3);
        CHECK(m.group88()[0].innerStride == 12);
        CHECK(m.group88()[1].innerCount == 1);
        // Inner lists follow the whole outer array, in order, back to back.
        CHECK(m.group88()[0].innerOffset == lay.at88Outer + 2 * 16);
        CHECK(m.group88()[1].innerOffset == m.group88()[0].innerOffset + 3 * 12);
    }
}

// HANDOFF Sec9.60: the `+0x88` group's 12-byte inner element decodes as a
// little-endian float32 (x,y,z) triple - CONFIRMED against the whole
// shipped population, not merely a guess. This pins the load-bearing rule:
// slot order (x=bytes 0-3, y=bytes 4-7, z=bytes 8-11) and little-endian
// float decoding, exactly the shape a real closed loop like
// `fountain_a_large_01`'s 25-point circle needs to reproduce.
void testGroup88PositionsDecodeAsFloatTriples() {
    ClmeshOptions opt;
    opt.count88 = 1;
    opt.inner88 = {3};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.group88().size() == 1);
    if (m.group88().size() != 1) return;
    const sr3clmesh::NestedEntry& e = m.group88()[0];
    CHECK(e.innerCount == 3);
    // Overwrite the 0x88 filler bytes buildClmesh() wrote with a known
    // triangle-ish point set - two points sharing x (a real degenerate-edge
    // shape this project's own population actually ships, see
    // int_hwy_2wayb~l1 in HANDOFF Sec9.60), one point negative on every axis.
    putF32At(b, e.innerOffset + 0 * 12 + 0, 11.3937f);
    putF32At(b, e.innerOffset + 0 * 12 + 4, -1.70738f);
    putF32At(b, e.innerOffset + 0 * 12 + 8, -13.7225f);
    putF32At(b, e.innerOffset + 1 * 12 + 0, 11.3937f);
    putF32At(b, e.innerOffset + 1 * 12 + 4, -1.73575f);
    putF32At(b, e.innerOffset + 1 * 12 + 8, -0.650024f);
    putF32At(b, e.innerOffset + 2 * 12 + 0, -5.2727f);
    putF32At(b, e.innerOffset + 2 * 12 + 4, -0.0055f);
    putF32At(b, e.innerOffset + 2 * 12 + 8, -0.0f);

    std::vector<sr3clmesh::Vec3> pts = m.group88Positions(e, view(b));
    CHECK(pts.size() == 3);
    if (pts.size() != 3) return;
    CHECK(pts[0].x == 11.3937f && pts[0].y == -1.70738f && pts[0].z == -13.7225f);
    CHECK(pts[1].x == 11.3937f && pts[1].y == -1.73575f && pts[1].z == -0.650024f);
    CHECK(pts[2].x == -5.2727f && pts[2].y == -0.0055f);
    // Deliberately-wrong-value check: a slot-swapped read (z where y should
    // be) must NOT match - proves the test can actually catch a field-order
    // bug rather than passing regardless of it.
    CHECK(!(pts[0].y == -13.7225f));
}

// Mutation case: group88() and groupFc() share NestedEntry but have
// different inner strides (12 vs 4). Handing a groupFc()-shaped entry to
// group88Positions() must throw rather than silently misdecode - the same
// "wrong stride is a real bug, not a shrug" discipline the rest of this
// reader already applies (kStride88Inner vs kStrideFcInner never used
// interchangeably in src/level_mesh.cpp).
void testGroup88PositionsMutationWrongStrideThrows() {
    ClmeshOptions opt;
    opt.count88 = 1;
    opt.inner88 = {2};
    opt.countFc = 3;
    // group 0 has plenty of REAL bytes after it (groups 1 and 2 together
    // contribute 8 more 4-byte elements) so that a wrongly-strided 12-byte
    // read starting at group 0's inner offset lands entirely inside the
    // buffer instead of running past EOF. That matters: an earlier version
    // of this test used {1, 0, 0} here, where group 0 sits right at the
    // tail's real end - the wrong-stride read then ran past EOF and
    // ByteView's own bounds check threw first, which made the test pass
    // for the WRONG reason (an accidental out-of-range read, not the
    // explicit stride guard group88Positions() is supposed to have) -
    // caught by temporarily deleting that guard and confirming the test
    // still (wrongly) reported [ok]. This shape closes that gap: with the
    // guard removed, the call below now returns silently instead of
    // throwing, and CHECK_THROWS genuinely fails, proving the guard - not
    // an incidental bounds check - is what this test exercises.
    opt.innerFc = {1, 4, 4};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.group88().size() == 1);
    CHECK(m.groupFc().size() == 3);
    if (m.group88().empty() || m.groupFc().empty()) return;
    // The real group88 entry decodes fine...
    (void)m.group88Positions(m.group88()[0], view(b));
    // ...but a groupFc entry (4-byte inner stride) must be rejected, not
    // silently read as if it were a group88 (12-byte stride) entry.
    CHECK_THROWS(m.group88Positions(m.groupFc()[0], view(b)));
}

void testTailDEightAndEEightPairRealigns() {
    // count(+0xd8) = 3 leaves the cursor 24 bytes in, i.e. 8 mod 16, so the
    // realign before the `+0xe8` array is observable here.
    ClmeshOptions opt;
    opt.countD8 = 3;
    opt.countE8 = 2;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.arrayD8().count == 3);
    CHECK(m.arrayD8().stride == 8);
    CHECK(m.arrayE8().count == 2);
    CHECK(m.arrayE8().stride == 16);
    CHECK(m.arrayD8().offset % 16 == 0);
    CHECK(m.arrayE8().offset == alignUp(m.arrayD8().offset + 24, 16));
    CHECK(m.arrayE8().offset == m.arrayD8().offset + 32); // NOT +24
}

void testTailMutationNoRealignBeforeEEight() {
    ClmeshOptions opt;
    opt.countD8 = 3;
    opt.countE8 = 2;
    opt.mutateNoRealignBeforeE8 = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailEEightGatedOnDEightNotItsOwnCount() {
    // Sec4.2: when count(+0xd8) is 0 the function skips straight to its final
    // section, so a non-zero count(+0xe8) must be ignored entirely.
    ClmeshOptions opt;
    opt.countD8 = 0;
    opt.countE8 = 4; // present in the header, absent from the file
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.arrayD8().empty());
    CHECK(m.arrayE8().empty());
}

void testTailMutationProseTrailingGroupOrder() {
    ClmeshOptions opt;
    opt.innerFc = {5, 2, 7}; // unequal, so the 12-byte offset really matters
    opt.mutateProseTrailingGroupOrder = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailMutationTwelveByteTrailingInnerElements() {
    ClmeshOptions opt;
    opt.innerFc = {5, 2, 7};
    opt.mutateTwelveByteTrailingInner = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailVariableInnerCountsAndEightAlignment() {
    ClmeshOptions opt;
    opt.innerFc = {5, 2, 7}; // 20, 8 and 28 bytes -> two of the three need a pad
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.groupFc().size() == 3);
    if (m.groupFc().size() == 3) {
        CHECK(m.groupFc()[0].innerCount == 5);
        CHECK(m.groupFc()[1].innerCount == 2);
        CHECK(m.groupFc()[2].innerCount == 7);
        CHECK(m.groupFc()[0].innerOffset == lay.atFcInner[0]);
        CHECK(m.groupFc()[1].innerOffset == lay.atFcInner[1]);
        CHECK(m.groupFc()[2].innerOffset == lay.atFcInner[2]);
        CHECK(m.groupFc()[1].innerOffset == alignUp(m.groupFc()[0].innerOffset + 20, 8));
    }
}

// ==========================================================================
// Part D: the gated `+0x78`/`+0x7c` pair
// ==========================================================================

void testGatedPairSentinelWithFlagBitClearConsumesNothing() {
    // The THIRD outcome (Sec4.4.6(f)): sentinel at `+0x78` and the flags
    // byte's bit 2 clear means nothing happens at all - not even a 16-byte
    // alignment. Asserting an alignment here costs 24 of the 4,232 DLC
    // entries their exact landing, so this is measured, not assumed.
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.headerFlags = 0;
    opt.count70 = 1; // 56 bytes is 8 mod 16, so an alignment WOULD be visible
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.pairIsSentinel());
    CHECK(m.pairFirst() == -1);
    CHECK(m.pairSecond() == -1);
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::None);
    CHECK(m.gatedPair().length == 0);
    CHECK(m.groupFcPrimary().offset == m.array70().offset + 56);
}

void testGatedPairRealValues() {
    {   // one value present
        ClmeshOptions opt;
        opt.pairFirst = 1;
        opt.pairSecond = -1;
        opt.gatedListBlockSize = 48;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
        CHECK(!m.pairIsSentinel());
        CHECK(m.pairFirst() == 1);
        CHECK(m.pairSecond() == -1);
        CHECK(m.landsOnEof());
        // Sec4.4.6(f): the cost is the block's OWN declared size at `+0x08`,
        // per file, not a constant.
        CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::ListBlock);
        CHECK(m.gatedPair().offset == lay.atGatedBlock);
        CHECK(m.gatedPair().blockSize == 48);
        CHECK(m.gatedPair().blockCount == 1);
    }
    {   // two values present - Sec4.2's "validity requires the returned count
        // to be under 3"
        ClmeshOptions opt;
        opt.pairFirst = 2;
        opt.pairSecond = 3;
        opt.gatedListBlockSize = 80;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
        CHECK(!m.pairIsSentinel());
        CHECK(m.pairFirst() == 2);
        CHECK(m.pairSecond() == 3);
        CHECK(m.landsOnEof());
        CHECK(m.gatedPair().blockSize == 80);
        CHECK(m.gatedPair().blockCount == 2);
    }
}

// MUTATION of the same term: the reader told to cost the list block at ZERO
// bytes - exactly the `kGatedPairBytes = 0` placeholder this reader shipped
// before Sec4.4.6(f) replaced it with the block's own size field. On the
// shipped population that placeholder costs 11.1% of all landings.
void testGatedPairMutationListBlockCostsZero() {
    ClmeshOptions opt;
    opt.pairFirst = 1;
    opt.pairSecond = -1;
    opt.gatedListBlockSize = 48;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::WalkOptions o;
    o.gatedPairZeroCost = true;
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b), o);
    CHECK(!m.landsOnEof());
}

// Sec4.4.6(f)'s REFUTATION of Sec4.2's `FUN_00749ba0` row: the sentinel
// branch does not build a default out of two hard-coded constants, it parses
// a real file-resident collision hull - a vertex list, a u16 index list, an
// opaque blob and two vec3s - in 18,706 of the 100,384 shipped entries.
void testGatedPairCollisionHull() {
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.headerFlags = sr3clmesh::kHullGateMask; // bit 2 of the flags byte
    opt.hullVertexCount = 4;
    opt.hullIndexCount = 6;
    opt.hullBlobLength = 28;
    opt.count70 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.pairIsSentinel());
    CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::CollisionHull);
    CHECK(m.gatedPair().offset == lay.atHull);
    const sr3clmesh::CollisionHull& h = m.gatedPair().hull;
    CHECK(h.vertexCount == 4);
    CHECK(h.indexCount == 6);
    CHECK(h.blobLength == 28);
    CHECK(h.blobOffset % 16 == 0);
    CHECK(h.vec3bOffset == h.vec3aOffset + 12);

    std::vector<sr3clmesh::Vec3> verts = m.hullVertices(view(b));
    CHECK(verts.size() == 4);
    if (verts.size() == 4) {
        CHECK(verts[0].x == 1.0f && verts[0].y == 0.0f && verts[0].z == 7.25f);
        CHECK(verts[3].x == 4.0f && verts[3].y == -7.5f);
        // Deliberately-wrong-value check: a slot swap must NOT match.
        CHECK(!(verts[3].y == 4.0f));
    }
    std::vector<uint16_t> idx = m.hullIndices(view(b));
    CHECK(idx.size() == 6);
    if (idx.size() == 6) {
        CHECK(idx[0] == 100 && idx[5] == 105);
    }
}

// The flags bit is the whole gate: the same file with bit 2 CLEAR must be
// read as consuming nothing, which (the hull bytes still being present) must
// then miss end-of-file. This is what makes kHullGateMask load-bearing rather
// than decorative - and the mask itself was settled by measurement, mask 0x04
// landing 4,232/4,232 DLC entries where mask 0x02 landed 3,436.
void testGatedPairMutationHullGateBitCleared() {
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.headerFlags = sr3clmesh::kHullGateMask;
    opt.hullVertexCount = 4;
    opt.hullIndexCount = 6;
    opt.hullBlobLength = 28;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    // Clear the bit in the built file's header, leaving the hull bytes there.
    putU32At(b, lay.headerAt + sr3clmesh::kFlagsByte, 0);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
    CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::None);
}

void testGatedPairMutationSentinelBranchConsumesBytes() {
    // MUTATION: the branch order read backwards - the sentinel case made to
    // consume cursor bytes. The tail must then miss its landing.
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.count70 = 1;
    opt.mutateSentinelBranchConsumesBytes = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

// ==========================================================================
// Part E: the MeshBlock reuse and FUN_0074a110's finalization
// ==========================================================================

void testResolvedReferenceIsAMeshSubBlock() {
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 1;
    // A REAL g-backed Mesh sub-block in the HEAD, so sr3mesh::MeshBlock has
    // something to resolve rather than a c-side-only stand-in.
    opt.headRealMeshes = {{{{-2.0f, -4.0f, -6.0f}}, {{2.0f, 4.0f, 6.0f}}, {{0.0f, 0.0f, 0.0f}}}};
    ClmeshLayout lay;
    std::vector<uint8_t> g;
    std::vector<uint8_t> c = buildClmesh(opt, lay, &g);

    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(c));
    CHECK(m.landsOnEof());
    CHECK(m.headEnd() == lay.headEnd);
    CHECK(m.meshBlockStart() == lay.meshBlockStart);
    CHECK(view(c).readU32LE(m.meshBlockStart()) == sr3mesh::kMeshVersion);

    std::vector<sr3mesh::MeshBlock> meshes = m.resolveReferencedMeshes(view(c), view(g));
    CHECK(meshes.size() == 1);
    if (meshes.size() == 1) {
        CHECK(meshes[0].checkValue() == 0xC0FFEE11u);
        CHECK(meshes[0].channels().size() == 1);
        CHECK(meshes[0].bulkInGFile());

        sr3clmesh::BoundingVolume bv = sr3clmesh::computeBoundingVolume(meshes[0]);
        CHECK(bv.valid);
        CHECK(bv.vertexCount == 3);
        CHECK(std::fabs(bv.min[0] - (-2.0f)) < 1e-6f);
        CHECK(std::fabs(bv.max[2] - 6.0f) < 1e-6f);
        CHECK(std::fabs(bv.halfExtent[0] - 2.0f) < 1e-6f);
        CHECK(std::fabs(bv.halfExtent[1] - 4.0f) < 1e-6f);
        CHECK(std::fabs(bv.halfExtent[2] - 6.0f) < 1e-6f);
        CHECK(std::fabs(bv.center[0]) < 1e-6f);
        // sqrt(4 + 16 + 36) = sqrt(56)
        CHECK(std::fabs(bv.radius - std::sqrt(56.0f)) < 1e-5f);
    }
}

void testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock() {
    // Real bug, found and fixed 2026-09-14 (HANDOFF Sec9.66): a single
    // bulk-in-g Mesh sub-block's own `cLength` (156 = 0x9C here) is not a
    // multiple of 8, so a SECOND back-to-back reference lands its own
    // pre-header quad ending on a `cursor % 8 == 4` position - one more
    // conditional align8 pad is needed before its header proper (flags,
    // channelCount, ...) begins, exactly the rule already established and
    // measured for `.gzn_pc` in `src/zone_geometry.cpp`. Before the fix,
    // `resolveReferencedMeshes()` used an unconditional `+0x10` and misread
    // the second block's flags/channelCount as zero, then failed either the
    // bulk-segment-length or check-value test - real, named files
    // (`hotel_complex`, `bank_floor4_destroy`) hit exactly this.
    ClmeshOptions opt;
    opt.countRefA = 2;
    opt.countRefB = 0;
    opt.headRealMeshes = {
        {{{-1.0f, -1.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}},
        {{{-5.0f, -5.0f, -5.0f}}, {{5.0f, 5.0f, 5.0f}}, {{0.0f, 0.0f, 0.0f}}},
    };
    ClmeshLayout lay;
    std::vector<uint8_t> g;
    std::vector<uint8_t> c = buildClmesh(opt, lay, &g);
    CHECK(lay.atHeadMesh.size() == 2);
    // The whole point of this fixture: block 2 must actually land on a
    // non-8-aligned cursor, or this test cannot distinguish the fix from
    // the bug it replaces.
    CHECK(lay.atHeadMesh[1] % 8 != 0);

    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(c));
    CHECK(m.landsOnEof());

    std::vector<sr3mesh::MeshBlock> meshes = m.resolveReferencedMeshes(view(c), view(g));
    CHECK(meshes.size() == 2);
    if (meshes.size() == 2) {
        CHECK(meshes[0].checkValue() == 0xC0FFEE11u);
        CHECK(meshes[1].checkValue() == 0xC0FFEE12u);
        CHECK(meshes[0].bulkInGFile());
        CHECK(meshes[1].bulkInGFile());
        CHECK(meshes[0].channels().size() == 1);
        CHECK(meshes[1].channels().size() == 1);

        sr3clmesh::BoundingVolume bv1 = sr3clmesh::computeBoundingVolume(meshes[1]);
        CHECK(bv1.valid);
        CHECK(bv1.vertexCount == 3);
        CHECK(std::fabs(bv1.min[0] - (-5.0f)) < 1e-6f);
        CHECK(std::fabs(bv1.max[0] - 5.0f) < 1e-6f);
    }
}

void testResolvedReferencesEmptyWhenCountIsZero() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    std::vector<uint8_t> g;
    CHECK(m.resolveReferencedMeshes(view(b), view(g)).empty());
}

void testFinalizationArithmetic() {
    // FUN_0074a110's three outputs, hand-computed, with no Mesh sub-block in
    // the way. Half-extent (max-min)/2, centre min+half-extent, radius the
    // length of the half-extent vector.
    sr3clmesh::BoundingVolume bv =
        sr3clmesh::boundingVolumeFromExtents({{1.0f, -3.0f, 10.0f}}, {{7.0f, 5.0f, 10.0f}});
    CHECK(bv.valid);
    CHECK(std::fabs(bv.halfExtent[0] - 3.0f) < 1e-6f);
    CHECK(std::fabs(bv.halfExtent[1] - 4.0f) < 1e-6f);
    CHECK(std::fabs(bv.halfExtent[2] - 0.0f) < 1e-6f);
    CHECK(std::fabs(bv.center[0] - 4.0f) < 1e-6f);
    CHECK(std::fabs(bv.center[1] - 1.0f) < 1e-6f);
    CHECK(std::fabs(bv.center[2] - 10.0f) < 1e-6f);
    CHECK(std::fabs(bv.radius - 5.0f) < 1e-6f); // 3-4-5
}

// ==========================================================================
// Part F: things the reader must NOT decode
// ==========================================================================

void testOpenArraysStayOpaque() {
    ClmeshOptions opt;
    opt.count70 = 1;
    opt.count98 = 1;
    opt.countA8 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    // All three are exposed as byte ranges of exactly count x stride, with
    // no field decoding at all - spec-physics-format.md Sec5 lists every one
    // of them as OPEN / UNKNOWN.
    CHECK(m.arrayBytes(m.array70(), view(b)).size() == 56);
    CHECK(m.arrayBytes(m.array98(), view(b)).size() == 52);
    CHECK(m.arrayBytes(m.arrayA8(), view(b)).size() == 96);
    CHECK(m.arrayBytes(m.array98(), view(b)).at(0) == 0x98);
    CHECK(m.arrayBytes(m.arrayA8(), view(b)).at(95) == 0xA8);
}

// ==========================================================================
// Part G: the MIDDLE - computed, never searched (Sec4.4.6)
// ==========================================================================

void testMiddleIsComputedNotSearched() {
    ClmeshOptions opt;
    opt.midMaterialNames = {"road_a_d.tga", "road_a_n.tga", "road_a_s.tga"};
    opt.midRecordSizes = {24, 40, 16};
    opt.midIndexCountA = 2;
    opt.midIndexCountB = 5;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    // The MIDDLE begins at the 16-aligned end of the HEAD, and the TAIL
    // begins exactly where the MIDDLE ends - one cursor, no gap, no search.
    CHECK(m.middleOffset() == alignUp(m.headEnd(), 16));
    CHECK(m.middleOffset() == lay.middleAt);
    CHECK(m.middleEnd() == lay.middleEnd);
    CHECK(m.tailStart() == m.middleEnd());
    CHECK(m.middleLength() == m.middleEnd() - m.middleOffset());

    const sr3clmesh::MiddleLayout& mid = m.middle();
    CHECK(mid.ok);
    CHECK(!mid.aborted);
    CHECK(mid.recordOffset == lay.middleRecordAt);
    CHECK(mid.materialSetOffset == lay.matSetAt);
    CHECK(mid.materialCount == 3);
    CHECK(mid.materialRecordOffsets.size() == 3);
    CHECK(mid.materialSlots.count == 3 && mid.materialSlots.stride == 8);
    CHECK(mid.lookupTable.count == 3 && mid.lookupTable.stride == 8);
    // Every shipped file has exactly two render-mesh groups.
    CHECK(mid.renderGroupCount == 2);
    CHECK(mid.renderGroups[0].offset == lay.atRenderGroup[0]);
    CHECK(mid.renderGroups[1].offset == lay.atRenderGroup[1]);
    CHECK(mid.renderGroups[0].indexList.count == 2);
    CHECK(mid.renderGroups[1].indexList.count == 5);
    CHECK(mid.renderGroups[0].mesh.checkValue == 0xA1A1A1A1u);
    CHECK(mid.renderGroups[1].mesh.checkValue == 0xB2B2B2B2u);
    // The u16 array then the u8 array, both indexList.count long, with no
    // alignment between them.
    CHECK(mid.renderGroups[1].u8ArrayOffset == mid.renderGroups[1].u16ArrayOffset + 5 * 2);
}

void testMiddleTrailingRecordArraysAndTheirAsymmetry() {
    // The one place the two 0x10-byte record arrays differ: group 1 holds its
    // per-record count at `+0x08`, group 2 at `+0x00`. Unequal counts and
    // unequal sub-strides (0x20 vs 4) make a swap observable.
    ClmeshOptions opt;
    opt.midGroupASub = {2, 1};
    opt.midGroupBSub = {3};
    opt.midTrailer34 = 2;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    const sr3clmesh::MiddleLayout& mid = m.middle();
    CHECK(mid.groupARecords.count == 2 && mid.groupARecords.stride == 0x10);
    CHECK(mid.groupBRecords.count == 1 && mid.groupBRecords.stride == 0x10);
    CHECK(mid.trailer34.count == 2 && mid.trailer34.stride == 0x34);
    CHECK(mid.trailer34.offset % 16 == 0);
}

void testMiddleRecordContentDecoding() {
    // Sec4.4.6(h): materialRecords()/trailerRecords()/groupAFeatureIndices()/
    // groupBFeatureIndices() decode real content, not just locate byte
    // ranges. One material record sized exactly large enough to hold its
    // real 0x30-byte header (0x38 = 4-byte size prefix + up to 4-byte pad to
    // the next 8-boundary + 0x30 header, so the header always fits inside
    // the record's own declared size regardless of alignment phase), two
    // group-1 sub-records, three group-2 inner elements, and two trailer
    // records - each poked with a distinct, recognizable value at exactly
    // the offset Sec4.4.6(h) confirms, everything else left at the
    // fixture's usual 0xA5/0x00 filler so a reader that read the wrong
    // offset would decode filler instead and fail the exact-value check.
    ClmeshOptions opt;
    opt.midMaterialNames = {"single_mat"};
    opt.midRecordSizes = {0x38};
    opt.midGroupASub = {2};
    opt.midGroupBSub = {3};
    opt.midTrailer34 = 2;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh located = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(located.landsOnEof());
    const sr3clmesh::MiddleLayout& mid = located.middle();
    CHECK(mid.materialRecordOffsets.size() == 1);
    CHECK(mid.groupADetail.size() == 1 && mid.groupADetail[0].innerCount == 2);
    CHECK(mid.groupBDetail.size() == 1 && mid.groupBDetail[0].innerCount == 3);
    CHECK(mid.trailer34.count == 2);

    // Poke known values at the exact offsets the walk just located - none of
    // this changes any length/count field, so the walk's own result above
    // stays valid and re-parsing below sees the identical layout.
    const size_t matHeader = alignUp(mid.materialRecordOffsets[0] + sr3clmesh::kMatRecordSizePrefix, 8);
    CHECK(matHeader + sr3clmesh::kMatRecordHeaderSize <= mid.materialRecordOffsets[0] + 0x38);
    putU32At(b, matHeader + sr3clmesh::kMatRecordHash0, 0x11111111u);
    putU32At(b, matHeader + sr3clmesh::kMatRecordHash1, 0x22222222u);
    b[matHeader + sr3clmesh::kMatRecordFlags] = 0x77;
    putU16At(b, matHeader + sr3clmesh::kMatRecordTexCount, 9);
    b[matHeader + sr3clmesh::kMatRecordConstCount] = 5;
    b[matHeader + sr3clmesh::kMatRecordVec4Count] = 3;

    const sr3clmesh::NestedEntry& gaEntry = mid.groupADetail[0];
    putU16At(b, gaEntry.innerOffset + 0 * gaEntry.innerStride + sr3clmesh::kGroupASubFeatureIndex,
             0x1234);
    putU16At(b, gaEntry.innerOffset + 1 * gaEntry.innerStride + sr3clmesh::kGroupASubFeatureIndex,
             0x5678);

    const sr3clmesh::NestedEntry& gbEntry = mid.groupBDetail[0];
    putU16At(b, gbEntry.innerOffset + 0 * gbEntry.innerStride + sr3clmesh::kGroupBInnerFeatureIndex, 11);
    putU16At(b, gbEntry.innerOffset + 1 * gbEntry.innerStride + sr3clmesh::kGroupBInnerFeatureIndex, 22);
    putU16At(b, gbEntry.innerOffset + 2 * gbEntry.innerStride + sr3clmesh::kGroupBInnerFeatureIndex, 33);

    const size_t tr0 = mid.trailer34.offset + 0 * mid.trailer34.stride;
    const size_t tr1 = mid.trailer34.offset + 1 * mid.trailer34.stride;
    putU32At(b, tr0 + sr3clmesh::kTrailerMaterialSlotRaw, 7);
    b[tr0 + sr3clmesh::kTrailerLodByte] = 0x04; // active
    putU32At(b, tr1 + sr3clmesh::kTrailerMaterialSlotRaw, 9);
    b[tr1 + sr3clmesh::kTrailerLodByte] = 0x02; // NOT active - bit 0x04 clear

    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());

    std::vector<sr3clmesh::MaterialRecord> mats = m.materialRecords(view(b));
    CHECK(mats.size() == 1);
    CHECK(mats[0].hash0 == 0x11111111u);
    CHECK(mats[0].hash1 == 0x22222222u);
    CHECK(mats[0].flags == 0x77);
    CHECK(mats[0].textureBindingCount == 9);
    CHECK(mats[0].constantNameCount == 5);
    CHECK(mats[0].vec4ConstantCount == 3);

    std::vector<uint16_t> gaIdx = m.groupAFeatureIndices(m.middle().groupADetail[0], view(b));
    CHECK(gaIdx.size() == 2 && gaIdx[0] == 0x1234 && gaIdx[1] == 0x5678);

    std::vector<uint16_t> gbIdx = m.groupBFeatureIndices(m.middle().groupBDetail[0], view(b));
    CHECK(gbIdx.size() == 3 && gbIdx[0] == 11 && gbIdx[1] == 22 && gbIdx[2] == 33);

    std::vector<sr3clmesh::TrailerRecord> trs = m.trailerRecords(view(b));
    CHECK(trs.size() == 2);
    CHECK(trs[0].materialSlotRaw == 7 && trs[0].lodActive());
    CHECK(trs[1].materialSlotRaw == 9 && !trs[1].lodActive());
}

void testGroupFeatureIndicesMutationWrongStrideThrows() {
    // MUTATION: calling groupAFeatureIndices() on a groupBDetail entry (or
    // vice versa) must throw, not silently misread a 4-byte stride as a
    // 0x20-byte one or vice versa - same discipline as
    // testGroup88PositionsMutationWrongStrideThrows.
    ClmeshOptions opt;
    opt.midGroupASub = {2};
    opt.midGroupBSub = {3};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK_THROWS(m.groupAFeatureIndices(m.middle().groupBDetail[0], view(b)));
    CHECK_THROWS(m.groupBFeatureIndices(m.middle().groupADetail[0], view(b)));
}

void testMiddleMutationNameTableNulByte() {
    // MUTATION: the material-set name table laid down WITHOUT the single
    // leading NUL byte. On the shipped population this term is worth 29% of
    // the landings on its own (Sec4.4.6(e)).
    ClmeshOptions opt;
    opt.mutateMiddleNoNulByte = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

void testMiddleMutationOneRenderGroup() {
    ClmeshOptions opt;
    opt.mutateMiddleOneRenderGroup = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

void testMiddleMutationNoLookupArray() {
    // MUTATION: the materialCount x 8 array at R[+0x08] left out of the file.
    // This is FUN_0074a110's lookup table, and it is a real cursor cost.
    ClmeshOptions opt;
    opt.mutateMiddleNoLookupArray = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

void testMiddleControlsAreScorableOneTermAtATime() {
    // The same three terms driven from the READER's side rather than the
    // fixture's: a correct file, walked with one term disabled, must miss.
    // This is what tools/validation/validate_clmesh.cpp scores per term.
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh good = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(good.landsOnEof());
    for (int term = 0; term < 4; ++term) {
        sr3clmesh::WalkOptions o;
        if (term == 0) o.nameTableNulByte = false;
        if (term == 1) o.secondRenderGroup = false;
        if (term == 2) o.lookupTableArray = false;
        if (term == 3) o.meshStepRoundUp16 = true;
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b), o);
        CHECK(!m.landsOnEof());
    }
    // ... and a MIDDLE started 4 bytes late must miss too.
    sr3clmesh::MiddleLayout shifted =
        sr3clmesh::walkMiddle(view(b), good.headEnd() + 4, b.size());
    CHECK(!(shifted.ok && shifted.end == good.middleEnd()));
}

void testHeaderFieldAccessorRejectsOutOfRange() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.headerField(sr3clmesh::kCountFc) == 3);
    CHECK_THROWS(m.headerField(0x140));
    CHECK_THROWS(m.headerField(0x49));
}

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    std::cout << (g_failures == before ? "[ok]   " : "[FAIL] ") << name << "\n";
}

} // namespace

int main() {
    run("testHeaderLocatedAfterMandatoryPad", testHeaderLocatedAfterMandatoryPad);
    run("testHeaderMandatoryPadWhenAlreadyAligned", testHeaderMandatoryPadWhenAlreadyAligned);
    run("testHeaderRejections", testHeaderRejections);

    run("testHeadTwoSlotArraysAreContiguous", testHeadTwoSlotArraysAreContiguous);
    run("testHeadMutationNoIndexListPayloads", testHeadMutationNoIndexListPayloads);
    run("testHeadIndexListPayloadsAreWalked", testHeadIndexListPayloadsAreWalked);
    run("testHeadMutationMeshStepRoundUp16", testHeadMutationMeshStepRoundUp16);
    run("testHeadMutationRealignBetweenSlotArrays", testHeadMutationRealignBetweenSlotArrays);
    run("testHeadEmptyWhenBothCountsZero", testHeadEmptyWhenBothCountsZero);

    run("testTailAllCountsZeroLandsExactly", testTailAllCountsZeroLandsExactly);
    run("testTailFiftySixByteArray", testTailFiftySixByteArray);
    run("testTailMutationEmptyArraysAlign", testTailMutationEmptyArraysAlign);
    run("testTailFiftyTwoAndNinetySixByteArrays", testTailFiftyTwoAndNinetySixByteArrays);
    run("testTailNestedGroupAtEightyEight", testTailNestedGroupAtEightyEight);
    run("testGroup88PositionsDecodeAsFloatTriples", testGroup88PositionsDecodeAsFloatTriples);
    run("testGroup88PositionsMutationWrongStrideThrows", testGroup88PositionsMutationWrongStrideThrows);
    run("testTailDEightAndEEightPairRealigns", testTailDEightAndEEightPairRealigns);
    run("testTailMutationNoRealignBeforeEEight", testTailMutationNoRealignBeforeEEight);
    run("testTailEEightGatedOnDEightNotItsOwnCount", testTailEEightGatedOnDEightNotItsOwnCount);
    run("testTailMutationProseTrailingGroupOrder", testTailMutationProseTrailingGroupOrder);
    run("testTailMutationTwelveByteTrailingInnerElements",
        testTailMutationTwelveByteTrailingInnerElements);
    run("testTailVariableInnerCountsAndEightAlignment",
        testTailVariableInnerCountsAndEightAlignment);

    run("testGatedPairSentinelWithFlagBitClearConsumesNothing",
        testGatedPairSentinelWithFlagBitClearConsumesNothing);
    run("testGatedPairRealValues", testGatedPairRealValues);
    run("testGatedPairMutationListBlockCostsZero", testGatedPairMutationListBlockCostsZero);
    run("testGatedPairCollisionHull", testGatedPairCollisionHull);
    run("testGatedPairMutationHullGateBitCleared", testGatedPairMutationHullGateBitCleared);
    run("testGatedPairMutationSentinelBranchConsumesBytes",
        testGatedPairMutationSentinelBranchConsumesBytes);

    run("testResolvedReferenceIsAMeshSubBlock", testResolvedReferenceIsAMeshSubBlock);
    run("testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock",
        testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock);
    run("testResolvedReferencesEmptyWhenCountIsZero", testResolvedReferencesEmptyWhenCountIsZero);
    run("testFinalizationArithmetic", testFinalizationArithmetic);

    run("testOpenArraysStayOpaque", testOpenArraysStayOpaque);
    run("testMiddleIsComputedNotSearched", testMiddleIsComputedNotSearched);
    run("testMiddleTrailingRecordArraysAndTheirAsymmetry",
        testMiddleTrailingRecordArraysAndTheirAsymmetry);
    run("testMiddleRecordContentDecoding", testMiddleRecordContentDecoding);
    run("testGroupFeatureIndicesMutationWrongStrideThrows",
        testGroupFeatureIndicesMutationWrongStrideThrows);
    run("testMiddleMutationNameTableNulByte", testMiddleMutationNameTableNulByte);
    run("testMiddleMutationOneRenderGroup", testMiddleMutationOneRenderGroup);
    run("testMiddleMutationNoLookupArray", testMiddleMutationNoLookupArray);
    run("testMiddleControlsAreScorableOneTermAtATime", testMiddleControlsAreScorableOneTermAtATime);
    run("testHeaderFieldAccessorRejectsOutOfRange", testHeaderFieldAccessorRejectsOutOfRange);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic clmesh-format tests passed.\n";
    return 0;
}
