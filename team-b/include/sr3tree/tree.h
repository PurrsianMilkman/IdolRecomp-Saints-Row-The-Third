// Reader for .csrt_pc / .gsrt_pc tree geometry (spec-tree-format.md, as
// re-synced 2026-09-20: 45,427 bytes, mtime 2026-09-20 14:00:50).
//
// SCOPE: an assembly of blocks this project already knows how to parse,
// plus one new fixed header. Walks: the shared material-reference block
// (reused from sr3geometry) -> mandatory pad -> the fixed 0x208-byte
// 'TREE' block -> the fixed 0x170-byte geometry sub-structure -> the
// material-set block (shared engine block, same inline-material-definition
// routine as spec-foliage-format.md Sec6) -> the generic index list ->
// up to 4 LOD presence entries, each gating a shared "Mesh" sub-block
// (spec-vertex-format.md, g-backed - data lives in .gsrt_pc) -> three
// runtime arrays (mostly opaque pointer slots) -> collision capsules.
//
// DECODED SINCE THE FIRST VERSION OF THIS READER (spec Sec13, 2026-09-20):
//  * The 'TREE' block +0x38..+0x207 region is ONE embedded wind-state
//    object (Sec13.2). Float index k lives at block offset 0x38 + 4k. The
//    38 floats k = 0x00..0x25 (block +0x38..+0xCF) are file-loaded and are
//    exposed as WindParameters (Sec13.2's role table); everything at
//    k >= 0x26 (block +0xD0..+0x207) is runtime state the wind routines
//    write and is ZERO ON DISK in 11/11 trees. It is checked, never
//    interpreted (TreeBlock::windRuntimeRegionAllZero()). This replaces the
//    first version's 115-float table that started at +0x3C and so missed
//    k = 0 at +0x38 (5.0 in 11/11).
//  * The geometry sub-structure's +0xE0..+0xEC are four LOD distances
//    A < B < C < D in metres (Sec13.5.2; D is also the cull distance) and
//    +0xF0/+0xF4 are derived at load and zero on disk. lodParameter*() is
//    the LOD parameter t of Sec13.5.2's table.
//
// STILL OPEN (spec Sec12 / Sec13.9) - surfaced raw or not at all:
//  * The GPU meaning of the seven wind output constants (Sec13.3); the
//    upload was not found. This reader does NOT build them, does NOT run
//    the per-frame wind simulation and does NOT upload anything.
//  * Geometry sub-structure +0x58/+0x60/+0x64/+0x68 (no consumer found,
//    Sec13.5.3), the 16-byte records (0 or 6 per tree) and the
//    +0x80..+0xDC float block that co-occurs with them (HYPOTHESIS only).
//  * Collision capsule header dwords (+0x00-+0x0F) and the tall pine's
//    trailing floats.
//  * The embedded Mesh sub-block's own vertex/channel CONTENTS - located
//    and version-checked via sr3mesh::MeshBlock, same as sr3geometry and
//    sr3foliage do for the identical shared structure.
// Guessing any of these would be inventing data spec-tree-format.md itself
// marks OPEN or HYPOTHESIS.
//
// A REAL, PRE-EMPTIVELY-APPLIED LESSON FROM THIS SESSION'S OWN CLMESH WORK
// (HANDOFF.md Sec9.66): a chain of back-to-back Mesh sub-blocks (spec Sec3:
// "each advanced by its own length field") gives NO guarantee that any
// block after the first lands on an 8-aligned cursor. sr3mesh::MeshBlock's
// internal header (flags, channelCount, ...) starts `roundUp8(cursor+16)`
// bytes in, not an unconditional `+0x10` - this reader computes that
// per-block, exactly like src/zone_geometry.cpp's own
// headerDisplacementFor() already does for .gzn_pc's identical shape,
// rather than repeating the bug .clmesh_pc's reference chain had until
// today.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3tree/errors.h"
#include "vpp/byte_view.h"

namespace sr3tree {

using vpp::ByteView;

// ---- constants, all CONFIRMED per spec-tree-format.md unless noted ----

constexpr uint32_t kTreeMagic = 0x54524545u; // 'TREE' packed (bytes E E R T), spec Sec4
constexpr uint32_t kTreeMinVersionExclusive = 0xCB; // loader requires version > this
constexpr size_t kTreeBlockSize = 0x208;
constexpr size_t kTreeCapsuleCountOffset = 0x18;

// The embedded wind-state object (spec Sec4 / Sec13.2). Float index k lives
// at block offset kTreeWindObjectOffset + 4k.
constexpr size_t kTreeWindObjectOffset = 0x38;
// k = 0x00..0x25: the file-loaded floats, block +0x38..+0xCF.
constexpr size_t kWindFileLoadedFloatCount = 0x26;
// k >= 0x26: runtime state, block +0xD0..+0x207, zero on disk 11/11.
constexpr size_t kWindRuntimeRegionOffset = kTreeWindObjectOffset + 4 * kWindFileLoadedFloatCount; // 0xD0
constexpr size_t kWindRuntimeRegionSize = kTreeBlockSize - kWindRuntimeRegionOffset;                // 0x138
constexpr size_t kWindBandCount = 4;

// Shipped global wind level (spec Sec13.1: static 0.1, every reference a
// read; HIGH CONFIDENCE that the shipped level is this constant - a
// pointer-mediated writer is not excluded).
constexpr float kShippedWindLevel = 0.1f;

constexpr size_t kGeometrySize = 0x170;
constexpr size_t kGeoBoundsMinOffset = 0x00;
constexpr size_t kGeoBoundsMaxOffset = 0x10;
constexpr size_t kGeoLodSlotCountOffset = 0x30;
constexpr size_t kGeoMaterialCountOffset = 0x40;
constexpr size_t kGeoSixteenByteCountOffset = 0x5C;
constexpr size_t kGeoOpenScaleOffset = 0x60;
constexpr size_t kGeoOpenHeightOffset = 0x64;
constexpr size_t kGeoOpenMinYOffset = 0x68;
// Spec Sec13.5 / Sec13.5.2 (CONFIRMED): four LOD distances A<B<C<D (metres,
// LOD-scale 1.0) at +0xE0..+0xEC; +0xF0/+0xF4 derived at load, zero on disk.
constexpr size_t kGeoLodDistancesOffset = 0xE0;
constexpr size_t kGeoLodDerivedOffset = 0xF0;

constexpr size_t kLodSlotCount = 4;
constexpr size_t kPresenceEntrySize = 8;
constexpr uint32_t kMeshSubBlockVersion = 9;

// The material-set block, FUN_00e40210 - same shared block sr3clmesh's
// MIDDLE parses (spec-physics-format.md Sec4.4.6(b)/(h)), independently
// re-implemented here per this project's convention of not cross-linking
// format readers for a block each already knows how to parse on its own.
constexpr size_t kMatSetHeaderSize = 0x20;
constexpr size_t kMatSetCountOffset = 0x00;
constexpr size_t kMatSetNameTableLengthOffset = 0x10;
constexpr size_t kMatSetSlotStride = 8;

// One inline material definition, FUN_00e71090 - same routine as
// spec-foliage-format.md Sec6.
constexpr size_t kMatRecordHeaderSize = 0x30;
constexpr size_t kMatRecordHashOffset = 0x00;
constexpr size_t kMatRecordVariantOffset = 0x04;
constexpr size_t kMatRecordFlagsOffset = 0x08;
constexpr size_t kMatRecordTexCountOffset = 0x0C;
constexpr size_t kMatRecordConstCountOffset = 0x0E;
constexpr size_t kMatRecordVec4CountOffset = 0x0F;
constexpr size_t kTextureBindingSize = 12;

// The generic index list, FUN_00e401d0 (spec Sec7): align 8, a 0x10-byte
// header whose +0x08 is the element count, then count x u32. In trees these
// u32s are PLACEHOLDERS (identity 0..n-1 on disk, 11/11) that the engine
// overwrites at load with material-cache handles - NOT a material-index
// remap (spec Sec7 correction 2026-09-20, Sec13.5.1).
constexpr size_t kIndexListHeaderSize = 0x10;
constexpr size_t kIndexListCountOffset = 0x08;
constexpr size_t kIndexListElementStride = 4;

// Collision capsules, spec Sec8.
constexpr size_t kCapsuleSize = 0x60;
constexpr size_t kCapsuleHeaderBytes = 0x10;
constexpr size_t kCapsuleStartOffset = 0x10;
constexpr size_t kCapsuleEndOffset = 0x20;
constexpr size_t kCapsuleRadiusOffset = 0x30;

constexpr size_t roundUp16(size_t v) { return (v + 15) / 16 * 16; }
constexpr size_t roundUp8(size_t v) { return (v + 7) / 8 * 8; }

// The material block's MANDATORY trailing pad (spec-geometry-format.md
// Sec3.1.1): 1-16 bytes, ALWAYS present - a full 16 when the block already
// ends 16-aligned, never a no-op. Same rule spec-foliage-format.md Sec3
// already applies for the identical shared material-reference block.
constexpr size_t padStrictlyTo16(size_t value) { return value / 16 * 16 + 16; }

// One 12-byte texture binding, spec Sec6 (the shared inline-material
// record's own array A).
struct TextureBinding {
    uint32_t nameOffset = 0;
    uint32_t slotHash = 0; // sampler ROLE (diffuse/normal/specular/...), NOT a filename hash - spec Sec6.1
    uint16_t resolvedIndex = 0;
    uint16_t flags = 0;
    std::string name; // resolved against the material-set block's own name table, for convenience
};

// One inline material definition, spec Sec6.
struct Material {
    size_t offset = 0;         // absolute offset of this record's own u32 size prefix
    uint32_t declaredSize = 0; // the loader checks the record consumes exactly this many bytes

    uint32_t shaderHash = 0;   // +0x00 - one distinct value across all 37 shipped ((0x967A81C0))
    uint32_t variantHash = 0;  // +0x04 - OPEN, meaning unknown
    uint32_t flags = 0;        // +0x08 - CONFIRMED present (0x40 x37), meaning beyond that OPEN

    std::vector<TextureBinding> textureBindings; // A x 12 bytes, 4-aligned
    std::vector<uint32_t> constantNameHashes;    // B x 4 bytes, 4-aligned
    std::vector<std::array<float, 4>> constantValues; // C x 16 bytes, 16-aligned
};

// ---- The 'TREE' block's wind-state object, spec Sec13.2 ----

// One of the four oscillator bands g = 0..3 (spec Sec13.2, rows 0x05..0x14
// and 0x20..0x23). Per frame the band's response is level^exponent, the
// live amplitude lerp(amplitudeAtLevel0, amplitudeAtLevel1, response) and
// the live frequency lerp(frequencyAtLevel0, frequencyAtLevel1, response).
// [CONFIRMED - the arithmetic; the amplitude/frequency NAMES are HIGH
// CONFIDENCE - they feed a shader, whose constant layout was not read.]
// Note (spec Sec13.8): the amplitude pair is NOT ordered (decreasing 2->1,
// 4->3 in five trees) - it is a pair of interpolation endpoints, not min/max.
struct WindOscillatorBand {
    float amplitudeAtLevel0 = 0.0f; // k = 5 + 4g
    float amplitudeAtLevel1 = 0.0f; // k = 6 + 4g
    float frequencyAtLevel0 = 0.0f; // k = 7 + 4g, per second of wind clock
    float frequencyAtLevel1 = 0.0f; // k = 8 + 4g
    float exponent = 0.0f;          // k = 0x20 + g, CONFIRMED power exponent e_g (0.75 .. 3 shipped, > 0 44/44)
};

// The 38 file-loaded floats k = 0x00..0x25 under the spec's role names.
// Labels are the spec's own: a field is documented CONFIRMED or with the
// spec's OPEN caveat; nothing here is upgraded beyond what Sec13.2 says.
struct WindParameters {
    // k0, block +0x38. CONFIRMED: strength response time (s); a strength
    // change finishes between 0.5*k0 and k0 from now. Also the base time of
    // every gust phase (Sec13.2.1). 5.0 in 11/11.
    float strengthResponseTime = 0.0f;
    // k1, block +0x3C. CONFIRMED: direction response time (s). 2.5 (5
    // trees) / 1.0 (6).
    float directionResponseTime = 0.0f;
    // k2, block +0x40. CONFIRMED packing (1/k2, or 0 when k2 == 0, into
    // constant c2.z). A length-like quantity; its role on the GPU is OPEN.
    float lengthLikeReciprocalToC2z = 0.0f;
    // k3, block +0x44. CONFIRMED: copied to c2.w (GPU role not documented).
    float copiedToC2w = 0.0f;
    // k4, block +0x48. CONFIRMED: copied to c6.x (GPU role not documented).
    float copiedToC6x = 0.0f;

    // k5..0x14 and k0x20..0x23: the four oscillator bands.
    std::array<WindOscillatorBand, kWindBandCount> bands{};

    // k0x15, block +0x8C. CONFIRMED use: gust onset rate (scaled x0.01). Read
    // ONLY on the master-less path, so INERT for trees in the shipped
    // configuration (Sec13.2.1, HIGH CONFIDENCE).
    float gustOnsetRate = 0.0f;
    // k0x16, block +0x90. CONFIRMED: x band-0 response -> c5.y.
    float scaleOfBand0ResponseToC5y = 0.0f;
    // k0x17, block +0x94. CONFIRMED: copied to c5.z.
    float copiedToC5z = 0.0f;
    // k0x18/k0x19, block +0x98/+0x9C. CONFIRMED use: gust target amplitude
    // range [lo, hi]; master-less path only, so INERT for trees. lo <= hi 11/11.
    float gustTargetAmplitudeLo = 0.0f;
    float gustTargetAmplitudeHi = 0.0f;
    // k0x1A/k0x1B, block +0xA0/+0xA4. CONFIRMED: gust plateau duration range
    // (s) [lo, hi]. lo <= hi 11/11.
    float gustPlateauDurationLo = 0.0f;
    float gustPlateauDurationHi = 0.0f;
    // k0x1C/k0x1D, block +0xA8/+0xAC. CONFIRMED: x current wind level ->
    // c6.y / c6.z (k0x1C is 0 in 11/11).
    float scaleOfLevelToC6y = 0.0f;
    float scaleOfLevelToC6z = 0.0f;
    // k0x1E/k0x1F, block +0xB0/+0xB4. CONFIRMED: copied to c4.y / c4.z.
    float copiedToC4y = 0.0f;
    float copiedToC4z = 0.0f;
    // k0x24/k0x25, block +0xC8/+0xCC. CONFIRMED: copied to c3.y / c3.z.
    float copiedToC3y = 0.0f;
    float copiedToC3z = 0.0f;
};

// Spec Sec13.2's role table applied to the 38 file-loaded floats
// (index = k). Pure; exposed so tests and harnesses can exercise the mapping
// without building a file.
WindParameters windParametersFromFloats(const std::array<float, kWindFileLoadedFloatCount>& k);

// Spec Sec13.4's derived steady-state values of one band: response
// level^e, live amplitude and live frequency. DERIVED, not measured.
struct WindBandSteadyState {
    float response = 0.0f;
    float amplitude = 0.0f;
    float frequency = 0.0f; // per second of wind clock
};
using WindSteadyState = std::array<WindBandSteadyState, kWindBandCount>;

// Sec13.4: osc_g = level^e_g, amplitude = lerp(amp0, amp1, osc_g), frequency
// = lerp(freq0, freq1, osc_g), lerp(a,b,t) = a + (b-a)*t. `level` is the
// wind level min(strength + gust, 1); for the shipped configuration (no
// gust, Sec13.2.1) that is kShippedWindLevel. Only the time-independent
// components (phases grow with the clock and are not modelled). Returns
// nullopt when `level` is outside [0, 1] (or NaN): the spec's level is
// clamped to at most 1 and a negative base has no defined power.
std::optional<WindSteadyState> windSteadyState(const WindParameters& p,
                                               float level = kShippedWindLevel);

// ---- LOD distances, spec Sec13.5.2 ----

// A, B, C, D in metres, at LOD-scale 1.0 as stored on disk (the engine
// multiplies them by a graphics-quality distance multiplier, default 1.0, at
// load - that multiplier is NOT applied here).
using LodDistances = std::array<float, 4>;

// The instance copy routine's own acceptance test (Sec13.5.2): strictly
// A < B < C < D (which implies B-A >= 0 and D-C >= 0). NaN fails.
bool lodDistancesValid(const LodDistances& d);

// The LOD parameter t of Sec13.5.2's table, for SQUARED camera distance q:
//   q <  A^2            : 1
//   A^2 <= q < B^2      : 1 - (q - A^2)/(B^2 - A^2)   (fades 1 -> 0)
//   B^2 <= q < C^2      : 0
//   C^2 <= q < D^2      : -(q - C^2)/(D^2 - C^2)      (0 -> -1)
//   q >= D^2            : -1
// NOTE the fade is linear in the SQUARED distance, not in the distance. The
// spec's four cases are continuous at every boundary. t = -1 means "no
// group" (culled); D is also the tree's cull distance. Returns nullopt when
// the distances are not lodDistancesValid(), or (a guard of THIS reader so
// the squared table is well defined - not a spec rule) not all finite or
// A < 0, or when q is NaN or negative.
std::optional<float> lodParameterForSquaredDistance(const LodDistances& d, float q);

// The same for a camera distance in metres (q = distance^2). Returns
// nullopt for a NaN or negative distance.
std::optional<float> lodParameterForDistance(const LodDistances& d, float distance);

// Spec Sec5 / Sec13.5.2 on the 'TREE' block, kept exact: what the block
// holds at the bytes the spec says it does.
struct TreeBlock {
    size_t offset = 0;
    uint32_t version = 0;
    uint32_t collisionCapsuleCount = 0; // +0x18

    // The 38 file-loaded wind floats: windFloats[k] is at block offset
    // 0x38 + 4k, k = 0x00..0x25 (Sec13.2). Raw values, index = k.
    std::array<float, kWindFileLoadedFloatCount> windFloats{};
    // The same 38 floats under the spec's role names.
    WindParameters wind;

    // Block +0xD0..+0x207 (k >= 0x26): runtime state the wind routines
    // write, ZERO ON DISK in 11/11 (spec Sec13.2, gate W1). Not interpreted
    // and never a parse error - the loader does not require it, it is an
    // observation about shipped data. `windRuntimeNonZeroDwords` counts the
    // non-zero dwords in that 0x138-byte region; 0 means all zero.
    uint32_t windRuntimeNonZeroDwords = 0;
    bool windRuntimeRegionAllZero() const { return windRuntimeNonZeroDwords == 0; }
};

// Spec Sec5: the fixed 0x170-byte geometry sub-structure. The four
// "open*" fields are present and plausible-looking (spec Sec5's own
// table) but no reader of them was found (Sec13.5.3, a bounded negative) -
// surfaced raw under names that say so, not as if their meaning were
// settled.
struct GeometrySubStructure {
    size_t offset = 0;
    std::array<float, 3> boundsMin{}; // +0x00 float4, w duplicates z (SIMD padding); CONFIRMED (Sec13.5)
    std::array<float, 3> boundsMax{}; // +0x10 float4; CONFIRMED
    uint32_t lodSlotCount = 0;        // +0x30 - 4 in 11/11
    uint32_t materialCount = 0;       // +0x40 - equals the material-set block's own count in 11/11
    uint32_t sixteenByteRecordCount = 0; // +0x5C - 0 or 6, OPEN meaning (no reader found, Sec13.5.3)
    float openScaleLike = 0.0f;       // +0x60, OPEN
    float openHeightLike = 0.0f;      // +0x64, OPEN
    float openMinYLike = 0.0f;        // +0x68, OPEN

    // +0xE0..+0xEC: four LOD distances A<B<C<D in metres. CONFIRMED (Sec13.5,
    // Sec13.5.2); strictly ascending 11/11.
    LodDistances lodDistances{};
    // +0xF0 / +0xF4: derived at load as (B-A)*s and (D-C)*s; ZERO ON DISK 11/11
    // (Sec13.5). Surfaced raw; the disk value is not the derived value.
    std::array<float, 2> lodDerivedOnDisk{};

    // D, the largest LOD distance, is also the tree's cull distance
    // (CONFIRMED, Sec13.5.2).
    float cullDistance() const { return lodDistances[3]; }
    bool lodDerivedOnDiskZero() const { return lodDerivedOnDisk[0] == 0.0f && lodDerivedOnDisk[1] == 0.0f; }
};

// One LOD slot: a presence entry, plus its Mesh sub-block when present.
// `mesh` is default-constructed (and must not be read) when !present.
struct LodSlot {
    size_t presenceEntryOffset = 0;
    bool present = false;
    sr3mesh::MeshBlock mesh;
};

// Spec Sec8: one 0x60-byte collision capsule. `headerBytes` (+0x00-+0x0F)
// is OPEN - surfaced as raw bytes, not decoded, since its content varies
// in kind (indices/flags in some files, float-like in the tall pine) with
// no single reading confirmed against consuming code.
struct CollisionCapsule {
    size_t offset = 0;
    std::array<uint8_t, kCapsuleHeaderBytes> headerBytes{}; // +0x00-+0x0F, OPEN
    // CONFIRMED - disassembly (spec Sec8 update 2026-09-12): the physics-shape
    // builder reads exactly +0x10/+0x20/+0x30. A coincident start/end is a
    // degenerate (sphere) capsule.
    std::array<float, 3> start{}; // +0x10
    std::array<float, 3> end{};   // +0x20
    float radius = 0.0f;          // +0x30
};

class Tree {
public:
    // Parses one .csrt_pc against its paired .gsrt_pc content (spec Sec2:
    // 11/11 real trees pair 1:1). Throws FormatError if the material
    // block, 'TREE' magic/version, or any Mesh sub-block fails to
    // validate, or if the whole walk does not land EXACTLY on end-of-file
    // - spec Sec3's own oracle, replay-verified 11/11.
    static Tree parse(ByteView content, ByteView gContent);

    const sr3geometry::MaterialBlock& materialReferenceBlock() const { return materialRefBlock_; }
    const TreeBlock& treeBlock() const { return treeBlock_; }
    const GeometrySubStructure& geometry() const { return geometry_; }

    // Sec13.5.2's LOD parameter t for this tree's own LOD distances.
    // nullopt under the conditions lodParameterForDistance() documents.
    std::optional<float> lodParameterAtDistance(float distance) const {
        return lodParameterForDistance(geometry_.lodDistances, distance);
    }
    // Sec13.4's steady-state wind values for this tree's own parameters.
    std::optional<WindSteadyState> windSteadyStateAtLevel(float level = kShippedWindLevel) const {
        return windSteadyState(treeBlock_.wind, level);
    }

    uint32_t materialCount() const { return materialCount_; }
    const std::vector<Material>& materials() const { return materials_; }
    const std::vector<std::string>& materialSetNames() const { return materialSetNames_; }

    // The index list (Sec7): on-disk placeholders, identity 0..n-1 in 11/11 -
    // NOT a material remap (Sec13.5.1).
    const std::vector<uint32_t>& materialIndexList() const { return materialIndexList_; }

    const std::array<LodSlot, kLodSlotCount>& lodSlots() const { return lodSlots_; }
    size_t presentLodCount() const {
        size_t n = 0;
        for (const LodSlot& s : lodSlots_) {
            if (s.present) ++n;
        }
        return n;
    }

    // Runtime arrays after the LOD slots: materialCount() x 8 bytes (per-
    // material runtime object slots, Sec13.5 +0x48), then 4 x 8 bytes (per-
    // LOD-slot mesh runtime object slots, +0x50), then the geometry
    // sub-structure's own sixteenByteRecordCount x 16 bytes (OPEN, no reader
    // found) - all located (offset/length), none decoded (spec Sec3).
    size_t runtimeArraysOffset() const { return runtimeArraysOffset_; }
    size_t runtimeArraysLength() const { return runtimeArraysLength_; }

    const std::vector<CollisionCapsule>& collisionCapsules() const { return collisionCapsules_; }

    size_t contentSize() const { return contentSize_; }

private:
    sr3geometry::MaterialBlock materialRefBlock_;
    TreeBlock treeBlock_;
    GeometrySubStructure geometry_;

    uint32_t materialCount_ = 0;
    std::vector<Material> materials_;
    std::vector<std::string> materialSetNames_;

    std::vector<uint32_t> materialIndexList_;

    std::array<LodSlot, kLodSlotCount> lodSlots_;

    size_t runtimeArraysOffset_ = 0;
    size_t runtimeArraysLength_ = 0;

    std::vector<CollisionCapsule> collisionCapsules_;

    size_t contentSize_ = 0;
};

} // namespace sr3tree
