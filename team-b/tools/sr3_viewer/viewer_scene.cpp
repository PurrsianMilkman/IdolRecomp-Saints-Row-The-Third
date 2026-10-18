#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_common.h"
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
#include "sr3vehicle/cvtf.h"

using namespace sr3viewer;

namespace {

// Loads and uploads one character mesh's raw decoded channel 0 - no
// skinning. The scene command demonstrates PLACEMENT; an unposed mesh is
// the exact same, already-confirmed-correct vertex data the `mesh`
// command renders. Full-skeleton animated playback is deliberately NOT
// used here - HANDOFF §9.56.2/§9.56.4 track a real, separate, user-
// deferred rendering artifact for that path (§9.61), and this command has
// no reason to reproduce it.
bool loadCharacterObject(const vpp::Container& container, const std::string& cmeshName,
                         ID3D11Device* device, PlacedObject& out, std::string& error) {
    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> cb, gb;
    if (!findEntry(container, cmeshName, cb, gb, gmeshName) || gb.empty()) {
        error = "could not find '" + cmeshName + "' (+ its paired g-file)";
        return false;
    }
    sr3geometry::MaterialBlock material =
        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) {
        error = cmeshName + ": no Mesh sub-block";
        return false;
    }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
        vpp::ByteView(cb.data(), cb.size()), geometry.meshSubBlockOffset(),
        vpp::ByteView(gb.data(), gb.size()));

    out.renderer = std::make_unique<sr3render::MeshRenderer>();
    if (!out.renderer->initialise(device, error)) return false;
    if (!out.renderer->upload(device, mesh, 0, 0, error)) return false;
    out.label = cmeshName;
    const uint32_t vertCount = mesh.channels().empty() ? 0u : mesh.channels()[0].elementCount;
    std::printf("object   : %-28s %u verts, bounds %.3f x %.3f x %.3f\n", cmeshName.c_str(),
                vertCount, static_cast<double>(out.renderer->boundsMax()[0] - out.renderer->boundsMin()[0]),
                static_cast<double>(out.renderer->boundsMax()[1] - out.renderer->boundsMin()[1]),
                static_cast<double>(out.renderer->boundsMax()[2] - out.renderer->boundsMin()[2]));
    return true;
}


// Loads and uploads one vehicle's embedded Mesh sub-block. sr3vehicle
// hands the assembly's mesh region straight to the same sr3geometry/
// sr3mesh readers a character mesh goes through (spec-vehicle-geometry.md
// §1/§7: ".ccar_pc IS NOT A NEW MESH FORMAT") - GeometryBlock::parseAt(),
// not parse(), because a vehicle carries no material block of its own
// (spec §7, 393/393 - see runMesh/validate_vehicle.cpp for the same
// pattern already in use elsewhere in this project).
bool loadVehicleObject(const vpp::Container& container, const std::string& cmeshName,
                       ID3D11Device* device, PlacedObject& out, std::string& error) {
    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> cb, gb;
    if (!findEntry(container, cmeshName, cb, gb, gmeshName) || gb.empty()) {
        error = "could not find '" + cmeshName + "' (+ its paired g-file)";
        return false;
    }
    sr3vehicle::Vehicle veh;
    try {
        veh = sr3vehicle::Vehicle::parse(vpp::ByteView(cb.data(), cb.size()));
    } catch (const std::exception& ex) {
        error = cmeshName + ": " + ex.what();
        return false;
    }
    sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parseAt(
        vpp::ByteView(cb.data(), cb.size()), veh.meshRegionOffset());
    if (!geometry.hasMeshSubBlock()) {
        error = cmeshName + ": no Mesh sub-block";
        return false;
    }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
        vpp::ByteView(cb.data(), cb.size()), geometry.meshSubBlockOffset(),
        vpp::ByteView(gb.data(), gb.size()));

    out.renderer = std::make_unique<sr3render::MeshRenderer>();
    if (!out.renderer->initialise(device, error)) return false;
    if (!out.renderer->upload(device, mesh, 0, 0, error)) return false;
    out.label = cmeshName;
    if (mesh.drawGroupsLocated()) {
        const size_t rangesInGroup0 = mesh.drawGroups().empty() ? 0 : mesh.drawGroups()[0].size();
        std::printf("           groups: located, %zu range(s) in group 0\n", rangesInGroup0);
    } else {
        std::printf("           groups: NOT located - falling back to one continuous strip "
                    "(produces bridge triangles - see runMesh's own note on this)\n");
    }
    std::printf("object   : %-28s %zu part(s), bounds %.3f x %.3f x %.3f\n", cmeshName.c_str(),
                veh.parts().size(),
                static_cast<double>(out.renderer->boundsMax()[0] - out.renderer->boundsMin()[0]),
                static_cast<double>(out.renderer->boundsMax()[1] - out.renderer->boundsMin()[1]),
                static_cast<double>(out.renderer->boundsMax()[2] - out.renderer->boundsMin()[2]));
    return true;
}

} // namespace



int runScene(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr,
                     "usage: sr3_viewer scene <characters.vpp_pc> <charA.ccmesh_pc> "
                     "<charB.ccmesh_pc>\n"
                     "                        <vehicles.vpp_pc> <vehicle.ccar_pc>\n"
                     "                        [--mode uv|checker|normal|material]\n"
                     "                        [--size W H] [--frames N] [--capture <out.png>]\n"
                     "                        [--capture-frame N] [--eye X Y Z] [--yaw D] "
                     "[--pitch D]\n"
                     "                        [--speed U] [--visible]\n"
                     "       Multi-object scene + free-fly camera. Places charA/charB/the\n"
                     "       vehicle at fixed, separated world positions and renders them\n"
                     "       together through one shared free camera (not the single-object\n"
                     "       orbit `mesh`/`pose`/`animpose` use).\n"
                     "       Fly controls, need an interactive window (GetAsyncKeyState polled\n"
                     "       once per frame): W/A/S/D move, Q/E down/up, arrow keys look,\n"
                     "       Shift = faster, Escape closes the window.\n"
                     "       --capture writes exactly what is on screen at --capture-frame\n"
                     "       (default frame 0) - verifiable with no human watching, same\n"
                     "       convention `view`'s --capture already uses.\n");
        return 1;
    }
    const std::string charArchivePath = argv[2];
    const std::string charAName = argv[3];
    const std::string charBName = argv[4];
    const std::string vehArchivePath = argv[5];
    const std::string vehicleName = argv[6];

    sr3render::MeshDrawMode mode = sr3render::MeshDrawMode::Checker;
    uint32_t winWidth = 1280, winHeight = 720;
    int maxFrames = -1;
    int captureFrame = 0;
    std::string capturePath;
    float eye[3] = {0.0f, 1.6f, -7.5f};
    float yaw = 0.0f, pitch = -0.12f;
    float moveSpeed = 3.0f; // world units/second at normal speed
    bool visible = false; // off by default - see sr3render/window.h's own doc comment

    for (int i = 7; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--size" && i + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++i]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else if (arg == "--capture-frame" && i + 1 < argc) {
            captureFrame = std::atoi(argv[++i]);
        } else if (arg == "--eye" && i + 3 < argc) {
            eye[0] = static_cast<float>(std::atof(argv[++i]));
            eye[1] = static_cast<float>(std::atof(argv[++i]));
            eye[2] = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--yaw" && i + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && i + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--speed" && i + 1 < argc) {
            moveSpeed = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--visible") {
            visible = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string m = argv[++i];
            if (m == "uv") mode = sr3render::MeshDrawMode::UvAsColour;
            else if (m == "checker") mode = sr3render::MeshDrawMode::Checker;
            else if (m == "normal") mode = sr3render::MeshDrawMode::NormalAsColour;
            else if (m == "material") mode = sr3render::MeshDrawMode::MaterialIdAsColour;
            else {
                std::fprintf(stderr, "unknown --mode '%s' (uv, checker, normal, material)\n",
                             m.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::vector<uint8_t> charArchive = readFile(charArchivePath);
    if (charArchive.empty()) {
        std::fprintf(stderr, "could not read %s\n", charArchivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> vehArchive = readFile(vehArchivePath);
    if (vehArchive.empty()) {
        std::fprintf(stderr, "could not read %s\n", vehArchivePath.c_str());
        return 1;
    }

    std::string error;
    sr3render::Window window;
    if (!window.create("SR3 Viewer - scene", winWidth, winHeight, error, visible)) {
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

    // ---- Scene composition (deliverable 2): a plain list of placed
    // objects - see this function's header comment for why nothing more
    // elaborate than PlacedObject/std::vector<PlacedObject> is here.
    std::vector<PlacedObject> scene(3);
    try {
        vpp::Container charContainer(vpp::ByteView(charArchive.data(), charArchive.size()));
        if (!loadCharacterObject(charContainer, charAName, device.device(), scene[0], error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (!loadCharacterObject(charContainer, charBName, device.device(), scene[1], error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "character archive error: %s\n", ex.what());
        return 1;
    }
    try {
        vpp::Container vehContainer(vpp::ByteView(vehArchive.data(), vehArchive.size()));
        if (!loadVehicleObject(vehContainer, vehicleName, device.device(), scene[2], error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "vehicle archive error: %s\n", ex.what());
        return 1;
    }

    // Fixed world placement: the two characters side by side, the vehicle
    // set back and centred between them - close enough to share one frame,
    // far enough apart that "these are separate objects at separate
    // positions" is unambiguous in a screenshot. charB's 180-degree yaw
    // exists purely to show buildWorldMatrix's rotation argument is real
    // and used, not just plumbed through unused.
    sr3render::buildWorldMatrix(-1.3f, 0.0f, 0.0f, 0.0f, 1.0f, scene[0].world);
    sr3render::buildWorldMatrix(1.3f, 0.0f, 0.0f, 3.14159265f, 1.0f, scene[1].world);
    sr3render::buildWorldMatrix(0.0f, 0.0f, 5.0f, 0.0f, 1.0f, scene[2].world);

    // One shared depth buffer for the whole scene, sized to the window,
    // owned by scene[0]'s renderer - MeshRenderer::draw() never reads a
    // renderer's OWN depth VIEW (only a depth STATE, i.e. test/write mode,
    // which every renderer sets identically), the view is bound externally
    // by whoever calls OMSetRenderTargets, exactly as runMesh/runPose/
    // runAnimPose already do via device.bindRenderTargetWithDepth(). Any
    // renderer in the scene could equally own it; scene[0] is arbitrary.
    if (!scene[0].renderer->createDepth(device.device(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
        return 1;
    }
    uint32_t depthWidth = winWidth, depthHeight = winHeight;

    std::printf("scene    : %zu object(s) - %s, %s (yaw 180), %s\n", scene.size(),
                scene[0].label.c_str(), scene[1].label.c_str(), scene[2].label.c_str());
    std::printf("camera   : free-fly, start eye (%.2f %.2f %.2f) yaw %.1f pitch %.1f  "
                "(W/A/S/D move, Q/E down/up, arrows look, Shift=fast, Esc=quit)\n",
                static_cast<double>(eye[0]), static_cast<double>(eye[1]),
                static_cast<double>(eye[2]), static_cast<double>(yaw * 180.0f / 3.14159265f),
                static_cast<double>(pitch * 180.0f / 3.14159265f));

    LARGE_INTEGER freq{}, prevTime{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prevTime);

    int frames = 0;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) {
            std::fprintf(stderr, "swap chain resize failed: %s\n", error.c_str());
            break;
        }
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (scene[0].renderer->createDepth(device.device(), swapChain.width(),
                                               swapChain.height(), error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(static_cast<double>(now.QuadPart - prevTime.QuadPart) /
                                      static_cast<double>(freq.QuadPart));
        prevTime = now;
        if (dt > 0.25f || dt < 0.0f) dt = 0.0f; // clamp a paused/resized window's huge first delta

        // ---- Free-fly camera update (deliverable 3): polled input, not
        // event-driven - see this function's header comment for why
        // GetAsyncKeyState rather than a Window WM_KEYDOWN/KEYUP hook.
        const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const float speed = moveSpeed * (fast ? 3.0f : 1.0f) * dt;
        const float lookSpeed = 1.6f * dt; // radians/second
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) yaw -= lookSpeed;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) yaw += lookSpeed;
        if (GetAsyncKeyState(VK_UP) & 0x8000) pitch += lookSpeed;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) pitch -= lookSpeed;
        const float pitchLimit = 1.5f;
        if (pitch > pitchLimit) pitch = pitchLimit;
        if (pitch < -pitchLimit) pitch = -pitchLimit;

        float forward[3] = {std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                            std::cos(pitch) * std::cos(yaw)};
        // Same cross(forward, worldUp) buildFreeViewProjection() itself
        // uses for its own `right` (worldUp = (0,1,0), which is why the y
        // component drops out): right = (-forward.z, 0, forward.x),
        // normalised. Getting this from a DIFFERENT formula than the one
        // the view matrix actually uses is exactly how a strafe key ends
        // up moving opposite to what the camera calls "right" - caught by
        // checking this against buildFreeViewProjection's own derivation
        // rather than trusting a second, independently-written formula to
        // agree with it by construction.
        float right[3] = {-forward[2], 0.0f, forward[0]};
        float rightLen = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        if (rightLen > 0.0f) { right[0] /= rightLen; right[2] /= rightLen; }
        if (GetAsyncKeyState('W') & 0x8000)
            for (int c = 0; c < 3; ++c) eye[c] += forward[c] * speed;
        if (GetAsyncKeyState('S') & 0x8000)
            for (int c = 0; c < 3; ++c) eye[c] -= forward[c] * speed;
        if (GetAsyncKeyState('D') & 0x8000)
            for (int c = 0; c < 3; ++c) eye[c] += right[c] * speed;
        if (GetAsyncKeyState('A') & 0x8000)
            for (int c = 0; c < 3; ++c) eye[c] -= right[c] * speed;
        if (GetAsyncKeyState('E') & 0x8000) eye[1] += speed;
        if (GetAsyncKeyState('Q') & 0x8000) eye[1] -= speed;

        float viewProjection[16];
        sr3render::buildFreeViewProjection(
            eye, yaw, pitch,
            static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height()), 0.05f,
            500.0f, viewProjection);

        swapChain.bind(device.context());
        ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
        device.context()->OMSetRenderTargets(1, &rtv, scene[0].renderer->depthView());
        swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
        scene[0].renderer->clearDepth(device.context());

        for (auto& obj : scene) {
            obj.renderer->draw(device.context(), viewProjection, mode, nullptr, obj.world);
        }

        if (!capturePath.empty() && frames == captureFrame) {
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error)) {
                if (sr3render::writePng(capturePath, swapChain.width(), swapChain.height(), pixels,
                                        error)) {
                    std::printf("captured : %s (frame %d)\n", capturePath.c_str(), frames);
                } else {
                    std::fprintf(stderr, "capture png failed: %s\n", error.c_str());
                }
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }

    std::printf("frames   : %d presented\n", frames);
    return 0;
}
