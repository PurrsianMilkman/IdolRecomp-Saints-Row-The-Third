// Reader for the shared "Mesh" sub-block's index buffer and per-vertex
// channel data (spec-vertex-format.md).
//
// This is the structure `sr3geometry` deliberately located-but-did-not-
// parse, and `sr3foliage` likewise. It is shared engine machinery embedded
// by at least six carrier formats, so this reader is written against the
// block itself rather than against any one carrier.
//
// THE PRE-HEADER FIELD OFFSETS WERE MEASURED, NOT ASSUMED. spec §2 states
// that a check value, a c-length and a g-length sit "immediately before
// the header" without fixing their offsets relative to the version field.
// Rather than guess, they were identified against an external oracle: the
// g-file's first u32 IS the check value and its size IS the g-length
// (spec §4.1, 1,937/1,937), so locating those two values in the c-file
// pins the layout. Result, on 400 real paired meshes: the check value sits
// at +0x04 in 400/400 and the g-length at +0x0C in 400/400. Two
// independent 32-bit values landing at fixed offsets across 400 files is
// not a coincidence a wrong reading could produce.
//
// WHAT THIS READER TRUSTS, AND WHY IT MATTERS:
//
//   The per-vertex stride comes from the channel record's OWN size bytes
//   (+0x04 plus +0x07), not from the layout-code table. The spec gives a
//   stride law - `base(layout_code) + 4 x texcoord_count`, exact on
//   26,601/26,601 channels - and this reader checks against it and reports
//   disagreement, but the record's declared value wins. That is this
//   project's own repeated lesson (HANDOFF.md §5 item 6, §9.4): when a
//   structure declares its own size, read it rather than re-deriving it.
//   Here it also means an unprobed layout code (11, 13, 24) still gets a
//   correct stride even though its field layout is unknown.
//
// WHAT IS DELIBERATELY NOT DECIDED HERE:
//
//  * PER-MATERIAL DRAW RANGES. Whether the index buffer subdivides into
//    per-material batches (via the still-undecoded outer six-array
//    structure) is open. It does not affect geometric correctness, only
//    the ability to issue per-material draw calls later.
//
// THREE THINGS THAT *WERE* OPEN AND ARE NOW SETTLED (spec corrected
// 2026-09-11, same day as its first release - read the current file, not a
// remembered version of it):
//
//  * TEXCOORDS ARE SIGNED 16-BIT FIXED POINT, SCALE 1024 - `int16 / 1024`.
//    NOT half-float. That reading is refuted outright: 94.6% of 2,222,554
//    shipped values have a half-float exponent field of zero, so almost
//    the whole population would decode as denormals at ~0.0001 and
//    collapse every mesh to a point. Signed matters too - 2.79% of values
//    exceed 32767, and read unsigned those become coordinates up to 63.99
//    instead of small negatives.
//  * +12 IS THE NORMAL, +16 IS THE TANGENT, settled by their fourth bytes:
//    the one at +16 takes exactly two values (0 and 255) and is therefore a
//    handedness SIGN, while the one at +12 carries 113 distinct values of
//    real per-vertex content. Use the sign when reconstructing a
//    bitangent or mirrored UV islands light wrong. The normal's own fourth
//    byte remains OPEN - now known not to be handedness.
//  * THE INDEX BUFFER IS A TRIANGLE STRIP, not a list (spec §8.1).
//    Separate strips are stitched with repeated indices forming zero-area
//    triangles, which are simply dropped. This is what the 38% of index
//    counts not divisible by three were telling us all along - a list
//    reading yields 0.532 non-degenerate triangles per vertex, which is
//    topologically impossible for a surface, against 1.594 for the strip
//    reading. See triangleListIndices().

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3mesh/errors.h"
#include "vpp/byte_view.h"

namespace sr3mesh {

using vpp::ByteView;

constexpr uint32_t kMeshVersion = 9;
constexpr size_t kHeaderSize = 0x70;
constexpr size_t kChannelRecordSize = 24;

// Pre-header layout, measured on 400 real paired meshes (see above).
constexpr size_t kVersionOffset = 0x00;
constexpr size_t kCheckValueOffset = 0x04;
constexpr size_t kCLengthOffset = 0x08;
constexpr size_t kGLengthOffset = 0x0C;
constexpr size_t kHeaderStart = 0x10;

// Header field offsets, relative to kHeaderStart (spec §2).
constexpr size_t kFlagsOffset = 0x00;
constexpr size_t kChannelCountOffset = 0x10;
constexpr size_t kIndexCountOffset = 0x20;
// u16 count of the BONE PALETTE (HANDOFF Sec9.62). spec-geometry-format.md
// Sec4.1.1 confirms, by disassembly, that the loader walks a u16-count-
// gated array declared here (`+0x38`/`+0x40`), but left its content a
// HYPOTHESIS ("a plain 16-bit index buffer"). It is not an index buffer.
// Read directly from shipped character meshes, it is a list of `count`
// u8 RIG BONE INDICES, strictly ascending, physically placed immediately
// after the channel records - and every vertex blend index in the block's
// skinned channels indexes THIS list, not the rig's bone array. See
// bonePalette() below and HANDOFF Sec9.62 for the evidence.
constexpr size_t kBonePaletteCountOffset = 0x38;
// u16 count of BONE PALETTE SETS (spec-geometry-format.md Sec4.1.1's
// second u16-count-gated array, `+0x48`/`+0x50`, "flat count x 2 bytes"):
// that many 2-byte SET DESCRIPTORS - [u8 count][u8 start offset into the
// palette] - follow the palette bytes. 1 set on brad/angel/brute
// ({56,0}/{54,0}/{53,0}); 2-3 sets on 46/318 character meshes, where the
// palette is several ascending runs concatenated and each set's start is
// the running sum of the earlier counts (e.g. liilian: {64,0},{62,64},
// {12,126}, 64+62+12 == 138 == palette). See bonePaletteSets().
constexpr size_t kBonePaletteSetCountOffset = 0x48;
constexpr size_t kIndexSizeOffset = 0x30;

constexpr uint8_t kFlagBulkInGFile = 0x01; // bit 0
constexpr uint8_t kFlagMultiStream = 0x04; // bit 2 - alternative representation, not supported here

// Which optional fields a layout code carries. Derived from spec §5's base
// table plus §6's field order; `known` is false for codes whose element
// layout the spec has not probed at all - as of this writing, every real
// shipped code has at least a HIGH-CONFIDENCE-or-better field decode
// (11/12/13/24 included, the last four to be resolved, spec-vertex-
// format.md Sec12.9-Sec12.12), so `known=false` currently only reflects a
// hypothetical unrecognised/future layout code.
struct LayoutInfo {
    bool known = false;
    uint32_t base = 0;
    bool hasNormal = false;
    bool hasTangent = false;
    bool hasSkinning = false;   // weights + indices, 8 bytes
    bool hasRigidPart = false;  // vehicles: one part index broadcast over 4 bytes

    // Bytes to skip in decodeChannel()'s cursor walk WITHOUT decoding them
    // into any Vertex field - a structural gap the stride law already
    // counts (spec's own `base` includes it) but whose CONTENT this reader
    // does not read. Zero for every layout code except 24 and 11/12/13:
    // code 24's `+16` slot was tested as a tangent and REFUTED
    // (spec-vertex-format.md Sec12.3 - pooled unit-vector rate only 8.0%
    // across 448 real channels, far below the ~100% a real tangent
    // shows), and its actual content is OPEN, not merely unconfirmed.
    // `known=true` is set for code 24 below despite that - position+normal
    // are independently CONFIRMED at full population (581,647/581,647
    // each) and are all this reader decodes; `reservedBytes` is what keeps
    // the cursor correctly landing on the real texcoords at `+20` instead
    // of misreading them starting 4 bytes early, at `+16`, without ever
    // interpreting what `+16` itself holds. See decodeChannel()'s own
    // cursor-arithmetic comment for where this is consumed.
    //
    // Codes 11/12/13 (trees, spec-vertex-format.md Sec12.12) reuse this
    // SAME field for their own trailing gap: bytes `+16..+32` of the
    // 32-byte base, which is the near-duplicate FLOAT16x3 pattern
    // (`+16..+22`, Sec12.9.7's ~70%-byte-identical-to-`+0` finding -
    // deliberately never read into a second Vertex field, same "reserved,
    // unread" treatment as code 24's own `+16` slot above) PLUS 10 further
    // unidentified bytes (`+22..+32`) - 16 bytes total, `reservedBytes=16`
    // for those three codes. See `hasPositionFloat16`/
    // `reservedBytesAfterPosition` below for their OTHER (leading) gap,
    // which this one field alone cannot express.
    uint32_t reservedBytes = 0;

    // Added for codes 11/12/13 (trees) - appended at the END of the struct
    // so every PRE-EXISTING layout code's positional aggregate-init list
    // (`{true, 16, true, false, false, false}` etc., codes 0-4/100/101)
    // keeps initialising exactly the same 6 leading fields it always did;
    // these two new fields silently default (false/0) for all of them,
    // which is what makes this addition a provable no-op elsewhere - the
    // same "append, never insert" discipline HANDOFF.md's "rule 17" is
    // about (a fixed structural slot misread by POSITION, not name).

    // True only for codes 11/12/13: Position is FLOAT16x3 (3x IEEE
    // half-precision floats, 6 bytes) at offset `+0`, not the plain
    // FLOAT32x3 (12 bytes) every other positioned code uses.
    // spec-vertex-format.md Sec12.12: HIGH CONFIDENCE - empirical, full
    // population (28,423/28,457 code 11, 609/609 code 12, 1,172/1,172
    // code 13 in-bounds against each tree's own real bbox; every channel
    // of every code at or above 90%), independently reproduced twice -
    // but explicitly NOT CONFIRMED: no CPU/GPU consumer of this byte
    // range has been found (Sec12.9.1's existing finding that neither
    // Mesh-block walker dereferences individual vertex fields extends to
    // this range too), and `+0`/`+16` are ~70% byte-identical
    // cross-channel duplicates with an existing, still-open hedge that
    // this pair could be runtime-populated/stale rather than authored
    // per-vertex data (Sec12.9.7) - this FLOAT16 finding is new evidence
    // TOWARD "real, authored data", not proof against "stale". Every
    // caller decoding Position via this flag should treat it at the same
    // HIGH-CONFIDENCE tier, one below every other CONFIRMED field this
    // reader exposes (Normal/Tangent included, even on the SAME layout
    // codes - only Position carries this caveat).
    bool hasPositionFloat16 = false;

    // The gap between Position and the next field (Normal, at `+8` for
    // codes 11/12/13) that the 12-byte-FLOAT32-position cursor arithmetic
    // has no room to express: FLOAT16x3 is only 6 bytes, but Normal still
    // sits at the SAME `+8` offset codes 11/12/13 already share, leaving a
    // 2-byte gap (`+6..+8`) whose content is UNKNOWN - not investigated by
    // this task's own scope, reserved/skipped like every other unread gap
    // in this format, never guessed. 0 for every other layout code
    // (default), so a positive value here is unique to 11/12/13.
    uint32_t reservedBytesAfterPosition = 0;
};

LayoutInfo layoutInfoFor(uint8_t layoutCode);

// One 24-byte channel record (spec §3).
struct Channel {
    uint32_t elementCount = 0;
    uint8_t sizeA = 0;
    uint8_t layoutCode = 0;
    uint8_t texcoordCount = 0;
    uint8_t sizeB = 0;

    size_t stride() const { return static_cast<size_t>(sizeA) + sizeB; }
    size_t dataOffset = 0; // absolute offset within the g-segment buffer
    size_t dataBytes = 0;

    // The stride the spec's law predicts. Equal to stride() on every real
    // channel measured; kept separate so a disagreement is visible rather
    // than silently resolved.
    size_t predictedStride = 0;
    bool strideMatchesLaw = false;
};

// Texcoord fixed-point scale: 1024 represents 1.0 (spec §6.5).
constexpr float kTexcoordScale = 1024.0f;

// A decoded vertex. Only fields the layout actually carries are populated.
struct Vertex {
    std::array<float, 3> position{};

    // UBYTE4N-decoded xyz, (b/255)*2-1 (spec §6.2).
    std::array<float, 3> normal{};
    // The normal's fourth byte. Real per-vertex content (113 distinct
    // values), NOT handedness and NOT padding - meaning OPEN (spec §6.2).
    // Surfaced raw so it is neither dropped nor misused as a sign.
    uint8_t normalW = 0;

    std::array<float, 3> tangent{};
    // The tangent's fourth byte is a bitangent-handedness sign ON
    // CHARACTERS/ITEMS ONLY - CONFIRMED there at 100.0000% extreme (0 or
    // 255) over 1,803 meshes / 3,346,977 vertices across two archives, zero
    // intermediate values. IT DOES NOT HOLD ON VEHICLES: measured at only
    // 17.9837% extreme over 372 meshes / 3,277,372 vertices - 82% of
    // vehicle tangentW bytes are spread smoothly across 1..254, peaking
    // near the midpoint (127/128), not at either extreme. `bitangent()`
    // below is NOT a valid handedness reconstruction for vehicle meshes;
    // do not call it there until this is re-derived. Measured 2026-09-12,
    // HANDOFF Sec9.50 - see that section before trusting this byte on any
    // carrier not already listed here as confirmed.
    uint8_t tangentW = 0;
    float tangentHandedness = 1.0f;

    std::array<uint8_t, 4> blendIndices{}; // 255 = unused lane
    std::array<float, 4> blendWeights{};
    uint8_t rigidPartIndex = 0;

    // Decoded as int16 / 1024 (spec §6.5). Signed - see the header.
    std::vector<std::array<float, 2>> texcoords;
    // The underlying signed 16-bit values, for diagnostics and for anyone
    // wanting to re-examine the encoding without re-reading the file.
    std::vector<std::array<int16_t, 2>> texcoordsRaw;

    // Bitangent from the stored handedness sign. Dropping the sign makes
    // mirrored UV islands light inside-out, which is why it is offered
    // here rather than left to each caller.
    //
    // CARRIER-SCOPED, NOT UNIVERSAL: the sign this reads from `tangentW` is
    // CONFIRMED only for characters/items (100.0000% extreme over 1,803
    // meshes). On VEHICLES it is measured at only 17.98% extreme - see the
    // comment on `tangentW` above and HANDOFF Sec9.50. Calling this on a
    // vehicle mesh today returns a value derived from a byte that is not a
    // clean sign for 82% of its vertices; nothing currently in this
    // project calls it on vehicles, and nothing should until re-derived.
    std::array<float, 3> bitangent() const {
        std::array<float, 3> b{normal[1] * tangent[2] - normal[2] * tangent[1],
                               normal[2] * tangent[0] - normal[0] * tangent[2],
                               normal[0] * tangent[1] - normal[1] * tangent[0]};
        for (float& v : b) v *= tangentHandedness;
        return b;
    }
};

class MeshBlock {
public:
    // Parses the Mesh sub-block at `meshOffset` within `cContent`, taking
    // bulk data from `gContent` when flags bit 0 is set (or from
    // `cContent` at the same cursor when it is clear - the inline case).
    //
    // Throws FormatError if the version is not 9, if flags bit 2 selects
    // the unsupported multi-stream representation, if any structure runs
    // past its buffer, or if the g-segment walk fails the spec's own
    // contract: consume exactly the declared g-length and land on a repeat
    // of the check value at both ends.
    //
    // `gSegmentOffset` is the byte offset WITHIN `gContent` at which this
    // block's g-segment begins. It defaults to 0 - the single-segment
    // carriers (characters, props, vehicles, trees, foliage) hand over a
    // buffer whose segment starts at byte 0, so every pre-existing caller
    // is byte-for-byte unaffected by this parameter existing. It is
    // non-zero only for a CHAINED carrier (`.gzn_pc`; see
    // spec-zone-data-format.md Sec10.4/Sec10.5), where segment k>0 starts
    // at whatever 4-aligned offset the running total of the preceding
    // segments' declared g-lengths lands on.
    //
    // WHY IT IS NEEDED AND NOT COSMETIC: the channel-array padding inside
    // the segment aligns to the ABSOLUTE 16-byte grid of the g-file, not
    // to the segment's own start, so the declared g-length is only
    // reproducible when the walk knows where the segment really begins.
    // Measured on `sr3_city~s0715` (12 g-backed segments, chain tiling
    // exactly to the 557,224-byte EOF): replaying with the absolute grid
    // reproduces the declared g-length in 12 / 12; replaying with a
    // segment-relative grid reproduces it only for the 2 segments that
    // happen to be 16-aligned and is wrong by +4 or +12 on the other 10
    // (1,280 -> 1,284; 720 -> 724; 1,176 -> 1,180; 234,088 -> 234,100;
    // 60,832 -> 60,836; 8,000 -> 8,004; 10,320 -> 10,324; 2,800 -> 2,804;
    // 235,240 -> 235,244; 948 -> 960).
    //
    // `headerDisplacement` is where the 0x70-byte header starts relative
    // to `meshOffset`. It defaults to kHeaderStart (0x10), which is what
    // every pre-existing caller gets. The real, measured rule is
    // `round_up(meshOffset + 16, 8) - meshOffset`, i.e. 0x10 for an
    // anchor at 0 (mod 8) and 0x14 for one at 4 (mod 8), where a zero
    // filler word occupies +0x10..+0x13 (HANDOFF Sec9.55.2, confirmed by
    // two independent oracles: 2,596 anchors at 0x10, 247 at 0x14, zero
    // ambiguous; spec-zone-data-format.md Sec10.1). Making that rule the
    // DEFAULT would change behaviour for every carrier sharing this
    // parser and is deliberately not done here - the caller that has
    // measured its own population opts in. sr3zone::ZoneGeometry::locate()
    // is currently the only one that does.
    static MeshBlock parse(ByteView cContent, size_t meshOffset, ByteView gContent,
                           size_t gSegmentOffset = 0,
                           size_t headerDisplacement = kHeaderStart);

    uint32_t checkValue() const { return checkValue_; }
    uint32_t cLength() const { return cLength_; }
    uint32_t gLength() const { return gLength_; }
    uint8_t flags() const { return flags_; }
    bool bulkInGFile() const { return (flags_ & kFlagBulkInGFile) != 0; }

    uint32_t indexCount() const { return indexCount_; }
    uint8_t indexElementSize() const { return indexElementSize_; }
    const std::vector<uint16_t>& indices() const { return indices_; }

    // One indexed draw descriptor (spec §8.2, 20 bytes).
    struct DrawRange {
        // The 20-byte record's +0x00 is packed 16:16, not a plain u32:
        //     materialId    = field & 0xFFFF
        //     submeshIndex  = field >> 16
        //
        // Measured over both populations before this was adopted:
        //   characters  0 / 6,402 ranges have a non-zero high half
        //               (max raw value 6) - so the earlier plain-u32
        //               reading was never wrong there and masking is a
        //               PROVABLE no-op, not a hopeful one;
        //   vehicles    77,886 / 87,012 ranges have one, with high values
        //               running 0..14 in a smooth descending distribution -
        //               the shape of a sub-mesh index. Read unpacked they
        //               produce ids like 917,541, which is what a 16:16
        //               pack misread as one value looks like.
        //
        // Masked unconditionally rather than per format: it changes nothing
        // on characters, by measurement.
        uint32_t materialId = 0;
        // High half - this project's OWN OTHER NAME for "high16"
        // (spec-vehicle-geometry.md §11.1). RESOLVED, not a hypothesis:
        // indexes this SAME mesh's own vertex CHANNEL array directly
        // (MeshBlock::channels()[submeshIndex]) - CONFIRMED by two
        // independent methods, HANDOFF.md §9.89 (2026-09-28): a real
        // disassembly read (spec-render-pipeline.md §18.6, no scaling/
        // masking) AND a full-population per-item join, 393/393 vehicles,
        // 90,290/90,290 draw ranges, zero exceptions. Sharper corroboration
        // added HANDOFF.md §9.103 (2026-09-29): per-submeshIndex max USED
        // index across all real draw groups equals that channel's own
        // elementCount-1 exactly, 9/9, not just index < channelCount.
        // This field carrying two names (submeshIndex here, high16 in the
        // spec text) is WHY this comment sat stale for a day after §9.89
        // closed it - see "one field, one name" in the rules list.
        uint32_t submeshIndex = 0;
        uint32_t startIndex = 0;
        uint32_t indexCount = 0;
        uint32_t minVertex = 0;
        uint32_t maxVertex = 0;
    };

    // Draw groups, CONFIRMED to be LOD levels (spec §8.2, "now confirmed by
    // rendering, not inferred" - each group independently rendered, same
    // pose/proportions/silhouette with detail falling away, refuting the
    // competing "mesh parts" hypothesis by direct visual inspection; fixed
    // from "believed" 2026-09-29, rule 16). Group 0 is the highest detail.
    // Empty if the group array could not be located - see
    // drawGroupsLocated().
    const std::vector<std::vector<DrawRange>>& drawGroups() const { return drawGroups_; }

    // False if the group/range array could not be found and validated. The
    // spec does not pin its offset unconditionally (it sits after the
    // channel array AND after flag-bit-1 arrays of unstated size), so this
    // reader SEARCHES 16-byte-aligned candidates and accepts only one that
    // satisfies the spec's own invariants: ranges contiguous from zero,
    // together covering the whole index buffer, with
    // minVertex <= maxVertex < vertex count.
    //
    // The search takes the FIRST candidate that validates, which is only
    // sound if no second one does. That used to be asserted here ("strong
    // enough that a wrong offset cannot pass") without ever being counted -
    // and a match rate with no ambiguity count is not a measurement
    // (HANDOFF §3). Now measured: `tools/validation/validate_groupsearch.cpp`
    // replicates the search independently and counts every surviving
    // candidate instead of stopping at the first. Result: **exactly one
    // survivor in 549 / 549 meshes**, no mesh with two. The invariants are
    // as strong as claimed - now on evidence rather than on confidence.
    bool drawGroupsLocated() const { return !drawGroups_.empty(); }

    // Expands the triangle strip of ONE draw range into a triangle list.
    // This is the correct way to consume the buffer: ranges are unrelated
    // pieces of the model, and reading across a boundary fabricates
    // triangles bridging them (spec §8.2).
    std::vector<uint32_t> triangleListForRange(const DrawRange& range) const;

    // All ranges of one group, expanded and concatenated - each range
    // restarted, never joined. Falls back to the whole buffer only if the
    // groups could not be located, which is reported rather than hidden.
    std::vector<uint32_t> triangleListForGroup(size_t groupIndex) const;

    // Expands the whole index buffer as ONE strip. Retained only to show
    // what the bridging artifact looks like; not how the format should be
    // drawn. Prefer triangleListForGroup().
    //
    // Winding is alternated per step, as strips require, so the resulting
    // list has consistent facing.
    //
    // Preferred over handing the strip straight to the GPU: the degenerate
    // triangles would self-cull there anyway, but expanding here makes the
    // real triangle count observable, which is exactly the statistic that
    // distinguished strip from list in the first place.
    std::vector<uint32_t> triangleListIndices() const;

    // Kept only as a diagnostic. It is NOT a usability test any more:
    // strips have no divisibility requirement, and ~38% of shipped blocks
    // failing it was in fact the first hint that these were not lists.
    bool indexCountDivisibleByThree() const { return indexCount_ % 3 == 0; }

    const std::vector<Channel>& channels() const { return channels_; }

    // The block's BONE PALETTE (HANDOFF Sec9.62): bonePalette()[k] is the
    // RIG bone index that vertex blend index `k` refers to, for every
    // skinned channel in this block. Empty when the header declares no
    // palette (count 0 at kBonePaletteCountOffset), when the block is
    // inline-mode (its bulk segment occupies the same cursor and no
    // inline-mode carrier has a skinning layout), or when the declared
    // bytes do not fit in the content - so a consumer must compare
    // bonePalette().size() against bonePaletteDeclaredCount() before
    // relying on it, rather than treating "empty" as "no palette". That
    // is deliberate: this reader is shared by six carrier formats whose
    // populations were validated before this field was understood, and a
    // throw here would change their behaviour; the sweep in
    // tools/validation/validate_bone_palette.cpp is where the population-
    // level invariants (ascending, in range, complete) are enforced.
    //
    // Measured (2026-09-13) on brad/angel/brute_flamethrower: 56/54/53
    // entries, every one a valid rig index, strictly ascending, count ==
    // highest blend index used + 1. The list is "the bones this mesh
    // skins to, in rig order" - helper bones (camera rig, handprop
    // attachment markers, spinebend, upperarmtwist1) are simply absent.
    const std::vector<uint8_t>& bonePalette() const { return bonePalette_; }
    uint16_t bonePaletteDeclaredCount() const { return bonePaletteDeclaredCount_; }

    // The palette's SETS: the 2-byte descriptors that follow the palette
    // bytes, `bonePaletteSetCountDeclared()` of them (header +0x48). Set k
    // is bonePalette()[start .. start+count), its own ascending run, and a
    // draw range that uses set k carries blend indices in [0, count). On
    // brad there is one set, {56, 0}. A block with more than one set
    // needs a per-draw-range set assignment that this reader does NOT
    // resolve (HANDOFF Sec9.62 - OPEN); consumers that need a single
    // palette must check bonePaletteSets().size() == 1 before using
    // bonePalette() as one list. Same non-fatal-on-truncation rule as
    // bonePalette().
    struct BonePaletteSet {
        uint8_t count = 0;
        uint8_t start = 0;
        bool operator==(const BonePaletteSet& o) const { return count == o.count && start == o.start; }
    };
    const std::vector<BonePaletteSet>& bonePaletteSets() const { return bonePaletteSets_; }
    uint16_t bonePaletteSetCountDeclared() const { return bonePaletteSetCountDeclared_; }

    // WHICH palette SET each draw range uses (HANDOFF Sec9.63.10). One
    // entry per draw range, in the same FLAT FILE ORDER the ranges appear
    // in - i.e. drawGroups()[0][0], [0][1], ..., [1][0], ... - so entry k
    // belongs to the k-th range of that concatenation. A vertex drawn by
    // range k whose blend lane says `j` is skinned to
    //     bonePalette()[ bonePaletteSets()[ drawRangePaletteSets()[k] ].start + j ]
    // rather than to bonePalette()[j]. On a single-set mesh every entry is
    // 0 and that formula collapses to the plain bonePalette()[j] reading
    // (272/272 measured), so a consumer can use it unconditionally.
    //
    // WHERE IT IS: immediately after the 20-byte draw-range records, with
    // no alignment and no gap, an array of `total range count` 8-byte
    // records. The u32 at each record's +0x00 is the set index; +0x04 is
    // zero in every shipped record (6,402/6,402) and is NOT decoded here.
    // The array's end is pinned independently: the Mesh sub-block's own
    // check value repeats in the 4 bytes immediately after it, and
    // meshOffset + cLength lands exactly there - 549/549 character meshes,
    // which is what fixes the array's extent rather than assuming it.
    //
    // SCOPE, stated up front because the neighbouring +0x48/+0x50 slot has
    // already burned this project once (see bonePalette()): this structure
    // is CONFIRMED for skinned character meshes only. The same test on 372
    // vehicle `.ccar_pc` Mesh sub-blocks fails outright - 0/372 land on the
    // check-value bookend - so vehicles do not carry it in this form, and
    // this reader does not populate it for them.
    //
    // Empty (rather than throwing) when: no bone palette was read, the
    // draw groups could not be located, the array does not fit the
    // content, or ANY entry is not a valid set index. A consumer must
    // therefore compare drawRangePaletteSets().size() against its own flat
    // range count rather than treating "empty" as "all zero" - same
    // non-fatal contract as bonePalette(), for the same reason (six carrier
    // formats share this parser).
    const std::vector<uint32_t>& drawRangePaletteSets() const { return drawRangePaletteSets_; }

    // Decodes channel `index`'s elements per spec §6. Throws FormatError
    // for an out-of-range index or a layout code whose field positions the
    // spec did not probe (rather than decoding at guessed offsets).
    std::vector<Vertex> decodeChannel(size_t index) const;

    // True if every channel's declared stride matched the spec's stride
    // law. False is a finding, not necessarily an error - see the header.
    bool allStridesMatchLaw() const;

private:
    uint32_t checkValue_ = 0;
    uint32_t cLength_ = 0;
    uint32_t gLength_ = 0;
    uint8_t flags_ = 0;
    uint32_t indexCount_ = 0;
    uint8_t indexElementSize_ = 0;
    std::vector<uint16_t> indices_;
    std::vector<Channel> channels_;
    std::vector<uint8_t> bonePalette_;
    uint16_t bonePaletteDeclaredCount_ = 0;
    std::vector<BonePaletteSet> bonePaletteSets_;
    uint16_t bonePaletteSetCountDeclared_ = 0;
    std::vector<uint32_t> drawRangePaletteSets_;
    std::vector<std::vector<DrawRange>> drawGroups_;
    std::vector<uint8_t> segment_; // the g-segment bytes this block owns
};

} // namespace sr3mesh
