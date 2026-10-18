// SR3 engine viewer / rendering milestone driver.
//
// Usage:
//   sr3_viewer clear <out.png> [width height]
//       Milestone 1: bring up a D3D11 device, clear an offscreen target to
//       a known colour, read it back and write it out. Proves device
//       creation, render-target binding and CPU readback end to end, and
//       produces an image to actually look at rather than a success code.
//
//   sr3_viewer texture <archive.vpp_pc> <name.cpeg_pc> <out.png> [index]
//       Milestone 2: pull a real texture pair straight out of a shipped
//       archive, decode it with the validated sr3texture reader, upload it
//       to the GPU and draw it. Nothing is extracted to disk - the whole
//       path runs in memory, archive to picture.
//
//   sr3_viewer list-textures <archive.vpp_pc> [limit]
//       Enumerate .cpeg_pc/.cvbm_pc entries and their textures, so a good
//       render target can be picked without guessing at names.
//
// Everything renders headless by design - see sr3render/device.h.
//
// Every command below that opens an interactive sr3render::Window (view,
// scene, vehicle, zone, zone-composite, clmesh, clmesh-tile, clmesh-batch,
// city, walk) creates it HIDDEN by default (focus-stealing fix, 2026-10-08) -
// WARP/offscreen rendering into the swap chain needs a valid HWND, not an
// on-screen one, and goldens/ctest already force WARP via SR3_FORCE_WARP=1.
// Pass --visible on any of those commands to opt back into the old
// on-screen, activating window, for interactive/manual use. See
// sr3render/window.h's own doc comment on Window::create()'s `visible`
// parameter for the full reasoning.

#include "viewer_platform.h"
#include "viewer_modes.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_signals.h"
#include "sr3airuntime/ped_crossing.h"
#include "sr3airuntime/ped_navigator.h"
#include "sr3airuntime/traffic_lane_follower.h"
#include "sr3airuntime/road_graph_placement.h"
#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"
#include "sr3airuntime/freeroam_population.h"
#include "sr3airuntime/police_roadblock_spawn.h"
#include "sr3airuntime/near_field_ped_population.h"
#include "sr3airuntime/npc_ai_runtime.h"
#include "sr3luahost/engine_state.h"
#include "sr3playeranim/bridge.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"


int main(int argc, char** argv) {
    if (std::getenv("SR3_UNBUFFERED")) std::setvbuf(stdout, nullptr, _IONBF, 0);  // DIAGNOSTIC ONLY (opt-in)
    if (argc < 2) {
        std::fprintf(stderr, "usage: sr3_viewer <command> [args]\n");
        std::fprintf(stderr, "  clear <out.png> [width height]\n");
        std::fprintf(stderr, "  list-textures <archive.vpp_pc> [limit]\n");
        std::fprintf(stderr, "  texture <archive.vpp_pc> <name.cpeg_pc> <out.png> [index]\n");
        std::fprintf(stderr, "  view <archive.vpp_pc> <name.cpeg_pc> [index] "
                             "[--frames N] [--capture <out.png>] [--size W H]\n");
        std::fprintf(stderr, "  mesh <archive.vpp_pc> <name.ccmesh_pc> <out.png> "
                             "[--mode uv|checker|normal|textured] [--yaw D] [--pitch D]\n");
        std::fprintf(stderr, "  pose <archive.vpp_pc> <name.ccmesh_pc> <name.rig_pc> "
                             "<out_prefix> [--mode uv|checker|normal|material] [--yaw D] "
                             "[--pitch D] [--angle D]\n"
                             "       (stage 1b: renders <out_prefix>_bind.png and "
                             "<out_prefix>_posed.png via sr3rig/pose.h CPU skinning)\n");
        std::fprintf(stderr,
                     "  animpose <characters.vpp_pc> <name.ccmesh_pc> <name.rig_pc> "
                     "<anim_archive.vpp_pc> <clip.anim_pc> <out_prefix>\n"
                     "           [--mode uv|checker|normal|material] [--yaw D] [--pitch D] "
                     "[--times f0,f1,...]\n"
                     "       (stage 2: samples the clip's real decoded rotation/translation "
                     "keys over time via\n"
                     "        sr3anim/sample.h + sr3rig/animated_pose.h, renders one PNG per "
                     "--times fraction)\n");
        std::fprintf(stderr,
                     "  scene <characters.vpp_pc> <charA.ccmesh_pc> <charB.ccmesh_pc> "
                     "<vehicles.vpp_pc> <vehicle.ccar_pc>\n"
                     "        [--mode uv|checker|normal|material] [--size W H] [--frames N] "
                     "[--capture <out.png>]\n"
                     "        [--capture-frame N] [--eye X Y Z] [--yaw D] [--pitch D] "
                     "[--speed U]\n"
                     "       (multi-object scene composition + free-fly camera: places two "
                     "character meshes\n"
                     "        and a vehicle mesh at separate world positions via "
                     "MeshRenderer::draw()'s new\n"
                     "        `world` parameter, and renders them together through "
                     "buildFreeViewProjection()'s\n"
                     "        keyboard-driven fly camera instead of the single-object orbit "
                     "mesh/pose/animpose use)\n");
        std::fprintf(stderr,
                     "  vehicle <archive.vpp_pc> <name.ccar_pc> <shaders_archive.vpp_pc>\n"
                     "           [--group N] [--size W H] [--frames N] [--capture <out.png>]\n"
                     "           [--capture-frame N] [--yaw D] [--pitch D] [--distance F]\n"
                     "       (real per-draw-range shader render: each draw range in the chosen\n"
                     "        LOD group resolves and draws with its own real compiled shader,\n"
                     "        matching tools/prototype_real_shader_draw_multishader.cpp's own\n"
                     "        standalone mechanism, presented as an ordinary windowed target)\n");
        std::fprintf(stderr,
                     "  zone <archive.vpp_pc> <name.czn_pc>\n"
                     "        [--block N] [--channel N] [--group N] [--size W H] [--frames N]\n"
                     "        [--capture <out.png>] [--capture-frame N] [--yaw D] [--pitch D]\n"
                     "        [--distance F]\n"
                     "       (real zone-tile geometry render: vertex layout code 24, POSITION+\n"
                     "        NORMAL only - see mesh_block.cpp's layoutInfoFor() case 24 comment\n"
                     "        for why the format's own +16 byte slot is never read)\n");
        std::fprintf(stderr,
                     "  zone-composite <archive.vpp_pc> <name.czn_pc>\n"
                     "        [--size W H] [--frames N] [--capture <out.png>]\n"
                     "        [--capture-frame N] [--yaw D] [--pitch D] [--distance F]\n"
                     "       (INVESTIGATION-ONLY, not a golden-baseline command: draws layout\n"
                     "        code 24 - flat red - and layout code 0 - MaterialIdAsColour -\n"
                     "        together in ONE frame from the SAME real Mesh sub-block, to test\n"
                     "        whether they are the same building's shell + facade or unrelated\n"
                     "        geometry - see runZoneTileComposite()'s own doc comment)\n");
        std::fprintf(stderr,
                     "  clmesh <archive.vpp_pc> <name.clmesh_pc>\n"
                     "        [--rendergroup N] [--size W H] [--frames N] [--capture <out.png>]\n"
                     "        [--capture-frame N] [--yaw D] [--pitch D] [--distance F]\n"
                     "       (real `.clmesh_pc`/`.glmesh_pc` static-prop render, REAL per-material\n"
                     "        textures bound via LevelMesh::materialTextureBinding() - plain\n"
                     "        diffuse-textured fallback shader, real shaderHash evidence reported\n"
                     "        but not wired up to a compiled shader - see runClmesh()'s own doc\n"
                     "        comment for the full honest scope note)\n");
        std::fprintf(stderr,
                     "  clmesh-tile <archive.vpp_pc> <name.str2_pc>\n"
                     "        [--size W H] [--frames N] [--capture <out.png>] [--capture-frame N]\n"
                     "        [--yaw D] [--pitch D] [--distance F] [--cols N]\n"
                     "       (renders EVERY real .clmesh_pc/.glmesh_pc pair inside one hN fine-cell\n"
                     "        container, e.g. 1018h0.str2_pc, together with real per-material\n"
                     "        textures - NOT real world placement, a simple local-origin grid only;\n"
                     "        real per-instance placement was investigated and refuted for this\n"
                     "        format - see runClmeshTile()'s own doc comment for the full honest\n"
                     "        result and evidence)\n");
        std::fprintf(stderr,
                     "  clmesh-batch <archive.vpp_pc> <name.str2_pc> <out_dir>\n"
                     "        [--rendergroup N] [--size W H] [--yaw D] [--pitch D] [--distance F]\n"
                     "        [--exclude <substring>]...\n"
                     "       (renders EVERY real .clmesh_pc/.glmesh_pc pair inside one hN fine-cell\n"
                     "        container to its OWN individually-readable PNG under <out_dir>, each\n"
                     "        prop's own camera auto-fit to ITS OWN real bounding box - unlike\n"
                     "        clmesh-tile's single shared-scale grid, a small prop and a large\n"
                     "        building both render clearly - see runClmeshBatch()'s own doc comment\n"
                     "        for the full reasoning)\n");
        std::fprintf(stderr,
                     "  city <sr3_city_0.vpp_pc> [<sr3_city_1.vpp_pc> ...] [--list] [--lattice-check]\n"
                     "        [--eye X Y Z] [--yaw D] [--pitch D] [--speed U] [--fly-to X Z] [--no-input]\n"
                     "        [--frames N] [--capture <out.png>] [--capture-frame N]\n"
                     "        [--capture-at N <out.png>]... [--tint] [--no-textures] [--no-outlines]\n"
                     "       (multi-cell STREAMING viewer: loads/evicts the real hN fine cells around\n"
                     "        the fly camera by the level-4 box rule, spec-world-streaming.md\n"
                     "        Sec10.4/Sec11; cells at their REAL Sec10.6(g) lattice positions, props\n"
                     "        inside a cell on a FAKE local grid - see runCity()'s own doc comment)\n");
        std::fprintf(stderr,
                     "  walk <characters.vpp_pc> <misc_tables.vpp_pc> <sr3_city_0.vpp_pc> [...]\n"
                     "        [--eye X Y Z] [--yaw D] [--pitch D] [--no-input] [--frames N]\n"
                     "        [--capture <out.png>] [--capture-frame N] [--size W H] [--tint]\n"
                     "        [--no-textures] [--no-outlines] [--speed U] [--far F]\n"
                     "       (player<->animation walking demo: brad driven by the real\n"
                     "        sr3playeranim::PlayerAnimBridge/'player state machine' node graph on the\n"
                     "        real city_corner ground, with the real follow camera's target kept in\n"
                     "        sync every frame - rigid-body render, no real per-clip skinning exists\n"
                     "        in this project; see runWalkDemo()'s own doc comment for the full\n"
                     "        REAL/CHOSEN/judgement-call breakdown)\n");
        std::fprintf(stderr,
                     "  playtest <characters.vpp_pc> <misc_tables.vpp_pc> <sr3_city_0.vpp_pc> [...]\n"
                     "       [--headless] [--no-input] [--frames N] [--capture <out.png>]\n"
                     "       [--capture-frame N] [--capture-frames F0,F1,...] [--size W H]\n"
                     "       [--spawn X Z] [--far F] [--anim-archive <path>] [--mouse-sensitivity F]\n"
                     "       [--script-forward-frames N] [--script-turn-frames N]\n"
                     "       [--hud <registration_list.txt> <misc.vpp_pc>]\n"
                     "       [--no-vehicle] [--vehicle-archive <path>] [--vehicle-name <name.ccar_pc>]\n"
                     "       [--wheels-archive <path>] [--vehicle-spawn X Z] [--script-vehicle]\n"
                     "       (playtest v0 - the first genuinely INTERACTIVE build: a real visible\n"
                     "        window, real WASD+mouse-look input driving the real player<->animation\n"
                     "        <->camera<->collision pipeline live, instead of a scripted/deterministic\n"
                     "        capture. --hud wires the real health/ammo/money/notoriety + objective-\n"
                     "        text overlay, PLUS a live top-right corner minimap (v0.3 polish task), in,\n"
                     "        OFF by default (spec-hud-runtime.md). One real vehicle is parked near\n"
                     "        spawn and driveable - walk up, press E to enter/exit (ON by default,\n"
                     "        --no-vehicle opts out). See runPlaytest()'s own doc comment for the full\n"
                     "        REAL/CHOSEN/SCOPE breakdown - keyboard+mouse only, no ambient-population\n"
                     "        in v0.)\n");
        std::fprintf(stderr,
                     "  drive_demo [<archive.vpp_pc> <name.ccar_pc>] [--visible] [--size W H]\n"
                     "             [--frames N] [--capture <out.png>] [--capture-frame N]\n"
                     "             [--capture-frames F0,F1,...] [--city <sr3_city_0.vpp_pc>] [--no-city]\n"
                     "       (vehicle physics demo, the vehicle twin of `walk`: drives a real vehicle\n"
                     "        mesh, real-paint-coloured (per-material Base_Paint_Color, not a texture)\n"
                     "        with spinning/steering wheels (kinematic approximation), through a SCRIPTED\n"
                     "        straight/turn/brake sequence on the real city_corner scenery, using the real\n"
                     "        sr3physics::VehicleSubstepSimulator and a simplified trailing camera\n"
                     "        (spec-player-runtime.md Sec4.7 step13/Sec4.10 row-identities only, no\n"
                     "        submode/blend/state-machine) - defaults to the real dlc1.vpp_pc /\n"
                     "        car_4dr_genki_0.ccar_pc / sr3_city_0.vpp_pc trio if omitted; see\n"
                     "        runDriveDemo()'s own doc comment for the full REAL/CHOSEN breakdown)\n");
        std::fprintf(stderr,
                     "  traffic_lane_demo [--ticks N]\n"
                     "       (traffic-AI lane-following demo, headless: drives one ambient vehicle along a\n"
                     "        real-shape (synthetic-DATA) Traffic 0x2236 graph via sr3airuntime::\n"
                     "        TrafficLaneGraph/StepLaneFollow, feeding the real\n"
                     "        sr3physics::VehicleSubstepSimulator through its one real DriverInput write\n"
                     "        surface; prints a per-tick stdout position trace - see runTrafficLaneDemo()'s\n"
                     "        own doc comment for the full REAL/SYNTHETIC-DATA/CHOSEN breakdown)\n");
        std::fprintf(stderr,
                     "  vint-doc <doc_archive.vpp_pc> <name.vint_doc> <tables_archive.vpp_pc> <out.png>\n"
                     "        --textures <archive.vpp_pc>... [--tables <overlay.vpp_pc>]... [--size W H]\n"
                     "        [--doc-size W H] [--fit height|width]\n"
                     "        [--srgb-colour on|off] [--srgb-texture on|off] [--fonts <archive.vpp_pc>]...\n"
                     "        [--text-placeholder | --no-text-placeholder]\n"
                     "       (Vint UI render, spec-vint-render.md Sec12-Sec15: real `bitmap` quads from real sheet\n"
                     "        sub-images, real `text` glyphs from the shipped .vf3_pc fonts, `group` draws nothing\n"
                     "        - see runVintDoc()'s own doc comment)\n");
        std::fprintf(stderr,
                     "  hud-live-demo <registration_list.txt> <out_prefix>\n"
                     "        --textures <archive.vpp_pc> [--textures ...] [--fonts <archive.vpp_pc>]...\n"
                     "       (spec-hud-runtime.md: focused health/ammo/money/notoriety overlay driven by the\n"
                     "        REAL get_current_hit_points/set_current_hit_points/cash_add/inv_weapon_add_\n"
                     "        temporary/game_hud_update_inventory/notoriety_set/on_notoriety_event natives\n"
                     "        through a real Host/EngineState, rendered via this project's own real VINT text\n"
                     "        pipeline - see runHudLiveDemo()'s own doc comment)\n");
        std::fprintf(stderr,
                     "  minimap-demo <out_prefix> --fonts <archive.vpp_pc> [--fonts ...]\n"
                     "        [--textures <archive.vpp_pc>]...\n"
                     "       (spec-hud-runtime.md Sec6/Sec6.4: corner-minimap live background transform -\n"
                     "        map-centre tracks the player/vehicle, rotation gated by the static-minimap\n"
                     "        option, player marker fixed at the minimap's own centre - see\n"
                     "        runMinimapDemo()'s own doc comment)\n");
        return 1;
    }
    const std::string command = argv[1];
    try {
        if (command == "clear") return runClear(argc, argv);
        if (command == "list-textures") return runListTextures(argc, argv);
        if (command == "texture") return runTexture(argc, argv);
        if (command == "view") return runView(argc, argv);
        if (command == "mesh") return runMesh(argc, argv);
        if (command == "probe-matlib") return runProbeMatlib(argc, argv);
        if (command == "pose") return runPose(argc, argv);
        if (command == "animpose") return runAnimPose(argc, argv);
        if (command == "scene") return runScene(argc, argv);
        if (command == "vehicle") return runVehicle(argc, argv);
        if (command == "zone") return runZoneTile(argc, argv);
        if (command == "zone-composite") return runZoneTileComposite(argc, argv);
        if (command == "clmesh") return runClmesh(argc, argv);
        if (command == "clmesh-tile") return runClmeshTile(argc, argv);
        if (command == "clmesh-batch") return runClmeshBatch(argc, argv);
        if (command == "city") return runCity(argc, argv);
        if (command == "walk") return runWalkDemo(argc, argv);
        if (command == "playtest") return runPlaytest(argc, argv);
        if (command == "drive_demo") return runDriveDemo(argc, argv);
        if (command == "traffic_lane_demo") return runTrafficLaneDemo(argc, argv);
        if (command == "vint-doc") return runVintDoc(argc, argv);
        if (command == "hud-live-demo") return runHudLiveDemo(argc, argv);
        if (command == "minimap-demo") return runMinimapDemo(argc, argv);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }

    std::fprintf(stderr, "unknown command: %s\n", command.c_str());
    return 1;
}
