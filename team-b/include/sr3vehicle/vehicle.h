// Reader for `.ccar_pc` vehicle assemblies (spec-vehicle-geometry.md).
//
// `.ccar_pc` IS NOT A NEW MESH FORMAT. It is an assembly container that
// embeds formats this project already reads and adds a part hierarchy on
// top:
//
//   * header `+0x04` -> the complete `.ccmesh_pc` chain: the `0x424BD00D`
//     sub-header, six arrays, and the shared "Mesh" sub-block. Feed
//     meshRegionOffset() to `sr3geometry::GeometryBlock` and the result to
//     `sr3mesh::MeshBlock` - both work unchanged (spec §7);
//   * header `+0x08` -> an embedded `.cmorph_pc` "Morph" block in 326/393,
//     mode 0, with its bulk data in the paired `.gcar_pc` at `+0x0C`;
//   * a named part hierarchy bound to `.rig_pc` bones.
//
// So this library is deliberately small: it reads the outer header and the
// part array, and hands the rest to the existing readers rather than
// reimplementing them.
//
// WHAT THIS READER DOES NOT DO, STATED PLAINLY.
//
// 1. **Materials.** The shared material-reference block is ABSENT in
//    393/393 vehicles (spec §7), so there is no material name list here at
//    all. The per-material texture BINDINGS are recoverable, though: they
//    live in each material's own record and `sr3geometry::MaterialBindings`
//    reads them by direct arithmetic (393/393 vehicles, HANDOFF §9.69). What
//    is still HYPOTHESIS and OPEN is where vehicle PAINT comes from (the
//    per-vehicle `.cvtf_pc` catalogue), so the bound textures are the
//    generic/shared ones and a faithfully painted vehicle cannot be produced
//    from this file alone; pretending otherwise would bind wrong maps.
//
// 2. **Bone binding.** Part transforms are expressed relative to `.rig_pc`
//    bones, and the engine resolves every part NAME to a bone by a
//    case-insensitive STRING compare over the rig's bone array (spec-rig-
//    format.md §13.1; an earlier reading, "through the rig's hash table", is
//    withdrawn). The rig is referenced from a global vehicle-info table, NOT
//    from this file, so this reader cannot find it on its own. `sr3rig` is a
//    hard dependency of vehicle *rendering*, and the caller must supply the
//    rig (`sr3rig::Rig::findBone` implements the same compare).
//
// 3. **Four hardcoded vehicles.** The engine special-cases
//    `truck_2dr_garbage01`, `sp_backhoe01`, `truck_2dr_tow01` and
//    `car_2dr_muscle04` by name, setting flag `0x8000` on particular hinged
//    parts (spec §5). That is behaviour, not data: a faithful reader will
//    differ on those four unless the caller reproduces it. Deliberately not
//    baked in here - see specialCaseNote().

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3vehicle/errors.h"
#include "vpp/byte_view.h"

namespace sr3vehicle {

using vpp::ByteView;

constexpr uint32_t kHeaderMagic = 0x38u;       // header +0x00, required by the loader
constexpr size_t kMeshRegionOffset = 0x04;
constexpr size_t kMorphOffset = 0x08;
constexpr size_t kGcarMorphOffset = 0x0C;
constexpr size_t kAnchorNameOffset = 0x3C;
constexpr size_t kPartCountOffset = 0x390;
constexpr size_t kPartArrayOffset = 0x3A0;
constexpr size_t kPartRecordSize = 0xE0;
constexpr uint32_t kNoParent = 0xFFFFFFFFu;
constexpr uint32_t kAbsent = 0xFFFFFFFFu;      // -1 in an offset field

// Part-type readings decoded from name/type co-occurrence over 9,951
// records (spec §4.1). HIGH CONFIDENCE, not CONFIRMED - and the rare values
// are deliberately absent rather than guessed.
const char* partTypeName(uint32_t type);

struct VehiclePart {
    // 4x4 f32, ROW-MAJOR, translation in row 3. The column-major reading
    // passes only on the 1,694 rotation-free records where both readings
    // coincide, so it is not an alternative (spec §4).
    std::array<float, 16> transform{};

    std::string name;        // resolves in 9,951/9,951
    uint32_t partType = 0;   // 1..26; see partTypeName()
    uint32_t flags = 0;      // bit 0x8000 set by the engine's special cases
    uint32_t parentIndex = kNoParent;

    // Record +0x68 bit 0x80: excluded from bounding-box accumulation.
    bool excludedFromBounds = false;

    bool isRoot() const { return parentIndex == kNoParent; }

    float translationX() const { return transform[12]; }
    float translationY() const { return transform[13]; }
    float translationZ() const { return transform[14]; }
};

class Vehicle {
public:
    // Parses a whole `.ccar_pc`. Throws FormatError if the `0x38` header
    // word is wrong, if the part array does not fit, if a part name pointer
    // does not resolve, or if a parent index is out of range - all
    // properties the spec confirms hold in 393/393 shipped files, so a
    // violation means this is not the format.
    static Vehicle parse(ByteView content);

    const std::vector<VehiclePart>& parts() const { return parts_; }

    // Offset of the embedded `.ccmesh_pc` chain. Hand this to
    // `sr3geometry::GeometryBlock::parse` - the `0x424BD00D` sub-header sits
    // exactly here in 393/393.
    size_t meshRegionOffset() const { return meshRegionOffset_; }

    bool hasMorph() const { return morphOffset_ != kAbsent; }
    uint32_t morphOffset() const { return morphOffset_; }
    // Offset into the PAIRED .gcar_pc (not this file) for the morph's bulk
    // data - it is >= the .ccar_pc size in all 326 morph-bearing files.
    uint32_t gcarMorphOffset() const { return gcarMorphOffset_; }

    // Bone name at header +0x3C, looked up before any part.
    const std::string& anchorName() const { return anchorName_; }

    // True when this vehicle is one of the four the engine special-cases by
    // name (spec §5). The reader does NOT apply them - it reports so a
    // caller can decide, because they are engine behaviour rather than
    // file content.
    static bool hasEngineSpecialCase(const std::string& vehicleName);
    static const char* specialCaseNote(const std::string& vehicleName);

private:
    std::vector<VehiclePart> parts_;
    size_t meshRegionOffset_ = 0;
    uint32_t morphOffset_ = kAbsent;
    uint32_t gcarMorphOffset_ = kAbsent;
    std::string anchorName_;
};

// The `.gcar_pc` 16-byte header: a cross-reference u32 equal to the
// embedded Mesh sub-block's check value (393/393), then 12 zero bytes.
// Returns false if the buffer is too small.
bool readGcarCrossReference(ByteView gcar, uint32_t& outValue);

} // namespace sr3vehicle
