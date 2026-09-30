// Which texture a draw range's material id actually selects.
//
// The draw range at `spec-vertex-format.md` §8.2 `+0x00` carries a material
// id, and the obvious readings of how that id picks a texture are all
// REFUTED (HANDOFF §9.18): it does not index the material block's name
// list, there is no fixed count of textures per material, materials do not
// own contiguous slices, and no texture name's hash appears in the file.
//
// Textures are not referenced by index at all. Each material owns a record
// past the end of the Mesh sub-block holding a run of 12-byte entries, and
// each entry names a texture by BYTE OFFSET into a mixed-case name blob:
//
//     +0x00  u32  byte offset into the name blob at subheader + 0x88
//     +0x04  u32  sampler / parameter name hash
//     +0x08  u32  slot index, sequential from 0
//
// Slot 0 is the diffuse map, slot 1 the normal map (parameter hash
// 0x2808EB90), slot 2 a shared surface-type material (Cloth_Matte, Skin_Ca,
// Metal...). That is why one character can have five materials over eight
// texture names: several materials share a diffuse/normal pair and differ
// only in their slot-2 surface.
//
// THE RECORD IS COMPUTED, NOT SEARCHED FOR (HANDOFF §9.69, superseding the
// sliding-window search this reader originally shipped with). The per-
// material record's position follows by direct arithmetic from the two
// fixed-stride descriptor arrays that precede the per-material loop
// (`spec-vertex-format.md` §8.4), and the binding run sits at a FIXED offset
// (+0x30) inside that record, past its own 0x30-byte header
// (`spec-vertex-format.md` §12.8, `spec-vehicle-geometry.md` §11.2). The
// i-th iteration of the per-material loop IS material i's own record - no
// count-matching guess is needed, because each record's position is derived
// from the one before it rather than found by pattern.
//
// This is why vehicles work here where the reader's original guard could
// not accept them at all: vehicles declare roughly TWICE the materials
// their draw ranges reference (`spec-vertex-format.md` §8.4.3), so an
// equality guard on "runs found == declared material count" was
// structurally unsatisfiable for that carrier (HANDOFF §9.34,
// `spec-vehicle-geometry.md` §7.1 item 1). Direct arithmetic needs no such
// count to accept a position, so both carriers now go through one
// mechanism.
//
// A record's own array is still validated per entry, not trusted blindly
// (`spec-vertex-format.md` §8.4.2):
//
//   * the parameter hash must be a real 32-bit constant (>= 256, not -1),
//     AND
//   * the name offset must resolve to a real NUL-terminated name landing on
//     a name BOUNDARY - the blob base, or immediately after a NUL, which
//     rejects a name carved out of the middle of another (e.g. `_df.tga`
//     truncated from `xx_df.tga`, which a naive "printable, has a dot" test
//     would accept).
//
// An entry failing either check is dropped rather than guessed at; a
// material whose array is entirely rejected (or genuinely declares zero
// entries) ends up with no textures, which diffuse()/normalMap() already
// treat as "unavailable" rather than a reason to fall back to a guess.
//
// Verified exact, whole population, both carriers, zero exceptions
// (`spec-vertex-format.md` §12.8): every material index scores 1.0000
// across 2,002/2,002 character files and 365/365 vehicle files, with the
// only excluded entries being materials whose own header genuinely declares
// no texture bindings at all.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3geometry/errors.h"
#include "vpp/byte_view.h"

namespace sr3geometry {

using vpp::ByteView;

// Parameter-name hashes that hold constant across unrelated meshes, so a
// slot can be recognised by hash rather than by position alone.
constexpr uint32_t kParamHashNormalMap = 0x2808EB90u;
constexpr uint32_t kParamHashSurfaceMaterial = 0xDFE71DA8u;
// The two colour samplers seen on character meshes. Measured: 0x03B65AF7
// names a `_d`/`_dp` texture in 854/854 entries; 0x69B48F91 in 701/980
// (the remainder are placeholder meshes binding `missing.tga`). Neither is
// universal across formats, which is exactly why diffuse() returns null
// rather than guessing when neither is present.
constexpr uint32_t kParamHashDiffuseA = 0x03B65AF7u;
constexpr uint32_t kParamHashDiffuseB = 0x69B48F91u;

struct TextureRef {
    uint32_t slot = 0;
    uint32_t paramHash = 0;
    std::string name; // as stored: MIXED case, unlike the material block's list
};

struct MaterialBinding {
    std::vector<TextureRef> textures;

    // THE SLOT INDEX IS NOT THE ROLE. It is only position-within-material.
    // Measured across two populations: parameter hash 0x2808EB90 sits at
    // slot 1 on 1,831 character-mesh entries and at slot 0 on vehicles. A
    // `diffuse()` keyed on "slot 0" therefore returns a NORMAL MAP on
    // vehicles while looking perfectly healthy - the silently-plausible
    // failure this reader exists to refuse. The role travels with the hash.
    const TextureRef* byParamHash(uint32_t paramHash) const {
        for (const TextureRef& t : textures)
            if (t.paramHash == paramHash) return &t;
        return nullptr;
    }

    // Hash-keyed, and CONFIRMED: 0x2808EB90 names a `_n` texture in
    // 1,833/1,833 character entries and 36/36 of the vehicle entries
    // carrying it.
    const std::string* normalMap() const {
        const TextureRef* t = byParamHash(kParamHashNormalMap);
        return t != nullptr ? &t->name : nullptr;
    }

    // The colour map, identified by hash rather than position. Returns null
    // when this material carries none of the known diffuse samplers -
    // callers must handle that instead of falling back to slot 0, which is
    // what made this wrong in the first place. Vehicles legitimately return
    // null here: their colour samplers are not among these. RESOLVED
    // 2026-09-29 (HANDOFF.md §9.106): vehicle paint is not a texture at
    // all - it is `Base_Paint_Color`, a per-material runtime float4
    // constant read from THIS SAME record's own array B/C (name-hash ->
    // Vector4 table, spec-vehicle-geometry.md §11.2), not from `.cvtf_pc`
    // and not from this diffuse-sampler mechanism. Confirmed by disassembly
    // (spec-render-pipeline.md §20.12.6) and independently reproduced
    // byte-exact against real `car_4dr_genki_0.ccar_pc` data.
    const std::string* diffuse() const {
        for (uint32_t h : {kParamHashDiffuseA, kParamHashDiffuseB}) {
            const TextureRef* t = byParamHash(h);
            if (t != nullptr) return &t->name;
        }
        return nullptr;
    }
};

class MaterialBindings {
public:
    // `subheaderOffset` is the 0x424BD00D geometry sub-header - i.e.
    // GeometryBlock::offset(). `meshBlockEnd` is one byte past the Mesh
    // sub-block (its offset plus its declared c-length); the per-material
    // records live beyond it.
    //
    // Never throws for a mesh that simply has no binding: that is reported
    // through located() == false, because placeholder meshes legitimately
    // have none. Throws FormatError only when the sub-header magic is
    // wrong, which means the caller passed the wrong offset.
    static MaterialBindings parse(ByteView content, size_t subheaderOffset,
                                  size_t meshBlockEnd);

    // Declared material count, u16 at subheader +0x0C. Equals
    // 1 + max(material id) in 549/549 meshes.
    uint16_t materialCount() const { return materialCount_; }

    // False when materialCount() == 0, or the per-material record chain
    // ran past the end of the file before reaching all of them (a truncated
    // file, not a format error - the sub-header itself was valid). True
    // whenever the record layout was walked successfully, even if some
    // individual material ends up with no textures - that is a per-material
    // outcome (materials()[i].textures.empty()), not a reason to call the
    // whole file unlocated. NOT an invitation to fall back to a guess.
    bool located() const { return located_; }

    // Indexed by material id. Empty when !located().
    const std::vector<MaterialBinding>& materials() const { return materials_; }

    // Convenience: the diffuse name for a draw range's material id, or
    // nullptr if unavailable for any reason.
    const std::string* diffuseFor(uint32_t materialId) const {
        if (!located_ || materialId >= materials_.size()) return nullptr;
        return materials_[materialId].diffuse();
    }

private:
    uint16_t materialCount_ = 0;
    bool located_ = false;
    std::vector<MaterialBinding> materials_;
};

} // namespace sr3geometry
