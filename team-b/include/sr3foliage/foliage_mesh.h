// Structural reader for .cfmesh_pc foliage meshes (spec-foliage-format.md).
//
// SCOPE: walks the whole container - shared material-reference block,
// outer `0x0FF1C1A1` block, the embedded "Mesh" sub-block's location and
// version, the material-handle/runtime-slot tables, the inline material
// definitions (shader hashes, texture bindings, shader constants), the
// texture-name table, and the trailing LOD/fade table - and exposes all of
// it structurally. The LOD/fade table was a HYPOTHESIS in the pre-2026-09-20
// spec; spec Sec11.2 now CONFIRMS its layout and meaning (count at outer
// +0x30, table at outer base + int32(outer+0x38), six 4-byte fields per
// 0x18-byte record) and this reader decodes it with the spec's field names.
//
// TWO THINGS IT DELIBERATELY DOES NOT DO:
//
//  * It does not parse the embedded Mesh sub-block's INTERNALS. It locates
//    the block and checks its version reads 9 (the confirmed anchor, 19/19
//    per spec Sec1), and stops there - exactly as sr3geometry does for the
//    same shared structure, and for the same reason: the channel/vertex
//    layout inside it is still OPEN (spec Sec10 item 5, the same open item
//    as spec-geometry-format.md's). Foliage is notable as the first real
//    observation of that block's INLINE mode (flags bit 0 clear in 19/19,
//    no g-file), so these 19 files are the samples to use when that layout
//    is eventually attacked - but attacking it is not this reader's job.
//  * It does not resolve shader hashes, sampler slots or texture handles.
//    Hashes are surfaced raw. Note spec Sec6.1's negative result: the
//    binding's `slot_hash` is NOT the engine hash of the texture filename
//    (0/79 against the name, its stem, and its lowercase form) - it names
//    the sampler ROLE, with only 3 distinct values across 79 bindings that
//    reference 16 distinct names. Do not "helpfully" try to match it to a
//    filename.
//
// It reuses sr3geometry::MaterialBlock for the block at +0x00 rather than
// reimplementing it: that block is shared engine infrastructure (spec
// Sec3, and confirmed across .matlib_pc/.ccmesh_pc/.cefct_pc/.clmesh_pc/
// .czh_pc), and it lives in sr3geometry only because that is where this
// project first needed it.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3foliage/errors.h"
#include "sr3geometry/material_block.h"
#include "vpp/byte_view.h"

namespace sr3foliage {

using vpp::ByteView;

constexpr uint32_t kOuterMagic = 0x0FF1C1A1u; // +0x00 of the outer block, CONFIRMED 19/19
constexpr uint32_t kOuterVersion = 5;          // +0x04, CONFIRMED - the loader requires it
constexpr uint32_t kMeshSubBlockVersion = 9;   // the shared "Mesh" block's version, CONFIRMED 19/19
constexpr uint32_t kNullOffset = 0xFFFFFFFFu;  // -1 means "absent" in every offset field here

constexpr size_t kMaterialHeaderSize = 0x30;
constexpr size_t kTextureBindingSize = 12;
constexpr size_t kLodRecordFloats = 6;                    // six 4-byte words per record ...
constexpr size_t kLodRecordSize = kLodRecordFloats * 4;   // ... = 0x18 bytes (spec Sec11.2)

// Material-handle table (spec Sec3): a 0x10-byte header {u32 ptr-slot, 0,
// count, ?} followed by `count` u32s, ending exactly where the runtime slot
// array (outer +0x18 target) begins.
constexpr size_t kMaterialHandleHeaderSize = 0x10;

// One 12-byte texture binding (spec Sec6.1).
struct TextureBinding {
    uint32_t nameOffset = 0; // index into the texture-name table at outer +0x28
    // Names the SAMPLER SLOT (diffuse/specular/normal), NOT the texture
    // file - see the header comment. Surfaced raw.
    uint32_t slotHash = 0;
    uint16_t resolvedIndex = 0; // runtime-resolved texture index
    uint16_t flags = 0;         // bit 0x08 selects the `misc-static.tga` fallback path
    std::string name;           // resolved from the name table for convenience
};

// One inline material definition (spec Sec6). Length-prefixed on disk.
struct Material {
    size_t offset = 0;        // absolute offset of this record's u32 size prefix
    uint32_t declaredSize = 0; // the u32 size prefix; the loader checks the record consumes exactly this

    uint32_t shaderHash = 0;  // +0x00, resolved at load to a shader object (2 distinct values shipped)
    uint32_t variantHash = 0; // +0x04, OPEN - 13 distinct values, meaning unknown
    uint32_t flags = 0;       // +0x08, only bit 0x08 has a confirmed meaning (fallback texture path)

    std::vector<TextureBinding> textureBindings;          // A x 12 bytes, 4-aligned
    std::vector<uint32_t> constantNameHashes;             // B x 4 bytes, 4-aligned
    std::vector<std::array<float, 4>> constantValues;     // C x 16 bytes, 16-aligned
};

// One record of the trailing LOD/fade table (spec Sec7, Sec11.2; 0x18 bytes).
// Field names are the spec's own. Label status per Sec11.2: the layout and
// the meaning of every field are CONFIRMED (disassembly of the per-frame
// instancer and the draw/LOD-range/fade helpers, plus the empirical checks
// this project reproduces on all 19 real files). The one HIGH CONFIDENCE
// (not read) item is that the fadeIn ramp is used by the shader as a linear
// 0 -> 1 blend `d*scale + bias`; this reader derives no ramp constants and
// makes no claim about that.
//
// The distances are world units (spec: "likely metres"). The two
// fadeOut values are, per the spec, `min`-ed at draw time with the layer's
// own values, or replaced by them when a global flag is set - that is a
// runtime override; the FILE values are what is surfaced here.
struct LodRecord {
    float fadeInStart = 0.0f;       // +0x00 distance where this LOD starts fading IN
    float fadeInEnd = 0.0f;         // +0x04 distance where it is fully in
    float fadeOutStart = 0.0f;      // +0x08 distance where it starts fading OUT
    float fadeOutEnd = 0.0f;        // +0x0C distance where it is gone; also its far distance
    uint32_t drawGroupIndex = 0;    // +0x10 which 0x30-byte draw-group record of the Mesh header this LOD draws (0/1/2 seen)
    uint32_t billboardFlag = 0;     // +0x14 non-zero -> drawn camera-facing; zero -> the item's own transform (0/1 seen)

    bool billboard() const { return billboardFlag != 0; }

    // LEGACY raw view kept only so callers written against the pre-2026-09-20
    // reader (tools/foliage_dump.cpp prints `values`) still compile. It is
    // the six 4-byte words of the record reinterpreted as f32. Words [0..3]
    // are the four fade distances and are meaningful as floats; words [4]
    // and [5] are u32s (drawGroupIndex, billboardFlag), so their float view
    // is a bit-pattern artefact - use the named fields instead.
    std::array<float, kLodRecordFloats> values{};
};

// The material-handle table (spec Sec3, Sec11.3). The engine WRITES these
// u32s at load (each becomes a handle from the deduplicating material
// cache), so the values on disk are placeholders, not keys: spec Sec11.3
// reports identity 0..n-1 in 19/19 files. They are surfaced so that fact is
// checkable (isIdentity()); nothing in this reader looks anything up by them.
struct MaterialHandleTable {
    size_t offset = 0;          // absolute offset of the 0x10-byte header
    uint32_t headerWord0 = 0;   // "ptr-slot" (runtime pointer slot), raw
    uint32_t headerWord1 = 0;   // spec: 0
    uint32_t headerCount = 0;   // spec: equals the outer +0x20 material count (19/19)
    uint32_t headerWord3 = 0;   // spec: "?" - unknown, raw
    std::vector<uint32_t> handles; // `headerCount` u32s

    // True when handles[i] == i for every i (the on-disk placeholder pattern).
    bool isIdentity() const {
        for (size_t i = 0; i < handles.size(); ++i) {
            if (handles[i] != static_cast<uint32_t>(i)) return false;
        }
        return true;
    }
};

class FoliageMesh {
public:
    // Parses a whole .cfmesh_pc. Throws FormatError if the material block
    // fails to validate (which aborts the real load for this format), if
    // the outer magic/version are wrong, if any offset runs past the end,
    // if the material sub-record chain does not end EXACTLY at the
    // outer block's own +0x28 target - spec Sec3's whole-population check
    // (19/19), re-run per file so a successful parse is that verification -
    // or if, with a LOD/fade table present, the bytes from the table start
    // (outer base + int32(outer+0x38)) to end-of-file are not EXACTLY
    // (outer +0x30) x 24 (spec Sec11.2, 19/19; no slack, no trailing bytes).
    static FoliageMesh parse(ByteView content);

    const sr3geometry::MaterialBlock& materialBlock() const { return materialBlock_; }

    size_t outerBlockOffset() const { return outerBlockOffset_; }
    uint32_t version() const { return version_; }
    uint32_t materialCount() const { return materialCount_; }

    // Absolute offset of the embedded "Mesh" sub-block, whose version was
    // checked to read kMeshSubBlockVersion. Its internals are deliberately
    // not parsed - see the header comment.
    bool hasMeshSubBlock() const { return hasMeshSubBlock_; }
    size_t meshSubBlockOffset() const { return meshSubBlockOffset_; }

    const std::vector<Material>& materials() const { return materials_; }

    // Every null-terminated name in the texture-name table (outer +0x28),
    // in table order. Bindings index into this table by byte offset, which
    // is what TextureBinding::name resolves against.
    const std::vector<std::string>& textureNames() const { return textureNames_; }

    // ---- Trailing LOD/fade table (spec Sec7, Sec11.2) ----

    // Outer +0x30: the RECORD COUNT of the LOD/fade table (1-3 in the real
    // population). Raw field as stored; equals lodRecords().size() whenever
    // a table is present (parse() throws otherwise).
    uint32_t lodRecordCount() const { return lodRecordCount_; }

    // Outer +0x38 is a signed offset RELATIVE TO THE OUTER-BLOCK BASE: the
    // table starts at outerBlockOffset() + int32(outer+0x38), NOT at
    // outer+0x38 itself (spec Sec11.2, wording clarified 2026-09-20). -1
    // means "no table". lodTableOffsetRaw() is the stored value as an i32.
    bool hasLodTable() const { return hasLodTable_; }
    int32_t lodTableOffsetRaw() const { return lodTableOffsetRaw_; }
    size_t lodTableOffset() const { return lodTableOffset_; }    // absolute; valid iff hasLodTable()
    // Bytes from the table start to end-of-file. parse() guarantees this is
    // EXACTLY lodRecordCount() x kLodRecordSize when a table is present -
    // the spec's 19/19 invariant, enforced per file.
    size_t lodTableTailBytes() const { return lodTableTailBytes_; }

    // The decoded records, in file order (successive LODs, nearest first).
    const std::vector<LodRecord>& lodRecords() const { return lodRecords_; }

    // Outer +0x34: a further u32 (values 40..292 in the population) that no
    // consumer was found to read. OPEN (spec Sec4, Sec11.2, Sec11.4). Raw.
    uint32_t outerField34() const { return outerField34_; }

    // ---- Material-handle table (spec Sec3, Sec11.3) ----

    // Present when the 0x10-byte header + materialCount() u32s fit between
    // the outer header and the runtime slot array they must end exactly at.
    // The u32s are load-time placeholders (identity 0..n-1, 19/19), not keys.
    bool hasMaterialHandleTable() const { return hasMaterialHandleTable_; }
    const MaterialHandleTable& materialHandleTable() const { return materialHandleTable_; }
    // Convenience: table present and every handle[i] == i.
    bool materialHandlesAreIdentity() const {
        return hasMaterialHandleTable_ && materialHandleTable_.isIdentity();
    }

private:
    sr3geometry::MaterialBlock materialBlock_;
    size_t outerBlockOffset_ = 0;
    uint32_t version_ = 0;
    uint32_t materialCount_ = 0;
    bool hasMeshSubBlock_ = false;
    size_t meshSubBlockOffset_ = 0;
    std::vector<Material> materials_;
    std::vector<std::string> textureNames_;
    std::vector<LodRecord> lodRecords_;
    uint32_t lodRecordCount_ = 0;
    uint32_t outerField34_ = 0;
    bool hasLodTable_ = false;
    int32_t lodTableOffsetRaw_ = -1;
    size_t lodTableOffset_ = 0;
    size_t lodTableTailBytes_ = 0;
    bool hasMaterialHandleTable_ = false;
    MaterialHandleTable materialHandleTable_;
};

} // namespace sr3foliage
