#pragma once

#include "viewer_platform.h"
#include "sr3mesh/mesh_block.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"
#include "sr3vehicle/cvtf.h"

namespace sr3viewer {
size_t alignUpV(size_t v, size_t a);

void transformPointRowVectorV(const float p[3], const float M[16], float out[3]);


// The real, per-material name-hash -> Vector4 table (array B/C) that
// authors Base_Paint_Color and siblings directly in THIS vehicle's own
// .ccar_pc - HANDOFF Sec9.106, ported from
// tools/prototype_real_shader_draw_multishader_paintcompose.cpp's own
// extractMaterialArrayBC() (see that file's top comment for the derivation:
// array B is one CRC-32-over-lowercased-name hash per entry, the SAME hash
// scheme as sr3fxo::hashLowerName; array C is one Vector4 per array-B entry,
// positionally paired). Reuses the SAME cursor arithmetic as
// extractMaterialShaderHashesV() above up to the per-material record, then
// continues past that record's 0x30-byte header into its own array A/B/C.
struct MaterialConstantV { uint32_t nameHash = 0; std::array<float, 4> value{}; };

struct MaterialArrayBCResultV {
    std::vector<std::vector<MaterialConstantV>> perMaterial; // indexed by materialId
    bool complete = false;
    std::string note;
};

MaterialArrayBCResultV extractMaterialArrayBCV(vpp::ByteView content, size_t subheaderOffset,
                                                size_t meshSubBlockOffset, uint32_t meshCLength,
                                                uint16_t materialCount);

bool createSolidColorTextureV(ID3D11Device* device, const float colorRGBA[4], ID3D11ShaderResourceView** outSrv);


// Resolves EVERY material's real paint colour directly by name-hash,
// WITHOUT compiling any shader (contrast runVehicle()'s own step 8, which
// only learns "this PS constant is named Base_Paint_Color" indirectly, by
// reflecting a compiled real pixel shader's own constant table). The real
// name is already confirmed and written down verbatim in this project's own
// include/sr3geometry/material_binding.h (diffuse()'s doc comment: "it is
// `Base_Paint_Color`, a per-material runtime float4 constant... Confirmed by
// disassembly ... independently reproduced byte-exact against real
// car_4dr_genki_0.ccar_pc data") - so hashing that exact literal with this
// project's own sr3fxo::hashLowerName() (the SAME hash function
// extractMaterialArrayBCV()'s array-B entries are already keyed by) finds
// the same real entry runVehicle() finds, without needing a compiled
// shader's reflection to tell us which constant name to look for. A
// material with no entry at that hash (this vehicle's own test fixture
// is NOT real car_4dr_genki xtbl data, so its materials' real array-B/C
// content - sourced from the REAL .ccar_pc bytes, independent of the
// fixture - may simply not declare this constant for every material id)
// gets a clearly-labelled neutral grey fallback instead, never a guess.
struct VehiclePaintColourV { bool resolved = false; float value[4] = {0.5f, 0.5f, 0.5f, 1.0f}; };

std::vector<VehiclePaintColourV> resolveVehiclePaintColoursV(vpp::ByteView ccarView, size_t geoOffset,
                                                              size_t meshSubBlockOffset, uint32_t meshCLength,
                                                              uint16_t materialCount);

void buildWheelDeltaTransformV(const float pivot[3], float spinAngleRad, float steerAngleRad, float out[16]);

bool isVehicleWheelPartV(const sr3vehicle::VehiclePart& part, bool* outIsFront = nullptr);

bool isPrimaryVehicleWheelPartV(const sr3vehicle::VehiclePart& part);


// ---------------------------------------------------------------------
// §12.1 wheel/rim catalogue (spec-vehicle-geometry.md §12.1, 2026-10-08):
// wheel/rim geometry does NOT live in any vehicle's own .ccar_pc/.gcar_pc -
// it lives once, globally, in vehicles_preload.vpp_pc's own top-level
// externalized_components.cvcomp_pc/.gvcomp_pc (+ a .cpeg_pc/.gpeg_pc
// texture pair, NOT used here - see the FlatTint draw call in
// runDriveDemo() below for why a CHOSEN flat tire-grey tint is used
// instead of a real resolved texture: resolving this container's own
// per-material texture binding is a separate, not-yet-attempted reader
// task, out of this task's own scope). Spec's own direct instruction: this
// reuses the EXISTING .ccar_pc Mesh-sub-block + part-record reader
// unchanged, "not a new format to implement" - the container carries its
// own SHORTER header (leading tag 15, not .ccar_pc's 0x38) but is,
// address-for-address, driven by the SAME loader chain downstream of it.
//
// CONFIRMED DIRECTLY THIS SESSION against the REAL shipped
// vehicles_preload.vpp_pc (this project's own game cache - an ordinary
// game ASSET file this project already reads dozens of elsewhere, NOT the
// executable - "Team B never disassembles" is about the .exe, not the
// game's own data files):
//   - header +0x00 reads 15 (decimal), exactly matching spec's own claim;
//   - header +0x04 is THE SAME FIELD .ccar_pc's own header carries at this
//     exact offset (sr3vehicle::kMeshRegionOffset) - verified byte-for-
//     byte: the value there (0x4480 in the real file) lands EXACTLY on the
//     real 0x424BD00D Mesh sub-block magic, the identical
//     GeometryBlock::parseAt() contract .ccar_pc already uses;
//   - spec's own by-name formula (index*0xE0+0x30+catalogueBase) resolves
//     with catalogueBase=0 (plain file-relative addressing, verified
//     directly - not assumed): record 0's own +0x00 4x4 transform, read at
//     absolute file offset 0x30, is a clean identity matrix, and
//     sequential records' +0x40 name pointers (direct absolute file
//     offsets - the SAME convention src/vehicle.cpp's own readName()
//     already uses for .ccar_pc, no extra "+8" fixup unlike .cvtf_pc's own
//     different idiom) resolve to "000","001","003","005","006","R100",
//     "R101",...,"R254" - 78 real records total in car_4dr_genki's own
//     real build, bounded exactly by the embedded mesh region's own offset
//     (the last valid record ends 16 bytes before the mesh region starts -
//     consistent with this project's own established 16-byte sub-block
//     alignment finding elsewhere).
//   - MEASURED, not assumed from spec's own "~90": exactly 78 real part
//     records, 73 of them real "RNNN" rim-style tokens (the other 5 -
//     "000"/"001"/"003"/"005"/"006" - are NOT rim names; purpose not
//     decoded here).
//   - A REAL, DIRECT, CHECKED CORRECTION to spec's own §12.1 "naming
//     precision" paragraph, worth relaying back: this file's OWN string
//     table carries ONLY bare "RNNN" rim tokens - NOT ONE
//     "W_0NN_*_*_Profile" wheel-profile name appears anywhere in it
//     (checked directly over the whole string region). The W_0NN names
//     spec also mentions live ONLY in each vehicle's own .cvtf_pc (array1,
//     sr3vehicle/cvtf.h's wheelNames()) - they do NOT additionally resolve
//     against THIS container, despite spec's own text suggesting "both...
//     resolve through the same shared .cvcomp_pc/.gvcomp_pc container".
//     This reader therefore only ever attempts to resolve bare "RNNN"
//     tokens against this file.
//
// Since there is no known part-COUNT field for this shorter header (OPEN -
// not given by spec, and nothing here invents one), this reader SCANS
// sequential 0xE0-byte records from file offset 0x30, exactly the same
// "self-validating search, no hardcoded count" idiom this project already
// uses for MeshBlock::drawGroupsLocated()/BlobA (see geometry_block.h's own
// top comment) - stopping at the first record whose name pointer fails to
// resolve, or that would run past the embedded mesh region.
struct ExternalizedCatalogPartV {
    std::array<float, 16> transform{};
    std::string name;
    uint32_t partType = 0;
    uint32_t flags = 0;
    uint32_t parentIndex = 0xFFFFFFFFu;
};

bool loadExternalizedWheelCatalogV(vpp::ByteView content, std::vector<ExternalizedCatalogPartV>& outParts,
                                   size_t& outMeshRegionOffset, uint32_t& outHeaderTag, std::string& diag);

int findExternalizedCatalogPartV(const std::vector<ExternalizedCatalogPartV>& parts, const std::string& name);


// Resolves a catalogue part's OWN renderable geometry out of the shared
// catalogue mesh, by VERTEX OWNERSHIP (sr3mesh::Vertex::rigidPartIndex)
// rather than by inventing any draw-range/part association - the per-
// part/draw-range segmentation question is explicitly OPEN project-wide
// (spec-vehicle-geometry.md §4 "+0xC0/+0xC4", §7.2 Q2, §11's whole series),
// so this reader sidesteps it the same way this file's OWN wheelCentroid/
// wheelDelta code already does for the vehicle's own wheel parts:
// `rigidPartIndex` is a per-vertex field decoded directly off the mesh
// buffer, independent of that open question. A draw range is kept only
// when EVERY vertex its own expanded triangle list touches carries the
// target rigidPartIndex - a defensive, self-validating rule (same spirit
// as this project's other "accept only a candidate that satisfies real
// invariants" idioms, e.g. MeshBlock::drawGroupsLocated()) - a range that
// is only PARTLY owned is dropped rather than guessed at, and reported as
// such via `diag`.
struct CatalogWheelMeshV {
    size_t channelIndex = 0;
    std::vector<bool> rangeMask;
    std::array<float, 3> centroid{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f}, boundsMax{0.0f, 0.0f, 0.0f};
    size_t rangesKept = 0, rangesSkippedMixed = 0, rangesSkippedForeign = 0;
};

bool resolveCatalogWheelMeshV(const sr3mesh::MeshBlock& catMesh, uint32_t targetPartIndex, CatalogWheelMeshV& out,
                              std::string& diag);


// CHOSEN default (spec §12.3: "vehicleType[+0x5c0], a per-vehicle-class
// scale constant (OPEN exact value - pick a CHOSEN reasonable default like
// 1.0... if no better source is found)"). Checked: this project's own
// existing steering/handling data (sr3physics::VehicleTuning,
// SteeringBaseTuning in vehicle_driving_slice.h) has no equivalent scale
// field - maxSteeringAngleRad/steeringRateRadPerSec are RATE/LIMIT
// constants, not a visual-angle-vs-mechanical-angle scale - so no better
// source was found and spec's own suggested default is used verbatim.
constexpr float kWheelVisualSteerScaleV = 1.0f;

void buildWheelAttachTransformV(const float pivot[3], const float anchor[3], float spinAngleRad, float steerAngleRad,
                                float scale, float out[16]);

sr3physics::VehicleTuning BuildDriveDemoVehicleTuning();

} // namespace sr3viewer
