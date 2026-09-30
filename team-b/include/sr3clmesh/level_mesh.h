// Reader for `.clmesh_pc`/`.glmesh_pc` - the engine's own "Level_Mesh"
// resource type (registered types 31/40, spec-format-inventory.md), the
// static-prop / collision-geometry container used throughout the
// open-world streaming tiles.
//
// SCOPE. This reader covers the container's own `0x4fe66afa` header and the
// whole sub-parser chain spec-physics-format.md Sec4.2/Sec4.4.6 characterise.
// It does NOT re-implement vertex geometry: the embedded "Mesh" sub-blocks it
// walks past are the same shared block `.ccmesh_pc`, `.cfmesh_pc`, `.gsrt_pc`
// and `.gzn_pc` already go through (spec-vertex-format.md), handed to
// sr3mesh::MeshBlock when a caller asks for their contents.
//
// ==========================================================================
// ONE CONTINUOUS COMPUTED WALK - NOTHING IS SEARCHED FOR (2026-09-13)
// ==========================================================================
//
// This reader's FIRST version (HANDOFF Sec9.59) could not size the region
// `FUN_007499d0` resolves into header `+0xb8`, so it walked the file in three
// disconnected parts and LOCATED the tail by trying every 8-aligned start and
// keeping the ones whose walk happened to finish exactly at end-of-file.
// That searched model reached 100,255 / 100,384 exact landings, at the cost
// of a 4.3% multi-candidate ambiguity rate and 129 entries it could not
// place at all.
//
// spec-physics-format.md Sec4.4.6 closed the gap: `thunk_FUN_00e3e590` is not
// a reference resolver, it is a PARSER that lays a `0x50`-byte record down
// inline at the 16-aligned cursor and then walks a material-set block and two
// render-mesh groups after it. Every term of that region's byte length is
// either a disassembly constant or a field read out of the file, so the
// middle is computed, the tail's start is `middleEnd()`, and the whole file
// walks on ONE cursor from `bodyStart()` to EOF. This reader now implements
// that. There is no search loop left, no candidate-layout count to report,
// and the 129 unplaceable entries were search artifacts: they land.
//
//   HEAD    bodyStart() .. headEnd()      `FUN_007499d0`'s two 8-byte-stride
//                                         slot arrays (`+0x48` -> `+0x40`,
//                                         `+0x58` -> `+0x50`), then the
//                                         count(+0x48) embedded Mesh
//                                         sub-blocks, THEN the count(+0x58)
//                                         index-list blocks (`FUN_00e401d0`).
//   MIDDLE  middleOffset() .. middleEnd() `thunk_FUN_00e3e590`: the `0x50`-byte
//                                         record, the material-set block, the
//                                         `materialCount x 8` lookup table,
//                                         and two render-mesh groups.
//   TAIL    tailStart() .. EOF            `FUN_00749b40` through
//                                         `FUN_00749e80`'s trailing nested
//                                         group, which ends EXACTLY at EOF.
//
// ==========================================================================
// THINGS THAT ARE NOT IN THE SPEC TEXT AND WERE MEASURED HERE
// ==========================================================================
//
// 0. AN EMPTY ARRAY CONSUMES NOTHING *AND ALIGNS NOTHING* - for
//    `FUN_00749c40`, `FUN_00749cf0`, `FUN_00749d40` and `FUN_00749e80`, which
//    test their count before aligning. `FUN_00749b40` is the exception: it
//    16-aligns UNCONDITIONALLY and only its array advance is conditional
//    (Sec4.4.2(a) as amended by Sec4.4.6). Both halves are load-bearing and
//    both are covered by the synthetic suite.
//
// 1. `FUN_00749e80`'S TRAILING NESTED GROUP IS `+0x108` FIRST, THEN `+0x100`.
//    Sec4.2's prose introduces the parallel count array (`+0x100`) before the
//    primary pointer array (`+0x108`); its own "Output slot(s)" column lists
//    them the other way round, and the file agrees with the column:
//
//        align 8
//        primary  array at `+0x108`:  count(+0xfc) x 8 bytes
//        parallel array at `+0x100`:  count(+0xfc) x 4 bytes
//        then, per entry: align 8, then thatEntryCount x 4 bytes
//
// 2. `count(+0xfc)` IS 3 IN EVERY SHIPPED FILE MEASURED - 100,384 of 100,384.
//    It behaves structurally as a count (its three entries carry three
//    independent per-entry counts) but nothing in shipped data exercises any
//    other value. Treat "3" as an unexamined constant, not a validated
//    variable.
//
// 3. THE GATED `+0x78`/`+0x7c` PAIR HAS *THREE* OUTCOMES, NOT TWO, AND BOTH
//    OF THE TWO THAT CONSUME BYTES ARE REAL FILE-RESIDENT DATA. Sec4.4.6(f)
//    replaced this reader's original `kGatedPairBytes = 0` placeholder:
//      * `+0x78 != -1`  -> `FUN_00748c70`: 16-align, then a block whose own
//        `+0x08` u32 is its TOTAL BYTE SIZE; the cursor advances by that.
//        Per-file, not a constant.
//      * `+0x78 == -1` AND the flags byte at header `+0x08` has bit 2 set ->
//        `FUN_00749ba0`: a real collision hull - vertex list, u16 index list,
//        opaque blob, two vec3s. NOT the "builds a default, consumes nothing"
//        reading Sec4.2 originally posed, which Sec4.4.6(f) REFUTES.
//      * otherwise -> nothing at all, not even an alignment.
//    WHICH BIT of the flags byte is the one place this reader had to resolve
//    an ambiguity in the spec's own prose by measurement: "bit 2" is written
//    as the mask `0x02` elsewhere in Sec4.2 (`byte(+8) & 2`, a different
//    gate), and as a bit INDEX it is the mask `0x04`. The index reading is
//    the right one - mask `0x04` lands 4,232/4,232 DLC entries, mask `0x02`
//    lands 3,436 - so kHullGateMask is `0x04`, MEASURED. Also measured: the
//    third outcome performs NO alignment (asserting one costs 24 of 4,232).
//
// 4. THE HEAD IS `8*count(+0x48) + 8*count(+0x58)` OF SLOTS, BOTH ARRAYS
//    BEFORE ANY RESOLUTION, THEN THE MESH SUB-BLOCKS, THEN ARRAY 2'S
//    INDEX-LIST BLOCKS. Not "array A, then A's resolved objects, then array
//    B": `vent_metala` (1,1) puts its first Mesh sub-block at bodyStart+16
//    and `diner_wall_2_a01` (2,2) puts its first at bodyStart+32. The
//    index-list tail is Sec4.4.6's correction to Sec4.4.1's published head
//    formula, which omitted it (and which this reader shipped).
//
//    RELATED, AND NOT IN EITHER SPEC: `count(+0x48)`, `count(+0x58)` and
//    `count(+0x70)` are the SAME NUMBER in 100,379 of 100,379 entries. Three
//    sub-parsers sized by one quantity means FUN_00749b40's 56-byte records
//    are one per resolved reference. What is IN a record stays OPEN.
//
// 5. THE HEADER'S FIRST `0x40` BYTES ARE REAL DATA, NOT JUST MAGIC+VERSION.
//    `+0x0c..+0x18` is a centre/radius-shaped float quad and `+0x20..+0x3c`
//    a min/max-shaped float set, with the centre reproducing `(min+max)/2`
//    on the axes checked. Neither spec describes these, so they are handed
//    back as bytes (headerBytes()) and NOT interpreted.
//
// 6. BOTH SIDES OF A MESH SUB-BLOCK STEP BY ITS DECLARED c-LENGTH EXACTLY,
//    WITH NO ROUNDING. Measured here first (HANDOFF Sec9.59: a roundUp16 step
//    cost every reference after the first), then read out of
//    `FUN_00e71410`'s own closing self-check by Sec4.4.6. The same step is
//    used at the two MIDDLE call sites, where a roundUp16 costs 85.8% of the
//    population.
//
// 7. `FUN_00749c40`'S 12-BYTE INNER ELEMENT IS A LOCAL-SPACE (x,y,z) FLOAT32
//    POSITION - CONFIRMED, not merely a plausible reading. Both open readings
//    Sec4.2 offered ("3 vertex/point indices" or "a small plane/face
//    descriptor") were tested against the COMPLETE 258-file / 388-outer /
//    2,368-inner population (HANDOFF Sec9.60): the u32 reading is dead on
//    arrival (mean magnitude in the BILLIONS, and count(+0x88)>0 never
//    co-occurs with count(+0x48)>0 in any of the 258 files - 0/258 - so
//    there is not even a candidate vertex list in the same file to index
//    into); the float reading is not just plausible but internally
//    self-consistent to the bit: `fountain_a_large_01`'s 25-point group is a
//    closed loop (first element bit-identical to the last) tracing a
//    circle of radius 5.2727 in the (slot0,slot2) plane at constant slot1;
//    `drain_round_01`'s 7-point group is the same shape at radius 0.50;
//    `patio_01` (9) and `wallstair_4mthin_02` (15, with slot1 - the
//    vertical axis in every sample - climbing 0.14->2.29, a real stair
//    profile) both close exactly; `pocmedianendcap001` (15) does NOT close
//    (distance 2.999, not a bug - an end cap is the one shape of the six
//    that should read as an open profile) and is left/right symmetric about
//    slot0=0 either way. Separately: every group is BYTE-IDENTICAL across
//    every duplicate tile placement of the same asset (18/18 multi-placement
//    stems checked) - local-space data, not a baked per-tile world transform.
//    group88Positions() decodes this. The outer record's `+0x04` field
//    (Sec4.2 calls it padding) is NOT padding - 388/388 outer records carry
//    a nonzero value there (range 3,299,712..23,812,808) - but it is not
//    decoded: it is constant across LOD variants of one asset yet IDENTICAL
//    across two unrelated assets (`fountain_a_large_01` and
//    `wallstair_4mthin_02` both read 23,812,808), which rules out a
//    per-asset name hash without saying what it actually is. Left OPEN.
//
// 8. THE MIDDLE'S OWN CONTENTS BEYOND ITS LENGTH: PARTIALLY DECODED NOW
//    (2026-09-14, Sec4.4.6(h)). Originally nothing about what a material
//    record or a `0x20`/`0x34`-byte sub-record CONTAINS was asserted -
//    locating them was enough to size the region. Sec4.4.6(h) traced real
//    runtime consumers (not just the three sizing walkers) for three fields,
//    all CONFIRMED - disassembly, and this reader now decodes exactly those
//    three, nothing more:
//      - materialRecords() - the material record's `+0x00`/`+0x04` hashes,
//        `+0x08` flags byte, and `+0x0C`/`+0x0E`/`+0x0F` array-length triple
//        (transfers directly from spec-foliage-format.md Sec6's CONFIRMED
//        layout - the same parser function, `FUN_00e71090`, reads it here).
//      - groupAFeatureIndices() - a `0x20`-byte sub-record's `+0x18` (u16),
//        an index into the group's OWN resolved-mesh reference, not into
//        anything inline here - read outside the three walkers, in the real
//        `.clmesh_pc`/`.glmesh_pc` runtime constructor `glmesh_ctor_FUN_0074c530`.
//      - groupBFeatureIndices() - the `+0x02` (u16) of group 2's existing
//        4-byte inner element, in the SAME index namespace as the above
//        (cross-referenced against it by the same consumer).
//      - trailerRecords() - a `0x34`-byte trailer's `+0x2C` (u32, a raw
//        pre-remap material-slot index - the REMAPPED runtime handle needs a
//        temporary per-parse table this reader has no on-disk equivalent
//        for, so only the raw index is decoded) and `+0x30` bit `0x04` (an
//        "active at this LOD tier" flag, confirmed via a real introsort +
//        comparator that partitions on exactly this bit).
//    Everything else in these three record types remains genuinely OPEN -
//    Sec4.4.6(h) says so explicitly for each. `+0x24` and `+0x34` of the
//    `0x50`-byte record itself are still read by nothing at all anywhere
//    (Sec4.4.6(d), stale authoring-tool addresses) and stay undecoded here.
//
// If a future sample contradicts any of the above, trust the sample.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3clmesh/errors.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "vpp/byte_view.h"

namespace sr3clmesh {

using vpp::ByteView;

// CONFIRMED - disassembly (spec-geometry-format.md Sec4.2): the format's own
// magic, its EXACT (not ranged) version requirement, and the fixed header
// size the constructor advances the cursor by.
constexpr uint32_t kLevelMeshMagic = 0x4fe66afau;
constexpr uint32_t kLevelMeshVersion = 20;
constexpr size_t kLevelMeshHeaderSize = 0x140;

// Header field offsets, relative to the magic. Every one is CONFIRMED -
// disassembly (spec-physics-format.md Sec4.2's table). The `kOut*` names are
// the slots the sub-parsers write resolved pointers back into; on disk those
// read zero, which is why this reader reports offsets of its own rather than
// trusting them.
constexpr size_t kFlagsByte      = 0x08; // byte; bit 2 gates the collision hull
constexpr size_t kCountRefA      = 0x48; // FUN_007499d0, array 1
constexpr size_t kOutRefA        = 0x40;
constexpr size_t kCountRefB      = 0x58; // FUN_007499d0, array 2
constexpr size_t kOutRefB        = 0x50;
constexpr size_t kOutSingleRefB8 = 0xb8; // FUN_007499d0's tail - the MIDDLE's `0x50`-byte record
constexpr size_t kCount70        = 0x70; // FUN_00749b40
constexpr size_t kOut68          = 0x68;
constexpr size_t kPairFirst      = 0x78; // FUN_00748c70 / FUN_00749ba0 gate
constexpr size_t kPairSecond     = 0x7c;
constexpr size_t kCount88        = 0x88; // FUN_00749c40, outer
constexpr size_t kOut90          = 0x90;
constexpr size_t kCount98        = 0x98; // FUN_00749cf0
constexpr size_t kOutA0          = 0xa0;
constexpr size_t kCountA8        = 0xa8; // FUN_00749d40
constexpr size_t kOutB0          = 0xb0;
constexpr size_t kCountD8        = 0xd8; // FUN_00749e80, flat array 1
constexpr size_t kOutE0          = 0xe0;
constexpr size_t kCountE8        = 0xe8; // FUN_00749e80, flat array 2
constexpr size_t kOutF0          = 0xf0;
constexpr size_t kCountFc        = 0xfc; // FUN_00749e80, trailing nested group
constexpr size_t kOutFcParallel  = 0x100;
constexpr size_t kOutFcPrimary   = 0x108;

// Element strides, all CONFIRMED - disassembly (same table).
constexpr size_t kStrideRef        = 8;
constexpr size_t kStride70         = 56;  // 0x38
constexpr size_t kStride88Outer    = 16;
constexpr size_t kStride88Inner    = 12;
constexpr size_t kStride98         = 52;  // 0x34
constexpr size_t kStrideA8         = 96;  // 0x60
constexpr size_t kStrideD8         = 8;
constexpr size_t kStrideE8         = 16;
constexpr size_t kStrideFcPrimary  = 8;
constexpr size_t kStrideFcParallel = 4;
constexpr size_t kStrideFcInner    = 4;

// ---- the MIDDLE: `thunk_FUN_00e3e590` (spec-physics-format.md Sec4.4.6) ----
// The `0x50`-byte record the parser lays down at the 16-aligned cursor, and
// which header `+0xb8` receives the address of. Every offset below is
// CONFIRMED - disassembly (Sec4.4.6(b)/(d)). `+0x24` and `+0x34` are
// deliberately absent: Sec4.4.6(d) establishes that NOTHING reads them (they
// carry stale authoring-tool addresses), so nothing here reads them either.
constexpr size_t kMidRecordSize     = 0x50;
constexpr size_t kMidMatSetSlot     = 0x00; // output slot -> the material-set header H; -1 aborts
constexpr size_t kMidLookupSlot     = 0x08; // output slot -> the materialCount x 8 lookup table
constexpr size_t kMidGroup1Slot     = 0x10; // output slot -> render group 1
constexpr size_t kMidGroup2Gate     = 0x18; // -1 would skip render group 2; never shipped
constexpr size_t kMidGroupACount    = 0x20; // group 1's 16-byte trailing records
constexpr size_t kMidGroupASlot     = 0x28;
constexpr size_t kMidGroupBCount    = 0x30; // group 2's 16-byte trailing records
constexpr size_t kMidGroupBSlot     = 0x38;
constexpr size_t kMidTrailerSlot    = 0x40;
constexpr size_t kMidTrailerCount   = 0x48; // count of a 0x34-byte record array

constexpr size_t kStrideGroupRecord = 0x10; // both trailing record arrays
constexpr size_t kGroupARecordCountAt = 0x08; // ... group 1's per-record count lives at +0x08
constexpr size_t kGroupBRecordCountAt = 0x00; // ... group 2's at +0x00 - the asymmetry is real
constexpr size_t kStrideGroupASub   = 0x20;
constexpr size_t kStrideGroupBSub   = 4;
constexpr size_t kStrideMidTrailer  = 0x34;

// Sec4.4.6(h), CONFIRMED - disassembly. Group 1's sub-record `+0x18` (u16,
// `glmesh_ctor_FUN_0074c530`) and group 2's inner element `+0x02` (u16, same
// consumer, same index namespace). Both are the ONLY offset either record
// type is confirmed read at; every other byte is genuinely OPEN.
constexpr size_t kGroupASubFeatureIndex = 0x18;
constexpr size_t kGroupBInnerFeatureIndex = 0x02;

// Sec4.4.6(h), CONFIRMED - disassembly (`FUN_00e3c6e0`, `glmesh_ctor_FUN_0074c530`).
// `+0x2C` is the RAW on-disk index, before the runtime remap through a
// temporary per-parse table this reader has no on-disk equivalent for - see
// note 8 at the top of this file. `+0x30` bit `0x04` is confirmed via a real
// introsort + comparator that partitions the array on exactly this bit.
constexpr size_t kTrailerMaterialSlotRaw = 0x2C;
constexpr size_t kTrailerLodByte         = 0x30;
constexpr uint32_t kTrailerLodActiveMask = 0x04;

// The material-set block, `FUN_00e40210`.
constexpr size_t kMatSetHeaderSize  = 0x20;
constexpr size_t kMatSetCount       = 0x00; // material record count - also FUN_0074a110's bound
constexpr size_t kMatSetNameTable   = 0x10; // name-table length in bytes
constexpr size_t kStrideMatSetSlot  = 8;
constexpr size_t kStrideLookupEntry = 8;

// One material record: `u32 size` (the whole record, size field included),
// 8-align, then this `0x30`-byte header - CONFIRMED, transfers directly from
// spec-foliage-format.md Sec6 via the shared parser (Sec4.4.6(h) part 1).
// `materialRecordOffsets` (MiddleLayout) points at the leading `u32 size`,
// not at the header - the two are NOT the same offset.
constexpr size_t kMatRecordSizePrefix = 4;
constexpr size_t kMatRecordHeaderSize = 0x30;
constexpr size_t kMatRecordHash0      = 0x00;
constexpr size_t kMatRecordHash1      = 0x04;
constexpr size_t kMatRecordFlags      = 0x08;
constexpr size_t kMatRecordTexCount   = 0x0C;
constexpr size_t kMatRecordConstCount = 0x0E;
constexpr size_t kMatRecordVec4Count  = 0x0F;

// One render-mesh group, `FUN_00e3c4e0`.
constexpr size_t kRenderGroupHeaderSize = 0x60;

// The generic index-list block, `FUN_00e401d0`: a `0x10`-byte header whose
// `+0x08` is the element count, then count x 4 bytes.
constexpr size_t kIndexListHeaderSize = 0x10;
constexpr size_t kIndexListCountAt    = 0x08;
constexpr size_t kIndexListStride     = 4;

// The sentinel at `+0x78` that selects FUN_00749ba0 instead of FUN_00748c70
// (spec-physics-format.md Sec4.2). CONFIRMED - disassembly.
constexpr int32_t kPairSentinel = -1;

// Which bit of the flags byte at header `+0x08` gates FUN_00749ba0's
// collision-hull parse. Sec4.4.6(f) says "bit 2"; read as a bit INDEX
// (mask 0x04), not as the literal mask 0x02 that Sec4.2 writes for a
// different gate on the same byte. MEASURED, see note 3 at the top of this
// file - the two readings differ by 796 of 4,232 DLC entries.
constexpr uint32_t kHullGateMask = 0x04;

// The collision hull's own strides, CONFIRMED - disassembly (Sec4.4.6(f)).
constexpr size_t kHullVertexStride = 12; // 3 x float32
constexpr size_t kHullIndexStride  = 2;  // u16

constexpr size_t roundUp16(size_t v) { return (v + 15) / 16 * 16; }
constexpr size_t roundUp8(size_t v) { return (v + 7) / 8 * 8; }
constexpr size_t roundUp4(size_t v) { return (v + 3) / 4 * 4; }

// One array the chain lays down. Same shape and same discipline as
// sr3geometry::GeometryBlock::RawArray: a located byte range with a count
// and a stride, and NO interpretation of what the bytes mean. For the 56-,
// 52- and 96-byte arrays and the 12-/4-byte nested elements that is not a
// shortcut - spec-physics-format.md Sec5 lists every one of them under
// "OPEN / UNKNOWN", so inventing a decode here would be inventing data.
struct RawArray {
    size_t offset = 0; // absolute, within the content passed to parse()
    size_t count = 0;
    size_t stride = 0;
    size_t byteLength() const { return count * stride; }
    bool empty() const { return count == 0; }
};

// One outer entry of either two-level nested group: FUN_00749c40's `+0x88`
// 16-byte records with 12-byte inner elements, and FUN_00749e80's `+0xfc`
// group with its parallel count array and 4-byte inner elements.
// `innerCount` is read from the file - from the outer record's own `+0x0`
// for the first group, from the parallel array for the second - and nothing
// about the inner elements' field layout is decoded.
struct NestedEntry {
    size_t outerOffset = 0; // the 16-byte outer record (group 1 only; 0 for group 2)
    size_t innerCount = 0;
    size_t innerOffset = 0;
    size_t innerStride = 0;
    size_t innerBytes() const { return innerCount * innerStride; }
};

// One decoded inner element of a `+0x88` group (note 7 at the top of this
// file). A little-endian float32 triple, local-space (not a baked per-tile
// world position - CONFIRMED empirically: byte-identical across every
// duplicate placement of the same asset). Field order is exactly the
// on-disk slot order (slot0, slot1, slot2); nothing here asserts which
// slot is engine "up" beyond the observation, true in every measured
// sample, that slot1 is the one with the narrowest range.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// ---- the pieces the MIDDLE walk locates ----------------------------------

// One `FUN_00e401d0` index-list block. LOCATED, not decoded: `count` is the
// block's own `+0x08` and the payload is `count x 4` bytes of something
// Sec4.4.6 calls "a generic index list" without pinning the element.
struct IndexListBlock {
    size_t offset = 0;  // the 0x10-byte header (8-aligned)
    size_t count = 0;
    size_t payloadOffset = 0;
    size_t end = 0;
};

// One step over the shared "Mesh" sub-block on its c-side, `FUN_00e71410`.
// Self-validating: version word 9, and the check value repeated in the last
// 4 bytes of the declared c-length. The step is `cLength` EXACTLY.
struct MeshStep {
    size_t offset = 0;
    uint32_t checkValue = 0;
    size_t cLength = 0;
    size_t end = 0;
};

// One render-mesh group, `FUN_00e3c4e0`: a `0x60`-byte header, an index-list
// block, an embedded Mesh sub-block, then a u16 array and a u8 array (both
// `indexList.count` long, with no alignment between them).
struct RenderGroup {
    size_t offset = 0; // the 0x60-byte header, 16-aligned
    IndexListBlock indexList{};
    MeshStep mesh{};
    size_t u16ArrayOffset = 0;
    size_t u8ArrayOffset = 0;
    size_t end = 0;
};

// One material record's decoded header fields, Sec4.4.6(h) part 1. Transfers
// directly from spec-foliage-format.md Sec6 (CONFIRMED there, 33/33 records)
// via the shared parser `FUN_00e71090` - the byte layout is not re-derived
// here. The three runtime output-pointer slots (`+0x10`/`+0x18`/`+0x20`,
// zero on disk) are deliberately NOT included: there is nothing to decode.
struct MaterialRecord {
    size_t offset = 0;
    uint32_t hash0 = 0;              // +0x00 - material/shader hash; population meaning OPEN here
    uint32_t hash1 = 0;              // +0x04 - a second, per-material-variant hash; meaning OPEN
    uint8_t flags = 0;               // +0x08 - CONFIRMED for bit 0x08 (fallback-texture select); rest OPEN
    uint16_t textureBindingCount = 0; // +0x0C
    uint8_t constantNameCount = 0;    // +0x0E
    uint8_t vec4ConstantCount = 0;    // +0x0F
};

// One `0x34`-byte trailer record's decoded fields, Sec4.4.6(h) part 3. Both
// CONFIRMED - disassembly, both the ONLY offsets this record type is
// confirmed read at anywhere; `+0x00`-`+0x2B`/`+0x31`-`+0x33` remain OPEN.
struct TrailerRecord {
    size_t offset = 0;
    uint32_t materialSlotRaw = 0; // +0x2C - RAW pre-remap index; see note 8, top of file
    uint8_t lodByte = 0;          // +0x30
    bool lodActive() const { return (lodByte & kTrailerLodActiveMask) != 0; }
};

// Everything the MIDDLE walk locates. Offsets and counts only - see note 8.
struct MiddleLayout {
    bool ok = false;
    size_t offset = 0; // == 16-aligned headEnd; also the `0x50`-byte record
    size_t end = 0;
    size_t length() const { return end >= offset ? end - offset : 0; }

    size_t recordOffset = 0;   // R, the `0x50`-byte record (header `+0xb8` receives this)
    bool aborted = false;      // R[+0x00] == -1: the never-shipped abort path

    size_t materialSetOffset = 0; // H
    uint32_t materialCount = 0;   // H[+0x00] - FUN_0074a110's lookup-table bound
    size_t nameTableOffset = 0;
    size_t nameTableLength = 0;
    RawArray materialSlots{};  // H[+0x00] x 8, inside the material-set block
    std::vector<size_t> materialRecordOffsets; // each record declares its own size at +0x00
    RawArray lookupTable{};    // R[+0x08]'s materialCount x 8 - the table FUN_0074a110 indexes

    size_t renderGroupCount = 0;
    RenderGroup renderGroups[2]{};

    RawArray groupARecords{};  // R[+0x20] x 0x10, each with its own count x 0x20 sub-list
    RawArray groupBRecords{};  // R[+0x30] x 0x10, each with its own count x 4 sub-list
    RawArray trailer34{};      // R[+0x48] x 0x34

    // Per-outer-entry sub-list location, one NestedEntry per groupARecords /
    // groupBRecords entry (same shape group88()/groupFc() already use) -
    // needed to decode Sec4.4.6(h)'s groupAFeatureIndices()/
    // groupBFeatureIndices() below, since the walk above only advances a
    // shared cursor and does not otherwise remember where each entry's own
    // sub-list starts.
    std::vector<NestedEntry> groupADetail;
    std::vector<NestedEntry> groupBDetail;
};

// ---- the gated `+0x78`/`+0x7c` pair (spec-physics-format.md Sec4.4.6(f)) --

enum class GatedPairBranch {
    None,          // sentinel at `+0x78` and the flags bit clear: nothing happens at all
    ListBlock,     // `+0x78 != -1`: FUN_00748c70, a block that declares its own total size
    CollisionHull, // sentinel + flags bit 2: FUN_00749ba0, a real file-resident hull
};

// FUN_00749ba0's hull, as Sec4.4.6(f) lays it out. LOCATED and, for the two
// lists whose element size the spec states outright, decodable.
struct CollisionHull {
    size_t offset = 0;
    size_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    size_t indexOffset = 0;
    uint32_t indexCount = 0;
    size_t blobOffset = 0;
    uint32_t blobLength = 0;
    size_t vec3aOffset = 0; // the two trailing 3 x float32 values
    size_t vec3bOffset = 0;
    size_t end = 0;
};

struct GatedPair {
    GatedPairBranch branch = GatedPairBranch::None;
    size_t offset = 0;
    size_t length = 0;  // bytes the cursor advanced by, including the 16-align
    size_t blockSize = 0;   // ListBlock only: the block's declared `+0x08` total size
    uint32_t blockCount = 0; // ListBlock only: its `+0x0c` entry count (< 3)
    CollisionHull hull{};
};

// ---- controls -------------------------------------------------------------
// Every field below disables or perturbs exactly ONE confirmed term of the
// walk so a harness can score that term on its own (HANDOFF Sec3: a match
// rate with no control is not a measurement). Defaults are the confirmed
// behaviour; the reader itself never changes them.
struct WalkOptions {
    bool nameTableNulByte = true;    // MATSET's name table starts at align16(H+0x20) + 1
    bool secondRenderGroup = true;   // GROUPS walks two render groups, not one
    bool lookupTableArray = true;    // the materialCount x 8 array at R[+0x08] exists
    bool meshStepRoundUp16 = false;  // a Mesh sub-block step is cLength exactly, not rounded
    bool headIndexLists = true;      // the HEAD's count(+0x58) index-list blocks exist
    bool gatedPairZeroCost = false;  // the ListBlock branch costs 0 bytes (the old placeholder)
    // The two places Sec4.4.6's prose is ambiguous and this reader had to
    // settle the question by measurement rather than by reading (note 3):
    // which bit of the flags byte gates the collision hull, and whether the
    // do-nothing third outcome still 16-aligns. Both are scored as controls
    // rather than asserted.
    uint32_t hullGateMask = kHullGateMask;   // 0x02 is the literal-mask reading
    bool gatedPairNoneBranchAligns = false;  // true = the no-op branch aligns anyway
    // The trailing nested group is primary(`+0x108`, 8-byte) FIRST, then
    // parallel(`+0x100`, 4-byte). Sec4.2's PROSE gives the opposite order;
    // this control builds the walk that way so the prose reading is scored.
    bool trailingGroupProseOrder = false;
};

// FUN_0074a110's finalization pass, in full. CONFIRMED - disassembly
// (spec-physics-format.md Sec4.2, last row): given a min/max corner pair it
// computes half-extents, a centre, and a circumscribed-sphere radius, and
// writes them at the referenced sub-object's `+0x20..+0x28`,
// `+0x10..+0x18` and `+0x1c`. The arithmetic below is exactly that and
// nothing else.
//
// WHERE THE min/max COMES FROM is the part this reader has to choose. In the
// engine it arrives through a virtual call on the resolved reference's own
// vtable slot `+0x10`, into a sub-object the spec did not characterise.
// Sec4.3 states what that sub-object supplies: "bounding volumes are derived
// from child geometry, not read as flat file data". So the concrete source
// used here is the decoded vertex positions of the embedded Mesh sub-block
// the reference resolves to - child geometry, exactly what Sec4.3 says feeds
// it.
struct BoundingVolume {
    std::array<float, 3> min{};
    std::array<float, 3> max{};
    std::array<float, 3> halfExtent{}; // (max - min) / 2   -> engine `+0x20/+0x24/+0x28`
    std::array<float, 3> center{};     // min + halfExtent  -> engine `+0x10/+0x14/+0x18`
    float radius = 0.0f;               // |halfExtent|      -> engine `+0x1c`
    size_t vertexCount = 0;
    bool valid = false;
};

// Computes a BoundingVolume from every decodable channel of `mesh`. Returns
// `valid == false` - rather than throwing, and rather than returning zeros
// that look like a real answer - when the mesh carries no channel this
// project can decode (sr3mesh::layoutInfoFor's unprobed layout codes).
BoundingVolume computeBoundingVolume(const sr3mesh::MeshBlock& mesh);

// The same arithmetic on a min/max pair supplied directly, so the formulas
// can be tested without a Mesh sub-block in hand.
BoundingVolume boundingVolumeFromExtents(const std::array<float, 3>& min,
                                         const std::array<float, 3>& max);

class LevelMesh {
public:
    // Parses the `.clmesh_pc` in `content`. Locates the `0x4fe66afa` header
    // after the shared material block the same way sr3geometry::GeometryBlock
    // locates `0x424BD00D` - searching 16-byte-aligned candidates from
    // roundUp16(material block end) rather than computing the pad, since that
    // pad is a mandatory minimum gap and not a plain round-up (geometry_block.h
    // carries the full story). Throws FormatError if no magic is found there,
    // or if the version is not exactly 20.
    //
    // NOTE: that ONE search is the shared material block's own pre-existing
    // rule, not this format's. From the header on, nothing is searched for.
    static LevelMesh parse(ByteView content, const WalkOptions& opt = WalkOptions());

    // Same walk for a caller that already knows where the header is.
    static LevelMesh parseAt(ByteView content, size_t headerOffset,
                             const WalkOptions& opt = WalkOptions());

    size_t headerOffset() const { return headerOffset_; }
    uint32_t version() const { return version_; }
    size_t bodyStart() const { return headerOffset_ + kLevelMeshHeaderSize; }
    size_t contentSize() const { return contentSize_; }

    // The raw `0x140` header. Its first `0x40` bytes carry float data no spec
    // describes (note 5 at the top of this file); handed back as bytes, not
    // decoded.
    ByteView headerBytes(ByteView content) const;

    // Any of the kCount*/kPair*/kOut* offsets, read as a u32 exactly as the
    // sub-parsers do.
    uint32_t headerField(size_t fieldOffset) const;

    // The whole computed walk ran to completion inside the buffer.
    bool walkComplete() const { return walkComplete_; }
    // ... and finished EXACTLY at end-of-file, which is this format's one
    // structural landmark: `FUN_00749e80`'s trailing nested group is the last
    // structure in the file.
    bool landsOnEof() const { return walkComplete_ && tailEnd_ == contentSize_; }

    // ---- HEAD: FUN_007499d0 ---------------------------------------------
    const RawArray& referenceArrayA() const { return refA_; } // count `+0x48`, out `+0x40`
    const RawArray& referenceArrayB() const { return refB_; } // count `+0x58`, out `+0x50`
    // Where the count(+0x48) embedded Mesh sub-blocks begin (right after both
    // slot arrays), and each one's located extent.
    size_t meshBlockStart() const { return meshBlockStart_; }
    const std::vector<MeshStep>& headMeshBlocks() const { return headMeshes_; }
    // Array 2's `FUN_00e401d0` index-list blocks, which follow every Mesh
    // sub-block (Sec4.4.6's correction to Sec4.4.1's head formula).
    const std::vector<IndexListBlock>& headIndexLists() const { return headIndexLists_; }
    // First byte after the whole HEAD.
    size_t headEnd() const { return headEnd_; }

    // ---- MIDDLE: thunk_FUN_00e3e590's `0x50`-byte record and its payload --
    const MiddleLayout& middle() const { return middle_; }
    size_t middleOffset() const { return middle_.offset; }
    size_t middleEnd() const { return middle_.end; }
    size_t middleLength() const { return middle_.length(); }

    // Sec4.4.6(h) - see note 8 at the top of this file for exactly what is
    // and isn't decoded. One MaterialRecord per middle().materialRecordOffsets.
    std::vector<MaterialRecord> materialRecords(ByteView content) const;

    // ---- Real texture binding (measured this session, 2026-09-30) -------
    // materialRecords()[i]'s own texture-binding array, decoded into the
    // SAME sr3geometry::TextureRef/MaterialBinding shape vehicles/characters
    // already use (sr3geometry/material_binding.h) - reused deliberately,
    // not reinvented, since the 12-byte binding-entry SHAPE is identical
    // (spec-vertex-format.md Sec8.3/Sec12.8: `+0x00` name-blob byte offset,
    // `+0x04` sampler/parameter hash, `+0x08` sequential slot index) and the
    // shared per-material-record parser producing it is the SAME one
    // (`FUN_00e71090`) spec-foliage-format.md Sec6/Sec6.1 and
    // spec-vertex-format.md Sec12.8 already document for foliage/trees/
    // vehicles/characters - materialRecords() above already decodes this
    // format's own use of that record's HEADER on that basis.
    //
    // WHAT IS CARRIER-SPECIFIC, and had to be measured rather than assumed:
    // WHICH name table `+0x00`'s byte offset resolves against. Vehicles/
    // characters resolve it against the mixed-case blob inside the
    // `0x424BD00D` GeometryBlock sub-header (subheader+0x88) -
    // sr3geometry::MaterialBindings::parse()'s own mechanism - but
    // `.clmesh_pc` structurally has NO `0x424BD00D` sub-header at all
    // (spec-geometry-format.md Sec4.2: "The clean, direct answer... no -
    // confirmed, not just unconfirmed" - it has its OWN distinct
    // `0x4fe66afa` header/walk instead, which is everything this whole
    // reader implements). This format's OWN candidate name table is the one
    // `materialSetStep` already decodes as part of the MIDDLE walk -
    // middle().nameTableOffset/nameTableLength, a table private to this
    // file's own material SET (not the file-opening shared `0x00043854`
    // MaterialBlock's lowercase name table, and not a per-record-local
    // table either - both were tested and refuted below).
    //
    // MEASURED, whole real population reachable from `1018h0.str2_pc`
    // (`sr3_city_0.vpp_pc`, `spec-world-streaming.md` Sec10.1/Sec10.5's
    // "Zone (High LOD)" container): 22/22 real `.clmesh_pc` files parse
    // cleanly; every real texture-binding entry's `+0x00` offset, read
    // RELATIVE to middle().nameTableOffset (not absolute, and not relative
    // to the outer MaterialBlock's own table - see below), lands exactly on
    // a real NUL-terminated `.tga` name starting at a name BOUNDARY inside
    // middle().nameTableOffset..+nameTableLength - **143/143 (100%)**
    // bindings across every material of every file, largest sample
    // `airport_controltower.clmesh_pc` (19 materials, 43 bindings, 43/43).
    // The two alternative hypotheses were tested on the same sample and
    // both fail: relative to the outer shared `0x00043854` MaterialBlock's
    // own name table, only 6/43 resolve (coincidental prefix collisions,
    // not a real mechanism - that table is a real, CONFIRMED, but
    // DIFFERENT, copy of a similar name list, per spec-geometry-format.md
    // Sec3.1/Sec4.2); as an absolute file offset, 1/43. See
    // tools/clmesh_probe.cpp (this session's own throwaway investigation
    // tool, kept in-tree per this project's own practice of keeping the
    // tool that found a real result) for the full per-file, per-binding
    // measurement this comment summarises.
    //
    // Also measured on the same sample: the binding's `paramHash` (this
    // format's own name for spec's "sampler/parameter hash") reproduces the
    // SAME cross-format role-hash constants already confirmed for
    // characters/vehicles/foliage/trees - `0x2808EB90` (normal map),
    // `0x69B48F91` (diffuse) - plus this format's own additional per-shader
    // sampler hashes (glass reflect-mask, decal, specular-role slots) on
    // richer materials. Use `sr3geometry::kParamHash*`/`byParamHash()` -
    // the role travels with the hash, exactly as documented there; do not
    // key on slot position.
    //
    // Never throws: returns an empty MaterialBinding (textures() empty) for
    // an out-of-range `materialIndex`, a material with zero declared
    // bindings, or a file with no located material-set name table (a
    // malformed/truncated file the MIDDLE walk itself would already have
    // rejected via walkComplete() - so this is a defensive return, not an
    // expected real-data case). An individual binding ENTRY is skipped
    // (not fabricated) when its parameter hash is implausible (< 256 or
    // 0xFFFFFFFF - the same validity guard spec-vertex-format.md Sec8.4.2
    // already states for the shared mechanism) or its name offset does not
    // land on a real name-table boundary - the same "drop, don't guess"
    // discipline sr3geometry::MaterialBindings::parse() already uses.
    sr3geometry::MaterialBinding materialTextureBinding(size_t materialIndex, ByteView content) const;

    // ---- Real per-material shader constants (measured 2026-09-30) -------
    // materialRecords()[i]'s own B (constantNameCount x 4-byte name-hash)
    // and C (vec4ConstantCount x 16-byte value) arrays - the two arrays
    // this reader's own MaterialRecord has counted since Sec4.4.6(h) part 1
    // (constantNameCount/vec4ConstantCount, header +0x0E/+0x0F) but never
    // decoded the CONTENTS of, until now. Layout transfers directly from
    // spec-foliage-format.md Sec6/Sec6.2 via the shared parser
    // (`FUN_00e71090`) materialRecords()/materialTextureBinding() already
    // rely on, CONFIRMED to run unconditionally on every real .clmesh_pc
    // material record by spec-physics-format.md Sec4.4.6(h) part 1's own
    // disassembly chain-trace: array order A (12-byte texture bindings,
    // already decoded by materialTextureBinding() above) then B (4-byte
    // name hashes, 4-aligned) then C (16-byte vec4 values, 16-aligned),
    // all three starting at the SAME `rec.offset + kMatRecordHeaderSize`
    // run-start materialTextureBinding() already uses for A.
    //
    // MEASURED THIS SESSION, whole real population of
    // `airport_controltower.clmesh_pc` (19/19 materials, the largest real
    // sample on hand): every real B hash, across all 14 materials this
    // tower's own pipeline resolves to an actually-drawable real shader
    // (tools/prototype_lit_clmesh_tower2.cpp, the consumer this accessor
    // was built for), matches a REAL CTAB constant name on that exact
    // material's own resolved role4/role6 shader (CRC-32 of the
    // lower-cased name, `sr3fxo::hashLowerName` - the SAME primitive
    // spec-fxo-format.md Sec7.6 defines) - **95/95 (100%)**, ZERO
    // unmatched - see that tool's own frozen golden-scene baseline
    // (tests/golden/clmesh_airport_controltower_lit/baseline_stdout.txt)
    // for the full per-material dump; a smaller hand-probe,
    // `tools/clmesh_lead_probe.cpp` (kept in-tree), independently
    // cross-checked 24 of those 95 by hand before the full pipeline run.
    // The matched C values
    // are internally consistent with each matched name's real semantic
    // meaning in every case checked (e.g. `Normal_Map_TilingU`/`TilingV`
    // read `1.0`, `Self_Illumination` reads `0.0`, `Diffuse_Color` reads
    // `(1,1,1,1)` - a neutral "no tint" default, not the previously-assumed
    // zero).
    //
    // INDEX CORRESPONDENCE - measured, not assumed. `vec4ConstantCount`
    // (C) is NOT always equal to `constantNameCount` (B): 18/19 real
    // `airport_controltower.clmesh_pc` materials have `C == B + 2`; the
    // one exception (`ir_mesh_depth_only`, a 0-texture material) has
    // `B == 0, C == 3`. So this is decoded as positional 1:1 for
    // `i in [0, min(B,C))` - `named[i].value` is `B[i]`'s own real value -
    // with any remaining C entries (index >= B.size()) returned separately
    // as `trailingUnnamed`, NOT force-matched to a name: every real B hash
    // checked this session names a scalar/vec4 CTAB constant
    // (`registerCount == 1`), so 1 C slot per B entry is consistent with
    // every real case measured, and the constant "+2" gap (not
    // proportional to B's own size) rules out "more slots for a
    // multi-register B entry" as the explanation for the extras - but WHAT
    // the extra trailing C slots themselves ARE for remains genuinely
    // OPEN, not guessed at.
    struct ShaderConstant {
        uint32_t nameHash = 0;          // B[i]: CRC-32 of the (lower-cased) constant name, sr3fxo::hashLowerName's own primitive
        std::array<float, 4> value{};   // C[i]: the positionally-matched real vec4 value
    };
    struct ShaderConstants {
        std::vector<ShaderConstant> named;               // B[i]/C[i] pairs, i in [0, min(B,C))
        std::vector<std::array<float, 4>> trailingUnnamed; // C entries at index >= B.size() - OPEN, not matched to any name
    };
    // Never throws: returns both vectors empty for an out-of-range
    // `materialIndex` or a record whose arrays run past `content`'s own
    // size (a truncated/malformed file - defensive, not expected on real
    // data, same discipline materialTextureBinding() already uses).
    ShaderConstants materialShaderConstants(size_t materialIndex, ByteView content) const;

    // One TrailerRecord per middle().trailer34 entry.
    std::vector<TrailerRecord> trailerRecords(ByteView content) const;
    // `entry` must be one of middle().groupADetail's / groupBDetail's own
    // entries (innerStride distinguishes them, same discipline as
    // group88Positions() below) - throws FormatError otherwise.
    std::vector<uint16_t> groupAFeatureIndices(const NestedEntry& entry, ByteView content) const;
    std::vector<uint16_t> groupBFeatureIndices(const NestedEntry& entry, ByteView content) const;

    // ---- the gated `+0x78`/`+0x7c` pair ---------------------------------
    bool pairIsSentinel() const { return pairFirst_ == kPairSentinel; }
    int32_t pairFirst() const { return pairFirst_; }
    int32_t pairSecond() const { return pairSecond_; }
    const GatedPair& gatedPair() const { return gatedPair_; }
    // The two lists inside FUN_00749ba0's hull, decoded at the element sizes
    // Sec4.4.6(f) states outright (3 x float32, u16). Empty when this file
    // does not take that branch.
    std::vector<Vec3> hullVertices(ByteView content) const;
    std::vector<uint16_t> hullIndices(ByteView content) const;

    // ---- TAIL: FUN_00749b40 .. FUN_00749e80 -----------------------------
    size_t tailStart() const { return tailStart_; }
    size_t tailEnd() const { return tailEnd_; } // == contentSize() when the walk lands

    const RawArray& array70() const { return array70_; }                 // FUN_00749b40
    const RawArray& group88Outer() const { return group88Outer_; }       // FUN_00749c40 outer
    const std::vector<NestedEntry>& group88() const { return group88_; } // ... and its inner lists

    // Decodes one group88() entry's inner list as (x,y,z) float32 positions
    // (note 7 at the top of this file - CONFIRMED, not a guess: every
    // closed-loop group in the shipped population closes to the bit, and
    // the shapes match their asset names exactly). Throws FormatError if
    // `entry` is not actually a `+0x88` group entry (innerStride must be
    // exactly kStride88Inner - group88() and groupFc() share NestedEntry
    // but have different strides, and swapping them would silently
    // misdecode rather than throw without this check).
    std::vector<Vec3> group88Positions(const NestedEntry& entry, ByteView content) const;
    const RawArray& array98() const { return array98_; }                 // FUN_00749cf0
    const RawArray& arrayA8() const { return arrayA8_; }                 // FUN_00749d40
    const RawArray& arrayD8() const { return arrayD8_; }                 // FUN_00749e80 flat 1
    const RawArray& arrayE8() const { return arrayE8_; }                 // FUN_00749e80 flat 2
    const RawArray& groupFcPrimary() const { return fcPrimary_; }        // out `+0x108`
    const RawArray& groupFcParallel() const { return fcParallel_; }      // out `+0x100`
    const std::vector<NestedEntry>& groupFc() const { return groupFc_; }

    // Bytes of any located array, sliced from the SAME buffer parse() was
    // given (non-owning, the convention used everywhere else in this project).
    ByteView arrayBytes(const RawArray& a, ByteView content) const;

    // Parses the `count(+0x48)` Mesh sub-blocks the first reference array
    // resolves to, via sr3mesh::MeshBlock - the same reader every other
    // carrier of this shared block already uses. `gContent` is the paired
    // `.glmesh_pc`. Each block's g-segment is placed by the same additive
    // chain `.gzn_pc` uses (segment 0 at byte 0, each later one where the
    // previous declared length ended). A block that will not parse is
    // skipped rather than guessed at, so the returned vector can be shorter
    // than count(+0x48). Never throws.
    std::vector<sr3mesh::MeshBlock> resolveReferencedMeshes(ByteView content,
                                                            ByteView gContent) const;

private:
    size_t headerOffset_ = 0;
    size_t contentSize_ = 0;
    uint32_t version_ = 0;
    std::array<uint32_t, kLevelMeshHeaderSize / 4> headerWords_{};

    RawArray refA_{}, refB_{};
    size_t meshBlockStart_ = 0;
    std::vector<MeshStep> headMeshes_;
    std::vector<IndexListBlock> headIndexLists_;
    size_t headEnd_ = 0;

    MiddleLayout middle_{};

    int32_t pairFirst_ = kPairSentinel;
    int32_t pairSecond_ = kPairSentinel;
    GatedPair gatedPair_{};

    bool walkComplete_ = false;
    size_t tailStart_ = 0;
    size_t tailEnd_ = 0;
    RawArray array70_{}, group88Outer_{}, array98_{}, arrayA8_{}, arrayD8_{}, arrayE8_{};
    RawArray fcPrimary_{}, fcParallel_{};
    std::vector<NestedEntry> group88_;
    std::vector<NestedEntry> groupFc_;
};

// ---- the three walks, exposed for harness controls ------------------------
// Each is the exact step the reader itself runs. They are free functions so a
// harness can run one part from a deliberately wrong start, or with one term
// of WalkOptions disabled, without going through LevelMesh::parse.

struct HeadWalk {
    bool ok = false;
    size_t end = 0;
    RawArray refA{}, refB{};
    size_t meshBlockStart = 0;
    std::vector<MeshStep> meshes;
    std::vector<IndexListBlock> indexLists;
};

// Walks the HEAD from `bodyStart` using the counts in `headerWords` (the
// `0x140` header read as u32s, i.e. headerWords[k/4] is the field at `+k`).
HeadWalk walkHead(ByteView content, size_t bodyStart, const uint32_t* headerWords, size_t limit,
                  const WalkOptions& opt = WalkOptions());

// Walks the MIDDLE from `start` (which it 16-aligns itself), i.e.
// thunk_FUN_00e3e590 -> FUN_00e3e590 in full.
MiddleLayout walkMiddle(ByteView content, size_t start, size_t limit,
                        const WalkOptions& opt = WalkOptions());

// Result of one tail walk.
struct TailWalk {
    bool ok = false;  // the walk stayed inside `limit`
    size_t end = 0;   // where it finished
    GatedPair gatedPair{};
    RawArray array70{}, group88Outer{}, array98{}, arrayA8{}, arrayD8{}, arrayE8{};
    RawArray fcPrimary{}, fcParallel{};
    std::vector<NestedEntry> group88;
    std::vector<NestedEntry> groupFc;
};

// Walks the TAIL from `start`, reading per-entry inner counts and the gated
// pair's own size fields out of `content`.
TailWalk walkTail(ByteView content, size_t start, const uint32_t* headerWords, size_t limit,
                  const WalkOptions& opt = WalkOptions());

} // namespace sr3clmesh
