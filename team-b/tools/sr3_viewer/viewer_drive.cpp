#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_city_support.h"
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
#include "vpp/container.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"
#include "sr3vehicle/cvtf.h"

using namespace sr3viewer;



int runDriveDemo(int argc, char** argv) {
    std::string archivePath =
        "D:\\Project Crreish\\Saints Row 3 CRREISH\\packfiles\\pc\\cache\\dlc1.vpp_pc"; // CHOSEN convenience
                                                                                        // default - same
                                                                                        // archive/ccar pairing
                                                                                        // golden_scene_check.cpp's
                                                                                        // checkVehicleGenkiScene()
                                                                                        // already uses
    std::string cmeshName = "car_4dr_genki_0.ccar_pc";
    // CHOSEN convenience default (this task): same dataRoot cache directory
    // as archivePath above, matching the real archive city_corner's own
    // frozen golden (tests/golden/city_corner) already loads from.
    std::string cityArchivePath =
        "D:\\Project Crreish\\Saints Row 3 CRREISH\\packfiles\\pc\\cache\\sr3_city_0.vpp_pc";
    bool loadCity = true; // --no-city opts out (fast iteration on camera/wheels; see usage text)
    // CHOSEN convenience default (this task, §12.1): same dataRoot cache
    // directory as archivePath/cityArchivePath above - vehicles_preload.vpp_pc
    // is where the shared wheel/rim catalogue (externalized_components.
    // cvcomp_pc/.gvcomp_pc) lives, NOT per-vehicle, see
    // loadExternalizedWheelCatalogV()'s own doc comment.
    std::string wheelsArchivePath =
        "D:\\Project Crreish\\Saints Row 3 CRREISH\\packfiles\\pc\\cache\\vehicles_preload.vpp_pc";
    bool loadWheelCatalog = true; // --no-wheels opts out (fast iteration; same shape as --no-city)

    int argi = 2;
    // Positional override: either BOTH <archive> and <name.ccar_pc> are given
    // (neither starting with "--"), or neither - matching this file's own
    // "override both or take the default pair" convenience shape.
    if (argi < argc && std::strncmp(argv[argi], "--", 2) != 0) {
        if (argi + 1 >= argc || std::strncmp(argv[argi + 1], "--", 2) == 0) {
            std::fprintf(stderr,
                         "usage: sr3_viewer drive_demo [<archive.vpp_pc> <name.ccar_pc>] "
                         "[--visible]\n"
                         "                             [--size W H] [--frames N] [--capture <out.png>]\n"
                         "                             [--capture-frame N] [--capture-frames "
                         "F0,F1,...]\n"
                         "                             [--city <sr3_city_0.vpp_pc>] [--no-city]\n"
                         "                             [--wheels <vehicles_preload.vpp_pc>] [--no-wheels]\n"
                         "       (give BOTH <archive> and <name.ccar_pc>, or neither - defaults to the\n"
                         "        real dlc1.vpp_pc / car_4dr_genki_0.ccar_pc pair, same archive/ccar\n"
                         "        pairing golden_scene_check.cpp's checkVehicleGenkiScene() uses)\n"
                         "       --city overrides the real city_corner archive the vehicle drives on\n"
                         "        (CHOSEN default: the same sr3_city_0.vpp_pc/location city_corner's own\n"
                         "        frozen golden uses); --no-city skips loading it entirely (void scene,\n"
                         "        v1 behaviour) for fast iteration on the camera/wheel changes.\n"
                         "       --wheels overrides the real shared wheel/rim catalogue archive\n"
                         "        (CHOSEN default: vehicles_preload.vpp_pc, spec-vehicle-geometry.md\n"
                         "        §12.1); --no-wheels skips attaching a resolved wheel mesh entirely.\n");
            return 1;
        }
        archivePath = argv[argi++];
        cmeshName = argv[argi++];
    }

    bool visible = false; // off by default - see sr3render/window.h's own doc comment
    uint32_t winWidth = 1280, winHeight = 720;
    // Scripted sequence (CHOSEN, documented): 90 frames straight (1.5 s),
    // 90 frames turning (1.5 s), 60 frames braking (1.0 s) at dt = 1/60 s -
    // 240 frames total, matching this file's own convention of `--frames`
    // defaulting to the scripted sequence's own total (see runWalkDemo()'s
    // own maxFrames default/comment).
    const int kStraightFrames = 90;
    const int kTurnFrames = 90;
    const int kBrakeFrames = 60;
    int maxFrames = kStraightFrames + kTurnFrames + kBrakeFrames; // 240
    std::string capturePath;
    int captureFrame = maxFrames - 1; // last frame, matching this file's own "--capture-frame alone" convention
    // Default --capture-frames (CHOSEN): one frame near the end of each
    // phase, so a 3-PNG sequence shows all three phases - overridable via
    // --capture-frames.
    std::vector<int> captureFrames = {kStraightFrames - 1, kStraightFrames + kTurnFrames - 1, maxFrames - 1};

    for (; argi < argc; ++argi) {
        const std::string arg = argv[argi];
        if (arg == "--visible") {
            visible = true;
        } else if (arg == "--size" && argi + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++argi]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++argi]));
        } else if (arg == "--frames" && argi + 1 < argc) {
            maxFrames = std::atoi(argv[++argi]);
        } else if (arg == "--capture" && argi + 1 < argc) {
            capturePath = argv[++argi];
        } else if (arg == "--capture-frame" && argi + 1 < argc) {
            captureFrame = std::atoi(argv[++argi]);
        } else if (arg == "--capture-frames" && argi + 1 < argc) {
            captureFrames.clear();
            const std::string csv = argv[++argi];
            size_t pos = 0;
            while (pos < csv.size()) {
                size_t comma = csv.find(',', pos);
                const std::string tok = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) captureFrames.push_back(std::atoi(tok.c_str()));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (arg == "--city" && argi + 1 < argc) {
            cityArchivePath = argv[++argi];
        } else if (arg == "--no-city") {
            loadCity = false;
        } else if (arg == "--wheels" && argi + 1 < argc) {
            wheelsArchivePath = argv[++argi];
        } else if (arg == "--no-wheels") {
            loadWheelCatalog = false;
        } else {
            std::fprintf(stderr,
                         "usage: sr3_viewer drive_demo [<archive.vpp_pc> <name.ccar_pc>] "
                         "[--visible]\n"
                         "                             [--size W H] [--frames N] [--capture <out.png>]\n"
                         "                             [--capture-frame N] [--capture-frames "
                         "F0,F1,...]\n"
                         "                             [--city <sr3_city_0.vpp_pc>] [--no-city]\n"
                         "                             [--wheels <vehicles_preload.vpp_pc>] [--no-wheels]\n"
                         "unrecognised argument '%s'\n",
                         arg.c_str());
            return 2;
        }
    }

    std::printf("=== drive_demo: vehicle physics demo (sr3physics::VehicleSubstepSimulator) ===\n");
    std::printf("REAL   : a real vehicle mesh (%s :: %s), driven every frame through\n"
                "         sr3physics::VehicleSubstepSimulator::Step() - the real 13 confirmed Havok\n"
                "         vehicle sub-steps (spec-physics-runtime.md Sec6) plus that simulator's own\n"
                "         chassis integration; state_.posX/posZ/heading/velForwardBody/yawRate/\n"
                "         steeringAngleCurrentRad below are that simulator's own real output, never\n"
                "         hand-animated. Every material's real Base_Paint_Color constant (not a\n"
                "         texture - see material_binding.h's diffuse() doc comment) and, when\n"
                "         present, each wheel part's real sr3vehicle::partTypeName() classification\n"
                "         are likewise read from this vehicle's own real .ccar_pc bytes.\n",
                archivePath.c_str(), cmeshName.c_str());
    std::printf("CHOSEN : representative 'normal sedan' VehicleTuning fixture (same fixture\n"
                "         tests/synthetic_physics_vehicle_substeps_test.cpp's own BuildTestVehicleTuning()\n"
                "         uses - NOT real car_4dr_genki xtbl data, this clean-room project has no access\n"
                "         to that); scripted 3-phase input (straight %d frames / turn %d frames / brake\n"
                "         %d frames @ dt=1/60s); a SIMPLIFIED trailing chase camera (spec-player-runtime.md\n"
                "         Sec4.7 step13/Sec4.10 row-identities only - no submode/blend/state-machine, see\n"
                "         this function's own doc comment); wheel spin/steer is a kinematic\n"
                "         approximation, not a confirmed real formula (see\n"
                "         buildWheelDeltaTransformV()'s own doc comment).\n",
                kStraightFrames, kTurnFrames, kBrakeFrames);

    // ---- Vehicle TUNING (CHOSEN fixture, real reader path) --------------------
    sr3physics::VehicleTuning tuning = BuildDriveDemoVehicleTuning();
    sr3physics::VehicleSubstepSimulator sim(tuning);

    // ---- Vehicle MESH (real) ---------------------------------------------------
    std::vector<uint8_t> vehArchive = readFileLargeC(archivePath);
    if (vehArchive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }

    std::string error;
    sr3render::Window window;
    if (!window.create("SR3 Viewer - drive_demo", winWidth, winHeight, error, visible)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }
    sr3render::RenderDevice device;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());
    sr3render::SwapChain swapChain;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }

    // ---- Vehicle mesh/parts/material load (inline, real data path) ------------
    // Loaded inline here (rather than via loadVehicleObject(), which `scene`'s
    // vehicle slot still uses byte-for-byte unchanged) because this command
    // additionally needs: (a) sr3vehicle::Vehicle::parts() - the real per-part
    // rigid transforms wheel spin/steer composes with (improvement 3), and
    // (b) sr3geometry::MaterialBindings + the real array-B/C paint constant -
    // for the real per-material texture (improvement 1). loadVehicleObject()
    // itself exposes neither.
    PlacedObject vehicleObj;
    std::unique_ptr<sr3vehicle::Vehicle> vehPtr;
    std::unique_ptr<sr3mesh::MeshBlock> meshPtr;
    std::vector<ID3D11ShaderResourceView*> vehiclePerMaterialSrv; // empty = fall back to Checker (bindings unresolved)
    // BUG FIX (2026-10-08 v4) - see the computation below, right after
    // meshPtr is parsed, for the full story. v3 (merged, then found
    // incomplete - see HANDOFF.md) masked this command's single upload()/
    // draw() call down to draw group 0's genuinely-channel-0 ranges (16/20),
    // which stopped the index-buffer corruption (the real shattered-body
    // root cause: a channel/range mismatch) but ALSO silently dropped the
    // other 4 ranges' real geometry (submeshIndex 1/2 - the middle-body
    // panels), since this command still only ever uploaded channel 0's
    // vertex data. v4 fixes THAT: one MeshRenderer PER CHANNEL present in
    // group 0 (not just channel 0), each fed its own decodeChannel(c) and
    // its own rangeMask restricted to that channel's own ranges - this
    // file's own already-established MultiChannelMultiSetRender pattern
    // (see that class's doc comment above), adapted here without its
    // bone-palette-set duplicate/remap machinery (irrelevant - this
    // command never skins through bone palettes; wheel parts are posed by
    // a per-vertex rigidPartIndex delta transform, computed below,
    // identically regardless of which channel a vertex decodes from).
    // vehicleChannels is declared out here (not inside the try{} below) so
    // the per-frame re-upload loop, further down and outside that try{},
    // can reuse the SAME per-channel renderers/masks every frame.
    struct VehicleChannelRender {
        size_t channelIndex = 0;
        std::unique_ptr<sr3render::MeshRenderer> renderer;
        std::vector<sr3mesh::Vertex> verts; // decodeChannel(channelIndex)'s raw output, this channel's own
        std::vector<bool> rangeMask;        // group 0's ranges that belong to this channel
    };
    std::vector<VehicleChannelRender> vehicleChannels;
    try {
        vpp::Container vehContainer(vpp::ByteView(vehArchive.data(), vehArchive.size()));
        std::string gcarName = cmeshName;
        {
            const size_t dot = gcarName.find_last_of('.');
            if (dot != std::string::npos && dot + 1 < gcarName.size()) gcarName[dot + 1] = 'g';
        }
        std::vector<uint8_t> ccarBytes, gcarBytes;
        if (!findEntry(vehContainer, cmeshName, ccarBytes, gcarBytes, gcarName) || gcarBytes.empty()) {
            std::fprintf(stderr, "could not find '%s' (+ its paired '%s')\n", cmeshName.c_str(), gcarName.c_str());
            return 1;
        }
        vehPtr = std::make_unique<sr3vehicle::Vehicle>(
            sr3vehicle::Vehicle::parse(vpp::ByteView(ccarBytes.data(), ccarBytes.size())));
        vpp::ByteView ccarView(ccarBytes.data(), ccarBytes.size());
        sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parseAt(ccarView, vehPtr->meshRegionOffset());
        if (!geo.hasMeshSubBlock()) {
            std::fprintf(stderr, "%s: vehicle geometry block has no Mesh sub-block\n", cmeshName.c_str());
            return 1;
        }
        meshPtr = std::make_unique<sr3mesh::MeshBlock>(sr3mesh::MeshBlock::parse(
            ccarView, geo.meshSubBlockOffset(), vpp::ByteView(gcarBytes.data(), gcarBytes.size())));

        // ---- BUG FIX (2026-10-08 v4) - render EVERY channel group 0's
        // ranges actually use, not just channel 0. Directly measured (same
        // `vehicle` command golden_scene_check.cpp's own frozen
        // vehicle_genki golden uses, --group 0): car_4dr_genki_0's draw
        // group 0 is NOT single-channel - of its 20 real draw ranges, 16
        // have submeshIndex==0, 3 have submeshIndex==1, 1 has
        // submeshIndex==2 (HANDOFF Sec9.89/Sec9.103/Sec9.104 rule 16:
        // submeshIndex IS the real channel index - confirmed field, not a
        // guess). v3 (merged, then found incomplete) restricted this
        // command's single upload() call to ONLY the 16 genuinely-channel-0
        // ranges - correct for THOSE ranges (stopped the index-buffer
        // corruption an earlier revision had: every range's indices read
        // against channel 0's vertex data regardless of its real channel),
        // but the other 4 ranges' real geometry (submeshIndex 1/2 - the
        // middle-body panels: doors/cabin walls, per the visual gap this
        // was caught from) was simply never uploaded at all - "masked out,
        // not mis-indexed" was correct about the corruption but still a
        // real coverage gap. THE FIX (v4): one MeshRenderer PER DISTINCT
        // CHANNEL present in group 0 (ascending channel order, same
        // enumeration MultiChannelMultiSetRender::build() above already
        // uses), each uploaded from THAT channel's own decodeChannel(c)
        // with a rangeMask restricted to only that channel's own ranges -
        // this file's own already-established multi-channel pattern,
        // reused rather than re-invented. loadVehicleObject() itself (and
        // every frozen golden that depends on it) remains untouched - this
        // is local to runDriveDemo()'s own separate inline loader.
        if (meshPtr->drawGroupsLocated() && !meshPtr->drawGroups().empty()) {
            const auto& group0 = meshPtr->drawGroups()[0];
            std::vector<uint32_t> distinctChannels;
            for (const auto& rg : group0) {
                if (std::find(distinctChannels.begin(), distinctChannels.end(), rg.submeshIndex) ==
                    distinctChannels.end()) {
                    distinctChannels.push_back(rg.submeshIndex);
                }
            }
            std::sort(distinctChannels.begin(), distinctChannels.end());
            for (uint32_t c32 : distinctChannels) {
                const size_t c = static_cast<size_t>(c32);
                if (c >= meshPtr->channels().size()) {
                    std::fprintf(stderr,
                                 "group 0 range names channel %zu, out of range for a %zu-channel mesh\n", c,
                                 meshPtr->channels().size());
                    return 1;
                }
                VehicleChannelRender vcr;
                vcr.channelIndex = c;
                vcr.rangeMask.assign(group0.size(), false);
                size_t kept = 0;
                for (size_t r = 0; r < group0.size(); ++r) {
                    if (group0[r].submeshIndex == c32) {
                        vcr.rangeMask[r] = true;
                        ++kept;
                    }
                }
                vcr.verts = meshPtr->decodeChannel(c);
                std::printf("[mesh]   group 0 channel %zu: %zu/%zu range(s), %zu vert(s) decoded\n", c, kept,
                            group0.size(), vcr.verts.size());
                vehicleChannels.push_back(std::move(vcr));
            }
        } else {
            // Draw groups not located at all (not hit by the real
            // car_4dr_genki_0 fixture this command defaults to, but handled
            // rather than crash): one channel-0-only target with no mask,
            // the same fallback upload() itself already applies when
            // drawGroupsLocated() is false (whole index buffer as one
            // strip).
            VehicleChannelRender vcr;
            vcr.channelIndex = 0;
            vcr.verts = meshPtr->decodeChannel(0);
            vehicleChannels.push_back(std::move(vcr));
        }

        for (auto& vcr : vehicleChannels) {
            vcr.renderer = std::make_unique<sr3render::MeshRenderer>();
            const std::vector<bool>* maskPtr = vcr.rangeMask.empty() ? nullptr : &vcr.rangeMask;
            if (!vcr.renderer->initialise(device.device(), error) ||
                !vcr.renderer->upload(device.device(), *meshPtr, vcr.channelIndex, 0, error, 0, nullptr, nullptr,
                                      maskPtr)) {
                std::fprintf(stderr, "channel %zu: %s\n", vcr.channelIndex, error.c_str());
                return 1;
            }
        }
        vehicleObj.label = cmeshName;
        float combinedMin[3] = {1e30f, 1e30f, 1e30f}, combinedMax[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& vcr : vehicleChannels) {
            const float* mn = vcr.renderer->boundsMin();
            const float* mx = vcr.renderer->boundsMax();
            for (int k = 0; k < 3; ++k) {
                if (mn[k] < combinedMin[k]) combinedMin[k] = mn[k];
                if (mx[k] > combinedMax[k]) combinedMax[k] = mx[k];
            }
        }
        std::printf("object   : %-28s %zu part(s), %zu channel(s) rendered, bounds %.3f x %.3f x %.3f\n",
                    cmeshName.c_str(), vehPtr->parts().size(), vehicleChannels.size(),
                    static_cast<double>(combinedMax[0] - combinedMin[0]),
                    static_cast<double>(combinedMax[1] - combinedMin[1]),
                    static_cast<double>(combinedMax[2] - combinedMin[2]));
        std::printf("[diag]   RAW (pre-transform) combined min/max: (%.3f, %.3f, %.3f)..(%.3f, %.3f, %.3f)\n",
                    static_cast<double>(combinedMin[0]), static_cast<double>(combinedMin[1]),
                    static_cast<double>(combinedMin[2]), static_cast<double>(combinedMax[0]),
                    static_cast<double>(combinedMax[1]), static_cast<double>(combinedMax[2]));

        // ---- Real per-material paint colour (improvement 1) - see
        // resolveVehiclePaintColoursV()'s own doc comment. ----
        try {
            sr3geometry::MaterialBindings bindings =
                sr3geometry::MaterialBindings::parse(ccarView, geo.offset(), geo.meshSubBlockOffset() + meshPtr->cLength());
            if (bindings.located() && bindings.materialCount() > 0) {
                std::vector<VehiclePaintColourV> paint = resolveVehiclePaintColoursV(
                    ccarView, geo.offset(), geo.meshSubBlockOffset(), meshPtr->cLength(), bindings.materialCount());
                vehiclePerMaterialSrv.assign(bindings.materialCount(), nullptr);
                for (uint32_t m = 0; m < bindings.materialCount(); ++m) {
                    ID3D11ShaderResourceView* srv = nullptr;
                    if (createSolidColorTextureV(device.device(), paint[m].value, &srv)) {
                        vehiclePerMaterialSrv[m] = srv;
                    }
                    std::printf("[paint] material %u: %s Base_Paint_Color (%.3f, %.3f, %.3f, %.3f)\n", m,
                                paint[m].resolved ? "REAL" : "UNRESOLVED (neutral-grey fallback)",
                                static_cast<double>(paint[m].value[0]), static_cast<double>(paint[m].value[1]),
                                static_cast<double>(paint[m].value[2]), static_cast<double>(paint[m].value[3]));
                }
                // ---- Magenta/purple edge investigation (this task,
                // 2026-10-08) - traced per the task brief's own instruction
                // to check the [paint] diagnostic rather than assume a bug.
                // drawTextured() (src/mesh_renderer.cpp) falls back to
                // MeshRenderer::materialIdColour() - a golden-ratio hue-
                // stepped DEBUG tint, explicitly NOT a resolved paint colour
                // - for any draw range whose materialId is either >=
                // perMaterial.size() (out of range for this vehicle's own
                // bindings.materialCount()) or whose createSolidColorTextureV()
                // call above failed. Neither case is "a real resolved paint
                // colour that happens to look magenta" - both are a genuine
                // debug/fallback colour, printed here explicitly so a
                // magenta (or any other oddly-saturated) edge can be traced
                // to its real cause instead of guessed at.
                if (meshPtr->drawGroupsLocated() && !meshPtr->drawGroups().empty()) {
                    std::set<uint32_t> outOfRangeMaterialIds;
                    for (const auto& rg : meshPtr->drawGroups()[0]) {
                        if (rg.materialId >= vehiclePerMaterialSrv.size()) outOfRangeMaterialIds.insert(rg.materialId);
                    }
                    if (!outOfRangeMaterialIds.empty()) {
                        std::string ids;
                        for (uint32_t id : outOfRangeMaterialIds) ids += std::to_string(id) + " ";
                        std::printf("[paint] WARNING: draw group 0 range(s) reference materialId(s) [ %s] >= "
                                    "this vehicle's own bindings.materialCount() (%u) - those range(s) fall back "
                                    "to MeshRenderer::materialIdColour()'s golden-ratio-hue DEBUG tint at draw "
                                    "time (NOT a real Base_Paint_Color), which can land on any hue including "
                                    "magenta/purple depending on the materialId value - this, not a resolved "
                                    "paint colour, is the real source of any magenta-looking edge\n",
                                    ids.c_str(), bindings.materialCount());
                    } else {
                        std::printf("[paint] every draw group 0 range's materialId is in range for this "
                                    "vehicle's own %u resolved material(s) - no materialIdColour() DEBUG-tint "
                                    "fallback is reachable from this scene's own draw ranges\n",
                                    bindings.materialCount());
                    }
                }
            } else {
                std::printf("[paint] material bindings not located - falling back to Checker (v1 behaviour)\n");
            }
        } catch (const std::exception& ex) {
            std::printf("[paint] material binding/paint resolution failed: %s - falling back to Checker\n", ex.what());
            vehiclePerMaterialSrv.clear();
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "vehicle archive error: %s\n", ex.what());
        return 1;
    }
    if (vehicleChannels.empty()) {
        std::fprintf(stderr, "no channel target built for draw group 0 - nothing to render\n");
        return 1;
    }
    // Shared depth buffer: only the FIRST channel's renderer owns one (same
    // "only the FIRST channel's renderer owns a depth buffer" convention
    // MultiChannelMultiSetRender uses above) - every later channel's draw()
    // call relies on that SAME view being bound externally, below.
    if (!vehicleChannels.front().renderer->createDepth(device.device(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
        return 1;
    }
    uint32_t depthWidth = winWidth, depthHeight = winHeight;
    sr3vehicle::Vehicle& veh = *vehPtr;
    sr3mesh::MeshBlock& mesh = *meshPtr;

    // ---- Wheel identification (improvement 3) - see isVehicleWheelPartV()'s
    // own doc comment. Printed honestly either way: a mesh genuinely without
    // identifiable wheel nodes gets no fabricated geometry. ----
    std::vector<bool> partIsWheel(veh.parts().size(), false);
    std::vector<bool> partIsFrontWheel(veh.parts().size(), false);
    std::vector<double> wheelRollAngleRad(veh.parts().size(), 0.0);
    size_t wheelPartCount = 0, frontWheelPartCount = 0;
    for (size_t i = 0; i < veh.parts().size(); ++i) {
        bool isFront = false;
        if (isVehicleWheelPartV(veh.parts()[i], &isFront)) {
            partIsWheel[i] = true;
            partIsFrontWheel[i] = isFront;
            ++wheelPartCount;
            if (isFront) ++frontWheelPartCount;
            std::printf("[wheel] part %zu '%s' (%s) - will spin%s\n", i, veh.parts()[i].name.c_str(),
                        sr3vehicle::partTypeName(veh.parts()[i].partType), isFront ? " and steer" : "");
        }
    }
    if (wheelPartCount == 0) {
        std::printf("[wheel] no identifiable wheel parts (sr3vehicle::partTypeName() wheel readings) on this "
                    "mesh - no wheel animation, honestly reported rather than fabricated\n");
    } else {
        std::printf("[wheel] %zu wheel part(s) identified, %zu of them front/steered\n", wheelPartCount,
                    frontWheelPartCount);
    }

    // ---- Wheel pivot centroids (BUG FIX, 2026-10-08 v3, extended v4) - see
    // buildWheelDeltaTransformV()'s own doc comment for the full story. Each
    // wheel part's pivot is derived directly from that SAME part's own raw
    // vertices (the real decode loadVehicleObject()'s v1 render path already
    // uploads byte-identical, for whichever channel(s) those vertices
    // actually live in - v4: summed across EVERY vehicleChannels[*].verts,
    // not just channel 0, since a wheel part is not guaranteed to own
    // channel-0 vertices alone; v3's own test run found only 1 of 8
    // identified wheel parts did) - NOT fetched from veh.parts()[i].transform's
    // translation row, sidestepping whatever channel/space mismatch made
    // that field unsafe as a per-vertex baseline for this simplified render
    // path. A wheel part with zero vertices across every channel (shouldn't
    // happen for an identified wheel part, but handled rather than divided-
    // by-zero) keeps centroid (0,0,0) and is simply not spun/steered
    // visibly (its delta transform still computes, just pivoted at the
    // origin - there is nothing to pivot since it owns no vertices).
    std::vector<std::array<double, 3>> wheelCentroidSum(veh.parts().size(), std::array<double, 3>{0.0, 0.0, 0.0});
    std::vector<size_t> wheelCentroidCount(veh.parts().size(), 0);
    for (const auto& vcr : vehicleChannels) {
        for (const auto& v : vcr.verts) {
            if (v.rigidPartIndex < partIsWheel.size() && partIsWheel[v.rigidPartIndex]) {
                wheelCentroidSum[v.rigidPartIndex][0] += static_cast<double>(v.position[0]);
                wheelCentroidSum[v.rigidPartIndex][1] += static_cast<double>(v.position[1]);
                wheelCentroidSum[v.rigidPartIndex][2] += static_cast<double>(v.position[2]);
                ++wheelCentroidCount[v.rigidPartIndex];
            }
        }
    }
    std::vector<std::array<float, 3>> wheelCentroid(veh.parts().size(), std::array<float, 3>{0.0f, 0.0f, 0.0f});
    for (size_t i = 0; i < veh.parts().size(); ++i) {
        if (wheelCentroidCount[i] == 0) continue;
        const double n = static_cast<double>(wheelCentroidCount[i]);
        wheelCentroid[i][0] = static_cast<float>(wheelCentroidSum[i][0] / n);
        wheelCentroid[i][1] = static_cast<float>(wheelCentroidSum[i][1] / n);
        wheelCentroid[i][2] = static_cast<float>(wheelCentroidSum[i][2] / n);
        std::printf("[wheel] part %zu pivot (derived centroid of %zu raw vertices): (%.3f, %.3f, %.3f)\n", i,
                    wheelCentroidCount[i], static_cast<double>(wheelCentroid[i][0]),
                    static_cast<double>(wheelCentroid[i][1]), static_cast<double>(wheelCentroid[i][2]));
    }
    // v4 MEASUREMENT (2026-10-08, this task): re-checked whether adding
    // channels 1/2 (not just channel 0) changes how many of the identified
    // wheel parts own ANY vertices, across EVERY channel group 0 actually
    // uses - it does not. Printed honestly either way, same as every other
    // honest-gap report in this function.
    {
        size_t wheelPartsWithVertices = 0;
        for (size_t i = 0; i < veh.parts().size(); ++i) {
            if (partIsWheel[i] && wheelCentroidCount[i] > 0) ++wheelPartsWithVertices;
        }
        std::printf("[wheel] %zu/%zu identified wheel part(s) own at least one vertex across all %zu located "
                    "channel(s) of group 0 - v4 (multi-channel) measurement, NOT assumed: the other %zu wheel "
                    "part(s) own no visible geometry in this draw group under ANY channel, so they are not an "
                    "upload/channel-selection gap this fix can close - either their visual mesh lives outside "
                    "group 0 entirely, or these sr3vehicle::parts() entries are rig/attach nodes without their "
                    "own owned geometry (both genuinely open questions, not resolved here - reported rather "
                    "than guessed at)\n",
                    wheelPartsWithVertices, wheelPartCount, vehicleChannels.size(),
                    wheelPartCount - wheelPartsWithVertices);
    }

    // ---- Doors/panels, body, and wheel-arch fit (THIS TASK, 2026-10-08 -
    // fixing a real regression the prior merge's own "doors/panels"
    // improvement introduced): that prior pass applied each part's own
    // REAL +0x00 4x4 transform to its OWN vertices (matched by
    // rigidPartIndex) on the theory (spec §12.2's own candidate 2) that raw
    // vertex data needs the per-part transform composed to reach its real
    // resting position. DIRECTLY MEASURED this task, and FALSIFIED:
    //
    //   - With the transform applied, type-4 (door/panel) vertices'
    //     transformed bbox reached (-1.166,-0.282,-2.099)..(2.031,0.989,
    //     3.520) - X up to 2.031 and Z up to 3.520, both FAR outside the
    //     vehicle's own RAW combined bbox (-1.082,-0.441,-2.984)..
    //     (1.082,0.830,2.635) despite every vertex belonging to a part of
    //     THIS SAME vehicle. The overrun is not random: raw body max X
    //     (1.082) + door_FR's own transform.translateX (0.949, see the
    //     prior pass's own measured value below) = 2.031, EXACTLY the
    //     printed door max X; raw body max Z (2.635) + door_FR's own
    //     translateZ (0.885) = 3.520, EXACTLY the printed door max Z. Two
    //     independent axes landing on an exact match is not coincidence -
    //     it is a DOUBLE TRANSLATION: the raw vertex position is ALREADY
    //     the part's correct final vehicle-local position (confirmed
    //     separately below), and adding the part's own +0x00
    //     transform.translate on top counts that offset twice.
    //   - Direct confirmation: with the transform REMOVED, type-4 vertices'
    //     own raw bbox is (-1.082,-0.441,-2.984)..(1.082,0.830,2.635) -
    //     BYTE-FOR-BYTE the vehicle's own combined raw bbox. Door/panel/
    //     hood/trunk vertices, in their raw encoded form with NO transform
    //     applied at all, already span exactly the real vehicle's own
    //     envelope - they are pre-placed, not awaiting composition.
    //   - This same bug also explains the ORIGINAL wheel-placement defect
    //     this task was filed for: type-1 ("fixed body") vertices go
    //     through the identical code path, so the double translation also
    //     dragged the BODY's own lower silhouette down and sideways
    //     (measured: transformed body Y min -0.751 vs raw body Y min
    //     -0.441, a -0.31 unit drop) - pulling the body's own rocker-panel/
    //     sill edge well BELOW the wheels' own real anchor position
    //     (wheel hub Y -0.218, radius 0.320 -> wheel spans [-0.538,+0.102],
    //     which sits comfortably INSIDE the real body's raw Y range
    //     [-0.441,+0.830]). The wheel attach code itself
    //     (buildWheelAttachTransformV, the primaryWheelSlots anchors, the
    //     suspension ease) was never the bug - it already read
    //     veh.parts()[i].translationX/Y/Z() as an ABSOLUTE final position
    //     (the correct model, matching this same finding), not as a delta
    //     to add onto something else. Direct before/after visual check
    //     (this task's own capture, body transform removed): the rear
    //     wheel now sits visibly tucked inside a real arch cutout in the
    //     body silhouette - see this task's own report for the captured
    //     frame.
    //
    // FIX: non-wheel vertices now use their RAW v.position directly -
    // EXACTLY the same treatment wheel-part vertices and out-of-range
    // rigidPartIndex values already got (see the single unconditional
    // assignment below) - matching what v1 of this renderer always did,
    // before the prior merge's falsified "needs the transform" theory.
    //
    // HONEST RESIDUAL on doors specifically (unchanged by this fix, not
    // papered over): even at their real, correctly-measured raw position,
    // type-4 vertices do not present as a fully convincing flush "closed
    // door panel" from this demo's side-on chase camera - a thin, angled
    // shape is visible near the windshield pillar rather than a solid
    // panel filling the cabin opening. This is NOT a positioning bug (the
    // position is now confirmed correct - see above), so the prior merge's
    // "translation-only, so a hinged door still shows a residual angled
    // flap" framing was the wrong diagnosis for the wrong reason (it
    // blamed a missing hinge rotation on top of a transform that should
    // never have been applied at all) - but the underlying visual gap it
    // described is real. Most likely explanation, consistent with
    // spec-vehicle-geometry.md §7.2 Q2 and §12.2's own still-OPEN item 3:
    // whether each part selects its own geometry subset out of the
    // vehicle's one combined embedded mesh is unresolved, so these
    // vertices may simply not encode a full solid panel face the way a
    // player would see it in-game (e.g. a frame/trim strip rather than the
    // whole door skin) - genuinely open, not invented here.
    std::vector<std::vector<std::array<float, 3>>> vehicleBasePositions(vehicleChannels.size());
    {
        std::vector<size_t> perTypeRawCount(32, 0);
        size_t outOfRangeCount = 0;
        for (size_t ci = 0; ci < vehicleChannels.size(); ++ci) {
            const auto& vcr = vehicleChannels[ci];
            vehicleBasePositions[ci].resize(vcr.verts.size());
            for (size_t vi = 0; vi < vcr.verts.size(); ++vi) {
                const auto& v = vcr.verts[vi];
                const size_t pidx = v.rigidPartIndex;
                // Every vertex (wheel or not, in-range or not) renders at
                // its RAW encoded position - see this block's own doc
                // comment for the direct measurement that falsified
                // composing the part's own +0x00 transform on top.
                vehicleBasePositions[ci][vi] = v.position;
                if (pidx >= veh.parts().size()) {
                    ++outOfRangeCount;
                } else {
                    const uint32_t t = veh.parts()[pidx].partType;
                    if (t < perTypeRawCount.size()) ++perTypeRawCount[t];
                }
            }
        }
        std::printf("[doors]  real part-array vertex ownership (RAW position, no +0x00 transform - see this "
                    "block's own doc comment for why), by type: ");
        bool any = false;
        for (size_t t = 0; t < perTypeRawCount.size(); ++t) {
            if (perTypeRawCount[t] == 0) continue;
            const char* nm = sr3vehicle::partTypeName(static_cast<uint32_t>(t));
            std::printf("%s%zu=%s(%zu)", any ? ", " : "", t, nm ? nm : "?", perTypeRawCount[t]);
            any = true;
        }
        if (!any) std::printf("none");
        std::printf(" (%zu vertex/vertices with an out-of-range rigidPartIndex)\n", outOfRangeCount);
    }

    // ---- Wheel/rim catalogue (improvement 1, this task, §12.1) - see
    // loadExternalizedWheelCatalogV()'s own doc comment for the full real/
    // CHOSEN breakdown. Loaded once, attached to the PRIMARY 4 wheel slots
    // only (isPrimaryVehicleWheelPartV() - part types 2/3). A vehicle
    // genuinely without identifiable primary wheel slots, or whose
    // catalogue load/resolve fails for any reason, gets no fabricated
    // wheel mesh - reported honestly, same discipline as every other
    // "no fake geometry" gap in this function. ----
    std::unique_ptr<sr3render::MeshRenderer> catalogWheelRenderer;
    std::array<float, 3> catalogWheelPivot{0.0f, 0.0f, 0.0f};
    // CHOSEN (this task, computed from REAL measured data): a uniform
    // scale calibrating the catalogue mesh's own native size against
    // tuning.wheelRadius (the SAME real constant the physics sim already
    // uses for rolling-speed, not a separately invented number) - see
    // buildWheelAttachTransformV()'s own call site below for exactly how
    // it is computed, and this block's own [rim] diagnostic for the real
    // measured bounds it is computed from.
    float catalogWheelScale = 1.0f;
    bool catalogWheelReady = false;
    // CHOSEN (spec §12.1's own Region-3/table3 "HONESTLY OPEN" note,
    // sr3vehicle/cvtf.h's own top comment): this project's existing
    // .cvtf_pc reader cannot resolve car_4dr_genki's REAL equipped rim
    // style (Region 3/table3, the per-vehicle style-preset index, is not
    // decoded by CvtfCatalog - exposed only as a raw, undecoded offset/
    // count). "R100" is the FIRST real rim token found in the catalogue's
    // own file order (record index 5, measured directly, see this
    // function's own [rim] diagnostic below) - a reasonable, clearly-
    // labelled placeholder, not a confirmed real equipped style.
    const std::string kChosenWheelStyleNameV = "R100";
    if (loadWheelCatalog) {
        std::vector<uint8_t> wheelsArchive = readFileLargeC(wheelsArchivePath);
        if (wheelsArchive.empty()) {
            std::fprintf(stderr, "[rim]    could not read %s - wheels will not be attached, vehicle still renders\n",
                         wheelsArchivePath.c_str());
        } else {
            try {
                vpp::Container wheelsContainer(vpp::ByteView(wheelsArchive.data(), wheelsArchive.size()));
                std::vector<uint8_t> catBytes, catGBytes;
                if (!findEntry(wheelsContainer, "externalized_components.cvcomp_pc", catBytes, catGBytes,
                               "externalized_components.gvcomp_pc") ||
                    catBytes.empty()) {
                    std::fprintf(stderr,
                                 "[rim]    could not find externalized_components.cvcomp_pc (+ its paired "
                                 ".gvcomp_pc) in %s\n",
                                 wheelsArchivePath.c_str());
                } else {
                    std::vector<ExternalizedCatalogPartV> catParts;
                    size_t catMeshRegionOffset = 0;
                    uint32_t catHeaderTag = 0;
                    std::string diag;
                    vpp::ByteView catView(catBytes.data(), catBytes.size());
                    if (!loadExternalizedWheelCatalogV(catView, catParts, catMeshRegionOffset, catHeaderTag, diag)) {
                        std::fprintf(stderr, "[rim]    catalogue header/part-array scan failed: %s\n", diag.c_str());
                    } else {
                        std::printf("[rim]    externalized_components.cvcomp_pc: %s\n", diag.c_str());
                        const int styleIdx = findExternalizedCatalogPartV(catParts, kChosenWheelStyleNameV);
                        if (styleIdx < 0) {
                            std::fprintf(stderr, "[rim]    style '%s' not found among %zu scanned catalogue part(s)\n",
                                         kChosenWheelStyleNameV.c_str(), catParts.size());
                        } else {
                            std::printf("[rim]    resolved style '%s' -> catalogue part index %d by name (CHOSEN "
                                        "style name, see this block's own doc comment) - confirms the by-name "
                                        "lookup mechanism spec §12.1 asks for\n",
                                        kChosenWheelStyleNameV.c_str(), styleIdx);
                            sr3geometry::GeometryBlock catGeo =
                                sr3geometry::GeometryBlock::parseAt(catView, catMeshRegionOffset);
                            if (!catGeo.hasMeshSubBlock()) {
                                std::fprintf(stderr, "[rim]    catalogue mesh region has no Mesh sub-block\n");
                            } else {
                                sr3mesh::MeshBlock catMesh = sr3mesh::MeshBlock::parse(
                                    catView, catGeo.meshSubBlockOffset(),
                                    vpp::ByteView(catGBytes.data(), catGBytes.size()));
                                CatalogWheelMeshV catWheelMesh;
                                std::string diag2;
                                // REAL FINDING (this task, measured directly
                                // against the real catalogue mesh - not
                                // assumed): NONE of the 73 real "RNNN" rim-
                                // token part records (including the
                                // resolved styleIdx) own ANY wholly-owned
                                // draw range in this file's rigidPartIndex
                                // scheme - only catalogue part index 0
                                // ("000") does, owning a single large range
                                // (36,838 vertices). This strongly suggests
                                // the ~90 RNNN records are TEXTURE/material
                                // skin selectors over ONE SHARED generic
                                // wheel+tire base shape (common game-dev
                                // practice: many cosmetic rim skins, one
                                // base mesh), not ~90 distinct mesh shapes -
                                // a real, checked finding, not assumed from
                                // spec text (spec itself does not claim
                                // either way). CHOSEN fallback: if the named
                                // style owns no geometry of its own, use the
                                // catalogue's shared base shape (part index
                                // 0) instead - attaching ACTUAL wheel/tire
                                // geometry rather than nothing, honestly
                                // labelled as the shared base shape rather
                                // than a style-specific one.
                                uint32_t geometryPartIdx = static_cast<uint32_t>(styleIdx);
                                if (!resolveCatalogWheelMeshV(catMesh, geometryPartIdx, catWheelMesh, diag2)) {
                                    std::printf("[rim]    style '%s' (part %d) owns no geometry of its own (%s) - "
                                                "falling back to the catalogue's shared base shape (part index 0, "
                                                "'%s'), see this block's own doc comment for why\n",
                                                kChosenWheelStyleNameV.c_str(), styleIdx, diag2.c_str(),
                                                catParts.empty() ? "?" : catParts[0].name.c_str());
                                    geometryPartIdx = 0;
                                    diag2.clear();
                                    resolveCatalogWheelMeshV(catMesh, geometryPartIdx, catWheelMesh, diag2);
                                }
                                if (catWheelMesh.rangeMask.empty()) {
                                    std::fprintf(stderr, "[rim]    could not resolve any wheel geometry (shared "
                                                         "base shape included): %s\n", diag2.c_str());
                                } else {
                                    std::printf("[rim]    resolved geometry from catalogue part %u ('%s'): %s\n",
                                                geometryPartIdx, catParts[geometryPartIdx].name.c_str(), diag2.c_str());
                                    catalogWheelRenderer = std::make_unique<sr3render::MeshRenderer>();
                                    std::string uploadErr;
                                    if (!catalogWheelRenderer->initialise(device.device(), uploadErr) ||
                                        !catalogWheelRenderer->upload(device.device(), catMesh,
                                                                      catWheelMesh.channelIndex, 0, uploadErr, 0,
                                                                      nullptr, nullptr, &catWheelMesh.rangeMask)) {
                                        std::fprintf(stderr, "[rim]    GPU upload failed: %s\n", uploadErr.c_str());
                                        catalogWheelRenderer.reset();
                                    } else {
                                        catalogWheelPivot = catWheelMesh.centroid;
                                        // CHOSEN calibration (this task,
                                        // measured directly - see this
                                        // block's own earlier doc comment):
                                        // the catalogue mesh's own measured
                                        // vertical half-extent (Y bounds)
                                        // stands in for "this mesh's own
                                        // native wheel radius", scaled up
                                        // to match tuning.wheelRadius (the
                                        // SAME real constant
                                        // sim.state_.wheels' own rolling
                                        // speed already uses - not a new
                                        // invented number). Guarded against
                                        // a degenerate (near-zero) measured
                                        // extent.
                                        const float measuredHalfHeight =
                                            0.5f * (catWheelMesh.boundsMax[1] - catWheelMesh.boundsMin[1]);
                                        catalogWheelScale = (measuredHalfHeight > 1e-4f)
                                                                ? static_cast<float>(tuning.wheelRadius) / measuredHalfHeight
                                                                : 1.0f;
                                        catalogWheelReady = true;
                                        std::printf("[rim]    attached, pivot (%.3f, %.3f, %.3f), scale %.3f "
                                                    "(measured half-height %.3f vs tuning.wheelRadius %.3f)\n",
                                                    static_cast<double>(catalogWheelPivot[0]),
                                                    static_cast<double>(catalogWheelPivot[1]),
                                                    static_cast<double>(catalogWheelPivot[2]),
                                                    static_cast<double>(catalogWheelScale),
                                                    static_cast<double>(measuredHalfHeight), tuning.wheelRadius);
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "[rim]    wheels archive error: %s - wheels will not be attached\n", ex.what());
            }
        }
    } else {
        std::printf("[rim]    --no-wheels given - no wheel mesh will be attached, v1-style raw vehicle geometry "
                    "only\n");
    }
    if (!catalogWheelReady) {
        std::printf("[rim]    no wheel mesh attached - honestly reported rather than fabricated, vehicle still "
                    "renders\n");
    }

    // ---- Primary wheel slot anchors (improvement 1/3, prior task) - the
    // REAL per-part +0x00 transform's own translation row, used ONLY as a
    // position marker for where to ATTACH the catalogue wheel mesh, exactly
    // the way the real engine's own equip splice (spec §12.3: render-object
    // +0x84) places a swapped-in shared wheel asset at a per-slot position.
    // Reading this translation as an ABSOLUTE final vehicle-local position
    // (not a delta composed onto anything) was always the correct model -
    // this task's own wheel-placement root-cause fix (see the vehicleBase
    // Positions block's own doc comment above) confirms it directly: raw
    // vertex data for every part is already final, so a part's translation
    // row is that same final-position convention, consistently.
    struct PrimaryWheelSlotV {
        size_t partIndex = 0;
        std::array<float, 3> anchor{0.0f, 0.0f, 0.0f};
        bool isFront = false;
        // CHOSEN: +1 when anchor.x >= 0, -1 otherwise. MEASURED, not
        // assumed (this task): this vehicle's real wheel-slot anchors
        // separate LEFT/RIGHT by X (FL x=-0.770 vs FR x=+0.768,
        // near-identical Z) and FRONT/REAR by Z (front z=+1.884 vs rear
        // z=-1.492, near-identical X) - X is the real lateral axis in this
        // vehicle's own object space, not Z (see makeRotationXRowVectorV()'s
        // own doc comment for the fuller cross-check, including the
        // catalogue mesh's own bounds and a direct visual confirmation).
        float sign = 1.0f;
        int simWheelIdx = 0; // CHOSEN mapping into sim.state_.wheels[0..3] (0=FL,1=FR,2=RL,3=RR by convention)
    };
    std::vector<PrimaryWheelSlotV> primaryWheelSlots;
    for (size_t i = 0; i < veh.parts().size(); ++i) {
        if (!isPrimaryVehicleWheelPartV(veh.parts()[i])) continue;
        PrimaryWheelSlotV slot;
        slot.partIndex = i;
        slot.anchor = {veh.parts()[i].translationX(), veh.parts()[i].translationY(), veh.parts()[i].translationZ()};
        slot.isFront = partIsFrontWheel[i];
        slot.sign = (slot.anchor[0] >= 0.0f) ? 1.0f : -1.0f;
        slot.simWheelIdx = (slot.isFront ? 0 : 2) + (slot.sign > 0.0f ? 1 : 0);
        primaryWheelSlots.push_back(slot);
        std::printf("[rim]    primary wheel slot: part %zu '%s' anchor (%.3f, %.3f, %.3f) side-sign %.0f -> "
                    "sim wheel index %d\n",
                    i, veh.parts()[i].name.c_str(), static_cast<double>(slot.anchor[0]),
                    static_cast<double>(slot.anchor[1]), static_cast<double>(slot.anchor[2]),
                    static_cast<double>(slot.sign), slot.simWheelIdx);
    }
    // ---- Suspension ease state (improvement 3, this task, §12.3) - one
    // smoothed visual Y-offset per sim wheel index, eased toward a CHOSEN
    // target derived from the REAL per-frame normalForce the physics sim
    // already computes (sub-steps 8/13's own load-transfer output -
    // sim.state_.wheels[k].normalForce), at the one CONFIRMED formula
    // component spec §12.3 gives for the secondary/dual-axle mechanism
    // (5% exponential ease per unit time, scaled by frame dt - a damped
    // approach, not a raw snap), applied here to the PRIMARY 4 wheels BY
    // ANALOGY (CHOSEN - §12.3 states the primary-wheel mechanism itself is
    // unresolved, only that the secondary one is confirmed; this reuses
    // that confirmed ease RATE, not a confirmed primary-wheel formula).
    // "Unit time" is read as sr3physics::kSuspensionMaxMicroStepSec
    // (1/60s, spec-physics-runtime.md §10.4's own CONFIRMED literal) - a
    // REAL existing constant reused for this interpretation rather than a
    // freshly invented one, CHOSEN because this demo's own frame dt is the
    // SAME 1/60s already (so this reduces to a clean, reviewable "5% per
    // frame" ease at this demo's own fixed frame rate).
    //
    // The TARGET itself is CHOSEN, not a Havok suspension-compressed
    // length (this harness's planar physics model, vehicle_substeps.h's
    // own banner, carries no vertical/compression state at all - see that
    // file's PLANAR SCOPE note): target offset = (currentNormalForce -
    // referenceNormalForce) * kSuspensionComplianceMPerN, clamped to
    // +/-kSuspensionMaxTravelM. The reference is each wheel's own
    // normalForce at frame 0 (its approximate static/resting load) -
    // captured once below. Sign: MORE load -> the wheel's anchor moves
    // UP in vehicle-local space (the body sinking toward a compressing
    // spring), matching the intuitive visual (braking loads the front,
    // so the front appears to dip).
    constexpr double kSuspensionComplianceMPerN = 0.00003; // CHOSEN (m/N)
    constexpr double kSuspensionMaxTravelM = 0.03;         // CHOSEN (m), +/-3cm
    std::array<double, 4> suspensionReferenceForce{-1.0, -1.0, -1.0, -1.0};
    std::array<double, 4> suspensionSmoothedOffsetY{0.0, 0.0, 0.0, 0.0};

    // ---- Start pose (CHOSEN): near city_corner's own frozen focus point, so
    // the vehicle is actually in frame from frame 0 - see this function's own
    // doc comment. heading 0 = facing +X (sim's own convention, see
    // VehicleInstanceState::heading's doc comment).
    sim.state_.posX = -960.0;
    sim.state_.posZ = -700.0;
    sim.state_.heading = 0.0;

    // ---- Real city_corner scenery (improvement 2) - static one-time load,
    // see this function's own top doc comment for why. ----
    std::vector<std::unique_ptr<CityArchive>> cityArchives;
    std::vector<CityCell> cityCells;
    std::vector<CityLoadedCell> cityLoadedCells;
    if (loadCity) {
        auto a = std::make_unique<CityArchive>();
        a->path = cityArchivePath;
        {
            const size_t slash = cityArchivePath.find_last_of("/\\");
            a->label = slash == std::string::npos ? cityArchivePath : cityArchivePath.substr(slash + 1);
        }
        a->bytes = mapFileLargeC(cityArchivePath);
        if (a->bytes.empty()) {
            std::fprintf(stderr, "[city] could not read %s - scenery will be empty, vehicle still renders\n",
                         cityArchivePath.c_str());
        } else {
            a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
            for (size_t i = 0; i < a->top->entries().size(); ++i) {
                const std::string& en = a->top->entries()[i].name;
                int col = 0, row = 0, n = 0;
                if (parseDefaultHnEntryName(en, col, row, n)) {
                    CityCell c;
                    c.name = en.substr(0, en.size() - 8);
                    c.entryName = en;
                    c.archiveIndex = 0;
                    c.entryIndex = i;
                    c.col = col;
                    c.row = row;
                    c.n = n;
                    cityFineCellCentre(col, row, n, c.centreX, c.centreZ);
                    cityCells.push_back(c);
                }
            }
            std::printf("[city] %s: %zu fine cell(s) enumerated\n", a->label.c_str(), cityCells.size());
            cityArchives.push_back(std::move(a));

            // Static load (CHOSEN, see this function's own top doc comment):
            // the real level-4 window rule (spec-world-streaming.md
            // Sec10.3/Sec10.4), evaluated ONCE at the vehicle's own start
            // focus rather than re-evaluated every frame - the same real
            // cityWantedCells() `city`/`walk` use, just called once.
            std::vector<size_t> wanted =
                cityWantedCells(cityCells, static_cast<float>(sim.state_.posX), static_cast<float>(sim.state_.posZ), 1.0f);
            std::printf("[city] static load around start focus (%.1f, %.1f): %zu cell(s)\n", sim.state_.posX,
                        sim.state_.posZ, wanted.size());
            for (size_t idx : wanted) {
                const CityCell& c = cityCells[idx];
                CityLoadedCell lc;
                lc.cellIndex = idx;
                try {
                    vpp::Container str2 = cityArchives[0]->top->openNested(c.entryIndex);
                    std::vector<std::string> clNames;
                    for (const auto& e : str2.entries())
                        if (endsWithNoCase(e.name, ".clmesh_pc")) clNames.push_back(e.name);
                    lc.propEntries = clNames.size();
                    for (const auto& nm : clNames) {
                        CityProp p;
                        std::string why;
                        if (loadCityProp(str2, nm, cityArchives, 0, device.device(), /*textures=*/true, p, why)) {
                            lc.props.push_back(std::move(p));
                        } else {
                            ++lc.propsFailed;
                        }
                    }
                } catch (const std::exception& ex) {
                    std::printf("[city]   %s: container open failed: %s\n", c.name.c_str(), ex.what());
                }
                // Same FAKE local grid fitted to the cell's real footprint
                // runCity()'s own loadCell lambda uses (real per-instance
                // placement is unresolved - see that lambda's own comment).
                const size_t m = lc.props.size();
                if (m > 0) {
                    const int cols = (std::max)(
                        1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(m) * kFineCellHalfX / kFineCellHalfZ))));
                    const int rows = static_cast<int>((m + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
                    const float slotX = 2.0f * kFineCellHalfX / static_cast<float>(cols);
                    const float slotZ = 2.0f * kFineCellHalfZ / static_cast<float>(rows);
                    for (size_t i = 0; i < m; ++i) {
                        CityProp& p = lc.props[i];
                        const int ci = static_cast<int>(i % static_cast<size_t>(cols));
                        const int ri = static_cast<int>(i / static_cast<size_t>(cols));
                        const float sx = c.centreX - kFineCellHalfX + (static_cast<float>(ci) + 0.5f) * slotX;
                        const float sz = c.centreZ + kFineCellHalfZ - (static_cast<float>(ri) + 0.5f) * slotZ;
                        const float lcx = (p.localMin[0] + p.localMax[0]) * 0.5f;
                        const float lcz = (p.localMin[2] + p.localMax[2]) * 0.5f;
                        sr3render::buildWorldMatrix(sx - lcx, 0.0f, sz - lcz, 0.0f, 1.0f, p.world);
                    }
                }
                std::printf("[city] LOAD  %-8s centre (%.1f, %.1f): %zu/%zu props uploaded\n", c.name.c_str(),
                            static_cast<double>(c.centreX), static_cast<double>(c.centreZ), lc.props.size(),
                            lc.propEntries);
                cityLoadedCells.push_back(std::move(lc));
            }
        }
    } else {
        std::printf("[city] --no-city given - void scene, v1 behaviour\n");
    }

    // ---- Simplified trailing camera (improvement 2's camera half; CHOSEN
    // constants, not recovered data) - see this function's own top doc
    // comment for the two REAL facts used (spec-player-runtime.md Sec4.10 row
    // identities / Sec4.7 step13's placement rule) and everything deliberately
    // skipped. `kCamDistance`/`kCamHeightAbovePivot`/`kPivotHeight` are a
    // modest, close, behind-and-above framing in the SAME spirit v1's own
    // chase offset picked (that offset was tried, visually inspected, and
    // confirmed to keep this real ~5.6m car legible - see v1's own superseded
    // comment, preserved in git history) - re-expressed here as an explicit
    // pivot/back/distance construction instead of a fixed world-frame delta,
    // because the task's own placement rule is stated that way.
    const float kCamDistance = 9.0f;          // CHOSEN (world units) behind the pivot
    const float kCamHeightAbovePivot = 4.0f;  // CHOSEN (world units) above the pivot
    const float kPivotHeight = 1.0f;          // CHOSEN (world units) - approximate cabin height above ground
    const float kCamFovY = 0.8f;              // CHOSEN, matching this file's other free-camera commands' order of magnitude
    const float kCamNearZ = 0.3f, kCamFarZ = 500.0f;

    constexpr double kDt = 1.0 / 60.0; // matches the substeps test's own kDt

    std::printf("scene    : vehicle + real city_corner scenery (%zu cell(s), %zu prop(s) total)\n",
                cityLoadedCells.size(), [&] {
                    size_t n = 0;
                    for (const auto& lc : cityLoadedCells) n += lc.props.size();
                    return n;
                }());
    std::printf("camera   : simplified trailing chase - pivot = vehicle pos + %.1f up, desired = pivot + %.1f*back "
                "(back = -forward, Sec4.7 step13/Sec4.10), + %.1f up, looking at pivot (CHOSEN framing/constants, "
                "no submode/blend/state-machine - see doc comment)\n",
                static_cast<double>(kPivotHeight), static_cast<double>(kCamDistance),
                static_cast<double>(kCamHeightAbovePivot));

    int frames = 0;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) {
            std::fprintf(stderr, "swap chain resize failed: %s\n", error.c_str());
            break;
        }
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (vehicleChannels.front().renderer->createDepth(device.device(), swapChain.width(), swapChain.height(),
                                                              error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }

        // ---- Scripted driver input for this frame (CHOSEN, see doc comment) ----
        const char* phaseName;
        sr3physics::DriverInput input;
        if (frames < kStraightFrames) {
            phaseName = "straight";
            input = sr3physics::DriverInput{0.6, 0.0, 0.0};
        } else if (frames < kStraightFrames + kTurnFrames) {
            phaseName = "turn";
            input = sr3physics::DriverInput{0.4, 0.0, 0.4};
        } else {
            phaseName = "brake";
            input = sr3physics::DriverInput{0.0, 1.0, 0.0};
        }
        const double usedDt = sim.Step(kDt, input);

        // ---- Vehicle world transform tracks the REAL physics state, every
        // frame - NOT a hand-animated path (the whole point of this demo). ----
        sr3render::buildWorldMatrixSimHeading(static_cast<float>(sim.state_.posX), 0.0f,
                                    static_cast<float>(sim.state_.posZ), static_cast<float>(sim.state_.heading),
                                    1.0f, vehicleObj.world);

        // ---- Wheels (improvement 3, CHOSEN kinematic approximation - see
        // buildWheelDeltaTransformV()'s own doc comment): spin every
        // wheel part proportional to the sim's own real forward body speed
        // (rolling-without-slipping, omega = v / wheelRadius, using the SAME
        // tuning.wheelRadius the physics sim itself already uses - not a
        // separately invented constant); steer front wheel parts by the
        // sim's own real, already rate-limited steeringAngleCurrentRad.
        //
        // BUG FIX (2026-10-08 v3): only a DELTA transform is built per wheel
        // part here (pivoted about that part's own derived raw-vertex
        // centroid, wheelCentroid - computed once above, NOT
        // veh.parts()[i].transform) - there is no longer a per-part
        // "baseline" transform for non-wheel parts at all. See the
        // per-vertex loop immediately below: a non-wheel vertex's position
        // is left completely untouched (byte-identical to its own channel's
        // raw decode), fixing the shattered-body bug an earlier revision of
        // this function left unresolved. v4 (2026-10-08) applies this SAME
        // per-vertex rule to every channel's own vertices, not just channel
        // 0 - wheelDelta is keyed by rigidPartIndex, not by channel, so the
        // identical transform applies correctly regardless of which
        // channel a given wheel vertex happens to decode from. ----
        std::vector<std::array<float, 16>> wheelDelta(veh.parts().size());
        for (size_t i = 0; i < veh.parts().size(); ++i) {
            if (!partIsWheel[i]) continue;
            wheelRollAngleRad[i] += (sim.state_.velForwardBody / tuning.wheelRadius) * usedDt;
            const float steerAngle = partIsFrontWheel[i] ? static_cast<float>(sim.state_.steeringAngleCurrentRad) : 0.0f;
            buildWheelDeltaTransformV(wheelCentroid[i].data(), static_cast<float>(wheelRollAngleRad[i]), steerAngle,
                                      wheelDelta[i].data());
        }
        // v6 (THIS task, wheel-placement root-cause fix): non-wheel
        // vertices start from vehicleBasePositions, which is now the RAW
        // decode again (v5's "apply the part's own +0x00 transform" theory
        // was directly falsified - see that computation's own doc comment
        // for the measured proof). Wheel vertices are unaffected (still the
        // raw-baseline + wheelDelta path, byte-for-byte as v3/v4).
        bool uploadFailed = false;
        for (size_t ci = 0; ci < vehicleChannels.size(); ++ci) {
            auto& vcr = vehicleChannels[ci];
            std::vector<std::array<float, 3>> overridePositions(vcr.verts.size());
            for (size_t vi = 0; vi < vcr.verts.size(); ++vi) {
                const auto& v = vcr.verts[vi];
                if (v.rigidPartIndex < partIsWheel.size() && partIsWheel[v.rigidPartIndex]) {
                    transformPointRowVectorV(v.position.data(), wheelDelta[v.rigidPartIndex].data(),
                                             overridePositions[vi].data());
                } else {
                    overridePositions[vi] = vehicleBasePositions[ci][vi]; // real per-part transform (or raw, out-of-range case)
                }
            }
            std::string uploadErr;
            // Same per-channel rangeMask as the initial upload above (BUG
            // FIX v3, extended v4 - see that computation's own comment) -
            // every per-frame re-upload must stay masked to this channel's
            // own genuine ranges too, or the shattered-body bug would
            // simply come back on frame 1.
            const std::vector<bool>* maskPtr = vcr.rangeMask.empty() ? nullptr : &vcr.rangeMask;
            if (!vcr.renderer->upload(device.device(), mesh, vcr.channelIndex, 0, uploadErr, 0, &overridePositions,
                                      nullptr, maskPtr)) {
                std::fprintf(stderr, "vehicle re-upload (wheel pose, channel %zu) failed: %s\n", vcr.channelIndex,
                             uploadErr.c_str());
                uploadFailed = true;
                break;
            }
        }
        if (uploadFailed) break;

        // ---- Attached catalogue wheel mesh world transforms (improvement
        // 1/3, this task, §12.1/§12.3): steer (REAL confirmed formula,
        // reconstructed from spec's own half-angle quaternion convention
        // into this file's plain-rotation-angle convention - the -0.5
        // half-angle factor is exactly undone by doubling back to a full
        // angle, leaving fullAngle = -sign*scale*steerAngle), spin (the
        // SAME real wheelRollAngleRad accumulator the existing vertex-delta
        // path already uses, just reused here for a second, independently
        // posed mesh), and a CHOSEN suspension ease (see
        // primaryWheelSlots/suspensionSmoothedOffsetY's own doc comment
        // above). Computed every frame regardless of whether a catalogue
        // mesh is actually attached (cheap, and keeps the suspension ease
        // state correctly warmed even across a transient load failure).
        std::vector<std::array<float, 16>> wheelAttachWorld(primaryWheelSlots.size());
        for (size_t s = 0; s < primaryWheelSlots.size(); ++s) {
            const PrimaryWheelSlotV& slot = primaryWheelSlots[s];
            const int wk = slot.simWheelIdx;
            if (suspensionReferenceForce[wk] < 0.0) {
                suspensionReferenceForce[wk] = sim.state_.wheels[wk].normalForce;
            }
            const double targetOffsetY = (std::max)(
                -kSuspensionMaxTravelM,
                (std::min)(kSuspensionMaxTravelM, (sim.state_.wheels[wk].normalForce - suspensionReferenceForce[wk]) *
                                                       kSuspensionComplianceMPerN));
            const double easeAlpha =
                (std::max)(0.0, (std::min)(1.0, 0.05 * (usedDt / sr3physics::kSuspensionMaxMicroStepSec)));
            suspensionSmoothedOffsetY[wk] += (targetOffsetY - suspensionSmoothedOffsetY[wk]) * easeAlpha;

            const float fullSteerAngle = slot.isFront
                                             ? static_cast<float>(-slot.sign * kWheelVisualSteerScaleV *
                                                                   sim.state_.steeringAngleCurrentRad)
                                             : 0.0f;
            const float anchorWithSuspension[3] = {slot.anchor[0],
                                                   slot.anchor[1] + static_cast<float>(suspensionSmoothedOffsetY[wk]),
                                                   slot.anchor[2]};
            buildWheelAttachTransformV(catalogWheelPivot.data(), anchorWithSuspension,
                                       static_cast<float>(wheelRollAngleRad[slot.partIndex]), fullSteerAngle,
                                       catalogWheelScale, wheelAttachWorld[s].data());
            float composed[16];
            sr3render::multiplyMatrix4x4(wheelAttachWorld[s].data(), vehicleObj.world, composed);
            std::memcpy(wheelAttachWorld[s].data(), composed, sizeof(composed));
        }

        // ---- Simplified trailing camera, recomputed every frame from the
        // vehicle's CURRENT real transform (CHOSEN constants, see doc
        // comment above; the two REAL facts used are the row0=forward
        // identity this project's own buildWorldMatrix already establishes
        // - consistent with spec-player-runtime.md Sec4.10's row-identity
        // convention for the SAME real matrix shape - and back=-forward,
        // desired=pivot+distance*back, Sec4.7 step13 simplified). ----
        const float heading = static_cast<float>(sim.state_.heading);
        const float vehForward[3] = {std::cos(heading), 0.0f, std::sin(heading)};
        const float back[3] = {-vehForward[0], 0.0f, -vehForward[2]};
        const float pivot[3] = {static_cast<float>(sim.state_.posX), kPivotHeight, static_cast<float>(sim.state_.posZ)};
        const float eye[3] = {pivot[0] + kCamDistance * back[0], pivot[1] + kCamHeightAbovePivot,
                              pivot[2] + kCamDistance * back[2]};
        float camForward[3] = {pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2]};
        float flen = std::sqrt(camForward[0] * camForward[0] + camForward[1] * camForward[1] +
                               camForward[2] * camForward[2]);
        if (flen <= 0.0f) flen = 1.0f;
        for (float& c : camForward) c /= flen;
        const float worldUp[3] = {0.0f, 1.0f, 0.0f};
        float camRight[3] = {camForward[1] * worldUp[2] - camForward[2] * worldUp[1],
                             camForward[2] * worldUp[0] - camForward[0] * worldUp[2],
                             camForward[0] * worldUp[1] - camForward[1] * worldUp[0]};
        float rlen = std::sqrt(camRight[0] * camRight[0] + camRight[1] * camRight[1] + camRight[2] * camRight[2]);
        if (rlen <= 0.0f) rlen = 1.0f;
        for (float& c : camRight) c /= rlen;
        const float camUp[3] = {camRight[1] * camForward[2] - camRight[2] * camForward[1],
                                camRight[2] * camForward[0] - camRight[0] * camForward[2],
                                camRight[0] * camForward[1] - camRight[1] * camForward[0]};
        float viewProjection[16];
        buildViewProjectionFromCamera(eye, camRight, camUp, camForward, kCamFovY,
                                      static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height()),
                                      kCamNearZ, kCamFarZ, viewProjection);

        swapChain.bind(device.context());
        ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
        device.context()->OMSetRenderTargets(1, &rtv, vehicleChannels.front().renderer->depthView());
        swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
        vehicleChannels.front().renderer->clearDepth(device.context());

        // Real city scenery first (opaque, depth-tested), then the vehicle -
        // same ordering runCity() itself uses.
        for (auto& lc : cityLoadedCells) {
            for (auto& p : lc.props) {
                for (auto& r : p.renderers) r->drawTextured(device.context(), viewProjection, p.perMaterialSrv, p.world);
            }
        }

        // v4 (2026-10-08): one draw call PER CHANNEL, all composited into
        // the same depth-tested render target - the established multi-
        // channel pattern (MultiChannelMultiSetRender::draw() above does the
        // same single-loop composite). vehiclePerMaterialSrv is indexed by
        // materialId, shared across every channel's own draw ranges (the
        // same real per-material binding this file already resolved once,
        // not per-channel data), so every channel's drawTextured() call
        // reuses it unchanged.
        for (auto& vcr : vehicleChannels) {
            if (!vehiclePerMaterialSrv.empty()) {
                vcr.renderer->drawTextured(device.context(), viewProjection, vehiclePerMaterialSrv, vehicleObj.world);
            } else {
                vcr.renderer->draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker, nullptr,
                                   vehicleObj.world);
            }
        }

        // ---- Attached catalogue wheel mesh (improvement 1, this task,
        // §12.1) - one draw call per primary wheel slot, same shared
        // GPU buffer (catalogWheelRenderer), different `world` transform
        // per slot (wheelAttachWorld, computed above). FlatTint (CHOSEN -
        // see loadExternalizedWheelCatalogV()'s own doc comment for why no
        // real per-material texture is resolved here): a dark neutral
        // tire/rim grey, not a guessed brand colour.
        if (catalogWheelReady) {
            const float kTireTint[4] = {0.12f, 0.12f, 0.13f, 1.0f}; // CHOSEN - dark neutral tire/rim grey
            for (const auto& w : wheelAttachWorld) {
                catalogWheelRenderer->draw(device.context(), viewProjection, sr3render::MeshDrawMode::FlatTint,
                                           nullptr, w.data(), kTireTint);
            }
        }

        // Every CAPTURED frame: a real sim-state diagnostic line, same spirit
        // as walk_demo's own per-captured-frame diagnostic line.
        const bool wantedByList = !captureFrames.empty() &&
                                  std::find(captureFrames.begin(), captureFrames.end(), frames) != captureFrames.end();
        const bool wantedSingle = captureFrames.empty() && frames == captureFrame;
        if (!capturePath.empty() && (wantedByList || wantedSingle)) {
            std::printf("captured : frame %d, phase %-8s pos (%.2f %.2f) heading %.1f deg, "
                        "velForwardBody %.2f m/s, yawRate %.3f rad/s, steeringAngle %.1f deg\n",
                        frames, phaseName, sim.state_.posX, sim.state_.posZ,
                        sim.state_.heading * 180.0 / 3.14159265, sim.state_.velForwardBody, sim.state_.yawRate,
                        sim.state_.steeringAngleCurrentRad * 180.0 / 3.14159265);
            std::vector<uint8_t> pixels;
            const std::string path = captureFrames.empty() ? capturePath
                                                            : capturePath + "_frame" + std::to_string(frames) + ".png";
            if (swapChain.capture(device.context(), pixels, error) &&
                sr3render::writePng(path, swapChain.width(), swapChain.height(), pixels, error)) {
                std::printf("           -> %s\n", path.c_str());
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }

    std::printf("frames   : %d presented; final vehicle pos (%.2f %.2f) heading %.1f deg, "
                "velForwardBody %.2f m/s\n",
                frames, sim.state_.posX, sim.state_.posZ, sim.state_.heading * 180.0 / 3.14159265,
                sim.state_.velForwardBody);
    return 0;
}
