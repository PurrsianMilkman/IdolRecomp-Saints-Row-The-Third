// Locates and parses the embedded "Mesh" sub-blocks (sr3mesh::MeshBlock,
// spec-vertex-format.md) that carry a zone's geometry - `.czn_pc`'s CPU
// side, `.gzn_pc`'s GPU side - per spec-zone-data-format.md Sec7/Sec9.4.
//
// THIS IS THE SAME SHARED MECHANISM ALREADY USED BY `.ccmesh_pc`/
// `.csmesh_pc` (sr3geometry::GeometryBlock), `.cfmesh_pc`
// (sr3foliage::FoliageMesh) and `.gsrt_pc` (trees, spec-tree-format.md
// Sec9) - it is reused here via sr3mesh::MeshBlock::parse(), not
// reimplemented, per this project's standing rule against a second copy of
// shared parsing logic drifting from the first.
//
// WHAT IS CONFIRMED (spec-zone-data-format.md Sec7, Sec9.4):
//
//  * `.gzn_pc` is the SAME g-segment shape as every other Mesh-block
//    carrier: the block's own check value, three zero words, then an
//    index buffer at +0x10 (Sec7.1: 1,002/1,002 for the leading check
//    value and the three zero words; 991/1,002 for the specific
//    `u16@+0x10 == 0` ascending-index-buffer opening value).
//  * The position of a Mesh sub-block inside `.czn_pc` is found the SAME
//    way as `.gcmesh_pc`/`.gsmesh_pc`/`.gsrt_pc`: a `u32 == 9` (the Mesh
//    sub-block's own version field) immediately followed (+0x04) by a
//    check value that also appears in the paired `.gzn_pc` - Sec7.2:
//    version-9 reading above background in only 1/1,002 files at any
//    OTHER offset in a +-20 byte window (control: 6/1,002 best single
//    offset).
//  * `mesh_scan.py`, run over the COMPLETE shipped 1,083-pair population
//    (spec-zone-data-format.md Sec9.4, not a sample), independently
//    replays exactly this check-value-locate + declared-length-consume +
//    trailing-bookend contract and gets **1,002 / 1,002** (100%) on every
//    file where the test could fail at all (a nonzero `.gzn_pc`) - the
//    other 81 are exactly the zero-byte `.gzn_pc` files Sec7.1 already
//    named, verified as an EXACT SET match, not merely a matching count.
//    This reader's locate() is a from-scratch reimplementation of that
//    same contract on top of sr3mesh::MeshBlock rather than a port of
//    mesh_scan.py itself, so its own population numbers (reported by
//    tools/validation/validate_zone.cpp) are an independent third replay,
//    not a restatement of Sec9.4's.
//
// WHAT THIS READER DOES NOT ATTEMPT: locating a Mesh sub-block by walking
// `.czn_pc`'s own top-level `{id, length}` record chain. That chain is
// explicitly HYPOTHESIS-unconfirmed (spec-zone-data-format.md Sec3) and,
// separately, CONFIRMED (Sec8.1, a controlled negative: 0/1,002 files have
// a chunk body start exactly at the first Mesh block, at or below a 0.3%
// random-offset control) to never actually contain the Mesh block at a
// chunk boundary - so a correct reader cannot reach the geometry by
// walking that chain even once it exists. locate() below instead scans
// `.czn_pc` directly for the version/check-value SIGNATURE, independent of
// any record-length arithmetic, which is exactly why it can find the
// geometry at all.
//
// MULTI-BLOCK ZONES - CORRECTED 2026-09-13, spec-zone-data-format.md
// Sec10.4/Sec10.5. A `.gzn_pc` is a contiguous CHAIN of segments, each
// shaped `[u32 tag][payload][u32 copy of the tag]`, each beginning exactly
// where the previous one ended, in the same order as their descriptors
// appear in the `.czn_pc`. **Only segment 0 is 16-aligned** (it starts at
// byte 0); every later segment lands at whatever 4-aligned offset the
// running total of the preceding declared g-lengths puts it at.
//
// This file previously located each g-backed block INDEPENDENTLY by
// searching 16-aligned offsets in `.gzn_pc` for its check value. That is
// the wrong instrument, not merely a strict one: it admits segment 0 plus
// whichever later segments happen to land on a 16-byte boundary, which is
// 2,843 of 39,073 g-backed candidates (7.28%) against a true 38,152
// (97.64%) - a 13.4x under-count (Sec10.2/Sec10.5).
//
// locate() now walks the chain instead: segment 0 via the (confirmed
// correct) 16-aligned search, then each subsequent block's g-offset by
// arithmetic - `previous offset + previous declared g-length` - CONFIRMED
// at that computed position by the `[tag]...[tag]` bookend before it is
// trusted. `~al` ("always loaded") zones pad between segments to the next
// 16-byte boundary, so that one extra computed position is tried too
// (Sec10.5: 97 of 928 tiling files use a skip, all of them `~al`). A
// candidate that no computed position confirms is skipped WITHOUT moving
// the cursor, so a coincidental `9` cannot derail the chain behind it.
//
// This is a CONFIRMATION step, not a discovery mechanism: Sec10.5's
// controls on it fire at 0.13% (same tag and length against a DIFFERENT
// zone's `.gzn_pc`), 0% (declared length perturbed by -4, +16 or -16) and
// 0.21% (+4). Independently re-derived here before the change was made -
// on `sr3_city~s0715` all 12 g-backed segments chain exactly additively
// and tile to the 557,224-byte EOF with no gap and no residual.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3mesh/mesh_block.h"
#include "sr3zone/errors.h"
#include "vpp/byte_view.h"

namespace sr3zone {

using vpp::ByteView;

// One located, fully-parsed Mesh sub-block embedded in a `.czn_pc` buffer.
struct ZoneMeshBlockEntry {
    // Absolute byte offset of the Mesh sub-block's version field (the
    // `u32 == 9` anchor) within the `.czn_pc` content passed to locate().
    size_t cznOffset = 0;

    // True if this block's bulk segment (index buffer + channel data)
    // lives in the paired `.gzn_pc` (flags bit 0 set - the common case,
    // 3,448 / 4,478 blocks per spec Sec7.3); false if it is inline in
    // `.czn_pc` itself, immediately after the channel records (the
    // remaining 1,030 / 4,478).
    bool fromGFile = false;

    // Absolute byte offset within `.gzn_pc` where this block's segment
    // starts - the chain position `previous offset + previous declared
    // g-length`, confirmed by the segment's own `[tag]...[tag]` bookend
    // before being accepted. 16-aligned only for segment 0; 4-aligned
    // otherwise. Meaningless when fromGFile is false.
    size_t gznOffset = 0;

    // The fully parsed, fully validated block - reused sr3mesh machinery,
    // so its index buffer, channels, and (where locatable) draw groups are
    // already decoded exactly as for any other carrier.
    sr3mesh::MeshBlock block;
};

class ZoneGeometry {
public:
    // Scans `cznContent` for every Mesh sub-block anchor (a 4-aligned
    // `u32 == 9` followed by a check-value field) and attempts to parse
    // each one via sr3mesh::MeshBlock::parse(), using `gznContent` for the
    // g-backed case. `gznContent` may be empty - pass an empty ByteView
    // for the 81 / 2,971 shipped zones with a zero-byte or absent
    // `.gzn_pc` (spec-zone-data-format.md Sec7.1); any g-backed candidate
    // simply cannot be located/validated against an empty buffer and is
    // skipped, not guessed at.
    //
    // A `u32 == 9` at 4-aligned offset `pos` is accepted as a REAL anchor
    // only if sr3mesh::MeshBlock::parse(cznContent, pos, ...) succeeds
    // without throwing - i.e. this function never trusts the version
    // field alone. That parse call enforces, internally, exactly the
    // invariants spec-zone-data-format.md Sec7/Sec9.4 measured: the
    // segment must open on the declared check value, consume EXACTLY the
    // declared length, and end on a repeated copy of the same check value
    // - so a coincidental `9` that is not a real Mesh sub-block is
    // rejected by machinery this reader did not have to reinvent, not by
    // an ad-hoc heuristic added here.
    //
    // Never throws for a file with zero real Mesh blocks (an empty
    // `.czn_pc` region, or one with no geometry at all) - returns an empty
    // vector instead, since "no anchors survived" is an expected, common
    // outcome (spec Sec7.1: 81 / 1,083 pairs have none), not a format
    // violation.
    static std::vector<ZoneMeshBlockEntry> locate(ByteView cznContent, ByteView gznContent);
};

} // namespace sr3zone
