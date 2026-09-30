// Reader for the `.ccmesh_pc`/`.csmesh_pc` "geometry block" (spec-
// geometry-format.md Sec4.1/Sec4.1.1): the second, format-specific
// sub-structure that follows the shared material block (material_block.h)
// - a small fixed header (magic, version, flags, and six independent
// per-array count/gating fields) followed by up to six independently-
// sized, independently-aligned raw arrays. This reader extracts those six
// arrays as OPAQUE byte blobs only (per the task's own scope: "raw array
// extraction") - none of arrays 1/2/3/5's semantic content is interpreted
// (spec-geometry-format.md Sec6 item 8: their meaning is still OPEN/
// UNKNOWN even though their existence/stride/gating is CONFIRMED via
// disassembly). Array 4 is HIGH CONFIDENCE known to be a monotonic
// ascending index/remap table (spec Sec3.2), but is still exposed as raw
// bytes here, not decoded, for API consistency with the other five.
//
// TWO THINGS IN THIS READER ARE NOT FROM THE SPEC TEXT DIRECTLY:
//
// 0. The 16-byte alignment pad between the material block and the
//    geometry block (spec Sec4.1) is NOT a plain "round up to the next
//    multiple of 16, no-op if already aligned": when the material block's
//    end already lands exactly on a 16-boundary, a FULL extra 16-byte pad
//    is inserted, so the magic sits 16 further out than a plain
//    roundUp16 predicts. parse() therefore SEARCHES nearby 16-byte-aligned
//    candidates for the real 0x424BD00D magic rather than computing its
//    position - see parse()'s own comment.
//
//    Measured across all 549 real .ccmesh_pc files in characters.vpp_pc:
//    exactly 21 need this extra pad, and in every one of the 21 the real
//    magic is at naive+16 with the naive position landing on a 16-
//    boundary - a clean, consistent rule, not a scattering of odd cases.
//
//    SINCE CONFIRMED INDEPENDENTLY: spec-foliage-format.md Sec3 states
//    this rule outright for `.cfmesh_pc` - "mandatory pad to the next
//    16-byte boundary, ALWAYS >= 1 byte", citing
//    spec-geometry-format.md Sec3.1.1 - which is exactly the behaviour
//    measured here. The search below therefore agrees with a stated rule
//    rather than merely working; it is kept because it is already
//    validated at 1803/1803 and is robust to the material block's end
//    landing anywhere, but a deterministic
//    `end/16*16 + 16` would be equivalent (see
//    src/foliage_mesh.cpp's padStrictlyTo16, which does exactly that).
//
//    Worth knowing if you compare notes with Team A: these same 21 files
//    were independently reported from the disassembly side as "tiny
//    degenerate/censor meshes with an all-zero geometry block." They are
//    not. They are ordinary meshes (3-5KB, 6-9 textures; only 3 of the 21
//    even have zero textures) that parse completely once the extra pad is
//    accounted for - a direct roundUp16 just lands on the pad's zero bytes
//    and looks like an empty block. Verified here by parsing all 21 in
//    full. No genuinely geometry-less .ccmesh_pc was found in the set.
//
// 1. `kBlobALengthFieldOffset`/`kBlobAContentPad`. Between the geometry
//    block's gating fields and array 1's start sits "BlobA" - a real,
//    explicit length-prefixed blob (spec-geometry-format.md Sec4.1.2
//    finding 2) holding the embedded second (mixed-case) copy of the
//    material names (spec Sec3.2's "mixed/proper case" name list -
//    confirmed to live HERE, inside the geometry block, not in some
//    undifferentiated trailing "footer" as the original phrasing
//    implied). Team A (disassembly) identified BlobA and its exact
//    offset (+0x88) and gave real-file length values, which this session
//    independently cross-checked byte-for-byte on two files (getting
//    exact matches - see git history/commit notes if this project ever
//    gets version control, or the session transcript, for the trace).
//
//    AN EARLIER VERSION OF THIS READER GOT THIS WRONG: rather than
//    reading BlobA's own length field, it tried to re-derive the same
//    length by walking the embedded name copy as N null-terminated
//    strings (mirroring the material block's own, genuinely-simple
//    format) plus an ad-hoc padding-byte search. That worked by
//    coincidence on simple meshes (at most one array populated) but
//    failed outright - confirmed via full-file brute-force search finding
//    NO valid position anywhere - on richer meshes with several arrays
//    populated at once, because the walk-N-strings model of BlobA's
//    *internal* layout was simply wrong, not just imprecise. Broad
//    validation against 549 real files in characters.vpp_pc had pegged
//    that version at 82% (451/549) before this fix; the length-field
//    approach below is expected to fix the richer-mesh cases outright
//    once re-validated, since it no longer depends on modeling BlobA's
//    internal shape at all - it just trusts the length Team A found.
//
// If a future sample contradicts either finding, trust the new sample and
// revisit these, not the other way around.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3geometry/errors.h"
#include "sr3geometry/material_block.h"
#include "vpp/byte_view.h"

namespace sr3geometry {

constexpr uint32_t kGeometryBlockMagic = 0x424BD00Du; // CONFIRMED - disassembly (spec Sec4.1)

// "BlobA" - see the file-level comment above. Offset relative to the
// geometry block's own magic. CONFIRMED - Team A (disassembly) +
// independent byte-for-byte verification this session on 2 real files.
constexpr size_t kBlobALengthFieldOffset = 0x88;
constexpr size_t kBlobAContentPad = 5; // 4-byte length field + 5 zero bytes, then BlobA's actual content

// Rounds up to the next multiple of 8 - spec Sec4.1's "(an 8-byte
// alignment for the final trailer step)", i.e. the step from the last
// populated array to the Mesh sub-block, as distinct from the 16-byte
// alignment used between arrays. Confirmed against three real files; see
// the file-level comment for why getting this wrong was invisible on
// simple meshes but broke every richer one.
constexpr size_t roundUp8(size_t value) {
    return (value + 7) / 8 * 8;
}

// Per-array count-field offsets, relative to the geometry block's own
// magic (spec Sec4.1's table). Array indices below are 0-based (index 0
// == spec's "array 1", ..., index 5 == spec's "array 6"), matching
// GeometryBlock::arrays()'s own indexing.
constexpr size_t kArrayStride[6] = {96, 24, 40, 4, 4, 2}; // CONFIRMED (spec Sec4.1 table)

class GeometryBlock {
public:
    struct RawArray {
        size_t offset = 0; // absolute byte offset within the content GeometryBlock::parse() was given
        size_t count = 0;
        size_t stride = 0;
        size_t byteLength() const { return count * stride; }
    };

    // Parses the geometry block embedded in `content` (a full .ccmesh_pc/
    // .csmesh_pc buffer), which must also contain `material` at its own
    // offset 0 (i.e. `material` must have been parsed from this same
    // `content` via MaterialBlock::parse(content)). Locates the geometry
    // block near roundUp16(material.totalSize) (spec Sec4.1's confirmed
    // 16-byte alignment pad after the material block - searched, not
    // computed directly, see the file-level comment), reads BlobA's
    // length field to locate array 1's start deterministically, and
    // throws FormatError if the magic doesn't match, if `content` is too
    // small to hold the fixed header/gating fields (up to +0x80) or
    // BlobA's own length field (+0x88), or if the computed array-space
    // start doesn't land on a real Mesh-sub-block version-9 marker after
    // walking all six arrays (spec Sec4.1.1) - kept as a real sanity
    // check on the BlobA arithmetic, not just trusted blind.
    static GeometryBlock parse(ByteView content, const MaterialBlock& material);

    // Same walk, for a carrier that STATES where the 0x424BD00D sub-header
    // is rather than implying it from a material block. `.ccar_pc` vehicles
    // need this: they give the offset at their header +0x04 and carry no
    // material block at all (absent in 393/393), so parse()'s search has
    // nothing to anchor against. Throws FormatError if the magic is not
    // actually there - a wrong offset fails immediately rather than
    // walking garbage.
    static GeometryBlock parseAt(ByteView content, size_t subheaderOffset);

    size_t offset() const { return offset_; }   // absolute offset of the 0x424BD00D magic within the content passed to parse()
    uint16_t version() const { return version_; }
    uint16_t flags() const { return flags_; }

    // Index 0 == spec's "array 1" ... index 5 == spec's "array 6".
    const std::array<RawArray, 6>& arrays() const { return arrays_; }

    // Raw bytes of arrays()[index], sliced from `content` (the SAME
    // buffer passed to parse() - not owned/cached, same non-owning-view
    // convention as vpp::Container). Empty view if that array's count is
    // 0. Throws FormatError for an out-of-range index.
    ByteView arrayBytes(size_t index, ByteView content) const;

    // True after any successful parse() - see the file-level comment
    // above: finding this marker (spec Sec4.1.1's own literal, disassembly-
    // confirmed version-9 check) is currently REQUIRED to locate the
    // arrays at all, so this is not an optional bonus check the way it may
    // look; parse() throws rather than returning with this false. Kept as
    // its own accessor (rather than folded away) so a future revision that
    // finds an alternative anchor for Mesh-block-less files doesn't need
    // an API change. Does NOT parse the sub-block's own internal fields
    // (channel records, the count/stride fields spec Sec4.1.1 flags as
    // strong-but-unconfirmed vertex-buffer candidates, etc.) - STALE UNTIL
    // 2026-09-29 (rule 16): this used to say those remained genuinely OPEN;
    // spec-geometry-format.md Sec6 item 8 itself now says the channel-array
    // question is CLOSED (see spec-vertex-format.md, 26,601/26,601 channels
    // resolved end to end) and Sec4.1.2 separately closes the count x
    // stride framing completely (it's the index buffer, at exact offset
    // 0x10). This codebase already ships that future pass -
    // `sr3mesh::MeshBlock`/`decodeChannel()` - which is why this accessor
    // itself still deliberately does nothing with these fields: the parsing
    // lives one layer up, not because the question is unanswered.
    bool hasMeshSubBlock() const { return hasMeshSubBlock_; }
    size_t meshSubBlockOffset() const { return meshSubBlockOffset_; } // valid only if hasMeshSubBlock()

private:
    size_t offset_ = 0;
    uint16_t version_ = 0;
    uint16_t flags_ = 0;
    std::array<RawArray, 6> arrays_{};
    bool hasMeshSubBlock_ = false;
    size_t meshSubBlockOffset_ = 0;
};

} // namespace sr3geometry
