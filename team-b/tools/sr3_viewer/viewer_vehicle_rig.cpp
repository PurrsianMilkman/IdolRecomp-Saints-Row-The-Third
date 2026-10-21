#include "viewer_platform.h"
#include "viewer_vehicle_rig.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3fxo/wrapper_header.h"
#include "sr3mesh/mesh_block.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"
#include "sr3xtbl/xtbl.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"
#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3vehicle/cvtf.h"

using namespace sr3viewer;

namespace sr3viewer {
size_t alignUpV(size_t v, size_t a) { return (v + a - 1) / a * a; }

void transformPointRowVectorV(const float p[3], const float M[16], float out[3]) {
    out[0] = p[0] * M[0] + p[1] * M[4] + p[2] * M[8] + M[12];
    out[1] = p[0] * M[1] + p[1] * M[5] + p[2] * M[9] + M[13];
    out[2] = p[0] * M[2] + p[1] * M[6] + p[2] * M[10] + M[14];
}

MaterialArrayBCResultV extractMaterialArrayBCV(vpp::ByteView content, size_t subheaderOffset,
                                                size_t meshSubBlockOffset, uint32_t meshCLength,
                                                uint16_t materialCount) {
    MaterialArrayBCResultV result;
    if (materialCount == 0 || materialCount > 4096) { result.note = "implausible materialCount"; return result; }
    const uint16_t n2 = content.readU16LE(subheaderOffset + 0x0E);
    size_t cursor = static_cast<size_t>(meshSubBlockOffset) + meshCLength;
    if (n2 > 0) {
        cursor = alignUpV(cursor, 8);
        cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4;
    }
    cursor = alignUpV(cursor, 8);
    cursor += static_cast<size_t>(materialCount) * 8;

    for (uint16_t i = 0; i < materialCount; ++i) {
        if (cursor + 4 > content.size()) { result.note = "ran out of bytes at material " + std::to_string(i); return result; }
        uint32_t declaredSize = content.readU32LE(cursor);
        size_t headerStart = alignUpV(cursor + 4, 8);
        if (headerStart + 0x30 > content.size()) { result.note = "header overrun at material " + std::to_string(i); return result; }
        uint16_t aCount = content.readU16LE(headerStart + 0x0C);
        uint8_t bCount = content.at(headerStart + 0x0E);
        size_t arrayABase = headerStart + 0x30;
        size_t arrayBBase = alignUpV(arrayABase + static_cast<size_t>(aCount) * 12, 4);
        size_t arrayCBase = alignUpV(arrayBBase + static_cast<size_t>(bCount) * 4, 16);
        std::vector<MaterialConstantV> consts;
        for (uint8_t k = 0; k < bCount; ++k) {
            size_t bOff = arrayBBase + static_cast<size_t>(k) * 4;
            size_t cOff = arrayCBase + static_cast<size_t>(k) * 16;
            if (bOff + 4 > content.size() || cOff + 16 > content.size()) break;
            MaterialConstantV mc;
            mc.nameHash = content.readU32LE(bOff);
            for (int w = 0; w < 4; ++w) {
                uint32_t u = content.readU32LE(cOff + static_cast<size_t>(w) * 4);
                std::memcpy(&mc.value[static_cast<size_t>(w)], &u, 4);
            }
            consts.push_back(mc);
        }
        result.perMaterial.push_back(std::move(consts));
        if (declaredSize == 0 || declaredSize > 1000000) {
            result.note = "implausible declaredSize=" + std::to_string(declaredSize) + " at material " + std::to_string(i);
            return result;
        }
        cursor += declaredSize;
    }
    result.complete = true;
    return result;
}


// ===========================================================================
// `drive_demo` (2026-10-08, v1; v2 improvements same day) - the vehicle twin
// of `walk`/runWalkDemo() above: drives a REAL vehicle mesh through a
// SCRIPTED (straight / turn / brake) input sequence, using the real,
// already-existing sr3physics::VehicleSubstepSimulator
// (include/sr3physics/vehicle_substeps.h + src/physics_vehicle_substeps.cpp)
// - a PARALLEL task's own stable, read-only contract (that header/.cpp and
// tests/synthetic_physics_vehicle_*_test.cpp are NOT touched by this
// command; only its public VehicleTuning/DriverInput/VehicleSubstepSimulator/
// VehicleInstanceState surface is consumed, exactly as documented at that
// header's own declarations).
//
// V2 (this task - a viewer-tool demo improvement, deliberately scoped
// pragmatically rather than as a core-engine feature; see each change's own
// helper-function doc comments above this one for the full mechanics):
//   1. REAL VEHICLE TEXTURE: the checker placeholder is gone. Every
//      material's REAL Base_Paint_Color constant (material_binding.h's own
//      documented finding - vehicle paint is a per-material float4 constant,
//      not a texture) is resolved directly by name-hash
//      (resolveVehiclePaintColoursV()) and displayed through
//      MeshRenderer::drawTextured()'s existing per-material-SRV mechanism -
//      the SAME real confirmed data vehicle_genki/vehicle_standard's own
//      frozen goldens display via runVehicle()'s much heavier real shader
//      pipeline, just through this file's lighter, already-established
//      per-material-texture path instead (see createSolidColorTextureV()'s
//      own doc comment for why the heavier pipeline was not ported here).
//   2. REAL SCENERY: the vehicle now drives on the SAME real city_corner
//      cell set (tests/golden/city_corner, frozen 744c088) - loaded via the
//      SAME real CityArchive/CityCell/loadCityProp machinery `city`/`walk`
//      already use, computed ONCE from cityWantedCells() at the vehicle's
//      own start focus (CHOSEN simplification: a STATIC one-time load, not
//      `city`/`walk`'s own dynamic per-frame streaming/eviction - this
//      demo's scripted 4-second drive stays within city_corner's own 160x140m
//      cell neighbourhood, so re-deriving a moving streaming window was not
//      needed for an honest result; said explicitly rather than silently
//      assumed). Ground/terrain tiles are deliberately NOT loaded - this
//      exact location's confirmed real finding (this comment's own prior
//      revision, still true) is that city_corner's 3 resident fine cells
//      carry zero real ground-plane Mesh data, so skipping it changes
//      nothing observable and avoids load time for a guaranteed no-op.
//   3. SIMPLIFIED TRAILING CAMERA: a chase camera follows the vehicle every
//      frame, built from exactly two REAL confirmed geometric facts in
//      spec-player-runtime.md and NOTHING else from that spec's own fuller
//      camera state machine:
//        - Sec4.10 "Row identities": the camera orientation matrix's row 1
//          is right, row 2 is up, row 3 is forward;
//        - Sec4.7 step 13's placement rule, simplified: desired position =
//          pivot + distance*back, back = -forward (row 3 negated) - with
//          collision resolution, the shoulder offset, FOV scaling, and the
//          distance-ramp/fine-aim/scope special cases ALL explicitly
//          dropped, since none of them matter for a straight demo drive on
//          simple scenery (see the per-frame camera block below for exactly
//          which two facts are used and where). The real game's own
//          submode/blend/state-machine system (the rest of Sec4.7/Sec4.10)
//          is NOT implemented - that is the real game's full camera engine,
//          wildly out of scope for a viewer-tool demo, and this comment says
//          so rather than quietly passing off a simplification as that
//          system.
//   4. WHEELS: identified via sr3vehicle::partTypeName()'s own confirmed
//      part-type reading (isVehicleWheelPartV() below) and spun/steered via
//      a CHOSEN, explicitly-flagged kinematic approximation
//      (buildWheelDeltaTransformV()) - NOT a confirmed real formula; see
//      that function's own doc comment for exactly what is assumed (v3,
//      2026-10-08: a DELTA pivoted about each wheel's own derived raw-vertex
//      centroid, applied only to that wheel's own vertices - fixing a
//      shattered-body bug an earlier revision left unresolved, see REAL/
//      CHOSEN breakdown below for the non-wheel-vertex guarantee this keeps).
//
// REAL (v1, unchanged):
//   * the vehicle mesh - the same real .ccar_pc embedded Mesh sub-block
//     (draw group 0, EVERY channel its ranges actually use - v4, 2026-10-08:
//     see the vehicleChannels computation below for why more than channel 0
//     is needed) `scene`'s vehicle slot and `loadVehicleObject()` already
//     use, just loaded inline here now (see below) so this command can
//     additionally reach the real per-part transforms/material bindings
//     `loadVehicleObject()` itself does not expose. Every NON-wheel vertex's
//     position uploaded to the GPU, every frame, is BYTE-IDENTICAL to its
//     own channel's raw decode (vehicleChannels[*].verts) - v3's bug fix
//     guarantee, still held by v4 - see the per-vertex loop in the frame
//     body below;
//   * the wheel pivot centroid each wheel part's spin/steer is applied
//     about - computed once, directly from that same wheel part's own raw
//     vertices (see the wheelCentroid computation below) - a real derived
//     value, not sourced from veh.parts()[i].transform;
//   * the physics - every frame, sim.Step(dt, input) runs the real 13
//     confirmed Havok vehicle sub-steps (spec-physics-runtime.md Sec6) plus
//     that file's own CHOSEN chassis integration; state_.posX/posZ/heading/
//     velForwardBody/yawRate/steeringAngleCurrentRad read back here are the
//     simulator's own real output, never hand-animated.
//
// CHOSEN (v1, unchanged):
//   * the vehicle TUNING fixture (BuildDriveDemoVehicleTuning() below) is the
//     IDENTICAL representative "normal sedan" XML, run through the SAME real
//     sr3xtbl::ParseDocument -> sr3vehicleinfo::ParseVehicleEntry ->
//     sr3physics::VehicleTuning::FromVehicleEntry() path, that
//     tests/synthetic_physics_vehicle_substeps_test.cpp's own
//     BuildTestVehicleTuning() already uses and this project has already
//     reviewed - explicitly NOT real car_4dr_genki xtbl data (this clean-room
//     project has no access to that), kept consistent with precedent rather
//     than inventing different numbers (per a direct instruction on this
//     task: the engine-torque-curve xtbl field-crossing finding tracked
//     separately in HANDOFF.md is real physics work for its own follow-up
//     unit, not touched here);
//   * the scripted 3-phase input sequence (straight/turn/brake, exact frame
//     counts documented at kStraightFrames/kTurnFrames/kBrakeFrames below)
//     and the fixed dt = 1/60 s (matching the substeps test's own kDt) - one
//     sim.Step() per rendered frame, the simplest 1:1 ratio;
//   * the vehicle's start position/offset - near city_corner's own frozen
//     focus point (tests/golden/walk_demo/COMMAND.txt's `--eye -957.66 15
//     -691.66`), so it is actually in frame from frame 0 onward, and so the
//     v2 city-scenery load above resolves to the SAME already-frozen 4-cell
//     neighbourhood city_corner's own golden uses.

// Builds the drive_demo VehicleTuning via the real sr3vehicleinfo/sr3xtbl
// reader path - the IDENTICAL fixture (same element nesting, same concrete
// numbers) as tests/synthetic_physics_vehicle_substeps_test.cpp's own
// BuildTestVehicleTuning(), copied rather than re-derived so this stays
// consistent with already-reviewed precedent (see this command's own task
// brief: "Do not invent different numbers"). A representative, CHOSEN
// "normal sedan" - rear-wheel drive, front-steer, moderate torque/mass - NOT
// real car_4dr_genki xtbl data (this clean-room project has no access to
// that).
// ---------------------------------------------------------------------
// drive_demo v2 helpers (2026-10-08, this task): real vehicle texture,
// real city_corner scenery + a simplified trailing camera, and kinematic
// wheel spin/steer - see this section's own per-function doc comments and
// runDriveDemo()'s own updated top comment for the full REAL/CHOSEN
// breakdown. Nothing here touches runVehicle()/loadVehicleObject()/
// MeshRenderer/runCity()/runWalkDemo() - every one of those stays
// byte-for-byte unchanged (their own frozen goldens are not at risk).
// ---------------------------------------------------------------------

// A 1x1 solid-colour SRV - same technique runVehicle()'s own placeholderTex
// already uses (step 10's own top comment), reused here to display a REAL
// per-material colour value (this vehicle's own Base_Paint_Color constant,
// see resolveVehiclePaintColoursV() below) through MeshRenderer::
// drawTextured()'s existing per-material-SRV mechanism, rather than
// through runVehicle()'s own much heavier real-per-draw-range CUSTOM-SHADER
// pipeline (D3D9 bytecode disassembly/translation/D3DCompile per distinct
// shader). Both paths display the SAME real confirmed data
// (material_binding.h's own documented finding: vehicle paint is not a
// texture, it is a per-material runtime float4 constant, HANDOFF Sec9.106) -
// this is a deliberately LIGHTER rendering of that same real value, chosen
// because drive_demo's vehicle is re-uploaded every frame (wheel pose, see
// buildWheelDeltaTransformV() below) where compiling a real shader
// per distinct material would be substantial unneeded machinery for a
// viewer-tool demo. `colorRGBA` is clamped to [0,1] here only because a
// texture needs byte channels - the same raw real array-B/C value
// runVehicle() feeds directly into an HLSL constant, not re-derived or
// re-scaled.
bool createSolidColorTextureV(ID3D11Device* device, const float colorRGBA[4], ID3D11ShaderResourceView** outSrv) {
    float c[4];
    for (int k = 0; k < 4; ++k) c[k] = colorRGBA[k] < 0.0f ? 0.0f : (colorRGBA[k] > 1.0f ? 1.0f : colorRGBA[k]);
    uint8_t px[4] = {static_cast<uint8_t>(c[0] * 255.0f + 0.5f), static_cast<uint8_t>(c[1] * 255.0f + 0.5f),
                     static_cast<uint8_t>(c[2] * 255.0f + 0.5f), static_cast<uint8_t>(c[3] * 255.0f + 0.5f)};
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem = px; sub.SysMemPitch = 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&td, &sub, &tex)) || !tex) return false;
    HRESULT hr = device->CreateShaderResourceView(tex, nullptr, outSrv);
    tex->Release(); // the SRV holds its own reference; the texture object itself is not needed past this call
    if (SUCCEEDED(hr) && *outSrv != nullptr) sr3render::trackSrv(*outSrv);  // released with the session (releaseTrackedSrvs)
    return SUCCEEDED(hr) && *outSrv != nullptr;
}

std::vector<VehiclePaintColourV> resolveVehiclePaintColoursV(vpp::ByteView ccarView, size_t geoOffset,
                                                              size_t meshSubBlockOffset, uint32_t meshCLength,
                                                              uint16_t materialCount) {
    std::vector<VehiclePaintColourV> out(materialCount);
    MaterialArrayBCResultV arrayBC = extractMaterialArrayBCV(ccarView, geoOffset, meshSubBlockOffset, meshCLength, materialCount);
    const uint32_t kPaintHash = sr3fxo::hashLowerName("Base_Paint_Color");
    for (uint32_t m = 0; m < materialCount && m < arrayBC.perMaterial.size(); ++m) {
        for (const auto& mc : arrayBC.perMaterial[m]) {
            if (mc.nameHash == kPaintHash) {
                out[m].resolved = true;
                for (int k = 0; k < 4; ++k) out[m].value[k] = mc.value[static_cast<size_t>(k)];
                break;
            }
        }
    }
    return out;
}

} // namespace sr3viewer

namespace {

// Rotation about the Z axis (row-vector convention, matching
// sr3render::buildWorldMatrix's own Y-axis rotation immediately below it in
// mesh_renderer.cpp) - used as the wheel SPIN axis (see
// buildWheelDeltaTransformV()'s own doc comment for why Z).
void makeRotationZRowVectorV(float angleRad, float out[16]) {
    const float c = std::cos(angleRad), s = std::sin(angleRad);
    const float m[16] = {
        c,    s,    0.0f, 0.0f,
        -s,   c,    0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::memcpy(out, m, sizeof(m));
}

void makeTranslationRowVectorV(float x, float y, float z, float out[16]) {
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    std::memcpy(out, m, sizeof(m));
}


// Rotation about the X axis (same row-vector convention as
// makeRotationZRowVectorV above) - used as the ATTACHED catalogue wheel
// mesh's own SPIN axis (buildWheelAttachTransformV below), NOT Z. This
// task's own direct measurement (not assumed): the real car_4dr_genki
// vehicle's own wheel-slot anchors (veh.parts()[i].transform translation,
// see the [rim] diagnostic in runDriveDemo()) separate LEFT/RIGHT by X
// (FL x=-0.770 vs FR x=+0.768, near-identical Z) and FRONT/REAR by Z
// (front z=+1.884 vs rear z=-1.492, near-identical X) - i.e. X is this
// vehicle's own real LATERAL/axle axis in its own object space, not Z (the
// existing buildWheelDeltaTransformV's own "lateral at +Z" doc comment,
// written for the vehicle's native near-empty wheel stub geometry, was
// an assumption never checked against real anchor data - this measurement
// refutes it for THIS vehicle). Separately corroborated by the catalogue
// wheel mesh's own measured bounds (resolveCatalogWheelMeshV's [rim]
// diagnostic): Y and Z extents are near-identical (~0.416/0.417) while X
// is visibly different (~0.507) - consistent with the wheel's circular
// face lying in the Y-Z plane, i.e. its own axle direction is X. Directly
// confirmed visually this task (frame-by-frame review, see the commit's
// own report): spinning about Z made the wheel silhouette collapse to a
// thin sliver as roll angle increased (tipping a disc edge-on to a
// side-framed camera), spinning about X keeps a stable circular/elliptical
// silhouette - the expected look for a wheel rolling about its own axle
// viewed from the side.
void makeRotationXRowVectorV(float angleRad, float out[16]) {
    const float c = std::cos(angleRad), s = std::sin(angleRad);
    const float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, c,    s,    0.0f,
        0.0f, -s,   c,    0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::memcpy(out, m, sizeof(m));
}

} // namespace

namespace sr3viewer {

// CHOSEN, EXPLICITLY-FLAGGED KINEMATIC APPROXIMATION - see runDriveDemo()'s
// own "WHEELS" doc-comment section for the full honesty statement; this
// function's own comment covers only the mechanics.
//
// BUG FIX (2026-10-08, v3): this is now a DELTA-ONLY transform - it is
// applied directly to a wheel vertex's own RAW (v1-identical, untransformed)
// position, NEVER composed onto veh.parts()[i].transform as a baseline. An
// earlier revision (preserved in git history, see the WIP commit this fixes)
// used `NewTransform = veh.parts()[i].transform * T(-pivot) * Rspin * Rsteer
// * T(pivot)` and applied THAT to every vertex (even non-wheel ones, as a
// pass-through baseline) - this produced a shattered/streaky body
// silhouette, because veh.parts()[i].transform is not a safe per-vertex
// baseline for this simplified render path (loadVehicleObject()'s v1 path
// never applies it to rendered vertices at all - see runVehicle()'s own
// bounds-only use of the same field for the real precedent). The fix
// sidesteps that question entirely rather than solving it (out of scope for
// a viewer-tool demo): non-wheel vertices are left completely untouched by
// runDriveDemo()'s per-vertex loop below, and wheel vertices get ONLY this
// delta, pivoted about `pivot` - now a REAL DERIVED value (the wheel part's
// own raw-vertex centroid, computed once in runDriveDemo() across EVERY
// channel's own decoded vertices - v4, 2026-10-08: a wheel part's vertices
// are not guaranteed to live in channel 0 alone, see that computation's own
// comment), NOT veh.parts()[i].transform's translation row:
//
//   Delta = T(-pivot) * Rspin * Rsteer * T(pivot)
//
// (row-vector composition: a wheel vertex is rotated about its own
// part's centroid by the spin/steer - exactly as if the wheel sub-mesh were
// rigidly attached to its axle and that axle were spun/steered in place).
//
// AXIS CHOICE (CHOSEN, not extracted from the rig/part data): this file's
// existing sim/mesh-placement convention already has heading 0 = facing
// +X with Y up (VehicleInstanceState::heading's own doc comment;
// sr3render::buildWorldMatrix never touches Y), and the SAME engine-wide
// cross(forward,worldUp)=right convention this file's camera code already
// uses elsewhere (buildFreeViewProjection/computeOrbitViewMatrixV) puts
// "right"/lateral at +Z when forward=+X. So: STEER rotates about object-
// space Y (the vehicle's own vertical/yaw axis - reuses
// sr3render::buildWorldMatrix(0,0,0,angle,1,...) directly, since a pure
// Y-rotation with zero translation is exactly what that function already
// builds), and SPIN rotates about object-space Z (the lateral axle axis).
// This assumes each wheel part's own install orientation in object space
// genuinely has its rolling plane perpendicular to Z - a reasonable
// assumption for a forward-facing standard car layout, but NOT a per-part
// axle direction actually decoded from this vehicle's own rig/part data
// (that would need the part's own local rotation decomposed out of its
// 4x4 transform, which this demo does not attempt - said plainly, not
// hidden, exactly as this task's own brief asks for an honest kinematic
// approximation rather than a fabricated "confirmed" axis).
void buildWheelDeltaTransformV(const float pivot[3], float spinAngleRad, float steerAngleRad, float out[16]) {
    float tNeg[16]; makeTranslationRowVectorV(-pivot[0], -pivot[1], -pivot[2], tNeg);
    float rSpin[16]; makeRotationZRowVectorV(spinAngleRad, rSpin);
    float rSteer[16]; sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, steerAngleRad, 1.0f, rSteer);
    float tPos[16]; makeTranslationRowVectorV(pivot[0], pivot[1], pivot[2], tPos);
    float step1[16]; sr3render::multiplyMatrix4x4(tNeg, rSpin, step1);
    float step2[16]; sr3render::multiplyMatrix4x4(step1, rSteer, step2);
    sr3render::multiplyMatrix4x4(step2, tPos, out);
}


// True for a part whose sr3vehicle::partTypeName() reading names it a wheel
// (any of the 5 confirmed wheel part types - see vehicle.cpp's own
// partTypeName(): "front wheel"/"rear wheel"/"front wheel (set 2)"/
// "rear wheel (set 2)"/"rear wheel (set 3)", part types 2/3/18/23/24).
// `outIsFront` (when non-null) reports whether it is specifically a FRONT
// wheel (steered) as opposed to rear (not steered) - this demo's simplified
// reading of "which wheels steer" (the real per-axle Wheel_Does_Steer field
// drives the PHYSICS already, via VehicleTuning::front/rear; this is only
// about which MESH parts visually yaw to match).
bool isVehicleWheelPartV(const sr3vehicle::VehiclePart& part, bool* outIsFront) {
    const bool isFront = part.partType == 2 || part.partType == 18;
    const bool isRear = part.partType == 3 || part.partType == 23 || part.partType == 24;
    if (outIsFront) *outIsFront = isFront;
    return isFront || isRear;
}


// True only for the PRIMARY 4-wheel set (part types 2/3 - "front wheel"/
// "rear wheel", §4.1) as opposed to the secondary/tertiary sets (18/23/24 -
// "...set 2"/"...set 3"). spec-vehicle-geometry.md §12.3 draws exactly this
// distinction for the suspension mechanism (CONFIRMED only for secondary/
// dual-axle sets, primary mechanism unresolved) - reused here for the same
// split for wheel-mesh ATTACHMENT (improvement 5, this task): only the
// primary 4 slots get a resolved externalized catalogue wheel mesh
// attached (CHOSEN scope - attaching to every set would render overlapping
// wheels for a 4-wheel car, since the secondary/tertiary sets are
// customization/alternate-axle alternatives, not additional wheels that
// are simultaneously visible - see loadExternalizedWheelCatalogV()'s own
// doc comment section for the full picture).
bool isPrimaryVehicleWheelPartV(const sr3vehicle::VehiclePart& part) {
    return part.partType == 2 || part.partType == 3;
}

} // namespace sr3viewer

namespace {

// Same NUL-terminated/printable-only/direct-absolute-offset read as
// src/vehicle.cpp's own anonymous-namespace readName() (not reusable
// directly - it is file-local there), duplicated here verbatim rather than
// exported, per spec's own "reuse the SAME reader" instruction applying to
// the FORMAT, not requiring a shared symbol.
bool readExternalizedCatalogNameV(vpp::ByteView content, uint32_t offset, std::string& out) {
    if (offset == 0xFFFFFFFFu || offset >= content.size()) return false;
    size_t at = offset;
    std::string s;
    while (at < content.size()) {
        uint8_t c = content.at(at);
        if (c == 0) break;
        if (c < 32 || c >= 127) return false;
        s.push_back(static_cast<char>(c));
        if (s.size() > 128) return false;
        ++at;
    }
    if (s.empty()) return false;
    out = s;
    return true;
}

} // namespace

namespace sr3viewer {

bool loadExternalizedWheelCatalogV(vpp::ByteView content, std::vector<ExternalizedCatalogPartV>& outParts,
                                   size_t& outMeshRegionOffset, uint32_t& outHeaderTag, std::string& diag) {
    if (content.size() < 0x30) {
        diag = "file smaller than the 0x30-byte short header";
        return false;
    }
    outHeaderTag = content.readU32LE(0x00);
    outMeshRegionOffset = content.readU32LE(0x04);
    if (outMeshRegionOffset == 0xFFFFFFFFu || outMeshRegionOffset >= content.size()) {
        diag = "header +0x04 (embedded mesh region offset) does not resolve inside this file";
        return false;
    }
    size_t idx = 0;
    while (true) {
        const size_t at = 0x30 + idx * 0xE0;
        if (at + 0xE0 > outMeshRegionOffset) break; // never scan into the mesh region itself
        const uint32_t nameOff = content.readU32LE(at + 0x40);
        std::string name;
        if (!readExternalizedCatalogNameV(content, nameOff, name)) break; // first invalid record ends the scan
        ExternalizedCatalogPartV p;
        for (size_t e = 0; e < 16; ++e) {
            uint32_t bits = content.readU32LE(at + e * 4);
            std::memcpy(&p.transform[e], &bits, sizeof(float));
        }
        p.name = name;
        p.partType = content.readU32LE(at + 0x44);
        p.flags = content.readU32LE(at + 0x48);
        p.parentIndex = content.readU32LE(at + 0x4C);
        outParts.push_back(std::move(p));
        ++idx;
    }
    diag = "headerTag=" + std::to_string(outHeaderTag) + " meshRegionOffset=" + std::to_string(outMeshRegionOffset) +
           " partsScanned=" + std::to_string(outParts.size());
    return !outParts.empty();
}


// Case-insensitive EXACT match only - the catalogue's own names are short,
// unambiguous bare tokens like "R100", unlike CvtfCatalog::findEntryByName's
// richer names that benefit from a substring fallback.
int findExternalizedCatalogPartV(const std::vector<ExternalizedCatalogPartV>& parts, const std::string& name) {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name.size() != name.size()) continue;
        bool eq = true;
        for (size_t c = 0; c < name.size() && eq; ++c) {
            char a = parts[i].name[c], b = name[c];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) eq = false;
        }
        if (eq) return static_cast<int>(i);
    }
    return -1;
}


bool resolveCatalogWheelMeshV(const sr3mesh::MeshBlock& catMesh, uint32_t targetPartIndex, CatalogWheelMeshV& out,
                              std::string& diag) {
    if (!catMesh.drawGroupsLocated() || catMesh.drawGroups().empty()) {
        diag = "catalogue mesh has no located draw groups";
        return false;
    }
    const auto& group0 = catMesh.drawGroups()[0];
    std::vector<uint32_t> distinctChannels;
    for (const auto& rg : group0) {
        if (std::find(distinctChannels.begin(), distinctChannels.end(), rg.submeshIndex) == distinctChannels.end())
            distinctChannels.push_back(rg.submeshIndex);
    }
    std::sort(distinctChannels.begin(), distinctChannels.end());
    for (uint32_t c : distinctChannels) {
        if (c >= catMesh.channels().size()) continue;
        std::vector<sr3mesh::Vertex> verts = catMesh.decodeChannel(static_cast<size_t>(c));
        std::vector<bool> mask(group0.size(), false);
        size_t kept = 0, mixed = 0, foreign = 0;
        for (size_t r = 0; r < group0.size(); ++r) {
            if (group0[r].submeshIndex != c) continue;
            std::vector<uint32_t> tri = catMesh.triangleListForRange(group0[r]);
            if (tri.empty()) continue;
            bool allOwned = true, anyOwned = false;
            for (uint32_t vi : tri) {
                if (vi >= verts.size() || verts[vi].rigidPartIndex != targetPartIndex) {
                    allOwned = false;
                } else {
                    anyOwned = true;
                }
            }
            if (allOwned && anyOwned) {
                mask[r] = true;
                ++kept;
            } else if (anyOwned) {
                ++mixed;
            } else {
                ++foreign;
            }
        }
        if (kept > 0) {
            double sx = 0, sy = 0, sz = 0;
            size_t n = 0;
            float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
            for (const auto& v : verts) {
                if (v.rigidPartIndex == targetPartIndex) {
                    sx += v.position[0]; sy += v.position[1]; sz += v.position[2];
                    ++n;
                    for (int k = 0; k < 3; ++k) {
                        if (v.position[k] < mn[k]) mn[k] = v.position[k];
                        if (v.position[k] > mx[k]) mx[k] = v.position[k];
                    }
                }
            }
            out.channelIndex = c;
            out.rangeMask = std::move(mask);
            out.rangesKept = kept;
            out.rangesSkippedMixed = mixed;
            out.rangesSkippedForeign = foreign;
            if (n > 0) {
                out.centroid = {static_cast<float>(sx / n), static_cast<float>(sy / n), static_cast<float>(sz / n)};
                out.boundsMin = {mn[0], mn[1], mn[2]};
                out.boundsMax = {mx[0], mx[1], mx[2]};
            }
            diag = "channel " + std::to_string(c) + ": " + std::to_string(kept) + " range(s) wholly owned, " +
                   std::to_string(mixed) + " mixed (dropped), " + std::to_string(foreign) + " foreign (dropped), " +
                   std::to_string(n) + " owned vertices, bounds (" + std::to_string(mn[0]) + "," +
                   std::to_string(mn[1]) + "," + std::to_string(mn[2]) + ")..(" + std::to_string(mx[0]) + "," +
                   std::to_string(mx[1]) + "," + std::to_string(mx[2]) + ")";
            return true;
        }
    }
    diag = "no draw range in any channel is wholly owned by rigidPartIndex " + std::to_string(targetPartIndex);
    return false;
}


// Builds a wheel ATTACH transform (distinct from buildWheelDeltaTransformV's
// POSE-IN-PLACE delta above): poses a mesh about its OWN local pivot
// (spin/steer), then PLACES the result at a DIFFERENT point (`anchor` -
// the vehicle's own real wheel-slot position) rather than back at its own
// pivot. Used for the externally-attached catalogue wheel mesh (§12.1),
// whose own local space has nothing to do with the vehicle's object space
// until placed. Same row-vector composition and SAME axis choices
// (steer = object-space Y, spin = object-space Z) as
// buildWheelDeltaTransformV above - see that function's own doc comment
// for the full CHOSEN-axis reasoning, unchanged here.
// `scale` (added this task, CHOSEN - see the call site's own doc comment):
// a uniform scale applied about `pivot` BEFORE spin/steer, calibrating the
// catalogue mesh's own native size against this demo's existing
// tuning.wheelRadius constant - the catalogue's shared base shape (§12.1's
// own fallback finding) measures smaller than a real car wheel in this
// vehicle's own file.
void buildWheelAttachTransformV(const float pivot[3], const float anchor[3], float spinAngleRad, float steerAngleRad,
                                float scale, float out[16]) {
    float tNeg[16]; makeTranslationRowVectorV(-pivot[0], -pivot[1], -pivot[2], tNeg);
    float sScale[16]; sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, 0.0f, scale, sScale);
    // X, not Z - see makeRotationXRowVectorV()'s own doc comment for the
    // real measurement (both vehicle-anchor and catalogue-mesh-bounds
    // based) and the direct visual confirmation (this task's frame review)
    // behind this axis choice.
    float rSpin[16]; makeRotationXRowVectorV(spinAngleRad, rSpin);
    float rSteer[16]; sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, steerAngleRad, 1.0f, rSteer);
    float tPos[16]; makeTranslationRowVectorV(anchor[0], anchor[1], anchor[2], tPos);
    float step0[16]; sr3render::multiplyMatrix4x4(tNeg, sScale, step0);
    float step1[16]; sr3render::multiplyMatrix4x4(step0, rSpin, step1);
    float step2[16]; sr3render::multiplyMatrix4x4(step1, rSteer, step2);
    sr3render::multiplyMatrix4x4(step2, tPos, out);
}


sr3physics::VehicleTuning BuildDriveDemoVehicleTuning() {
    const char* xml =
        "<root><Table><Vehicle>"
        "<Name>test_sedan01</Name>"
        "<Mass>1400</Mass>"
        "<Engine>"
        "<Torque>280</Torque><Min_RPM>800</Min_RPM><Opt_RPM>4500</Opt_RPM><Max_RPM>6500</Max_RPM>"
        "<Min_RPM_Torque_Factor>0.5</Min_RPM_Torque_Factor><Max_RPM_Torque_Factor>0.4</Max_RPM_Torque_Factor>"
        // BUG FIX (2026-10-08, flagged by a sibling agent working the same
        // file area, folded into this task's own commit rather than a
        // second conflicting edit): this fixture never set the 3 real
        // Min/Opt/Max_RPM_Resistance xtbl fields, so VehicleTuning fell
        // back to its own CHOSEN struct defaults (torqueFactorIdle/Peak/
        // Redline = 0.5/1.0/0.5) - per EngineNetTorque()'s CONFIRMED CROSSED
        // mapping (spec-physics-runtime.md Sec10.2, src/physics_vehicle_
        // driving_slice.cpp) netTorque = resistanceFactor*throttle -
        // torqueFactor, and torqueFactor's own low-band value never drops
        // below torqueFactorIdle=0.5 while resistanceFactor is scaled by
        // f^2 (f=0 at idle rpm) - so netTorque is strictly negative at
        // idle and stays bounded well below what full throttle could ever
        // overcome across the low band, making this demo's scripted engine
        // net-negative (reverse-biased) at the rpms it actually drives
        // through. Same real fix already proven correct and exercised by
        // tests/synthetic_physics_vehicle_substeps_test.cpp's own
        // BuildTestVehicleTuning() (that function's own Engine-block doc
        // comment has the full derivation) - verbatim same 3 values ported
        // here rather than re-derived, since this fixture shares the exact
        // same Mass/Torque/RPM band/gearing as that one. Min_RPM_Torque_
        // Factor/Max_RPM_Torque_Factor deliberately left at this fixture's
        // own existing 0.5/0.4 (NOT ported to that test's own revised
        // 1.0/1.0) - verified empirically this task (see runDriveDemo()'s
        // own per-frame RPM/position trace) that 0.5/0.4 is already enough
        // once the structural-negative bug above is gone: resistanceFactor
        // (CROSSED from these two fields) now has a near-zero torqueFactor
        // to beat instead of >=0.5, so the car accelerates forward under
        // the scripted straight-phase throttle without needing the test
        // fixture's own separate (different problem: "bounded under 20% of
        // rated torque", not negative) 1.0/1.0 headroom fix.
        "<Min_RPM_Resistance>0.02</Min_RPM_Resistance><Opt_RPM_Resistance>0.0</Opt_RPM_Resistance>"
        "<Max_RPM_Resistance>0.001</Max_RPM_Resistance>"
        "</Engine>"
        "<Axles>"
        "<Axle><Wheel_Does_Steer>yes</Wheel_Does_Steer><Wheel_Friction>1.0</Wheel_Friction>"
        "<Wheel_Max_Friction>1.0</Wheel_Max_Friction><Wheel_Braking_Torque>800</Wheel_Braking_Torque>"
        "<Wheel_Engine_Torque>0.0</Wheel_Engine_Torque></Axle>"
        "<Axle><Wheel_Does_Steer>no</Wheel_Does_Steer><Wheel_Friction>1.0</Wheel_Friction>"
        "<Wheel_Max_Friction>1.0</Wheel_Max_Friction><Wheel_Braking_Torque>600</Wheel_Braking_Torque>"
        "<Wheel_Engine_Torque>1.0</Wheel_Engine_Torque></Axle>"
        "</Axles>"
        "<Transmission>"
        "<Gear_Ratios><Element>3.4</Element><Element>2.1</Element><Element>1.4</Element><Element>1.0</Element><Element>0.8</Element></Gear_Ratios>"
        "<Diff_Gear_Ratio>3.7</Diff_Gear_Ratio>"
        "</Transmission>"
        "<Steering><Max_Steering_Angle>35</Max_Steering_Angle></Steering>"
        "<Aerodynamics><Drag_Coefficient>0.32</Drag_Coefficient><Frontal_Area>2.2</Frontal_Area><Air_Density>1.2</Air_Density></Aerodynamics>"
        "<Vehicle_Type><Automobile></Automobile></Vehicle_Type>"
        "</Vehicle></Table></root>";
    sr3xtbl::Document doc = sr3xtbl::ParseDocument(xml);
    const sr3xtbl::Node* row = sr3xtbl::FindChild(doc.table(), "Vehicle");
    sr3vehicleinfo::VehicleEntry entry = sr3vehicleinfo::ParseVehicleEntry(row);
    return sr3physics::VehicleTuning::FromVehicleEntry(entry);
}

} // namespace sr3viewer