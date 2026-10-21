#pragma once

#include "viewer_platform.h"
#include "viewer_common.h"
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
#include "sr3geometry/character_matlib.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "sr3vehicle/vehicle.h"
#include "sr3player/ground_follow.h"
#include "vpp/container.h"
#include "sr3player/camera_free_seed.h"
#include "sr3player/character_collision.h"
#include "sr3player/seated_ride_pose.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"
#include "sr3vehicle/cvtf.h"

namespace sr3viewer {

// Adapter (vehicle-world-collision follow-up task, 2026-10-08, spec-
// physics-runtime.md §10a) - wraps a sr3player::CameraCollision (the SAME
// real swept-sphere query RealCameraCollision above already provides the
// follow camera - one real `.clmesh_pc` triangle source, now a THIRD
// consumer) as an sr3physics::IVehicleWorldCollisionQuery, the small
// decoupled seam include/sr3physics/vehicle_world_collision.h's own
// ApplyVehicleWorldCollisionResponse() needs so that function stays
// testable without any D3D11/CameraCollision dependency. A plain type
// adapter, NOT a second triangle decode and NOT a new query
// implementation - see PlaytestVehicleRig::ApplyWorldCollisionResponse()
// (above runDriveDemo(), this file) for the one real call site.
class RealCameraCollisionAsVehicleWorld : public sr3physics::IVehicleWorldCollisionQuery {
public:
    explicit RealCameraCollisionAsVehicleWorld(sr3player::CameraCollision& inner) : inner_(inner) {}
    bool SweepSphere(double x0, double y0, double z0, double x1, double y1, double z1, double radius,
                      sr3physics::VehicleWorldContact& out) const override {
        std::optional<sr3player::CameraSweepHit> hit =
            inner_.sweepSphere(sr3player::Vec3{x0, y0, z0}, sr3player::Vec3{x1, y1, z1}, radius);
        if (!hit) return false;
        out.x = hit->position.x;
        out.y = hit->position.y;
        out.z = hit->position.z;
        out.nx = hit->normal.x;
        out.ny = hit->normal.y;
        out.nz = hit->normal.z;
        return true;
    }

private:
    sr3player::CameraCollision& inner_;
};


// ===========================================================================
// PlaytestVehicleRig (2026-10-08, playtest vehicle-entry task) - a self-
// contained vehicle load/pose/draw helper for `playtest`'s new "walk up to
// a car, press E to get in, drive, press E to get out" interaction
// (runPlaytest(), far below this file). PORTS the real mesh-load/wheel-
// pose/paint/wheel-catalogue-attach PATTERN of runDriveDemo() immediately
// above by calling every one of that section's own free helper functions
// UNCHANGED (BuildDriveDemoVehicleTuning, isVehicleWheelPartV/
// isPrimaryVehicleWheelPartV, buildWheelDeltaTransformV/
// buildWheelAttachTransformV, resolveVehiclePaintColoursV/
// createSolidColorTextureV, loadExternalizedWheelCatalogV/
// resolveCatalogWheelMeshV/findExternalizedCatalogPartV, transformPointRowVectorV)
// rather than re-deriving any of their own logic - only the per-command
// "glue" (archive load, member storage, upload-call plumbing) is
// duplicated here, and runDriveDemo() itself is left completely untouched
// (not one line changed) so its own frozen golden
// (tests/golden/drive_demo_coverage) stays byte-for-byte safe.
//
// Carries the SAME car-parts wheel-placement fix runDriveDemo() now has
// (commit e225cdd + follow-up): non-wheel vertices render at their RAW,
// untransformed decode position (`Channel::basePositions`, built
// identically to runDriveDemo()'s own vehicleBasePositions), wheel
// vertices get ONLY a pivoted delta transform (buildWheelDeltaTransformV,
// pivoted about the wheel part's own derived raw-vertex centroid) -
// verified by reusing the identical code shape, not re-derived or assumed
// still fixed.
struct PlaytestVehicleRig {
    struct Channel {
        size_t channelIndex = 0;
        std::unique_ptr<sr3render::MeshRenderer> renderer;
        std::vector<sr3mesh::Vertex> verts;
        std::vector<bool> rangeMask;
        std::vector<std::array<float, 3>> basePositions; // raw decode position - the wheel-placement fix
        size_t drawGroup = 0; // AW-15: draw group this channel renderer draws (0 for everything but RebuildLodHeadChannels())
    };
    struct WheelSlot {
        size_t partIndex = 0;
        std::array<float, 3> anchor{0.0f, 0.0f, 0.0f};
        bool isFront = false;
        float sign = 1.0f;
        int simWheelIdx = 0;
    };

    sr3physics::VehicleTuning tuning;
    std::unique_ptr<sr3physics::VehicleSubstepSimulator> sim;
    std::unique_ptr<sr3vehicle::Vehicle> vehPtr;
    std::unique_ptr<sr3mesh::MeshBlock> meshPtr;
    std::vector<Channel> channels;
    std::vector<ID3D11ShaderResourceView*> perMaterialSrv;
    std::vector<bool> partIsWheel, partIsFrontWheel;
    std::vector<double> wheelRollAngleRad;
    std::vector<std::array<float, 3>> wheelCentroid;

    // External wheel/rim catalogue attach (optional - see LoadWheelCatalog()'s
    // own doc comment: a load failure here still leaves the vehicle's own
    // body renderable, honestly reported via catalogWheelReady==false rather
    // than fabricated).
    std::unique_ptr<sr3render::MeshRenderer> catalogWheelRenderer;
    std::array<float, 3> catalogWheelPivot{0.0f, 0.0f, 0.0f};
    float catalogWheelScale = 1.0f;
    bool catalogWheelReady = false;
    std::vector<WheelSlot> wheelSlots;
    std::array<double, 4> suspensionReferenceForce{-1.0, -1.0, -1.0, -1.0};
    std::array<double, 4> suspensionSmoothedOffsetY{0.0, 0.0, 0.0, 0.0};
    std::vector<std::array<float, 16>> wheelAttachWorld; // vehicle-LOCAL, composed with the caller's world at Draw() time

    // World collision (THIS TASK, 2026-10-08, spec-physics-runtime.md §10a) -
    // a single bounding-SPHERE approximation of the vehicle's own footprint,
    // CHOSEN per this task's own brief ("a swept-sphere or swept-box test
    // against the vehicle's own approximate extent is fine") rather than the
    // real per-part hkpBoxShape set §10a cites (not reproduced - that is a
    // Havok-internal shape census this project has no narrowphase for, same
    // boundary as every other Havok-internal item this spec file declines to
    // cross). Computed in Load() from tuning.wheelBase/trackWidth (themselves
    // already CHOSEN, not xtbl-sourced - see VehicleTuning's own doc
    // comment) plus a CHOSEN front/rear overhang margin, since wheelBase/
    // trackWidth are axle-to-axle/wheel-to-wheel measurements, not bumper-to-
    // bumper ones.
    double collisionRadius = 2.1;

    bool ready = false;

    // Loads the real vehicle mesh/parts/materials from `vehArchivePath` ::
    // `cmeshName` (same real .ccar_pc reader runDriveDemo() uses) and,
    // when `wheelsArchivePath` is non-empty, the real externalized wheel/
    // rim catalogue (spec-vehicle-geometry.md §12.1). Returns false only on
    // a genuine body-mesh load failure (outDiag explains why) - a wheel-
    // catalogue failure alone does not fail this call (catalogWheelReady
    // stays false, the body still renders), matching runDriveDemo()'s own
    // "vehicle still renders" discipline.
    // `preloadedArchive` (ambient-model-variety task): when non-null the vehicle archive bytes are taken from
    // this view (a caller-owned mapping shared across many Load() calls) instead of reading `vehArchivePath`.
    bool Load(ID3D11Device* device, const std::string& vehArchivePath, const std::string& cmeshName,
              const std::string& wheelsArchivePath, std::string& outDiag,
              const vpp::ByteView* preloadedArchive = nullptr) {
        tuning = BuildDriveDemoVehicleTuning();
        // car_4dr_genki (this rig's vehicle) chassis values from spec-physics-runtime.md
        // 10.11.9 test vectors (static, resolved from shipped data) replace the synthetic
        // sedan's CHOSEN chassis numbers. Engine/gearing stay the drive_demo fixture
        // (spec 10.11.9 gives gears only; torque scale is OPEN, runtime global 0x02845af4).
        {
            tuning.mass = 1500.0;                                   // Mass
            tuning.yawInertia = tuning.mass * 2.18906;               // 10.11.2: Mass x Yaw_Unit_Inertia
            tuning.wheelBase = 3.3784;                                // 10.11.9 wheelbase
            tuning.trackWidth = 0.5 * (1.5376 + 1.4405);              // mean of F/R track
            tuning.wheelRadius = sr3physics::WheelRadiusFromSizeIndex(11);  // 0.2458
            // COM height above ground: wheel centre y -0.2185 minus radius gives ground at -0.4643;
            // COM y -0.3127 (10.11.9) -> 0.1516 m. Derived from spec numbers (sag ignored).
            tuning.cgHeight = (-0.3127) - (-0.2185 - tuning.wheelRadius);
            tuning.maxSteeringAngleRad = 0.61087;                      // 35 deg
            tuning.steeringRateRadPerSec = 18.850;                     // Steering_Wheel_Max_Speed
            tuning.steeringCenteringRateRadPerSec = 18.850;             // return speed
            tuning.steeringSpeedThresholdMPerSec = 18.999;              // Max_Speed_Steering
            tuning.front.wheelFriction = tuning.rear.wheelFriction = 1.25;      // Wheel_Friction
            tuning.front.wheelMaxFriction = tuning.rear.wheelMaxFriction = 2.5;  // Wheel_Max_Friction
            tuning.front.wheelBrakingTorqueNm = 4000.0;                // Wheel_Braking_Torque F/R
            tuning.rear.wheelBrakingTorqueNm = 3500.0;
            tuning.dragCoefficient = 0.7; tuning.frontalArea = 1.0; tuning.airDensity = 1.3;
            tuning.liftCoefficient = -5.0;
            tuning.normalSpinDamping = 2.0; tuning.collisionSpinDamping = 6.0;  // 10.11.8 velocity damper
            tuning.collisionSpinThresholdRad = 4.0143;
            tuning.maximumSpeedMs = 37.998;                             // Maximum_Speed hard clamp (10.11.10)
        }
        sim = std::make_unique<sr3physics::VehicleSubstepSimulator>(tuning);
        // World-collision bounding sphere (THIS TASK) - see collisionRadius's
        // own doc comment above for the full citation; 0.6m CHOSEN overhang
        // margin for front/rear bumper beyond the wheelbase/track box.
        constexpr double kVehicleCollisionOverhangMarginM = 0.6;
        collisionRadius = 0.5 * std::sqrt(tuning.wheelBase * tuning.wheelBase + tuning.trackWidth * tuning.trackWidth) +
                          kVehicleCollisionOverhangMarginM;

        std::vector<uint8_t> vehArchive;
        if (!preloadedArchive) vehArchive = readFileLargeC(vehArchivePath);
        if (!preloadedArchive && vehArchive.empty()) { outDiag = "could not read " + vehArchivePath; return false; }
        try {
            vpp::Container vehContainer(preloadedArchive ? *preloadedArchive
                                                         : vpp::ByteView(vehArchive.data(), vehArchive.size()));
            std::string gcarName = cmeshName;
            {
                const size_t dot = gcarName.find_last_of('.');
                if (dot != std::string::npos && dot + 1 < gcarName.size()) gcarName[dot + 1] = 'g';
            }
            std::vector<uint8_t> ccarBytes, gcarBytes;
            if (!findEntry(vehContainer, cmeshName, ccarBytes, gcarBytes, gcarName) || gcarBytes.empty()) {
                outDiag = "could not find '" + cmeshName + "' (+ its paired '" + gcarName + "')";
                return false;
            }
            vehPtr = std::make_unique<sr3vehicle::Vehicle>(
                sr3vehicle::Vehicle::parse(vpp::ByteView(ccarBytes.data(), ccarBytes.size())));
            vpp::ByteView ccarView(ccarBytes.data(), ccarBytes.size());
            sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parseAt(ccarView, vehPtr->meshRegionOffset());
            if (!geo.hasMeshSubBlock()) { outDiag = cmeshName + ": vehicle geometry block has no Mesh sub-block"; return false; }
            meshPtr = std::make_unique<sr3mesh::MeshBlock>(sr3mesh::MeshBlock::parse(
                ccarView, geo.meshSubBlockOffset(), vpp::ByteView(gcarBytes.data(), gcarBytes.size())));

            // ---- per-channel upload, same v4 multi-channel fix as runDriveDemo() ----
            if (meshPtr->drawGroupsLocated() && !meshPtr->drawGroups().empty()) {
                const auto& group0 = meshPtr->drawGroups()[0];
                std::vector<uint32_t> distinctChannels;
                for (const auto& rg : group0)
                    if (std::find(distinctChannels.begin(), distinctChannels.end(), rg.submeshIndex) ==
                        distinctChannels.end())
                        distinctChannels.push_back(rg.submeshIndex);
                std::sort(distinctChannels.begin(), distinctChannels.end());
                for (uint32_t c32 : distinctChannels) {
                    const size_t c = static_cast<size_t>(c32);
                    if (c >= meshPtr->channels().size()) { outDiag = "group 0 range names an out-of-range channel"; return false; }
                    Channel ch;
                    ch.channelIndex = c;
                    ch.rangeMask.assign(group0.size(), false);
                    for (size_t r = 0; r < group0.size(); ++r)
                        if (group0[r].submeshIndex == c32) ch.rangeMask[r] = true;
                    ch.verts = meshPtr->decodeChannel(c);
                    channels.push_back(std::move(ch));
                }
            } else {
                Channel ch;
                ch.channelIndex = 0;
                ch.verts = meshPtr->decodeChannel(0);
                channels.push_back(std::move(ch));
            }
            for (auto& ch : channels) {
                ch.renderer = std::make_unique<sr3render::MeshRenderer>();
                const std::vector<bool>* maskPtr = ch.rangeMask.empty() ? nullptr : &ch.rangeMask;
                std::string uploadErr;
                if (!ch.renderer->initialise(device, uploadErr) ||
                    !ch.renderer->upload(device, *meshPtr, ch.channelIndex, 0, uploadErr, ch.drawGroup, nullptr, nullptr, maskPtr)) {
                    outDiag = "channel " + std::to_string(ch.channelIndex) + ": " + uploadErr;
                    return false;
                }
            }

            // ---- real per-material paint colour (same as runDriveDemo()) ----
            try {
                sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
                    ccarView, geo.offset(), geo.meshSubBlockOffset() + meshPtr->cLength());
                if (bindings.located() && bindings.materialCount() > 0) {
                    std::vector<VehiclePaintColourV> paint = resolveVehiclePaintColoursV(
                        ccarView, geo.offset(), geo.meshSubBlockOffset(), meshPtr->cLength(), bindings.materialCount());
                    perMaterialSrv.assign(bindings.materialCount(), nullptr);
                    for (uint32_t m = 0; m < bindings.materialCount(); ++m) {
                        ID3D11ShaderResourceView* srv = nullptr;
                        if (createSolidColorTextureV(device, paint[m].value, &srv)) perMaterialSrv[m] = srv;
                    }
                }
            } catch (const std::exception&) {
                perMaterialSrv.clear(); // honest fallback to Checker, same as runDriveDemo()
            }

            // ---- wheel identification + derived raw-vertex centroids (same as runDriveDemo()) ----
            partIsWheel.assign(vehPtr->parts().size(), false);
            partIsFrontWheel.assign(vehPtr->parts().size(), false);
            wheelRollAngleRad.assign(vehPtr->parts().size(), 0.0);
            wheelCentroid.assign(vehPtr->parts().size(), std::array<float, 3>{0.0f, 0.0f, 0.0f});
            for (size_t i = 0; i < vehPtr->parts().size(); ++i) {
                bool isFront = false;
                if (isVehicleWheelPartV(vehPtr->parts()[i], &isFront)) {
                    partIsWheel[i] = true;
                    partIsFrontWheel[i] = isFront;
                }
            }
            std::vector<std::array<double, 3>> centroidSum(vehPtr->parts().size(), std::array<double, 3>{0.0, 0.0, 0.0});
            std::vector<size_t> centroidCount(vehPtr->parts().size(), 0);
            for (const auto& ch : channels) {
                for (const auto& v : ch.verts) {
                    if (v.rigidPartIndex < partIsWheel.size() && partIsWheel[v.rigidPartIndex]) {
                        centroidSum[v.rigidPartIndex][0] += static_cast<double>(v.position[0]);
                        centroidSum[v.rigidPartIndex][1] += static_cast<double>(v.position[1]);
                        centroidSum[v.rigidPartIndex][2] += static_cast<double>(v.position[2]);
                        ++centroidCount[v.rigidPartIndex];
                    }
                }
            }
            for (size_t i = 0; i < vehPtr->parts().size(); ++i) {
                if (centroidCount[i] == 0) continue;
                const double n = static_cast<double>(centroidCount[i]);
                wheelCentroid[i] = {static_cast<float>(centroidSum[i][0] / n), static_cast<float>(centroidSum[i][1] / n),
                                    static_cast<float>(centroidSum[i][2] / n)};
            }

            // ---- base positions: THE WHEEL-PLACEMENT FIX - every vertex (wheel
            // or not) renders at its RAW decoded position; wheel vertices get
            // ONLY a pivoted delta on top (PoseAndUpload() below), never a
            // composed per-part baseline transform - see this struct's own top
            // doc comment for the measured proof this ports from runDriveDemo(). ----
            for (auto& ch : channels) {
                ch.basePositions.resize(ch.verts.size());
                for (size_t vi = 0; vi < ch.verts.size(); ++vi) ch.basePositions[vi] = ch.verts[vi].position;
            }
        } catch (const std::exception& ex) {
            outDiag = std::string("vehicle archive error: ") + ex.what();
            return false;
        }
        if (channels.empty()) { outDiag = "no channel target built for draw group 0"; return false; }

        if (!wheelsArchivePath.empty()) LoadWheelCatalog(device, wheelsArchivePath);

        // ---- primary wheel slot anchors (same as runDriveDemo(), see that
        // function's own doc comment above PrimaryWheelSlotV for the full
        // citation) - used only to place the externally-attached catalogue
        // wheel mesh, when one was resolved. ----
        for (size_t i = 0; i < vehPtr->parts().size(); ++i) {
            if (!isPrimaryVehicleWheelPartV(vehPtr->parts()[i])) continue;
            WheelSlot slot;
            slot.partIndex = i;
            slot.anchor = {vehPtr->parts()[i].translationX(), vehPtr->parts()[i].translationY(),
                           vehPtr->parts()[i].translationZ()};
            slot.isFront = partIsFrontWheel[i];
            slot.sign = (slot.anchor[0] >= 0.0f) ? 1.0f : -1.0f;
            slot.simWheelIdx = (slot.isFront ? 0 : 2) + (slot.sign > 0.0f ? 1 : 0);
            wheelSlots.push_back(slot);
        }
        wheelAttachWorld.resize(wheelSlots.size());
        ready = true;
        return true;
    }

    // Loads the real shared wheel/rim catalogue (spec-vehicle-geometry.md
    // §12.1) and resolves the SAME CHOSEN placeholder style runDriveDemo()
    // uses ("R100", the first real rim token in the catalogue's own file
    // order - see that function's own doc comment for the full citation of
    // why this is a labelled placeholder, not a confirmed equipped style).
    // Any failure along this path is reported via stderr and leaves
    // catalogWheelReady==false - the vehicle body still renders without
    // visible rim geometry, honestly, rather than fabricating one.
    void LoadWheelCatalog(ID3D11Device* device, const std::string& wheelsArchivePath) {
        std::vector<uint8_t> wheelsArchive = readFileLargeC(wheelsArchivePath);
        if (wheelsArchive.empty()) {
            std::fprintf(stderr, "[playtest-vehicle] could not read %s - wheels will not be attached\n",
                         wheelsArchivePath.c_str());
            return;
        }
        try {
            vpp::Container wheelsContainer(vpp::ByteView(wheelsArchive.data(), wheelsArchive.size()));
            std::vector<uint8_t> catBytes, catGBytes;
            if (!findEntry(wheelsContainer, "externalized_components.cvcomp_pc", catBytes, catGBytes,
                           "externalized_components.gvcomp_pc") ||
                catBytes.empty()) {
                std::fprintf(stderr, "[playtest-vehicle] externalized_components.cvcomp_pc not found in %s\n",
                             wheelsArchivePath.c_str());
                return;
            }
            std::vector<ExternalizedCatalogPartV> catParts;
            size_t catMeshRegionOffset = 0;
            uint32_t catHeaderTag = 0;
            std::string diag;
            vpp::ByteView catView(catBytes.data(), catBytes.size());
            if (!loadExternalizedWheelCatalogV(catView, catParts, catMeshRegionOffset, catHeaderTag, diag)) {
                std::fprintf(stderr, "[playtest-vehicle] catalogue scan failed: %s\n", diag.c_str());
                return;
            }
            const std::string kChosenWheelStyleNameV = "R100"; // same CHOSEN placeholder as runDriveDemo()
            const int styleIdx = findExternalizedCatalogPartV(catParts, kChosenWheelStyleNameV);
            if (styleIdx < 0) {
                std::fprintf(stderr, "[playtest-vehicle] style '%s' not found among %zu scanned part(s)\n",
                             kChosenWheelStyleNameV.c_str(), catParts.size());
                return;
            }
            sr3geometry::GeometryBlock catGeo = sr3geometry::GeometryBlock::parseAt(catView, catMeshRegionOffset);
            if (!catGeo.hasMeshSubBlock()) {
                std::fprintf(stderr, "[playtest-vehicle] catalogue mesh region has no Mesh sub-block\n");
                return;
            }
            sr3mesh::MeshBlock catMesh = sr3mesh::MeshBlock::parse(catView, catGeo.meshSubBlockOffset(),
                                                                   vpp::ByteView(catGBytes.data(), catGBytes.size()));
            CatalogWheelMeshV catWheelMesh;
            std::string diag2;
            uint32_t geometryPartIdx = static_cast<uint32_t>(styleIdx);
            if (!resolveCatalogWheelMeshV(catMesh, geometryPartIdx, catWheelMesh, diag2)) {
                // Same CHOSEN fallback as runDriveDemo(): the named style owns
                // no geometry of its own (measured finding, see
                // loadExternalizedWheelCatalogV()'s own doc comment) - fall
                // back to the catalogue's shared base shape (part index 0).
                geometryPartIdx = 0;
                diag2.clear();
                resolveCatalogWheelMeshV(catMesh, geometryPartIdx, catWheelMesh, diag2);
            }
            if (catWheelMesh.rangeMask.empty()) {
                std::fprintf(stderr, "[playtest-vehicle] could not resolve any wheel geometry: %s\n", diag2.c_str());
                return;
            }
            catalogWheelRenderer = std::make_unique<sr3render::MeshRenderer>();
            std::string uploadErr;
            if (!catalogWheelRenderer->initialise(device, uploadErr) ||
                !catalogWheelRenderer->upload(device, catMesh, catWheelMesh.channelIndex, 0, uploadErr, 0, nullptr,
                                              nullptr, &catWheelMesh.rangeMask)) {
                std::fprintf(stderr, "[playtest-vehicle] rim GPU upload failed: %s\n", uploadErr.c_str());
                catalogWheelRenderer.reset();
                return;
            }
            catalogWheelPivot = catWheelMesh.centroid;
            const float measuredHalfHeight = 0.5f * (catWheelMesh.boundsMax[1] - catWheelMesh.boundsMin[1]);
            catalogWheelScale =
                (measuredHalfHeight > 1e-4f) ? static_cast<float>(tuning.wheelRadius) / measuredHalfHeight : 1.0f;
            catalogWheelReady = true;
            std::printf("[playtest-vehicle] rim catalogue attached (style '%s' -> part %u), pivot (%.3f %.3f %.3f), "
                        "scale %.3f\n",
                        kChosenWheelStyleNameV.c_str(), geometryPartIdx, static_cast<double>(catalogWheelPivot[0]),
                        static_cast<double>(catalogWheelPivot[1]), static_cast<double>(catalogWheelPivot[2]),
                        static_cast<double>(catalogWheelScale));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "[playtest-vehicle] wheels archive error: %s - wheels will not be attached\n",
                         ex.what());
        }
    }

    // Advances the real physics sim one step (sr3physics::VehicleSubstepSimulator,
    // the SAME 13 confirmed Havok vehicle sub-steps runDriveDemo() drives),
    // then (THIS TASK, 2026-10-08, spec-physics-runtime.md §10a) tests the
    // resulting candidate position against REAL `.clmesh_pc` world collision
    // geometry and applies a plain physics-impulse response before re-posing/
    // re-uploading the mesh. Call only while actually driving (playtest's own
    // `inVehicle` flag) - a parked vehicle simply keeps whatever pose
    // PoseAndUpload(device, 0.0) last uploaded.
    //
    // `worldCollision`/`worldY`: the SAME real collision-triangle source and
    // query primitive the player's own on-foot collision (CharacterProxyDriver
    // -> ClmeshTriangleCollisionWorld) and the camera's own collision
    // (RealCameraCollision, tools/sr3_viewer.cpp, just above runWalkDemo())
    // already query - reused here via RealCameraCollision's own public
    // sweepSphere() (sr3player::CameraCollision interface), NOT a second
    // triangle decode and NOT a new shape test invented for vehicles (this
    // task's own brief: "this project's existing player-collision code
    // already has a swept-sphere primitive, reuse it"). `worldCollision` may
    // be nullptr (no collision world wired by the caller) - Advance() then
    // behaves exactly as before this task, unchanged.
    //
    // CHOSEN SIMPLIFICATION, stated plainly (see spec-physics-runtime.md
    // §10a's own "deferred contact-event queue" finding for the real-engine
    // architecture this intentionally does NOT reproduce): the real engine
    // stages Havok contact events into a 300-record queue drained once per
    // frame on a SEPARATE main thread from the Havok physics-step thread that
    // detected them - architecture for the real engine's multi-threaded
    // Havok step. This project's playtest loop is single-threaded and
    // synchronous (one Step() call IS this harness's own "physics substep"
    // granularity - there are no finer sub-steps a caller outside
    // VehicleSubstepSimulator can observe), so detecting and responding to
    // the contact immediately, in the same call, is a faithful, honestly-
    // scoped SIMPLIFICATION of the real deferred-queue mechanism (collapsing
    // "detect now, apply next frame" into "detect and apply now"), not a
    // silent omission of it.
    void Advance(ID3D11Device* device, double dt, const sr3physics::DriverInput& input,
                 sr3player::CameraCollision* worldCollision = nullptr, double worldY = 0.0) {
        if (!ready || !sim) return;
        const double prevX = sim->state_.posX;
        const double prevZ = sim->state_.posZ;
        const double usedDt = sim->Step(dt, input);
        if (worldCollision) ApplyWorldCollisionResponse(*worldCollision, prevX, prevZ, worldY);
        PoseAndUpload(device, usedDt);
    }

    // The collision-RESPONSE half of Advance() above, split out so it is its
    // own independently-readable unit. Sweeps collisionRadius's own bounding
    // sphere from the chassis's position BEFORE this Step() call to its
    // candidate position AFTER it (both at the caller-supplied `worldY` -
    // this model is PLANAR, vehicle_substeps.h's own "no vertical chassis
    // motion" scope note, so there is only ever one world Y to test at). On a
    // hit:
    //   1. Snap the chassis position to the sweep's own contact position
    //      (already the last non-penetrating point along the attempted move,
    //      by construction of SweptSphereVsTriangle3D) - "keep the position
    //      from penetrating the surface", per this task's own brief.
    //   2. Apply a plain physics impulse (spec-physics-runtime.md §10a:
    //      "There is no separate Volition-authored impulse/restitution
    //      formula at the game layer for the generic case - Havok's own
    //      solver does it directly" - CHOSEN concrete shape matching that
    //      finding: zero the WORLD-space velocity component pointing INTO the
    //      contact normal, leave the tangential component untouched). A
    //      head-on hit (velocity ~antiparallel to the normal) stops the
    //      vehicle almost dead; a glancing hit (velocity mostly tangential)
    //      removes only the small inward sliver and the vehicle keeps
    //      sliding along the surface - the believable "stop/deflect" shape
    //      this task's own brief asks for, not a bespoke bounce/restitution
    //      formula. The contact normal's own Y component (if any - a sloped
    //      triangle) is deliberately dropped: this model has no vertical
    //      velocity state to apply it to (same PLANAR SCOPE boundary as
    //      posY above), so only the normal's XZ projection is used, CHOSEN
    //      and flagged here rather than silently ignored.
    // Yaw rate is deliberately left untouched - a real rotational collision
    // response needs a contact-point lever arm/angular-impulse formula §10a
    // does not give (its own "no separate Volition-authored formula" finding
    // covers the LINEAR impulse only); inventing one would be exactly the
    // "no invented fixes" violation this project's own standing rule forbids.
    //
    // The actual math lives in include/sr3physics/vehicle_world_collision.h's
    // ApplyVehicleWorldCollisionResponse() (src/physics_vehicle_world_
    // collision.cpp) - a small, D3D11/CameraCollision-FREE library function,
    // independently unit-tested against a synthetic flat-wall double
    // (tests/synthetic_physics_vehicle_world_collision_test.cpp). This method
    // is the thin real-engine glue: wraps `worldCollision` as an
    // sr3physics::IVehicleWorldCollisionQuery (RealCameraCollisionAsVehicleWorld,
    // just above runWalkDemo() in this file) and copies sim->state_'s
    // relevant fields in and back out.
    void ApplyWorldCollisionResponse(sr3player::CameraCollision& worldCollision, double prevX, double prevZ,
                                      double worldY) {
        if (!sim) return;
        RealCameraCollisionAsVehicleWorld adapter(worldCollision);
        sr3physics::VehicleWorldCollisionState state;
        state.posX = sim->state_.posX;
        state.posZ = sim->state_.posZ;
        state.heading = sim->state_.heading;
        state.velForwardBody = sim->state_.velForwardBody;
        state.velRightBody = sim->state_.velRightBody;
        const double candidateX = state.posX, candidateZ = state.posZ;
        sr3physics::ApplyVehicleWorldCollisionResponse(adapter, prevX, prevZ, worldY, collisionRadius, state);
        // Same diagnostic shape as the character proxy's/camera's own
        // "[collision]"/"[camera-collision] frame-local ... hit" prints
        // (this file, above) - fires only when a real contact actually
        // moved the candidate position (i.e. SweepSphere found a hit),
        // whether or not the velocity was also clamped (see
        // ApplyVehicleWorldCollisionResponse()'s own "into >= 0" no-clamp
        // branch - position still moves to the contact point either way).
        if (std::abs(state.posX - candidateX) > 1e-9 || std::abs(state.posZ - candidateZ) > 1e-9) {
            std::printf("[vehicle-collision] frame-local hit: candidate (%.2f %.2f) clamped to (%.2f %.2f), speed "
                        "%.2f -> %.2f m/s\n",
                        candidateX, candidateZ, state.posX, state.posZ,
                        std::sqrt(sim->state_.velForwardBody * sim->state_.velForwardBody +
                                  sim->state_.velRightBody * sim->state_.velRightBody),
                        std::sqrt(state.velForwardBody * state.velForwardBody + state.velRightBody * state.velRightBody));
        }
        sim->state_.posX = state.posX;
        sim->state_.posZ = state.posZ;
        sim->state_.velForwardBody = state.velForwardBody;
        sim->state_.velRightBody = state.velRightBody;
    }

    // Re-poses every wheel part (roll + steer, kinematic approximation -
    // see buildWheelDeltaTransformV()'s own doc comment) from the sim's
    // CURRENT state and re-uploads the mesh. `usedDt` drives both the wheel
    // roll accumulator (omega = v/wheelRadius) and the suspension ease -
    // pass 0.0 for a one-time STATIC pose (e.g. immediately after Load(),
    // so a parked vehicle has a sane resting pose before it is ever driven).
    void PoseAndUpload(ID3D11Device* device, double usedDt) {
        if (!ready) return;
        std::vector<std::array<float, 16>> wheelDelta(vehPtr->parts().size());
        for (size_t i = 0; i < vehPtr->parts().size(); ++i) {
            if (!partIsWheel[i]) continue;
            wheelRollAngleRad[i] += (sim->state_.velForwardBody / tuning.wheelRadius) * usedDt;
            const float steerAngle =
                partIsFrontWheel[i] ? static_cast<float>(sim->state_.steeringAngleCurrentRad) : 0.0f;
            buildWheelDeltaTransformV(wheelCentroid[i].data(), static_cast<float>(wheelRollAngleRad[i]), steerAngle,
                                      wheelDelta[i].data());
        }
        for (auto& ch : channels) {
            std::vector<std::array<float, 3>> overridePositions(ch.verts.size());
            for (size_t vi = 0; vi < ch.verts.size(); ++vi) {
                const auto& v = ch.verts[vi];
                if (v.rigidPartIndex < partIsWheel.size() && partIsWheel[v.rigidPartIndex]) {
                    transformPointRowVectorV(v.position.data(), wheelDelta[v.rigidPartIndex].data(),
                                             overridePositions[vi].data());
                } else {
                    overridePositions[vi] = ch.basePositions[vi]; // the wheel-placement fix - raw position
                }
            }
            std::string uploadErr;
            const std::vector<bool>* maskPtr = ch.rangeMask.empty() ? nullptr : &ch.rangeMask;
            if (!ch.renderer->upload(device, *meshPtr, ch.channelIndex, 0, uploadErr, ch.drawGroup, &overridePositions, nullptr,
                                     maskPtr)) {
                std::fprintf(stderr, "[playtest-vehicle] re-upload (channel %zu) failed: %s\n", ch.channelIndex,
                             uploadErr.c_str());
            }
        }

        // Attached catalogue wheel world transforms (vehicle-LOCAL - composed
        // with the caller's current world matrix at Draw() time) - same
        // suspension-ease formula as runDriveDemo(), see that function's own
        // doc comment above primaryWheelSlots for the full citation.
        for (size_t s = 0; s < wheelSlots.size(); ++s) {
            const WheelSlot& slot = wheelSlots[s];
            const int wk = slot.simWheelIdx;
            if (suspensionReferenceForce[wk] < 0.0) suspensionReferenceForce[wk] = sim->state_.wheels[wk].normalForce;
            constexpr double kSuspensionComplianceMPerN = 0.00003; // CHOSEN (m/N), same as runDriveDemo()
            constexpr double kSuspensionMaxTravelM = 0.03;         // CHOSEN (m), +/-3cm, same as runDriveDemo()
            const double targetOffsetY = (std::max)(
                -kSuspensionMaxTravelM,
                (std::min)(kSuspensionMaxTravelM, (sim->state_.wheels[wk].normalForce - suspensionReferenceForce[wk]) *
                                                       kSuspensionComplianceMPerN));
            const double easeAlpha =
                (std::max)(0.0, (std::min)(1.0, 0.05 * (usedDt / sr3physics::kSuspensionMaxMicroStepSec)));
            suspensionSmoothedOffsetY[wk] += (targetOffsetY - suspensionSmoothedOffsetY[wk]) * easeAlpha;
            const float fullSteerAngle = slot.isFront
                                             ? static_cast<float>(-slot.sign * kWheelVisualSteerScaleV *
                                                                   sim->state_.steeringAngleCurrentRad)
                                             : 0.0f;
            const float anchorWithSuspension[3] = {slot.anchor[0],
                                                   slot.anchor[1] + static_cast<float>(suspensionSmoothedOffsetY[wk]),
                                                   slot.anchor[2]};
            buildWheelAttachTransformV(catalogWheelPivot.data(), anchorWithSuspension,
                                       static_cast<float>(wheelRollAngleRad[slot.partIndex]), fullSteerAngle,
                                       catalogWheelScale, wheelAttachWorld[s].data());
        }
    }

    // AW-15: helicopter / airplane meshes keep their parts in many draw groups (heli_standard_02: 78, plane_standard01: 81)
    // where group 0 is a few triangles only, so the group-0-only load above draws nothing visible for them (cars have the
    // whole body in group 0). Groups are a part x LOD grid (spec-vehicle-geometry.md Sec11: LOD levels, group index mod 3
    // for high16); the exact grid is not specified for aircraft. CHOSEN: draw the head of every LOD chain, where a chain
    // starts at the first group and at every group whose index count exceeds the previous group's (LODs shrink). Replaces
    // `channels` (group 0 stays for cars; call this only for craft models). Returns the number of groups drawn.
    size_t RebuildLodHeadChannels(ID3D11Device* device) {
        if (!ready || !meshPtr || !meshPtr->drawGroupsLocated()) return 0;
        const auto& groups = meshPtr->drawGroups();
        std::vector<size_t> heads;
        uint64_t prevTotal = 0;
        for (size_t g = 0; g < groups.size(); ++g) {
            uint64_t total = 0;
            for (const auto& rg : groups[g]) total += rg.indexCount;
            if (g == 0 || total > prevTotal) heads.push_back(g);
            prevTotal = total;
        }
        std::map<size_t, std::vector<sr3mesh::Vertex>> decoded;
        std::vector<Channel> rebuilt;
        for (size_t g : heads) {
            std::vector<uint32_t> distinct;
            for (const auto& rg : groups[g])
                if (std::find(distinct.begin(), distinct.end(), rg.submeshIndex) == distinct.end()) distinct.push_back(rg.submeshIndex);
            for (uint32_t c32 : distinct) {
                const size_t c = static_cast<size_t>(c32);
                if (c >= meshPtr->channels().size()) continue;
                Channel ch;
                ch.channelIndex = c;
                ch.drawGroup = g;
                ch.rangeMask.assign(groups[g].size(), false);
                for (size_t r = 0; r < groups[g].size(); ++r) ch.rangeMask[r] = groups[g][r].submeshIndex == c32;
                auto it = decoded.find(c);
                if (it == decoded.end()) it = decoded.emplace(c, meshPtr->decodeChannel(c)).first;
                ch.verts = it->second;
                ch.basePositions.resize(ch.verts.size());
                for (size_t vi = 0; vi < ch.verts.size(); ++vi) ch.basePositions[vi] = ch.verts[vi].position;
                ch.renderer = std::make_unique<sr3render::MeshRenderer>();
                std::string err;
                if (!ch.renderer->initialise(device, err) ||
                    !ch.renderer->upload(device, *meshPtr, c, 0, err, g, nullptr, nullptr, &ch.rangeMask)) {
                    std::fprintf(stderr, "[playtest-vehicle] LOD-head group %zu channel %zu upload failed: %s\n", g, c, err.c_str());
                    continue;
                }
                rebuilt.push_back(std::move(ch));
            }
        }
        if (rebuilt.empty()) return 0;
        channels = std::move(rebuilt);
        return heads.size();
    }

    // Draws the vehicle body (real per-material paint when resolved, else
    // Checker) and, when attached, the rim catalogue mesh (CHOSEN flat tire
    // tint - see runDriveDemo()'s own identical draw call for why) at
    // `vehicleWorld` (the caller's current placement, built from
    // sim->state_.posX/posZ/heading every frame - see runPlaytest()'s own
    // call site).
    // `sunDirection`/`ambientColor`/`sunColor` (this task, 2026-10-08, sky/
    // lighting/TOD): forwarded unchanged to MeshRenderer::draw()/
    // drawTextured() - see that function's own comment. All three default
    // to null, which those functions' own old-behaviour defaults reproduce
    // byte-for-byte - runDriveDemo()'s own pre-existing call site (below)
    // passes neither, so its frozen drive_demo_coverage goldens are
    // unaffected; only runPlaytest() passes real values.
    // `lightViewProjection`/`shadowMap`/`shadowSampler` (this task, 2026-10-09,
    // sun-shadow-map): forwarded unchanged to MeshRenderer::draw()/
    // drawTextured() - same byte-for-byte-by-default contract as
    // sunDirection/ambientColor/sunColor above (every pre-existing caller
    // passes none of these six trailing parameters).
    void Draw(ID3D11DeviceContext* ctx, const float viewProjection[16], const float vehicleWorld[16],
             const float sunDirection[3] = nullptr, const float ambientColor[3] = nullptr,
             const float sunColor[3] = nullptr, const float lightViewProjection[16] = nullptr,
             ID3D11ShaderResourceView* shadowMap = nullptr, ID3D11SamplerState* shadowSampler = nullptr) {
        if (!ready) return;
        for (auto& ch : channels) {
            if (!perMaterialSrv.empty())
                ch.renderer->drawTextured(ctx, viewProjection, perMaterialSrv, vehicleWorld, sunDirection,
                                          ambientColor, sunColor, lightViewProjection, shadowMap, shadowSampler);
            else
                ch.renderer->draw(ctx, viewProjection, sr3render::MeshDrawMode::Checker, nullptr, vehicleWorld,
                                  nullptr, sunDirection, ambientColor, sunColor, lightViewProjection, shadowMap,
                                  shadowSampler);
        }
        if (catalogWheelReady) {
            const float kTireTint[4] = {0.12f, 0.12f, 0.13f, 1.0f}; // CHOSEN dark neutral tire/rim grey, same as runDriveDemo()
            for (const auto& w : wheelAttachWorld) {
                float composed[16];
                sr3render::multiplyMatrix4x4(w.data(), vehicleWorld, composed);
                catalogWheelRenderer->draw(ctx, viewProjection, sr3render::MeshDrawMode::FlatTint, nullptr, composed,
                                           kTireTint, sunDirection, ambientColor, sunColor, lightViewProjection,
                                           shadowMap, shadowSampler);
            }
        }
    }

    // Sun shadow map depth-only pass (THIS TASK, 2026-10-09, sun-shadow-map):
    // the SAME body channels + attached wheels as Draw() above, mirrored 1:1
    // but through MeshRenderer::drawDepthOnly() (lit/texture/material
    // irrelevant - depth only) and the sun's own light-space matrix instead
    // of the camera's. Called from runPlaytest()'s own shadow-generation
    // block, before Draw() runs for the same frame.
    void DrawDepthOnly(ID3D11DeviceContext* ctx, const float lightViewProjection[16],
                       const float vehicleWorld[16]) {
        if (!ready) return;
        for (auto& ch : channels) ch.renderer->drawDepthOnly(ctx, lightViewProjection, vehicleWorld);
        if (catalogWheelReady) {
            for (const auto& w : wheelAttachWorld) {
                float composed[16];
                sr3render::multiplyMatrix4x4(w.data(), vehicleWorld, composed);
                catalogWheelRenderer->drawDepthOnly(ctx, lightViewProjection, composed);
            }
        }
    }
};

} // namespace sr3viewer
