// sr3zone/zone_heightmap.h - reader + sampler for the `.czn_pc` Heightmap
// (0x2244) record: spec-zone-data-format.md Sec13.5 ("0x2244 Heightmap,
// deep-dive: real per-tile elevation data, a dedicated 1,008-slot registry,
// and a previously undocumented runtime grid sampler", 2026-10-08).
// Dispatched top-priority in direct response to the live-playtest "ground
// missing" bug (Brad and the parked car standing on a flat Y=0 plane with a
// hole showing sky-blue through the ground at their feet) - this is the
// real on-disk elevation-data half of the fix; tools/sr3_viewer.cpp's
// playtest command (runPlaytest()) is the one consumer, feeding sampled
// heights into sr3player/ground_follow.h's CHOSEN simplified ground-
// follower instead of its old flat groundY=0.0 assumption.
//
// THIS IS A SEPARATE CONTENT KIND FROM 0x2237 (road-surface render
// geometry, zone_geometry.h's own Mesh sub-block mechanism) AND FROM
// 0x2236 (Traffic, traffic_graph.h) - Sec13.5 is explicit this handler
// "stores the record's absolute body pointer unconditionally... no inline
// decode of any kind" at load time, with its own dedicated 1,008-slot hash
// registry, not a reuse of either. Do not conflate this reader with
// ZoneGeometry::locate() or TrafficGraph::locate().
//
// SCOPE NOTE (explicitly honoured here, not re-argued): Sec13.5's own
// closing paragraph states the open question is "whether this content
// kind's height samples are ever converted into visible, rendered ground
// geometry at all... as opposed to being purely a scalar elevation-query
// API for physics/AI/camera code" - this reader/sampler is used ONLY as
// that scalar elevation-query source (feeding a character/vehicle's own Y
// position), never to build or render a heightmap-derived mesh. Whether
// 0x2244 data is EVER the thing that closes the "sky-blue gap under Brad's
// feet" visual void bug is a SEPARATE, still-OPEN question (whether any
// visible ground geometry renders at the sampled height at all) - this
// file only fixes where the character/vehicle's OWN Y coordinate sits,
// which is a related but not identical fact. See runPlaytest()'s own doc
// comment and this task's final report for the precise, honest before/
// after distinction.
//
// WHAT IS CONFIRMED (Sec13.5, disassembly of the handler 0x00864200, the
// registry 0x028a0248, and the runtime query chain 0x00861aa0/0x00b41260/
// 0x00b412c0/0x00b41370, population-validated 1,194/1,194 real records):
//
//  * On-disk record format, past the already-documented 12-byte chain
//    header (tag, length, extra - spec-zone-data-format.md Sec1's own
//    Finding-0 correction): a 16-byte mini-header, then a flat row-major
//    `u16[gridW*gridH]` height-sample array (little-endian), length always
//    exactly `16 + gridW*gridH*2`, zero slack, over all 1,194 records:
//      +0x00  i32  origin X, world-space metres
//      +0x04  i32  origin Z, world-space metres
//      +0x08  u16  grid width
//      +0x0a  u16  unused by every reader traced (0 in all 1,194 records)
//      +0x0c  u16  grid height
//      +0x0e  u16  unused by every reader traced (0 in all 1,194 records)
//      +0x10.. u16[width*height]  height samples, row-major, width
//              fast-varying
//  * Origin: CONFIRMED (1,194/1,194, exact zero-error match) to be the
//    tile's own low-X/low-Z corner: `origin_x = centre_x - 160`,
//    `origin_z = centre_z - 140` against this project's own
//    cityTileCentre() formula (tools/sr3_viewer.cpp) - re-verified on the
//    SAME 9 tiles the sibling ground-mesh-origin investigation used,
//    INCLUDING all 4 reported-broken tiles (0915/0914/0715/0716): zero
//    difference between the "broken" and "good" groups. This is the one
//    fact this reader/sampler relies on most directly - it is why tile
//    0915's own Heightmap record can be trusted as an elevation source even
//    though its SEPARATE ground-MESH record is independently flagged
//    anomalous (spec-terrain-format.md Sec5 item 2) - the two findings are
//    independently confirmed, unrelated facts about two different content
//    kinds, not the same bug seen twice.
//  * Fine vs. coarse grid: CONFIRMED discriminator is the `~al` suffix
//    (container kind 0x1B, "Level Always Loaded"), NOT mission-variant
//    (`^`) naming. Ordinary tiles: 32x28 grid @ 10m spacing (320x280m tile,
//    exact). `~al` companions: 8x7 grid @ 40m spacing (a clean 4x
//    downsample per axis). `spacing` itself is NOT stored on disk -
//    assigned at registration time from which container kind the record
//    came from (HeightmapContainerKind below; the caller must say which,
//    exactly as spec states "not stored on disk, inferred from which
//    container kind the record came from").
//  * The runtime grid-cell resolver (0x00b412c0), CONFIRMED by full
//    disassembly - see ResolveHeightmapCell() below for the exact formula
//    reproduced, including its own literal `(int)` cast (truncation, not
//    floor) and its "boundary-inclusive" bounds check.
//
// WHAT IS HIGH CONFIDENCE, NOT INDEPENDENTLY CONFIRMED (Sec13.5's own
// wording, carried forward unchanged): the raw stored value IS elevation
// in whole metres, no further scale/bias - "no independent ground-truth
// elevation source... was cross-checked against it this pass. OPEN."
//
// WHAT IS THE ONE GENUINE GAP THIS FILE FILLS IN ITSELF (Sec13.5's own
// "genuinely missing link, stated honestly"): neither 0x00b412c0 nor its
// sibling 0x00b41370 ever dereferences the payload pointer to read an
// actual height sample - both resolve a (col,row) index and return a
// bool/neighbour-index only; "the literal read... and whatever final
// scale/bias is applied, lives in an unlocated caller... a well-anchored
// next step. OPEN." SampleHeightmapHeight() below performs that literal
// read itself - `heights[row*gridW+col]`, returned as a plain metres value
// with no further transform - as this reader's own CHOSEN, clearly-labelled
// completion of that one open step (see its own comment for the boundary-
// clamp decision this entails).
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO: build or expose anything
// mesh/vertex-shaped from this data (see the SCOPE NOTE above); resolve
// the sibling neighbour-cell function 0x00b41370's own role (Sec13.5:
// "HIGH CONFIDENCE support for bilinear interpolation or a normal-vector
// computation, not independently established") - SampleHeightmapHeight()
// below is a direct single-cell lookup only, no interpolation, per this
// task's own brief ("a direct lookup is fine - bilinear interpolation...
// is a nice-to-have, not required").

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3zone/errors.h"
#include "vpp/byte_view.h"

namespace sr3zone {

using vpp::ByteView;

// 0x2244 - the Heightmap section's own zone-content-kind id (spec-zone-
// data-format.md Sec13.5). Used the same way kTrafficSectionTag is -
// locate()'s own top-level `0x8000xxxx` tag scan, CHOSEN by analogy with
// the already-confirmed 0x2234 top-level record shape (see traffic_graph.h's
// own identical note), not independently re-confirmed for this exact tag.
constexpr uint32_t kHeightmapSectionTag = 0x2244u;

constexpr size_t kHeightmapMiniHeaderSize = 16;  // CONFIRMED, Sec13.5's on-disk table above.
constexpr size_t kHeightmapSampleStride = 2;     // one u16 per sample, CONFIRMED.

// CONFIRMED (Sec13.5): spacing is NOT stored on disk - it is assigned at
// registration time from which container kind the record's own zone came
// from. The caller must say which; see HeightmapContainerKind/
// HeightmapGridSpacing() below.
constexpr double kHeightmapSpacingOrdinary = 10.0;      // ordinary tile, 32x28 grid
constexpr double kHeightmapSpacingAlwaysLoaded = 40.0;  // `~al` companion, 8x7 grid

enum class HeightmapContainerKind {
    Ordinary,      // an ordinary per-tile zone (container kind != 0x1B) - spacing 10m
    AlwaysLoaded,  // an `~al` "Level Always Loaded" companion tile - spacing 40m
};

inline double HeightmapGridSpacing(HeightmapContainerKind kind) {
    return kind == HeightmapContainerKind::AlwaysLoaded ? kHeightmapSpacingAlwaysLoaded : kHeightmapSpacingOrdinary;
}

// A fully parsed Heightmap (0x2244) record. `containerKind` (and therefore
// `spacing()`) is CALLER-SUPPLIED at parse time, not read from the bytes -
// see the file comment above for why (it is not stored on disk at all).
struct HeightmapRecord {
    size_t payloadOffset = 0;
    int32_t originX = 0;  // +0x00, CONFIRMED i32 world-space metres (tile low-X corner)
    int32_t originZ = 0;  // +0x04, CONFIRMED i32 world-space metres (tile low-Z corner)
    uint16_t gridW = 0;   // +0x08, CONFIRMED
    uint16_t gridH = 0;   // +0x0c, CONFIRMED
    // +0x0a/+0x0e: CONFIRMED always 0 over 1,194/1,194 real records, but
    // (matching this project's own zone_header.h convention for a field
    // that is CONFIRMED-present-but-OPEN-meaning) not enforced here as a
    // hard parse rule - a future sample reading nonzero would be a finding,
    // not necessarily a parse failure.
    uint16_t unusedAt0x0a = 0;
    uint16_t unusedAt0x0e = 0;
    HeightmapContainerKind containerKind = HeightmapContainerKind::Ordinary;
    // Row-major, width fast-varying, length == gridW*gridH. Stored as
    // int16_t (not uint16_t) - Sec13.5's own measured global range,
    // "-5..+1,498", requires a signed reading; readU16LE()'s bit pattern is
    // reinterpreted via static_cast, the standard little-endian two's-
    // complement reading (-5 on disk as 0xFFFB round-trips correctly).
    std::vector<int16_t> heights;

    double spacing() const { return HeightmapGridSpacing(containerKind); }
};

class Heightmap {
public:
    // Scans `cznContent` for every top-level Heightmap-record chain entry -
    // a 4-aligned `u32 tag == (0x80000000 | kHeightmapSectionTag)` - BY
    // ANALOGY with the already-confirmed 0x2234 top-level record shape
    // (same CHOSEN-not-independently-confirmed-for-this-tag caveat
    // traffic_graph.h's own locate() states). Parses the payload (12 bytes
    // past the tag) via parseAt() for each candidate, tagging every result
    // with `containerKind`; keeps only candidates that parse successfully.
    // Never throws; returns an empty vector if none are found/valid.
    static std::vector<HeightmapRecord> locate(ByteView cznContent, HeightmapContainerKind containerKind);

    // Parses exactly one Heightmap payload at a KNOWN offset (the 16-byte
    // mini-header's own start, i.e. already past the 12-byte chain header).
    // Throws FormatError if the buffer is too small for the mini-header, or
    // if the declared `gridW*gridH` height-sample array does not fit in the
    // remaining buffer - both genuine structural violations, matching this
    // project's own FormatError convention (never thrown for an OPEN/
    // HIGH-CONFIDENCE field, only a hard structural one).
    static HeightmapRecord parseAt(ByteView cznContent, size_t payloadOffset, HeightmapContainerKind containerKind);
};

// The real grid-cell resolver `0x00b412c0`, CONFIRMED by full disassembly
// (Sec13.5): converts a raw world (x,z) into a bounds-checked grid
// (col,row). Reproduces the confirmed formula EXACTLY, including:
//   - the literal `(int)` cast of a double division (C++'s own truncation-
//     toward-zero, NOT floor(), for a negative dividend - this is what the
//     spec's own pseudocode block literally shows, not a paraphrase);
//   - the "boundary-inclusive" bounds check (col==gridW / row==gridH is
//     ACCEPTED, only col>gridW / row>gridH fails).
// CHOSEN safety addition, not itself stated either way by spec: a negative
// col or row is also rejected here. Sec13.5's own pseudocode only states
// the UPPER bound test ("fail if col > gridW or row > gridH") and does not
// specify signed/unsigned comparison semantics for a negative result; since
// this reader's own array index type is unsigned, silently accepting a
// negative value here would risk an unsigned-underflow read rather than
// reproduce any CONFIRMED engine behaviour either way - a safety guard, not
// a contradiction of the documented, population-tested in-bounds case.
// Never throws; `valid` is false for any out-of-range input.
struct HeightmapCell {
    bool valid = false;
    int32_t col = 0;
    int32_t row = 0;
};
HeightmapCell ResolveHeightmapCell(const HeightmapRecord& rec, double worldX, double worldZ);

// CHOSEN completion of Sec13.5's own "genuinely missing link" (see this
// file's top comment): the literal `heights[row*gridW+col]` read, returned
// as a plain metres value with NO further scale/bias (Sec13.5: "HIGH
// CONFIDENCE, not independently confirmed: raw value = elevation in whole
// metres, no transform"). Resolves the cell via ResolveHeightmapCell()
// first; returns false (and leaves `outHeight` untouched) if that resolves
// outside the grid, or if the record has a zero-sized grid.
//
// CHOSEN boundary handling: ResolveHeightmapCell() above (CONFIRMED)
// accepts col==gridW / row==gridH as a valid in-range result, but that
// index sits one past this array's own last zero-based element - and
// Sec13.5 is explicit NEITHER confirmed function ever actually dereferences
// the payload for this boundary case either (that is exactly the "missing
// link" this function fills in). So there is no confirmed read behaviour to
// reproduce for the boundary row/column specifically; this function clamps
// to the last real index (gridW-1/gridH-1) rather than reading one past the
// array - a bounds-safe CHOSEN interpretation, not a guess at a specific
// unconfirmed engine behaviour.
bool SampleHeightmapHeight(const HeightmapRecord& rec, double worldX, double worldZ, float& outHeight);

}  // namespace sr3zone
