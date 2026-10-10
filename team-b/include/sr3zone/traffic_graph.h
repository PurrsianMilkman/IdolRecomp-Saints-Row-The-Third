// sr3zone/traffic_graph.h - reader for the `.czn_pc` Traffic (0x2236)
// record: spec-zone-data-format.md Sec13.2, the sub-paragraph beginning
// "Traffic (0x2236)'s own payload format, previously undisassembled, is now
// CONFIRMED, 2026-10-08". Read alongside spec-ai-runtime.md Sec18 ("Traffic
// AI lane-following: consumer module, pool mechanics, and Havok
// integration"), which traces the CODE that reads this DATA - this file is
// the data-format half only, per this project's established DATA/CODE
// separation convention (the same split as sr3zone/zone_objects.h vs
// sr3airuntime/navpoint_selector.h). The lane-following/graph-hopping LOGIC
// itself lives in sr3airuntime/traffic_lane_follower.h, which consumes
// TrafficRecord below rather than re-parsing anything.
//
// THIS IS A SEPARATE CONTENT KIND FROM 0x2237 (road-surface render
// geometry, this directory's own zone_geometry.h Mesh sub-block mechanism)
// AND FROM THE XTBL `Spline_Type` one-hot bitmask (spec-tables-traffic-ai.md
// Sec14.4) - Sec13.2 is explicit the Traffic record "shares no code with
// either." Do not conflate this reader with ZoneGeometry::locate(), and do
// not expect a `Spline_Type` bit anywhere in this file.
//
// WHAT IS CONFIRMED (Sec13.2, disassembly of the loader `0x00a69570`, full
// 139-instruction body, "all 14 pairs and all three special-pair mechanisms
// read instruction-for-instruction"):
//
//  * A 0x70-byte header holding 14 independent
//    `{count:s32, selfRelativePointer:s32}` sub-array descriptor pairs (an
//    exact 14*8=0x70 arithmetic closure, "not estimated").
//  * Of the 14, FIVE have a confirmed STRUCTURAL role, each keyed here by
//    its own BYTE OFFSET - not by the spec prose's pair NUMBER, see the
//    "PAIR NUMBERING" note below for why:
//      +0x00/+0x04 - the 64-byte-stride "extended" per-segment array
//                    (kExtendedSegmentArrayHeaderOffset)
//      +0x30/+0x34 - the 24-byte-stride segment array
//                    (kSegmentArrayHeaderOffset)
//      +0x38/+0x3c - the point pool (kPointPoolHeaderOffset)
//      +0x40/+0x44 - the "active traffic array" pool descriptor, which
//                    re-anchors the real engine's global singleton
//                    0x028921d0 on every zone load
//                    (kActiveTrafficPoolHeaderOffset) - CONFIRMED NOT a
//                    lane-iteration cursor (Sec13.2's own explicit
//                    correction: "most plausibly a per-zone pool of
//                    ambient-vehicle/traffic-slot handles"). Parsed here
//                    only for completeness; not consumed by
//                    sr3airuntime/traffic_lane_follower.h.
//      +0x48/+0x4c - the Sidewalks loader's own cross-link anchor
//                    (kSidewalksCrossLinkHeaderOffset) - irrelevant to this
//                    project's own lane-following use, parsed for
//                    completeness only.
//    The remaining 9 pairs (+0x08/+0x10/+0x18/+0x20/+0x28/+0x50/+0x58/
//    +0x60/+0x68) are generic self-relative-pointer fixups with NO
//    confirmed semantics (Sec13.2 does not name them individually) -
//    parsed generically (count + resolved offset only, never dereferenced).
//
//  PAIR NUMBERING - A FLAGGED, UNRESOLVED SPEC INCONSISTENCY, NOT FIXED
//  HERE. Sec13.2's own prose numbers these pairs 1-14 and states pair #1 ==
//  +0x00, pair #7 == +0x30, pair #9 == +0x40 - all three consistent with
//  the simple sequential rule "pair N starts at byte (N-1)*8". It
//  separately states pair #14 == +0x48/+0x4c, which that SAME sequential
//  rule would instead place at +0x68 (the slot pairs #1/#7/#9 already
//  establish as pair #10's). This reader does not attempt to guess which
//  number is the typo - every pair is keyed by its own CONFIRMED BYTE
//  OFFSET only, unambiguous for all five named roles above, and no "pair
//  number" is ever surfaced by this header's own API.
//
//  * The 24-byte segment record: ONLY its own +0x04 field is confirmed
//    (either the sentinel -1/null, or a "self-relative offset" into the
//    point pool). +0x00 and +0x08-+0x17 (20 of the 24 bytes) are OPEN -
//    Sec13.2's own words - stored verbatim, not modelled beyond that.
//  * The "self-relative offset" conversion rule itself. Sec13.2 names the
//    mechanism but does not re-spell the arithmetic for THIS content kind;
//    spec-zone-data-format.md Sec12.3 is the one place Team A spells out
//    exactly what "self-relative" means for one of this engine's own
//    on-disk fixup fields generally ("fixes up six self-relative offset
//    fields... each -1 -> null or a file-relative byte offset converted to
//    an absolute pointer" - a DIFFERENT content kind, the Navmesh cell
//    record, but the same engine-wide fixup CONVENTION). This reader
//    applies that exact rule, with "file" read as "this record's own
//    payload" (absolute = payloadOffset + storedValue) - the natural
//    reading for a self-contained sub-record, and the only one under which
//    every resolved offset in this reader's own synthetic test fixtures
//    lands inside the payload it came from.
//  * The 64-byte extended per-segment record, index-parallel with the
//    24-byte segment array (same N, same ordinal i - Sec13.2: "confirmed
//    index-parallel... three independent ways", including "pointer-to-
//    index recovery via signed division-by-24" by the real consumer
//    0x00b1c5a0). This reader reproduces that SAME index-parallel
//    correspondence directly - by construction, since it reads both arrays
//    by ordinal index off their own resolved offsets rather than via live
//    pointer arithmetic - the natural translation of "pointer-to-index
//    recovery by division" into a byte-buffer reader that never holds a
//    bare pointer in the first place. parseAt() throws FormatError if the
//    two arrays declare different counts (a genuine structural violation,
//    not an OPEN field). Fields read from it:
//      +0x00  u16  anchor point-pool index (feeds the quantized codec)
//      +0x1c  u32  raw word; bits 28-29 = the 2-bit "segment kind" flag
//                  (extractor 0x00a69af0 - TrafficSegmentKind() below)
//      +0x24  i16[3]  quantized offset triple A
//      +0x2a  i16[3]  quantized offset triple B
//      +0x3e  u16  CSR-style cumulative point count
//    Every other byte of the 64 is OPEN, not modelled.
//
// WHAT IS CHOSEN (the point pool's own element shape/stride and the
// quantization scale K) - Sec13.2 and spec-ai-runtime.md Sec18 are both
// explicit these are not confirmed:
//
//  * POINT POOL ELEMENT SHAPE: Sec13.2 names the point pool only as "a
//    shared point pool", with no stated per-element stride. This reader
//    CHOOSES a tightly-packed float3 (12-byte stride, x/y/z float32) - the
//    simplest shape consistent with Sec13.2's own "a generic count/
//    get-Nth-position accessor pair" framing for the CSR point-list reader
//    (0x007d1040/0x007d3830) and with the quantized codec's own "one
//    float3 anchor" wording. A future unit that traces either accessor to
//    its own last instruction should correct this if the real stride
//    differs - nothing downstream of TrafficPointPool::at() assumes
//    anything about the real accessors' own internals beyond "returns a
//    float3 by index".
//  * QUANTIZATION SCALE K: Sec13.2 confirms the formula exactly
//    (`anchor.axis + s16value * K`) and that K is "a fixed quantization-
//    scale double at 0x0115a220, value not decoded" - and this project's
//    clean-room rule forbids ever reading that address out of the shipped
//    executable, so K cannot be recovered this way regardless. The default
//    below (1.0) is a CHOSEN, explicitly neutral placeholder - every
//    caller can override it (DecodeQuantizedSegmentPoints()'s own `scale`
//    parameter) - not a claim about the real constant's magnitude.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO: decide which segments are
// graph-ADJACENT to which others (i.e. the actual lane-following topology).
// Sec13.2/Sec18.1 are explicit the real segment-to-segment hop algorithm
// was never found in any traced consumer function - only that the real
// code "hops between graph nodes/segments" using this same point-indexed
// data. Inventing a second "confirmed" adjacency fact here, on top of an
// already-CHOSEN point-pool stride, would compound one open question on
// another. See sr3airuntime/traffic_lane_follower.h's own file comment for
// the CHOSEN, geometry-based adjacency rule that file builds ON TOP of this
// reader's CONFIRMED-structure/CHOSEN-stride point-pool and CSR decode.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3zone/errors.h"
#include "vpp/byte_view.h"

namespace sr3zone {

using vpp::ByteView;

// 0x2236 - the Traffic section's own zone-content-kind id (spec-zone-data-
// format.md Sec13.2). NOT used by locate()'s own signature scan beyond the
// top-level `0x8000xxxx`-tag convention - kept here as the format's own
// name for callers that already have a payload offset from elsewhere.
constexpr uint32_t kTrafficSectionTag = 0x2236u;

constexpr size_t kTrafficHeaderPairCount = 14;
constexpr size_t kTrafficHeaderSize = kTrafficHeaderPairCount * 8; // 0x70, CONFIRMED (Sec13.2)

constexpr size_t kExtendedSegmentArrayHeaderOffset = 0x00;
constexpr size_t kSegmentArrayHeaderOffset = 0x30;
constexpr size_t kPointPoolHeaderOffset = 0x38;
constexpr size_t kActiveTrafficPoolHeaderOffset = 0x40;
constexpr size_t kSidewalksCrossLinkHeaderOffset = 0x48;

constexpr size_t kSegmentRecordStride = 24;          // 0x18, CONFIRMED (Sec13.2)
constexpr size_t kExtendedSegmentRecordStride = 64;  // 0x40, CONFIRMED (Sec13.2)
constexpr size_t kPointPoolElementStride = 12;       // CHOSEN (float3) - see file comment

constexpr int32_t kSelfRelativeNullSentinel = -1; // CONFIRMED (Sec13.2: "-1 -> null")

// CHOSEN neutral placeholder for the real 0x0115a220 quantization-scale
// double - see this header's own file comment. NOT a sampled/measured
// value.
constexpr double kDefaultQuantizationScale = 1.0;

// A decoded point-pool element - see file comment re CHOSEN stride.
struct TrafficPoint {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// One generic header sub-array descriptor pair, resolved - every one of the
// 14 (Sec13.2). `resolvedOffset` is only meaningful when `present`.
struct TrafficSubArrayRef {
    int32_t count = 0;
    int32_t rawPointer = kSelfRelativeNullSentinel;
    bool present = false;  // rawPointer != -1. `count == 0` is NOT itself
                            // treated as "absent" - Sec13.2 does not state
                            // that convention, so this reader does not
                            // invent it; a present-but-zero-count array is
                            // reported as such, not collapsed.
    size_t resolvedOffset = 0;
};

// pair #8's point pool, CHOSEN float3 stride (see file comment above).
class TrafficPointPool {
public:
    TrafficPointPool() = default;
    TrafficPointPool(ByteView content, TrafficSubArrayRef ref);

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

    // Throws std::out_of_range if index >= size().
    TrafficPoint at(size_t index) const;

private:
    ByteView content_;
    size_t base_ = 0;
    size_t count_ = 0;
};

// One 24-byte segment-array record (pair at +0x30/+0x34). Field-by-field
// per the file comment above - only `pointOffsetRaw`/`hasPointOffset` are
// CONFIRMED to mean anything; the rest is stored verbatim, OPEN.
struct TrafficSegmentEntry {
    size_t index = 0;  // ordinal index, shared with TrafficSegmentExtended
    uint32_t fieldAt0x00 = 0;  // OPEN
    bool hasPointOffset = false;                         // +0x04 != sentinel
    int32_t pointOffsetRaw = kSelfRelativeNullSentinel;   // +0x04, raw stored value
    size_t resolvedPointOffset = 0;  // payloadOffset + pointOffsetRaw; meaningful only if hasPointOffset
    std::array<uint8_t, 16> rawOpenBytes{};  // +0x08..+0x17, OPEN, verbatim
};

// One 64-byte extended-segment-array record (pair at +0x00/+0x04),
// index-parallel with TrafficSegmentEntry at the same `index`.
struct TrafficSegmentExtended {
    size_t index = 0;
    uint16_t anchorPointIndex = 0;              // +0x00
    uint32_t rawFieldAt0x1c = 0;                // +0x1c, raw (TrafficSegmentKind() extracts bits 28-29)
    std::array<int16_t, 3> quantizedOffsetA{};  // +0x24
    std::array<int16_t, 3> quantizedOffsetB{};  // +0x2a
    uint16_t cumulativePointCount = 0;          // +0x3e
};

// extractor `0x00a69af0`: bits 28-29 of +0x1c. CONFIRMED (Sec13.2: "a
// 2-bit 'segment kind' flag at bits 28-29... at least one consumer branches
// away from its 'normal' path when non-zero").
inline uint8_t TrafficSegmentKind(const TrafficSegmentExtended& ext) {
    return static_cast<uint8_t>((ext.rawFieldAt0x1c >> 28) & 0x3u);
}

// The quantized anchor+int16-offset position codec, `0x00a6a442`
// (CONFIRMED formula; CHOSEN pool/scale - see file comment). `valid` is
// false iff `ext.anchorPointIndex` is out of `pool`'s range (never
// throws).
struct QuantizedSegmentPoints {
    bool valid = false;
    TrafficPoint pointA;  // anchor + quantizedOffsetA * scale, per axis
    TrafficPoint pointB;  // anchor + quantizedOffsetB * scale, per axis
};
QuantizedSegmentPoints DecodeQuantizedSegmentPoints(const TrafficSegmentExtended& ext, const TrafficPointPool& pool,
                                                     double scale = kDefaultQuantizationScale);

// The CSR-style per-segment point-list range (Sec13.2: "a CSR-style
// cumulative point-count at +0x3e driving a variable-length per-segment
// 3-D point-list copy"). CONFIRMED: the CSR indexing SHAPE (a prefix-sum
// range per ordinal segment: segment i owns pool indices
// [cumulative(i-1), cumulative(i)), cumulative(-1) == 0). Returns an empty
// range {0,0} if `segmentIndex` is out of range.
struct SegmentPointRange {
    uint16_t begin = 0;
    uint16_t end = 0;  // exclusive
};
SegmentPointRange SegmentCsrRange(const std::vector<TrafficSegmentExtended>& extendedSegments, size_t segmentIndex);

// Resolves one segment's own CSR range (via SegmentCsrRange()) against
// `pool` and returns the decoded point list, in order. CHOSEN: this reader
// resolves the CSR range against the SAME point pool the quantized codec's
// anchor lookup uses - `0x007d1040`/`0x007d3830`'s own backing object is
// OPEN per Sec13.2 ("object class not identified... whether... is really
// pair #8's point pool... was not cross-checked"); this is this file's own
// documented choice, not a disassembly-confirmed fact. Indices that fall
// outside `pool`'s own size are silently dropped (never throws) rather than
// treated as a hard parse error - a point pool smaller than a segment's own
// declared CSR range is reported as a short list, not a crash.
std::vector<TrafficPoint> DecodeSegmentPointList(const std::vector<TrafficSegmentExtended>& extendedSegments,
                                                  size_t segmentIndex, const TrafficPointPool& pool);

// A fully parsed Traffic (0x2236) record.
struct TrafficRecord {
    size_t payloadOffset = 0;
    std::array<TrafficSubArrayRef, kTrafficHeaderPairCount> rawPairs{};  // all 14, by slot index (byteOffset == slot*8)

    std::vector<TrafficSegmentEntry> segments;             // +0x30/+0x34
    std::vector<TrafficSegmentExtended> extendedSegments;  // +0x00/+0x04, index-parallel with `segments`
    TrafficPointPool pointPool;                             // +0x38/+0x3c

    // Thin named wrappers over rawPairs[] for the two further
    // structurally-named-but-not-dereferenced pairs (see file comment) -
    // not separate storage.
    const TrafficSubArrayRef& activeTrafficPoolRef() const {
        return rawPairs[kActiveTrafficPoolHeaderOffset / 8];
    }
    const TrafficSubArrayRef& sidewalksCrossLinkRef() const {
        return rawPairs[kSidewalksCrossLinkHeaderOffset / 8];
    }
};

class TrafficGraph {
public:
    // Scans `cznContent` for a top-level Traffic-record chain entry - a
    // 4-aligned `u32 tag == (0x80000000 | kTrafficSectionTag)` - BY ANALOGY
    // with the already-confirmed 0x2234 top-level record shape (sr3zone/
    // zone_objects.h: "every real top-level section id in this engine
    // ships as 0x8000xxxx... 12-byte {tag,length,extra}, extra confirmed
    // 0") - NOT independently re-confirmed for 0x2236 specifically by this
    // project (CHOSEN, by analogy, not CONFIRMED for this exact tag).
    // Parses the payload (12 bytes past the tag) via parseAt() for each
    // candidate; keeps only candidates that parse successfully. Never
    // throws; returns an empty vector if none are found/valid.
    static std::vector<TrafficRecord> locate(ByteView cznContent);

    // Parses exactly one Traffic payload at a KNOWN offset (the 0x70-byte
    // header's own start, i.e. already past any top-level chain header).
    // Throws FormatError if the buffer is too small for the header, if any
    // of the 5 structurally-special pairs resolves outside the buffer, or
    // if `segments`/`extendedSegments` declare different counts (the one
    // hard CONFIRMED invariant - Sec13.2's "index-parallel... same N").
    static TrafficRecord parseAt(ByteView cznContent, size_t payloadOffset);
};

}  // namespace sr3zone
